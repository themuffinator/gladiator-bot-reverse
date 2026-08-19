#include "bot_goal.h"

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "botlib/aas/aas_local.h"
#include "botlib/aas/aas_map.h"
#include "botlib/ai_move/bot_move.h"
#include "botlib/common/l_assets.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/interface/botlib_interface.h"
#include "botlib/precomp/l_script.h"
#include "q2bridge/bridge.h"
#include "q2bridge/botlib.h"

#define BOT_GOAL_MAX_MAPLOCATIONS 256
#define BOT_GOAL_MAX_CAMPSPOTS 256
#define BOT_GOAL_MAX_MODELINDEXES 256
#define BOT_GOAL_TRAVELTIME_SCALE 0.01f
#define BOT_GOAL_MAX_MODEL_PATH 128
#define BOT_GOAL_DEFAULT_RESPAWN_TIME 30.0f
#define BOT_GOAL_MINIMUM_AVOID_TIME 10.0f
#define BOT_GOAL_DROPPED_AVOID_TIME 10.0f
#define BOT_GOAL_DROPPED_SCORE_BONUS 20.0f
#define BOT_GOAL_DROPPED_TIMEOUT 30.0f
#define BOT_GOAL_ENTITY_LINK_DISTANCE 20.0f

#define BOT_GOAL_ITEMFLAG_NOTFREE	(1 << 0)
#define BOT_GOAL_ITEMFLAG_NOTTEAM	(1 << 1)
#define BOT_GOAL_ITEMFLAG_NOTSINGLE	(1 << 2)
#define BOT_GOAL_ITEMFLAG_NOTBOT	(1 << 3)

void StripDoubleQuotes(char *string);

typedef struct bot_goalstate_adapter_s
{
	bot_goalstate_t *state;
	int client;
	int itemweightcount;
	bool itemweightconfig_owned;
	bool itemweightindex_owned;
	bool state_owned;
} bot_goalstate_adapter_t;

#define BOT_GOAL_RETAIL_HANDLE_BASE (MAX_CLIENTS + 1)
#define BOT_GOAL_HANDLE_LIMIT (2 * MAX_CLIENTS + 1)

static bot_goalstate_t *g_goalstates[BOT_GOAL_HANDLE_LIMIT + 1];
static bot_goalstate_adapter_t g_goalstate_adapters[BOT_GOAL_HANDLE_LIMIT + 1];

static float g_next_entity_item_update_time = 0.0f;

typedef struct bot_levelitem_s
{
	int number;
	int iteminfo;
	vec3_t origin;
	int areanum;
	vec3_t goalorigin;
	int entitynum;
	float timeout;
	struct bot_levelitem_s *prev;
	struct bot_levelitem_s *next;
} bot_levelitem_t;

typedef struct bot_levelitem_adapter_s
{
	char classname[64];
	vec3_t mins;
	vec3_t maxs;
	float base_weight;
	float respawntime;
	float next_respawn_time;
	int goalflags;
	int itemflags;
	bool valid;
	bool raw_item;
} bot_levelitem_adapter_t;

_Static_assert(sizeof(bot_goal_t) == 0x38, "retail bot goal size");
_Static_assert(offsetof(bot_levelitem_t, number) == 0x00, "retail level item number offset");
_Static_assert(offsetof(bot_levelitem_t, iteminfo) == 0x04, "retail level item info offset");
_Static_assert(offsetof(bot_levelitem_t, origin) == 0x08, "retail level item origin offset");
_Static_assert(offsetof(bot_levelitem_t, areanum) == 0x14, "retail level item area offset");
_Static_assert(offsetof(bot_levelitem_t, goalorigin) == 0x18, "retail level item goal origin offset");
_Static_assert(offsetof(bot_levelitem_t, entitynum) == 0x24, "retail level item entity offset");
_Static_assert(offsetof(bot_levelitem_t, timeout) == 0x28, "retail level item timeout offset");

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(offsetof(bot_goalstate_t, itemweightconfig) == 0x000,
	"retail goal-state config offset");
_Static_assert(offsetof(bot_goalstate_t, itemweightindex) == 0x004,
	"retail goal-state index offset");
_Static_assert(offsetof(bot_goalstate_t, goalstack) == 0x008,
	"retail goal-state stack offset");
_Static_assert(offsetof(bot_goalstate_t, goalstacktop) == 0x1c8,
	"retail goal-state top offset");
_Static_assert(offsetof(bot_goalstate_t, avoidgoals) == 0x1cc,
	"retail goal-state avoid-number offset");
_Static_assert(offsetof(bot_goalstate_t, avoidgoaltimes) == 0x2cc,
	"retail goal-state avoid-time offset");
_Static_assert(sizeof(bot_goalstate_t) == 0x3cc, "retail goal-state size");
_Static_assert(offsetof(bot_levelitem_t, prev) == 0x2c, "retail level item prev offset");
_Static_assert(offsetof(bot_levelitem_t, next) == 0x30, "retail level item next offset");
_Static_assert(sizeof(bot_levelitem_t) == 0x34, "retail level item size");
#endif

typedef struct bot_maplocation_s
{
	vec3_t origin;
	int areanum;
	char name[64];
	bool valid;
} bot_maplocation_t;

typedef struct bot_campspot_s
{
	vec3_t origin;
	int areanum;
	char name[64];
	float range;
	float weight;
	float wait;
	float random;
	bool valid;
} bot_campspot_t;

typedef struct bot_itemdef_s
{
	char name[80];
	char classname[80];
	char model[80];
	int modelindex;
	int type;
	int index;
	float respawntime;
	vec3_t mins;
	vec3_t maxs;
	int number;
} bot_itemdef_t;

typedef struct bot_itemconfig_s
{
	int numiteminfo;
	uint32_t iteminfo;
} bot_itemconfig_t;

_Static_assert(offsetof(bot_itemdef_t, name) == 0x00, "retail iteminfo name offset");
_Static_assert(offsetof(bot_itemdef_t, classname) == 0x50, "retail iteminfo classname offset");
_Static_assert(offsetof(bot_itemdef_t, model) == 0xa0, "retail iteminfo model offset");
_Static_assert(offsetof(bot_itemdef_t, modelindex) == 0xf0, "retail iteminfo modelindex offset");
_Static_assert(offsetof(bot_itemdef_t, type) == 0xf4, "retail iteminfo type offset");
_Static_assert(offsetof(bot_itemdef_t, index) == 0xf8, "retail iteminfo index offset");
_Static_assert(offsetof(bot_itemdef_t, respawntime) == 0xfc, "retail iteminfo respawn offset");
_Static_assert(offsetof(bot_itemdef_t, mins) == 0x100, "retail iteminfo mins offset");
_Static_assert(offsetof(bot_itemdef_t, maxs) == 0x10c, "retail iteminfo maxs offset");
_Static_assert(offsetof(bot_itemdef_t, number) == 0x118, "retail iteminfo number offset");
_Static_assert(sizeof(bot_itemdef_t) == 0x11c, "retail iteminfo size");

_Static_assert(offsetof(bot_itemconfig_t, numiteminfo) == 0x00,
	"retail item config count offset");
_Static_assert(offsetof(bot_itemconfig_t, iteminfo) == 0x04,
	"retail item config array offset");
_Static_assert(sizeof(bot_itemconfig_t) == 0x08, "retail item config size");

typedef struct bot_goal_parsed_entity_s
{
	char classname[64];
	bool has_classname;
	char message[64];
	vec3_t origin;
	bool has_origin;
	int spawnflags;
	bool has_spawnflags;
	float range;
	bool has_range;
	float weight;
	bool has_weight;
	float wait;
	bool has_wait;
	float random;
	bool has_random;
} bot_goal_parsed_entity_t;

static bot_levelitem_t *g_levelitems = NULL;
static bot_levelitem_adapter_t *g_levelitem_adapters = NULL;
static bot_levelitem_t *g_levelitem_head = NULL;
static bot_levelitem_t *g_levelitem_free = NULL;
static int g_levelitem_capacity = 0;
static int g_static_levelitem_count = 0;
static int g_next_levelitem_number = 1;
static bool g_levelitem_scan_aborted = false;

static char (*g_extra_iteminfo_names)[64] = NULL;
static int g_extra_iteminfo_count = 0;
static int g_extra_iteminfo_capacity = 0;

static bot_maplocation_t g_maplocations[BOT_GOAL_MAX_MAPLOCATIONS];
static int g_maplocation_count = 0;

static bot_campspot_t g_campspots[BOT_GOAL_MAX_CAMPSPOTS];
static int g_campspot_count = 0;

static bot_itemconfig_t *g_itemconfig = NULL;
static bot_itemdef_t *g_itemdefs = NULL;
static int g_itemdef_count = 0;
static int g_itemdef_capacity = 0;
static bool g_itemdefs_loaded = false;

static char g_modelindex_names[BOT_GOAL_MAX_MODELINDEXES][BOT_GOAL_MAX_MODEL_PATH];
static int g_modelindex_count = 0;

static int BotGoal_PointAreaNum(const vec3_t origin);
static int BotGoal_StartAreaForState(bot_goalstate_t *gs, const vec3_t origin);
static bot_goalstate_t *BotGoalStateFromHandle(int handle);
static bot_goalstate_adapter_t *BotGoalStateAdapter(bot_goalstate_t *gs);
static const bot_goalstate_adapter_t *BotGoalStateAdapterConst(const bot_goalstate_t *gs);
static bot_levelitem_adapter_t *BotGoal_LevelItemAdapter(bot_levelitem_t *item);
static const bot_levelitem_adapter_t *BotGoal_LevelItemAdapterConst(const bot_levelitem_t *item);
static bot_levelitem_t *BotGoal_AllocLevelItem(void);
static const bot_itemdef_t *BotGoal_ItemDefForLevelItem(const bot_levelitem_t *item);
static bool BotGoal_EnsureWeightCapacity(bot_goalstate_t *gs);
static float BotGoal_EvaluateItemWeight(const bot_goalstate_t *gs,
	const int *inventory,
	const bot_levelitem_t *item);
static float BotGoal_ItemBaseWeight(const bot_levelitem_t *item, float fuzzy_weight);
static float BotGoal_AvoidGoalTimeForState(const bot_goalstate_t *gs, int number);
static float BotGoal_RespawnAvoidTimeForItem(const bot_levelitem_t *item);
static float BotGoal_AvoidTimeForItem(const bot_levelitem_t *item);
static bot_levelitem_t *BotGoal_FindLevelItem(int number);
static bot_levelitem_t *BotGoal_FindCompatibilityLevelItem(int number);
static void BotGoal_ActivateLevelItem(bot_levelitem_t *item);
static void BotGoal_RemoveLevelItem(bot_levelitem_t *item);
static int BotGoal_FindItemInfoIndex(const char *classname);
static int BotGoal_RegisterItemInfo(const char *classname);
static int BotGoal_ItemInfoCount(void);
static const char *BotGoal_ItemInfoName(int index);
static int BotGoal_IndexFromModel(const char *model);
static void BotGoal_ResolveItemDefModelIndexes(void);
static void BotGoal_ClearLevelItemState(void);
static void BotGoal_ClearLevelItems(void);
static bool BotGoal_BeginLevelItemLoad(void);
static void BotGoal_RebuildWeightIndices(bot_goalstate_t *gs);
static int BotGoal_StrIcmp(const char *lhs, const char *rhs);
static bool BotGoal_LevelItemAllowed(const bot_levelitem_t *item);
static bool BotGoal_LevelItemSelectable(const bot_levelitem_t *item);
static void BotGoal_CopyLevelItemGoal(const bot_levelitem_t *item,
	const bot_itemdef_t *itemdef,
	bot_goal_t *goal);
static void BotGoal_CopyLevelItemGoalForLookup(const bot_levelitem_t *item,
	const bot_itemdef_t *itemdef,
	bot_goal_t *goal);
static void BotGoal_CopySelectedItemGoal(const bot_levelitem_t *item, bot_goal_t *goal);
static bool BotGoal_VectorEquals(const vec3_t lhs, const vec3_t rhs);
static bool BotGoal_ReadSignedFloat(pc_source_t *source, float *out);
static bool BotGoal_ReadSignedInt(pc_source_t *source, int *out);
static bool BotGoal_ReadVector(pc_source_t *source, vec3_t out);
static bot_itemdef_t *BotGoal_FindItemDef(const char *classname);
static bot_itemdef_t *BotGoal_FindItemDefExact(const char *classname);
static bot_itemdef_t *BotGoal_FindItemDefByModelIndex(int modelindex);
static bot_itemdef_t *BotGoal_RegisterItemDef(const char *classname);
static bool BotGoal_ParseItemInfoBlock(pc_source_t *source, bot_itemdef_t *itemdef);
static bool BotGoal_BeginItemDefLoad(int capacity);
static void BotGoal_AbandonItemDefs(void);
static void BotGoal_ClearItemDefs(void);
static bool BotGoal_LoadItemDefs(void);
static int BotGoal_NotSpawnFlags(void);
static void BotGoal_AddMapLocation(const bot_goal_parsed_entity_t *entity);
static void BotGoal_AddCampSpot(const bot_goal_parsed_entity_t *entity);
static void BotGoal_AddRawLevelItemFromBSPEntity(const aas_bspentity_t *entity,
	int notspawnflags);
