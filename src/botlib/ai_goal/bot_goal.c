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
#define BOT_GOAL_TRAVELTIME_SCALE 0.01f
#define BOT_GOAL_ASSET_MAX_PATH 512
#define BOT_GOAL_DEFAULT_RESPAWN_TIME 30.0f
#define BOT_GOAL_MINIMUM_AVOID_TIME 10.0f
#define BOT_GOAL_DROPPED_AVOID_TIME 10.0f
#define BOT_GOAL_DEFAULT_ITEM_HALF_EXTENT 15.0f

#define BOT_GOAL_ITEMFLAG_NOTFREE	(1 << 0)
#define BOT_GOAL_ITEMFLAG_NOTTEAM	(1 << 1)
#define BOT_GOAL_ITEMFLAG_NOTSINGLE	(1 << 2)
#define BOT_GOAL_ITEMFLAG_NOTBOT	(1 << 3)

static bot_goalstate_t *g_goalstates[MAX_CLIENTS + 1];

static float g_goal_current_time = 0.0f;

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
	vec3_t mins;
	vec3_t maxs;
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

static int BotGoal_PointAreaNum(const vec3_t origin);
static int BotGoal_StartAreaForState(bot_goalstate_t *gs, const vec3_t origin);
static bot_goalstate_t *BotGoalStateFromHandle(int handle);
static bool BotGoal_EnsureWeightCapacity(bot_goalstate_t *gs);
static float BotGoal_EvaluateItemWeight(const bot_goalstate_t *gs,
                                        const int *inventory,
                                        int iteminfo_index);
static float BotGoal_ItemBaseWeight(const bot_levelitem_t *item, float fuzzy_weight);
static float BotGoal_AvoidGoalTimeForState(const bot_goalstate_t *gs, int number);
static float BotGoal_AvoidTimeForItem(const bot_levelitem_t *item);
static bot_levelitem_t *BotGoal_FindLevelItem(int number);
static int BotGoal_FindItemInfoIndex(const char *classname);
static int BotGoal_RegisterItemInfo(const char *classname);
static bool BotGoal_BuildWeightPath(const char *filename, char *buffer, size_t size);
static void BotGoal_RebuildWeightIndices(bot_goalstate_t *gs);
static float BotGoal_RandomScale(float range);
static int BotGoal_StrIcmp(const char *lhs, const char *rhs);
static bool BotGoal_LevelItemAllowed(const bot_levelitem_t *item);
static bool BotGoal_ReadSignedFloat(pc_source_t *source, float *out);
static bool BotGoal_ReadVector(pc_source_t *source, vec3_t out);
static bool BotGoal_SkipValue(pc_source_t *source);
static bot_itemdef_t *BotGoal_FindItemDef(const char *classname);
static bot_itemdef_t *BotGoal_RegisterItemDef(const char *classname);
static bool BotGoal_ParseItemInfoBlock(pc_source_t *source, bot_itemdef_t *itemdef);
static void BotGoal_LoadItemDefs(void);
static bool BotGoal_ParseFloatString(const char *value, float *out);
static bool BotGoal_ParseIntString(const char *value, int *out);
static bool BotGoal_ParseVectorString(const char *value, vec3_t out);
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
        gs->goalstacktop = -1;
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

    gs->goalstacktop = -1;
    gs->numavoidgoals = 0;
    gs->numavoidreach = 0;
    gs->lastreachabilityarea = 0;

    memset(gs->avoidgoals, 0, sizeof(gs->avoidgoals));
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

