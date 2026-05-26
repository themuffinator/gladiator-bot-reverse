#include "bot_goal.h"

#include <ctype.h>
#include <errno.h>
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

#define BOT_GOAL_MAX_LEVELITEMS 512
#define BOT_GOAL_MAX_MAPLOCATIONS 256
#define BOT_GOAL_MAX_CAMPSPOTS 256
#define BOT_GOAL_MAX_ITEMDEFS 512
#define BOT_GOAL_MAX_MODELINDEXES 256
#define BOT_GOAL_TRAVELTIME_SCALE 0.01f
#define BOT_GOAL_ASSET_MAX_PATH 512
#define BOT_GOAL_MAX_MODEL_PATH 128
#define BOT_GOAL_DEFAULT_RESPAWN_TIME 30.0f
#define BOT_GOAL_MINIMUM_AVOID_TIME 10.0f
#define BOT_GOAL_DROPPED_AVOID_TIME 10.0f
#define BOT_GOAL_DROPPED_TIMEOUT 30.0f
#define BOT_GOAL_DEFAULT_ITEM_HALF_EXTENT 15.0f
#define BOT_GOAL_ENTITY_LINK_DISTANCE 20.0f

#define BOT_GOAL_ITEMFLAG_NOTFREE	(1 << 0)
#define BOT_GOAL_ITEMFLAG_NOTTEAM	(1 << 1)
#define BOT_GOAL_ITEMFLAG_NOTSINGLE	(1 << 2)
#define BOT_GOAL_ITEMFLAG_NOTBOT	(1 << 3)

static bot_goalstate_t *g_goalstates[MAX_CLIENTS + 1];

static float g_goal_current_time = 0.0f;
static float g_next_entity_item_update_time = 0.0f;

typedef struct bot_levelitem_s
{
	bot_goal_t goal;
	char classname[64];
	float base_weight;
	float respawntime;
	float next_respawn_time;
	float timeout;
	int flags;
	bool valid;
} bot_levelitem_t;

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
	char classname[64];
	char name[64];
	char model[BOT_GOAL_MAX_MODEL_PATH];
	vec3_t mins;
	vec3_t maxs;
	int modelindex;
	float respawntime;
	bool valid;
} bot_itemdef_t;

typedef struct bot_goal_parsed_entity_s
{
	char classname[64];
	bool has_classname;
	char message[64];
	vec3_t origin;
	bool has_origin;
	int spawnflags;
	bool has_spawnflags;
	int notfree;
	bool has_notfree;
	int notteam;
	bool has_notteam;
	int notsingle;
	bool has_notsingle;
	int notbot;
	bool has_notbot;
	float range;
	bool has_range;
	float weight;
	bool has_weight;
	float wait;
	bool has_wait;
	float random;
	bool has_random;
} bot_goal_parsed_entity_t;

static bot_levelitem_t g_levelitems[BOT_GOAL_MAX_LEVELITEMS];
static int g_levelitem_count = 0;
static int g_next_levelitem_number = 1;

static char g_iteminfo_names[BOT_GOAL_MAX_LEVELITEMS][64];
static int g_iteminfo_count = 0;

static bot_maplocation_t g_maplocations[BOT_GOAL_MAX_MAPLOCATIONS];
static int g_maplocation_count = 0;

static bot_campspot_t g_campspots[BOT_GOAL_MAX_CAMPSPOTS];
static int g_campspot_count = 0;

static bot_itemdef_t g_itemdefs[BOT_GOAL_MAX_ITEMDEFS];
static int g_itemdef_count = 0;
static bool g_itemdefs_loaded = false;

static char g_modelindex_names[BOT_GOAL_MAX_MODELINDEXES][BOT_GOAL_MAX_MODEL_PATH];
static int g_modelindex_count = 0;

static int BotGoal_PointAreaNum(const vec3_t origin);
static int BotGoal_StartAreaForState(bot_goalstate_t *gs, const vec3_t origin);
static bot_goalstate_t *BotGoalStateFromHandle(int handle);
static bool BotGoal_EnsureWeightCapacity(bot_goalstate_t *gs);
static float BotGoal_EvaluateItemWeight(const bot_goalstate_t *gs,
	const int *inventory,
	int iteminfo_index);
static float BotGoal_ItemBaseWeight(const bot_levelitem_t *item, float fuzzy_weight);
static float BotGoal_AvoidGoalTimeForState(const bot_goalstate_t *gs, int number);
static float BotGoal_RespawnAvoidTimeForItem(const bot_levelitem_t *item);
static float BotGoal_AvoidTimeForItem(const bot_levelitem_t *item);
static bot_levelitem_t *BotGoal_FindLevelItem(int number);
static int BotGoal_FindItemInfoIndex(const char *classname);
static int BotGoal_RegisterItemInfo(const char *classname);
static int BotGoal_IndexFromModel(const char *model);
static void BotGoal_ResolveItemDefModelIndexes(void);
static bool BotGoal_BuildWeightPath(const char *filename, char *buffer, size_t size);
static void BotGoal_ClearLevelItemState(void);
static void BotGoal_RebuildWeightIndices(bot_goalstate_t *gs);
static int BotGoal_StrIcmp(const char *lhs, const char *rhs);
static bool BotGoal_LevelItemAllowed(const bot_levelitem_t *item);
static bool BotGoal_LevelItemSelectable(const bot_levelitem_t *item);
static void BotGoal_CopyLevelItemGoal(const bot_levelitem_t *item,
	const bot_itemdef_t *itemdef,
	bot_goal_t *goal);
static void BotGoal_CopySelectedItemGoal(const bot_levelitem_t *item, bot_goal_t *goal);
static bool BotGoal_VectorEquals(const vec3_t lhs, const vec3_t rhs);
static float BotGoal_DistanceSquared(const vec3_t lhs, const vec3_t rhs);
static int BotGoal_StaticLevelItemCount(void);
static bool BotGoal_ReadSignedFloat(pc_source_t *source, float *out);
static bool BotGoal_ReadVector(pc_source_t *source, vec3_t out);
static bool BotGoal_SkipValue(pc_source_t *source);
static bot_itemdef_t *BotGoal_FindItemDef(const char *classname);
static bot_itemdef_t *BotGoal_FindItemDefByModelIndex(int modelindex);
static bot_itemdef_t *BotGoal_RegisterItemDef(const char *classname);
static bool BotGoal_ParseItemInfoBlock(pc_source_t *source, bot_itemdef_t *itemdef);
static char *BotGoal_ReadTextFile(const char *path, size_t *out_length);
static void BotGoal_StripComments(char *data);
static const char *BotGoal_FindWord(const char *cursor, const char *end, const char *word);
static bool BotGoal_ScanRawToken(const char **cursor, const char *end, char *out, size_t size);
static const char *BotGoal_FindMatchingBrace(const char *open_brace, const char *end);
static bool BotGoal_ParseRawFieldString(const char *block, const char *end, const char *field, char *out, size_t size);
static bool BotGoal_ParseRawFieldFloat(const char *block, const char *end, const char *field, float *out);
static bool BotGoal_ParseRawFieldVector(const char *block, const char *end, const char *field, vec3_t out);
static void BotGoal_LoadItemDefsRaw(const char *path);
static bool BotGoal_LoadItemDefs(void);
static bool BotGoal_ParseFloatString(const char *value, float *out);
static bool BotGoal_ParseIntString(const char *value, int *out);
static bool BotGoal_ParseVectorString(const char *value, vec3_t out);
static int BotGoal_NotSpawnFlags(void);
static bool BotGoal_EntityFilteredBySpawnFlags(const bot_goal_parsed_entity_t *entity);
static bool BotGoal_DropItemToFloor(vec3_t origin, const vec3_t mins, const vec3_t maxs);
static bool BotGoal_ParseQuotedToken(const char **cursor, const char *end, char **out_token);
static void BotGoal_SkipMalformedEntity(const char **cursor, const char *end);
static int32_t BotGoal_LittleLong(int32_t value);
static bool BotGoal_BuildMapPath(char *buffer, size_t size, const char *mapname, const char *extension);
static bool BotGoal_LoadEntityLump(char **out_data, size_t *out_length);
static bool BotGoal_IsLikelyItemClassname(const char *classname);
static void BotGoal_AddMapLocation(const bot_goal_parsed_entity_t *entity);
static void BotGoal_AddCampSpot(const bot_goal_parsed_entity_t *entity);
static void BotGoal_AddLevelItemFromEntity(const bot_goal_parsed_entity_t *entity);
static void BotGoal_RegisterParsedEntity(const bot_goal_parsed_entity_t *entity);
static void BotGoal_ParseEntityLump(const char *data, size_t length);

void BotGoal_SetCurrentTime(float now)
{
    g_goal_current_time = now;
}

float BotGoal_CurrentTime(void)
{
    return g_goal_current_time;
}

static bot_goalstate_t *BotGoalStateFromHandle(int handle)
{
    if (handle <= 0 || handle > MAX_CLIENTS)
    {
        return NULL;
    }
    return g_goalstates[handle];
}

const bot_goalstate_t *BotGoalStatePeek(int handle)
{
    return BotGoalStateFromHandle(handle);
}

int BotAllocGoalState(int client)
{
    for (int handle = 1; handle <= MAX_CLIENTS; ++handle)
    {
        if (g_goalstates[handle] != NULL)
        {
            continue;
        }

        bot_goalstate_t *gs = (bot_goalstate_t *)GetClearedMemory(sizeof(bot_goalstate_t));
        if (gs == NULL)
        {
            BotLib_Print(PRT_FATAL, "BotAllocGoalState: allocation failed\n");
            return 0;
        }

        gs->client = client;
        gs->goalstacktop = 0;
        gs->itemweightcount = 0;
        g_goalstates[handle] = gs;
        return handle;
    }

    BotLib_Print(PRT_ERROR, "BotAllocGoalState: no free goal state slots\n");
    return 0;
}