static void BotGoal_AddCompatibilityInfoEntity(const aas_bspentity_t *entity);
static void BotGoal_AddCompatibilityInfoEntities(const aas_bspentity_t *entity);

void SourceError(pc_source_t *source, char *str, ...);
int PC_ExpectAnyToken(pc_source_t *source, pc_token_t *token);
int PC_CheckTokenString(pc_source_t *source, char *string);

/*
=============
BotGoal_SetCurrentTime

Updates the AAS-time mirror used by the retail goal leaves.
=============
*/
void BotGoal_SetCurrentTime(float now)
{
	aasworld.time = now;
}

/*
=============
BotGoal_CurrentTime

Returns the AAS-time mirror used by the retail goal leaves.
=============
*/
float BotGoal_CurrentTime(void)
{
	return AAS_Time();
}

/*
=============
BotGoalStateFromHandle

Resolves a compatibility handle to its embedded retail goal-state core.
=============
*/
static bot_goalstate_t *BotGoalStateFromHandle(int handle)
{
	if (handle <= 0 || handle > BOT_GOAL_HANDLE_LIMIT)
	{
		return NULL;
	}
	return g_goalstates[handle];
}

/*
=============
BotGoalStateAdapter

Returns adapter-only ownership metadata stored after the retail core.
=============
*/
static bot_goalstate_adapter_t *BotGoalStateAdapter(bot_goalstate_t *gs)
{
	if (gs == NULL)
	{
		return NULL;
	}

	for (int handle = 1; handle <= BOT_GOAL_HANDLE_LIMIT; ++handle)
	{
		if (g_goalstate_adapters[handle].state == gs)
		{
			return &g_goalstate_adapters[handle];
		}
	}
	return NULL;
}

/*
=============
BotGoalStateAdapterConst

Returns read-only adapter metadata stored after the retail core.
=============
*/
static const bot_goalstate_adapter_t *BotGoalStateAdapterConst(const bot_goalstate_t *gs)
{
	return BotGoalStateAdapter((bot_goalstate_t *)gs);
}

/*
=============
BotGoalStatePeek

Exposes the exact logical retail core for diagnostics and tests.
=============
*/
const bot_goalstate_t *BotGoalStatePeek(int handle)
{
	return BotGoalStateFromHandle(handle);
}

/*
=============
BotGoal_FreeOwnedWeights

Releases the currently owned retail config/index while retaining the retail
leaf's stale core pointer values.
=============
*/
static void BotGoal_FreeOwnedWeights(bot_goalstate_adapter_t *adapter)
{
	if (adapter == NULL || adapter->state == NULL)
	{
		return;
	}

	if (adapter->itemweightconfig_owned && adapter->state->itemweightconfig != NULL)
	{
		FreeWeightConfig2(adapter->state->itemweightconfig);
	}
	if (adapter->itemweightindex_owned && adapter->state->itemweightindex != NULL)
	{
		FreeMemory(adapter->state->itemweightindex);
	}

	adapter->itemweightconfig_owned = false;
	adapter->itemweightindex_owned = false;
	adapter->itemweightcount = 0;
}

/*
=============
BotAllocGoalState

Allocates a handle wrapper whose first bytes are the exact retail core.
=============
*/
int BotAllocGoalState(int client)
{
	for (int handle = 1; handle <= MAX_CLIENTS; ++handle)
	{
		if (g_goalstates[handle] != NULL)
		{
			continue;
		}

		bot_goalstate_t *state =
			(bot_goalstate_t *)GetClearedMemory(sizeof(*state));
		if (state == NULL)
		{
			BotLib_Print(PRT_FATAL, "BotAllocGoalState: allocation failed\n");
			return 0;
		}

		bot_goalstate_adapter_t *adapter = &g_goalstate_adapters[handle];
		memset(adapter, 0, sizeof(*adapter));
		adapter->state = state;
		adapter->client = client;
		adapter->state_owned = true;
		g_goalstates[handle] = state;
		return handle;
	}

	BotLib_Print(PRT_ERROR, "BotAllocGoalState: no free goal state slots\n");
	return 0;
}

/*
=============
BotBindRetailGoalState

Binds a cleared goal core embedded in a retail client slab to its reserved,
collision-free internal handle without allocating a tracked owner block.
=============
*/
int BotBindRetailGoalState(int client, bot_goalstate_t *state)
{
	if (client < 0 || client > MAX_CLIENTS || state == NULL)
	{
		return 0;
	}

	int handle = BOT_GOAL_RETAIL_HANDLE_BASE + client;
	bot_goalstate_adapter_t *adapter = &g_goalstate_adapters[handle];
	if (adapter->state == state)
	{
		return handle;
	}
	if (adapter->state != NULL)
	{
		BotFreeGoalState(handle);
	}

	memset(state, 0, sizeof(*state));
	memset(adapter, 0, sizeof(*adapter));
	adapter->state = state;
	adapter->client = client;
	g_goalstates[handle] = state;
	return handle;
}

/*
=============
BotRebindRetailGoalState

Moves reserved-handle metadata after a complete retail client record is copied
to another physical slab slot. The already-copied core remains untouched.
=============
*/
int BotRebindRetailGoalState(int old_handle,
	int client,
	bot_goalstate_t *state)
{
	if (client < 0 || client > MAX_CLIENTS || state == NULL)
	{
		return 0;
	}

	int new_handle = BOT_GOAL_RETAIL_HANDLE_BASE + client;
	if (old_handle == new_handle)
	{
		g_goalstates[new_handle] = state;
		g_goalstate_adapters[new_handle].state = state;
		g_goalstate_adapters[new_handle].client = client;
		return new_handle;
	}

	if (old_handle <= 0 || old_handle > BOT_GOAL_HANDLE_LIMIT ||
		g_goalstates[old_handle] == NULL)
	{
		return BotBindRetailGoalState(client, state);
	}
	if (g_goalstates[new_handle] != NULL)
	{
		BotFreeGoalState(new_handle);
	}

	g_goalstate_adapters[new_handle] = g_goalstate_adapters[old_handle];
	g_goalstate_adapters[new_handle].state = state;
	g_goalstate_adapters[new_handle].client = client;
	g_goalstate_adapters[new_handle].state_owned = false;
	g_goalstates[new_handle] = state;

	memset(&g_goalstate_adapters[old_handle],
		0,
		sizeof(g_goalstate_adapters[old_handle]));
	g_goalstates[old_handle] = NULL;
	return new_handle;
}

/*
=============
BotFreeGoalState

Frees adapter-owned weights and the handle wrapper.
=============
*/
void BotFreeGoalState(int handle)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
		return;
	}

	BotGoal_FreeOwnedWeights(BotGoalStateAdapter(gs));
	bot_goalstate_adapter_t *adapter = BotGoalStateAdapter(gs);
	if (adapter != NULL && adapter->state_owned)
	{
		FreeMemory(gs);
	}
	if (adapter != NULL)
	{
		memset(adapter, 0, sizeof(*adapter));
	}
	g_goalstates[handle] = NULL;
}

/*
=============
BotResetGoalState

Clears the retail stack and fixed avoid table without touching weights.
=============
*/
void BotResetGoalState(int handle)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
		return;
	}

	memset(gs->goalstack, 0, sizeof(gs->goalstack));
	gs->goalstacktop = 0;
	memset(gs->avoidgoals, 0, sizeof(gs->avoidgoals));
	memset(gs->avoidgoaltimes, 0, sizeof(gs->avoidgoaltimes));
}

/*
=============
BotGoal_EnsureWeightCapacity

Resizes a goal state's iteminfo-index table to the current retail config.
=============
*/
static bool BotGoal_EnsureWeightCapacity(bot_goalstate_t *gs)
{
	if (gs == NULL)
	{
		return false;
	}

	bot_goalstate_adapter_t *adapter = BotGoalStateAdapter(gs);
	int iteminfo_count = BotGoal_ItemInfoCount();
	if (adapter->itemweightcount == iteminfo_count &&
		adapter->itemweightindex_owned)
	{
		return true;
	}

	int new_count = iteminfo_count;
	if (new_count <= 0)
	{
		if (adapter->itemweightindex_owned && gs->itemweightindex != NULL)
		{
			FreeMemory(gs->itemweightindex);
		}
		gs->itemweightindex = GetClearedMemory(0);
		if (gs->itemweightindex == NULL)
		{
			adapter->itemweightindex_owned = false;
			adapter->itemweightcount = 0;
			return false;
		}
		adapter->itemweightindex_owned = true;
		adapter->itemweightcount = 0;
		return true;
	}

	int *indices = (int *)GetClearedMemory(sizeof(int) * (size_t)new_count);
	if (indices == NULL)
	{
		return false;
	}

	for (int i = 0; i < new_count; ++i)
	{
		indices[i] = -1;
	}

	if (adapter->itemweightindex_owned && gs->itemweightindex != NULL &&
		adapter->itemweightcount > 0)
	{
		int copy = adapter->itemweightcount;
		if (copy > new_count)
		{
			copy = new_count;
		}
		memcpy(indices, gs->itemweightindex, sizeof(int) * (size_t)copy);
		FreeMemory(gs->itemweightindex);
	}

	gs->itemweightindex = indices;
	adapter->itemweightindex_owned = true;
	adapter->itemweightcount = new_count;
	return true;
}

/*
=============
BotGoal_ItemWeightIndexByteSize

Returns the tracked allocation size for a goal state's item-weight index.
=============
*/
size_t BotGoal_ItemWeightIndexByteSize(int handle)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL || gs->itemweightindex == NULL ||
		!BotGoalStateAdapterConst(gs)->itemweightindex_owned)
	{
		return 0;
	}

	return (size_t)BotGoalStateAdapterConst(gs)->itemweightcount * sizeof(int);
}

/*
=============
BotLoadItemWeights

Loads one distinct retail-owned config and rebuilds its item index.
=============
*/
int BotLoadItemWeights(int handle, const char *filename)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
		BotLib_Print(PRT_ERROR, "BotLoadItemWeights: invalid goal state %d\n", handle);
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	bot_goalstate_adapter_t *adapter = BotGoalStateAdapter(gs);
	/* Retail abandons the prior owner and stores the loader result before testing it. */
	adapter->itemweightconfig_owned = false;
	bot_weight_config_t *config = ReadWeightConfigUncached(filename);
	gs->itemweightconfig = config;
	if (config == NULL)
	{
		BotLib_Print(PRT_FATAL, "couldn't load weights\n");
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	/* Retail overwrites these pointers without releasing the prior allocation. */
	adapter->itemweightconfig_owned = true;
	if (!g_itemdefs_loaded)
	{
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	adapter->itemweightindex_owned = false;
	adapter->itemweightcount = g_itemdef_count;
	gs->itemweightindex = GetClearedMemory(sizeof(*gs->itemweightindex) *
		(size_t)g_itemdef_count);
	if (gs->itemweightindex == NULL)
	{
		BotLib_Print(PRT_FATAL, "BotLoadItemWeights: weight index allocation failed\n");
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}
	adapter->itemweightindex_owned = true;

	for (int i = 0; i < g_itemdef_count; ++i)
	{
		const char *name = g_itemdefs[i].classname;
		gs->itemweightindex[i] = BotWeight_FindIndex(gs->itemweightconfig, name);
		if (gs->itemweightindex[i] < 0)
		{
			BotLib_LogWrite("item info %d \"%s\" has no fuzzy weight",
				i,
				name);
		}
	}

	return BLERR_NOERROR;
}

/*
=============
BotFreeItemWeights
=============
*/
void BotFreeItemWeights(int handle)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
		return;
	}

	BotGoal_FreeOwnedWeights(BotGoalStateAdapter(gs));
}

/*
=============
BotWeightIndex
=============
*/
int BotWeightIndex(int handle, const char *classname)
{
	if (!BotLibraryEnsureSetup("BotWeightIndex")) {
		return -1;
	}

	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL) {
		BotLib_Print(PRT_ERROR, "BotWeightIndex: invalid goal state %d\n", handle);
		return -1;
	}

	if (classname == NULL) {
		return -1;
	}

	const bot_goalstate_adapter_t *adapter = BotGoalStateAdapterConst(gs);
	int index = BotGoal_FindItemInfoIndex(classname);
	if (index < 0) {
		return -1;
	}
	if (index >= g_itemdef_count) {
		return (gs->itemweightconfig != NULL)
			? BotWeight_FindIndex(gs->itemweightconfig, classname)
			: -1;
	}
	if (index >= adapter->itemweightcount || !adapter->itemweightindex_owned) {
		return -1;
	}

	return gs->itemweightindex[index];
}

/*
=============
BotResetAvoidGoals
=============
*/
void BotResetAvoidGoals(int handle)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
		return;
	}

	memset(gs->avoidgoals, 0, sizeof(gs->avoidgoals));
	memset(gs->avoidgoaltimes, 0, sizeof(gs->avoidgoaltimes));
}

