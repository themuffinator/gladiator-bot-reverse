#ifndef GLADIATOR_BOTLIB_AAS_AAS_MAP_H
#define GLADIATOR_BOTLIB_AAS_AAS_MAP_H

#include <stdint.h>

#include "../../q2bridge/aas_translation.h"

/*
 * Quake II BSP file layout -------------------------------------------------
 */

typedef struct q2_lump_s
{
    int32_t offset;
    int32_t length;
} q2_lump_t;

typedef enum q2_bsp_lump_id_e
{
    Q2_BSP_LUMP_ENTITIES = 0,
    Q2_BSP_LUMP_PLANES = 1,
    Q2_BSP_LUMP_VERTICES = 2,
    Q2_BSP_LUMP_VISIBILITY = 3,
    Q2_BSP_LUMP_NODES = 4,
    Q2_BSP_LUMP_TEXINFO = 5,
    Q2_BSP_LUMP_FACES = 6,
    Q2_BSP_LUMP_LIGHTING = 7,
    Q2_BSP_LUMP_LEAFS = 8,
    Q2_BSP_LUMP_LEAFFACES = 9,
    Q2_BSP_LUMP_LEAFBRUSHES = 10,
    Q2_BSP_LUMP_EDGES = 11,
    Q2_BSP_LUMP_SURFEDGES = 12,
    Q2_BSP_LUMP_MODELS = 13,
    Q2_BSP_LUMP_BRUSHES = 14,
    Q2_BSP_LUMP_BRUSHSIDES = 15,
    Q2_BSP_LUMP_POP = 16,
    Q2_BSP_LUMP_AREAS = 17,
    Q2_BSP_LUMP_AREAPORTALS = 18,
    Q2_BSP_LUMP_MAX = 19
} q2_bsp_lump_id_t;

#define Q2_BSP_IDENT   ((int32_t)('I' | ('B' << 8) | ('S' << 16) | ('P' << 24)))
#define Q2_BSP_VERSION 38

typedef struct q2_bsp_header_s
{
    int32_t ident;
    int32_t version;
    q2_lump_t lumps[Q2_BSP_LUMP_MAX];
} q2_bsp_header_t;

/*
 * Quake II AAS file layout -------------------------------------------------
 */

typedef enum q2_aas_lump_id_e
{
    Q2_AAS_LUMP_BBOXES = 0,
    Q2_AAS_LUMP_VERTEXES = 1,
    Q2_AAS_LUMP_PLANES = 2,
    Q2_AAS_LUMP_EDGES = 3,
    Q2_AAS_LUMP_EDGEINDEX = 4,
    Q2_AAS_LUMP_FACES = 5,
    Q2_AAS_LUMP_FACEINDEX = 6,
    Q2_AAS_LUMP_AREAS = 7,
    Q2_AAS_LUMP_AREASETTINGS = 8,
    Q2_AAS_LUMP_REACHABILITY = 9,
    Q2_AAS_LUMP_NODES = 10,
    Q2_AAS_LUMP_PORTALS = 11,
    Q2_AAS_LUMP_PORTALINDEX = 12,
    Q2_AAS_LUMP_CLUSTERS = 13,
    Q2_AAS_LUMP_MAX = 14
} q2_aas_lump_id_t;

#define Q2_AAS_IDENT        ((int32_t)('E' | ('A' << 8) | ('A' << 16) | ('S' << 24)))
#define Q2_AAS_VERSION_OLD  2
#define Q2_AAS_VERSION      3

typedef struct q2_aas_header_s
{
    int32_t ident;
    int32_t version;
    q2_lump_t lumps[Q2_AAS_LUMP_MAX];
} q2_aas_header_t;

#ifdef __cplusplus
extern "C" {
#endif

int AAS_LoadMap(const char *mapname,
                int modelindexes, char *modelindex[],
                int soundindexes, char *soundindex[],
                int imageindexes, char *imageindex[]);

int AAS_Init(void);
void AAS_Shutdown(void);

int AAS_UpdateEntity(int ent, const AASEntityFrame *state);

qboolean AAS_WorldLoaded(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GLADIATOR_BOTLIB_AAS_AAS_MAP_H */
