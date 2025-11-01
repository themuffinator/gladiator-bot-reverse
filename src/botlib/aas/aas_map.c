#include "aas_map.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aas_local.h"
#include "../common/l_log.h"

/*
 * Global AAS world state.  The original DLL zeroed the data_100667e0 block
 * during shutdown; the struct layout mirrors that memory region.
 */
aas_world_t aasworld = {0};

static int32_t AAS_LittleLong(int32_t value)
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

static uint32_t AAS_LittleUnsigned(uint32_t value)
{
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return value;
#else
    return (uint32_t)AAS_LittleLong((int32_t)value);
#endif
}

static uint32_t AAS_CRC32Update(uint32_t crc, const void *data, size_t length)
{
    static uint32_t table[256];
    static int tableInitialised = 0;

    if (!tableInitialised)
    {
        for (uint32_t index = 0; index < 256U; ++index)
        {
            uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit)
            {
                if (value & 1U)
                {
                    value = (value >> 1) ^ 0xEDB88320U;
                }
                else
                {
                    value >>= 1;
                }
            }

            table[index] = value;
        }

        tableInitialised = 1;
    }

    const uint8_t *bytes = (const uint8_t *)data;
    crc = ~crc;
    for (size_t i = 0; i < length; ++i)
    {
        crc = table[(crc ^ bytes[i]) & 0xFFU] ^ (crc >> 8);
    }

    return ~crc;
}

static qboolean AAS_ComputeFileChecksum(const char *path, uint32_t *checksum)
{
    if (path == NULL || checksum == NULL)
    {
        return qfalse;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL)
    {
        return qfalse;
    }

    uint8_t buffer[8192];
    size_t bytesRead;
    uint32_t crc = 0U;

    while ((bytesRead = fread(buffer, 1U, sizeof(buffer), file)) > 0U)
    {
        crc = AAS_CRC32Update(crc, buffer, bytesRead);
    }

    if (ferror(file))
    {
        fclose(file);
        return qfalse;
    }

    fclose(file);
    *checksum = crc;
    return qtrue;
}

static int AAS_StringEndsWithIgnoreCase(const char *value, const char *suffix)
{
    if (value == NULL || suffix == NULL)
    {
        return 0;
    }

    size_t valueLength = strlen(value);
    size_t suffixLength = strlen(suffix);
    if (suffixLength == 0)
    {
        return 1;
    }

    if (suffixLength > valueLength)
    {
        return 0;
    }

    const char *valueSuffix = value + valueLength - suffixLength;
    for (size_t index = 0; index < suffixLength; ++index)
    {
        char lhs = (char)tolower((unsigned char)valueSuffix[index]);
        char rhs = (char)tolower((unsigned char)suffix[index]);
        if (lhs != rhs)
        {
            return 0;
        }
    }

    return 1;
}

static qboolean AAS_BuildPath(char *buffer,
                              size_t bufferSize,
                              const char *mapname,
                              const char *extension)
{
    if (buffer == NULL || bufferSize == 0U)
    {
        return qfalse;
    }

    buffer[0] = '\0';

    if (mapname == NULL || *mapname == '\0')
    {
        return qfalse;
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
        return qfalse;
    }

    if (extension != NULL && *extension != '\0'
        && !AAS_StringEndsWithIgnoreCase(buffer, extension))
    {
        size_t currentLength = (size_t)written;
        if (currentLength + strlen(extension) + 1U > bufferSize)
        {
            buffer[0] = '\0';
            return qfalse;
        }

        strncat(buffer, extension, bufferSize - currentLength - 1U);
    }

    return qtrue;
}

static long AAS_GetFileSize(FILE *file)
{
    if (file == NULL)
    {
        return -1L;
    }

    long current = ftell(file);
    if (current < 0)
    {
        return -1L;
    }

    if (fseek(file, 0L, SEEK_END) != 0)
    {
        return -1L;
    }

    long size = ftell(file);
    if (size < 0)
    {
        return -1L;
    }

    if (fseek(file, current, SEEK_SET) != 0)
    {
        return -1L;
    }

    return size;
}

