#include "bot_goal.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../q2bridge/botlib.h"
#include "../aas/aas_local.h"
#include "../aas/aas_route.h"
#include "../common/l_libvar.h"
#include "../common/l_log.h"
#include "../common/l_memory.h"
#include "../precomp/l_script.h"
#include "../weight/bot_weight.h"

#define ITEMCONFIG_BASE_PATH "dev_tools/assets/itemconfig/"

/* ------------------------------------------------------------------------- */
/*  Quake III compatibility constants                                        */
/* ------------------------------------------------------------------------- */

#define TRAVELTIME_SCALE 0.01f
#define AVOID_MINIMUM_TIME 10.0f
#define AVOID_DEFAULT_TIME 30.0f
#define AVOID_DROPPED_TIME 10.0f

#define IFL_NOTFREE   0x0001
#define IFL_NOTTEAM   0x0002
#define IFL_NOTSINGLE 0x0004
#define IFL_NOTBOT    0x0008
#define IFL_ROAM      0x0010

typedef struct goal_iteminfo_s
{
    char classname[64];
    char name[64];
    int type;
    int index;
    float respawntime;
    vec3_t mins;
    vec3_t maxs;
    int number;
} goal_iteminfo_t;

typedef struct goal_itemconfig_s
{
    goal_iteminfo_t *items;
    int item_count;
} goal_itemconfig_t;

typedef struct goal_level_item_s
{
    bot_goal_t goal;
    int iteminfo;
    int number;
    int flags;
    float weight_scale;
    float timeout;
} goal_level_item_t;

struct bot_goalstate_s
{
    bot_weight_config_t *itemweightconfig;
    int *itemweightindex;
    int itemweightcount;
    int client;
    int lastreachabilityarea;
    bot_goal_t goalstack[MAX_GOALSTACK];
    int goalstacktop;
    int avoidgoals[MAX_AVOIDGOALS];
    float avoidgoaltimes[MAX_AVOIDGOALS];
};

static bot_goalstate_t *g_goalstates[MAX_CLIENTS + 1] = {0};
static goal_itemconfig_t *g_itemconfig = NULL;
static goal_level_item_t *g_levelitems = NULL;
static size_t g_levelitem_count = 0;

/* ------------------------------------------------------------------------- */
/*  Utility helpers                                                           */
/* ------------------------------------------------------------------------- */

extern aas_world_t aasworld;

void StripDoubleQuotes(char *string);

static float Goal_CurrentTime(void)
{
    return aasworld.time;
}

static bot_goalstate_t *GoalStateFromHandle(int handle)
{
    if (handle <= 0 || handle > MAX_CLIENTS)
    {
        BotLib_Print(PRT_FATAL, "goal state handle %d out of range\n", handle);
        return NULL;
    }

    bot_goalstate_t *state = g_goalstates[handle];
    if (state == NULL)
    {
        BotLib_Print(PRT_FATAL, "invalid goal state %d\n", handle);
        return NULL;
    }

    return state;
}

static void Goal_FreeItemConfig(goal_itemconfig_t *config)
{
    if (config == NULL)
    {
        return;
    }

    FreeMemory(config->items);
    FreeMemory(config);
}

static void Goal_ClearLevelItems(void)
{
    FreeMemory(g_levelitems);
    g_levelitems = NULL;
    g_levelitem_count = 0;
}

static const goal_iteminfo_t *Goal_LookupItemInfo(int iteminfo)
{
    if (g_itemconfig == NULL || iteminfo < 0 || iteminfo >= g_itemconfig->item_count)
    {
        return NULL;
    }

    return &g_itemconfig->items[iteminfo];
}