/*
=============
BotAddToAvoidGoals

Store a retail fixed-slot goal avoidance deadline.
=============
*/
void BotAddToAvoidGoals(int handle, int number, float avoidtime)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
		return;
	}

	for (int i = 0; i < BOT_GOAL_MAX_AVOID; ++i)
	{
		if (AAS_Time() > gs->avoidgoaltimes[i])
		{
			gs->avoidgoals[i] = number;
			gs->avoidgoaltimes[i] = AAS_Time() + avoidtime;
			return;
		}
	}
}

/*
=============
BotRemoveFromAvoidGoals
=============
*/
void BotRemoveFromAvoidGoals(int handle, int number)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL || number == 0)
	{
		return;
	}

	for (int i = 0; i < BOT_GOAL_MAX_AVOID; ++i)
	{
		if (gs->avoidgoals[i] == number && gs->avoidgoaltimes[i] >= AAS_Time())
		{
			gs->avoidgoaltimes[i] = 0.0f;
			return;
		}
	}
}

/*
=============
BotAvoidGoalTime
=============
*/
float BotAvoidGoalTime(int handle, int number)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
		return 0.0f;
	}

	for (int i = 0; i < BOT_GOAL_MAX_AVOID; ++i)
	{
		if (gs->avoidgoals[i] == number && gs->avoidgoaltimes[i] >= AAS_Time())
		{
			return gs->avoidgoaltimes[i] - AAS_Time();
		}
	}
	return 0.0f;
}

/*
=============
BotSetAvoidGoalTime
=============
*/
void BotSetAvoidGoalTime(int handle, int number, float avoidtime)
{
	if (avoidtime < 0.0f)
	{
		const bot_levelitem_t *item = BotGoal_FindLevelItem(number);
		if (item == NULL)
		{
			return;
		}
		avoidtime = BotGoal_RespawnAvoidTimeForItem(item);
	}

	BotAddToAvoidGoals(handle, number, avoidtime);
}

/*
=============
BotPushGoal
=============
*/
int BotPushGoal(int handle, const bot_goal_t *goal)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL || goal == NULL)
	{
		return 0;
	}

	if (gs->goalstacktop >= BOT_GOAL_MAX_STACK - 1)
	{
		BotLib_Print(PRT_ERROR, "goal heap overflow\n");
		BotDumpGoalStack(handle);
		return 0;
	}

	gs->goalstacktop++;
	gs->goalstack[gs->goalstacktop] = *goal;
	return gs->goalstacktop;
}

/*
=============
BotPopGoal
=============
*/
int BotPopGoal(int handle)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL || gs->goalstacktop <= 0)
	{
		return 0;
	}

	gs->goalstacktop--;
	return gs->goalstacktop;
}

/*
=============
BotEmptyGoalStack
=============
*/
void BotEmptyGoalStack(int handle)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
		return;
	}

	gs->goalstacktop = 0;
}

/*
=============
BotGetTopGoal
=============
*/
int BotGetTopGoal(int handle, bot_goal_t *goal)
{
	const bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL || gs->goalstacktop <= 0)
	{
		return 0;
	}

	if (goal != NULL)
	{
		*goal = gs->goalstack[gs->goalstacktop];
	}
	return 1;
}

/*
=============
BotGetSecondGoal
=============
*/
int BotGetSecondGoal(int handle, bot_goal_t *goal)
{
	const bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL || gs->goalstacktop <= 1)
	{
		return 0;
	}

	if (goal != NULL)
	{
		*goal = gs->goalstack[gs->goalstacktop - 1];
	}
	return 1;
}

/*
=============
BotGoal_EvaluateItemWeight

Samples the retail undecided fuzzy path used by LTG/NBG item scoring.
=============
*/
static float BotGoal_EvaluateItemWeight(const bot_goalstate_t *gs,
	const int *inventory,
	const bot_levelitem_t *item)
{
	float weight = 0.0f;
	const bot_goalstate_adapter_t *adapter =
		(gs != NULL) ? BotGoalStateAdapterConst(gs) : NULL;
	const bot_levelitem_adapter_t *item_adapter =
		BotGoal_LevelItemAdapterConst(item);
	if (gs != NULL && item_adapter != NULL && !item_adapter->raw_item &&
		gs->itemweightconfig != NULL && adapter->itemweightconfig_owned)
	{
		int fuzzy_index = BotWeight_FindIndex(gs->itemweightconfig,
			item_adapter->classname);
		return (fuzzy_index >= 0)
			? FuzzyWeightUndecided(inventory, gs->itemweightconfig, fuzzy_index)
			: 0.0f;
	}

	int iteminfo_index = (item != NULL) ? item->iteminfo : -1;
	if (gs != NULL && gs->itemweightconfig != NULL &&
		gs->itemweightindex != NULL &&
		adapter->itemweightconfig_owned && adapter->itemweightindex_owned &&
		iteminfo_index >= 0 && iteminfo_index < adapter->itemweightcount)
	{
		int fuzzy_index = gs->itemweightindex[iteminfo_index];
		if (fuzzy_index >= 0)
		{
			weight = FuzzyWeightUndecided(inventory, gs->itemweightconfig, fuzzy_index);
		}
	}
	return weight;
}

/*
=============
BotGoal_ItemBaseWeight

Applies Gladiator/Q3 roam scaling while preserving reconstructed fixture
weights for synthetic level items.
=============
*/
static float BotGoal_ItemBaseWeight(const bot_levelitem_t *item, float fuzzy_weight)
{
	if (item == NULL)
	{
		return fuzzy_weight;
	}

	const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
	if (adapter == NULL)
	{
		return fuzzy_weight;
	}

	if ((adapter->goalflags & GFL_ROAM) && adapter->base_weight > 0.0f)
	{
		if (fuzzy_weight > 0.0f)
		{
			return fuzzy_weight * adapter->base_weight;
		}
		return adapter->base_weight;
	}

	if (fuzzy_weight > 0.0f)
	{
		return fuzzy_weight + adapter->base_weight;
	}

	return adapter->base_weight;
}

/*
=============
BotGoal_AvoidGoalTimeForState

Returns the remaining avoid time without going back through the public handle.
=============
*/
static float BotGoal_AvoidGoalTimeForState(const bot_goalstate_t *gs, int number)
{
	if (gs == NULL)
	{
		return 0.0f;
	}

	for (int i = 0; i < BOT_GOAL_MAX_AVOID; ++i)
	{
		if (gs->avoidgoals[i] == number && gs->avoidgoaltimes[i] >= AAS_Time())
		{
			return gs->avoidgoaltimes[i] - AAS_Time();
		}
	}

	return 0.0f;
}

/*
=============
BotGoal_RespawnAvoidTimeForItem

Derive the respawn/default/minimum avoid timeout used by the public negative
BotSetAvoidGoalTime path.
=============
*/
static float BotGoal_RespawnAvoidTimeForItem(const bot_levelitem_t *item)
{
	if (item == NULL)
	{
		return BOT_GOAL_DEFAULT_RESPAWN_TIME;
	}

	const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
	float avoidtime = (adapter != NULL) ? adapter->respawntime : 0.0f;
	if (avoidtime <= 0.0f)
	{
		avoidtime = BOT_GOAL_DEFAULT_RESPAWN_TIME;
	}
	if (avoidtime < BOT_GOAL_MINIMUM_AVOID_TIME)
	{
		avoidtime = BOT_GOAL_MINIMUM_AVOID_TIME;
	}
	return avoidtime;
}

/*
=============
BotGoal_AvoidTimeForItem

Computes the retail avoid timeout assigned after choosing an item goal.
=============
*/
static float BotGoal_AvoidTimeForItem(const bot_levelitem_t *item)
{
	if (item == NULL)
	{
		return BOT_GOAL_DEFAULT_RESPAWN_TIME;
	}

	const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
	if (adapter == NULL)
	{
		return BOT_GOAL_DEFAULT_RESPAWN_TIME;
	}

	if (adapter->raw_item)
	{
		if (adapter->respawntime != 0.0f)
		{
			return adapter->respawntime;
		}
		return BOT_GOAL_DEFAULT_RESPAWN_TIME;
	}

	if (item->timeout > 0.0f)
	{
		return BOT_GOAL_DROPPED_AVOID_TIME;
	}

	return BotGoal_RespawnAvoidTimeForItem(item);
}

/*
=============
BotGoal_PointAreaNum

Linear-scan fallback for resolving the area containing a point. Valid area
numbers run from one to numAreas minus one because retail counts the dummy
zero area in numAreas.
=============
*/
static int BotGoal_PointAreaNum(const vec3_t origin)
{
	if (!aasworld.loaded || aasworld.areas == NULL || aasworld.numAreas <= 0)
	{
		return 0;
	}

	for (int areanum = 1; areanum < aasworld.numAreas; ++areanum)
	{
		const aas_area_t *area = &aasworld.areas[areanum];
		if (origin[0] < area->mins[0] || origin[0] > area->maxs[0])
		{
			continue;
		}
		if (origin[1] < area->mins[1] || origin[1] > area->maxs[1])
		{
			continue;
		}
		if (origin[2] < area->mins[2] || origin[2] > area->maxs[2])
		{
			continue;
		}
		return areanum;
	}

	return 0;
}

/*
=============
BotGoal_StartAreaForState

Resolve the bot's current reachability area. Retail selection calls
BotReachabilityArea with the inverted swim test, so the extra drop-to-floor
pass runs for a bot standing in air or on ground and is suppressed while it is
submerged in lava, slime, or water. Gladiator rejects a missing or unreachable
result directly; it does not retain the Q3 last-area fallback.
=============
*/
static int BotGoal_StartAreaForState(bot_goalstate_t *gs, const vec3_t origin)
{
	if (gs == NULL)
	{
		return 0;
	}

	int testground = !AAS_Swimming(origin);
	int areanum = BotReachabilityArea(origin, testground);
	if (areanum <= 0 || AAS_AreaReachability(areanum) == 0)
	{
		return 0;
	}

	return areanum;
}

/*
=============
BotGoal_LevelItemAdapter

Finds compatibility metadata stored in the parallel level-item sidecar.
=============
*/
static bot_levelitem_adapter_t *BotGoal_LevelItemAdapter(bot_levelitem_t *item)
{
	if (item == NULL || g_levelitems == NULL || g_levelitem_adapters == NULL)
	{
		return NULL;
	}

	ptrdiff_t index = item - g_levelitems;
	if (index < 0 || index >= g_levelitem_capacity)
	{
		return NULL;
	}
	return &g_levelitem_adapters[index];
}

/*
=============
BotGoal_LevelItemAdapterConst

Finds read-only compatibility metadata for a retail level-item record.
=============
*/
static const bot_levelitem_adapter_t *BotGoal_LevelItemAdapterConst(const bot_levelitem_t *item)
{
	return BotGoal_LevelItemAdapter((bot_levelitem_t *)item);
}

/*
=============
BotGoal_AllocLevelItem

Pops the retail free-list head without clearing the raw record.
=============
*/
static bot_levelitem_t *BotGoal_AllocLevelItem(void)
{
	bot_levelitem_t *item = g_levelitem_free;
	if (item == NULL)
	{
		BotLib_Print(PRT_FATAL, "out of level items\n");
	}
	else
	{
		g_levelitem_free = item->next;
	}
	return item;
}

/*
=============
BotGoal_ItemDefForLevelItem

Uses the stored iteminfo number for raw records so duplicate definitions retain
their declaration identity.
=============
*/
static const bot_itemdef_t *BotGoal_ItemDefForLevelItem(const bot_levelitem_t *item)
{
	if (item == NULL)
	{
		return NULL;
	}
	if (item->iteminfo >= 0 && item->iteminfo < g_itemdef_count)
	{
		return &g_itemdefs[item->iteminfo];
	}

	const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
	return (adapter != NULL) ? BotGoal_FindItemDef(adapter->classname) : NULL;
}

/*
=============
BotGoal_FindLevelItem

Returns the retail record for a public number, falling back to a compatibility
registration only when no raw record owns that number.
=============
*/
static bot_levelitem_t *BotGoal_FindLevelItem(int number)
{
	bot_levelitem_t *compatibility = NULL;
	for (bot_levelitem_t *item = g_levelitem_head; item != NULL; item = item->next)
	{
		if (item->number != number)
		{
			continue;
		}

		const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
		if (adapter != NULL && adapter->raw_item)
		{
			return item;
		}
		if (compatibility == NULL)
		{
			compatibility = item;
		}
	}
	return compatibility;
}

/*
=============
BotGoal_FindCompatibilityLevelItem

Finds only an extension-owned registration so it cannot overwrite a retail
map or dropped-item record that happens to use the same public number.
=============
*/
static bot_levelitem_t *BotGoal_FindCompatibilityLevelItem(int number)
{
	for (bot_levelitem_t *item = g_levelitem_head; item != NULL; item = item->next)
	{
		const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
		if (item->number == number && adapter != NULL && !adapter->raw_item)
		{
			return item;
		}
	}
	return NULL;
}

/*
=============
BotGoal_ActivateLevelItem

Inserts a level-item record at the retail active-list head.
=============
*/
static void BotGoal_ActivateLevelItem(bot_levelitem_t *item)
{
	if (item == NULL)
	{
		return;
	}

	item->prev = NULL;
	item->next = g_levelitem_head;
	if (g_levelitem_head != NULL)
	{
		g_levelitem_head->prev = item;
	}
	g_levelitem_head = item;
}

