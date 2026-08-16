#include "aas_sound.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "shared/q_platform.h"

#include "aas_local.h"
#include "aas_map.h"
#include "botlib/common/l_assets.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/common/l_struct.h"
#include "botlib/precomp/l_precomp.h"
#include "botlib/precomp/l_script.h"

typedef struct aas_sound_state_s
{
    aas_soundinfo_t *infos;
    size_t info_count;
    size_t info_capacity;

    char **asset_names;
	aas_soundinfo_t **asset_info_by_index;
    size_t asset_count;

    aas_sound_event_t *sound_events;
    size_t sound_event_count;
    size_t sound_scheduled_count;
    size_t sound_event_capacity;
	int sound_active_head;
	int sound_active_tail;
	int sound_scheduled_head;
	int sound_scheduled_tail;
	int sound_free_head;

    aas_pointlight_event_t *pointlight_events;
    size_t pointlight_event_count;
    size_t pointlight_event_capacity;
	int pointlight_active_head;
	int pointlight_free_head;

    aas_sound_event_summary_t *sound_summaries;
    size_t sound_summary_count;
    size_t sound_summary_capacity;
    bool sound_summaries_dirty;

    aas_pointlight_event_summary_t *pointlight_summaries;
    size_t pointlight_summary_count;
    size_t pointlight_summary_capacity;
    bool pointlight_summaries_dirty;

    float frame_time;
    float previous_frame_time;
    bool frame_time_initialised;
    bool initialised;
} aas_sound_state_t;

#define AAS_POINTLIGHT_NULL_INDEX (-1)
#define AAS_SOUND_NULL_INDEX (-1)
#define AAS_SOUND_INFO_OFS(field) ((int)offsetof(aas_soundinfo_t, field))

/*
 * Retail fielddef table at .data 0x1005c070, six 0x1c-byte records of
 * {name, offset, type, maxarray, floatmin, floatmax, substruct}. Only volume
 * (0x1005c08c) and duration (0x1005c0a8) carry type 0x0203, i.e.
 * FT_FLOAT | FT_BOUNDED, with floatmax 0x42a00000 (80.0f) and 0x41200000
 * (10.0f). recognition (0x1005c0e0) stores floatmax 1.0f but its type word is
 * plain 0x0003, so it is deliberately left unbounded here.
 */
static const fielddef_t g_aas_soundinfo_fields[] = {
	{"name", AAS_SOUND_INFO_OFS(name), FT_STRING, 0, 0.0f, 0.0f, NULL},
	{"volume", AAS_SOUND_INFO_OFS(volume), FT_FLOAT | FT_BOUNDED, 0, 0.0f,
		80.0f, NULL},
	{"duration", AAS_SOUND_INFO_OFS(duration), FT_FLOAT | FT_BOUNDED, 0, 0.0f,
		10.0f, NULL},
	{"type", AAS_SOUND_INFO_OFS(type), FT_INT, 0, 0.0f, 0.0f, NULL},
	{"recognition", AAS_SOUND_INFO_OFS(recognition), FT_FLOAT, 0, 0.0f, 1.0f,
		NULL},
	{"string", AAS_SOUND_INFO_OFS(string), FT_STRING, 0, 0.0f, 0.0f, NULL},
	{NULL, 0, 0, 0, 0.0f, 0.0f, NULL},
};

static const structdef_t g_aas_soundinfo_struct = {
	(int)sizeof(aas_soundinfo_t),
	g_aas_soundinfo_fields,
};

static aas_sound_state_t g_aas_sound_state;

static bool AAS_Sound_PushInfo(const aas_soundinfo_t *info);

/*
=============
AAS_Sound_InitInfoDefaults

Zero one raw 0xb0-byte soundinfo record before ReadStructure fills it.
=============
*/
static void AAS_Sound_InitInfoDefaults(aas_soundinfo_t *info)
{
	if (info == NULL)
	{
		return;
	}

	/*
	 * Retail clears the whole record and defines no field defaults:
	 * 0x1001c904 __memfill_u32(record, 0, 0x2c) == 0xb0 zero bytes. The
	 * 80.0f/10.0f/1.0f values are the fielddef floatmax clamps stored at
	 * .data 0x1005c0a0/0x1005c0bc/0x1005c0f4, not initial values.
	 */
	memset(info, 0, sizeof(*info));
}

/*
=============
AAS_Sound_NormalizeName

Normalize a host-facing lookup name for the retained compatibility query path.
=============
*/
static void AAS_Sound_NormalizeName(const char *input, char *output, size_t size)
{
    if (output == NULL || size == 0)
    {
        return;
    }

    output[0] = '\0';
    if (input == NULL)
    {
        return;
    }

    char temp[128];
    size_t out_index = 0U;
    for (const char *cursor = input; *cursor != '\0' && out_index + 1U < sizeof(temp); ++cursor)
    {
        unsigned char c = (unsigned char)*cursor;
        if (c == '\\')
        {
            c = '/';
        }
        temp[out_index++] = (char)tolower(c);
    }
    temp[out_index] = '\0';

    const char *normalized = temp;
    if (strncmp(temp, "sound/", 6) == 0)
    {
        normalized = temp + 6;
    }

    strncpy(output, normalized, size - 1U);
    output[size - 1U] = '\0';
}

/*
=============
AAS_Sound_ParseConfigSource

Consumes only top-level `soundinfo` records from a preprocessed source, as in
retail `sub_1001c760`.
=============
*/
static bool AAS_Sound_ParseConfigSource(pc_source_t *source)
{
	if (source == NULL)
	{
		return false;
	}

	for (;;)
	{
		pc_token_t definition;
		int read_result = PC_ReadToken(source, &definition);
		if (read_result == 0)
		{
			return true;
		}
		if (read_result < 0)
		{
			return false;
		}
		/*
		 * Retail routes both soundconfig diagnostics through SourceError
		 * (0x1001c9bd j_sub_10039200), which prefixes them with
		 * "file %s, line %d: " at 0x1003924a, rather than calling
		 * botimport.Print directly.
		 */
		if (definition.type != TT_NAME ||
			strcmp(definition.string, "soundinfo") != 0)
		{
			SourceError(source,
				"unknown definition %s\n",
				definition.string);
			return false;
		}
		if (g_aas_sound_state.info_count >= g_aas_sound_state.info_capacity)
		{
			SourceError(source,
				"more than %d sound infos defined\n",
				(int)g_aas_sound_state.info_capacity);
			return false;
		}

		aas_soundinfo_t info;
		AAS_Sound_InitInfoDefaults(&info);
		if (!ReadStructure(source, &g_aas_soundinfo_struct, &info) ||
			!AAS_Sound_PushInfo(&info))
		{
			return false;
		}
	}
}

static bool AAS_Sound_EnsureInfoCapacity(void)
{
    if (g_aas_sound_state.infos != NULL)
    {
        return true;
    }

	g_aas_sound_state.infos = (aas_soundinfo_t *)GetClearedMemory(
		g_aas_sound_state.info_capacity * sizeof(aas_soundinfo_t));
    return (g_aas_sound_state.infos != NULL);
}