void BotFreeGoalState(int handle)
{
    bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
    if (gs == NULL)
    {
        return;
    }

    if (gs->itemweightconfig != NULL)
    {
        FreeWeightConfig(gs->itemweightconfig);
        gs->itemweightconfig = NULL;
    }

    if (gs->itemweightindex != NULL)
    {
        FreeMemory(gs->itemweightindex);
        gs->itemweightindex = NULL;
    }

    gs->itemweightcount = 0;

    FreeMemory(gs);
    g_goalstates[handle] = NULL;
}

void BotResetGoalState(int handle)
{
    bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
    if (gs == NULL)
    {
        return;
    }

	memset(gs->goalstack, 0, sizeof(gs->goalstack));
	gs->goalstacktop = 0;
	gs->numavoidreach = 0;
	gs->lastreachabilityarea = 0;

	memset(gs->avoidgoals, 0, sizeof(gs->avoidgoals));
	memset(gs->avoidgoaltimes, 0, sizeof(gs->avoidgoaltimes));
	memset(gs->avoidreach, 0, sizeof(gs->avoidreach));
	memset(gs->avoidreachtimes, 0, sizeof(gs->avoidreachtimes));
}

static bool BotGoal_BuildWeightPath(const char *filename, char *buffer, size_t size)
{
    if (buffer == NULL || size == 0)
    {
        return false;
    }

    const char *requested = filename;
    if (requested == NULL || requested[0] == '\0')
    {
        requested = LibVarString("itemconfig", "items.c");
    }

    if (requested == NULL || requested[0] == '\0')
    {
        return false;
    }

    return BotLib_ResolveAssetPath(requested, "itemconfig", buffer, size);
}

static bool BotGoal_EnsureWeightCapacity(bot_goalstate_t *gs)
{
    if (gs == NULL)
    {
        return false;
    }

    if (gs->itemweightcount == g_iteminfo_count)
    {
        return true;
    }

    int new_count = g_iteminfo_count;
    if (new_count <= 0)
    {
        if (gs->itemweightindex != NULL)
        {
            FreeMemory(gs->itemweightindex);
            gs->itemweightindex = NULL;
        }
        gs->itemweightcount = 0;
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

    if (gs->itemweightindex != NULL && gs->itemweightcount > 0)
    {
        int copy = gs->itemweightcount;
        if (copy > new_count)
        {
            copy = new_count;
        }
        memcpy(indices, gs->itemweightindex, sizeof(int) * (size_t)copy);
        FreeMemory(gs->itemweightindex);
    }

    gs->itemweightindex = indices;
    gs->itemweightcount = new_count;
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
	if (gs == NULL || gs->itemweightindex == NULL)
	{
		return 0;
	}

	return MemoryByteSize(gs->itemweightindex);
}

/*
=============
BotLoadItemWeights
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

	bot_weight_config_t *config = ReadWeightConfig(filename);
	if (config == NULL)
	{
		BotLib_Print(PRT_FATAL, "couldn't load weights\n");
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	if (!g_itemdefs_loaded)
	{
		FreeWeightConfig(config);
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	if (!BotGoal_EnsureWeightCapacity(gs))
	{
		BotLib_Print(PRT_ERROR, "BotLoadItemWeights: weight index allocation failed\n");
		FreeWeightConfig(config);
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	if (gs->itemweightconfig != NULL)
	{
		FreeWeightConfig(gs->itemweightconfig);
		gs->itemweightconfig = NULL;
	}

	gs->itemweightconfig = config;

	for (int i = 0; i < gs->itemweightcount; ++i)
	{
		const char *classname = g_iteminfo_names[i];
		gs->itemweightindex[i] = BotWeight_FindIndex(gs->itemweightconfig, classname);
		if (gs->itemweightindex[i] < 0)
		{
			BotLib_Print(PRT_WARNING,
						 "item info %d \"%s\" has no fuzzy weight\n",
						 i,
						 classname);
		}
	}

	BotLib_Print(PRT_DEVELOPER,
				 "%6d bytes item index\n",
				 (int)BotGoal_ItemWeightIndexByteSize(handle));
	return BLERR_NOERROR;
}

void BotFreeItemWeights(int handle)
{
    bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
    if (gs == NULL)
    {
        return;
    }

    if (gs->itemweightconfig != NULL)
    {
        FreeWeightConfig(gs->itemweightconfig);
        gs->itemweightconfig = NULL;
    }

    if (gs->itemweightindex != NULL)
    {
        FreeMemory(gs->itemweightindex);
        gs->itemweightindex = NULL;
    }

    gs->itemweightcount = 0;
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

	int index = BotGoal_FindItemInfoIndex(classname);
	if (index < 0 || index >= gs->itemweightcount) {
		return -1;
	}

	return gs->itemweightindex[index];
}

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

void BotAddToAvoidGoals(int handle, int number, float avoidtime)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL || number == 0)
	{
		return;
	}

	float now = BotGoal_CurrentTime();
	float expiry = now + ((avoidtime > 0.0f) ? avoidtime : 0.0f);

	for (int i = 0; i < BOT_GOAL_MAX_AVOID; ++i)
	{
		if (gs->avoidgoaltimes[i] < now)
		{
			gs->avoidgoals[i] = number;
			gs->avoidgoaltimes[i] = expiry;
			return;
		}
	}
}

void BotRemoveFromAvoidGoals(int handle, int number)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL || number == 0)
	{
		return;
	}

	float now = BotGoal_CurrentTime();
	for (int i = 0; i < BOT_GOAL_MAX_AVOID; ++i)
	{
		if (gs->avoidgoals[i] == number && gs->avoidgoaltimes[i] >= now)
		{
			gs->avoidgoaltimes[i] = 0.0f;
			return;
		}
	}
}

float BotAvoidGoalTime(int handle, int number)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
		return 0.0f;
	}

	float now = BotGoal_CurrentTime();
	for (int i = 0; i < BOT_GOAL_MAX_AVOID; ++i)
	{
		if (gs->avoidgoals[i] == number && gs->avoidgoaltimes[i] >= now)
		{
			return gs->avoidgoaltimes[i] - now;
		}
	}
	return 0.0f;
}

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

void BotResetAvoidReach(int handle)
{
    bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
    if (gs == NULL)
    {
        return;
    }

    gs->numavoidreach = 0;
    memset(gs->avoidreach, 0, sizeof(gs->avoidreach));
    memset(gs->avoidreachtimes, 0, sizeof(gs->avoidreachtimes));
}

void BotAddToAvoidReach(int handle, int number, float avoidtime)
{
    bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
    if (gs == NULL || number <= 0)
    {
        return;
    }

    float expiry = BotGoal_CurrentTime() + ((avoidtime > 0.0f) ? avoidtime : 0.0f);
    for (int i = 0; i < gs->numavoidreach; ++i)
    {
        if (gs->avoidreach[i] == number)
        {
            gs->avoidreachtimes[i] = expiry;
            return;
        }
    }

    if (gs->numavoidreach < BOT_GOAL_MAX_AVOIDREACH)
    {
        gs->avoidreach[gs->numavoidreach] = number;
        gs->avoidreachtimes[gs->numavoidreach] = expiry;
        gs->numavoidreach++;
        return;
    }

    int oldest = 0;
    for (int i = 1; i < BOT_GOAL_MAX_AVOIDREACH; ++i)
    {
        if (gs->avoidreachtimes[i] < gs->avoidreachtimes[oldest])
        {
            oldest = i;
        }
    }
    gs->avoidreach[oldest] = number;
    gs->avoidreachtimes[oldest] = expiry;
}

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
    return 1;
}

int BotPopGoal(int handle)
{
    bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
    if (gs == NULL || gs->goalstacktop <= 0)
    {
        return 0;
    }

    gs->goalstacktop--;
    return 1;
}

void BotEmptyGoalStack(int handle)
{
    bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
    if (gs == NULL)
    {
        return;
    }

    gs->goalstacktop = 0;
}

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
	int iteminfo_index)
{
	float weight = 0.0f;
	if (gs != NULL && gs->itemweightconfig != NULL &&
		gs->itemweightindex != NULL &&
		iteminfo_index >= 0 && iteminfo_index < gs->itemweightcount)
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

	if ((item->goal.flags & GFL_ROAM) && item->base_weight > 0.0f)
	{
		if (fuzzy_weight > 0.0f)
		{
			return fuzzy_weight * item->base_weight;
		}
		return item->base_weight;
	}

	if (fuzzy_weight > 0.0f)
	{
		return fuzzy_weight + item->base_weight;
	}

	return item->base_weight;
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

	float now = BotGoal_CurrentTime();
	for (int i = 0; i < BOT_GOAL_MAX_AVOID; ++i)
	{
		if (gs->avoidgoals[i] == number && gs->avoidgoaltimes[i] >= now)
		{
			return gs->avoidgoaltimes[i] - now;
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

	float avoidtime = item->respawntime;
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

	if (item->timeout > 0.0f)
	{
		return BOT_GOAL_DROPPED_AVOID_TIME;
	}

	return BotGoal_RespawnAvoidTimeForItem(item);
}

static int BotGoal_PointAreaNum(const vec3_t origin)
{
    if (!aasworld.loaded || aasworld.areas == NULL || aasworld.numAreas <= 0)
    {
        return 0;
    }

    for (int areanum = 1; areanum <= aasworld.numAreas; ++areanum)
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

Resolve the bot's current reachability area using the movement helper first,
then fall back to the last usable goal area like the retail code.
=============
*/
static int BotGoal_StartAreaForState(bot_goalstate_t *gs, const vec3_t origin)
{
	if (gs == NULL)
	{
		return 0;
	}

	int areanum = BotReachabilityArea(origin, gs->client);
	if (areanum <= 0)
	{
		areanum = BotGoal_PointAreaNum(origin);
	}
	if (areanum <= 0 || AAS_AreaReachability(areanum) == 0)
	{
		areanum = gs->lastreachabilityarea;
	}
	if (areanum > 0)
	{
		gs->lastreachabilityarea = areanum;
	}

	return areanum;
}

static bot_levelitem_t *BotGoal_FindLevelItem(int number)
{
    for (int i = 0; i < g_levelitem_count; ++i)
    {
        if (g_levelitems[i].valid && g_levelitems[i].goal.number == number)
        {
            return &g_levelitems[i];
        }
    }
    return NULL;
}

static int BotGoal_FindItemInfoIndex(const char *classname)
{
    if (classname == NULL)
    {
        return -1;
    }

    for (int i = 0; i < g_iteminfo_count; ++i)
    {
        if (strcmp(g_iteminfo_names[i], classname) == 0)
        {
            return i;
        }
    }
    return -1;
}

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

    if (g_iteminfo_count >= BOT_GOAL_MAX_LEVELITEMS)
    {
        BotLib_Print(PRT_ERROR, "BotGoal_RegisterItemInfo: capacity exceeded for %s\n", classname);
        return -1;
    }

    strncpy(g_iteminfo_names[g_iteminfo_count], classname, sizeof(g_iteminfo_names[0]) - 1);
    g_iteminfo_names[g_iteminfo_count][sizeof(g_iteminfo_names[0]) - 1] = '\0';
    int index = g_iteminfo_count;
    g_iteminfo_count++;

    for (int handle = 1; handle <= MAX_CLIENTS; ++handle)
    {
        bot_goalstate_t *gs = g_goalstates[handle];
        if (gs == NULL)
        {
            continue;
        }
        if (!BotGoal_EnsureWeightCapacity(gs))
        {
            BotLib_Print(PRT_WARNING,
                         "BotGoal_RegisterItemInfo: failed to expand weight index for handle %d\n",
                         handle);
        }
		if (gs->itemweightconfig != NULL &&
			gs->itemweightindex != NULL &&
			index < gs->itemweightcount)
		{
			gs->itemweightindex[index] =
				BotWeight_FindIndex(gs->itemweightconfig, g_iteminfo_names[index]);
		}
    }

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

	for (int i = 0; i < g_modelindex_count; ++i)
	{
		if (g_modelindex_names[i][0] == '\0')
		{
			continue;
		}
		if (strcmp(g_modelindex_names[i], model) == 0)
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
		if (!itemdef->valid)
		{
			continue;
		}
		if (itemdef->modelindex > 0)
		{
			continue;
		}
		itemdef->modelindex = BotGoal_IndexFromModel(itemdef->model);
		if (g_modelindex_count > 0 && itemdef->modelindex <= 0)
		{
			BotLib_LogWrite("item %s has modelindex 0", itemdef->classname);
		}
	}
}

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

	bot_levelitem_t *existing = BotGoal_FindLevelItem(number);
	bot_levelitem_t *slot = existing;
	if (slot == NULL)
	{
		for (int i = 0; i < g_levelitem_count; ++i)
		{
            if (!g_levelitems[i].valid)
            {
                slot = &g_levelitems[i];
                break;
            }
        }
        if (slot == NULL)
        {
            if (g_levelitem_count >= BOT_GOAL_MAX_LEVELITEMS)
            {
                BotLib_Print(PRT_ERROR, "BotGoal_RegisterLevelItem: too many level items\n");
                return 0;
            }
            slot = &g_levelitems[g_levelitem_count++];
        }
	}

	slot->goal = setup->goal;
	slot->goal.number = number;
	slot->goal.iteminfo = iteminfo;
	slot->goal.flags = setup->flags;
	if (!(slot->goal.flags & (GFL_ITEM | GFL_ROAM | GFL_DROPPED)))
	{
		slot->goal.flags |= GFL_ITEM;
    }

    if (slot->goal.areanum <= 0)
    {
        slot->goal.areanum = BotGoal_PointAreaNum(slot->goal.origin);
    }

    strncpy(slot->classname, setup->classname, sizeof(slot->classname) - 1);
    slot->classname[sizeof(slot->classname) - 1] = '\0';
    slot->base_weight = setup->weight;
    slot->respawntime = (setup->respawntime > 0.0f) ? setup->respawntime : 0.0f;
    slot->next_respawn_time = BotGoal_CurrentTime();
	slot->timeout = 0.0f;
	if ((slot->goal.flags & GFL_DROPPED) && setup->respawntime > 0.0f)
	{
		slot->timeout = BotGoal_CurrentTime() + setup->respawntime;
	}
	slot->flags = setup->itemflags;
	slot->valid = true;
	return slot->goal.number;
}