static void Goal_ParseVector(pc_script_t *script, vec3_t out)
{
    if (!PS_ExpectTokenString(script, "{"))
    {
        VectorClear(out);
        return;
    }

    for (int i = 0; i < 3; ++i)
    {
        pc_token_t value;
        if (!PS_ExpectTokenType(script, TT_NUMBER, TT_INTEGER | TT_FLOAT | TT_DECIMAL, &value))
        {
            VectorClear(out);
            PS_SkipUntilString(script, "}");
            return;
        }

        out[i] = (float)value.floatvalue;

        if (i < 2)
        {
            pc_token_t comma;
            if (PS_CheckTokenString(script, ","))
            {
                continue;
            }

            /* allow whitespace separated values */
            script->script_p = script->lastscript_p;
        }
    }

    PS_ExpectTokenString(script, "}");
}

static void Goal_CopyString(char *destination, size_t capacity, const char *source)
{
    if (destination == NULL || capacity == 0)
    {
        return;
    }

    if (source == NULL)
    {
        destination[0] = '\0';
        return;
    }

    strncpy(destination, source, capacity - 1);
    destination[capacity - 1] = '\0';
}

static void Goal_ParseIteminfoBlock(pc_script_t *script, goal_itemconfig_t *config, goal_iteminfo_t *info)
{
    pc_token_t token;

    while (PS_ReadToken(script, &token))
    {
        if (!strcmp(token.string, "}"))
        {
            break;
        }

        if (!strcmp(token.string, "name"))
        {
            if (PS_ExpectTokenType(script, TT_STRING, 0, &token))
            {
                StripDoubleQuotes(token.string);
                Goal_CopyString(info->name, sizeof(info->name), token.string);
            }
            continue;
        }

        if (!strcmp(token.string, "type"))
        {
            if (PS_ExpectTokenType(script, TT_NUMBER, TT_INTEGER, &token))
            {
                info->type = (int)token.intvalue;
            }
            continue;
        }

        if (!strcmp(token.string, "index"))
        {
            if (PS_ExpectTokenType(script, TT_NUMBER, TT_INTEGER, &token))
            {
                info->index = (int)token.intvalue;
            }
            continue;
        }

        if (!strcmp(token.string, "respawntime"))
        {
            if (PS_ExpectTokenType(script, TT_NUMBER, TT_INTEGER | TT_FLOAT | TT_DECIMAL, &token))
            {
                info->respawntime = (float)token.floatvalue;
            }
            continue;
        }

        if (!strcmp(token.string, "mins"))
        {
            Goal_ParseVector(script, info->mins);
            continue;
        }

        if (!strcmp(token.string, "maxs"))
        {
            Goal_ParseVector(script, info->maxs);
            continue;
        }

        /* skip values we do not currently need */
        PS_ReadToken(script, &token);
    }

    (void)config;
}

static goal_itemconfig_t *Goal_LoadItemConfig(const char *filename)
{
    if (filename == NULL || filename[0] == '\0')
    {
        return NULL;
    }

    char path[512];
    if (strchr(filename, '/') != NULL || strchr(filename, '\\') != NULL)
    {
        Goal_CopyString(path, sizeof(path), filename);
    }
    else
    {
        snprintf(path, sizeof(path), "%s%s", ITEMCONFIG_BASE_PATH, filename);
    }

    pc_script_t *script = LoadScriptFile(path);
    if (script == NULL)
    {
        BotLib_Print(PRT_ERROR, "couldn't load %s\n", path);
        return NULL;
    }

    int max_items = (int)LibVarValue("max_iteminfo", "256");
    if (max_items <= 0)
    {
        max_items = 256;
        LibVarSet("max_iteminfo", "256");
    }

    goal_itemconfig_t *config = GetClearedMemory(sizeof(*config));
    if (config == NULL)
    {
        PS_FreeScript(script);
        BotLib_Print(PRT_FATAL, "Goal_LoadItemConfig: allocation failed\n");
        return NULL;
    }

    config->items = GetClearedMemory((size_t)max_items * sizeof(goal_iteminfo_t));
    if (config->items == NULL)
    {
        PS_FreeScript(script);
        FreeMemory(config);
        BotLib_Print(PRT_FATAL, "Goal_LoadItemConfig: allocation failed\n");
        return NULL;
    }

    pc_token_t token;
    while (PS_ReadToken(script, &token))
    {
        if (token.string[0] == '\0')
        {
            continue;
        }

        if (token.string[0] == '#')
        {
            /* Skip preprocessor directives. */
            continue;
        }

        if (strcmp(token.string, "iteminfo") != 0)
        {
            continue;
        }

        if (config->item_count >= max_items)
        {
            BotLib_Print(PRT_ERROR, "more than %d item info defined\n", max_items);
            break;
        }

        goal_iteminfo_t *info = &config->items[config->item_count];
        memset(info, 0, sizeof(*info));

        if (!PS_ExpectTokenType(script, TT_STRING, 0, &token))
        {
            break;
        }

        StripDoubleQuotes(token.string);
        Goal_CopyString(info->classname, sizeof(info->classname), token.string);

        if (!PS_ExpectTokenString(script, "{"))
        {
            break;
        }

        info->number = config->item_count;
        Goal_ParseIteminfoBlock(script, config, info);
        config->item_count += 1;
    }

    PS_FreeScript(script);

    if (config->item_count == 0)
    {
        BotLib_Print(PRT_WARNING, "no item info loaded\n");
    }
    else
    {
        BotLib_Print(PRT_MESSAGE, "loaded %s\n", path);
    }

    return config;
}