/*
=============
BotGoal_RemoveLevelItem

Unlinks a level-item record before returning its pool slot to the free set.
=============
*/
static void BotGoal_RemoveLevelItem(bot_levelitem_t *item)
{
	bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapter(item);
	if (item == NULL || adapter == NULL || !adapter->valid)
	{
		return;
	}

	if (item->prev != NULL)
	{
		item->prev->next = item->next;
	}
	else
	{
		g_levelitem_head = item->next;
	}
	if (item->next != NULL)
	{
		item->next->prev = item->prev;
	}

	adapter->valid = false;
	item->next = g_levelitem_free;
	g_levelitem_free = item;
}

/*
=============
BotGoal_FindItemInfoIndex
=============
*/
static int BotGoal_FindItemInfoIndex(const char *classname)
{
	if (classname == NULL || classname[0] == '\0')
	{
		return -1;
	}

	bot_itemdef_t *itemdef = BotGoal_FindItemDef(classname);
	if (itemdef != NULL)
	{
		return (int)(itemdef - g_itemdefs);
	}

	for (int i = 0; i < g_extra_iteminfo_count; ++i)
	{
		if (strcmp(g_extra_iteminfo_names[i], classname) == 0)
		{
			return g_itemdef_count + i;
		}
	}
	return -1;
}

/*
=============
BotGoal_ItemInfoCount

Returns the retail itemconfig count plus non-retail external registration slots.
=============
*/
static int BotGoal_ItemInfoCount(void)
{
	return g_itemdef_count;
}

/*
=============
BotGoal_ItemInfoName

Resolves a fuzzy-weight index to a retail itemconfig classname or extension name.
=============
*/
static const char *BotGoal_ItemInfoName(int index)
{
	if (index < 0)
	{
		return NULL;
	}
	if (index < g_itemdef_count)
	{
		return g_itemdefs[index].classname;
	}

	index -= g_itemdef_count;
	if (index < g_extra_iteminfo_count)
	{
		return g_extra_iteminfo_names[index];
	}
	return NULL;
}

/*
=============
BotGoal_RegisterItemInfo

Adds a compatibility-only external classname after the retail itemconfig table.
=============
*/
static int BotGoal_RegisterItemInfo(const char *classname)
{
	if (classname == NULL || classname[0] == '\0')
	{
		return -1;
	}

	int existing = BotGoal_FindItemInfoIndex(classname);
	if (existing >= 0)
	{
		return existing;
	}

	if (g_extra_iteminfo_names == NULL ||
		g_extra_iteminfo_count >= g_extra_iteminfo_capacity)
	{
		BotLib_Print(PRT_FATAL, "out of level items\n");
		return -1;
	}

	strncpy(g_extra_iteminfo_names[g_extra_iteminfo_count],
		classname,
		sizeof(g_extra_iteminfo_names[0]) - 1);
	g_extra_iteminfo_names[g_extra_iteminfo_count]
		[sizeof(g_extra_iteminfo_names[0]) - 1] = '\0';
	int index = g_itemdef_count + g_extra_iteminfo_count;
	++g_extra_iteminfo_count;

	return index;
}

/*
=============
BotGoal_IndexFromModel

Resolve a Quake II model index from the BotLoadMap model table.
=============
*/
static int BotGoal_IndexFromModel(const char *model)
{
	if (model == NULL || model[0] == '\0')
	{
		return 0;
	}
	if (g_modelindex_count <= 0)
	{
		BotLib_Print(PRT_ERROR, "IndexFromModel: index not setup \"%s\"\n", model);
		return 0;
	}

	for (int i = 0; i < g_modelindex_count; ++i)
	{
		if (g_modelindex_names[i][0] == '\0')
		{
			continue;
		}
		if (BotGoal_StrIcmp(g_modelindex_names[i], model) == 0)
		{
			return i;
		}
	}

	return 0;
}

/*
=============
BotGoal_ResolveItemDefModelIndexes

Refresh iteminfo model indexes after a map's model table is available.
=============
*/
static void BotGoal_ResolveItemDefModelIndexes(void)
{
	for (int i = 0; i < g_itemdef_count; ++i)
	{
		bot_itemdef_t *itemdef = &g_itemdefs[i];
		itemdef->modelindex = BotGoal_IndexFromModel(itemdef->model);
		if (itemdef->modelindex <= 0)
		{
			BotLib_LogWrite("item %s has modelindex 0", itemdef->classname);
		}
	}
}

/*
=============
BotGoal_RegisterLevelItem

Registers a direct compatibility level item in the shared active/free lists.
=============
*/
int BotGoal_RegisterLevelItem(const bot_levelitem_setup_t *setup)
{
	if (setup == NULL || setup->classname == NULL || setup->classname[0] == '\0')
	{
		return 0;
	}

	int number = setup->goal.number;
	if (number <= 0)
	{
		number = g_next_levelitem_number++;
	}
	else if (number >= g_next_levelitem_number)
	{
		g_next_levelitem_number = number + 1;
	}

	int iteminfo = BotGoal_RegisterItemInfo(setup->classname);
	if (iteminfo < 0)
	{
		return 0;
	}

	bot_levelitem_t *existing = BotGoal_FindCompatibilityLevelItem(number);
	bot_levelitem_t *slot = existing;
	bool new_item = false;
	if (slot == NULL)
	{
		slot = BotGoal_AllocLevelItem();
		if (slot == NULL)
		{
			return 0;
		}
		new_item = true;
	}

	bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapter(slot);
	if (adapter == NULL)
	{
		return 0;
	}

	bot_levelitem_t *previous = slot->prev;
	bot_levelitem_t *next = slot->next;
	memset(slot, 0, sizeof(*slot));
	if (!new_item)
	{
		slot->prev = previous;
		slot->next = next;
	}
	memset(adapter, 0, sizeof(*adapter));
	slot->number = number;
	slot->iteminfo = iteminfo;
	VectorCopy(setup->goal.origin, slot->origin);
	VectorCopy(setup->goal.origin, slot->goalorigin);
	slot->areanum = setup->goal.areanum;
	slot->entitynum = setup->goal.entitynum;
	if (slot->areanum <= 0)
	{
		slot->areanum = BotGoal_PointAreaNum(slot->goalorigin);
	}

	strncpy(adapter->classname, setup->classname, sizeof(adapter->classname) - 1);
	adapter->classname[sizeof(adapter->classname) - 1] = '\0';
	VectorCopy(setup->goal.mins, adapter->mins);
	VectorCopy(setup->goal.maxs, adapter->maxs);
	adapter->base_weight = setup->weight;
	adapter->respawntime = (setup->respawntime > 0.0f) ? setup->respawntime : 0.0f;
	adapter->next_respawn_time = BotGoal_CurrentTime();
	adapter->goalflags = setup->flags;
	if (!(adapter->goalflags & (GFL_ITEM | GFL_ROAM | GFL_DROPPED)))
	{
		adapter->goalflags |= GFL_ITEM;
	}
	adapter->itemflags = setup->itemflags;
	adapter->valid = true;
	adapter->raw_item = false;

	slot->timeout = 0.0f;
	if ((adapter->goalflags & GFL_DROPPED) && setup->respawntime > 0.0f)
	{
		slot->timeout = BotGoal_CurrentTime() + setup->respawntime;
	}
	if (new_item)
	{
		BotGoal_ActivateLevelItem(slot);
	}
	return slot->number;
}

/*
=============
BotGoal_UnregisterLevelItem
=============
*/
void BotGoal_UnregisterLevelItem(int number)
{
	bot_levelitem_t *item = BotGoal_FindCompatibilityLevelItem(number);
	if (item != NULL)
	{
		BotGoal_RemoveLevelItem(item);
	}
}

/*
=============
BotGoal_MarkItemTaken
=============
*/
void BotGoal_MarkItemTaken(int number, float respawn_delay)
{
	bot_levelitem_t *item = BotGoal_FindCompatibilityLevelItem(number);
	bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapter(item);
	if (item == NULL || adapter == NULL)
	{
		return;
	}

	float delay = respawn_delay;
	if (delay <= 0.0f)
	{
		delay = adapter->respawntime;
	}
	if (delay < 0.0f)
	{
		delay = 0.0f;
	}

	adapter->next_respawn_time = BotGoal_CurrentTime() + delay;
}

/*
=============
BotGoal_SetMapModelIndexes

Cache the model table supplied through BotLoadMap for item model matching.
=============
*/
void BotGoal_SetMapModelIndexes(int modelindexes, char *modelindex[])
{
	memset(g_modelindex_names, 0, sizeof(g_modelindex_names));
	g_modelindex_count = 0;

	if (modelindexes <= 0 || modelindex == NULL)
	{
		return;
	}

	if (modelindexes > BOT_GOAL_MAX_MODELINDEXES)
	{
		modelindexes = BOT_GOAL_MAX_MODELINDEXES;
	}

	g_modelindex_count = modelindexes;
	for (int i = 0; i < modelindexes; ++i)
	{
		if (modelindex[i] == NULL)
		{
			continue;
		}
		strncpy(g_modelindex_names[i], modelindex[i], sizeof(g_modelindex_names[i]) - 1);
		g_modelindex_names[i][sizeof(g_modelindex_names[i]) - 1] = '\0';
	}

}

/*
=============
BotGoal_VectorEquals
=============
*/
static bool BotGoal_VectorEquals(const vec3_t lhs, const vec3_t rhs)
{
	return lhs[0] == rhs[0] && lhs[1] == rhs[1] && lhs[2] == rhs[2];
}

/*
=============
BotUpdateEntityItems

Refresh dropped or temporary entity items each frame.
=============
*/
void BotUpdateEntityItems(void)
{
	/*
	 * Retail carries the itemconfig index used by the entity-link path in EBX,
	 * a function-local it loads once at 0x1002fa90 from a slot that is only
	 * written later with the modelindex - an uninitialised read - and
	 * thereafter assigns solely in the modelindex search loop at
	 * 0x1002fbef-0x1002fc0d, which leaves it at numitems when the search
	 * fails.  The link branch at 0x1002fba5 then indexes ic->items with THAT
	 * value, not with the matched levelitem's own iteminfo.
	 *
	 * The pre-first-search value is genuine stack garbage and is not
	 * reproducible; g_itemdef_count is the only defined value retail can also
	 * produce, and it addresses the zeroed slot the loader always leaves
	 * spare, so the box comes out as {0,0,0}/{0,0,0} exactly as it does in
	 * retail after a failed search.
	 */
	int reused_iteminfo = g_itemdef_count;

	for (bot_levelitem_t *item = g_levelitem_head; item != NULL; )
	{
		bot_levelitem_t *next_item = item->next;
		if (item->timeout != 0.0f && AAS_Time() > item->timeout)
		{
			BotGoal_RemoveLevelItem(item);
		}
		item = next_item;
	}

	if (!g_itemdefs_loaded)
	{
		return;
	}

	for (int entnum = AAS_NextEntity(0);
		entnum != 0;
		entnum = AAS_NextEntity(entnum))
	{
		int modelindex = AAS_EntityModelindex(entnum);
		if (modelindex == 0)
		{
			continue;
		}

		aas_entityinfo_t entityinfo;
		AAS_EntityInfo(entnum, &entityinfo);
		if (!BotGoal_VectorEquals(entityinfo.origin, entityinfo.lastvisorigin))
		{
			continue;
		}

		bool handled = false;
		for (bot_levelitem_t *item = g_levelitem_head; item != NULL; item = item->next)
		{
			const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
			if (adapter == NULL || !adapter->raw_item)
			{
				continue;
			}
			const bot_itemdef_t *itemdef = BotGoal_ItemDefForLevelItem(item);
			if (itemdef == NULL || itemdef->modelindex != modelindex)
			{
				continue;
			}

			if (item->entitynum == 0)
			{
				vec3_t delta;
				VectorSubtract(item->origin, entityinfo.origin, delta);
				if (sqrtf(DotProduct(delta, delta)) >= BOT_GOAL_ENTITY_LINK_DISTANCE)
				{
					continue;
				}

				item->entitynum = entnum;
				VectorCopy(entityinfo.origin, item->origin);
				/*
				 * 0x1002fba5 forms ic->items[EBX] here, NOT
				 * ic->items[li->iteminfo] - the index the match at 0x1002fb18
				 * used.  Every shipped iteminfo carries the same
				 * {-15,-15,-15}/{15,15,15} box, so a stale but in-range index
				 * is unobservable; the reachable case is the search having
				 * left it at numitems, which selects the zeroed spare slot.
				 */
				vec3_t reusedmins;
				vec3_t reusedmaxs;
				if (g_itemdefs != NULL &&
					reused_iteminfo >= 0 &&
					reused_iteminfo < g_itemdef_capacity)
				{
					VectorCopy(g_itemdefs[reused_iteminfo].mins, reusedmins);
					VectorCopy(g_itemdefs[reused_iteminfo].maxs, reusedmaxs);
				}
				else
				{
					VectorClear(reusedmins);
					VectorClear(reusedmaxs);
				}
				item->areanum = AAS_BestReachableArea(item->origin,
					reusedmins,
					reusedmaxs,
					item->goalorigin);
			}
			else
			{
				if (item->entitynum != entnum)
				{
					continue;
				}
				VectorCopy(entityinfo.origin, item->origin);
			}
			handled = true;
			break;
		}

		if (handled)
		{
			continue;
		}

		/*
		 * 0x1002fbef-0x1002fc0d walks the itemconfig by modelindex and leaves
		 * the index in EBX whether or not it matched.  The not-found value
		 * (== numitems) is load-bearing: it is what makes the link branch above
		 * read the zeroed spare slot on a later iteration.
		 */
		for (reused_iteminfo = 0;
			reused_iteminfo < g_itemdef_count;
			++reused_iteminfo)
		{
			if (g_itemdefs[reused_iteminfo].modelindex == modelindex)
			{
				break;
			}
		}
		if (reused_iteminfo >= g_itemdef_count)
		{
			continue;
		}
		const bot_itemdef_t *itemdef = &g_itemdefs[reused_iteminfo];

		bot_levelitem_t *item = BotGoal_AllocLevelItem();
		if (item == NULL)
		{
			return;
		}

		bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapter(item);
		if (adapter == NULL)
		{
			return;
		}
		memset(adapter, 0, sizeof(*adapter));
		item->number = g_static_levelitem_count + entnum;
		item->iteminfo = (int)(itemdef - g_itemdefs);
		VectorCopy(entityinfo.origin, item->origin);
		item->areanum = AAS_BestReachableArea(entityinfo.origin,
			itemdef->mins,
			itemdef->maxs,
			item->goalorigin);
		item->entitynum = entnum;
		item->timeout = AAS_Time() + BOT_GOAL_DROPPED_TIMEOUT;
		strncpy(adapter->classname, itemdef->classname,
			sizeof(adapter->classname) - 1);
		adapter->classname[sizeof(adapter->classname) - 1] = '\0';
		VectorCopy(itemdef->mins, adapter->mins);
		VectorCopy(itemdef->maxs, adapter->maxs);
		adapter->respawntime = itemdef->respawntime;
		adapter->goalflags = GFL_ITEM;
		adapter->valid = true;
		adapter->raw_item = true;
		BotGoal_ActivateLevelItem(item);
	}
}

