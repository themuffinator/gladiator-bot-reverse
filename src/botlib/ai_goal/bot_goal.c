#include "bot_goal.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "botlib/aas/aas_local.h"
#include "botlib/aas/aas_map.h"
#include "botlib/common/l_assets.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
<<<<<<< Updated upstream
<<<<<<< Updated upstream
<<<<<<< Updated upstream
#include "botlib/interface/botlib_interface.h"
#include "q2bridge/bridge.h"
=======
=======
>>>>>>> Stashed changes
=======
>>>>>>> Stashed changes
#include "botlib/common/l_precomp.h"
#include "botlib/common/l_struct.h"
#include "q2bridge/bridge.h"
#include "q2bridge/botlib.h"
<<<<<<< Updated upstream
<<<<<<< Updated upstream
>>>>>>> Stashed changes
=======
>>>>>>> Stashed changes
=======
>>>>>>> Stashed changes

#define BOT_GOAL_MAX_LEVELITEMS 512
#define BOT_GOAL_TRAVELTIME_SCALE 0.01f
#define BOT_GOAL_ASSET_MAX_PATH 512

void StripDoubleQuotes(char *string);
void SourceError(pc_source_t *source, char *str, ...);

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

typedef struct bot_iteminfo_s
{
	char name[MAX_STRINGFIELD];
	char classname[MAX_STRINGFIELD];
	char model[MAX_STRINGFIELD];
	int modelindex;
	int type;
	int index;
	float respawntime;
	vec3_t mins;
	vec3_t maxs;
	int number;
} bot_iteminfo_t;

typedef struct bot_itemconfig_s
{
	int numiteminfo;
	bot_iteminfo_t *iteminfo;
} bot_itemconfig_t;

#define BOT_ITEMINFO_OFS(x) ((int)(offsetof(bot_iteminfo_t, x)))

static fielddef_t g_iteminfo_fields[] = {
	{"name", BOT_ITEMINFO_OFS(name), FT_STRING},
	{"model", BOT_ITEMINFO_OFS(model), FT_STRING},
	{"modelindex", BOT_ITEMINFO_OFS(modelindex), FT_INT},
	{"type", BOT_ITEMINFO_OFS(type), FT_INT},
	{"index", BOT_ITEMINFO_OFS(index), FT_INT},
	{"respawntime", BOT_ITEMINFO_OFS(respawntime), FT_FLOAT},
	{"mins", BOT_ITEMINFO_OFS(mins), FT_FLOAT | FT_ARRAY, 3},
	{"maxs", BOT_ITEMINFO_OFS(maxs), FT_FLOAT | FT_ARRAY, 3},
	{NULL, 0, 0}
};

static structdef_t g_iteminfo_struct = {
	sizeof(bot_iteminfo_t), g_iteminfo_fields
};

static bot_levelitem_t g_levelitems[BOT_GOAL_MAX_LEVELITEMS];
static int g_levelitem_count = 0;
static int g_levelitem_limit = BOT_GOAL_MAX_LEVELITEMS;
static int g_levelitem_static_count = 0;

static const char **g_goal_map_models = NULL;
static int g_goal_map_model_count = 0;

static char g_iteminfo_names[BOT_GOAL_MAX_LEVELITEMS][MAX_STRINGFIELD];
static int g_iteminfo_count = 0;
static bot_itemconfig_t *g_itemconfig = NULL;

static int BotGoal_PointAreaNum(const vec3_t origin);
static bot_goalstate_t *BotGoalStateFromHandle(int handle);
static bot_itemconfig_t *BotGoal_ParseItemConfig(const char *filename);
static void BotGoal_StoreItemInfoNames(const bot_itemconfig_t *config);
static int *BotGoal_BuildItemWeightIndex(const bot_weight_config_t *config);
static bool BotGoal_EnsureWeightCapacity(bot_goalstate_t *gs);
static float BotGoal_EvaluateItemWeight(const bot_goalstate_t *gs,
                                        const int *inventory,
                                        int iteminfo_index);
static bool BotGoal_IsAvoided(const bot_goalstate_t *gs, int number);
static bot_levelitem_t *BotGoal_FindLevelItem(int number);
static int BotGoal_FindItemInfoIndex(const char *classname);
static int BotGoal_RegisterItemInfo(const char *classname);
static int BotGoal_FindItemInfoByModelIndex(int modelindex);
static bool BotGoal_EntityIsStationary(const aas_entity_t *entity);
static int32_t BotGoal_LittleLong(int32_t value);
static bool BotGoal_StringEndsWithIgnoreCase(const char *value, const char *suffix);
static bool BotGoal_BuildMapPath(char *buffer, size_t bufferSize, const char *mapname, const char *extension);
static bool BotGoal_LoadEntityLump(char **data, size_t *length);
static bool BotGoal_ParseVector(const char *value, vec3_t out);
static bool BotGoal_ParseIntValue(const char *value, int *out);
static bool BotGoal_DropToFloor(vec3_t origin, const vec3_t mins, const vec3_t maxs);
static void BotGoal_ClearLevelItems(void);

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