int BotLoadItemWeights(int handle, const char *filename)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
		BotLib_Print(PRT_ERROR, "BotLoadItemWeights: invalid goal state %d\n", handle);
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	char path[BOT_GOAL_ASSET_MAX_PATH];
	if (!BotGoal_BuildWeightPath(filename, path, sizeof(path)))
	{
		BotLib_Print(PRT_ERROR,
					 "BotLoadItemWeights: unable to resolve %s\n",
					 filename != NULL ? filename : "<null>");
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	bot_weight_config_t *config = ReadWeightConfig(path);
	if (config == NULL)
	{
		BotLib_Print(PRT_FATAL, "BotLoadItemWeights: couldn't load %s\n", path);
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

    gs->numavoidgoals = 0;
    memset(gs->avoidgoals, 0, sizeof(gs->avoidgoals));
}

void BotAddToAvoidGoals(int handle, int number, float avoidtime)
{
    bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
    if (gs == NULL || number == 0)
    {
        return;
    }

    float expiry = BotGoal_CurrentTime() + ((avoidtime > 0.0f) ? avoidtime : 0.0f);

    for (int i = 0; i < gs->numavoidgoals; ++i)
    {
        if (gs->avoidgoals[i].number == number)
        {
            gs->avoidgoals[i].timeout = expiry;
            return;
        }
    }

    if (gs->numavoidgoals < BOT_GOAL_MAX_AVOID)
    {
        gs->avoidgoals[gs->numavoidgoals].number = number;
        gs->avoidgoals[gs->numavoidgoals].timeout = expiry;
        gs->numavoidgoals++;
        return;
    }

    int oldest = 0;
    for (int i = 1; i < BOT_GOAL_MAX_AVOID; ++i)
    {
        if (gs->avoidgoals[i].timeout < gs->avoidgoals[oldest].timeout)
        {
            oldest = i;
        }
    }

    gs->avoidgoals[oldest].number = number;
    gs->avoidgoals[oldest].timeout = expiry;
}

void BotRemoveFromAvoidGoals(int handle, int number)
{
    bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
    if (gs == NULL || number == 0)
    {
        return;
    }

    for (int i = 0; i < gs->numavoidgoals; ++i)
    {
        if (gs->avoidgoals[i].number == number)
        {
            for (int j = i; j < gs->numavoidgoals - 1; ++j)
            {
                gs->avoidgoals[j] = gs->avoidgoals[j + 1];
            }
            gs->numavoidgoals--;
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
    for (int i = 0; i < gs->numavoidgoals; ++i)
    {
        if (gs->avoidgoals[i].number == number)
        {
            float remaining = gs->avoidgoals[i].timeout - now;
            return (remaining > 0.0f) ? remaining : 0.0f;
        }
    }
    return 0.0f;
}

void BotSetAvoidGoalTime(int handle, int number, float avoidtime)
{
    if (avoidtime <= 0.0f)
    {
        BotRemoveFromAvoidGoals(handle, number);
        return;
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

    if (gs->goalstacktop + 1 >= BOT_GOAL_MAX_STACK)
    {
        for (int i = 1; i < BOT_GOAL_MAX_STACK; ++i)
        {
            gs->goalstack[i - 1] = gs->goalstack[i];
        }
        gs->goalstacktop = BOT_GOAL_MAX_STACK - 2;
    }

    gs->goalstacktop++;
    gs->goalstack[gs->goalstacktop] = *goal;
    return 1;
}

int BotPopGoal(int handle)
{
    bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
    if (gs == NULL || gs->goalstacktop < 0)
    {
        return 0;
    }

    gs->goalstacktop--;
    if (gs->goalstacktop < -1)
    {
        gs->goalstacktop = -1;
    }
    return 1;
}

void BotEmptyGoalStack(int handle)
{
    bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
    if (gs == NULL)
    {
        return;
    }

    gs->goalstacktop = -1;
}

int BotGetTopGoal(int handle, bot_goal_t *goal)
{
    const bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
    if (gs == NULL || gs->goalstacktop < 0)
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
    if (gs == NULL || gs->goalstacktop < 1)
    {
        return 0;
    }

    if (goal != NULL)
    {
        *goal = gs->goalstack[gs->goalstacktop - 1];
    }
    return 1;
}

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
            weight = FuzzyWeight(inventory, gs->itemweightconfig, fuzzy_index);
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
	for (int i = 0; i < gs->numavoidgoals; ++i)
	{
		if (gs->avoidgoals[i].number == number)
		{
			float remaining = gs->avoidgoals[i].timeout - now;
			return (remaining > 0.0f) ? remaining : 0.0f;
		}
	}

	return 0.0f;
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

	if (item->goal.flags & GFL_DROPPED)
	{
		return BOT_GOAL_DROPPED_AVOID_TIME;
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
	if (areanum <= 0)
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

		if (!(item->goal.flags & GFL_DROPPED))
		{
			continue;
		}

		if (item->timeout > 0.0f && item->timeout <= now)
		{
			item->valid = false;
			continue;
		}

		int entnum = item->goal.entitynum;
		if (entnum < 0 || entnum >= aasworld.maxEntities)
		{
			continue;
		}

		aas_entity_t *entity = &aasworld.entities[entnum];
		if (!entity->inuse)
		{
			item->valid = false;
			continue;
		}

		bool origin_changed = entity->origin[0] != item->goal.origin[0] ||
		                      entity->origin[1] != item->goal.origin[1] ||
		                      entity->origin[2] != item->goal.origin[2];
		if (origin_changed)
		{
			VectorCopy(entity->origin, item->goal.origin);
		}

		if (origin_changed || item->goal.areanum <= 0)
		{
			item->goal.areanum = BotGoal_PointAreaNum(item->goal.origin);
		}
	}
}

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
        if (!item->valid)
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
        best_goal = item->goal;
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
		if (ltg_time <= 0)
		{
			ltg_time = 99999;
		}
	}

    for (int i = 0; i < g_levelitem_count; ++i)
    {
        const bot_levelitem_t *item = &g_levelitems[i];
        if (!item->valid)
        {
            continue;
        }

        if (ltg != NULL && item->goal.number == ltg->number)
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

		if (maxtime > 0.0f && (float)travel_time >= maxtime)
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

		if (ltg != NULL && !(item->goal.flags & GFL_DROPPED))
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
        best_goal = item->goal;
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
    if (goal == NULL)
    {
        return 0;
    }

    vec3_t mins;
    vec3_t maxs;
    VectorAdd(goal->origin, goal->mins, mins);
    VectorAdd(goal->origin, goal->maxs, maxs);

    if (origin[0] < mins[0] || origin[0] > maxs[0])
    {
        return 0;
    }
    if (origin[1] < mins[1] || origin[1] > maxs[1])
    {
        return 0;
    }
    if (origin[2] < mins[2] || origin[2] > maxs[2])
    {
        return 0;
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
	VectorAdd(goal->mins, goal->maxs, middle);
	middle[0] *= 0.5f;
	middle[1] *= 0.5f;
	middle[2] *= 0.5f;
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
BotGoal_LoadItemDefs
=============
*/
static void BotGoal_LoadItemDefs(void)
{
	g_itemdef_count = 0;
	memset(g_itemdefs, 0, sizeof(g_itemdefs));

	char itemconfig_path[BOT_GOAL_ASSET_MAX_PATH];
	if (!BotGoal_BuildWeightPath(NULL, itemconfig_path, sizeof(itemconfig_path)))
	{
		return;
	}

	pc_source_t *source = PC_LoadSourceFile(itemconfig_path);
	if (source == NULL)
	{
		return;
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
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
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

	bot_maplocation_t *location = &g_maplocations[g_maplocation_count++];
	memset(location, 0, sizeof(*location));
	VectorCopy(entity->origin, location->origin);
	location->areanum = BotGoal_PointAreaNum(location->origin);
	if (entity->message[0] != '\0')
	{
		strncpy(location->name, entity->message, sizeof(location->name) - 1);
		location->name[sizeof(location->name) - 1] = '\0';
	}
	location->valid = true;
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

	bot_campspot_t *spot = &g_campspots[g_campspot_count++];
	memset(spot, 0, sizeof(*spot));
	VectorCopy(entity->origin, spot->origin);
	spot->areanum = BotGoal_PointAreaNum(spot->origin);
	if (spot->areanum <= 0)
	{
		g_campspot_count--;
		return;
	}

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

	VectorCopy(entity->origin, setup.goal.origin);
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
        snprintf(name, (size_t)size, "%d", number);
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
    BotLib_Print(PRT_MESSAGE, "BotDumpAvoidGoals: state %d has %d entries\n", handle, gs->numavoidgoals);
    for (int i = 0; i < gs->numavoidgoals; ++i)
    {
        float remaining = gs->avoidgoals[i].timeout - now;
        BotLib_Print(PRT_MESSAGE,
                     "  goal %d remaining %.2f\n",
                     gs->avoidgoals[i].number,
                     remaining > 0.0f ? remaining : 0.0f);
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

    BotLib_Print(PRT_MESSAGE, "BotDumpGoalStack: state %d depth %d\n", handle, gs->goalstacktop + 1);
    for (int i = gs->goalstacktop; i >= 0; --i)
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
BotGoal_RandomScale
=============
*/
static float BotGoal_RandomScale(float range)
{
	if (range <= 0.0f)
	{
		return 1.0f;
	}

	float unit = (float)rand() / (float)RAND_MAX;
	float centered = (unit * 2.0f) - 1.0f;
	float scale = 1.0f + (centered * range);
	if (scale < 0.01f)
	{
		scale = 0.01f;
	}
	return scale;
}

/*
=============
BotInitLevelItems
=============
*/
void BotInitLevelItems(void)
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

	BotGoal_LoadItemDefs();

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

	int start = 0;
	if (index >= 0)
	{
		for (int i = 0; i < g_levelitem_count; ++i)
		{
			const bot_levelitem_t *item = &g_levelitems[i];
			if (!item->valid)
			{
				continue;
			}
			if (item->goal.number == index)
			{
				start = i + 1;
				break;
			}
		}
	}

	for (int i = start; i < g_levelitem_count; ++i)
	{
		const bot_levelitem_t *item = &g_levelitems[i];
		if (!item->valid)
		{
			continue;
		}
		if (!BotGoal_LevelItemAllowed(item))
		{
			continue;
		}

		if (classname != NULL && classname[0] != '\0')
		{
			bool matched = false;
			if (BotGoal_StrIcmp(item->classname, classname) == 0)
			{
				matched = true;
			}
			else
			{
				const bot_itemdef_t *itemdef = BotGoal_FindItemDef(item->classname);
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

		memcpy(goal, &item->goal, sizeof(*goal));
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
	bot_weight_config_t *child_config;
	const char *source_path;

	if (first == NULL || second == NULL || out == NULL)
	{
		return;
	}

	if (first->itemweightconfig == NULL || second->itemweightconfig == NULL)
	{
		return;
	}

	source_path = first->itemweightconfig->source_file;
	if (source_path == NULL || source_path[0] == '\0')
	{
		source_path = second->itemweightconfig->source_file;
	}
	if (source_path == NULL || source_path[0] == '\0')
	{
		BotLib_Print(PRT_ERROR, "BotInterbreedGoalFuzzyLogic: parent configs have no source path\n");
		return;
	}

	child_config = ReadWeightConfig(source_path);
	if (child_config == NULL)
	{
		BotLib_Print(PRT_ERROR, "BotInterbreedGoalFuzzyLogic: couldn't clone %s\n", source_path);
		return;
	}

	InterbreedWeightConfigs(first->itemweightconfig, second->itemweightconfig, child_config);

	if (out->itemweightconfig != NULL)
	{
		FreeWeightConfig(out->itemweightconfig);
	}
	out->itemweightconfig = child_config;
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
	bot_goalstate_t *gs = BotGoalStateFromHandle(goalstate);
	if (gs == NULL || gs->itemweightconfig == NULL)
	{
		return;
	}

	if (range <= 0.0f)
	{
		EvolveWeightConfig(gs->itemweightconfig);
		BotGoal_RebuildWeightIndices(gs);
		return;
	}

	for (int i = 0; i < gs->itemweightconfig->num_weights; ++i)
	{
		const char *name = gs->itemweightconfig->weights[i].name;
		if (name == NULL || name[0] == '\0')
		{
			continue;
		}
		ScaleWeight(gs->itemweightconfig, name, BotGoal_RandomScale(range));
	}

	ScaleBalanceRange(gs->itemweightconfig, BotGoal_RandomScale(range));
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

	BotInitLevelItems();
}