/*
=============
BotUpdateEntityItemsThrottled

Runs the dynamic item refresh through the retail one-second BotAI tick gate.
=============
*/
void BotUpdateEntityItemsThrottled(float now)
{
	(void)now;
	if (AAS_Time() <= g_next_entity_item_update_time)
	{
		return;
	}

	BotUpdateEntityItems();
	g_next_entity_item_update_time = AAS_Time() + 1.0f;
}

/*
=============
BotGoal_LevelItemSelectable

Raw Gladiator map/dropped records are selected from the active list without an
entity-link guard. The successor-style unlinked-static/roam guard remains only
for direct compatibility registrations.
=============
*/
static bool BotGoal_LevelItemSelectable(const bot_levelitem_t *item)
{
	const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
	if (item == NULL || adapter == NULL || !adapter->valid)
	{
		return false;
	}

	if (adapter->raw_item)
	{
		return true;
	}

	if (item->entitynum == 0 && (adapter->goalflags & GFL_ROAM) == 0)
	{
		return false;
	}

	return true;
}

/*
=============
BotGoal_LevelItemScore
=============
*/
static double BotGoal_LevelItemScore(bot_goalstate_t *gs,
	const bot_levelitem_t *item,
	int start_area,
	const int *inventory,
	int travelflags,
	int *travel_time,
	bool require_travel)
{
	const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
	if (item == NULL || adapter == NULL || !adapter->valid)
	{
		return -DBL_MAX;
	}

	if (!BotGoal_LevelItemAllowed(item))
	{
		return -DBL_MAX;
	}

	if (item->areanum <= 0)
	{
		return -DBL_MAX;
	}

	float fuzzy_weight = BotGoal_EvaluateItemWeight(gs, inventory, item);
	float weight = BotGoal_ItemBaseWeight(item, fuzzy_weight);
	if (weight <= 0.0f)
	{
		return -DBL_MAX;
	}

	int time = 0;
	if (start_area > 0 && item->areanum > 0)
	{
		time = (int)(uint16_t)AAS_AreaTravelTimeToGoalArea(start_area,
			NULL,
			item->areanum,
			travelflags);
	}

	if (travel_time != NULL)
	{
		*travel_time = time;
	}

	if (require_travel && time <= 0)
	{
		return -DBL_MAX;
	}

	double score = (double)weight;
	if (time > 0)
	{
		score /= (double)time * 0.01;
	}

	if (item->timeout != 0.0f)
	{
		score += BOT_GOAL_DROPPED_SCORE_BONUS;
	}

	return score;
}

/*
=============
BotChooseLTGItem

Choose a retail long-term item goal or fall back to a random roam goal.
=============
*/
int BotChooseLTGItem(int handle, const vec3_t origin, const int *inventory, int travelflags)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
		return 0;
	}

	if (gs->itemweightconfig == NULL)
	{
		return 0;
	}

	int start_area = BotGoal_StartAreaForState(gs, origin);
	if (start_area <= 0)
	{
		return 0;
	}

	/*
	 * Retail samples the global item config before walking the level-item list
	 * and returns zero when it is absent. The roam fallback lives inside that
	 * branch, so a library without an item config never produces a roam goal.
	 */
	if (!g_itemdefs_loaded)
	{
		return 0;
	}

	float now = BotGoal_CurrentTime();
	float best_score = 0.0f;
	const bot_levelitem_t *best_item = NULL;
	bot_goal_t best_goal = {0};

	for (const bot_levelitem_t *item = g_levelitem_head; item != NULL; item = item->next)
	{
		if (BotGoal_AvoidGoalTimeForState(gs, item->number) > 0.0f)
		{
			continue;
		}

		if (!BotGoal_LevelItemSelectable(item))
		{
			continue;
		}

		int travel_time = 0;
		double score = BotGoal_LevelItemScore(gs,
			item,
			start_area,
			inventory,
			travelflags,
			&travel_time,
			true);
		if (score <= -DBL_MAX)
		{
			continue;
		}

		float travel_seconds = (float)travel_time * 0.009f;
		const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
		if (adapter != NULL && !adapter->raw_item &&
			adapter->next_respawn_time > now + travel_seconds)
		{
			continue;
		}

		if (score <= best_score)
		{
			continue;
		}

		best_score = score;
		best_item = item;
		BotGoal_CopySelectedItemGoal(item, &best_goal);
	}

	if (best_item == NULL)
	{
		if (!AAS_RandomGoalArea(start_area,
			travelflags,
			&best_goal.areanum,
			best_goal.origin))
		{
			return 0;
		}

		VectorSet(best_goal.mins, -15.0f, -15.0f, -15.0f);
		VectorSet(best_goal.maxs, 15.0f, 15.0f, 15.0f);
		best_goal.entitynum = 0;
		best_goal.number = 0;
		best_goal.flags = GFL_ROAM;
		best_goal.iteminfo = 0;
		(void)BotPushGoal(handle, &best_goal);
		return 1;
	}

	BotAddToAvoidGoals(handle, best_item->number, BotGoal_AvoidTimeForItem(best_item));
	(void)BotPushGoal(handle, &best_goal);
	return 1;
}

/*
=============
BotChooseNBGItem

Choose a retail nearby item goal within the travel and detour limits.
=============
*/
int BotChooseNBGItem(int handle,
	const vec3_t origin,
	const int *inventory,
	int travelflags,
	const bot_goal_t *ltg,
	float maxtime)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
		return 0;
	}

	if (gs->itemweightconfig == NULL)
	{
		return 0;
	}

	int start_area = BotGoal_StartAreaForState(gs, origin);
	if (start_area <= 0)
	{
		return 0;
	}

	/*
	 * The retail leaf resolves the direct long-term travel time before it
	 * samples the global item config, then returns zero when that config is
	 * absent.
	 */
	int ltg_time = 99999;
	if (ltg != NULL)
	{
		ltg_time = (int)(uint16_t)AAS_AreaTravelTimeToGoalArea(start_area,
			NULL,
			ltg->areanum,
			travelflags);
	}

	if (!g_itemdefs_loaded)
	{
		return 0;
	}

	float now = BotGoal_CurrentTime();
	float best_score = 0.0f;
	const bot_levelitem_t *best_item = NULL;
	bot_goal_t best_goal = {0};

	for (const bot_levelitem_t *item = g_levelitem_head; item != NULL; item = item->next)
	{
		if (BotGoal_AvoidGoalTimeForState(gs, item->number) > 0.0f)
		{
			continue;
		}

		if (!BotGoal_LevelItemSelectable(item))
		{
			continue;
		}

		int travel_time = 0;
		double score = BotGoal_LevelItemScore(gs,
			item,
			start_area,
			inventory,
			travelflags,
			&travel_time,
			true);
		if (score <= -DBL_MAX)
		{
			continue;
		}

		if ((float)travel_time >= maxtime)
		{
			continue;
		}

		float travel_seconds = (float)travel_time * 0.009f;
		const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
		if (adapter != NULL && !adapter->raw_item &&
			adapter->next_respawn_time > now + travel_seconds)
		{
			continue;
		}

		if (score <= best_score)
		{
			continue;
		}

		if (ltg != NULL)
		{
			int return_time = (int)(uint16_t)AAS_AreaTravelTimeToGoalArea(item->areanum,
				NULL,
				ltg->areanum,
				travelflags);
			if (return_time > ltg_time)
			{
				continue;
			}
		}

		best_score = score;
		best_item = item;
		BotGoal_CopySelectedItemGoal(item, &best_goal);
	}

	if (best_item == NULL)
	{
		return 0;
	}

	BotAddToAvoidGoals(handle, best_item->number, BotGoal_AvoidTimeForItem(best_item));
	(void)BotPushGoal(handle, &best_goal);
	return 1;
}

/*
=============
BotGoal_EvaluateStackGoal
=============
*/
float BotGoal_EvaluateStackGoal(int handle,
	const bot_goal_t *goal,
	const vec3_t origin,
	int start_area,
	const int *inventory,
	int travelflags,
	int *travel_time)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL || goal == NULL)
	{
		if (travel_time != NULL)
		{
			*travel_time = 0;
		}
		return -FLT_MAX;
	}

	const bot_levelitem_t *item = BotGoal_FindLevelItem(goal->number);
	int start = start_area;
	if (start <= 0)
	{
		start = BotGoal_PointAreaNum(origin);
	}

	int computed_travel = 0;
	float score = -FLT_MAX;

	if (item != NULL)
	{
		score = BotGoal_LevelItemScore(gs,
			item,
			start,
			inventory,
			travelflags,
			&computed_travel,
			false);
	}
	else if (start > 0 && goal->areanum > 0)
	{
		vec3_t start_point;
		VectorCopy(origin, start_point);
		computed_travel = AAS_AreaTravelTimeToGoalArea(start, start_point, goal->areanum, travelflags);
		score = (goal->number != 0) ? 1.0f : 0.0f;
	}

	if (travel_time != NULL)
	{
		*travel_time = computed_travel;
	}

	return score;
}

/*
=============
BotTouchingGoal

Test the player origin against the retail safety-shrunk contact bounds.
=============
*/
int BotTouchingGoal(const vec3_t origin, const bot_goal_t *goal)
{
	if (origin == NULL || goal == NULL)
	{
		return 0;
	}

	const vec3_t boxmins = {-16.0f, -16.0f, -24.0f};
	const vec3_t boxmaxs = {16.0f, 16.0f, 32.0f};
	const vec3_t safety_mins = {-4.0f, -4.0f, 0.0f};
	const vec3_t safety_maxs = {4.0f, 4.0f, 10.0f};

	for (int i = 0; i < 3; ++i)
	{
		float absmin = goal->origin[i] + goal->mins[i] - boxmaxs[i] - safety_mins[i];
		float absmax = goal->origin[i] + goal->maxs[i] - boxmins[i] - safety_maxs[i];
		if (origin[i] < absmin || origin[i] > absmax)
		{
			return 0;
		}
	}

	return 1;
}

/*
=============
BotItemGoalInVisButNotVisible

Checks for item goals that are in AAS visibility but blocked by a trace.
=============
*/
int BotItemGoalInVisButNotVisible(int viewer, vec3_t eye, vec3_t viewangles, bot_goal_t *goal)
{
	if (goal == NULL || eye == NULL)
	{
		return 0;
	}

	if ((goal->flags & GFL_ITEM) == 0)
	{
		return 0;
	}

	vec3_t middle;
	VectorCopy(goal->mins, middle);
	VectorAdd(goal->origin, middle, middle);

	vec3_t mins = {0.0f, 0.0f, 0.0f};
	vec3_t maxs = {0.0f, 0.0f, 0.0f};
	bsp_trace_t trace = Q2_Trace(eye, mins, maxs, middle, viewer, MASK_SOLID);
	if (trace.fraction >= 1.0f)
	{
		int entnum = goal->entitynum;
		if (entnum <= 0)
		{
			return 1;
		}

		aas_entityinfo_t entityinfo;
		AAS_EntityInfo(entnum, &entityinfo);
		if (!entityinfo.valid)
		{
			return 1;
		}
	}

	(void)viewangles;
	return 0;
}

