#include "bot_goal.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "botlib_aas/aas_local.h"
#include "botlib_common/l_assets.h"
#include "botlib_common/l_libvar.h"
#include "botlib_common/l_log.h"
#include "botlib_common/l_memory.h"

#define BOT_GOAL_MAX_LEVELITEMS 512
#define BOT_GOAL_TRAVELTIME_SCALE 0.01f
#define BOT_GOAL_ASSET_MAX_PATH 512

static bot_goalstate_t *g_goalstates[MAX_CLIENTS + 1];

static float g_goal_current_time = 0.0f;

typedef struct bot_levelitem_s
{
    bot_goal_t goal;
    char classname[64];
    float base_weight;
    float respawntime;
    float next_respawn_time;
    int flags;
    bool valid;
} bot_levelitem_t;

static bot_levelitem_t g_levelitems[BOT_GOAL_MAX_LEVELITEMS];
static int g_levelitem_count = 0;

static char g_iteminfo_names[BOT_GOAL_MAX_LEVELITEMS][64];
static int g_iteminfo_count = 0;

static int BotGoal_PointAreaNum(const vec3_t origin);
static bot_goalstate_t *BotGoalStateFromHandle(int handle);
static bool BotGoal_EnsureWeightCapacity(bot_goalstate_t *gs);
static float BotGoal_EvaluateItemWeight(const bot_goalstate_t *gs,
                                        const int *inventory,
                                        int iteminfo_index);
static bool BotGoal_IsAvoided(const bot_goalstate_t *gs, int number);
static bot_levelitem_t *BotGoal_FindLevelItem(int number);
static int BotGoal_FindItemInfoIndex(const char *classname);
static int BotGoal_RegisterItemInfo(const char *classname);
static bool BotGoal_BuildWeightPath(const char *filename, char *buffer, size_t size);

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
        return 0;
    }

    char path[BOT_GOAL_ASSET_MAX_PATH];
    if (!BotGoal_BuildWeightPath(filename, path, sizeof(path)))
    {
        BotLib_Print(PRT_ERROR, "BotLoadItemWeights: unable to resolve %s\n", filename);
        return 0;
    }

    bot_weight_config_t *config = ReadWeightConfig(path);
    if (config == NULL)
    {
        BotLib_Print(PRT_FATAL, "BotLoadItemWeights: couldn't load %s\n", path);
        return 0;
    }

    if (gs->itemweightconfig != NULL)
    {
        FreeWeightConfig(gs->itemweightconfig);
        gs->itemweightconfig = NULL;
    }

    gs->itemweightconfig = config;

    if (!BotGoal_EnsureWeightCapacity(gs))
    {
        BotLib_Print(PRT_ERROR, "BotLoadItemWeights: weight index allocation failed\n");
        return 0;
    }

    for (int i = 0; i < gs->itemweightcount; ++i)
    {
        const char *classname = g_iteminfo_names[i];
        gs->itemweightindex[i] = BotWeight_FindIndex(gs->itemweightconfig, classname);
        if (gs->itemweightindex[i] < 0)
        {
            BotLib_Print(PRT_WARNING,
                         "BotLoadItemWeights: item '%s' missing weight definition in %s\n",
                         classname,
                         path);
        }
    }

    return 1;
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

int BotWeightIndex(int handle, const char *classname)
{
    bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
    if (gs == NULL || classname == NULL)
    {
        return -1;
    }

    int index = BotGoal_FindItemInfoIndex(classname);
    if (index < 0 || index >= gs->itemweightcount)
    {
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

static bool BotGoal_IsAvoided(const bot_goalstate_t *gs, int number)
{
    if (gs == NULL)
    {
        return false;
    }

    float now = BotGoal_CurrentTime();
    for (int i = 0; i < gs->numavoidgoals; ++i)
    {
        if (gs->avoidgoals[i].number == number && gs->avoidgoals[i].timeout > now)
        {
            return true;
        }
    }
    return false;
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
    }

    return index;
}

int BotGoal_RegisterLevelItem(const bot_levelitem_setup_t *setup)
{
    if (setup == NULL || setup->classname == NULL || setup->classname[0] == '\0')
    {
        return 0;
    }

    int iteminfo = BotGoal_RegisterItemInfo(setup->classname);
    if (iteminfo < 0)
    {
        return 0;
    }

    bot_levelitem_t *existing = BotGoal_FindLevelItem(setup->goal.number);
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
    slot->flags = setup->flags;
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

static float BotGoal_LevelItemScore(bot_goalstate_t *gs,
                                    const bot_levelitem_t *item,
                                    const vec3_t origin,
                                    int start_area,
                                    const int *inventory,
                                    int travelflags,
                                    int *travel_time)
{
    if (item == NULL || !item->valid)
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

    float weight = BotGoal_EvaluateItemWeight(gs, inventory, item->goal.iteminfo);
    weight += item->base_weight;
    if (weight <= 0.0f)
    {
        return -FLT_MAX;
    }

    float score = weight - (float)time * BOT_GOAL_TRAVELTIME_SCALE;
    return score;
}

int BotChooseLTGItem(int handle, const vec3_t origin, const int *inventory, int travelflags)
{
    bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
    if (gs == NULL)
    {
        return 0;
    }

    int start_area = BotGoal_PointAreaNum(origin);
    if (start_area <= 0)
    {
        start_area = gs->lastreachabilityarea;
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

        if (BotGoal_IsAvoided(gs, item->goal.number))
        {
            continue;
        }

        if (item->next_respawn_time > now)
        {
            continue;
        }

        int travel_time = 0;
        float score = BotGoal_LevelItemScore(gs, item, origin, start_area, inventory, travelflags, &travel_time);
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

    gs->lastreachabilityarea = start_area;
    return BotPushGoal(handle, &best_goal);
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

    int start_area = BotGoal_PointAreaNum(origin);
    if (start_area <= 0)
    {
        start_area = gs->lastreachabilityarea;
    }

    float now = BotGoal_CurrentTime();
    float best_score = -FLT_MAX;
    const bot_levelitem_t *best_item = NULL;
    bot_goal_t best_goal = {0};
    float max_travel_time = (maxtime > 0.0f) ? (maxtime / BOT_GOAL_TRAVELTIME_SCALE) : 0.0f;

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

        if (BotGoal_IsAvoided(gs, item->goal.number))
        {
            continue;
        }

        if (item->next_respawn_time > now)
        {
            continue;
        }

        int travel_time = 0;
        float score = BotGoal_LevelItemScore(gs, item, origin, start_area, inventory, travelflags, &travel_time);
        if (score <= best_score)
        {
            continue;
        }

        if (max_travel_time > 0.0f && (float)travel_time > max_travel_time)
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

    return BotPushGoal(handle, &best_goal);
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
                                       &computed_travel);
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