/*
=============
BotGoal_ParseItemConfig
=============
*/
static bot_itemconfig_t *BotGoal_ParseItemConfig(const char *filename)
{
	if (filename == NULL || filename[0] == '\0')
	{
		return NULL;
	}

	int max_iteminfo = (int)LibVarValue("max_iteminfo", "256");
	if (max_iteminfo < 0)
	{
		BotLib_Print(PRT_ERROR, "max_iteminfo = %d\n", max_iteminfo);
		max_iteminfo = 256;
		LibVarSet("max_iteminfo", "256");
	}

	char path[BOT_GOAL_ASSET_MAX_PATH];
	if (!BotLib_ResolveAssetPath(filename, "itemconfig", path, sizeof(path)))
	{
		BotLib_Print(PRT_ERROR, "couldn't find %s\n", filename);
		return NULL;
	}

	pc_source_t *source = PC_LoadSourceFile(path);
	if (source == NULL)
	{
		BotLib_Print(PRT_ERROR, "counldn't load %s\n", path);
		return NULL;
	}

	bot_itemconfig_t *config =
		(bot_itemconfig_t *)GetClearedMemory(sizeof(bot_itemconfig_t)
		                                     + (size_t)max_iteminfo * sizeof(bot_iteminfo_t));
	if (config == NULL)
	{
		PC_FreeSource(source);
		return NULL;
	}

	config->numiteminfo = 0;
	config->iteminfo = (bot_iteminfo_t *)((char *)config + sizeof(bot_itemconfig_t));

	pc_token_t token;
	while (PC_ReadToken(source, &token))
	{
		if (strcmp(token.string, "iteminfo") != 0)
		{
			SourceError(source, "unknown definition %s\n", token.string);
			PC_FreeSource(source);
			FreeMemory(config);
			return NULL;
		}

		if (config->numiteminfo >= max_iteminfo)
		{
			SourceError(source, "more than %d item info defined\n", max_iteminfo);
			PC_FreeSource(source);
			FreeMemory(config);
			return NULL;
		}

		bot_iteminfo_t *info = &config->iteminfo[config->numiteminfo];
		memset(info, 0, sizeof(*info));

		if (!PC_ExpectTokenType(source, TT_STRING, 0, &token))
		{
			PC_FreeSource(source);
			FreeMemory(config);
			return NULL;
		}

		StripDoubleQuotes(token.string);
		strncpy(info->classname, token.string, sizeof(info->classname) - 1);
		info->classname[sizeof(info->classname) - 1] = '\0';

		if (!ReadStructure(source, &g_iteminfo_struct, info))
		{
			PC_FreeSource(source);
			FreeMemory(config);
			return NULL;
		}

		info->number = config->numiteminfo;
		config->numiteminfo++;
	}

	PC_FreeSource(source);

	if (config->numiteminfo == 0)
	{
		BotLib_Print(PRT_WARNING, "no item info loaded\n");
	}

	BotLib_Print(PRT_MESSAGE, "loaded %s\n", path);
	return config;
}

/*
=============
BotGoal_StoreItemInfoNames
=============
*/
static void BotGoal_StoreItemInfoNames(const bot_itemconfig_t *config)
{
	g_iteminfo_count = 0;
	if (config == NULL || config->numiteminfo <= 0)
	{
		return;
	}

	int limit = config->numiteminfo;
	if (limit > BOT_GOAL_MAX_LEVELITEMS)
	{
		limit = BOT_GOAL_MAX_LEVELITEMS;
	}

	for (int i = 0; i < limit; ++i)
	{
		strncpy(g_iteminfo_names[i],
		        config->iteminfo[i].classname,
		        sizeof(g_iteminfo_names[i]) - 1);
		g_iteminfo_names[i][sizeof(g_iteminfo_names[i]) - 1] = '\0';
	}

	g_iteminfo_count = limit;
}