static int AAS_ReadLump(FILE *file,
                        const q2_lump_t *lump,
                        size_t elementSize,
                        void **outBuffer,
                        int *outCount,
                        long fileSize,
                        int seekError,
                        int readError)
{
    if (outBuffer == NULL)
    {
        return readError;
    }

    *outBuffer = NULL;
    if (outCount != NULL)
    {
        *outCount = 0;
    }

    if (lump == NULL || file == NULL)
    {
        return readError;
    }

    if (lump->length == 0)
    {
        return BLERR_NOERROR;
    }

    if (lump->offset < 0 || lump->length < 0)
    {
        return readError;
    }

    long end = (long)lump->offset + (long)lump->length;
    if (fileSize >= 0 && (lump->offset > fileSize || end > fileSize))
    {
        return readError;
    }

    if (elementSize == 0U || (size_t)lump->length % elementSize != 0U)
    {
        return readError;
    }

    size_t count = (size_t)lump->length / elementSize;
    if (count > (size_t)INT_MAX)
    {
        return readError;
    }

    if (fseek(file, lump->offset, SEEK_SET) != 0)
    {
        return seekError;
    }

    void *buffer = malloc(count * elementSize);
    if (buffer == NULL)
    {
        return readError;
    }

    size_t read = fread(buffer, elementSize, count, file);
    if (read != count)
    {
        free(buffer);
        return readError;
    }

    *outBuffer = buffer;
    if (outCount != NULL)
    {
        *outCount = (int)count;
    }

    return BLERR_NOERROR;
}

static void AAS_ClearWorld(void)
{
    if (aasworld.entities != NULL)
    {
        free(aasworld.entities);
        aasworld.entities = NULL;
    }

    if (aasworld.areas != NULL)
    {
        free(aasworld.areas);
        aasworld.areas = NULL;
    }

    if (aasworld.reachability != NULL)
    {
        free(aasworld.reachability);
        aasworld.reachability = NULL;
    }

    if (aasworld.nodes != NULL)
    {
        free(aasworld.nodes);
        aasworld.nodes = NULL;
    }

    memset(&aasworld, 0, sizeof(aasworld));
}