static void Goal_EnsureItemConfigLoaded(void)
{
    if (g_itemconfig != NULL)
    {
        return;
    }

    const char *filename = LibVarString("itemconfig", "items.c");
    g_itemconfig = Goal_LoadItemConfig(filename);
    if (g_itemconfig == NULL)
    {
        BotLib_Print(PRT_FATAL, "BotSetupGoalAI: failed to load %s\n", filename);
    }
}

static int *Goal_CreateWeightIndex(const bot_weight_config_t *weights, const goal_itemconfig_t *config)
{
    if (weights == NULL || config == NULL || config->item_count <= 0)
    {
        return NULL;
    }

    int *table = GetClearedMemory((size_t)config->item_count * sizeof(int));
    if (table == NULL)
    {
        return NULL;
    }

    for (int i = 0; i < config->item_count; ++i)
    {
        table[i] = BotWeight_FindIndex(weights, config->items[i].classname);
        if (table[i] < 0)
        {
            BotLib_Print(PRT_WARNING,
                         "item info %d \"%s\" has no fuzzy weight\n",
                         i,
                         config->items[i].classname);
        }
    }

    return table;
}

static void Goal_AddToAvoid(bot_goalstate_t *gs, int number, float avoidtime)
{
    if (gs == NULL)
    {
        return;
    }

    float expiry = Goal_CurrentTime() + avoidtime;

    for (int i = 0; i < MAX_AVOIDGOALS; ++i)
    {
        if (gs->avoidgoals[i] == number)
        {
            gs->avoidgoaltimes[i] = expiry;
            return;
        }
    }

    int candidate = -1;
    for (int i = 0; i < MAX_AVOIDGOALS; ++i)
    {
        if (gs->avoidgoaltimes[i] < Goal_CurrentTime())
        {
            candidate = i;
            break;
        }
    }

    if (candidate < 0)
    {
        candidate = 0;
    }

    gs->avoidgoals[candidate] = number;
    gs->avoidgoaltimes[candidate] = expiry;
}

static float Goal_ItemRespawnTime(int number)
{
    if (g_levelitems == NULL)
    {
        return 0.0f;
    }

    for (size_t index = 0; index < g_levelitem_count; ++index)
    {
        if (g_levelitems[index].number != number)
        {
            continue;
        }

        const goal_iteminfo_t *info = Goal_LookupItemInfo(g_levelitems[index].iteminfo);
        if (info == NULL)
        {
            return 0.0f;
        }

        return info->respawntime;
    }

    return 0.0f;
}