/*
=============
BotGoal_LoadItemConfig
=============
*/
int BotGoal_LoadItemConfig(void)
{
	const char *filename = LibVarString("itemconfig", "items.c");
	bot_itemconfig_t *config = BotGoal_ParseItemConfig(filename);
	if (config == NULL)
	{
		BotLib_Print(PRT_FATAL, "couldn't load item config\n");
		return BLERR_CANNOTLOADITEMCONFIG;
	}

	if (g_itemconfig != NULL)
	{
		FreeMemory(g_itemconfig);
	}

	g_itemconfig = config;
	BotGoal_StoreItemInfoNames(config);
	return BLERR_NOERROR;
}

/*
=============
BotGoal_ShutdownItemConfig
=============
*/
void BotGoal_ShutdownItemConfig(void)
{
	if (g_itemconfig != NULL)
	{
		FreeMemory(g_itemconfig);
		g_itemconfig = NULL;
	}

	g_iteminfo_count = 0;
}

/*
=============
BotGoal_SetMapModels
=============
*/
void BotGoal_SetMapModels(const char **models, int count)
{
	g_goal_map_models = models;
	g_goal_map_model_count = (models != NULL && count > 0) ? count : 0;

	if (g_itemconfig == NULL || g_itemconfig->numiteminfo <= 0)
	{
		return;
	}

	for (int i = 0; i < g_itemconfig->numiteminfo; ++i)
	{
		int modelindex = 0;
		const char *model = g_itemconfig->iteminfo[i].model;

		if (model != NULL && model[0] != '\0' && g_goal_map_models != NULL)
		{
			for (int j = 0; j < g_goal_map_model_count; ++j)
			{
				const char *entry = g_goal_map_models[j];
				if (entry == NULL || entry[0] == '\0')
				{
					continue;
				}
				if (Q_stricmp(entry, model) == 0)
				{
					modelindex = j;
					break;
				}
			}
		}

		g_itemconfig->iteminfo[i].modelindex = modelindex;
	}
}