static bool AAS_Sound_PushInfo(const aas_soundinfo_t *info)
{
    if (info == NULL)
    {
        return false;
    }

    if (!AAS_Sound_EnsureInfoCapacity())
    {
        return false;
    }

	/*
	 * Retail has no diagnostic here: the caller rejects an over-capacity
	 * config on the soundinfo keyword itself (0x1001c8e8), so this guard is
	 * only a defensive backstop and must stay silent.
	 */
	if (g_aas_sound_state.info_count >= g_aas_sound_state.info_capacity)
	{
		return false;
	}

    g_aas_sound_state.infos[g_aas_sound_state.info_count++] = *info;
    return true;
}


static void AAS_Sound_FreeInfos(void)
{
	FreeMemory(g_aas_sound_state.infos);
    g_aas_sound_state.infos = NULL;
    g_aas_sound_state.info_count = 0U;
    g_aas_sound_state.info_capacity = 0U;
}

static void AAS_Sound_FreeAssets(void)
{
    FreeMemory(g_aas_sound_state.asset_info_by_index);

    g_aas_sound_state.asset_names = NULL;
	g_aas_sound_state.asset_info_by_index = NULL;
    g_aas_sound_state.asset_count = 0U;
}

/*
=============
AAS_Sound_FreeSoundEvents

Release the replaceable retail max_aassounds heap.
=============
*/
static void AAS_Sound_FreeSoundEvents(void)
{
	FreeMemory(g_aas_sound_state.sound_events);

	g_aas_sound_state.sound_events = NULL;
	g_aas_sound_state.sound_event_count = 0U;
	g_aas_sound_state.sound_scheduled_count = 0U;
	g_aas_sound_state.sound_event_capacity = 0U;
	g_aas_sound_state.sound_active_head = AAS_SOUND_NULL_INDEX;
	g_aas_sound_state.sound_active_tail = AAS_SOUND_NULL_INDEX;
	g_aas_sound_state.sound_scheduled_head = AAS_SOUND_NULL_INDEX;
	g_aas_sound_state.sound_scheduled_tail = AAS_SOUND_NULL_INDEX;
	g_aas_sound_state.sound_free_head = AAS_SOUND_NULL_INDEX;
}

/*
=============
AAS_Sound_FreePointLightEvents

Release the separately-owned max_aaslights heap for explicit test teardown.
=============
*/
static void AAS_Sound_FreePointLightEvents(void)
{
	FreeMemory(g_aas_sound_state.pointlight_events);

	g_aas_sound_state.pointlight_events = NULL;
	g_aas_sound_state.pointlight_event_count = 0U;
	g_aas_sound_state.pointlight_event_capacity = 0U;
	g_aas_sound_state.pointlight_active_head = AAS_POINTLIGHT_NULL_INDEX;
	g_aas_sound_state.pointlight_free_head = AAS_POINTLIGHT_NULL_INDEX;
}

/*
=============
AAS_Sound_FreeEvents

Release both independently-owned sensory heaps during explicit teardown.
=============
*/
static void AAS_Sound_FreeEvents(void)
{
	AAS_Sound_FreeSoundEvents();
	AAS_Sound_FreePointLightEvents();
}

/*
=============
AAS_Sound_InitSoundHeap

Build the raw 0x34-byte sound-record free list in ascending slot order.
=============
*/
static void AAS_Sound_InitSoundHeap(void)
{
	g_aas_sound_state.sound_active_head = AAS_SOUND_NULL_INDEX;
	g_aas_sound_state.sound_active_tail = AAS_SOUND_NULL_INDEX;
	g_aas_sound_state.sound_scheduled_head = AAS_SOUND_NULL_INDEX;
	g_aas_sound_state.sound_scheduled_tail = AAS_SOUND_NULL_INDEX;
	g_aas_sound_state.sound_free_head = AAS_SOUND_NULL_INDEX;
	g_aas_sound_state.sound_event_count = 0U;
	g_aas_sound_state.sound_scheduled_count = 0U;

	if (g_aas_sound_state.sound_events == NULL ||
		g_aas_sound_state.sound_event_capacity == 0U)
	{
		return;
	}

	g_aas_sound_state.sound_free_head = 0;
	for (size_t index = 0U;
		index < g_aas_sound_state.sound_event_capacity;
		++index)
	{
		aas_sound_event_t *event = &g_aas_sound_state.sound_events[index];
		event->prev_index = (index == 0U) ? AAS_SOUND_NULL_INDEX :
			(int)(index - 1U);
		event->next_index =
			(index + 1U < g_aas_sound_state.sound_event_capacity) ?
				(int)(index + 1U) : AAS_SOUND_NULL_INDEX;
	}
}

static void AAS_Sound_FreeSummaries(void)
{
    free(g_aas_sound_state.sound_summaries);
    free(g_aas_sound_state.pointlight_summaries);

    g_aas_sound_state.sound_summaries = NULL;
    g_aas_sound_state.pointlight_summaries = NULL;
    g_aas_sound_state.sound_summary_count = 0U;
    g_aas_sound_state.sound_summary_capacity = 0U;
    g_aas_sound_state.sound_summaries_dirty = false;
    g_aas_sound_state.pointlight_summary_count = 0U;
    g_aas_sound_state.pointlight_summary_capacity = 0U;
    g_aas_sound_state.pointlight_summaries_dirty = false;
}

/*
=============
AAS_Sound_InitPointLightHeap

Build the retail doubly-linked free list in ascending slot order.
=============
*/
static void AAS_Sound_InitPointLightHeap(void)
{
	g_aas_sound_state.pointlight_active_head = AAS_POINTLIGHT_NULL_INDEX;
	g_aas_sound_state.pointlight_free_head = AAS_POINTLIGHT_NULL_INDEX;

	if (g_aas_sound_state.pointlight_events == NULL ||
		g_aas_sound_state.pointlight_event_capacity == 0U)
	{
		return;
	}

	g_aas_sound_state.pointlight_free_head = 0;
	for (size_t index = 0U;
		index < g_aas_sound_state.pointlight_event_capacity;
		++index)
	{
		aas_pointlight_event_t *event =
			&g_aas_sound_state.pointlight_events[index];
		event->prev_index = (index == 0U) ?
			AAS_POINTLIGHT_NULL_INDEX : (int)(index - 1U);
		event->next_index =
			(index + 1U < g_aas_sound_state.pointlight_event_capacity) ?
				(int)(index + 1U) : AAS_POINTLIGHT_NULL_INDEX;
	}
}

/*
=============
AAS_Sound_FreeSoundSummaries

Release only the compatibility view derived from the sound-event heap.
=============
*/
static void AAS_Sound_FreeSoundSummaries(void)
{
	free(g_aas_sound_state.sound_summaries);

	g_aas_sound_state.sound_summaries = NULL;
	g_aas_sound_state.sound_summary_count = 0U;
	g_aas_sound_state.sound_summary_capacity = 0U;
	g_aas_sound_state.sound_summaries_dirty = false;
}