/* ------------------------------------------------------------------------- */
/*  Public API                                                                */
/* ------------------------------------------------------------------------- */

int BotAllocGoalState(int client)
{
    for (int handle = 1; handle <= MAX_CLIENTS; ++handle)
    {
        if (g_goalstates[handle] != NULL)
        {
            continue;
        }

        bot_goalstate_t *state = GetClearedMemory(sizeof(*state));
        if (state == NULL)
        {
            BotLib_Print(PRT_FATAL, "BotAllocGoalState: allocation failed\n");
            return 0;
        }

        state->client = client;
        state->goalstacktop = 0;
        g_goalstates[handle] = state;
        return handle;
    }

    BotLib_Print(PRT_ERROR, "BotAllocGoalState: no free handles\n");
    return 0;
}

void BotFreeGoalState(int handle)
{
    bot_goalstate_t *state = GoalStateFromHandle(handle);
    if (state == NULL)
    {
        return;
    }

    BotFreeItemWeights(handle);
    FreeMemory(state);
    g_goalstates[handle] = NULL;
}

void BotResetGoalState(int goalstate)
{
    bot_goalstate_t *state = GoalStateFromHandle(goalstate);
    if (state == NULL)
    {
        return;
    }

    memset(state->goalstack, 0, sizeof(state->goalstack));
    state->goalstacktop = 0;
    BotResetAvoidGoals(goalstate);
}

int BotLoadItemWeights(int goalstate, const char *filename)
{
    bot_goalstate_t *state = GoalStateFromHandle(goalstate);
    if (state == NULL)
    {
        return BLERR_CANNOTLOADITEMWEIGHTS;
    }

    Goal_EnsureItemConfigLoaded();
    if (g_itemconfig == NULL)
    {
        return BLERR_CANNOTLOADITEMCONFIG;
    }

    bot_weight_config_t *config = BotReadWeightsFile(filename);
    if (config == NULL)
    {
        BotLib_Print(PRT_FATAL, "couldn't load weights\n");
        return BLERR_CANNOTLOADITEMWEIGHTS;
    }

    int *index = Goal_CreateWeightIndex(config, g_itemconfig);
    if (g_itemconfig->item_count > 0 && index == NULL)
    {
        BotLib_Print(PRT_FATAL, "BotLoadItemWeights: failed to build weight index\n");
        FreeWeightConfig(config);
        return BLERR_CANNOTLOADITEMWEIGHTS;
    }

    if (state->itemweightconfig != NULL)
    {
        FreeWeightConfig(state->itemweightconfig);
        state->itemweightconfig = NULL;
    }

    if (state->itemweightindex != NULL)
    {
        FreeMemory(state->itemweightindex);
        state->itemweightindex = NULL;
        state->itemweightcount = 0;
    }

    state->itemweightconfig = config;
    state->itemweightindex = index;
    state->itemweightcount = (index != NULL) ? g_itemconfig->item_count : 0;
    return BLERR_NOERROR;
}

void BotFreeItemWeights(int goalstate)
{
    bot_goalstate_t *state = GoalStateFromHandle(goalstate);
    if (state == NULL)
    {
        return;
    }

    if (state->itemweightconfig != NULL)
    {
        FreeWeightConfig(state->itemweightconfig);
        state->itemweightconfig = NULL;
    }

    if (state->itemweightindex != NULL)
    {
        FreeMemory(state->itemweightindex);
        state->itemweightindex = NULL;
    }

    state->itemweightcount = 0;
}

void BotPushGoal(int goalstate, const bot_goal_t *goal)
{
    bot_goalstate_t *state = GoalStateFromHandle(goalstate);
    if (state == NULL || goal == NULL)
    {
        return;
    }

    if (state->goalstacktop >= MAX_GOALSTACK - 1)
    {
        BotLib_Print(PRT_ERROR, "goal heap overflow\n");
        return;
    }

    state->goalstacktop += 1;
    memcpy(&state->goalstack[state->goalstacktop], goal, sizeof(bot_goal_t));
}

