#include "bot_goal.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "botlib/aas/aas_map.h"
#include "botlib/aas/aas_local.h"
#include "botlib/common/l_assets.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/common/l_struct.h"
#include "botlib/precomp/l_precomp.h"

void StripDoubleQuotes(char *string);
void SourceError(pc_source_t *source, char *str, ...);

#define BOT_GOAL_MAX_LEVELITEMS 512
#define BOT_GOAL_TRAVELTIME_SCALE 0.01f
#define BOT_GOAL_ASSET_MAX_PATH 512

static bot_goalstate_t *g_goalstates[MAX_CLIENTS + 1];

static float g_goal_current_time = 0.0f;

typedef struct bot_iteminfo_s
{
	char classname[MAX_STRINGFIELD];
	char name[MAX_STRINGFIELD];
	char model[MAX_STRINGFIELD];
	int modelindex;
	int type;
	int index;
	float respawntime;
	vec3_t mins;
	vec3_t maxs;
	int number;
} bot_iteminfo_t;

typedef struct bot_map_entity_s
{
	char classname[64];
	vec3_t origin;
	int spawnflags;
	int notfree;
	int notteam;
	int notsingle;
	int notbot;
	float weight;
	bool hasClassname;
	bool hasOrigin;
} bot_map_entity_t;

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

static bot_iteminfo_t g_iteminfos[BOT_GOAL_MAX_LEVELITEMS];
static int g_iteminfo_count = 0;

static int g_bot_gametype = 0;

#define BOT_GOAL_ITEM_NOTFREE   0x0008
#define BOT_GOAL_ITEM_NOTTEAM   0x0010
#define BOT_GOAL_ITEM_NOTSINGLE 0x0020
#define BOT_GOAL_ITEM_NOTBOT    0x0040

enum
{
	BOT_GOAL_GT_FFA = 0,
	BOT_GOAL_GT_TOURNAMENT = 1,
	BOT_GOAL_GT_SINGLE_PLAYER = 2,
	BOT_GOAL_GT_TEAM = 3,
	BOT_GOAL_GT_CTF = 4
};

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

#define BOT_GOAL_ITEMINFO_OFS(x) (int)&(((bot_iteminfo_t *)0)->x)

static const fielddef_t g_bot_goal_iteminfo_fields[] =
{
	{"name", BOT_GOAL_ITEMINFO_OFS(name), FT_STRING},
	{"model", BOT_GOAL_ITEMINFO_OFS(model), FT_STRING},
	{"modelindex", BOT_GOAL_ITEMINFO_OFS(modelindex), FT_INT},
	{"type", BOT_GOAL_ITEMINFO_OFS(type), FT_INT},
	{"index", BOT_GOAL_ITEMINFO_OFS(index), FT_INT},
	{"respawntime", BOT_GOAL_ITEMINFO_OFS(respawntime), FT_FLOAT},
	{"mins", BOT_GOAL_ITEMINFO_OFS(mins), FT_FLOAT | FT_ARRAY, 3},
	{"maxs", BOT_GOAL_ITEMINFO_OFS(maxs), FT_FLOAT | FT_ARRAY, 3},
	{NULL, 0, 0}
};

static const structdef_t g_bot_goal_iteminfo_struct =
{
	sizeof(bot_iteminfo_t),
	g_bot_goal_iteminfo_fields
};

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

/*
=============
BotGoal_ResetItemInfo

Clear cached item configuration data.
=============
*/
static void BotGoal_ResetItemInfo(void)
{
	g_iteminfo_count = 0;
	memset(g_iteminfos, 0, sizeof(g_iteminfos));
}

/*
=============
BotGoal_ResetLevelItems

Clear cached level item entries.
=============
*/
static void BotGoal_ResetLevelItems(void)
{
	g_levelitem_count = 0;
	for (int i = 0; i < BOT_GOAL_MAX_LEVELITEMS; ++i)
	{
		g_levelitems[i].valid = false;
	}
}