/*
=============
AAS_Sound_ReadCapacityLibVar

Converts and bounds a retail sensory-pool libvar, restoring its raw fallback
value when it falls outside the recovered inclusive range.
=============
*/
static int AAS_Sound_ReadCapacityLibVar(const char *name,
	const char *default_value,
	int maximum,
	int fallback)
{
	float configured = LibVarValue(name, default_value);
	int count;
	if (!isfinite(configured) ||
		(double)configured < (double)INT_MIN ||
		(double)configured >= (double)INT_MAX + 1.0)
	{
		count = INT_MIN;
	}
	else
	{
		count = (int)configured;
	}

	if (count < 0 || count > maximum)
	{
		BotLib_Print(PRT_ERROR,
			"%s out of range [0, %d]\n",
			name,
			maximum);
		count = fallback;
		LibVarSet(name, default_value);
	}

	return count;
}

/*
=============
AAS_SoundSubsystem_Init

Initialise the retail max_aassounds heap and soundinfo metadata. The separate
max_aaslights heap is built only by its recovered standalone initializer.
=============
*/
int AAS_SoundSubsystem_Init(void)
{
	/*
	 * Retail AAS_SetupSound (0x1001d260) is exactly
	 *     sub_1001cab0();
	 *     return sub_1001c760(LibVarString("soundconfig", "sounds.c"));
	 * so the max_aassounds heap is built first, the config name is read
	 * second, and max_soundinfo is not touched until the metadata loader runs.
	 * Each of those leaves also releases only its own old buffer, immediately
	 * before reallocating it (0x1001cafb / 0x1001c7b3), rather than clearing
	 * everything up front.  Both orders are observable through the
	 * out-of-range diagnostics and their LibVarSet resets.
	 */
	AAS_Sound_FreeSoundSummaries();
	g_aas_sound_state.initialised = false;

	AAS_Sound_FreeSoundEvents();
	int max_aassounds = AAS_Sound_ReadCapacityLibVar("max_aassounds",
		"256",
		65536,
		256);
	g_aas_sound_state.sound_event_capacity = (size_t)max_aassounds;

    if (g_aas_sound_state.sound_event_capacity > 0U)
    {
		g_aas_sound_state.sound_events =
			(aas_sound_event_t *)GetMemory(
				g_aas_sound_state.sound_event_capacity *
				sizeof(aas_sound_event_t));
		if (g_aas_sound_state.sound_events == NULL)
        {
			AAS_Sound_FreeInfos();
			AAS_Sound_FreeSoundEvents();
            return BLERR_INVALIDIMPORT;
        }
	}
	AAS_Sound_InitSoundHeap();

	botlib_asset_resolution_t resolution;
	const char *requested = LibVarString("soundconfig", "sounds.c");

	AAS_Sound_FreeInfos();
	int max_soundinfo = AAS_Sound_ReadCapacityLibVar("max_soundinfo",
		"256",
		65535,
		256);
	g_aas_sound_state.info_capacity = (size_t)max_soundinfo;
	if (!AAS_Sound_EnsureInfoCapacity())
	{
		AAS_Sound_FreeInfos();
		return BLERR_INVALIDIMPORT;
	}

	if (!BotLib_ResolveAssetPathDetailed(requested, "soundconfig", &resolution))
	{
		BotLib_Print(PRT_ERROR, "couldn't find %s\n", requested);
		g_aas_sound_state.initialised = true;
		return BLERR_NOERROR;
	}

	pc_source_t *source = PC_LoadSourceFile(resolution.resolved_path);
	if (source == NULL)
	{
		BotLib_Print(PRT_ERROR, "counldn't load %s\n", requested);
		g_aas_sound_state.initialised = true;
		return BLERR_NOERROR;
	}

	bool parsed = AAS_Sound_ParseConfigSource(source);
	PC_FreeSource(source);

	if (!parsed)
	{
		g_aas_sound_state.initialised = true;
		return BLERR_NOERROR;
	}

	/*
	 * Retail reports the provenance of the config it just parsed, and only on
	 * the success path: 0x1001c978 "loaded %s\%s\n" when the file came out of
	 * a pak container and 0x1001c9e7 "loaded %s\n" for a loose file, both at
	 * PRT_MESSAGE. The parse-failure path returns at 0x1001c9d7 first.
	 */
	if (resolution.pak_entry_length != 0)
	{
		BotLib_Print(PRT_MESSAGE, "loaded %s\\%s\n",
			resolution.source_path,
			requested);
	}
	else
	{
		BotLib_Print(PRT_MESSAGE, "loaded %s\n", requested);
	}

	g_aas_sound_state.initialised = true;
	return BLERR_NOERROR;
}

/*
=============
AAS_SoundSubsystem_InitPointLightHeap

Build the unconnected retail max_aaslights heap recovered at 0x1000d340.
Retail's normal BotSetup path does not invoke this helper.
=============
*/
void AAS_SoundSubsystem_InitPointLightHeap(void)
{
	float configured = LibVarValue("max_aaslights", "128");
	int capacity;
	if (!isfinite(configured) ||
		(double)configured < (double)INT_MIN ||
		(double)configured >= (double)INT_MAX + 1.0)
	{
		capacity = INT_MIN;
	}
	else
	{
		capacity = (int)configured;
	}

	if (capacity < 0 || capacity > 65536)
	{
		BotLib_Print(PRT_ERROR, "max_aaslights out of range [0, 65536]\n");
		capacity = 128;
	}

	/* The raw helper overwrites its heap pointer without a preceding free. */
	g_aas_sound_state.pointlight_event_capacity = (size_t)capacity;
	g_aas_sound_state.pointlight_event_count = 0U;
	g_aas_sound_state.pointlight_active_head = AAS_POINTLIGHT_NULL_INDEX;
	g_aas_sound_state.pointlight_free_head = AAS_POINTLIGHT_NULL_INDEX;
	if (g_aas_sound_state.pointlight_event_capacity == 0U)
	{
		return;
	}

	g_aas_sound_state.pointlight_events =
		(aas_pointlight_event_t *)GetMemory(
			g_aas_sound_state.pointlight_event_capacity *
			sizeof(aas_pointlight_event_t));
	if (g_aas_sound_state.pointlight_events == NULL)
	{
		g_aas_sound_state.pointlight_event_capacity = 0U;
		return;
	}

	AAS_Sound_InitPointLightHeap();
}

/*
=============
AAS_SoundSubsystem_Shutdown

Mirror retail sub_1001d290, which intentionally performs no cleanup. The AAS
global-state clear and BotMemory_Shutdown own normal teardown.
=============
*/
void AAS_SoundSubsystem_Shutdown(void)
{
}

/*
=============
AAS_SoundSubsystem_ResetState

Discard the sound-state references when AAS tears down. Retail's final AAS
global memset performs this without directly releasing the tracked sensory
pools; BotMemory_Shutdown owns their eventual reclamation.
=============
*/
void AAS_SoundSubsystem_ResetState(void)
{
	/* Compatibility summaries use the C heap and have no retail counterpart. */
	AAS_Sound_FreeSummaries();
	memset(&g_aas_sound_state, 0, sizeof(g_aas_sound_state));
}