void BotPopGoal(int goalstate)
{
    bot_goalstate_t *state = GoalStateFromHandle(goalstate);
    if (state == NULL)
    {
        return;
    }

    if (state->goalstacktop > 0)
    {
        state->goalstacktop -= 1;
    }
}

void BotEmptyGoalStack(int goalstate)
{
    bot_goalstate_t *state = GoalStateFromHandle(goalstate);
    if (state == NULL)
    {
        return;
    }

    state->goalstacktop = 0;
}

int BotGetTopGoal(int goalstate, bot_goal_t *goal)
{
    bot_goalstate_t *state = GoalStateFromHandle(goalstate);
    if (state == NULL || goal == NULL)
    {
        return qfalse;
    }

    if (state->goalstacktop <= 0)
    {
        return qfalse;
    }

    memcpy(goal, &state->goalstack[state->goalstacktop], sizeof(bot_goal_t));
    return qtrue;
}

int BotGetSecondGoal(int goalstate, bot_goal_t *goal)
{
    bot_goalstate_t *state = GoalStateFromHandle(goalstate);
    if (state == NULL || goal == NULL)
    {
        return qfalse;
    }

    if (state->goalstacktop <= 1)
    {
        return qfalse;
    }

    memcpy(goal, &state->goalstack[state->goalstacktop - 1], sizeof(bot_goal_t));
    return qtrue;
}

void BotResetAvoidGoals(int goalstate)
{
    bot_goalstate_t *state = GoalStateFromHandle(goalstate);
    if (state == NULL)
    {
        return;
    }

    memset(state->avoidgoals, 0, sizeof(state->avoidgoals));
    memset(state->avoidgoaltimes, 0, sizeof(state->avoidgoaltimes));
}

void BotRemoveFromAvoidGoals(int goalstate, int number)
{
    bot_goalstate_t *state = GoalStateFromHandle(goalstate);
    if (state == NULL)
    {
        return;
    }

    for (int i = 0; i < MAX_AVOIDGOALS; ++i)
    {
        if (state->avoidgoals[i] != number)
        {
            continue;
        }

        state->avoidgoals[i] = 0;
        state->avoidgoaltimes[i] = 0.0f;
        return;
    }
}

float BotAvoidGoalTime(int goalstate, int number)
{
    bot_goalstate_t *state = GoalStateFromHandle(goalstate);
    if (state == NULL)
    {
        return 0.0f;
    }

    float current = Goal_CurrentTime();
    for (int i = 0; i < MAX_AVOIDGOALS; ++i)
    {
        if (state->avoidgoals[i] == number && state->avoidgoaltimes[i] >= current)
        {
            return state->avoidgoaltimes[i] - current;
        }
    }

    return 0.0f;
}

void BotSetAvoidGoalTime(int goalstate, int number, float avoidtime)
{
    bot_goalstate_t *state = GoalStateFromHandle(goalstate);
    if (state == NULL)
    {
        return;
    }

    if (avoidtime < 0.0f)
    {
        float respawn = Goal_ItemRespawnTime(number);
        if (respawn <= 0.0f)
        {
            return;
        }

        avoidtime = respawn;
    }

    if (avoidtime < AVOID_MINIMUM_TIME)
    {
        avoidtime = AVOID_MINIMUM_TIME;
    }

    Goal_AddToAvoid(state, number, avoidtime);
}

static int Goal_ResolveArea(bot_goalstate_t *state, const vec3_t origin)
{
    (void)origin;

    /*
     * The full Gladiator implementation queries BotReachabilityArea. The
     * placeholder falls back to the last known area until the movement module
     * is reconstructed.
     */
    return state->lastreachabilityarea;
}

