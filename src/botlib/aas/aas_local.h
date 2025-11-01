#ifndef GLADIATOR_BOTLIB_AAS_AAS_LOCAL_H
#define GLADIATOR_BOTLIB_AAS_AAS_LOCAL_H

#include <stddef.h>

#include "../../shared/q_shared.h"
#include "../../q2bridge/botlib.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Internal AAS data structures reconstructed from the gladiator.dll HLIL.
 * The fields mirror the memory blocks accessed by the original
 * AAS_LoadMap/AAS_UpdateEntity/AAS_Shutdown routines so that future
 * reverse-engineered code can drop straight into the placeholders.
 */

typedef struct aas_area_s
{
    int areanum;        /* sequential identifier */
    int numfaces;       /* faces contributing to the convex hull */
    int firstface;      /* index into the global face list */
    vec3_t mins;        /* absolute bounds for collision queries */
    vec3_t maxs;
    vec3_t center;      /* cached centroid used by routing */
} aas_area_t;

typedef struct aas_node_s
{
    int planenum;       /* splitting plane */
    int children[2];    /* <0 means leaf/area, 0 means solid */
} aas_node_t;

typedef struct aas_reachability_s
{
    int areanum;
    int facenum;
    int edgenum;
    vec3_t start;
    vec3_t end;
    int traveltype;
    unsigned short traveltime;
    unsigned short reserved; /* padding observed in the original binary */
} aas_reachability_t;

typedef struct aas_link_s
{
    int entnum;
    int areanum;
    struct aas_link_s *next_ent;
    struct aas_link_s *prev_ent;
    struct aas_link_s *next_area;
    struct aas_link_s *prev_area;
} aas_link_t;

typedef struct bsp_link_s
{
    int entnum;
    int leafnum;
    struct bsp_link_s *next_ent;
    struct bsp_link_s *prev_ent;
    struct bsp_link_s *next_leaf;
    struct bsp_link_s *prev_leaf;
} bsp_link_t;

typedef struct aas_entity_s
{
    qboolean inuse;         /* equivalent to *(entity + 0x00) */
    float lastUpdateTime;   /* stored at offset 0x04 */
    float deltaTime;        /* offset 0x08, time since previous update */
    int number;             /* entity slot number (offset 0x0c) */

    vec3_t origin;          /* offsets 0x10 - 0x1c */
    vec3_t angles;          /* offsets 0x1c - 0x28 */
    vec3_t old_origin;      /* offsets 0x28 - 0x34 */
    vec3_t previousOrigin;  /* offsets 0x34 - 0x40 */

    vec3_t mins;            /* offsets 0x40 - 0x4c */
    vec3_t maxs;            /* offsets 0x4c - 0x58 */

    int solid;              /* offset 0x58 */
    int modelindex;         /* offset 0x5c (stored as modelNum + 1 in HLIL) */
    int modelindex2;        /* offsets 0x60 - 0x68 capture secondary models */
    int modelindex3;
    int modelindex4;
    int frame;              /* offset 0x6c */
    int skinnum;            /* offset 0x70 (written elsewhere in HLIL) */
    int effects;            /* offset 0x74 */
    int renderfx;           /* offset 0x78 */
    int sound;              /* offset 0x7c */
    int eventid;            /* offset 0x80 */

    aas_link_t *areas;      /* offset 0x84 in original 32-bit build */
    bsp_link_t *leaves;     /* offset 0x88 in original 32-bit build */
} aas_entity_t;

typedef struct aas_world_s
{
    qboolean loaded;        /* mirrors data_100667e0 */
    qboolean initialized;   /* mirrors data_100667e4 */
    qboolean entitiesValid; /* mirrors data_100667e8 */
    float time;             /* mirrors data_100667ec */
    int numFrames;          /* frame counter updated each BotStartFrame */
    int bspChecksum;        /* checksum recorded during AAS_LoadMap */
    int aasChecksum;        /* checksum of the loaded .aas file */

    char aasFilePath[MAX_FILEPATH];
    char mapName[MAX_FILEPATH];

    int numAreas;
    aas_area_t *areas;

    int numReachability;
    aas_reachability_t *reachability;

    int numNodes;
    aas_node_t *nodes;

    int maxEntities;
    aas_entity_t *entities; /* base pointer from data_100669a0 */
} aas_world_t;

extern aas_world_t aasworld;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GLADIATOR_BOTLIB_AAS_AAS_LOCAL_H */