/*
=============
BotGoal_StrIcmp
=============
*/
static int BotGoal_StrIcmp(const char *lhs, const char *rhs)
{
	if (lhs == rhs)
	{
		return 0;
	}
	if (lhs == NULL)
	{
		return -1;
	}
	if (rhs == NULL)
	{
		return 1;
	}

	while (*lhs != '\0' && *rhs != '\0')
	{
		int lc = tolower((unsigned char)*lhs);
		int rc = tolower((unsigned char)*rhs);
		if (lc != rc)
		{
			return lc - rc;
		}
		++lhs;
		++rhs;
	}

	return tolower((unsigned char)*lhs) - tolower((unsigned char)*rhs);
}

/*
=============
BotGoal_CurrentGameType
=============
*/
static int BotGoal_CurrentGameType(void)
{
	float value = LibVarValue("g_gametype", "");
	if (value == 0.0f)
	{
		value = LibVarValue("gametype", "0");
	}
	return (int)value;
}

/*
=============
BotGoal_NotSpawnFlags
=============
*/
static int BotGoal_NotSpawnFlags(void)
{
	return (int)LibVarValue("notspawnflags", "2048");
}

/*
=============
BotGoal_LevelItemAllowed
=============
*/
static bool BotGoal_LevelItemAllowed(const bot_levelitem_t *item)
{
	const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
	if (item == NULL || adapter == NULL || !adapter->valid)
	{
		return false;
	}
	if (adapter->raw_item)
	{
		return true;
	}

	if (adapter->itemflags & BOT_GOAL_ITEMFLAG_NOTBOT)
	{
		return false;
	}

	int gametype = BotGoal_CurrentGameType();
	if (gametype == 2)
	{
		if (adapter->itemflags & BOT_GOAL_ITEMFLAG_NOTSINGLE)
		{
			return false;
		}
	}
	else if (gametype >= 3)
	{
		if (adapter->itemflags & BOT_GOAL_ITEMFLAG_NOTTEAM)
		{
			return false;
		}
	}
	else
	{
		if (adapter->itemflags & BOT_GOAL_ITEMFLAG_NOTFREE)
		{
			return false;
		}
	}

	return true;
}

/*
=============
BotGoal_CopyLevelItemGoal

Builds the complete goal record used by item selection.
=============
*/
static void BotGoal_CopyLevelItemGoal(const bot_levelitem_t *item,
	const bot_itemdef_t *itemdef,
	bot_goal_t *goal)
{
	const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
	memset(goal, 0, sizeof(*goal));
	goal->areanum = item->areanum;
	VectorCopy(item->goalorigin, goal->origin);
	goal->entitynum = item->entitynum;
	if (itemdef != NULL)
	{
		VectorCopy(itemdef->mins, goal->mins);
		VectorCopy(itemdef->maxs, goal->maxs);
	}
	else
	{
		VectorCopy(adapter->mins, goal->mins);
		VectorCopy(adapter->maxs, goal->maxs);
	}
	goal->number = item->number;
	goal->flags = GFL_ITEM;
	if (adapter != NULL && !adapter->raw_item && item->timeout > 0.0f)
	{
		goal->flags |= GFL_DROPPED;
	}
	if (adapter != NULL && !adapter->raw_item && (adapter->goalflags & GFL_ROAM))
	{
		goal->flags |= GFL_ROAM;
	}
	goal->iteminfo = item->iteminfo;
}

/*
=============
BotGoal_CopyLevelItemGoalForLookup

Mirrors sub_1002f890's partial public-goal write. The retail getter deliberately
leaves the caller's flags and iteminfo fields untouched.
=============
*/
static void BotGoal_CopyLevelItemGoalForLookup(const bot_levelitem_t *item,
	const bot_itemdef_t *itemdef,
	bot_goal_t *goal)
{
	const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
	goal->areanum = item->areanum;
	VectorCopy(item->goalorigin, goal->origin);
	goal->entitynum = item->entitynum;
	if (itemdef != NULL)
	{
		VectorCopy(itemdef->mins, goal->mins);
		VectorCopy(itemdef->maxs, goal->maxs);
	}
	else
	{
		VectorCopy(adapter->mins, goal->mins);
		VectorCopy(adapter->maxs, goal->maxs);
	}
	goal->number = item->number;
}

/*
=============
BotGoal_CopySelectedItemGoal

Build the goal record pushed by LTG/NBG item selection.
=============
*/
static void BotGoal_CopySelectedItemGoal(const bot_levelitem_t *item, bot_goal_t *goal)
{
	const bot_itemdef_t *itemdef = BotGoal_ItemDefForLevelItem(item);
	BotGoal_CopyLevelItemGoal(item, itemdef, goal);
}

/*
=============
BotGoal_ReadSignedFloat
=============
*/
static bool BotGoal_ReadSignedFloat(pc_source_t *source, float *out)
{
	pc_token_t token;
	bool negative = false;

	if (source == NULL || out == NULL)
	{
		return false;
	}

	if (!PC_ExpectAnyToken(source, &token))
	{
		return false;
	}

	if (token.type == TT_PUNCTUATION)
	{
		if (token.subtype != P_SUB)
		{
			SourceError(source, "unexpected punctuation %s", token.string);
			return false;
		}
		negative = true;
		if (!PC_ExpectAnyToken(source, &token))
		{
			return false;
		}
	}

	if (token.type != TT_NUMBER)
	{
		SourceError(source, "expected number, found %s", token.string);
		return false;
	}

	float value = (token.subtype & TT_FLOAT) ? (float)token.floatvalue : (float)token.intvalue;
	*out = negative ? -value : value;
	return true;
}

/*
=============
BotGoal_ReadSignedInt

Read the signed integer representation used by retail item type/index fields.
=============
*/
static bool BotGoal_ReadSignedInt(pc_source_t *source, int *out)
{
	pc_token_t token;
	bool negative = false;

	if (source == NULL || out == NULL)
	{
		return false;
	}

	if (!PC_ExpectAnyToken(source, &token))
	{
		return false;
	}

	if (token.type == TT_PUNCTUATION)
	{
		if (token.subtype != P_SUB)
		{
			SourceError(source, "unexpected punctuation %s", token.string);
			return false;
		}
		negative = true;
		if (!PC_ExpectAnyToken(source, &token))
		{
			return false;
		}
	}

	if (token.type != TT_NUMBER)
	{
		SourceError(source, "expected number, found %s", token.string);
		return false;
	}
	if ((token.subtype & TT_INTEGER) == 0)
	{
		SourceError(source, "unexpected float");
		return false;
	}

	int64_t value = (int64_t)token.intvalue;
	if (negative)
	{
		value = -value;
	}
	if (value < -32768 || value > 32767)
	{
		SourceError(source, "value %d out of range [%d, %d]",
			(int)value,
			-32768,
			32767);
		return false;
	}

	*out = (int)value;
	return true;
}

/*
=============
BotGoal_ReadVector
=============
*/
static bool BotGoal_ReadVector(pc_source_t *source, vec3_t out)
{
	pc_token_t token;

	if (source == NULL || out == NULL)
	{
		return false;
	}

	if (!PC_ExpectTokenType(source, TT_PUNCTUATION, P_BRACEOPEN, &token))
	{
		return false;
	}

	for (int axis = 0; axis < 3; ++axis)
	{
		if (PC_CheckTokenString(source, "}"))
		{
			return true;
		}
		if (!BotGoal_ReadSignedFloat(source, &out[axis]))
		{
			return false;
		}

		if (!PC_ExpectAnyToken(source, &token))
		{
			return false;
		}
		if (token.type == TT_PUNCTUATION && token.subtype == P_BRACECLOSE)
		{
			return true;
		}
		if (token.type != TT_PUNCTUATION || token.subtype != P_COMMA)
		{
			SourceError(source, "expected a comma, found %s", token.string);
			return false;
		}
	}

	return true;
}

/*
=============
BotGoal_FindItemDef
=============
*/
static bot_itemdef_t *BotGoal_FindItemDef(const char *classname)
{
	if (classname == NULL || classname[0] == '\0')
	{
		return NULL;
	}

	for (int i = 0; i < g_itemdef_count; ++i)
	{
		if (BotGoal_StrIcmp(g_itemdefs[i].classname, classname) == 0)
		{
			return &g_itemdefs[i];
		}
	}

	return NULL;
}

/*
=============
BotGoal_FindItemDefExact

Uses the raw BotInitLevelItems classname comparison, which is case-sensitive
despite the wider compatibility lookup surface accepting case variants.
=============
*/
static bot_itemdef_t *BotGoal_FindItemDefExact(const char *classname)
{
	if (classname == NULL || classname[0] == '\0')
	{
		return NULL;
	}

	for (int i = 0; i < g_itemdef_count; ++i)
	{
		if (strcmp(g_itemdefs[i].classname, classname) == 0)
		{
			return &g_itemdefs[i];
		}
	}

	return NULL;
}

/*
=============
BotGoal_FindItemDefByModelIndex
=============
*/
static bot_itemdef_t *BotGoal_FindItemDefByModelIndex(int modelindex)
{
	if (modelindex <= 0)
	{
		return NULL;
	}

	for (int i = 0; i < g_itemdef_count; ++i)
	{
		if (g_itemdefs[i].modelindex == modelindex)
		{
			return &g_itemdefs[i];
		}
	}

	return NULL;
}

/*
=============
BotGoal_RegisterItemDef
=============
*/
static bot_itemdef_t *BotGoal_RegisterItemDef(const char *classname)
{
	/*
	 * The retail loader copies the token following iteminfo without testing it
	 * for content, so an explicitly empty classname is a valid declaration.
	 */
	if (classname == NULL)
	{
		return NULL;
	}

	if (g_itemdefs == NULL || g_itemdef_count >= g_itemdef_capacity)
	{
		return NULL;
	}

	bot_itemdef_t *itemdef = &g_itemdefs[g_itemdef_count];
	memset(itemdef, 0, sizeof(*itemdef));
	strncpy(itemdef->classname, classname, sizeof(itemdef->classname) - 1);
	itemdef->classname[sizeof(itemdef->classname) - 1] = '\0';
	itemdef->number = g_itemdef_count;
	++g_itemdef_count;
	g_itemconfig->numiteminfo = g_itemdef_count;
	return itemdef;
}

/*
=============
BotGoal_ParseItemInfoBlock
=============
*/
static bool BotGoal_ParseItemInfoBlock(pc_source_t *source, bot_itemdef_t *itemdef)
{
	pc_token_t token;

	if (source == NULL || itemdef == NULL)
	{
		return false;
	}

	if (!PC_ExpectTokenType(source, TT_PUNCTUATION, P_BRACEOPEN, &token))
	{
		return false;
	}

	while (PC_ReadToken(source, &token))
	{
		if (token.type == TT_PUNCTUATION && token.subtype == P_BRACECLOSE)
		{
			return true;
		}

		if (token.type == TT_NAME &&
			(strcmp(token.string, "name") == 0 || strcmp(token.string, "model") == 0))
		{
			pc_token_t value_token;
			if (!PC_ExpectTokenType(source, TT_STRING, 0, &value_token))
			{
				return false;
			}
			StripDoubleQuotes(value_token.string);
			char *destination = strcmp(token.string, "name") == 0 ?
				itemdef->name : itemdef->model;
			strncpy(destination, value_token.string, 79);
			destination[79] = '\0';
			continue;
		}

		if (token.type == TT_NAME &&
			(strcmp(token.string, "type") == 0 || strcmp(token.string, "index") == 0))
		{
			int value = 0;
			if (!BotGoal_ReadSignedInt(source, &value))
			{
				return false;
			}
			if (strcmp(token.string, "type") == 0)
			{
				itemdef->type = value;
			}
			else
			{
				itemdef->index = value;
			}
			continue;
		}

		if (token.type == TT_NAME && strcmp(token.string, "respawntime") == 0)
		{
			float value = 0.0f;
			if (!BotGoal_ReadSignedFloat(source, &value))
			{
				return false;
			}
			itemdef->respawntime = value;
			continue;
		}

		if (token.type == TT_NAME && strcmp(token.string, "mins") == 0)
		{
			if (!BotGoal_ReadVector(source, itemdef->mins))
			{
				return false;
			}
			continue;
		}

		if (token.type == TT_NAME && strcmp(token.string, "maxs") == 0)
		{
			if (!BotGoal_ReadVector(source, itemdef->maxs))
			{
				return false;
			}
			continue;
		}

		SourceError(source, "unknown structure field %s", token.string);
		return false;
	}

	SourceError(source, "couldn't read expected token");
	return false;
}

/*
=============
BotGoal_ClearItemDefs

Releases the retail-sized item-definition table before a reload or shutdown.
=============
*/
static void BotGoal_ClearItemDefs(void)
{
	if (g_itemconfig != NULL)
	{
		FreeMemory(g_itemconfig);
	}

	g_itemconfig = NULL;
	g_itemdefs = NULL;
	g_itemdef_count = 0;
	g_itemdef_capacity = 0;
	g_itemdefs_loaded = false;
}