void AAS_SoundSubsystem_ClearMapAssets(void)
{
    AAS_Sound_FreeAssets();
    g_aas_sound_state.sound_summaries_dirty = true;
}

/*
=============
AAS_SoundSubsystem_RegisterMapAssets

Build the raw cleared sound-index-to-soundinfo pointer table over engine-owned
asset name slots.
=============
*/
bool AAS_SoundSubsystem_RegisterMapAssets(int count, char *assets[])
{
    AAS_Sound_FreeAssets();

    if (count < 0)
    {
        return true;
    }

    size_t desired = (size_t)count;
	g_aas_sound_state.asset_info_by_index =
		(aas_soundinfo_t **)GetMemory(desired *
			sizeof(*g_aas_sound_state.asset_info_by_index));
	if (g_aas_sound_state.asset_info_by_index == NULL)
    {
        AAS_Sound_FreeAssets();
		return false;
	}

	memset(g_aas_sound_state.asset_info_by_index,
		0,
		desired * sizeof(*g_aas_sound_state.asset_info_by_index));
	g_aas_sound_state.asset_names = assets;
    for (size_t i = 0; i < desired; ++i)
    {
        const char *name = (assets != NULL) ? assets[i] : NULL;
        if (name == NULL)
        {
            continue;
        }

        for (size_t info_index = 0U;
            info_index < g_aas_sound_state.info_count;
            ++info_index)
        {
            aas_soundinfo_t *info =
				&g_aas_sound_state.infos[info_index];
			/*
			 * Retail compares through j_sub_10043c10 at 0x1001d1c9, which
			 * tail-calls _stricmp (sub_10045cb0). Case folding is the only
			 * transform: no backslash conversion and no "sound/" stripping.
			 */
			if (Q_stricmp(info->name, name) == 0)
			{
				g_aas_sound_state.asset_info_by_index[i] = info;
				break;
			}
        }
    }

    g_aas_sound_state.asset_count = desired;
    g_aas_sound_state.sound_summaries_dirty = true;
    return true;
}

/*
=============
AAS_Sound_PointLightIndexValid

Validate a host-side index before following a reconstructed retail link.
=============
*/
static bool AAS_Sound_PointLightIndexValid(int index)
{
	return index >= 0 &&
		(size_t)index < g_aas_sound_state.pointlight_event_capacity;
}

/*
=============
AAS_Sound_FreeActivePointLight

Unlink one live light and push its unchanged record onto the free-list head.
=============
*/
static void AAS_Sound_FreeActivePointLight(int index)
{
	if (!AAS_Sound_PointLightIndexValid(index))
	{
		return;
	}

	aas_pointlight_event_t *event =
		&g_aas_sound_state.pointlight_events[index];
	int previous = event->prev_index;
	int next = event->next_index;

	if (AAS_Sound_PointLightIndexValid(previous))
	{
		g_aas_sound_state.pointlight_events[previous].next_index = next;
	}
	else
	{
		g_aas_sound_state.pointlight_active_head = next;
	}
	if (AAS_Sound_PointLightIndexValid(next))
	{
		g_aas_sound_state.pointlight_events[next].prev_index = previous;
	}

	event->prev_index = AAS_POINTLIGHT_NULL_INDEX;
	event->next_index = g_aas_sound_state.pointlight_free_head;
	if (AAS_Sound_PointLightIndexValid(g_aas_sound_state.pointlight_free_head))
	{
		g_aas_sound_state.pointlight_events[
			g_aas_sound_state.pointlight_free_head].prev_index = index;
	}
	g_aas_sound_state.pointlight_free_head = index;
	if (g_aas_sound_state.pointlight_event_count > 0U)
	{
		g_aas_sound_state.pointlight_event_count -= 1U;
	}
}

/*
=============
AAS_Sound_ExpirePointLights

Expire retail point lights only when their absolute expiry precedes the frame.
=============
*/
static void AAS_Sound_ExpirePointLights(float frame_time)
{
	int index = g_aas_sound_state.pointlight_active_head;
	while (AAS_Sound_PointLightIndexValid(index))
	{
		aas_pointlight_event_t *event =
			&g_aas_sound_state.pointlight_events[index];
		int next = event->next_index;
		if (event->time < frame_time)
		{
			AAS_Sound_FreeActivePointLight(index);
			g_aas_sound_state.pointlight_summaries_dirty = true;
		}
		index = next;
	}
}

/*
=============
AAS_Sound_EventIndexValid

Validate an index before following a link in the reconstructed sound heap.
=============
*/
static bool AAS_Sound_EventIndexValid(int index)
{
	return index >= 0 &&
		(size_t)index < g_aas_sound_state.sound_event_capacity;
}

/*
=============
AAS_Sound_PopFreeEvent

Pop the first retail sound record from the free list.
=============
*/
static int AAS_Sound_PopFreeEvent(void)
{
	int index = g_aas_sound_state.sound_free_head;
	if (!AAS_Sound_EventIndexValid(index))
	{
		return AAS_SOUND_NULL_INDEX;
	}

	aas_sound_event_t *event = &g_aas_sound_state.sound_events[index];
	g_aas_sound_state.sound_free_head = event->next_index;
	if (AAS_Sound_EventIndexValid(g_aas_sound_state.sound_free_head))
	{
		g_aas_sound_state.sound_events[
			g_aas_sound_state.sound_free_head].prev_index = AAS_SOUND_NULL_INDEX;
	}
	event->next_index = AAS_SOUND_NULL_INDEX;
	event->prev_index = AAS_SOUND_NULL_INDEX;
	return index;
}

/*
=============
AAS_Sound_PushFreeEvent

Return an unlinked sound record to the retail free-list head.
=============
*/
static void AAS_Sound_PushFreeEvent(int index)
{
	if (!AAS_Sound_EventIndexValid(index))
	{
		return;
	}

	aas_sound_event_t *event = &g_aas_sound_state.sound_events[index];
	event->prev_index = AAS_SOUND_NULL_INDEX;
	event->next_index = g_aas_sound_state.sound_free_head;
	if (AAS_Sound_EventIndexValid(g_aas_sound_state.sound_free_head))
	{
		g_aas_sound_state.sound_events[
			g_aas_sound_state.sound_free_head].prev_index = index;
	}
	g_aas_sound_state.sound_free_head = index;
}

/*
=============
AAS_Sound_UnlinkActiveEvent

Unlink an active sound without changing its record contents.
=============
*/
static void AAS_Sound_UnlinkActiveEvent(int index)
{
	if (!AAS_Sound_EventIndexValid(index))
	{
		return;
	}

	aas_sound_event_t *event = &g_aas_sound_state.sound_events[index];
	int previous = event->prev_index;
	int next = event->next_index;
	if (AAS_Sound_EventIndexValid(previous))
	{
		g_aas_sound_state.sound_events[previous].next_index = next;
	}
	else
	{
		g_aas_sound_state.sound_active_head = next;
	}
	if (AAS_Sound_EventIndexValid(next))
	{
		g_aas_sound_state.sound_events[next].prev_index = previous;
	}
	else
	{
		g_aas_sound_state.sound_active_tail = previous;
	}
	event->next_index = AAS_SOUND_NULL_INDEX;
	event->prev_index = AAS_SOUND_NULL_INDEX;
	if (g_aas_sound_state.sound_event_count > 0U)
	{
		g_aas_sound_state.sound_event_count -= 1U;
	}
}