/*
=============
BotGoal_LoadItemConfig

Parse item configuration definitions into the local iteminfo table.
=============
*/
static bool BotGoal_LoadItemConfig(const char *filename)
{
	BotGoal_ResetItemInfo();

	const char *requested = filename;
	if (requested == NULL || requested[0] == '\0')
	{
		requested = LibVarString("itemconfig", "items.c");
	}

	if (requested == NULL || requested[0] == '\0')
	{
		return false;
	}

	char path[BOT_GOAL_ASSET_MAX_PATH];
	if (!BotLib_ResolveAssetPath(requested, "itemconfig", path, sizeof(path)))
	{
		BotLib_Print(PRT_WARNING, "BotGoal_LoadItemConfig: unable to resolve %s\n", requested);
		return false;
	}

	pc_source_t *source = PC_LoadSourceFile(path);
	if (source == NULL)
	{
		BotLib_Print(PRT_ERROR, "BotGoal_LoadItemConfig: unable to load %s\n", path);
		return false;
	}

	int max_iteminfo = (int)LibVarValue("max_iteminfo", "256");
	if (max_iteminfo <= 0 || max_iteminfo > BOT_GOAL_MAX_LEVELITEMS)
	{
		max_iteminfo = BOT_GOAL_MAX_LEVELITEMS;
	}

	pc_token_t token;
	while (PC_ReadToken(source, &token))
	{
		if (strcmp(token.string, "iteminfo") != 0)
		{
			SourceError(source, "unknown definition %s", token.string);
			PC_FreeSource(source);
			BotGoal_ResetItemInfo();
			return false;
		}

		if (g_iteminfo_count >= max_iteminfo || g_iteminfo_count >= BOT_GOAL_MAX_LEVELITEMS)
		{
			SourceError(source, "more than %d item info defined", max_iteminfo);
			PC_FreeSource(source);
			BotGoal_ResetItemInfo();
			return false;
		}

		if (!PC_ExpectTokenType(source, TT_STRING, 0, &token))
		{
			PC_FreeSource(source);
			BotGoal_ResetItemInfo();
			return false;
		}

		StripDoubleQuotes(token.string);
		bot_iteminfo_t *info = &g_iteminfos[g_iteminfo_count];
		memset(info, 0, sizeof(*info));
		strncpy(info->classname, token.string, sizeof(info->classname) - 1);
		info->classname[sizeof(info->classname) - 1] = '\0';

		if (!ReadStructure(source, &g_bot_goal_iteminfo_struct, info))
		{
			PC_FreeSource(source);
			BotGoal_ResetItemInfo();
			return false;
		}

		info->number = g_iteminfo_count;
		g_iteminfo_count++;
	}

	PC_FreeSource(source);

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
			             "BotGoal_LoadItemConfig: failed to expand weight index for handle %d\n",
			             handle);
		}
	}

	if (g_iteminfo_count == 0)
	{
		BotLib_Print(PRT_WARNING, "BotGoal_LoadItemConfig: no item info loaded from %s\n", path);
		return false;
	}

	BotLib_Print(PRT_MESSAGE, "BotGoal_LoadItemConfig: loaded %s\n", path);
	return true;
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
	if (suffix_length > value_length)
	{
		return false;
	}

	const char *value_suffix = value + value_length - suffix_length;
	for (size_t i = 0; i < suffix_length; ++i)
	{
		char lhs = (char)tolower((unsigned char)value_suffix[i]);
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

Resolve a BSP path from the current map name.
=============
*/
static bool BotGoal_BuildMapPath(const char *mapname, char *buffer, size_t size)
{
	if (buffer == NULL || size == 0)
	{
		return false;
	}

	buffer[0] = '\0';

	if (mapname == NULL || mapname[0] == '\0')
	{
		return false;
	}

	char requested[BOT_GOAL_ASSET_MAX_PATH];
	int prefix_needed = 1;
	if (strncmp(mapname, "maps/", 5) == 0 || strncmp(mapname, "maps\\", 5) == 0)
	{
		prefix_needed = 0;
	}

	int written;
	if (prefix_needed)
	{
		written = snprintf(requested, sizeof(requested), "maps/%s", mapname);
	}
	else
	{
		written = snprintf(requested, sizeof(requested), "%s", mapname);
	}

	if (written < 0 || (size_t)written >= sizeof(requested))
	{
		return false;
	}

	if (!BotGoal_StringEndsWithIgnoreCase(requested, ".bsp"))
	{
		size_t current_length = (size_t)written;
		if (current_length + 4 + 1 > sizeof(requested))
		{
			return false;
		}
		strncat(requested, ".bsp", sizeof(requested) - current_length - 1);
	}

	if (!BotLib_ResolveAssetPath(requested, "maps", buffer, size))
	{
		snprintf(buffer, size, "%s", requested);
		return false;
	}

	return true;
}

/*
=============
BotGoal_LittleLong
=============
*/
static int32_t BotGoal_LittleLong(int32_t value)
{
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
	uint32_t u = (uint32_t)value;
	u = (u >> 24)
	    | ((u >> 8) & 0x0000FF00U)
	    | ((u << 8) & 0x00FF0000U)
	    | (u << 24);
	return (int32_t)u;
#else
	return value;
#endif
}

/*
=============
BotGoal_ReadEntityLump

Extract the raw entity lump from the current BSP.
=============
*/
static bool BotGoal_ReadEntityLump(const char *mapname, char **buffer, size_t *length)
{
	if (buffer != NULL)
	{
		*buffer = NULL;
	}
	if (length != NULL)
	{
		*length = 0U;
	}

	char path[BOT_GOAL_ASSET_MAX_PATH];
	if (!BotGoal_BuildMapPath(mapname, path, sizeof(path)))
	{
		BotLib_Print(PRT_WARNING, "BotGoal_ReadEntityLump: unable to resolve map %s\n",
		             mapname ? mapname : "<null>");
		return false;
	}

	FILE *file = fopen(path, "rb");
	if (file == NULL)
	{
		BotLib_Print(PRT_ERROR, "BotGoal_ReadEntityLump: failed to open %s\n", path);
		return false;
	}

	q2_bsp_header_t header;
	if (fread(&header, sizeof(header), 1, file) != 1)
	{
		fclose(file);
		BotLib_Print(PRT_ERROR, "BotGoal_ReadEntityLump: failed to read BSP header %s\n", path);
		return false;
	}

	header.ident = BotGoal_LittleLong(header.ident);
	header.version = BotGoal_LittleLong(header.version);
	for (int i = 0; i < Q2_BSP_LUMP_MAX; ++i)
	{
		header.lumps[i].offset = BotGoal_LittleLong(header.lumps[i].offset);
		header.lumps[i].length = BotGoal_LittleLong(header.lumps[i].length);
	}

	if (header.ident != Q2_BSP_IDENT || header.version != Q2_BSP_VERSION)
	{
		fclose(file);
		BotLib_Print(PRT_ERROR, "BotGoal_ReadEntityLump: invalid BSP header %s\n", path);
		return false;
	}

	const q2_lump_t *lump = &header.lumps[Q2_BSP_LUMP_ENTITIES];
	if (lump->length <= 0 || lump->offset < 0)
	{
		fclose(file);
		BotLib_Print(PRT_WARNING, "BotGoal_ReadEntityLump: empty entity lump in %s\n", path);
		return false;
	}

	if (fseek(file, lump->offset, SEEK_SET) != 0)
	{
		fclose(file);
		BotLib_Print(PRT_ERROR, "BotGoal_ReadEntityLump: failed to seek entity lump in %s\n", path);
		return false;
	}

	char *data = (char *)GetClearedMemory((size_t)lump->length + 1U);
	if (data == NULL)
	{
		fclose(file);
		BotLib_Print(PRT_ERROR, "BotGoal_ReadEntityLump: out of memory reading %s\n", path);
		return false;
	}

	size_t read_bytes = fread(data, 1, (size_t)lump->length, file);
	fclose(file);
	if (read_bytes != (size_t)lump->length)
	{
		FreeMemory(data);
		BotLib_Print(PRT_ERROR, "BotGoal_ReadEntityLump: short read for %s\n", path);
		return false;
	}

	data[lump->length] = '\0';

	if (buffer != NULL)
	{
		*buffer = data;
	}
	else
	{
		FreeMemory(data);
	}

	if (length != NULL)
	{
		*length = (size_t)lump->length;
	}

	return true;
}

/*
=============
BotGoal_SkipWhitespace
=============
*/
static void BotGoal_SkipWhitespace(const char **cursor, const char *end)
{
	if (cursor == NULL || *cursor == NULL)
	{
		return;
	}

	while (*cursor < end && isspace((unsigned char)**cursor))
	{
		(*cursor)++;
	}
}

/*
=============
BotGoal_ParseQuotedToken
=============
*/
static bool BotGoal_ParseQuotedToken(const char **cursor, const char *end, char *buffer, size_t size)
{
	if (cursor == NULL || *cursor == NULL || buffer == NULL || size == 0)
	{
		return false;
	}

	BotGoal_SkipWhitespace(cursor, end);
	if (*cursor >= end || **cursor != '"')
	{
		return false;
	}

	(*cursor)++;
	size_t written = 0;
	while (*cursor < end && **cursor != '"')
	{
		char c = **cursor;
		if (c == '\\' && (*cursor + 1) < end)
		{
			(*cursor)++;
			c = **cursor;
		}
		if (written + 1 < size)
		{
			buffer[written++] = c;
		}
		(*cursor)++;
	}

	if (*cursor >= end || **cursor != '"')
	{
		return false;
	}

	(*cursor)++;
	buffer[written] = '\0';
	return true;
}

/*
=============
BotGoal_ParseVector3
=============
*/
static bool BotGoal_ParseVector3(const char *text, vec3_t out)
{
	if (text == NULL || out == NULL)
	{
		return false;
	}

	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	if (sscanf(text, "%f %f %f", &x, &y, &z) != 3)
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
BotGoal_ParseMapEntity
=============
*/
static bool BotGoal_ParseMapEntity(const char **cursor, const char *end, bot_map_entity_t *entity)
{
	if (cursor == NULL || *cursor == NULL || entity == NULL)
	{
		return false;
	}

	memset(entity, 0, sizeof(*entity));

	BotGoal_SkipWhitespace(cursor, end);
	if (*cursor >= end)
	{
		return false;
	}

	if (**cursor != '{')
	{
		return false;
	}

	(*cursor)++;

	while (true)
	{
		BotGoal_SkipWhitespace(cursor, end);
		if (*cursor >= end)
		{
			return false;
		}

		if (**cursor == '}')
		{
			(*cursor)++;
			break;
		}

		char key[64];
		char value[256];
		if (!BotGoal_ParseQuotedToken(cursor, end, key, sizeof(key)))
		{
			return false;
		}
		if (!BotGoal_ParseQuotedToken(cursor, end, value, sizeof(value)))
		{
			return false;
		}

		if (strcmp(key, "classname") == 0)
		{
			strncpy(entity->classname, value, sizeof(entity->classname) - 1);
			entity->classname[sizeof(entity->classname) - 1] = '\0';
			entity->hasClassname = true;
		}
		else if (strcmp(key, "origin") == 0)
		{
			if (BotGoal_ParseVector3(value, entity->origin))
			{
				entity->hasOrigin = true;
			}
		}
		else if (strcmp(key, "spawnflags") == 0)
		{
			entity->spawnflags = (int)strtol(value, NULL, 10);
		}
		else if (strcmp(key, "notfree") == 0)
		{
			entity->notfree = (int)strtol(value, NULL, 10);
		}
		else if (strcmp(key, "notteam") == 0)
		{
			entity->notteam = (int)strtol(value, NULL, 10);
		}
		else if (strcmp(key, "notsingle") == 0)
		{
			entity->notsingle = (int)strtol(value, NULL, 10);
		}
		else if (strcmp(key, "notbot") == 0)
		{
			entity->notbot = (int)strtol(value, NULL, 10);
		}
		else if (strcmp(key, "weight") == 0)
		{
			entity->weight = (float)strtod(value, NULL);
		}
	}

	return true;
}

/*
=============
BotGoal_ItemAllowedByGameType
=============
*/
static bool BotGoal_ItemAllowedByGameType(int flags)
{
	if ((flags & BOT_GOAL_ITEM_NOTBOT) != 0)
	{
		return false;
	}

	if (g_bot_gametype == BOT_GOAL_GT_SINGLE_PLAYER)
	{
		return (flags & BOT_GOAL_ITEM_NOTSINGLE) == 0;
	}

	if (g_bot_gametype >= BOT_GOAL_GT_TEAM)
	{
		return (flags & BOT_GOAL_ITEM_NOTTEAM) == 0;
	}

	return (flags & BOT_GOAL_ITEM_NOTFREE) == 0;
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
        const char *classname = g_iteminfos[i].classname;
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
        if (strcmp(g_iteminfos[i].classname, classname) == 0)
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

	strncpy(g_iteminfos[g_iteminfo_count].classname,
	        classname,
	        sizeof(g_iteminfos[0].classname) - 1);
	g_iteminfos[g_iteminfo_count].classname[sizeof(g_iteminfos[0].classname) - 1] = '\0';
	g_iteminfos[g_iteminfo_count].name[0] = '\0';
	g_iteminfos[g_iteminfo_count].model[0] = '\0';
	g_iteminfos[g_iteminfo_count].modelindex = 0;
	g_iteminfos[g_iteminfo_count].type = 0;
	g_iteminfos[g_iteminfo_count].index = 0;
	g_iteminfos[g_iteminfo_count].respawntime = 0.0f;
	VectorClear(g_iteminfos[g_iteminfo_count].mins);
	VectorClear(g_iteminfos[g_iteminfo_count].maxs);
	g_iteminfos[g_iteminfo_count].number = g_iteminfo_count;
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

		if (!BotGoal_ItemAllowedByGameType(item->flags))
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

		if (!BotGoal_ItemAllowedByGameType(item->flags))
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

	const char *label = item->classname;
	if (item->goal.iteminfo >= 0 && item->goal.iteminfo < g_iteminfo_count)
	{
		const bot_iteminfo_t *info = &g_iteminfos[item->goal.iteminfo];
		if (info->name[0] != '\0')
		{
			label = info->name;
		}
	}

	snprintf(name, (size_t)size, "%s", label);
}

/*
=============
BotInitLevelItems

Parse BSP entities to populate item and level-item tables.
=============
*/
void BotInitLevelItems(void)
{
	BotGoal_ResetLevelItems();
	BotGoal_LoadItemConfig(NULL);

	g_bot_gametype = (int)LibVarValue("g_gametype", "0");

	if (!AAS_WorldLoaded())
	{
		return;
	}

	char *entity_data = NULL;
	size_t entity_length = 0;
	if (!BotGoal_ReadEntityLump(aasworld.mapName, &entity_data, &entity_length))
	{
		return;
	}

	const char *cursor = entity_data;
	const char *end = entity_data + entity_length;
	int item_number = 0;
	int found_items = 0;

	while (true)
	{
		BotGoal_SkipWhitespace(&cursor, end);
		if (cursor >= end || *cursor == '\0')
		{
			break;
		}

		bot_map_entity_t entity;
		if (!BotGoal_ParseMapEntity(&cursor, end, &entity))
		{
			BotLib_Print(PRT_WARNING, "BotInitLevelItems: malformed entity data\n");
			break;
		}

		if (!entity.hasClassname)
		{
			continue;
		}

		if (!entity.hasOrigin)
		{
			BotLib_Print(PRT_WARNING,
			             "BotInitLevelItems: item %s missing origin\n",
			             entity.classname);
			continue;
		}

		int flags = 0;
		if (entity.notfree)
		{
			flags |= BOT_GOAL_ITEM_NOTFREE;
		}
		if (entity.notteam)
		{
			flags |= BOT_GOAL_ITEM_NOTTEAM;
		}
		if (entity.notsingle)
		{
			flags |= BOT_GOAL_ITEM_NOTSINGLE;
		}
		if (entity.notbot)
		{
			flags |= BOT_GOAL_ITEM_NOTBOT;
		}

		int iteminfo = BotGoal_FindItemInfoIndex(entity.classname);
		if (iteminfo < 0)
		{
			iteminfo = BotGoal_RegisterItemInfo(entity.classname);
		}

		if (iteminfo < 0)
		{
			continue;
		}

		bot_levelitem_setup_t setup;
		memset(&setup, 0, sizeof(setup));

		const bot_iteminfo_t *info = &g_iteminfos[iteminfo];
		setup.classname = info->classname;
		setup.goal.number = ++item_number;
		setup.goal.entitynum = 0;
		setup.goal.areanum = 0;
		VectorCopy(entity.origin, setup.goal.origin);
		VectorCopy(info->mins, setup.goal.mins);
		VectorCopy(info->maxs, setup.goal.maxs);
		setup.respawntime = info->respawntime;
		setup.weight = entity.weight;

		if (strcmp(entity.classname, "item_botroam") == 0)
		{
			setup.flags = flags | GFL_ROAM;
		}
		else
		{
			setup.flags = flags | GFL_ITEM;
		}

		if (!BotGoal_RegisterLevelItem(&setup))
		{
			continue;
		}

		found_items++;
	}

	FreeMemory(entity_data);
	BotLib_Print(PRT_MESSAGE, "BotInitLevelItems: found %d level items\n", found_items);
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
