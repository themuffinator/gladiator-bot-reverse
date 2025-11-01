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

/*
 * Travel type definitions reconstructed from the Quake III / Gladiator
 * binaries. The values mirror the original enum encoded in the reachability
 * lumps and the move code switch statements.
 */
#define MAX_TRAVELTYPES 32

#define TRAVEL_INVALID       1
#define TRAVEL_WALK          2
#define TRAVEL_CROUCH        3
#define TRAVEL_BARRIERJUMP   4
#define TRAVEL_JUMP          5
#define TRAVEL_LADDER        6
#define TRAVEL_WALKOFFLEDGE  7
#define TRAVEL_SWIM          8
#define TRAVEL_WATERJUMP     9
#define TRAVEL_TELEPORT      10
#define TRAVEL_ELEVATOR      11
#define TRAVEL_ROCKETJUMP    12
#define TRAVEL_BFGJUMP       13
#define TRAVEL_GRAPPLEHOOK   14
#define TRAVEL_DOUBLEJUMP    15
#define TRAVEL_RAMPJUMP      16
#define TRAVEL_STRAFEJUMP    17
#define TRAVEL_JUMPPAD       18
#define TRAVEL_FUNCBOB       19

#define TRAVELTYPE_MASK      0x00FFFFFF
#define TRAVELFLAG_NOTTEAM1  (1 << 24)
#define TRAVELFLAG_NOTTEAM2  (1 << 25)

/* Travel flags mirrored from be_aas.h so routing filters can be ported. */
#define TFL_INVALID      0x00000001
#define TFL_WALK         0x00000002
#define TFL_CROUCH       0x00000004
#define TFL_BARRIERJUMP  0x00000008
#define TFL_JUMP         0x00000010
#define TFL_LADDER       0x00000020
#define TFL_WALKOFFLEDGE 0x00000080
#define TFL_SWIM         0x00000100
#define TFL_WATERJUMP    0x00000200
#define TFL_TELEPORT     0x00000400
#define TFL_ELEVATOR     0x00000800
#define TFL_ROCKETJUMP   0x00001000
#define TFL_BFGJUMP      0x00002000
#define TFL_GRAPPLEHOOK  0x00004000
#define TFL_DOUBLEJUMP   0x00008000
#define TFL_RAMPJUMP     0x00010000
#define TFL_STRAFEJUMP   0x00020000
#define TFL_JUMPPAD      0x00040000
#define TFL_AIR          0x00080000
#define TFL_WATER        0x00100000
#define TFL_SLIME        0x00200000
#define TFL_LAVA         0x00400000
#define TFL_DONOTENTER   0x00800000
#define TFL_FUNCBOB      0x01000000
#define TFL_FLIGHT       0x02000000
#define TFL_BRIDGE       0x04000000
#define TFL_NOTTEAM1     0x08000000
#define TFL_NOTTEAM2     0x10000000

#define TFL_DEFAULT                                                                            \
    (TFL_WALK | TFL_CROUCH | TFL_BARRIERJUMP | TFL_JUMP | TFL_LADDER | TFL_WALKOFFLEDGE        \
     | TFL_SWIM | TFL_WATERJUMP | TFL_TELEPORT | TFL_ELEVATOR | TFL_AIR | TFL_WATER            \
     | TFL_JUMPPAD | TFL_FUNCBOB)

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

typedef struct aas_areasettings_s
{
    int contents;
    int areaflags;
    int presencetype;
    int cluster;
    int clusterareanum;
    int numreachableareas;
    int firstreachablearea;
} aas_areasettings_t;

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

    unsigned int *areaOccupancyBits; /* mirrors bitfield built by AAS_AreaEdict */
    size_t areaOccupancyWords;       /* number of 32-bit words allocated */
    int areaOccupancyCount;          /* total linked areas for diagnostics */
    qboolean outsideAllAreas;        /* qtrue if no valid areas were found */
    float lastOutsideUpdate;         /* aasworld.time when outsideAllAreas became true */
} aas_entity_t;

typedef struct aas_reversedreachability_s
{
    int count;
    int *reachIndexes;
} aas_reversedreachability_t;

typedef struct aas_routingcache_s
{
    int goalArea;
    int travelflags;
    unsigned short *traveltimes;
    struct aas_routingcache_s *hashNext;
    struct aas_routingcache_s *prev;
    struct aas_routingcache_s *next;
} aas_routingcache_t;

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

    int numAreaSettings;
    aas_areasettings_t *areasettings;

    int *reachabilityFromArea; /* index of the source area for each reachability */
    aas_reversedreachability_t *reversedReachability;

    int numNodes;
    aas_node_t *nodes;

    int maxEntities;
    aas_entity_t *entities; /* base pointer from data_100669a0 */

    size_t areaEntityListCount;  /* number of heads in areaEntityLists */
    aas_link_t **areaEntityLists; /* entities linked per area */

    int travelflagfortype[MAX_TRAVELTYPES];

    size_t routingCacheTableSize;
    aas_routingcache_t **routingCacheTable;
    aas_routingcache_t *routingCacheHead;
    aas_routingcache_t *routingCacheTail;
} aas_world_t;

extern aas_world_t aasworld;

void AAS_InitTravelFlagFromType(void);
void AAS_ClearReachabilityData(void);
int AAS_PrepareReachability(void);
void AAS_FreeAllRoutingCaches(void);
void AAS_InvalidateRouteCache(void);
int AAS_AreaTravelTimeToGoalArea(int areanum, vec3_t origin, int goalareanum, int travelflags);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GLADIATOR_BOTLIB_AAS_AAS_LOCAL_H */