/*
=============
AAS_Sound_UnlinkScheduledEvent

Unlink one delayed sound from the start-time-ordered retail queue.
=============
*/
static void AAS_Sound_UnlinkScheduledEvent(int index)
{
	if (!AAS_Sound_EventIndexValid(index))
	{
		return;
	}

	aas_sound_event_t *event = &g_aas_sound_state.sound_events[index];
	int previous = event->prev_index;
	int next = event->next_index;
	if (AAS_Sound_EventIndexValid(previous))
	{
		g_aas_sound_state.sound_events[previous].next_index = next;
	}
	else
	{
		g_aas_sound_state.sound_scheduled_head = next;
	}
	if (AAS_Sound_EventIndexValid(next))
	{
		g_aas_sound_state.sound_events[next].prev_index = previous;
	}
	else
	{
		g_aas_sound_state.sound_scheduled_tail = previous;
	}
	event->next_index = AAS_SOUND_NULL_INDEX;
	event->prev_index = AAS_SOUND_NULL_INDEX;
	if (g_aas_sound_state.sound_scheduled_count > 0U)
	{
		g_aas_sound_state.sound_scheduled_count -= 1U;
	}
}

/*
=============
AAS_Sound_InsertActiveEvent

Insert one record by ascending end time, retaining the raw equal-time order.
=============
*/
static void AAS_Sound_InsertActiveEvent(int index)
{
	if (!AAS_Sound_EventIndexValid(index))
	{
		return;
	}

	aas_sound_event_t *event = &g_aas_sound_state.sound_events[index];
	int previous = g_aas_sound_state.sound_active_tail;
	while (AAS_Sound_EventIndexValid(previous) &&
		g_aas_sound_state.sound_events[previous].end >= event->end)
	{
		previous = g_aas_sound_state.sound_events[previous].prev_index;
	}

	int next = AAS_Sound_EventIndexValid(previous) ?
		g_aas_sound_state.sound_events[previous].next_index :
		g_aas_sound_state.sound_active_head;
	event->prev_index = previous;
	event->next_index = next;
	if (AAS_Sound_EventIndexValid(previous))
	{
		g_aas_sound_state.sound_events[previous].next_index = index;
	}
	else
	{
		g_aas_sound_state.sound_active_head = index;
	}
	if (AAS_Sound_EventIndexValid(next))
	{
		g_aas_sound_state.sound_events[next].prev_index = index;
	}
	else
	{
		g_aas_sound_state.sound_active_tail = index;
	}
	g_aas_sound_state.sound_event_count += 1U;
}

/*
=============
AAS_Sound_InsertScheduledEvent

Insert one record by ascending start time, retaining the raw equal-time order.
=============
*/
static void AAS_Sound_InsertScheduledEvent(int index)
{
	if (!AAS_Sound_EventIndexValid(index))
	{
		return;
	}

	aas_sound_event_t *event = &g_aas_sound_state.sound_events[index];
	int previous = g_aas_sound_state.sound_scheduled_tail;
	while (AAS_Sound_EventIndexValid(previous) &&
		g_aas_sound_state.sound_events[previous].start >= event->start)
	{
		previous = g_aas_sound_state.sound_events[previous].prev_index;
	}

	int next = AAS_Sound_EventIndexValid(previous) ?
		g_aas_sound_state.sound_events[previous].next_index :
		g_aas_sound_state.sound_scheduled_head;
	event->prev_index = previous;
	event->next_index = next;
	if (AAS_Sound_EventIndexValid(previous))
	{
		g_aas_sound_state.sound_events[previous].next_index = index;
	}
	else
	{
		g_aas_sound_state.sound_scheduled_head = index;
	}
	if (AAS_Sound_EventIndexValid(next))
	{
		g_aas_sound_state.sound_events[next].prev_index = index;
	}
	else
	{
		g_aas_sound_state.sound_scheduled_tail = index;
	}
	g_aas_sound_state.sound_scheduled_count += 1U;
}

/*
=============
AAS_Sound_RemoveActiveEntitySound

Remove the first active record with the exact entity/sound-index pair.
=============
*/
static void AAS_Sound_RemoveActiveEntitySound(int ent, int soundindex)
{
	for (int index = g_aas_sound_state.sound_active_head;
		AAS_Sound_EventIndexValid(index);
		index = g_aas_sound_state.sound_events[index].next_index)
	{
		aas_sound_event_t *event = &g_aas_sound_state.sound_events[index];
		if (event->ent == ent && event->soundindex == soundindex)
		{
			AAS_Sound_UnlinkActiveEvent(index);
			AAS_Sound_PushFreeEvent(index);
			return;
		}
	}
}

/*
=============
AAS_Sound_ExpireActiveSounds

Return every active record whose end is less than or equal to this frame.
=============
*/
static void AAS_Sound_ExpireActiveSounds(float frame_time)
{
	while (AAS_Sound_EventIndexValid(g_aas_sound_state.sound_active_head))
	{
		int index = g_aas_sound_state.sound_active_head;
		if (!(g_aas_sound_state.sound_events[index].end <= frame_time))
		{
			break;
		}
		AAS_Sound_UnlinkActiveEvent(index);
		AAS_Sound_PushFreeEvent(index);
	}
}

/*
=============
AAS_Sound_ActivateScheduledSounds

Move records whose start strictly precedes this frame into the active list.
=============
*/
static void AAS_Sound_ActivateScheduledSounds(float frame_time)
{
	while (AAS_Sound_EventIndexValid(g_aas_sound_state.sound_scheduled_head))
	{
		int index = g_aas_sound_state.sound_scheduled_head;
		aas_sound_event_t *event = &g_aas_sound_state.sound_events[index];
		if (!(event->start < frame_time))
		{
			break;
		}

		int ent = event->ent;
		int soundindex = event->soundindex;
		AAS_Sound_UnlinkScheduledEvent(index);
		AAS_Sound_RemoveActiveEntitySound(ent, soundindex);
		AAS_Sound_InsertActiveEvent(index);
	}
}

/*
=============
AAS_SoundSubsystem_SetFrameTime

Advance sensory time and run the raw sound and point-light expiry passes.
=============
*/
void AAS_SoundSubsystem_SetFrameTime(float time)
{
    if (!g_aas_sound_state.frame_time_initialised)
    {
        g_aas_sound_state.previous_frame_time = time;
        g_aas_sound_state.frame_time_initialised = true;
    }
    else
    {
        g_aas_sound_state.previous_frame_time = g_aas_sound_state.frame_time;
    }

    g_aas_sound_state.frame_time = time;
	AAS_Sound_ExpireActiveSounds(time);
	AAS_Sound_ActivateScheduledSounds(time);
	AAS_Sound_ExpirePointLights(time);
    g_aas_sound_state.sound_summaries_dirty = true;
    g_aas_sound_state.pointlight_summaries_dirty = true;
}