int AAS_LoadMap(const char *mapname,
                int modelindexes, char *modelindex[],
                int soundindexes, char *soundindex[],
                int imageindexes, char *imageindex[])
{
    (void)modelindexes;
    (void)modelindex;
    (void)soundindexes;
    (void)soundindex;
    (void)imageindexes;
    (void)imageindex;

    if (mapname == NULL || *mapname == '\0')
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: map name not specified\n");
        return BLERR_NOAASFILE;
    }

    AAS_ClearWorld();

    strncpy(aasworld.mapName, mapname, sizeof(aasworld.mapName) - 1U);
    aasworld.mapName[sizeof(aasworld.mapName) - 1U] = '\0';

    char bspPath[MAX_FILEPATH];
    char aasPath[MAX_FILEPATH];

    if (!AAS_BuildPath(bspPath, sizeof(bspPath), mapname, ".bsp"))
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: BSP path too long for %s\n", mapname);
        return BLERR_NOAASFILE;
    }

    if (!AAS_BuildPath(aasPath, sizeof(aasPath), mapname, ".aas"))
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: AAS path too long for %s\n", mapname);
        return BLERR_NOAASFILE;
    }

    FILE *bspFile = fopen(bspPath, "rb");
    if (bspFile == NULL)
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: cannot open BSP %s (%s)\n", bspPath, strerror(errno));
        return BLERR_CANNOTOPENBSPFILE;
    }

    q2_bsp_header_t bspHeader;
    if (fread(&bspHeader, sizeof(bspHeader), 1U, bspFile) != 1U)
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: failed to read BSP header from %s\n", bspPath);
        fclose(bspFile);
        return BLERR_CANNOTREADBSPHEADER;
    }

    fclose(bspFile);

    bspHeader.ident = AAS_LittleLong(bspHeader.ident);
    bspHeader.version = AAS_LittleLong(bspHeader.version);

    if (bspHeader.ident != Q2_BSP_IDENT)
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: %s is not a Quake II BSP\n", bspPath);
        return BLERR_WRONGBSPFILEID;
    }

    if (bspHeader.version != Q2_BSP_VERSION)
    {
        BotLib_Print(PRT_ERROR,
                     "AAS_LoadMap: BSP %s has version %d (expected %d)\n",
                     bspPath,
                     bspHeader.version,
                     Q2_BSP_VERSION);
        return BLERR_WRONGBSPFILEVERSION;
    }

    uint32_t bspChecksum = 0U;
    if (!AAS_ComputeFileChecksum(bspPath, &bspChecksum))
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: failed to compute BSP checksum for %s\n", bspPath);
        return BLERR_CANNOTREADBSPHEADER;
    }

    FILE *aasFile = fopen(aasPath, "rb");
    if (aasFile == NULL)
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: cannot open AAS %s (%s)\n", aasPath, strerror(errno));
        return BLERR_CANNOTOPENAASFILE;
    }

    q2_aas_header_t aasHeader;
    if (fread(&aasHeader, sizeof(aasHeader), 1U, aasFile) != 1U)
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: failed to read AAS header from %s\n", aasPath);
        fclose(aasFile);
        return BLERR_CANNOTREADAASHEADER;
    }

    aasHeader.ident = AAS_LittleLong(aasHeader.ident);
    aasHeader.version = AAS_LittleLong(aasHeader.version);
    for (int index = 0; index < Q2_AAS_LUMP_MAX; ++index)
    {
        aasHeader.lumps[index].offset = AAS_LittleLong(aasHeader.lumps[index].offset);
        aasHeader.lumps[index].length = AAS_LittleLong(aasHeader.lumps[index].length);
    }

    if (aasHeader.ident != Q2_AAS_IDENT)
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: %s is not an AAS file\n", aasPath);
        fclose(aasFile);
        return BLERR_WRONGAASFILEID;
    }

    if (aasHeader.version == Q2_AAS_VERSION_OLD)
    {
        BotLib_Print(PRT_WARNING, "AAS_LoadMap: %s uses the deprecated AAS version 2\n", aasPath);
    }
    else if (aasHeader.version != Q2_AAS_VERSION)
    {
        BotLib_Print(PRT_ERROR,
                     "AAS_LoadMap: %s has version %d (expected %d)\n",
                     aasPath,
                     aasHeader.version,
                     Q2_AAS_VERSION);
        fclose(aasFile);
        return BLERR_WRONGAASFILEVERSION;
    }

    long aasFileSize = AAS_GetFileSize(aasFile);
    if (aasFileSize < 0)
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: failed to determine size of %s\n", aasPath);
        fclose(aasFile);
        return BLERR_CANNOTREADAASHEADER;
    }

    aas_area_t *areas = NULL;
    int numAreas = 0;
    int result = AAS_ReadLump(aasFile,
                              &aasHeader.lumps[Q2_AAS_LUMP_AREAS],
                              sizeof(aas_area_t),
                              (void **)&areas,
                              &numAreas,
                              aasFileSize,
                              BLERR_CANNOTSEEKTOAASFILE,
                              BLERR_CANNOTREADAASLUMP);
    if (result != BLERR_NOERROR)
    {
        fclose(aasFile);
        return result;
    }

    aas_reachability_t *reachability = NULL;
    int numReachability = 0;
    result = AAS_ReadLump(aasFile,
                          &aasHeader.lumps[Q2_AAS_LUMP_REACHABILITY],
                          sizeof(aas_reachability_t),
                          (void **)&reachability,
                          &numReachability,
                          aasFileSize,
                          BLERR_CANNOTSEEKTOAASFILE,
                          BLERR_CANNOTREADAASLUMP);
    if (result != BLERR_NOERROR)
    {
        free(areas);
        fclose(aasFile);
        return result;
    }

    aas_node_t *nodes = NULL;
    int numNodes = 0;
    result = AAS_ReadLump(aasFile,
                          &aasHeader.lumps[Q2_AAS_LUMP_NODES],
                          sizeof(aas_node_t),
                          (void **)&nodes,
                          &numNodes,
                          aasFileSize,
                          BLERR_CANNOTSEEKTOAASFILE,
                          BLERR_CANNOTREADAASLUMP);
    if (result != BLERR_NOERROR)
    {
        free(areas);
        free(reachability);
        fclose(aasFile);
        return result;
    }

    fclose(aasFile);

    uint32_t aasChecksum = 0U;
    if (!AAS_ComputeFileChecksum(aasPath, &aasChecksum))
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: failed to compute checksum for %s\n", aasPath);
        free(areas);
        free(reachability);
        free(nodes);
        return BLERR_CANNOTREADAASHEADER;
    }

    strncpy(aasworld.aasFilePath, aasPath, sizeof(aasworld.aasFilePath) - 1U);
    aasworld.aasFilePath[sizeof(aasworld.aasFilePath) - 1U] = '\0';

    aasworld.bspChecksum = (int)bspChecksum;
    aasworld.aasChecksum = (int)aasChecksum;
    aasworld.numAreas = numAreas;
    aasworld.areas = areas;
    aasworld.numReachability = numReachability;
    aasworld.reachability = reachability;
    aasworld.numNodes = numNodes;
    aasworld.nodes = nodes;
    aasworld.maxEntities = 0;
    aasworld.entities = NULL;
    aasworld.entitiesValid = qfalse;
    aasworld.time = 0.0f;
    aasworld.numFrames = 0;
    aasworld.loaded = qtrue;
    aasworld.initialized = qtrue;

    return BLERR_NOERROR;
}