/*
=============
BotGoal_BuildItemWeightIndex
=============
*/
static int *BotGoal_BuildItemWeightIndex(const bot_weight_config_t *config)
{
	if (config == NULL || g_itemconfig == NULL || g_itemconfig->numiteminfo <= 0)
	{
		return NULL;
	}

	int *index = (int *)GetClearedMemory(sizeof(int) * (size_t)g_itemconfig->numiteminfo);
	if (index == NULL)
	{
		return NULL;
	}

	for (int i = 0; i < g_itemconfig->numiteminfo; ++i)
	{
		index[i] = BotWeight_FindIndex(config, g_itemconfig->iteminfo[i].classname);
		if (index[i] < 0)
		{
			BotLib_LogWrite("item info %d \"%s\" has no fuzzy weight\r\n",
			                i,
			                g_itemconfig->iteminfo[i].classname);
		}
	}

	return index;
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
BotLoadItemWeights
=============
*/
int BotLoadItemWeights(int handle, const char *filename)
{
	bot_goalstate_t *gs = BotGoalStateFromHandle(handle);
	if (gs == NULL)
	{
<<<<<<< Updated upstream
<<<<<<< Updated upstream
<<<<<<< Updated upstream
		BotLib_Print(PRT_ERROR, "BotLoadItemWeights: invalid goal state %d\n", handle);
		return 0;
	}

	char path[BOT_GOAL_ASSET_MAX_PATH];
	if (!BotGoal_BuildWeightPath(filename, path, sizeof(path)))
	{
		BotLib_Print(PRT_ERROR,
					 "BotLoadItemWeights: unable to resolve %s\n",
					 filename != NULL ? filename : "<null>");
		return 0;
	}

	bot_weight_config_t *config = ReadWeightConfig(path);
	if (config == NULL)
	{
		BotLib_Print(PRT_FATAL, "BotLoadItemWeights: couldn't load %s\n", path);
		return 0;
	}

	if (!BotGoal_EnsureWeightCapacity(gs))
	{
		BotLib_Print(PRT_ERROR, "BotLoadItemWeights: weight index allocation failed\n");
		FreeWeightConfig(config);
		return 0;
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

	return 1;
=======
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	bot_weight_config_t *config = ReadWeightConfig(filename);
	if (config == NULL)
	{
		BotLib_Print(PRT_FATAL, "couldn't load weights\n");
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	if (g_itemconfig == NULL)
	{
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	int *index = BotGoal_BuildItemWeightIndex(config);
	if (index == NULL)
	{
		FreeWeightConfig(config);
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

=======
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	bot_weight_config_t *config = ReadWeightConfig(filename);
	if (config == NULL)
	{
		BotLib_Print(PRT_FATAL, "couldn't load weights\n");
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	if (g_itemconfig == NULL)
	{
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	int *index = BotGoal_BuildItemWeightIndex(config);
	if (index == NULL)
	{
		FreeWeightConfig(config);
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

>>>>>>> Stashed changes
=======
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	bot_weight_config_t *config = ReadWeightConfig(filename);
	if (config == NULL)
	{
		BotLib_Print(PRT_FATAL, "couldn't load weights\n");
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	if (g_itemconfig == NULL)
	{
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

	int *index = BotGoal_BuildItemWeightIndex(config);
	if (index == NULL)
	{
		FreeWeightConfig(config);
		return BLERR_CANNOTLOADITEMWEIGHTS;
	}

>>>>>>> Stashed changes
	gs->itemweightconfig = config;
	gs->itemweightindex = index;
	gs->itemweightcount = g_itemconfig->numiteminfo;
	return BLERR_NOERROR;
<<<<<<< Updated upstream
<<<<<<< Updated upstream
>>>>>>> Stashed changes
=======
>>>>>>> Stashed changes
=======
>>>>>>> Stashed changes
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

/*
=============
BotGoal_FindItemInfoByModelIndex
=============
*/
static int BotGoal_FindItemInfoByModelIndex(int modelindex)
{
	if (g_itemconfig == NULL || g_itemconfig->numiteminfo <= 0)
	{
		return -1;
	}

	if (modelindex <= 0)
	{
		return -1;
	}

	for (int i = 0; i < g_itemconfig->numiteminfo; ++i)
	{
		if (g_itemconfig->iteminfo[i].modelindex == modelindex)
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

/*
=============
BotGoal_EntityIsStationary
=============
*/
static bool BotGoal_EntityIsStationary(const aas_entity_t *entity)
{
	if (entity == NULL)
	{
		return false;
	}

	const float epsilon = 0.01f;
	for (int axis = 0; axis < 3; ++axis)
	{
		if (fabsf(entity->origin[axis] - entity->old_origin[axis]) > epsilon)
		{
			return false;
		}
	}

	return true;
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
	slot->timeout = 0.0f;
	if ((slot->goal.flags & GFL_DROPPED) && setup->respawntime > 0.0f)
	{
		slot->timeout = BotGoal_CurrentTime() + setup->respawntime;
	}
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

/*
=============
BotItemGoalInVisButNotVisible

Checks for item goals that are in AAS visibility but blocked by a trace.
=============
*/
int BotItemGoalInVisButNotVisible(int viewer, const vec3_t eye, const vec3_t viewangles, const bot_goal_t *goal)
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
	VectorScale(middle, 0.5f, middle);
	VectorAdd(goal->origin, middle, middle);

	vec3_t start;
	VectorCopy(eye, start);
	vec3_t mins = {0.0f, 0.0f, 0.0f};
	vec3_t maxs = {0.0f, 0.0f, 0.0f};
	bsp_trace_t aas_trace = Q2_Trace(start, mins, maxs, middle, viewer, CONTENTS_SOLID);
	if (aas_trace.fraction < 1.0f)
	{
		return 0;
	}

	bsp_trace_t trace = Q2_Trace(start, mins, maxs, middle, viewer, MASK_SHOT);
	if (trace.fraction < 1.0f)
	{
		return 1;
	}

	(void)viewangles;
	return 0;
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

	size_t valueLength = strlen(value);
	size_t suffixLength = strlen(suffix);
	if (suffixLength == 0)
	{
		return true;
	}

	if (suffixLength > valueLength)
	{
		return false;
	}

	const char *valueSuffix = value + valueLength - suffixLength;
	for (size_t index = 0; index < suffixLength; ++index)
	{
		char lhs = (char)tolower((unsigned char)valueSuffix[index]);
		char rhs = (char)tolower((unsigned char)suffix[index]);
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
static bool BotGoal_BuildMapPath(char *buffer, size_t bufferSize, const char *mapname, const char *extension)
{
	if (buffer == NULL || bufferSize == 0U)
	{
		return false;
	}

	buffer[0] = '\0';

	if (mapname == NULL || *mapname == '\0')
	{
		return false;
	}

	int prefixNeeded = 1;
	if (strncmp(mapname, "maps/", 5) == 0 || strncmp(mapname, "maps\\", 5) == 0)
	{
		prefixNeeded = 0;
	}

	int written;
	if (prefixNeeded)
	{
		written = snprintf(buffer, bufferSize, "maps/%s", mapname);
	}
	else
	{
		written = snprintf(buffer, bufferSize, "%s", mapname);
	}

	if (written < 0 || (size_t)written >= bufferSize)
	{
		buffer[0] = '\0';
		return false;
	}

	if (extension != NULL && *extension != '\0'
	    && !BotGoal_StringEndsWithIgnoreCase(buffer, extension))
	{
		size_t currentLength = (size_t)written;
		if (currentLength + strlen(extension) + 1U > bufferSize)
		{
			buffer[0] = '\0';
			return false;
		}

		strncat(buffer, extension, bufferSize - currentLength - 1U);
	}

	return true;
}

/*
=============
BotGoal_LoadEntityLump
=============
*/
static bool BotGoal_LoadEntityLump(char **data, size_t *length)
{
	if (data == NULL || length == NULL)
	{
		return false;
	}

	*data = NULL;
	*length = 0U;

	if (!aasworld.loaded || aasworld.mapName[0] == '\0')
	{
		return false;
	}

	char relative[BOT_GOAL_ASSET_MAX_PATH];
	if (!BotGoal_BuildMapPath(relative, sizeof(relative), aasworld.mapName, ".bsp"))
	{
		return false;
	}

	char path[BOT_GOAL_ASSET_MAX_PATH];
	if (!BotLib_ResolveAssetPath(relative, "maps", path, sizeof(path)))
	{
		return false;
	}

	FILE *file = fopen(path, "rb");
	if (file == NULL)
	{
		return false;
	}

	q2_bsp_header_t header;
	if (fread(&header, sizeof(header), 1U, file) != 1U)
	{
		fclose(file);
		return false;
	}

	header.ident = BotGoal_LittleLong(header.ident);
	header.version = BotGoal_LittleLong(header.version);
	for (int index = 0; index < Q2_BSP_LUMP_MAX; ++index)
	{
		header.lumps[index].offset = BotGoal_LittleLong(header.lumps[index].offset);
		header.lumps[index].length = BotGoal_LittleLong(header.lumps[index].length);
	}

	if (header.ident != Q2_BSP_IDENT || header.version != Q2_BSP_VERSION)
	{
		fclose(file);
		return false;
	}

	const q2_lump_t *entitiesLump = &header.lumps[Q2_BSP_LUMP_ENTITIES];
	if (entitiesLump->length <= 0 || entitiesLump->offset < 0)
	{
		fclose(file);
		return false;
	}

	if (fseek(file, entitiesLump->offset, SEEK_SET) != 0)
	{
		fclose(file);
		return false;
	}

	size_t lumpLength = (size_t)entitiesLump->length;
	char *buffer = (char *)malloc(lumpLength + 1U);
	if (buffer == NULL)
	{
		fclose(file);
		return false;
	}

	size_t readLength = fread(buffer, 1U, lumpLength, file);
	fclose(file);
	if (readLength != lumpLength)
	{
		free(buffer);
		return false;
	}

	buffer[lumpLength] = '\0';
	*data = buffer;
	*length = lumpLength;
	return true;
}

/*
=============
BotGoal_ParseVector
=============
*/
static bool BotGoal_ParseVector(const char *value, vec3_t out)
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
BotGoal_ParseIntValue
=============
*/
static bool BotGoal_ParseIntValue(const char *value, int *out)
{
	if (value == NULL || out == NULL)
	{
		return false;
	}

	char *end = NULL;
	long parsed = strtol(value, &end, 10);
	if (end == value)
	{
		return false;
	}

	*out = (int)parsed;
	return true;
}

/*
=============
BotGoal_DropToFloor
=============
*/
static bool BotGoal_DropToFloor(vec3_t origin, const vec3_t mins, const vec3_t maxs)
{
	vec3_t end;
	vec3_t minsCopy;
	vec3_t maxsCopy;

	VectorCopy(origin, end);
	end[2] -= 100.0f;

	VectorCopy(mins, minsCopy);
	VectorCopy(maxs, maxsCopy);

	bsp_trace_t trace = Q2_Trace(origin, minsCopy, maxsCopy, end, -1, CONTENTS_SOLID);
	if (trace.startsolid)
	{
		return false;
	}

	VectorCopy(trace.endpos, origin);
	return true;
}

/*
=============
BotGoal_ClearLevelItems
=============
*/
static void BotGoal_ClearLevelItems(void)
{
	memset(g_levelitems, 0, sizeof(g_levelitems));
	g_levelitem_count = 0;
	g_levelitem_static_count = 0;
}

/*
=============
BotInitLevelItems
=============
*/
void BotInitLevelItems(void)
{
	BotGoal_ClearLevelItems();

	int max_levelitems = (int)LibVarValue("max_levelitems", "512");
	if (max_levelitems <= 0)
	{
		max_levelitems = BOT_GOAL_MAX_LEVELITEMS;
	}
	if (max_levelitems > BOT_GOAL_MAX_LEVELITEMS)
	{
		max_levelitems = BOT_GOAL_MAX_LEVELITEMS;
	}
	g_levelitem_limit = max_levelitems;

	if (g_itemconfig == NULL || g_itemconfig->numiteminfo <= 0)
	{
		return;
	}

	if (!aasworld.loaded)
	{
		return;
	}

	for (int i = 0; i < g_itemconfig->numiteminfo; ++i)
	{
		if (g_itemconfig->iteminfo[i].modelindex == 0)
		{
			BotLib_LogWrite("item %s has modelindex 0", g_itemconfig->iteminfo[i].classname);
		}
	}

	char *entityData = NULL;
	size_t entityLength = 0U;
	if (!BotGoal_LoadEntityLump(&entityData, &entityLength))
	{
		return;
	}

	pc_source_t *source = PC_LoadSourceMemory("bot_goal_entities", entityData, entityLength);
	if (source == NULL)
	{
		free(entityData);
		return;
	}

	int notspawnflags = (int)LibVarValue("notspawnflags", "2048");

	pc_token_t token;
	while (PC_ReadToken(source, &token))
	{
		if (strcmp(token.string, "{") != 0)
		{
			continue;
		}

		char classname[MAX_STRINGFIELD] = {0};
		vec3_t origin = {0.0f, 0.0f, 0.0f};
		int spawnflags = 0;
		bool hasClassname = false;
		bool hasOrigin = false;

		while (PC_ReadToken(source, &token))
		{
			if (strcmp(token.string, "}") == 0)
			{
				break;
			}

			if (token.type != TT_STRING)
			{
				continue;
			}

			StripDoubleQuotes(token.string);
			char key[MAX_STRINGFIELD];
			strncpy(key, token.string, sizeof(key) - 1U);
			key[sizeof(key) - 1U] = '\0';

			if (!PC_ReadToken(source, &token))
			{
				break;
			}

			if (token.type != TT_STRING)
			{
				continue;
			}

			StripDoubleQuotes(token.string);

			if (strcmp(key, "classname") == 0)
			{
				strncpy(classname, token.string, sizeof(classname) - 1U);
				classname[sizeof(classname) - 1U] = '\0';
				hasClassname = true;
			}
			else if (strcmp(key, "origin") == 0)
			{
				if (BotGoal_ParseVector(token.string, origin))
				{
					hasOrigin = true;
				}
			}
			else if (strcmp(key, "spawnflags") == 0)
			{
				BotGoal_ParseIntValue(token.string, &spawnflags);
			}
		}

		if (!hasClassname)
		{
			continue;
		}

		if ((spawnflags & notspawnflags) != 0)
		{
			continue;
		}

		int iteminfo_index = -1;
		for (int i = 0; i < g_itemconfig->numiteminfo; ++i)
		{
			if (strcmp(classname, g_itemconfig->iteminfo[i].classname) == 0)
			{
				iteminfo_index = i;
				break;
			}
		}

		if (iteminfo_index < 0)
		{
			BotLib_LogWrite("entity %s unkown item", classname);
			continue;
		}

		if (!hasOrigin)
		{
			BotLib_Print(PRT_ERROR, "item %s without origin\n", classname);
			continue;
		}

		const bot_iteminfo_t *iteminfo = &g_itemconfig->iteminfo[iteminfo_index];
		vec3_t itemOrigin;
		VectorCopy(origin, itemOrigin);

		if (!(spawnflags & 1))
		{
			if (!BotGoal_DropToFloor(itemOrigin, iteminfo->mins, iteminfo->maxs))
			{
				BotLib_Print(PRT_MESSAGE,
				             "%s in solid at (%1.1f %1.1f %1.1f)\n",
				             classname,
				             itemOrigin[0],
				             itemOrigin[1],
				             itemOrigin[2]);
			}
		}

		if (g_levelitem_count >= g_levelitem_limit)
		{
			BotLib_Print(PRT_FATAL, "out of level items\n");
			break;
		}

		bot_levelitem_setup_t setup = {0};
		setup.classname = iteminfo->classname;
		setup.goal.number = g_levelitem_count + 1;
		VectorCopy(itemOrigin, setup.goal.origin);
		VectorCopy(iteminfo->mins, setup.goal.mins);
		VectorCopy(iteminfo->maxs, setup.goal.maxs);
		setup.goal.areanum = 0;
		setup.goal.entitynum = 0;
		setup.flags = 0;
		setup.respawntime = iteminfo->respawntime;
		setup.weight = 0.0f;

		BotGoal_RegisterLevelItem(&setup);
	}

	PC_FreeSource(source);
	free(entityData);

	g_levelitem_static_count = g_levelitem_count;
	BotLib_Print(PRT_MESSAGE, "found %d level items\n", g_levelitem_count);
}

/*
=============
BotUpdateEntityItems
=============
*/
void BotUpdateEntityItems(void)
{
	float now = BotGoal_CurrentTime();
	const float dropped_timeout = 30.0f;

	for (int i = 0; i < g_levelitem_count; ++i)
	{
		bot_levelitem_t *item = &g_levelitems[i];
		if (!item->valid)
		{
			continue;
		}

		if ((item->goal.flags & GFL_DROPPED) == 0)
		{
			continue;
		}

		if (item->next_respawn_time > 0.0f && item->next_respawn_time <= now)
		{
			item->valid = false;
			item->goal.entitynum = 0;
			continue;
		}

		int entnum = item->goal.entitynum;
		if (entnum <= 0)
		{
			continue;
		}

		if (aasworld.entities != NULL && aasworld.maxEntities > 0)
		{
			if (entnum >= aasworld.maxEntities || !aasworld.entities[entnum].inuse)
			{
				item->valid = false;
				item->goal.entitynum = 0;
				continue;
			}

			if (g_itemconfig != NULL
			    && item->goal.iteminfo >= 0
			    && item->goal.iteminfo < g_itemconfig->numiteminfo)
			{
				int expected_model = g_itemconfig->iteminfo[item->goal.iteminfo].modelindex;
				if (expected_model > 0 && aasworld.entities[entnum].modelindex != expected_model)
				{
					item->valid = false;
					item->goal.entitynum = 0;
					continue;
				}
			}
		}
	}

	if (g_itemconfig == NULL || g_itemconfig->numiteminfo <= 0)
	{
		return;
	}

	if (aasworld.entities == NULL || aasworld.maxEntities <= 0)
	{
		return;
	}

	for (int ent = 1; ent < aasworld.maxEntities; ++ent)
	{
		const aas_entity_t *entity = &aasworld.entities[ent];
		if (!entity->inuse)
		{
			continue;
		}

		if (entity->solid != SOLID_TRIGGER)
		{
			continue;
		}

		int modelindex = entity->modelindex;
		if (modelindex <= 0)
		{
			continue;
		}

		int iteminfo_index = BotGoal_FindItemInfoByModelIndex(modelindex);
		if (iteminfo_index < 0)
		{
			continue;
		}

		if (!BotGoal_EntityIsStationary(entity))
		{
			continue;
		}

		bot_levelitem_t *linked = NULL;
		for (int i = 0; i < g_levelitem_count; ++i)
		{
			bot_levelitem_t *item = &g_levelitems[i];
			if (!item->valid)
			{
				continue;
			}
			if (item->goal.entitynum == ent)
			{
				linked = item;
				break;
			}
		}

		if (linked != NULL)
		{
			if (linked->goal.iteminfo != iteminfo_index)
			{
				if (linked->goal.flags & GFL_DROPPED)
				{
					linked->valid = false;
				}
				else
				{
					linked->goal.entitynum = 0;
				}
				linked = NULL;
			}
			else
			{
				VectorCopy(entity->origin, linked->goal.origin);
				int area = BotGoal_PointAreaNum(linked->goal.origin);
				if (area > 0)
				{
					linked->goal.areanum = area;
				}
				continue;
			}
		}

		for (int i = 0; i < g_levelitem_count; ++i)
		{
			bot_levelitem_t *item = &g_levelitems[i];
			if (!item->valid)
			{
				continue;
			}
			if (item->goal.entitynum != 0)
			{
				continue;
			}
			if (item->goal.flags & GFL_DROPPED)
			{
				continue;
			}
			if (item->goal.iteminfo != iteminfo_index)
			{
				continue;
			}

			vec3_t dir;
			VectorSubtract(item->goal.origin, entity->origin, dir);
			float dist2 = DotProduct(dir, dir);
			if (dist2 < 900.0f)
			{
				item->goal.entitynum = ent;
				VectorCopy(entity->origin, item->goal.origin);
				int area = BotGoal_PointAreaNum(item->goal.origin);
				if (area > 0)
				{
					item->goal.areanum = area;
				}
				linked = item;
				break;
			}
		}

		if (linked != NULL)
		{
			continue;
		}

		if (g_levelitem_count >= g_levelitem_limit)
		{
			BotLib_Print(PRT_FATAL, "out of level items\n");
			return;
		}

		const bot_iteminfo_t *iteminfo = &g_itemconfig->iteminfo[iteminfo_index];
		bot_levelitem_setup_t setup = {0};
		setup.classname = iteminfo->classname;
		setup.goal.number = g_levelitem_static_count + ent;
		VectorCopy(entity->origin, setup.goal.origin);
		VectorCopy(iteminfo->mins, setup.goal.mins);
		VectorCopy(iteminfo->maxs, setup.goal.maxs);
		setup.goal.areanum = 0;
		setup.goal.entitynum = ent;
		setup.flags = GFL_ITEM | GFL_DROPPED;
		setup.respawntime = 0.0f;
		setup.weight = 0.0f;

		int number = BotGoal_RegisterLevelItem(&setup);
		if (number != 0)
		{
			bot_levelitem_t *item = BotGoal_FindLevelItem(number);
			if (item != NULL)
			{
				item->next_respawn_time = now + dropped_timeout;
			}
		}
	}
}

/*
=============
BotGetLevelItemGoal
=============
*/
int BotGetLevelItemGoal(int index, char *classname, bot_goal_t *goal)
{
	if (g_itemconfig == NULL || classname == NULL || goal == NULL)
	{
		return -1;
	}

	int start = 0;
	if (index >= 0)
	{
		for (int i = 0; i < g_levelitem_count; ++i)
		{
			if (g_levelitems[i].valid && g_levelitems[i].goal.number == index)
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

		if (Q_stricmp(classname, item->classname) != 0)
		{
			continue;
		}

		*goal = item->goal;
		return item->goal.number;
	}

	return -1;
}

/*
=============
BotItemGoalInVisButNotVisible
=============
*/
int BotItemGoalInVisButNotVisible(int viewer, vec3_t eye, vec3_t viewangles, bot_goal_t *goal)
{
	(void)viewangles;

	if (goal == NULL || (goal->flags & GFL_ITEM) == 0)
	{
		return 0;
	}

	vec3_t middle;
	VectorAdd(goal->mins, goal->mins, middle);
	VectorScale(middle, 0.5f, middle);
	VectorAdd(goal->origin, middle, middle);

	bsp_trace_t trace = Q2_Trace(eye, NULL, NULL, middle, viewer, CONTENTS_SOLID);
	if (trace.fraction >= 1.0f)
	{
		if (goal->entitynum <= 0)
		{
			return 0;
		}

		if (goal->entitynum >= 0 && goal->entitynum < aasworld.maxEntities && aasworld.entities != NULL)
		{
			const aas_entity_t *entity = &aasworld.entities[goal->entitynum];
			if (entity->lastUpdateTime < aasworld.time - 0.5f)
			{
				return 1;
			}
		}
	}

	return 0;
}

/*
=============
BotGetNextCampSpotGoal
=============
*/
int BotGetNextCampSpotGoal(int num, bot_goal_t *goal)
{
	(void)num;
	(void)goal;
	return 0;
}

/*
=============
BotGetMapLocationGoal
=============
*/
int BotGetMapLocationGoal(char *name, bot_goal_t *goal)
{
	(void)name;
	(void)goal;
	return 0;
}

/*
=============
BotInterbreedGoalFuzzyLogic
=============
*/
void BotInterbreedGoalFuzzyLogic(int parent1, int parent2, int child)
{
	(void)parent1;
	(void)parent2;
	(void)child;
}

/*
=============
BotSaveGoalFuzzyLogic
=============
*/
void BotSaveGoalFuzzyLogic(int goalstate, char *filename)
{
	(void)goalstate;
	(void)filename;
}

/*
=============
BotMutateGoalFuzzyLogic
=============
*/
void BotMutateGoalFuzzyLogic(int goalstate, float range)
{
	(void)goalstate;
	(void)range;
}

/*
=============
BotSetupGoalAI
=============
*/
int BotSetupGoalAI(void)
{
	return BotGoal_LoadItemConfig();
}

/*
=============
BotShutdownGoalAI
=============
*/
void BotShutdownGoalAI(void)
{
	BotGoal_ClearLevelItems();
	BotGoal_ShutdownItemConfig();
	g_levelitem_limit = BOT_GOAL_MAX_LEVELITEMS;

	for (int handle = 1; handle <= MAX_CLIENTS; ++handle)
	{
		if (g_goalstates[handle] != NULL)
		{
			BotFreeGoalState(handle);
		}
	}
}