/*
=============
AAS_SoundSubsystem_ResetFrameEvents

Retained host entry point. Retail advances the two sound lists by time instead
of clearing them at frame boundaries.
=============
*/
void AAS_SoundSubsystem_ResetFrameEvents(void)
{
    g_aas_sound_state.pointlight_summary_count = 0U;
    g_aas_sound_state.sound_summaries_dirty = true;
    g_aas_sound_state.pointlight_summaries_dirty = true;
}

/*
=============
AAS_SoundSubsystem_UpdateSound

Mirror the raw BotUpdateSound leaf, including its sound-index diagnostic and
handled no-table/empty-heap paths.
=============
*/
int AAS_SoundSubsystem_UpdateSound(const vec3_t origin,
	int ent,
	int channel,
	int soundindex,
	float volume,
	float attenuation,
	float timeofs)
{
	if (!g_aas_sound_state.initialised)
	{
		return BLERR_INVALIDIMPORT;
	}
	if (soundindex < 0 || (size_t)soundindex >= g_aas_sound_state.asset_count)
	{
		BotLib_Print(PRT_FATAL,
			"sound index %d out of range [0, %d]\n",
			soundindex,
			(int)g_aas_sound_state.asset_count);
		return BLERR_INVALIDSOUNDINDEX;
	}
	if (g_aas_sound_state.sound_events == NULL ||
		g_aas_sound_state.sound_event_capacity == 0U ||
		g_aas_sound_state.asset_info_by_index == NULL)
	{
		BotLib_Print(PRT_MESSAGE, "no soundindex to soundinfo table\n");
		return BLERR_NOERROR;
	}

	aas_soundinfo_t *info =
		g_aas_sound_state.asset_info_by_index[soundindex];
	if (info == NULL)
	{
		return BLERR_NOERROR;
	}

	/*
	 * Retail compares timeofs against zero and branches on the x87 equal bit:
	 * 0x1001ce95 builds the status word and 0x1001ce9a tests mask 0x40 (C3),
	 * not 0x01 (C0, '<'). An immediate emit therefore drops the already-active
	 * record for the same (entity, soundindex) pair; a delayed emit does not.
	 */
	if (timeofs == 0.0f)
	{
		AAS_Sound_RemoveActiveEntitySound(ent, soundindex);
	}

	int index = AAS_Sound_PopFreeEvent();
	if (!AAS_Sound_EventIndexValid(index))
	{
		BotLib_Print(PRT_ERROR, "empty sound heap\n");
		return BLERR_NOERROR;
	}

	aas_sound_event_t *event = &g_aas_sound_state.sound_events[index];

    if (origin != NULL)
    {
        VectorCopy(origin, event->origin);
    }
    else
    {
        VectorClear(event->origin);
    }

	/*
	 * Retail computes the end time as (AAS_Time() + duration) + timeofs at
	 * 0x1001cee4, re-reading the clock instead of reusing the start value.
	 * Keep that operand order so the float rounding of the active-list sort
	 * key matches.
	 */
	event->start = g_aas_sound_state.frame_time + timeofs;
	event->end = g_aas_sound_state.frame_time + info->duration + timeofs;
	event->zero = 0;
	event->ent = ent;
    event->channel = channel;
    event->soundindex = soundindex;
    event->volume = volume;
    event->attenuation = attenuation;
	AAS_Sound_InsertScheduledEvent(index);
    g_aas_sound_state.sound_summaries_dirty = true;

	return BLERR_NOERROR;
}

/*
=============
AAS_SoundSubsystem_RecordSound

Retain the host boolean convenience surface over the raw status-returning API.
=============
*/
bool AAS_SoundSubsystem_RecordSound(const vec3_t origin,
	int ent,
	int channel,
	int soundindex,
	float volume,
	float attenuation,
	float timeofs)
{
	return AAS_SoundSubsystem_UpdateSound(origin,
		ent,
		channel,
		soundindex,
		volume,
		attenuation,
		timeofs) == BLERR_NOERROR;
}

/*
=============
AAS_SoundSubsystem_RecordPointLight

Allocate and link one retail point-light record. The reversed origin copy is
intentional and matches gladiator.dll at 0x1000d55c-0x1000d571. An empty heap
is a handled retail diagnostic; false only reports an uninitialised subsystem.
=============
*/
bool AAS_SoundSubsystem_RecordPointLight(vec3_t origin,
	int ent,
	float radius,
	float r,
	float g,
	float b,
	float time,
	float decay)
{
	int index = g_aas_sound_state.pointlight_free_head;
	if (!AAS_Sound_PointLightIndexValid(index))
	{
		BotLib_Print(PRT_MESSAGE, "WARNING: empty light heap\n");
		return true;
	}

	aas_pointlight_event_t *event =
		&g_aas_sound_state.pointlight_events[index];
	g_aas_sound_state.pointlight_free_head = event->next_index;
	if (AAS_Sound_PointLightIndexValid(g_aas_sound_state.pointlight_free_head))
	{
		g_aas_sound_state.pointlight_events[
			g_aas_sound_state.pointlight_free_head].prev_index =
			AAS_POINTLIGHT_NULL_INDEX;
	}

	/* Retail writes the stale slot origin back through the caller pointer. */
	if (origin != NULL)
	{
		VectorCopy(event->origin, origin);
	}
	event->ent = ent;
	event->color[0] = r;
	event->color[1] = g;
	event->color[2] = b;
	event->radius = radius;
	event->time = time;
	event->timestamp = g_aas_sound_state.frame_time;
	event->decay = decay;

	event->prev_index = AAS_POINTLIGHT_NULL_INDEX;
	event->next_index = g_aas_sound_state.pointlight_active_head;
	if (AAS_Sound_PointLightIndexValid(g_aas_sound_state.pointlight_active_head))
	{
		g_aas_sound_state.pointlight_events[
			g_aas_sound_state.pointlight_active_head].prev_index = index;
	}
	g_aas_sound_state.pointlight_active_head = index;
	g_aas_sound_state.pointlight_event_count += 1U;
	g_aas_sound_state.pointlight_summaries_dirty = true;
	return true;
}

size_t AAS_SoundSubsystem_SoundEventCount(void)
{
    return g_aas_sound_state.sound_event_count;
}

const aas_sound_event_t *AAS_SoundSubsystem_SoundEvent(size_t index)
{
    if (index >= g_aas_sound_state.sound_event_count)
    {
        return NULL;
    }

	int event_index = g_aas_sound_state.sound_active_head;
	for (size_t position = 0U; position < index; ++position)
	{
		if (!AAS_Sound_EventIndexValid(event_index))
		{
			return NULL;
		}
		event_index = g_aas_sound_state.sound_events[event_index].next_index;
	}
	if (!AAS_Sound_EventIndexValid(event_index))
	{
		return NULL;
	}
	return &g_aas_sound_state.sound_events[event_index];
}