/*
=============
BotGoal_BeginItemDefLoad

Allocates the iteminfo table from the retail max_iteminfo libvar contract.
=============
*/
static bool BotGoal_BeginItemDefLoad(int capacity)
{
	BotGoal_ClearItemDefs();
	g_itemdef_capacity = capacity;
	if ((size_t)capacity >
		(SIZE_MAX - sizeof(*g_itemconfig)) / sizeof(*g_itemdefs))
	{
		return false;
	}

	size_t allocation_size = sizeof(*g_itemconfig) +
		(size_t)capacity * sizeof(*g_itemdefs);
	g_itemconfig = GetClearedMemory(allocation_size);
	if (g_itemconfig == NULL)
	{
		return false;
	}

	g_itemdefs = (bot_itemdef_t *)(g_itemconfig + 1);
	g_itemconfig->iteminfo = (uint32_t)(uintptr_t)g_itemdefs;
	return true;
}

/*
=============
BotGoal_AbandonItemDefs

Matches retail's direct global pointer overwrite on repeated setup. The old
block remains owned by the botlib arena and is reclaimed with that arena.
=============
*/
static void BotGoal_AbandonItemDefs(void)
{
	g_itemconfig = NULL;
	g_itemdefs = NULL;
	g_itemdef_count = 0;
	g_itemdef_capacity = 0;
	g_itemdefs_loaded = false;
}

/*
=============
BotGoal_ClearLevelItems

Releases the dynamic retail level-item pool and external compatibility names.
=============
*/
static void BotGoal_ClearLevelItems(void)
{
	if (g_levelitems != NULL)
	{
		FreeMemory(g_levelitems);
		g_levelitems = NULL;
	}
	if (g_levelitem_adapters != NULL)
	{
		FreeMemory(g_levelitem_adapters);
		g_levelitem_adapters = NULL;
	}
	if (g_extra_iteminfo_names != NULL)
	{
		FreeMemory(g_extra_iteminfo_names);
		g_extra_iteminfo_names = NULL;
	}

	g_levelitem_head = NULL;
	g_levelitem_free = NULL;
	g_levelitem_capacity = 0;
	g_static_levelitem_count = 0;
	g_levelitem_scan_aborted = false;
	g_extra_iteminfo_count = 0;
	g_extra_iteminfo_capacity = 0;
	g_next_levelitem_number = 1;
}

/*
=============
BotGoal_BeginLevelItemLoad

Creates the retail max_levelitems pool used for the current map's items.
=============
*/
static bool BotGoal_BeginLevelItemLoad(void)
{
	int capacity = (int)LibVarValue("max_levelitems", "512");
	g_levelitem_capacity = capacity;
	g_extra_iteminfo_capacity = capacity;
	if (capacity <= 0)
	{
		g_levelitem_capacity = 0;
		g_extra_iteminfo_capacity = 0;
		return true;
	}
	if ((size_t)capacity > SIZE_MAX / sizeof(*g_levelitems) ||
		(size_t)capacity > SIZE_MAX / sizeof(*g_levelitem_adapters) ||
		(size_t)capacity > SIZE_MAX / sizeof(*g_extra_iteminfo_names))
	{
		return false;
	}

	g_levelitems = GetMemory((size_t)capacity * sizeof(*g_levelitems));
	g_levelitem_adapters = GetClearedMemory((size_t)capacity * sizeof(*g_levelitem_adapters));
	g_extra_iteminfo_names = GetClearedMemory((size_t)capacity * sizeof(*g_extra_iteminfo_names));
	if (g_levelitems == NULL || g_levelitem_adapters == NULL ||
		g_extra_iteminfo_names == NULL)
	{
		BotGoal_ClearLevelItems();
		return false;
	}
	/*
	 * Retail source shape. Its link loop stops two records short of the pool,
	 * so record capacity - 2 stays reachable from the free list with an
	 * uninitialized next pointer while the terminator lands on the detached
	 * final record. Every reachable link below matches retail; the single
	 * substitution is the explicit terminator on record capacity - 2, which
	 * replaces retail's read of uninitialized GetMemory bytes with the value a
	 * zeroed heap would have supplied. The usable slot count is unchanged.
	 */
	for (int i = 0; i < capacity - 2; ++i)
	{
		g_levelitems[i].next = &g_levelitems[i + 1];
	}
	g_levelitems[capacity - 1].next = NULL;
	if (capacity >= 2)
	{
		g_levelitems[capacity - 2].next = NULL;
	}
	g_levelitem_free = g_levelitems;

	return true;
}

/*
=============
BotGoal_ClearLevelItemState
=============
*/
static void BotGoal_ClearLevelItemState(void)
{
	BotGoal_ClearLevelItems();
	memset(g_maplocations, 0, sizeof(g_maplocations));
	g_maplocation_count = 0;
	memset(g_campspots, 0, sizeof(g_campspots));
	g_campspot_count = 0;
}

/*
=============
BotGoal_ForgetLevelItemAllocations

Forgets map-owned pointers after the library allocator has released all blocks.
This preserves the retail goal shutdown leaf while allowing a later setup in
the same host process.
=============
*/
void BotGoal_ForgetLevelItemAllocations(void)
{
	memset(g_goalstates, 0, sizeof(g_goalstates));
	memset(g_goalstate_adapters, 0, sizeof(g_goalstate_adapters));
	g_itemconfig = NULL;
	g_itemdefs = NULL;
	g_itemdef_count = 0;
	g_itemdef_capacity = 0;
	g_itemdefs_loaded = false;
	g_levelitems = NULL;
	g_levelitem_adapters = NULL;
	g_levelitem_head = NULL;
	g_levelitem_free = NULL;
	g_levelitem_capacity = 0;
	g_static_levelitem_count = 0;
	g_next_levelitem_number = 1;
	g_levelitem_scan_aborted = false;
	g_extra_iteminfo_names = NULL;
	g_extra_iteminfo_count = 0;
	g_extra_iteminfo_capacity = 0;
	memset(g_maplocations, 0, sizeof(g_maplocations));
	g_maplocation_count = 0;
	memset(g_campspots, 0, sizeof(g_campspots));
	g_campspot_count = 0;
}

/*
=============
BotGoal_LoadItemDefs
=============
*/
static bool BotGoal_LoadItemDefs(void)
{
	const char *itemconfig_name = LibVarString("itemconfig", "items.c");
	if (itemconfig_name == NULL || itemconfig_name[0] == '\0')
	{
		itemconfig_name = "items.c";
	}

	int capacity = (int)LibVarValue("max_iteminfo", "256");
	if (capacity < 0)
	{
		BotLib_Print(PRT_ERROR, "max_iteminfo = %d\n", capacity);
		LibVarSet("max_iteminfo", "256");
		capacity = 256;
	}

	BotGoal_AbandonItemDefs();
	botlib_asset_resolution_t resolution;
	if (!BotLib_ResolveAssetPathDetailed(itemconfig_name,
		NULL,
		&resolution))
	{
		BotLib_Print(PRT_ERROR, "couldn't find %s\n", itemconfig_name);
		return false;
	}

	pc_source_t *source = PC_LoadSourceFile(itemconfig_name);
	if (source == NULL)
	{
		BotLib_Print(PRT_ERROR, "counldn't load %s\n", itemconfig_name);
		return false;
	}
	if (!BotGoal_BeginItemDefLoad(capacity))
	{
		PC_FreeSource(source);
		return false;
	}

	pc_token_t token;
	bool parsed = true;
	while (PC_ReadToken(source, &token))
	{
		if (strcmp(token.string, "iteminfo") != 0)
		{
			SourceError(source, "unknown definition %s\n", token.string);
			parsed = false;
			break;
		}

		/*
		 * Retail rejects an over-capacity config on the iteminfo keyword
		 * itself, before it consumes the classname token, so the capacity
		 * diagnostic wins over any later token error on the same definition.
		 */
		if (g_itemdefs == NULL || g_itemdef_count >= g_itemdef_capacity)
		{
			SourceError(source, "more than %d item info defined\n",
				g_itemdef_capacity);
			parsed = false;
			break;
		}

		pc_token_t classname_token;
		if (!PC_ExpectTokenType(source, TT_STRING, 0, &classname_token))
		{
			/*
			 * Deliberate deviation.  Retail's other three parse-loop exits
			 * release the parser with FreeSource, but this one calls
			 * FreeMemory on the source pointer instead (0x1002ee4x, both
			 * frees targeting 0x100390b0), so it reclaims only the 1624-byte
			 * source_t header and leaks the script stack, the file image, the
			 * pushed-back tokens, every define and the 4096-byte define hash.
			 * Reproducing that means deliberately leaking several KB on an
			 * error path whose only retail-visible effect is two diagnostic
			 * counters, so we take the shared full PC_FreeSource below.
			 */
			parsed = false;
			break;
		}
		StripDoubleQuotes(classname_token.string);

		bot_itemdef_t *itemdef = BotGoal_RegisterItemDef(classname_token.string);
		if (itemdef == NULL)
		{
			parsed = false;
			break;
		}

		if (!BotGoal_ParseItemInfoBlock(source, itemdef))
		{
			parsed = false;
			break;
		}
	}

	PC_FreeSource(source);
	if (!parsed)
	{
		BotGoal_ClearItemDefs();
		return false;
	}
	if (g_itemdef_count == 0)
	{
		BotLib_Print(PRT_WARNING, "no item info loaded\n");
	}
	if (resolution.pak_entry_length != 0)
	{
		BotLib_Print(PRT_MESSAGE, "loaded %s\\%s\n",
			resolution.source_path,
			itemconfig_name);
	}
	else
	{
		BotLib_Print(PRT_MESSAGE, "loaded %s\n", itemconfig_name);
	}
	g_itemdefs_loaded = true;
	return true;
}

/*
=============
BotGoal_AddMapLocation
=============
*/
static void BotGoal_AddMapLocation(const bot_goal_parsed_entity_t *entity)
{
	if (entity == NULL || !entity->has_origin)
	{
		return;
	}

	if (g_maplocation_count >= BOT_GOAL_MAX_MAPLOCATIONS)
	{
		return;
	}

	if (g_maplocation_count > 0)
	{
		memmove(&g_maplocations[1],
				&g_maplocations[0],
				sizeof(g_maplocations[0]) * (size_t)g_maplocation_count);
	}

	bot_maplocation_t *location = &g_maplocations[0];
	memset(location, 0, sizeof(*location));
	VectorCopy(entity->origin, location->origin);
	location->areanum = BotGoal_PointAreaNum(location->origin);
	if (entity->message[0] != '\0')
	{
		strncpy(location->name, entity->message, sizeof(location->name) - 1);
		location->name[sizeof(location->name) - 1] = '\0';
	}
	location->valid = true;
	++g_maplocation_count;
}

/*
=============
BotGoal_AddCampSpot
=============
*/
static void BotGoal_AddCampSpot(const bot_goal_parsed_entity_t *entity)
{
	if (entity == NULL || !entity->has_origin)
	{
		return;
	}

	if (g_campspot_count >= BOT_GOAL_MAX_CAMPSPOTS)
	{
		return;
	}

	bot_campspot_t new_spot;
	memset(&new_spot, 0, sizeof(new_spot));
	VectorCopy(entity->origin, new_spot.origin);
	new_spot.areanum = BotGoal_PointAreaNum(new_spot.origin);
	if (new_spot.areanum <= 0)
	{
		return;
	}

	bot_campspot_t *spot = &new_spot;

	if (entity->message[0] != '\0')
	{
		strncpy(spot->name, entity->message, sizeof(spot->name) - 1);
		spot->name[sizeof(spot->name) - 1] = '\0';
	}
	if (entity->has_range)
	{
		spot->range = entity->range;
	}
	if (entity->has_weight)
	{
		spot->weight = entity->weight;
	}
	if (entity->has_wait)
	{
		spot->wait = entity->wait;
	}
	if (entity->has_random)
	{
		spot->random = entity->random;
	}
	spot->valid = true;

	if (g_campspot_count > 0)
	{
		memmove(&g_campspots[1],
				&g_campspots[0],
				sizeof(g_campspots[0]) * (size_t)g_campspot_count);
	}
	g_campspots[0] = new_spot;
	++g_campspot_count;
}