static float Goal_ComputeAvoidTime(bot_goalstate_t *state, const goal_level_item_t *item)
{
    if (item == NULL)
    {
        return 0.0f;
    }

    if (item->timeout > 0.0f)
    {
        return AVOID_DROPPED_TIME;
    }

    const goal_iteminfo_t *info = Goal_LookupItemInfo(item->iteminfo);
    if (info == NULL)
    {
        return AVOID_DEFAULT_TIME;
    }

    float avoid = info->respawntime;
    if (avoid <= 0.0f)
    {
        avoid = AVOID_DEFAULT_TIME;
    }

    if (avoid < AVOID_MINIMUM_TIME)
    {
        avoid = AVOID_MINIMUM_TIME;
    }

    return avoid;
}

static float Goal_ComputeWeight(bot_goalstate_t *state,
                                const goal_level_item_t *item,
                                const int *inventory,
                                float travel_time)
{
    if (state == NULL || item == NULL || state->itemweightconfig == NULL || inventory == NULL)
    {
        return 0.0f;
    }

    const goal_iteminfo_t *info = Goal_LookupItemInfo(item->iteminfo);
    if (info == NULL)
    {
        return 0.0f;
    }

    if (info->number < 0 || info->number >= state->itemweightcount)
    {
        return 0.0f;
    }

    int weight_index = state->itemweightindex != NULL ? state->itemweightindex[info->number] : -1;
    if (weight_index < 0)
    {
        return 0.0f;
    }

    float weight = FuzzyWeight(inventory, state->itemweightconfig, weight_index);
    if (weight <= 0.0f)
    {
        return 0.0f;
    }

    if (item->flags & IFL_ROAM)
    {
        weight *= item->weight_scale;
    }

    if (travel_time <= 0.0f)
    {
        return 0.0f;
    }

    return weight / (travel_time * TRAVELTIME_SCALE);
}

static qboolean Goal_CanUseItem(bot_goalstate_t *state,
                                const goal_level_item_t *item,
                                int gametype)
{
    if (state == NULL || item == NULL)
    {
        return qfalse;
    }

    switch (gametype)
    {
        case 0: /* free for all */
            if (item->flags & IFL_NOTFREE)
            {
                return qfalse;
            }
            break;
        case 3: /* team modes */
        default:
            if (gametype >= 3 && (item->flags & IFL_NOTTEAM))
            {
                return qfalse;
            }
            break;
    }

    if (item->flags & IFL_NOTBOT)
    {
        return qfalse;
    }

    return qtrue;
}

int BotChooseLTGItem(int goalstate, vec3_t origin, const int *inventory, int travelflags)
{
    bot_goalstate_t *state = GoalStateFromHandle(goalstate);
    if (state == NULL || state->itemweightconfig == NULL || g_itemconfig == NULL)
    {
        return qfalse;
    }

    int areanum = Goal_ResolveArea(state, origin);
    state->lastreachabilityarea = areanum;

    float best_score = 0.0f;
    const goal_level_item_t *best_item = NULL;

    for (size_t index = 0; index < g_levelitem_count; ++index)
    {
        const goal_level_item_t *item = &g_levelitems[index];
        if (!Goal_CanUseItem(state, item, (int)LibVarValue("g_gametype", "0")))
        {
            continue;
        }

        if (item->goal.areanum <= 0)
        {
            continue;
        }

        float avoid = BotAvoidGoalTime(goalstate, item->number);
        if (avoid > 0.0f)
        {
            continue;
        }

        float travel = 0.0f;
        if (areanum > 0)
        {
            travel = (float)AAS_AreaTravelTimeToGoalArea(areanum, origin, item->goal.areanum, travelflags);
        }

        if (travel <= 0.0f)
        {
            continue;
        }

        float score = Goal_ComputeWeight(state, item, inventory, travel);
        if (score <= 0.0f)
        {
            continue;
        }

        if (score > best_score)
        {
            best_score = score;
            best_item = item;
        }
    }

    if (best_item == NULL)
    {
        return qfalse;
    }

    bot_goal_t goal = best_item->goal;
    goal.flags = GFL_ITEM;
    if (best_item->flags & IFL_ROAM)
    {
        goal.flags |= GFL_ROAM;
    }
    if (best_item->timeout > 0.0f)
    {
        goal.flags |= GFL_DROPPED;
    }

    float avoid = Goal_ComputeAvoidTime(state, best_item);
    Goal_AddToAvoid(state, best_item->number, avoid);
    BotPushGoal(goalstate, &goal);
    return qtrue;
}