/*
=============
AAS_SoundSubsystem_PointLightCount

Return the number of currently linked persistent lights.
=============
*/
size_t AAS_SoundSubsystem_PointLightCount(void)
{
    return g_aas_sound_state.pointlight_event_count;
}

/*
=============
AAS_SoundSubsystem_PointLight

Return a live light by newest-first active-list position.
=============
*/
const aas_pointlight_event_t *AAS_SoundSubsystem_PointLight(size_t index)
{
    if (index >= g_aas_sound_state.pointlight_event_count)
    {
        return NULL;
    }

	int event_index = g_aas_sound_state.pointlight_active_head;
	for (size_t position = 0U; position < index; ++position)
	{
		if (!AAS_Sound_PointLightIndexValid(event_index))
		{
			return NULL;
		}
		event_index =
			g_aas_sound_state.pointlight_events[event_index].next_index;
	}
	if (!AAS_Sound_PointLightIndexValid(event_index))
	{
		return NULL;
	}
	return &g_aas_sound_state.pointlight_events[event_index];
}

/*
=============
AAS_PointLight

Sample the BSP lightmap 4096 units downward, then accumulate live dynamic
lights in newest-first order exactly as the retail scalar routine does.
=============
*/
int AAS_PointLight(const vec3_t origin, int *red, int *green, int *blue)
{
	vec3_t start;
	vec3_t end;
	vec3_t sample_point;
	int sample_red = 0;
	int sample_green = 0;
	int sample_blue = 0;
	int light = 255;

	if (origin != NULL)
	{
		VectorCopy(origin, start);
		VectorCopy(origin, sample_point);
	}
	else
	{
		VectorClear(start);
		VectorClear(sample_point);
	}
	VectorCopy(start, end);
	end[2] -= 4096.0f;

	if (origin != NULL &&
		AAS_BSPTracePointLight(start,
			end,
			sample_point,
			&sample_red,
			&sample_green,
			&sample_blue))
	{
		light = (sample_red + sample_green + sample_blue) / 3;
	}
	/*
	 * Retail leaves sample_point/RGB uninitialised on a static miss. Keeping
	 * the request point and zero RGB is the sole deterministic UB guard here.
	 */

	int event_index = g_aas_sound_state.pointlight_active_head;
	while (AAS_Sound_PointLightIndexValid(event_index))
	{
		const aas_pointlight_event_t *event =
			&g_aas_sound_state.pointlight_events[event_index];
		float dx = sample_point[0] - event->origin[0];
		float dy = sample_point[1] - event->origin[1];
		float dz = sample_point[2] - event->origin[2];
		float addition = event->radius - sqrtf(dx * dx + dy * dy + dz * dz);
		if (addition > 0.0f)
		{
			light = (int)((float)light + addition);
			sample_red = (int)((float)sample_red + event->color[0]);
			sample_green = (int)((float)sample_green + event->color[1]);
			sample_blue = (int)((float)sample_blue + event->color[2]);
		}
		event_index = event->next_index;
	}

	if (red != NULL)
	{
		*red = sample_red;
	}
	if (green != NULL)
	{
		*green = sample_green;
	}
	if (blue != NULL)
	{
		*blue = sample_blue;
	}
	return light;
}

static int AAS_SoundSubsystem_SortSoundSummaries(const void *lhs, const void *rhs)
{
    const aas_sound_event_summary_t *a = (const aas_sound_event_summary_t *)lhs;
    const aas_sound_event_summary_t *b = (const aas_sound_event_summary_t *)rhs;

    if (a->timestamp > b->timestamp)
    {
        return -1;
    }
    if (a->timestamp < b->timestamp)
    {
        return 1;
    }
    return 0;
}

static int AAS_SoundSubsystem_SortPointLightSummaries(const void *lhs, const void *rhs)
{
    const aas_pointlight_event_summary_t *a =
        (const aas_pointlight_event_summary_t *)lhs;
    const aas_pointlight_event_summary_t *b =
        (const aas_pointlight_event_summary_t *)rhs;

    if (a->timestamp > b->timestamp)
    {
        return -1;
    }
    if (a->timestamp < b->timestamp)
    {
        return 1;
    }
    return 0;
}

static bool AAS_SoundSubsystem_EnsureSoundSummaryCapacity(size_t desired)
{
    if (g_aas_sound_state.sound_summary_capacity >= desired)
    {
        return true;
    }

    aas_sound_event_summary_t *resized =
        (aas_sound_event_summary_t *)realloc(g_aas_sound_state.sound_summaries,
                                             desired * sizeof(aas_sound_event_summary_t));
    if (resized == NULL)
    {
        BotLib_Print(PRT_ERROR,
                     "AAS_Sound: failed to allocate sound summary buffer (%zu)\n",
                     desired);
        return false;
    }

    g_aas_sound_state.sound_summaries = resized;
    g_aas_sound_state.sound_summary_capacity = desired;
    return true;
}

static bool AAS_SoundSubsystem_EnsurePointLightSummaryCapacity(size_t desired)
{
    if (g_aas_sound_state.pointlight_summary_capacity >= desired)
    {
        return true;
    }

    aas_pointlight_event_summary_t *resized =
        (aas_pointlight_event_summary_t *)realloc(
            g_aas_sound_state.pointlight_summaries,
            desired * sizeof(aas_pointlight_event_summary_t));
    if (resized == NULL)
    {
        BotLib_Print(PRT_ERROR,
                     "AAS_Sound: failed to allocate point light summary buffer (%zu)\n",
                     desired);
        return false;
    }

    g_aas_sound_state.pointlight_summaries = resized;
    g_aas_sound_state.pointlight_summary_capacity = desired;
    return true;
}

static bool AAS_SoundSubsystem_RebuildSoundSummaries(void)
{
    size_t event_count = g_aas_sound_state.sound_event_count;
    if (event_count == 0U)
    {
        g_aas_sound_state.sound_summary_count = 0U;
        g_aas_sound_state.sound_summaries_dirty = false;
        return true;
    }

    if (!AAS_SoundSubsystem_EnsureSoundSummaryCapacity(event_count))
    {
        g_aas_sound_state.sound_summary_count = 0U;
        return false;
    }

    int event_index = g_aas_sound_state.sound_active_head;
	for (size_t index = 0U; index < event_count; ++index)
    {
        if (!AAS_Sound_EventIndexValid(event_index))
        {
            g_aas_sound_state.sound_summary_count = 0U;
            return false;
        }

		const aas_sound_event_t *event =
			&g_aas_sound_state.sound_events[event_index];
        aas_sound_event_summary_t *summary = &g_aas_sound_state.sound_summaries[index];

		const aas_soundinfo_t *info =
			(event->soundindex >= 0 &&
			(size_t)event->soundindex < g_aas_sound_state.asset_count &&
			g_aas_sound_state.asset_info_by_index != NULL) ?
				g_aas_sound_state.asset_info_by_index[event->soundindex] : NULL;
		int info_index = (info != NULL &&
			info >= g_aas_sound_state.infos &&
			info < g_aas_sound_state.infos + g_aas_sound_state.info_count) ?
			(int)(info - g_aas_sound_state.infos) : -1;

        summary->event = event;
		summary->timestamp = event->start;
        summary->info_index = info_index;
        summary->info = info;
        summary->has_info = (info != NULL);
        summary->sound_type = (info != NULL) ? info->type : 0;

        summary->has_expiry = false;
        summary->expiry_time = 0.0f;
        summary->expired = false;
        summary->expired_this_frame = false;

        if (info != NULL)
        {
            summary->has_expiry = true;
			summary->expiry_time = event->end;

            if (g_aas_sound_state.frame_time >= summary->expiry_time)
            {
                summary->expired = true;
                if (g_aas_sound_state.frame_time_initialised
                    && g_aas_sound_state.previous_frame_time < summary->expiry_time)
                {
                    summary->expired_this_frame = true;
                }
            }
        }

		event_index = event->next_index;
    }

    qsort(g_aas_sound_state.sound_summaries,
          event_count,
          sizeof(aas_sound_event_summary_t),
          AAS_SoundSubsystem_SortSoundSummaries);

    size_t prune_limit = g_aas_sound_state.sound_event_capacity;
    g_aas_sound_state.sound_summary_count =
        (prune_limit > 0U && event_count > prune_limit) ? prune_limit : event_count;
    g_aas_sound_state.sound_summaries_dirty = false;
    return true;
}