void BotGoal_UnregisterLevelItem(int number)
{
    bot_levelitem_t *item = BotGoal_FindLevelItem(number);
    if (item != NULL)
    {
        item->valid = false;
    }
}

void BotGoal_MarkItemTaken(int number, float respawn_delay)
{
    bot_levelitem_t *item = BotGoal_FindLevelItem(number);
    if (item == NULL)
    {
        return;
    }

    float delay = respawn_delay;
    if (delay <= 0.0f)
    {
        delay = item->respawntime;
    }
    if (delay < 0.0f)
    {
        delay = 0.0f;
    }

    item->next_respawn_time = BotGoal_CurrentTime() + delay;
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
		BotGoal_ResolveItemDefModelIndexes();
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

	BotGoal_ResolveItemDefModelIndexes();
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
BotGoal_DistanceSquared
=============
*/
static float BotGoal_DistanceSquared(const vec3_t lhs, const vec3_t rhs)
{
	float dx = lhs[0] - rhs[0];
	float dy = lhs[1] - rhs[1];
	float dz = lhs[2] - rhs[2];
	return dx * dx + dy * dy + dz * dz;
}

/*
=============
BotGoal_StaticLevelItemCount
=============
*/
static int BotGoal_StaticLevelItemCount(void)
{
	int count = 0;
	for (int i = 0; i < g_levelitem_count; ++i)
	{
		const bot_levelitem_t *item = &g_levelitems[i];
		if (!item->valid)
		{
			continue;
		}
		if (item->goal.flags & GFL_DROPPED)
		{
			continue;
		}
		++count;
	}
	return count;
}

/*
=============
BotUpdateEntityItems

Refresh dropped or temporary entity items each frame.
=============
*/
void BotUpdateEntityItems(void)
{
	if (!aasworld.loaded || aasworld.entities == NULL || aasworld.maxEntities <= 0)
	{
		return;
	}

	float now = aasworld.time;

	for (int i = 0; i < g_levelitem_count; ++i)
	{
		bot_levelitem_t *item = &g_levelitems[i];
		if (!item->valid)
		{
			continue;
		}

		if (item->timeout > 0.0f && item->timeout < now)
		{
			item->valid = false;
			continue;
		}
	}

	float link_distance_squared = BOT_GOAL_ENTITY_LINK_DISTANCE * BOT_GOAL_ENTITY_LINK_DISTANCE;

	for (int entnum = 1; entnum < aasworld.maxEntities; ++entnum)
	{
		aas_entity_t *entity = &aasworld.entities[entnum];
		if (!entity->inuse || entity->modelindex <= 0)
		{
			continue;
		}

		if (!BotGoal_VectorEquals(entity->origin, entity->previousOrigin))
		{
			continue;
		}

		bool handled = false;
		for (int i = 0; i < g_levelitem_count; ++i)
		{
			bot_levelitem_t *item = &g_levelitems[i];
			if (!item->valid || item->goal.entitynum != entnum)
			{
				continue;
			}

			const bot_itemdef_t *itemdef = BotGoal_FindItemDef(item->classname);
			if (itemdef == NULL || itemdef->modelindex != entity->modelindex)
			{
				item->valid = false;
				break;
			}

			if (!BotGoal_VectorEquals(entity->origin, item->goal.origin))
			{
				VectorCopy(entity->origin, item->goal.origin);
				item->goal.areanum = BotGoal_PointAreaNum(item->goal.origin);
			}
			handled = true;
			break;
		}

		if (handled)
		{
			continue;
		}

		for (int i = 0; i < g_levelitem_count; ++i)
		{
			bot_levelitem_t *item = &g_levelitems[i];
			if (!item->valid || item->goal.entitynum != 0)
			{
				continue;
			}
			if (!BotGoal_LevelItemAllowed(item))
			{
				continue;
			}

			const bot_itemdef_t *itemdef = BotGoal_FindItemDef(item->classname);
			if (itemdef == NULL || itemdef->modelindex != entity->modelindex)
			{
				continue;
			}
			if (BotGoal_DistanceSquared(item->goal.origin, entity->origin) >= link_distance_squared)
			{
				continue;
			}

			item->goal.entitynum = entnum;
			if (!BotGoal_VectorEquals(entity->origin, item->goal.origin))
			{
				VectorCopy(entity->origin, item->goal.origin);
				item->goal.areanum = BotGoal_PointAreaNum(item->goal.origin);
			}
			handled = true;
			break;
		}

		if (handled)
		{
			continue;
		}

		const bot_itemdef_t *itemdef = BotGoal_FindItemDefByModelIndex(entity->modelindex);
		if (itemdef == NULL)
		{
			continue;
		}

		bot_levelitem_setup_t setup;
		memset(&setup, 0, sizeof(setup));
		setup.classname = itemdef->classname;
		setup.flags = GFL_ITEM | GFL_DROPPED;
		setup.respawntime = BOT_GOAL_DROPPED_TIMEOUT;
		setup.goal.number = BotGoal_StaticLevelItemCount() + entnum;
		setup.goal.entitynum = entnum;
		setup.goal.flags = GFL_ITEM | GFL_DROPPED;
		VectorCopy(entity->origin, setup.goal.origin);
		VectorCopy(itemdef->mins, setup.goal.mins);
		VectorCopy(itemdef->maxs, setup.goal.maxs);
		setup.goal.areanum = BotGoal_PointAreaNum(setup.goal.origin);
		if (AAS_AreaJumpPad(setup.goal.areanum))
		{
			continue;
		}

		int number = BotGoal_RegisterLevelItem(&setup);
		if (number <= 0)
		{
			continue;
		}

		bot_levelitem_t *item = BotGoal_FindLevelItem(number);
		if (item != NULL)
		{
			item->timeout = now + BOT_GOAL_DROPPED_TIMEOUT;
		}
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
	BotGoal_SetCurrentTime(now);
	if (now <= g_next_entity_item_update_time)
	{
		return;
	}

	BotUpdateEntityItems();
	g_next_entity_item_update_time = now + 1.0f;
}

/*
=============
BotGoal_LevelItemSelectable

Mirrors the retail selector guard that ignores static item records until they
have linked to a live entity, except for explicit roam goals.
=============
*/
static bool BotGoal_LevelItemSelectable(const bot_levelitem_t *item)
{
	if (item == NULL || !item->valid)
	{
		return false;
	}

	if (item->goal.entitynum == 0 && (item->goal.flags & GFL_ROAM) == 0)
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
static float BotGoal_LevelItemScore(bot_goalstate_t *gs,
									const bot_levelitem_t *item,
									const vec3_t origin,
									int start_area,
									const int *inventory,
									int travelflags,
									int *travel_time,
									bool require_travel)
{
	if (item == NULL || !item->valid)
	{
		return -FLT_MAX;
	}

	if (!BotGoal_LevelItemAllowed(item))
	{
		return -FLT_MAX;
	}

	if (item->goal.areanum <= 0)
	{
		return -FLT_MAX;
	}

	int time = 0;
	if (start_area > 0 && item->goal.areanum > 0)
	{
		vec3_t start;
		VectorCopy(origin, start);
		time = AAS_AreaTravelTimeToGoalArea(start_area, start, item->goal.areanum, travelflags);
	}

	if (travel_time != NULL)
	{
		*travel_time = time;
	}

	if (require_travel && time <= 0)
	{
		return -FLT_MAX;
	}

	float fuzzy_weight = BotGoal_EvaluateItemWeight(gs, inventory, item->goal.iteminfo);
	float weight = BotGoal_ItemBaseWeight(item, fuzzy_weight);
	if (weight <= 0.0f)
	{
		return -FLT_MAX;
	}

	if (time > 0)
	{
		return weight / ((float)time * BOT_GOAL_TRAVELTIME_SCALE);
	}

	return weight;
}

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

	float now = BotGoal_CurrentTime();
	float best_score = -FLT_MAX;
	const bot_levelitem_t *best_item = NULL;
	bot_goal_t best_goal = {0};

	for (int i = 0; i < g_levelitem_count; ++i)
	{
		const bot_levelitem_t *item = &g_levelitems[i];
		if (!BotGoal_LevelItemSelectable(item))
		{
			continue;
		}

        int travel_time = 0;
		float score = BotGoal_LevelItemScore(gs,
											 item,
											 origin,
											 start_area,
											 inventory,
											 travelflags,
											 &travel_time,
											 true);
		if (score <= -FLT_MAX)
		{
			continue;
		}

		float travel_seconds = (float)travel_time * 0.009f;
		if (BotGoal_AvoidGoalTimeForState(gs, item->goal.number) - travel_seconds > 0.0f)
		{
			continue;
		}

		if (item->next_respawn_time > now + travel_seconds)
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
        return 0;
    }

	if (!BotPushGoal(handle, &best_goal))
	{
		return 0;
	}

	BotAddToAvoidGoals(handle, best_item->goal.number, BotGoal_AvoidTimeForItem(best_item));
	return 1;
}

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

    float now = BotGoal_CurrentTime();
    float best_score = -FLT_MAX;
    const bot_levelitem_t *best_item = NULL;
    bot_goal_t best_goal = {0};
	int ltg_time = 99999;
	if (ltg != NULL && ltg->areanum > 0)
	{
		vec3_t start;
		VectorCopy(origin, start);
		ltg_time = AAS_AreaTravelTimeToGoalArea(start_area, start, ltg->areanum, travelflags);
	}

	for (int i = 0; i < g_levelitem_count; ++i)
	{
		const bot_levelitem_t *item = &g_levelitems[i];
		if (!BotGoal_LevelItemSelectable(item))
		{
			continue;
		}

        int travel_time = 0;
		float score = BotGoal_LevelItemScore(gs,
											 item,
											 origin,
											 start_area,
											 inventory,
											 travelflags,
											 &travel_time,
											 true);
		if (score <= -FLT_MAX)
		{
			continue;
		}

		if ((float)travel_time >= maxtime)
		{
			continue;
		}

		float travel_seconds = (float)travel_time * 0.009f;
		if (BotGoal_AvoidGoalTimeForState(gs, item->goal.number) - travel_seconds > 0.0f)
		{
			continue;
		}

		if (item->next_respawn_time > now + travel_seconds)
		{
			continue;
		}

		if (ltg != NULL && item->timeout <= 0.0f)
		{
			vec3_t item_origin;
			VectorCopy(item->goal.origin, item_origin);
			int return_time = AAS_AreaTravelTimeToGoalArea(item->goal.areanum,
														  item_origin,
														  ltg->areanum,
														  travelflags);
			if (return_time > ltg_time)
			{
				continue;
			}
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
        return 0;
    }

	if (!BotPushGoal(handle, &best_goal))
	{
		return 0;
	}

	BotAddToAvoidGoals(handle, best_item->goal.number, BotGoal_AvoidTimeForItem(best_item));
	return 1;
}

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
                                       origin,
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

int BotTouchingGoal(const vec3_t origin, const bot_goal_t *goal)
{
	if (origin == NULL || goal == NULL)
	{
		return 0;
	}

	vec3_t boxmins;
	vec3_t boxmaxs;
	AAS_PresenceTypeBoundingBox(PRESENCE_NORMAL, boxmins, boxmaxs);

	for (int i = 0; i < 3; ++i)
	{
		float absmin = goal->origin[i] + goal->mins[i] - boxmaxs[i];
		float absmax = goal->origin[i] + goal->maxs[i] - boxmins[i];
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
	bsp_trace_t trace = Q2_Trace(eye, mins, maxs, middle, viewer, CONTENTS_SOLID);
	if (trace.fraction >= 1.0f)
	{
		int entnum = goal->entitynum;
		if (entnum <= 0 || aasworld.entities == NULL || entnum >= aasworld.maxEntities)
		{
			return 0;
		}

		const aas_entity_t *entity = &aasworld.entities[entnum];
		if (entity->lastUpdateTime < aasworld.time - 0.5f)
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
BotGoal_EntityFilteredBySpawnFlags
=============
*/
static bool BotGoal_EntityFilteredBySpawnFlags(const bot_goal_parsed_entity_t *entity)
{
	if (entity == NULL)
	{
		return false;
	}

	int spawnflags = entity->has_spawnflags ? entity->spawnflags : 0;
	return (spawnflags & BotGoal_NotSpawnFlags()) != 0;
}

/*
=============
BotGoal_DropItemToFloor

Drop a stationary BSP item down before resolving its goal origin.
=============
*/
static bool BotGoal_DropItemToFloor(vec3_t origin, const vec3_t mins, const vec3_t maxs)
{
	if (origin == NULL)
	{
		return false;
	}

	vec3_t end;
	VectorCopy(origin, end);
	end[2] -= 100.0f;

	bsp_trace_t trace = AAS_Trace(origin, mins, maxs, end, 0, CONTENTS_SOLID);
	if (trace.startsolid || trace.fraction >= 1.0f)
	{
		return false;
	}

	VectorCopy(trace.endpos, origin);
	return true;
}

/*
=============
BotGoal_LevelItemAllowed
=============
*/
static bool BotGoal_LevelItemAllowed(const bot_levelitem_t *item)
{
	if (item == NULL || !item->valid)
	{
		return false;
	}

	if (item->flags & BOT_GOAL_ITEMFLAG_NOTBOT)
	{
		return false;
	}

	int gametype = BotGoal_CurrentGameType();
	if (gametype == 2)
	{
		if (item->flags & BOT_GOAL_ITEMFLAG_NOTSINGLE)
		{
			return false;
		}
	}
	else if (gametype >= 3)
	{
		if (item->flags & BOT_GOAL_ITEMFLAG_NOTTEAM)
		{
			return false;
		}
	}
	else
	{
		if (item->flags & BOT_GOAL_ITEMFLAG_NOTFREE)
		{
			return false;
		}
	}

	return true;
}

/*
=============
BotGoal_CopyLevelItemGoal

Build the public goal record returned by the level-item lookup helper.
=============
*/
static void BotGoal_CopyLevelItemGoal(const bot_levelitem_t *item,
	const bot_itemdef_t *itemdef,
	bot_goal_t *goal)
{
	memset(goal, 0, sizeof(*goal));
	goal->areanum = item->goal.areanum;
	VectorCopy(item->goal.origin, goal->origin);
	goal->entitynum = item->goal.entitynum;
	if (itemdef != NULL)
	{
		VectorCopy(itemdef->mins, goal->mins);
		VectorCopy(itemdef->maxs, goal->maxs);
	}
	else
	{
		VectorCopy(item->goal.mins, goal->mins);
		VectorCopy(item->goal.maxs, goal->maxs);
	}
	goal->number = item->goal.number;
	goal->flags = GFL_ITEM;
	if (item->timeout > 0.0f)
	{
		goal->flags |= GFL_DROPPED;
	}
	goal->iteminfo = item->goal.iteminfo;
}

/*
=============
BotGoal_CopySelectedItemGoal

Build the goal record pushed by LTG/NBG item selection.
=============
*/
static void BotGoal_CopySelectedItemGoal(const bot_levelitem_t *item, bot_goal_t *goal)
{
	const bot_itemdef_t *itemdef = BotGoal_FindItemDef(item->classname);
	BotGoal_CopyLevelItemGoal(item, itemdef, goal);
	if (item->goal.flags & GFL_ROAM)
	{
		goal->flags |= GFL_ROAM;
	}
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

	if (!PC_ReadToken(source, &token))
	{
		return false;
	}

	if (token.type == TT_PUNCTUATION && token.subtype == P_SUB)
	{
		negative = true;
		if (!PC_ReadToken(source, &token))
		{
			return false;
		}
	}

	if (token.type != TT_NUMBER)
	{
		return false;
	}

	float value = (token.subtype & TT_FLOAT) ? (float)token.floatvalue : (float)token.intvalue;
	*out = negative ? -value : value;
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
		if (!BotGoal_ReadSignedFloat(source, &out[axis]))
		{
			return false;
		}

		if (axis < 2)
		{
			if (!PC_ExpectTokenType(source, TT_PUNCTUATION, P_COMMA, &token))
			{
				return false;
			}
		}
	}

	if (!PC_ExpectTokenType(source, TT_PUNCTUATION, P_BRACECLOSE, &token))
	{
		return false;
	}

	return true;
}

/*
=============
BotGoal_SkipValue
=============
*/
static bool BotGoal_SkipValue(pc_source_t *source)
{
	pc_token_t token;
	if (source == NULL)
	{
		return false;
	}

	if (!PC_ReadToken(source, &token))
	{
		return false;
	}

	if (token.type == TT_PUNCTUATION && token.subtype == P_BRACEOPEN)
	{
		int depth = 1;
		while (depth > 0)
		{
			if (!PC_ReadToken(source, &token))
			{
				return false;
			}

			if (token.type != TT_PUNCTUATION)
			{
				continue;
			}

			if (token.subtype == P_BRACEOPEN)
			{
				++depth;
			}
			else if (token.subtype == P_BRACECLOSE)
			{
				--depth;
			}
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
		if (!g_itemdefs[i].valid)
		{
			continue;
		}
		if (BotGoal_StrIcmp(g_itemdefs[i].classname, classname) == 0)
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
		if (!g_itemdefs[i].valid)
		{
			continue;
		}
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
	if (classname == NULL || classname[0] == '\0')
	{
		return NULL;
	}

	bot_itemdef_t *existing = BotGoal_FindItemDef(classname);
	if (existing != NULL)
	{
		return existing;
	}

	if (g_itemdef_count >= BOT_GOAL_MAX_ITEMDEFS)
	{
		return NULL;
	}

	bot_itemdef_t *itemdef = &g_itemdefs[g_itemdef_count++];
	memset(itemdef, 0, sizeof(*itemdef));
	strncpy(itemdef->classname, classname, sizeof(itemdef->classname) - 1);
	itemdef->classname[sizeof(itemdef->classname) - 1] = '\0';
	strncpy(itemdef->name, classname, sizeof(itemdef->name) - 1);
	itemdef->name[sizeof(itemdef->name) - 1] = '\0';
	VectorSet(itemdef->mins,
			  -BOT_GOAL_DEFAULT_ITEM_HALF_EXTENT,
			  -BOT_GOAL_DEFAULT_ITEM_HALF_EXTENT,
			  -BOT_GOAL_DEFAULT_ITEM_HALF_EXTENT);
	VectorSet(itemdef->maxs,
			  BOT_GOAL_DEFAULT_ITEM_HALF_EXTENT,
			  BOT_GOAL_DEFAULT_ITEM_HALF_EXTENT,
			  BOT_GOAL_DEFAULT_ITEM_HALF_EXTENT);
	itemdef->respawntime = BOT_GOAL_DEFAULT_RESPAWN_TIME;
	itemdef->valid = true;
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

		if (token.type != TT_NAME)
		{
			if (!BotGoal_SkipValue(source))
			{
				return false;
			}
			continue;
		}

		if (strcmp(token.string, "respawntime") == 0)
		{
			float value = 0.0f;
			if (BotGoal_ReadSignedFloat(source, &value))
			{
				itemdef->respawntime = value;
			}
			continue;
		}

		if (strcmp(token.string, "name") == 0)
		{
			pc_token_t value_token;
			if (PC_ReadToken(source, &value_token))
			{
				if (value_token.type == TT_STRING || value_token.type == TT_NAME)
				{
					strncpy(itemdef->name, value_token.string, sizeof(itemdef->name) - 1);
					itemdef->name[sizeof(itemdef->name) - 1] = '\0';
				}
			}
			continue;
		}

		if (strcmp(token.string, "model") == 0)
		{
			pc_token_t value_token;
			if (PC_ReadToken(source, &value_token))
			{
				if (value_token.type == TT_STRING || value_token.type == TT_NAME)
				{
					strncpy(itemdef->model, value_token.string, sizeof(itemdef->model) - 1);
					itemdef->model[sizeof(itemdef->model) - 1] = '\0';
				}
			}
			continue;
		}

		if (strcmp(token.string, "modelindex") == 0)
		{
			float value = 0.0f;
			if (BotGoal_ReadSignedFloat(source, &value))
			{
				itemdef->modelindex = (int)value;
			}
			continue;
		}

		if (strcmp(token.string, "mins") == 0)
		{
			BotGoal_ReadVector(source, itemdef->mins);
			continue;
		}

		if (strcmp(token.string, "maxs") == 0)
		{
			BotGoal_ReadVector(source, itemdef->maxs);
			continue;
		}

		if (!BotGoal_SkipValue(source))
		{
			return false;
		}
	}

	return false;
}

/*
=============
BotGoal_ReadTextFile
=============
*/
static char *BotGoal_ReadTextFile(const char *path, size_t *out_length)
{
	if (out_length != NULL)
	{
		*out_length = 0U;
	}
	if (path == NULL || path[0] == '\0')
	{
		return NULL;
	}

	FILE *file = fopen(path, "rb");
	if (file == NULL)
	{
		return NULL;
	}

	if (fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		return NULL;
	}
	long length = ftell(file);
	if (length < 0)
	{
		fclose(file);
		return NULL;
	}
	if (fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return NULL;
	}

	char *buffer = (char *)malloc((size_t)length + 1U);
	if (buffer == NULL)
	{
		fclose(file);
		return NULL;
	}

	size_t read = fread(buffer, 1U, (size_t)length, file);
	fclose(file);
	if (read != (size_t)length)
	{
		free(buffer);
		return NULL;
	}

	buffer[read] = '\0';
	if (out_length != NULL)
	{
		*out_length = read;
	}
	return buffer;
}

/*
=============
BotGoal_StripComments
=============
*/
static void BotGoal_StripComments(char *data)
{
	if (data == NULL)
	{
		return;
	}

	bool in_quote = false;
	for (char *cursor = data; cursor[0] != '\0'; ++cursor)
	{
		if (cursor[0] == '"' && (cursor == data || cursor[-1] != '\\'))
		{
			in_quote = !in_quote;
			continue;
		}
		if (in_quote)
		{
			continue;
		}

		if (cursor[0] == '/' && cursor[1] == '/')
		{
			cursor[0] = ' ';
			cursor[1] = ' ';
			cursor += 2;
			while (cursor[0] != '\0' && cursor[0] != '\n')
			{
				cursor[0] = ' ';
				++cursor;
			}
			if (cursor[0] == '\0')
			{
				break;
			}
		}
		else if (cursor[0] == '/' && cursor[1] == '*')
		{
			cursor[0] = ' ';
			cursor[1] = ' ';
			cursor += 2;
			while (cursor[0] != '\0')
			{
				if (cursor[0] == '*' && cursor[1] == '/')
				{
					cursor[0] = ' ';
					cursor[1] = ' ';
					++cursor;
					break;
				}
				if (cursor[0] != '\n')
				{
					cursor[0] = ' ';
				}
				++cursor;
			}
			if (cursor[0] == '\0')
			{
				break;
			}
		}
	}
}

/*
=============
BotGoal_FindWord
=============
*/
static const char *BotGoal_FindWord(const char *cursor, const char *end, const char *word)
{
	if (cursor == NULL || end == NULL || word == NULL || word[0] == '\0')
	{
		return NULL;
	}

	size_t length = strlen(word);
	while (cursor < end)
	{
		const char *match = strstr(cursor, word);
		if (match == NULL || match + length > end)
		{
			return NULL;
		}

		bool before = (match == cursor) || (!isalnum((unsigned char)match[-1]) && match[-1] != '_');
		bool after = (match + length >= end) ||
			(!isalnum((unsigned char)match[length]) && match[length] != '_');
		if (before && after)
		{
			return match;
		}

		cursor = match + length;
	}

	return NULL;
}

/*
=============
BotGoal_ScanRawToken
=============
*/
static bool BotGoal_ScanRawToken(const char **cursor, const char *end, char *out, size_t size)
{
	if (cursor == NULL || *cursor == NULL || end == NULL || out == NULL || size == 0U)
	{
		return false;
	}

	const char *position = *cursor;
	while (position < end && isspace((unsigned char)*position))
	{
		++position;
	}
	if (position >= end)
	{
		return false;
	}

	size_t used = 0U;
	if (*position == '"')
	{
		++position;
		while (position < end && *position != '"')
		{
			if (used + 1U < size)
			{
				out[used++] = *position;
			}
			++position;
		}
		if (position < end && *position == '"')
		{
			++position;
		}
	}
	else
	{
		while (position < end &&
			!isspace((unsigned char)*position) &&
			*position != '{' &&
			*position != '}' &&
			*position != ',')
		{
			if (used + 1U < size)
			{
				out[used++] = *position;
			}
			++position;
		}
	}

	out[used] = '\0';
	*cursor = position;
	return used > 0U;
}

/*
=============
BotGoal_FindMatchingBrace
=============
*/
static const char *BotGoal_FindMatchingBrace(const char *open_brace, const char *end)
{
	if (open_brace == NULL || end == NULL || open_brace >= end || *open_brace != '{')
	{
		return NULL;
	}

	int depth = 0;
	for (const char *cursor = open_brace; cursor < end; ++cursor)
	{
		if (*cursor == '{')
		{
			++depth;
		}
		else if (*cursor == '}')
		{
			--depth;
			if (depth == 0)
			{
				return cursor;
			}
		}
	}

	return NULL;
}

/*
=============
BotGoal_ParseRawFieldString
=============
*/
static bool BotGoal_ParseRawFieldString(const char *block, const char *end, const char *field, char *out, size_t size)
{
	const char *field_pos = BotGoal_FindWord(block, end, field);
	if (field_pos == NULL)
	{
		return false;
	}

	field_pos += strlen(field);
	return BotGoal_ScanRawToken(&field_pos, end, out, size);
}

/*
=============
BotGoal_ParseRawFieldFloat
=============
*/
static bool BotGoal_ParseRawFieldFloat(const char *block, const char *end, const char *field, float *out)
{
	char token[64];
	if (!BotGoal_ParseRawFieldString(block, end, field, token, sizeof(token)))
	{
		return false;
	}
	return BotGoal_ParseFloatString(token, out);
}

/*
=============
BotGoal_ParseRawFieldVector
=============
*/
static bool BotGoal_ParseRawFieldVector(const char *block, const char *end, const char *field, vec3_t out)
{
	const char *field_pos = BotGoal_FindWord(block, end, field);
	if (field_pos == NULL || out == NULL)
	{
		return false;
	}

	field_pos += strlen(field);
	while (field_pos < end && isspace((unsigned char)*field_pos))
	{
		++field_pos;
	}
	if (field_pos >= end || *field_pos != '{')
	{
		return false;
	}

	const char *close = strchr(field_pos, '}');
	if (close == NULL || close > end)
	{
		return false;
	}

	char token[128];
	size_t length = (size_t)(close - field_pos - 1);
	if (length >= sizeof(token))
	{
		return false;
	}
	memcpy(token, field_pos + 1, length);
	token[length] = '\0';
	for (size_t i = 0; i < length; ++i)
	{
		if (token[i] == ',')
		{
			token[i] = ' ';
		}
	}

	return sscanf(token, "%f %f %f", &out[0], &out[1], &out[2]) == 3;
}

/*
=============
BotGoal_LoadItemDefsRaw

Fallback parser for iteminfo blocks when precompiler conditionals reject the
Quake II item config in standalone tests.
=============
*/
static void BotGoal_LoadItemDefsRaw(const char *path)
{
	size_t length = 0U;
	char *data = BotGoal_ReadTextFile(path, &length);
	if (data == NULL)
	{
		return;
	}

	BotGoal_StripComments(data);
	const char *cursor = data;
	const char *end = data + length;
	while (cursor < end)
	{
		const char *iteminfo = BotGoal_FindWord(cursor, end, "iteminfo");
		if (iteminfo == NULL)
		{
			break;
		}

		cursor = iteminfo + strlen("iteminfo");
		char classname[64];
		if (!BotGoal_ScanRawToken(&cursor, end, classname, sizeof(classname)))
		{
			continue;
		}

		while (cursor < end && isspace((unsigned char)*cursor))
		{
			++cursor;
		}
		if (cursor >= end || *cursor != '{')
		{
			continue;
		}

		const char *block_end = BotGoal_FindMatchingBrace(cursor, end);
		if (block_end == NULL)
		{
			break;
		}

		bot_itemdef_t *itemdef = BotGoal_RegisterItemDef(classname);
		if (itemdef != NULL)
		{
			BotGoal_ParseRawFieldString(cursor, block_end, "name", itemdef->name, sizeof(itemdef->name));
			BotGoal_ParseRawFieldString(cursor, block_end, "model", itemdef->model, sizeof(itemdef->model));
			BotGoal_ParseRawFieldFloat(cursor, block_end, "respawntime", &itemdef->respawntime);
			BotGoal_ParseRawFieldVector(cursor, block_end, "mins", itemdef->mins);
			BotGoal_ParseRawFieldVector(cursor, block_end, "maxs", itemdef->maxs);

			float modelindex = 0.0f;
			if (BotGoal_ParseRawFieldFloat(cursor, block_end, "modelindex", &modelindex))
			{
				itemdef->modelindex = (int)modelindex;
			}
		}

		cursor = block_end + 1;
	}

	free(data);
}

/*
=============
BotGoal_ClearLevelItemState
=============
*/
static void BotGoal_ClearLevelItemState(void)
{
	memset(g_levelitems, 0, sizeof(g_levelitems));
	g_levelitem_count = 0;
	g_next_levelitem_number = 1;

	memset(g_iteminfo_names, 0, sizeof(g_iteminfo_names));
	g_iteminfo_count = 0;
	memset(g_maplocations, 0, sizeof(g_maplocations));
	g_maplocation_count = 0;
	memset(g_campspots, 0, sizeof(g_campspots));
	g_campspot_count = 0;

	memset(g_itemdefs, 0, sizeof(g_itemdefs));
	g_itemdef_count = 0;
	g_itemdefs_loaded = false;
}

/*
=============
BotGoal_LoadItemDefs
=============
*/
static bool BotGoal_LoadItemDefs(void)
{
	g_itemdef_count = 0;
	g_itemdefs_loaded = false;
	memset(g_itemdefs, 0, sizeof(g_itemdefs));

	char itemconfig_path[BOT_GOAL_ASSET_MAX_PATH];
	if (!BotGoal_BuildWeightPath(NULL, itemconfig_path, sizeof(itemconfig_path)))
	{
		return false;
	}

	pc_source_t *source = PC_LoadSourceFile(itemconfig_path);
	if (source == NULL)
	{
		return false;
	}

	pc_token_t token;
	while (PC_ReadToken(source, &token))
	{
		if (token.type != TT_NAME || strcmp(token.string, "iteminfo") != 0)
		{
			continue;
		}

		pc_token_t classname_token;
		if (!PC_ReadToken(source, &classname_token))
		{
			break;
		}
		if (classname_token.type != TT_STRING && classname_token.type != TT_NAME)
		{
			break;
		}

		bot_itemdef_t *itemdef = BotGoal_RegisterItemDef(classname_token.string);
		if (itemdef == NULL)
		{
			BotGoal_SkipValue(source);
			continue;
		}

		BotGoal_ParseItemInfoBlock(source, itemdef);
	}

	PC_FreeSource(source);
	BotGoal_LoadItemDefsRaw(itemconfig_path);
	g_itemdefs_loaded = true;
	BotGoal_ResolveItemDefModelIndexes();
	return true;
}

/*
=============
BotGoal_ParseFloatString
=============
*/
static bool BotGoal_ParseFloatString(const char *value, float *out)
{
	if (value == NULL || out == NULL)
	{
		return false;
	}

	errno = 0;
	char *endptr = NULL;
	float parsed = strtof(value, &endptr);
	if (endptr == value || errno == ERANGE)
	{
		return false;
	}

	while (endptr != NULL && *endptr != '\0' && isspace((unsigned char)*endptr))
	{
		++endptr;
	}

	if (endptr != NULL && *endptr != '\0')
	{
		return false;
	}

	*out = parsed;
	return true;
}

/*
=============
BotGoal_ParseIntString
=============
*/
static bool BotGoal_ParseIntString(const char *value, int *out)
{
	if (value == NULL || out == NULL)
	{
		return false;
	}

	errno = 0;
	char *endptr = NULL;
	long parsed = strtol(value, &endptr, 10);
	if (endptr == value || errno == ERANGE || parsed > INT_MAX || parsed < INT_MIN)
	{
		return false;
	}

	while (endptr != NULL && *endptr != '\0' && isspace((unsigned char)*endptr))
	{
		++endptr;
	}

	if (endptr != NULL && *endptr != '\0')
	{
		return false;
	}

	*out = (int)parsed;
	return true;
}

/*
=============
BotGoal_ParseVectorString
=============
*/
static bool BotGoal_ParseVectorString(const char *value, vec3_t out)
{
	if (value == NULL || out == NULL)
	{
		return false;
	}

	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	if (sscanf(value, "%f %f %f", &x, &y, &z) != 3)
	{
		return false;
	}

	out[0] = x;
	out[1] = y;
	out[2] = z;
	return true;
}

/*
=============
BotGoal_ParseQuotedToken
=============
*/
static bool BotGoal_ParseQuotedToken(const char **cursor, const char *end, char **out_token)
{
	if (cursor == NULL || *cursor == NULL || out_token == NULL)
	{
		return false;
	}

	const char *position = *cursor;
	while (position < end && isspace((unsigned char)*position))
	{
		++position;
	}

	if (position >= end || *position != '"')
	{
		*cursor = position;
		return false;
	}

	++position;
	const char *start = position;
	bool escape = false;
	while (position < end)
	{
		char ch = *position;
		if (!escape && ch == '\\')
		{
			escape = true;
			++position;
			continue;
		}

		if (!escape && ch == '"')
		{
			size_t raw_length = (size_t)(position - start);
			char *token = (char *)malloc(raw_length + 1U);
			if (token == NULL)
			{
				BotLib_Print(PRT_WARNING,
							 "BotGoal_ParseEntityLump: out of memory parsing token\n");
				*cursor = position;
				return false;
			}

			const char *reader = start;
			char *writer = token;
			escape = false;
			while (reader < position)
			{
				char rc = *reader++;
				if (!escape && rc == '\\')
				{
					escape = true;
					continue;
				}

				*writer++ = rc;
				escape = false;
			}
			*writer = '\0';

			++position;
			*cursor = position;
			*out_token = token;
			return true;
		}

		escape = false;
		++position;
	}

	BotLib_Print(PRT_WARNING,
				 "BotGoal_ParseEntityLump: unterminated quoted token in entity lump\n");
	*cursor = position;
	return false;
}

/*
=============
BotGoal_SkipMalformedEntity
=============
*/
static void BotGoal_SkipMalformedEntity(const char **cursor, const char *end)
{
	if (cursor == NULL || *cursor == NULL)
	{
		return;
	}

	const char *position = *cursor;
	while (position < end)
	{
		if (*position == '}')
		{
			++position;
			break;
		}
		++position;
	}

	*cursor = position;
}

/*
=============
BotGoal_LittleLong
=============
*/
static int32_t BotGoal_LittleLong(int32_t value)
{
#if defined(_WIN32) || (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
	return value;
#else
	uint32_t u = (uint32_t)value;
	return (int32_t)((u >> 24)
					 | ((u >> 8) & 0x0000FF00U)
					 | ((u << 8) & 0x00FF0000U)
					 | (u << 24));
#endif
}

/*
=============
BotGoal_StringEndsWithIgnoreCase
=============
*/
static bool BotGoal_StringEndsWithIgnoreCase(const char *value, const char *suffix)
{
	if (value == NULL || suffix == NULL)
	{
		return false;
	}

	size_t value_length = strlen(value);
	size_t suffix_length = strlen(suffix);
	if (suffix_length == 0U)
	{
		return true;
	}
	if (suffix_length > value_length)
	{
		return false;
	}

	const char *tail = value + (value_length - suffix_length);
	for (size_t i = 0U; i < suffix_length; ++i)
	{
		char lhs = (char)tolower((unsigned char)tail[i]);
		char rhs = (char)tolower((unsigned char)suffix[i]);
		if (lhs != rhs)
		{
			return false;
		}
	}
	return true;
}

/*
=============
BotGoal_BuildMapPath
=============
*/
static bool BotGoal_BuildMapPath(char *buffer, size_t size, const char *mapname, const char *extension)
{
	if (buffer == NULL || size == 0U || mapname == NULL || mapname[0] == '\0')
	{
		return false;
	}

	buffer[0] = '\0';

	int prefix_needed = 1;
	if (strncmp(mapname, "maps/", 5) == 0 || strncmp(mapname, "maps\\", 5) == 0)
	{
		prefix_needed = 0;
	}

	int written = 0;
	if (prefix_needed)
	{
		written = snprintf(buffer, size, "maps/%s", mapname);
	}
	else
	{
		written = snprintf(buffer, size, "%s", mapname);
	}

	if (written < 0 || (size_t)written >= size)
	{
		buffer[0] = '\0';
		return false;
	}

	if (extension != NULL && extension[0] != '\0'
		&& !BotGoal_StringEndsWithIgnoreCase(buffer, extension))
	{
		size_t current_length = (size_t)written;
		size_t extension_length = strlen(extension);
		if (current_length + extension_length + 1U > size)
		{
			buffer[0] = '\0';
			return false;
		}

		strncat(buffer, extension, size - current_length - 1U);
	}

	return true;
}

/*
=============
BotGoal_LoadEntityLump
=============
*/
static bool BotGoal_LoadEntityLump(char **out_data, size_t *out_length)
{
	if (out_data == NULL || out_length == NULL)
	{
		return false;
	}

	*out_data = NULL;
	*out_length = 0U;

	if (!aasworld.loaded || aasworld.mapName[0] == '\0')
	{
		return false;
	}

	char bsp_path[MAX_FILEPATH];
	if (!BotGoal_BuildMapPath(bsp_path, sizeof(bsp_path), aasworld.mapName, ".bsp"))
	{
		return false;
	}

	FILE *bsp_file = fopen(bsp_path, "rb");
	if (bsp_file == NULL)
	{
		return false;
	}

	q2_bsp_header_t bsp_header;
	if (fread(&bsp_header, sizeof(bsp_header), 1U, bsp_file) != 1U)
	{
		fclose(bsp_file);
		return false;
	}

	bsp_header.ident = BotGoal_LittleLong(bsp_header.ident);
	bsp_header.version = BotGoal_LittleLong(bsp_header.version);
	for (int i = 0; i < Q2_BSP_LUMP_MAX; ++i)
	{
		bsp_header.lumps[i].offset = BotGoal_LittleLong(bsp_header.lumps[i].offset);
		bsp_header.lumps[i].length = BotGoal_LittleLong(bsp_header.lumps[i].length);
	}

	if (bsp_header.ident != Q2_BSP_IDENT || bsp_header.version != Q2_BSP_VERSION)
	{
		fclose(bsp_file);
		return false;
	}

	const q2_lump_t *entities_lump = &bsp_header.lumps[Q2_BSP_LUMP_ENTITIES];
	if (entities_lump->length <= 0 || entities_lump->offset < 0)
	{
		fclose(bsp_file);
		return false;
	}

	if (fseek(bsp_file, entities_lump->offset, SEEK_SET) != 0)
	{
		fclose(bsp_file);
		return false;
	}

	size_t lump_length = (size_t)entities_lump->length;
	char *data = (char *)malloc(lump_length + 1U);
	if (data == NULL)
	{
		fclose(bsp_file);
		return false;
	}

	size_t read_length = fread(data, 1U, lump_length, bsp_file);
	fclose(bsp_file);

	if (read_length != lump_length)
	{
		free(data);
		return false;
	}

	data[lump_length] = '\0';
	*out_data = data;
	*out_length = lump_length;
	return true;
}

/*
=============
BotGoal_IsLikelyItemClassname
=============
*/
static bool BotGoal_IsLikelyItemClassname(const char *classname)
{
	if (classname == NULL || classname[0] == '\0')
	{
		return false;
	}

	if (BotGoal_FindItemDef(classname) != NULL)
	{
		return true;
	}

	if (BotGoal_StrIcmp(classname, "item_botroam") == 0)
	{
		return true;
	}

	if (strncmp(classname, "item_", 5) == 0
		|| strncmp(classname, "weapon_", 7) == 0
		|| strncmp(classname, "ammo_", 5) == 0
		|| strncmp(classname, "key_", 4) == 0)
	{
		return true;
	}

	return false;
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
BotGoal_AddLevelItemFromEntity
=============
*/
static void BotGoal_AddLevelItemFromEntity(const bot_goal_parsed_entity_t *entity)
{
	if (entity == NULL || !entity->has_classname || !entity->has_origin)
	{
		return;
	}

	bot_itemdef_t *itemdef = BotGoal_FindItemDef(entity->classname);
	if (itemdef == NULL)
	{
		return;
	}
	if (BotGoal_EntityFilteredBySpawnFlags(entity))
	{
		return;
	}

	int itemflags = 0;
	if (entity->has_notfree && entity->notfree != 0)
	{
		itemflags |= BOT_GOAL_ITEMFLAG_NOTFREE;
	}
	if (entity->has_notteam && entity->notteam != 0)
	{
		itemflags |= BOT_GOAL_ITEMFLAG_NOTTEAM;
	}
	if (entity->has_notsingle && entity->notsingle != 0)
	{
		itemflags |= BOT_GOAL_ITEMFLAG_NOTSINGLE;
	}
	if (entity->has_notbot && entity->notbot != 0)
	{
		itemflags |= BOT_GOAL_ITEMFLAG_NOTBOT;
	}

	bot_levelitem_setup_t setup;
	memset(&setup, 0, sizeof(setup));
	setup.classname = entity->classname;
	setup.flags = GFL_ITEM;
	setup.itemflags = itemflags;
	setup.respawntime = itemdef->respawntime;
	setup.weight = 0.0f;

	if (BotGoal_StrIcmp(entity->classname, "item_botroam") == 0)
	{
		setup.flags |= GFL_ROAM;
		setup.weight = entity->has_weight ? entity->weight : 1.0f;
	}

	vec3_t origin;
	VectorCopy(entity->origin, origin);
	int spawnflags = entity->has_spawnflags ? entity->spawnflags : 0;
	if ((spawnflags & 1) == 0 && !BotGoal_DropItemToFloor(origin, itemdef->mins, itemdef->maxs))
	{
		BotLib_Print(PRT_MESSAGE,
					 "%s in solid at (%1.1f %1.1f %1.1f)\n",
					 entity->classname,
					 entity->origin[0],
					 entity->origin[1],
					 entity->origin[2]);
	}

	VectorCopy(origin, setup.goal.origin);
	VectorCopy(itemdef->mins, setup.goal.mins);
	VectorCopy(itemdef->maxs, setup.goal.maxs);
	setup.goal.areanum = BotGoal_PointAreaNum(setup.goal.origin);
	setup.goal.entitynum = 0;
	setup.goal.number = 0;

	BotGoal_RegisterLevelItem(&setup);
}

/*
=============
BotGoal_RegisterParsedEntity
=============
*/
static void BotGoal_RegisterParsedEntity(const bot_goal_parsed_entity_t *entity)
{
	if (entity == NULL || !entity->has_classname)
	{
		return;
	}

	if (BotGoal_StrIcmp(entity->classname, "target_location") == 0)
	{
		BotGoal_AddMapLocation(entity);
		return;
	}

	if (BotGoal_StrIcmp(entity->classname, "info_camp") == 0)
	{
		BotGoal_AddCampSpot(entity);
		return;
	}

	if (BotGoal_IsLikelyItemClassname(entity->classname))
	{
		BotGoal_AddLevelItemFromEntity(entity);
	}
}

/*
=============
BotGoal_ParseEntityLump
=============
*/
static void BotGoal_ParseEntityLump(const char *data, size_t length)
{
	if (data == NULL || length == 0U)
	{
		return;
	}

	const char *cursor = data;
	const char *end = data + length;

	while (cursor < end)
	{
		while (cursor < end && isspace((unsigned char)*cursor))
		{
			++cursor;
		}

		if (cursor >= end)
		{
			break;
		}

		if (*cursor != '{')
		{
			++cursor;
			continue;
		}

		++cursor;
		bot_goal_parsed_entity_t entity;
		memset(&entity, 0, sizeof(entity));
		bool malformed = false;

		while (cursor < end)
		{
			while (cursor < end && isspace((unsigned char)*cursor))
			{
				++cursor;
			}

			if (cursor >= end)
			{
				malformed = true;
				break;
			}

			if (*cursor == '}')
			{
				++cursor;
				break;
			}

			char *key = NULL;
			if (!BotGoal_ParseQuotedToken(&cursor, end, &key))
			{
				if (key != NULL)
				{
					free(key);
				}
				malformed = true;
				BotGoal_SkipMalformedEntity(&cursor, end);
				break;
			}

			while (cursor < end && isspace((unsigned char)*cursor))
			{
				++cursor;
			}

			char *value = NULL;
			if (!BotGoal_ParseQuotedToken(&cursor, end, &value))
			{
				if (key != NULL)
				{
					free(key);
				}
				if (value != NULL)
				{
					free(value);
				}
				malformed = true;
				BotGoal_SkipMalformedEntity(&cursor, end);
				break;
			}

			if (key != NULL && value != NULL)
			{
				if (strcmp(key, "classname") == 0)
				{
					strncpy(entity.classname, value, sizeof(entity.classname) - 1);
					entity.classname[sizeof(entity.classname) - 1] = '\0';
					entity.has_classname = true;
				}
				else if (strcmp(key, "message") == 0)
				{
					strncpy(entity.message, value, sizeof(entity.message) - 1);
					entity.message[sizeof(entity.message) - 1] = '\0';
				}
				else if (strcmp(key, "origin") == 0)
				{
					vec3_t parsed_origin;
					if (BotGoal_ParseVectorString(value, parsed_origin))
					{
						VectorCopy(parsed_origin, entity.origin);
						entity.has_origin = true;
					}
				}
				else if (strcmp(key, "spawnflags") == 0)
				{
					int parsed = 0;
					if (BotGoal_ParseIntString(value, &parsed))
					{
						entity.spawnflags = parsed;
						entity.has_spawnflags = true;
					}
				}
				else if (strcmp(key, "notfree") == 0)
				{
					int parsed = 0;
					if (BotGoal_ParseIntString(value, &parsed))
					{
						entity.notfree = parsed;
						entity.has_notfree = true;
					}
				}
				else if (strcmp(key, "notteam") == 0)
				{
					int parsed = 0;
					if (BotGoal_ParseIntString(value, &parsed))
					{
						entity.notteam = parsed;
						entity.has_notteam = true;
					}
				}
				else if (strcmp(key, "notsingle") == 0)
				{
					int parsed = 0;
					if (BotGoal_ParseIntString(value, &parsed))
					{
						entity.notsingle = parsed;
						entity.has_notsingle = true;
					}
				}
				else if (strcmp(key, "notbot") == 0)
				{
					int parsed = 0;
					if (BotGoal_ParseIntString(value, &parsed))
					{
						entity.notbot = parsed;
						entity.has_notbot = true;
					}
				}
				else if (strcmp(key, "range") == 0)
				{
					float parsed = 0.0f;
					if (BotGoal_ParseFloatString(value, &parsed))
					{
						entity.range = parsed;
						entity.has_range = true;
					}
				}
				else if (strcmp(key, "weight") == 0)
				{
					float parsed = 0.0f;
					if (BotGoal_ParseFloatString(value, &parsed))
					{
						entity.weight = parsed;
						entity.has_weight = true;
					}
				}
				else if (strcmp(key, "wait") == 0)
				{
					float parsed = 0.0f;
					if (BotGoal_ParseFloatString(value, &parsed))
					{
						entity.wait = parsed;
						entity.has_wait = true;
					}
				}
				else if (strcmp(key, "random") == 0)
				{
					float parsed = 0.0f;
					if (BotGoal_ParseFloatString(value, &parsed))
					{
						entity.random = parsed;
						entity.has_random = true;
					}
				}
			}

			if (key != NULL)
			{
				free(key);
			}
			if (value != NULL)
			{
				free(value);
			}
		}

		if (!malformed)
		{
			BotGoal_RegisterParsedEntity(&entity);
		}
	}
}

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

	const bot_itemdef_t *itemdef = BotGoal_FindItemDef(item->classname);
	if (itemdef != NULL && itemdef->name[0] != '\0')
	{
		snprintf(name, (size_t)size, "%s", itemdef->name);
		return;
	}

    snprintf(name, (size_t)size, "%s", item->classname);
}

void BotDumpAvoidGoals(int handle)
{
	const bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
		BotLib_Print(PRT_MESSAGE, "BotDumpAvoidGoals: invalid goal state %d\n", handle);
		return;
	}

	float now = BotGoal_CurrentTime();
	BotLib_Print(PRT_MESSAGE, "BotDumpAvoidGoals: state %d\n", handle);
	for (int i = 0; i < BOT_GOAL_MAX_AVOID; ++i)
	{
		if (gs->avoidgoaltimes[i] >= now)
		{
			BotLib_Print(PRT_MESSAGE,
						 "  goal %d remaining %.2f\n",
						 gs->avoidgoals[i],
						 gs->avoidgoaltimes[i] - now);
		}
	}
}

void BotDumpGoalStack(int handle)
{
    const bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
    if (gs == NULL)
    {
        BotLib_Print(PRT_MESSAGE, "BotDumpGoalStack: invalid goal state %d\n", handle);
        return;
    }

    BotLib_Print(PRT_MESSAGE, "BotDumpGoalStack: state %d depth %d\n", handle, gs->goalstacktop);
    for (int i = gs->goalstacktop; i >= 1; --i)
    {
        const bot_goal_t *goal = &gs->goalstack[i];
        char name[64];
        BotGoalName(goal->number, name, sizeof(name));
        BotLib_Print(PRT_MESSAGE,
                     "  [%d] goal %d area %d name %s\n",
                     i,
                     goal->number,
                     goal->areanum,
                     name);
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

	for (int i = 0; i < gs->itemweightcount; ++i)
	{
		gs->itemweightindex[i] = BotWeight_FindIndex(gs->itemweightconfig, g_iteminfo_names[i]);
	}
}

/*
=============
BotInitLevelItems
=============
*/
void BotInitLevelItems(void)
{
	g_next_entity_item_update_time = 0.0f;
	BotGoal_ClearLevelItemState();
	(void)BotGoal_LoadItemDefs();

	char *entity_lump = NULL;
	size_t entity_lump_length = 0U;
	if (BotGoal_LoadEntityLump(&entity_lump, &entity_lump_length))
	{
		BotGoal_ParseEntityLump(entity_lump, entity_lump_length);
		free(entity_lump);
	}

	for (int handle = 1; handle <= MAX_CLIENTS; ++handle)
	{
		bot_goalstate_t *gs = g_goalstates[handle];
		if (gs == NULL)
		{
			continue;
		}
		BotGoal_EnsureWeightCapacity(gs);
	}
}

/*
=============
BotGetLevelItemGoal
=============
*/
int BotGetLevelItemGoal(int index, char *classname, bot_goal_t *goal)
{
	if (goal == NULL)
	{
		return -1;
	}

	for (int i = 0; i < g_levelitem_count; ++i)
	{
		const bot_levelitem_t *item = &g_levelitems[i];
		if (!item->valid)
		{
			continue;
		}
		if (index >= 0 && item->goal.number <= index)
		{
			continue;
		}
		if (!BotGoal_LevelItemAllowed(item))
		{
			continue;
		}

		const bot_itemdef_t *itemdef = BotGoal_FindItemDef(item->classname);
		if (classname != NULL && classname[0] != '\0')
		{
			bool matched = false;
			if (BotGoal_StrIcmp(item->classname, classname) == 0)
			{
				matched = true;
			}
			else
			{
				if (itemdef != NULL && itemdef->name[0] != '\0'
					&& BotGoal_StrIcmp(itemdef->name, classname) == 0)
				{
					matched = true;
				}
			}

			if (!matched)
			{
				continue;
			}
		}

		BotGoal_CopyLevelItemGoal(item, itemdef, goal);
		return item->goal.number;
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
	BotInitLevelItems();
	if (!g_itemdefs_loaded)
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
	for (int handle = 1; handle <= MAX_CLIENTS; ++handle)
	{
		if (g_goalstates[handle] != NULL)
		{
			BotFreeGoalState(handle);
		}
	}

	BotGoal_ClearLevelItemState();
	BotGoal_SetMapModelIndexes(0, NULL);
	g_next_entity_item_update_time = 0.0f;
}