/*
=============
BotGoal_AddRawLevelItemFromBSPEntity

Creates one retail level-item record from the reversed linked BSP entity list.
=============
*/
static void BotGoal_AddRawLevelItemFromBSPEntity(const aas_bspentity_t *entity,
	int notspawnflags)
{
	const char *classname = AAS_ValueForBSPEpairKey(entity, "classname");
	if (classname == NULL ||
		(AAS_IntForBSPEpairKey(entity, "spawnflags") & notspawnflags) != 0)
	{
		return;
	}

	bot_itemdef_t *itemdef = BotGoal_FindItemDefExact(classname);
	if (itemdef == NULL)
	{
		BotLib_LogWrite("entity %s unkown item", classname);
		return;
	}

	vec3_t origin;
	if (!AAS_VectorForBSPEpairKey(entity, "origin", origin))
	{
		BotLib_Print(PRT_ERROR, "item %s without origin\n", classname);
		return;
	}

	bot_levelitem_t *item = BotGoal_AllocLevelItem();
	if (item == NULL)
	{
		g_levelitem_scan_aborted = true;
		return;
	}

	item->number = ++g_static_levelitem_count;
	if (g_next_levelitem_number <= item->number)
	{
		g_next_levelitem_number = item->number + 1;
	}
	item->timeout = 0.0f;
	item->entitynum = 0;
	if (!AAS_DropToFloor(origin, itemdef->mins, itemdef->maxs))
	{
		BotLib_Print(PRT_MESSAGE,
			"%s in solid at (%1.1f %1.1f %1.1f)\n",
			classname,
			origin[0],
			origin[1],
			origin[2]);
	}
	item->iteminfo = (int)(itemdef - g_itemdefs);
	VectorCopy(origin, item->origin);
	item->areanum = AAS_BestReachableArea(origin,
		itemdef->mins,
		itemdef->maxs,
		item->goalorigin);

	bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapter(item);
	if (adapter != NULL)
	{
		memset(adapter, 0, sizeof(*adapter));
		strncpy(adapter->classname, itemdef->classname,
			sizeof(adapter->classname) - 1);
		adapter->classname[sizeof(adapter->classname) - 1] = '\0';
		VectorCopy(itemdef->mins, adapter->mins);
		VectorCopy(itemdef->maxs, adapter->maxs);
		adapter->respawntime = itemdef->respawntime;
		adapter->goalflags = GFL_ITEM;
		adapter->valid = true;
		adapter->raw_item = true;
	}
	BotGoal_ActivateLevelItem(item);
}

/*
=============
BotGoal_AddCompatibilityInfoEntity

Collects Q3 camp/location extensions only after the retail item scan.
=============
*/
static void BotGoal_AddCompatibilityInfoEntity(const aas_bspentity_t *entity)
{
	const char *classname = AAS_ValueForBSPEpairKey(entity, "classname");
	if (classname == NULL ||
		(BotGoal_StrIcmp(classname, "target_location") != 0 &&
		BotGoal_StrIcmp(classname, "info_camp") != 0))
	{
		return;
	}

	bot_goal_parsed_entity_t parsed;
	memset(&parsed, 0, sizeof(parsed));
	strncpy(parsed.classname, classname, sizeof(parsed.classname) - 1);
	parsed.has_classname = true;
	const char *message = AAS_ValueForBSPEpairKey(entity, "message");
	if (message != NULL)
	{
		strncpy(parsed.message, message, sizeof(parsed.message) - 1);
	}
	parsed.has_origin = AAS_VectorForBSPEpairKey(entity, "origin", parsed.origin) != 0;
	if (AAS_ValueForBSPEpairKey(entity, "range") != NULL)
	{
		parsed.has_range = true;
		parsed.range = AAS_FloatForBSPEpairKey(entity, "range");
	}
	if (AAS_ValueForBSPEpairKey(entity, "weight") != NULL)
	{
		parsed.has_weight = true;
		parsed.weight = AAS_FloatForBSPEpairKey(entity, "weight");
	}
	if (AAS_ValueForBSPEpairKey(entity, "wait") != NULL)
	{
		parsed.has_wait = true;
		parsed.wait = AAS_FloatForBSPEpairKey(entity, "wait");
	}
	if (AAS_ValueForBSPEpairKey(entity, "random") != NULL)
	{
		parsed.has_random = true;
		parsed.random = AAS_FloatForBSPEpairKey(entity, "random");
	}

	if (BotGoal_StrIcmp(classname, "target_location") == 0)
	{
		BotGoal_AddMapLocation(&parsed);
	}
	else
	{
		BotGoal_AddCampSpot(&parsed);
	}
}

/*
=============
BotGoal_AddCompatibilityInfoEntities

Visits compatibility-only entities in textual order after the reversed retail
item scan.
=============
*/
static void BotGoal_AddCompatibilityInfoEntities(const aas_bspentity_t *entity)
{
	if (entity == NULL)
	{
		return;
	}
	BotGoal_AddCompatibilityInfoEntities(entity->next);
	BotGoal_AddCompatibilityInfoEntity(entity);
}

/*
=============
BotGoalName

Copies the configured display name for the first matching active goal.
=============
*/
void BotGoalName(int number, char *name, int size)
{
	if (name == NULL || size <= 0)
	{
		return;
	}

	const bot_levelitem_t *item = BotGoal_FindLevelItem(number);
	if (item == NULL)
	{
		name[0] = '\0';
		return;
	}

	const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
	const bot_itemdef_t *itemdef = BotGoal_ItemDefForLevelItem(item);
	if (adapter != NULL && adapter->raw_item)
	{
		snprintf(name, (size_t)size, "%s", itemdef != NULL ? itemdef->name : "");
		return;
	}
	if (itemdef != NULL && itemdef->name[0] != '\0')
	{
		snprintf(name, (size_t)size, "%s", itemdef->name);
		return;
	}

	snprintf(name, (size_t)size, "%s", adapter != NULL ? adapter->classname : "");
}

/*
=============
BotDumpAvoidGoals

Writes active retail avoid entries without a heading.
=============
*/
void BotDumpAvoidGoals(int handle)
{
	const bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
		return;
	}

	for (int i = 0; i < BOT_GOAL_MAX_AVOID; ++i)
	{
		if (gs->avoidgoaltimes[i] >= AAS_Time())
		{
			char name[80];
			BotGoalName(gs->avoidgoals[i], name, sizeof(name));
			BotLib_LogWrite("avoid goal %s, number %d for %f seconds",
				name,
				gs->avoidgoals[i],
				gs->avoidgoaltimes[i] - AAS_Time());
		}
	}
}

/*
=============
BotDumpGoalStack

Writes the retail stack from the bottom upward without a heading.
=============
*/
void BotDumpGoalStack(int handle)
{
	const bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
		return;
	}

	for (int i = 1; i <= gs->goalstacktop; ++i)
	{
		char name[80];
		BotGoalName(gs->goalstack[i].number, name, sizeof(name));
		BotLib_LogWrite("%d: %s", i, name);
	}
}

/*
=============
BotGoal_RebuildWeightIndices
=============
*/
static void BotGoal_RebuildWeightIndices(bot_goalstate_t *gs)
{
	if (gs == NULL)
	{
		return;
	}

	if (!BotGoal_EnsureWeightCapacity(gs))
	{
		return;
	}

	if (gs->itemweightconfig == NULL || gs->itemweightindex == NULL)
	{
		return;
	}

	const bot_goalstate_adapter_t *adapter = BotGoalStateAdapterConst(gs);
	for (int i = 0; i < adapter->itemweightcount; ++i)
	{
		const char *classname = BotGoal_ItemInfoName(i);
		gs->itemweightindex[i] = (classname != NULL)
			? BotWeight_FindIndex(gs->itemweightconfig, classname)
			: -1;
	}
}

/*
=============
BotInitLevelItems

Retail parses its own BSP entity list here and never releases it. The separate
map-load leaf keeps a distinct global list that it does free before re-parsing,
so this list really is abandoned once per map load. The reconstruction keeps
that ownership exactly: releasing it here would hand every caller a different
heap profile than retail.
=============
*/
void BotInitLevelItems(void)
{
	BotGoal_ClearLevelItemState();
	if (!BotGoal_BeginLevelItemLoad())
	{
		BotLib_Print(PRT_FATAL, "out of level items\n");
		return;
	}
	if (!g_itemdefs_loaded)
	{
		return;
	}

	aas_bspentity_t *entities = AAS_LoadBSPEntities();
	int notspawnflags = BotGoal_NotSpawnFlags();
	BotGoal_ResolveItemDefModelIndexes();
	for (const aas_bspentity_t *entity = entities;
		entity != NULL && !g_levelitem_scan_aborted;
		entity = entity->next)
	{
		BotGoal_AddRawLevelItemFromBSPEntity(entity, notspawnflags);
	}
	BotGoal_AddCompatibilityInfoEntities(entities);
	if (!g_levelitem_scan_aborted)
	{
		BotLib_Print(PRT_MESSAGE, "found %d level items\n", g_static_levelitem_count);
	}
}

/*
=============
BotGetLevelItemGoal
=============
*/
int BotGetLevelItemGoal(int index, char *classname, bot_goal_t *goal)
{
	if (classname == NULL || goal == NULL)
	{
		return -1;
	}

	for (const bot_levelitem_t *item = g_levelitem_head; item != NULL; item = item->next)
	{
		if (item->number <= index)
		{
			continue;
		}

		const bot_levelitem_adapter_t *adapter = BotGoal_LevelItemAdapterConst(item);
		const bot_itemdef_t *itemdef = BotGoal_ItemDefForLevelItem(item);
		bool matched = false;
		if (adapter != NULL && adapter->raw_item)
		{
			matched = itemdef != NULL && BotGoal_StrIcmp(itemdef->name, classname) == 0;
		}
		else if (adapter != NULL)
		{
			if (BotGoal_StrIcmp(adapter->classname, classname) == 0)
			{
				matched = true;
			}
			else if (itemdef != NULL && itemdef->name[0] != '\0' &&
				BotGoal_StrIcmp(itemdef->name, classname) == 0)
			{
				matched = true;
			}
		}
		if (!matched)
		{
			continue;
		}

		BotGoal_CopyLevelItemGoalForLookup(item, itemdef, goal);
		return item->number;
	}

	return -1;
}

/*
=============
BotGetNextCampSpotGoal
=============
*/
int BotGetNextCampSpotGoal(int num, bot_goal_t *goal)
{
	if (goal == NULL)
	{
		return 0;
	}

	if (num < 0)
	{
		num = 0;
	}

	int index = num;
	for (int i = 0; i < g_campspot_count; ++i)
	{
		const bot_campspot_t *spot = &g_campspots[i];
		if (!spot->valid)
		{
			continue;
		}

		if (--index >= 0)
		{
			continue;
		}

		memset(goal, 0, sizeof(*goal));
		goal->areanum = spot->areanum;
		VectorCopy(spot->origin, goal->origin);
		goal->entitynum = 0;
		VectorSet(goal->mins, -8.0f, -8.0f, -8.0f);
		VectorSet(goal->maxs, 8.0f, 8.0f, 8.0f);
		return num + 1;
	}

	return 0;
}

/*
=============
BotGetMapLocationGoal
=============
*/
int BotGetMapLocationGoal(char *name, bot_goal_t *goal)
{
	if (name == NULL || name[0] == '\0' || goal == NULL)
	{
		return 0;
	}

	for (int i = 0; i < g_maplocation_count; ++i)
	{
		const bot_maplocation_t *location = &g_maplocations[i];
		if (!location->valid)
		{
			continue;
		}

		if (BotGoal_StrIcmp(location->name, name) != 0)
		{
			continue;
		}

		memset(goal, 0, sizeof(*goal));
		goal->areanum = location->areanum;
		VectorCopy(location->origin, goal->origin);
		goal->entitynum = 0;
		VectorSet(goal->mins, -8.0f, -8.0f, -8.0f);
		VectorSet(goal->maxs, 8.0f, 8.0f, 8.0f);
		return 1;
	}

	return 0;
}

/*
=============
BotInterbreedGoalFuzzyLogic
=============
*/
void BotInterbreedGoalFuzzyLogic(int parent1, int parent2, int child)
{
	bot_goalstate_t *first = BotGoalStateFromHandle(parent1);
	bot_goalstate_t *second = BotGoalStateFromHandle(parent2);
	bot_goalstate_t *out = BotGoalStateFromHandle(child);

	if (first == NULL || second == NULL || out == NULL)
	{
		return;
	}

	if (first->itemweightconfig == NULL || second->itemweightconfig == NULL
		|| out->itemweightconfig == NULL)
	{
		return;
	}

	InterbreedWeightConfigs(first->itemweightconfig,
							second->itemweightconfig,
							out->itemweightconfig);
	BotGoal_RebuildWeightIndices(out);
}

/*
=============
BotSaveGoalFuzzyLogic
=============
*/
void BotSaveGoalFuzzyLogic(int goalstate, char *filename)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(goalstate);
	if (gs == NULL || filename == NULL || filename[0] == '\0' || gs->itemweightconfig == NULL)
	{
		return;
	}

	WriteWeightConfig(filename, gs->itemweightconfig);
}

/*
=============
BotMutateGoalFuzzyLogic
=============
*/
void BotMutateGoalFuzzyLogic(int goalstate, float range)
{
	(void)range;

	bot_goalstate_t *gs = BotGoalStateFromHandle(goalstate);
	if (gs == NULL || gs->itemweightconfig == NULL)
	{
		return;
	}

	EvolveWeightConfig(gs->itemweightconfig);
	BotGoal_RebuildWeightIndices(gs);
}

/*
=============
BotSetupGoalAI
=============
*/
int BotSetupGoalAI(void)
{
	if (!BotGoal_LoadItemDefs())
	{
		BotLib_Print(PRT_FATAL, "couldn't load item config\n");
		return BLERR_CANNOTLOADITEMCONFIG;
	}
	return BLERR_NOERROR;
}

/*
=============
BotShutdownGoalAI
=============
*/
void BotShutdownGoalAI(void)
{
	BotGoal_ClearItemDefs();
}