/*
=============
AAS_SoundSubsystem_RebuildPointLightSummaries

Project the newest-first active light list into the compatibility summary view.
=============
*/
static bool AAS_SoundSubsystem_RebuildPointLightSummaries(void)
{
    size_t event_count = g_aas_sound_state.pointlight_event_count;
    if (event_count == 0U)
    {
        g_aas_sound_state.pointlight_summary_count = 0U;
        g_aas_sound_state.pointlight_summaries_dirty = false;
        return true;
    }

    if (!AAS_SoundSubsystem_EnsurePointLightSummaryCapacity(event_count))
    {
        g_aas_sound_state.pointlight_summary_count = 0U;
        return false;
    }

	int event_index = g_aas_sound_state.pointlight_active_head;
    for (size_t index = 0; index < event_count; ++index)
    {
		if (!AAS_Sound_PointLightIndexValid(event_index))
		{
			g_aas_sound_state.pointlight_summary_count = index;
			return false;
		}
		const aas_pointlight_event_t *event =
			&g_aas_sound_state.pointlight_events[event_index];
        aas_pointlight_event_summary_t *summary =
            &g_aas_sound_state.pointlight_summaries[index];

        summary->event = event;
        summary->timestamp = event->timestamp;
        summary->has_expiry = false;
        summary->expiry_time = 0.0f;
        summary->expired = false;
        summary->expired_this_frame = false;

        float lifetime = (event->time > 0.0f) ? event->time : 0.0f;
        float decay = (event->decay > 0.0f) ? event->decay : 0.0f;
        float expiry_time = event->timestamp + lifetime + decay;

        if (lifetime > 0.0f || decay > 0.0f)
        {
            summary->has_expiry = true;
            summary->expiry_time = expiry_time;

            if (g_aas_sound_state.frame_time >= expiry_time)
            {
                summary->expired = true;
                if (g_aas_sound_state.frame_time_initialised
                    && g_aas_sound_state.previous_frame_time < expiry_time)
                {
                    summary->expired_this_frame = true;
                }
            }
        }
		event_index = event->next_index;
    }

    qsort(g_aas_sound_state.pointlight_summaries,
          event_count,
          sizeof(aas_pointlight_event_summary_t),
          AAS_SoundSubsystem_SortPointLightSummaries);

    size_t pointlight_limit = g_aas_sound_state.pointlight_event_capacity;
    g_aas_sound_state.pointlight_summary_count =
        (pointlight_limit > 0U && event_count > pointlight_limit) ? pointlight_limit : event_count;
    g_aas_sound_state.pointlight_summaries_dirty = false;
    return true;
}

size_t AAS_SoundSubsystem_InfoCount(void)
{
    return g_aas_sound_state.info_count;
}

const aas_soundinfo_t *AAS_SoundSubsystem_Info(size_t index)
{
    if (index >= g_aas_sound_state.info_count)
    {
        return NULL;
    }
    return &g_aas_sound_state.infos[index];
}

int AAS_SoundSubsystem_FindInfoIndex(const char *name)
{
    if (name == NULL || name[0] == '\0' || g_aas_sound_state.infos == NULL)
    {
        return -1;
    }

    char normalized[128];
    AAS_Sound_NormalizeName(name, normalized, sizeof(normalized));

    for (size_t i = 0; i < g_aas_sound_state.info_count; ++i)
    {
        const aas_soundinfo_t *info = &g_aas_sound_state.infos[i];
		char info_normalized[sizeof(info->name)];
		AAS_Sound_NormalizeName(info->name,
			info_normalized,
			sizeof(info_normalized));
		if (strcmp(info_normalized, normalized) == 0)
        {
            return (int)i;
        }
    }

    return -1;
}

const aas_soundinfo_t *AAS_SoundSubsystem_InfoForSoundIndex(int soundindex)
{
    if (soundindex < 0 || (size_t)soundindex >= g_aas_sound_state.asset_count
		|| g_aas_sound_state.asset_info_by_index == NULL)
    {
        return NULL;
    }

	return g_aas_sound_state.asset_info_by_index[soundindex];
}

const char *AAS_SoundSubsystem_AssetName(int soundindex)
{
    if (soundindex < 0 || (size_t)soundindex >= g_aas_sound_state.asset_count
        || g_aas_sound_state.asset_names == NULL)
    {
        return NULL;
    }
    return g_aas_sound_state.asset_names[soundindex];
}

int AAS_SoundSubsystem_SoundTypeForIndex(int soundindex)
{
    const aas_soundinfo_t *info = AAS_SoundSubsystem_InfoForSoundIndex(soundindex);
    return (info != NULL) ? info->type : 0;
}

size_t AAS_SoundSubsystem_SoundSummaries(const aas_sound_event_summary_t **summaries)
{
    if (summaries == NULL)
    {
        return 0U;
    }

    if (!g_aas_sound_state.initialised)
    {
        *summaries = NULL;
        return 0U;
    }

    if (g_aas_sound_state.sound_summaries_dirty
        && !AAS_SoundSubsystem_RebuildSoundSummaries())
    {
        *summaries = NULL;
        return 0U;
    }

    *summaries = g_aas_sound_state.sound_summaries;
    return g_aas_sound_state.sound_summary_count;
}

size_t
AAS_SoundSubsystem_PointLightSummaries(const aas_pointlight_event_summary_t **summaries)
{
    if (summaries == NULL)
    {
        return 0U;
    }

    if (!g_aas_sound_state.initialised)
    {
        *summaries = NULL;
        return 0U;
    }

    if (g_aas_sound_state.pointlight_summaries_dirty
        && !AAS_SoundSubsystem_RebuildPointLightSummaries())
    {
        *summaries = NULL;
        return 0U;
    }

    *summaries = g_aas_sound_state.pointlight_summaries;
    return g_aas_sound_state.pointlight_summary_count;
}