void AAS_Shutdown(void)
{
    if (aasworld.loaded || aasworld.initialized)
    {
        BotLib_Print(PRT_MESSAGE, "AAS shutdown.\n");
    }

    AAS_ClearWorld();
}

static int AAS_EnsureEntityCapacity(int ent)
{
    if (ent < 0)
    {
        return BLERR_INVALIDENTITYNUMBER;
    }

    if (ent < aasworld.maxEntities)
    {
        return BLERR_NOERROR;
    }

    size_t previousCount = (size_t)aasworld.maxEntities;
    size_t requiredCount = (size_t)ent + 1U;
    size_t newSize = requiredCount * sizeof(aas_entity_t);

    aas_entity_t *resized = realloc(aasworld.entities, newSize);
    if (resized == NULL)
    {
        return BLERR_INVALIDENTITYNUMBER;
    }

    /* zero initialise the new tail */
    if (requiredCount > previousCount)
    {
        size_t delta = requiredCount - previousCount;
        memset(resized + previousCount, 0, delta * sizeof(aas_entity_t));
    }

    aasworld.entities = resized;
    aasworld.maxEntities = (int)requiredCount;
    return BLERR_NOERROR;
}

int AAS_UpdateEntity(int ent, bot_updateentity_t *state)
{
    if (!aasworld.loaded)
    {
        return BLERR_NOAASFILE;
    }

    if (state == NULL)
    {
        return BLERR_INVALIDENTITYNUMBER;
    }

    int ensureResult = AAS_EnsureEntityCapacity(ent);
    if (ensureResult != BLERR_NOERROR)
    {
        return ensureResult;
    }

    assert(aasworld.entities != NULL);
    aas_entity_t *entity = &aasworld.entities[ent];

    /* Preserve the historical fields before copying the new state. */
    VectorCopy(entity->origin, entity->previousOrigin);
    VectorCopy(state->angles, entity->angles);

    VectorCopy(state->origin, entity->origin);
    VectorCopy(state->old_origin, entity->old_origin);
    VectorCopy(state->mins, entity->mins);
    VectorCopy(state->maxs, entity->maxs);

    entity->number = ent;
    entity->inuse = qtrue;
    entity->solid = state->solid;
    entity->modelindex = state->modelindex;
    entity->modelindex2 = state->modelindex2;
    entity->modelindex3 = state->modelindex3;
    entity->modelindex4 = state->modelindex4;
    entity->frame = state->frame;
    entity->skinnum = state->skinnum;
    entity->effects = state->effects;
    entity->renderfx = state->renderfx;
    entity->sound = state->sound;
    entity->eventid = state->event;

    /*
     * TODO: Capture timing information (data_10062934/0x84) and route the
     * results through AAS link management once entity areas are implemented.
     */

    aasworld.entitiesValid = qtrue;
    return BLERR_NOERROR;
}