int BotChooseNBGItem(int goalstate,
                     vec3_t origin,
                     const int *inventory,
                     int travelflags,
                     const bot_goal_t *ltg,
                     float maxtime)
{
    bot_goalstate_t *state = GoalStateFromHandle(goalstate);
    if (state == NULL || state->itemweightconfig == NULL || g_itemconfig == NULL)
    {
        return qfalse;
    }

    int areanum = Goal_ResolveArea(state, origin);
    state->lastreachabilityarea = areanum;

    float ltg_time = 99999.0f;
    if (ltg != NULL && ltg->areanum > 0 && areanum > 0)
    {
        ltg_time = (float)AAS_AreaTravelTimeToGoalArea(areanum, origin, ltg->areanum, travelflags);
    }

    float best_score = 0.0f;
    const goal_level_item_t *best_item = NULL;

    for (size_t index = 0; index < g_levelitem_count; ++index)
    {
        const goal_level_item_t *item = &g_levelitems[index];
        if (!Goal_CanUseItem(state, item, (int)LibVarValue("g_gametype", "0")))
        {
            continue;
        }

        if (item->goal.areanum <= 0)
        {
            continue;
        }

        float avoid = BotAvoidGoalTime(goalstate, item->number);
        if (avoid > 0.0f)
        {
            continue;
        }

        float travel = 0.0f;
        if (areanum > 0)
        {
            travel = (float)AAS_AreaTravelTimeToGoalArea(areanum, origin, item->goal.areanum, travelflags);
        }

        if (travel <= 0.0f || travel >= maxtime)
        {
            continue;
        }

        float score = Goal_ComputeWeight(state, item, inventory, travel);
        if (score <= 0.0f)
        {
            continue;
        }

        float return_time = 0.0f;
        if (ltg != NULL && ltg->areanum > 0)
        {
            return_time = (float)AAS_AreaTravelTimeToGoalArea(item->goal.areanum,
                                                              item->goal.origin,
                                                              ltg->areanum,
                                                              travelflags);
        }

        if (return_time > ltg_time)
        {
            continue;
        }

        if (score > best_score)
        {
            best_score = score;
            best_item = item;
        }
    }

    if (best_item == NULL)
    {
        return qfalse;
    }

    bot_goal_t goal = best_item->goal;
    goal.flags = GFL_ITEM;
    if (best_item->flags & IFL_ROAM)
    {
        goal.flags |= GFL_ROAM;
    }
    if (best_item->timeout > 0.0f)
    {
        goal.flags |= GFL_DROPPED;
    }

    float avoid = Goal_ComputeAvoidTime(state, best_item);
    Goal_AddToAvoid(state, best_item->number, avoid);
    BotPushGoal(goalstate, &goal);
    return qtrue;
}

/* ------------------------------------------------------------------------- */
/*  Module lifecycle                                                          */
/* ------------------------------------------------------------------------- */

void BotGoal_Shutdown(void)
{
    for (int handle = 1; handle <= MAX_CLIENTS; ++handle)
    {
        if (g_goalstates[handle] == NULL)
        {
            continue;
        }

        BotFreeGoalState(handle);
    }

    Goal_FreeItemConfig(g_itemconfig);
    g_itemconfig = NULL;
    Goal_ClearLevelItems();
}

void BotGoal_SetClient(int goalstate, int client)
{
    bot_goalstate_t *state = GoalStateFromHandle(goalstate);
    if (state == NULL)
    {
        return;
    }

    state->client = client;
    state->lastreachabilityarea = 0;
}

