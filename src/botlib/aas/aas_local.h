#ifndef GLADIATOR_BOTLIB_AAS_AAS_LOCAL_H
#define GLADIATOR_BOTLIB_AAS_AAS_LOCAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shared/q_shared.h"
#include "q2bridge/botlib.h"

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

typedef struct aas_plane_s
{
	vec3_t normal;
	float dist;
	int type;
} aas_plane_t;

/* Raw Quake II dtexinfo_t layout; retail loads each record as 0x4c bytes. */
typedef struct aas_bsptexinfo_s
{
	float vecs[2][4];
	int flags;
	int value;
	char texture[32];
	int nexttexinfo;
} aas_bsptexinfo_t;

typedef struct aas_bspmodel_s
{
	vec3_t mins;
	vec3_t maxs;
	vec3_t origin;
	int headnode;
	int firstface;
	int numfaces;
} aas_bspmodel_t;

typedef struct aas_bspnode_s
{
	int planenum;
	int children[2];
	short mins[3];
	short maxs[3];
	unsigned short firstface;
	unsigned short numfaces;
} aas_bspnode_t;

/* Raw Quake II dface_t layout; retail loads each record as 0x14 bytes. */
typedef struct aas_bspface_s
{
	unsigned short planenum;
	short side;
	int firstedge;
	short numedges;
	short texinfo;
	unsigned char styles[4];
	int lightofs;
} aas_bspface_t;

/* Raw Quake II dedge_t layout; retail loads each record as four bytes. */
typedef struct aas_bspedge_s
{
	unsigned short v[2];
} aas_bspedge_t;

/* Retail cache generated for each BSP face by its surface-extent pass. */
typedef struct aas_bspsurfaceextent_s
{
	short texturemins[2];
	short extents[2];
} aas_bspsurfaceextent_t;

typedef char aas_bspface_size[(sizeof(aas_bspface_t) == 0x14U) ? 1 : -1];
typedef char aas_bspedge_size[(sizeof(aas_bspedge_t) == 0x04U) ? 1 : -1];
typedef char aas_bspsurfaceextent_size[
	(sizeof(aas_bspsurfaceextent_t) == 0x08U) ? 1 : -1];

typedef struct aas_bspleaf_s
{
	int contents;
	short cluster;
	short area;
	short mins[3];
	short maxs[3];
	unsigned short firstleafface;
	unsigned short numleaffaces;
	unsigned short firstleafbrush;
	unsigned short numleafbrushes;
} aas_bspleaf_t;

typedef struct aas_bspbrushside_s
{
	unsigned short planenum;
	short texinfo;
} aas_bspbrushside_t;

typedef struct aas_bspbrush_s
{
	int firstside;
	int numsides;
	int contents;
} aas_bspbrush_t;

typedef struct aas_bspepair_s
{
	char *key;
	char *value;
	struct aas_bspepair_s *next;
} aas_bspepair_t;

typedef struct aas_bspentity_s
{
	aas_bspepair_t *epairs;
	struct aas_bspentity_s *next;
} aas_bspentity_t;

typedef vec3_t aas_vertex_t;

typedef struct aas_bbox_s
{
	int presencetype;
	int flags;
	vec3_t mins;
	vec3_t maxs;
} aas_bbox_t;

typedef struct aas_edge_s
{
	int v[2];
} aas_edge_t;

typedef struct aas_face_s
{
	int planenum;
	int faceflags;
	int numedges;
	int firstedge;
	int frontarea;
	int backarea;
} aas_face_t;

typedef struct aas_areainfo_s
{
	int cluster;
	int contents;
	int flags;
	int presencetype;
	vec3_t mins;
	vec3_t maxs;
	vec3_t center;
} aas_areainfo_t;

#define AAS_ENTITYTYPE_GENERAL 0
#define AAS_ENTITYTYPE_PLAYER  1
#define AAS_ENTITYTYPE_ITEM    2
#define AAS_ENTITYTYPE_MISSILE 3
#define AAS_ENTITYTYPE_MOVER   4

typedef struct aas_entityinfo_s
{
	qboolean valid;
	int type;
	int flags;
	float ltime;
	float update_time;
	int number;
	vec3_t origin;
	vec3_t angles;
	vec3_t old_origin;
	vec3_t lastvisorigin;
	vec3_t mins;
	vec3_t maxs;
	int groundent;
	int solid;
	int modelindex;
	int modelindex2;
	int modelindex3;
	int modelindex4;
	int frame;
	int skinnum;
	int eventid;
	int effects;
	int renderfx;
	int sound;
} aas_entityinfo_t;

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

/* Area settings flags mirrored from Quake III's aasfile.h. */
#ifndef PRESENCE_NONE
#define PRESENCE_NONE    1
#endif
#ifndef PRESENCE_NORMAL
#define PRESENCE_NORMAL  2
#endif
#ifndef PRESENCE_CROUCH
#define PRESENCE_CROUCH  4
#endif

#define AAS_AREACONTENTS_WATER         1
#define AAS_AREACONTENTS_LAVA          2
#define AAS_AREACONTENTS_SLIME         4
#define AAS_AREACONTENTS_CLUSTERPORTAL 8
#define AAS_AREACONTENTS_TELEPORTAL    16
#define AAS_AREACONTENTS_ROUTEPORTAL   32
#define AAS_AREACONTENTS_TELEPORTER    64
#define AAS_AREACONTENTS_JUMPPAD       128
#define AAS_AREACONTENTS_DONOTENTER    256
#define AAS_AREACONTENTS_VIEWPORTAL    512
#define AAS_AREACONTENTS_MOVER         1024
#define AAS_AREACONTENTS_NOTTEAM1      2048
#define AAS_AREACONTENTS_NOTTEAM2      4096

#define AAS_FACE_SOLID         1
#define AAS_FACE_LADDER        2
#define AAS_FACE_GROUND        4
#define AAS_FACE_GAP           8
#define AAS_FACE_LIQUID        16
#define AAS_FACE_LIQUIDSURFACE 32
#define AAS_FACE_BRIDGE        64

#define AAS_AREA_GROUNDED 1
#define AAS_AREA_LADDER   2
#define AAS_AREA_LIQUID   4
#define AAS_AREA_DISABLED 8
#define AAS_AREA_WEAPONJUMP 32
#define AAS_AREA_BRIDGE   16

#define SE_NONE                 0
#define SE_HITGROUND            1
#define SE_LEAVEGROUND          2
#define SE_ENTERWATER           4
#define SE_ENTERSLIME           8
#define SE_ENTERLAVA            16
#define SE_HITGROUNDDAMAGE      32
#define SE_GAP                  64
#define SE_TOUCHJUMPPAD         128
#define SE_TOUCHTELEPORTER      256
#define SE_ENTERAREA            512
#define SE_HITGROUNDAREA        1024
#define SE_HITBOUNDINGBOX       2048
#define SE_TOUCHCLUSTERPORTAL   4096

typedef struct aas_trace_s
{
	qboolean startsolid;
	float fraction;
	vec3_t endpos;
	int ent;
	int lastarea;
	int area;
	int planenum;
	cplane_t plane;
} aas_trace_t;

/*
 * The retail client-movement result embeds the original 0x24-byte AAS trace
 * prefix.  The reconstruction's internal aas_trace_t additionally caches the
 * resolved collision plane, so keep the public result trace compact and copy
 * the shared fields explicitly at the prediction boundary.
 */
typedef struct aas_clientmove_trace_s
{
	qboolean startsolid;
	float fraction;
	vec3_t endpos;
	int ent;
	int lastarea;
	int area;
	int planenum;
} aas_clientmove_trace_t;

typedef struct aas_clientmove_s
{
	vec3_t endpos;
	vec3_t velocity;
	aas_clientmove_trace_t trace;
	int presencetype;
	int stopevent;
	int endcontents;
	float time;
	int frames;
} aas_clientmove_t;

#define RSE_NONE          0
#define RSE_NOROUTE       1
#define RSE_USETRAVELTYPE 2
#define RSE_ENTERCONTENTS 4
#define RSE_ENTERAREA     8

typedef struct aas_predictroute_s
{
	vec3_t endpos;
	int endarea;
	int stopevent;
	int endcontents;
	int endtravelflags;
	int numareas;
	int time;
} aas_predictroute_t;

#define ALTROUTEGOAL_ALL            1
#define ALTROUTEGOAL_CLUSTERPORTALS 2
#define ALTROUTEGOAL_VIEWPORTALS    4

typedef struct aas_altroutegoal_s
{
	vec3_t origin;
	int areanum;
	unsigned short starttraveltime;
	unsigned short goaltraveltime;
	unsigned short extratraveltime;
} aas_altroutegoal_t;

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

typedef struct aas_lreachability_s
{
	int areanum;
	int facenum;
	int edgenum;
	vec3_t start;
	vec3_t end;
	int traveltype;
	unsigned short traveltime;
	struct aas_lreachability_s *next;
} aas_lreachability_t;

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

typedef struct aas_portal_s
{
	int areanum;
	int frontcluster;
	int backcluster;
	int clusterareanum[2];
} aas_portal_t;

typedef struct aas_cluster_s
{
	int numareas;
	int numportals;
	int firstportal;
} aas_cluster_t;

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
    qboolean isMover;       /* derived from the mover catalogue during translation */
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

typedef struct aas_reachabilityareas_s
{
	int firstarea;
	int numareas;
} aas_reachabilityareas_t;

typedef struct aas_routingcache_s
{
    int goalArea;
    int travelflags;
    unsigned short *traveltimes;
    int *reachabilities;
    struct aas_routingcache_s *hashNext;
    struct aas_routingcache_s *prev;
    struct aas_routingcache_s *next;
} aas_routingcache_t;

/*
 * Fixed-width mirror of the retail x86 routing-cache header.  Runtime cache
 * links use native pointers below, while this type records the original ABI
 * offsets independently of host pointer width.
 */
typedef struct aas_retailroutingcache32_s
{
	float time;
	int32_t cluster;
	int32_t areanum;
	float origin[3];
	float starttraveltime;
	int32_t travelflags;
	uint32_t prev;
	uint32_t next;
	uint16_t traveltimes[1];
} aas_retailroutingcache32_t;

/* Host-native representation of the retail variable-sized cache record. */
typedef struct aas_retailroutingcache_s
{
	float time;
	int cluster;
	int areanum;
	vec3_t origin;
	float starttraveltime;
	int travelflags;
	struct aas_retailroutingcache_s *prev;
	struct aas_retailroutingcache_s *next;
	unsigned short traveltimes[1];
} aas_retailroutingcache_t;

typedef struct aas_world_s
{
	qboolean loaded;        /* mirrors data_100667e0 */
	qboolean initialized;   /* mirrors data_100667e4 */
	qboolean entitiesValid;
	qboolean saveFile;      /* mirrors data_100667e8 */
	int numReachabilityAreas;
	float time;             /* mirrors data_100667ec */
	int bspChecksum;        /* checksum recorded during AAS_LoadMap */
	int aasChecksum;        /* checksum of the loaded .aas file */
	uint16_t bspEntityChecksum; /* retail parser-source CRC for the BSP entity lump */
	int bspEntityDataSize;  /* mirrors retail entdatasize */
	char *bspEntityData;    /* mirrors retail dentdata ownership */

    char aasFilePath[MAX_FILEPATH];
    char mapName[MAX_FILEPATH];

	int numBspModels;
	aas_bspmodel_t *bspModels;

	int numBspNodes;
	aas_bspnode_t *bspNodes;

	int numBspLeaves;
	aas_bspleaf_t *bspLeaves;
	int numBspVisibilityClusters;
	size_t bspVisibilitySize;
	unsigned char *bspVisibility;

	int bspLeafBrushIndexSize;
	unsigned short *bspLeafBrushes;

	int numBspPlanes;
	aas_plane_t *bspPlanes;

	int numBspTexInfo;
	aas_bsptexinfo_t *bspTexInfo;

	int numBspVertexes;
	vec3_t *bspVertexes;

	int numBspEdges;
	aas_bspedge_t *bspEdges;

	int bspSurfEdgeIndexSize;
	int *bspSurfEdges;

	int numBspFaces;
	aas_bspface_t *bspFaces;
	aas_bspsurfaceextent_t *bspSurfaceExtents;

	int bspLightDataSize;
	unsigned char *bspLightData;

	int numBspBrushSides;
	aas_bspbrushside_t *bspBrushSides;

	int numBspBrushes;
	aas_bspbrush_t *bspBrushes;

    int numAreas;
    aas_area_t *areas;

    int numBBoxes;
    aas_bbox_t *bboxes;

    int numVertexes;
    aas_vertex_t *vertexes;

    int numEdges;
    aas_edge_t *edges;

    int edgeIndexSize;
    int *edgeIndex;

    int numFaces;
    aas_face_t *faces;

    int faceIndexSize;
    int *faceIndex;

    int numReachability;
    aas_reachability_t *reachability;

    int numAreaSettings;
    aas_areasettings_t *areasettings;

    int *reachabilityFromArea; /* index of the source area for each reachability */
    aas_reversedreachability_t *reversedReachability;
    aas_reachabilityareas_t *reachabilityAreas;
    int *reachabilityAreaIndex;
    int reachabilityAreaIndexSize;

    int numNodes;
    aas_node_t *nodes;

    int numPlanes;
    aas_plane_t *planes;

    int numPortals;
    aas_portal_t *portals;

    int portalIndexSize;
    int *portalIndex;

    int numClusters;
    aas_cluster_t *clusters;

    int maxEntities;
	int maxClients;
    aas_entity_t *entities; /* base pointer from data_100669a0 */

    size_t areaEntityListCount;  /* number of heads in areaEntityLists */
    aas_link_t **areaEntityLists; /* entities linked per area */

    size_t bspLeafEntityListCount; /* number of heads in bspLeafEntityLists */
    bsp_link_t **bspLeafEntityLists; /* entities linked per Quake II BSP leaf */

    int travelflagfortype[MAX_TRAVELTYPES];

    size_t routingCacheTableSize;
    aas_routingcache_t **routingCacheTable;
    aas_routingcache_t *routingCacheHead;
    aas_routingcache_t *routingCacheTail;
	aas_retailroutingcache_t ***retailClusterAreaCache;
	aas_retailroutingcache_t **retailPortalCache;

    int *areacontentstravelflags;
} aas_world_t;

extern aas_world_t aasworld;

int AAS_Loaded(void);
int AAS_Initialized(void);
float AAS_Time(void);
int AAS_ConfigureEntityLimits(int maxentities, int maxclients);
int AAS_EntityVisible(int viewer,
	const vec3_t eye,
	const vec3_t viewangles,
	float fieldofview,
	int entnum);
int AAS_VisibleEntities(int viewer,
	const vec3_t eye,
	const vec3_t viewangles,
	float fieldofview,
	int maxentities,
	int *entitynums);
int AAS_NextEntity(int entnum);
void AAS_InitTravelFlagFromType(void);
int AAS_TravelFlagForType(int traveltype);
int AAS_GetAreaContentsTravelFlags(int areanum);
void AAS_InitAreaContentsTravelFlags(void);
int AAS_AreaContentsTravelFlags(int areanum);
bool AAS_AreaTravelAllowed(int areanum, int travelflags);
bool AAS_AreaDoNotEnter(int areanum);
bool AAS_AreaDisabled(int areanum);
bool AAS_AreaHasLadder(int areanum);
int AAS_AreaCluster(int areanum);
int AAS_AreaPresenceType(int areanum);
int AAS_AreaInfo(int areanum, aas_areainfo_t *info);
void AAS_PresenceTypeBoundingBox(int presencetype, vec3_t mins, vec3_t maxs);
int AAS_PointAreaNum(const vec3_t point);
int AAS_PointPresenceType(const vec3_t point);
int AAS_PointReachabilityAreaIndex(const vec3_t origin);
bsp_trace_t AAS_Trace(const vec3_t start,
                      const vec3_t mins,
                      const vec3_t maxs,
                      const vec3_t end,
                      int passent,
                      int contentmask);
int AAS_PointContents(const vec3_t point);
void AAS_EntityInfo(int entnum, aas_entityinfo_t *info);
void AAS_EntityOrigin(int entnum, vec3_t origin);
int AAS_EntityModelindex(int entnum);
int AAS_EntityRenderFX(int entnum);
int AAS_EntityModelNum(int entnum);
const char *AAS_ModelFromIndex(int index);
int IndexFromModel(const char *model);
const char *AAS_SoundFromIndex(int index);
int AAS_IndexFromSound(const char *sound);
const char *AAS_ImageFromIndex(int index);
int AAS_IndexFromImage(const char *image);
void AAS_EntitySize(int entnum, vec3_t mins, vec3_t maxs);
int AAS_OriginOfMoverWithModelNum(int modelnum, vec3_t origin);
int AAS_NearestEntity(const vec3_t origin, int modelindex);
void AAS_BSPModelMinsMaxsOrigin(int modelnum,
                                const vec3_t angles,
                                vec3_t mins,
                                vec3_t maxs,
                                vec3_t origin);
bsp_trace_t AAS_TraceBSPModel(int modelnum,
                              const vec3_t angles,
                              const vec3_t origin,
                              const vec3_t start,
                              const vec3_t mins,
                              const vec3_t maxs,
                              const vec3_t end,
                              int contentmask);
qboolean AAS_EntityCollision(int entnum,
                             const vec3_t start,
                             const vec3_t boxmins,
                             const vec3_t boxmaxs,
                             const vec3_t end,
                             int contentmask,
                             bsp_trace_t *trace);
aas_trace_t AAS_TraceClientBBox(const vec3_t start, const vec3_t end, int presencetype, int passent);
int AAS_TraceAreas(const vec3_t start, const vec3_t end, int *areas, vec3_t *points, int maxareas);
int AAS_BBoxAreas(const vec3_t absmins, const vec3_t absmaxs, int *areas, int maxareas);
aas_bspentity_t *AAS_ParseBSPEntities(const char *data, size_t length);
aas_bspentity_t *AAS_LoadBSPEntities(void);
void AAS_FreeBSPEntities(aas_bspentity_t *entities);
const char *AAS_ValueForBSPEpairKey(const aas_bspentity_t *entity, const char *key);
qboolean AAS_VectorForBSPEpairKey(const aas_bspentity_t *entity,
	const char *key,
	vec3_t value);
float AAS_FloatForBSPEpairKey(const aas_bspentity_t *entity, const char *key);
int AAS_IntForBSPEpairKey(const aas_bspentity_t *entity, const char *key);
aas_plane_t *AAS_PlaneFromNum(int planenum);
qboolean AAS_PointInsideFace(int facenum, const vec3_t point, float epsilon);
aas_face_t *AAS_AreaGroundFace(int areanum, const vec3_t point);
aas_face_t *AAS_TraceEndFace(const aas_trace_t *trace);
void AAS_FacePlane(int facenum, vec3_t normal, float *dist);
float AAS_FaceArea(const aas_face_t *face);
float AAS_AreaVolume(int areanum);
float AAS_AreaGroundFaceArea(int areanum);
void AAS_FaceCenter(int facenum, vec3_t center);
void AAS_ClearReachabilityData(void);
int AAS_PrepareReachability(void);
int AAS_AreaReachability(int areanum);
int AAS_BestReachableLinkArea(aas_link_t *areas);
int AAS_BestReachableEntityArea(int entnum);
int AAS_BestReachableArea(const vec3_t origin,
	const vec3_t mins,
	const vec3_t maxs,
	vec3_t goalorigin);
int AAS_AreaCrouch(int areanum);
int AAS_AreaSwim(int areanum);
int AAS_AreaLiquid(int areanum);
int AAS_AreaLava(int areanum);
int AAS_AreaSlime(int areanum);
int AAS_AreaGrounded(int areanum);
int AAS_AreaLadder(int areanum);
int AAS_AreaJumpPad(int areanum);
int AAS_AreaTeleporter(int areanum);
int AAS_AreaClusterPortal(int areanum);
int AAS_FallDamageDistance(void);
float AAS_FallDelta(float distance);
float AAS_MaxJumpHeight(float phys_jumpvel);
float AAS_MaxJumpDistance(float phys_jumpvel);
unsigned short AAS_BarrierJumpTravelTime(void);
qboolean AAS_ReachabilityExists(int area1num, int area2num);
int AAS_NearbySolidOrGap(const vec3_t start, const vec3_t end);
void AAS_SetupReachabilityHeap(void);
void AAS_ShutDownReachabilityHeap(void);
aas_lreachability_t *AAS_AllocReachability(void);
void AAS_FreeReachability(aas_lreachability_t *reachability);
void AAS_InitReachability(void);
int AAS_ContinueInitReachability(void);
void AAS_StoreReachability(void);
void AAS_InitClustering(void);
void AAS_Optimize(void);
int AAS_WriteAASFile(const char *filename);
int AAS_Reachability_Swim(int area1num, int area2num);
int AAS_Reachability_EqualFloorHeight(int area1num, int area2num);
int AAS_Reachability_Step_Barrier_WaterJump_WalkOffLedge(int area1num,
	int area2num);
float AAS_ClosestEdgePoints(const vec3_t v1,
	const vec3_t v2,
	const vec3_t v3,
	const vec3_t v4,
	const aas_plane_t *plane1,
	const aas_plane_t *plane2,
	vec3_t beststart,
	vec3_t bestend,
	float bestdist);
int AAS_Reachability_Jump(int area1num, int area2num);
int AAS_Reachability_Ladder(int area1num, int area2num);
int AAS_Reachability_TeleportEntityList(const aas_bspentity_t *entities);
void AAS_Reachability_Teleport(void);
int AAS_Reachability_ElevatorEntityList(const aas_bspentity_t *entities);
void AAS_Reachability_Elevator(void);
int AAS_Reachability_Grapple(int area1num, int area2num);
int AAS_SetWeaponJumpAreaFlagsEntityList(const aas_bspentity_t *entities);
void AAS_SetWeaponJumpAreaFlags(void);
int AAS_Reachability_WeaponJump(int area1num, int area2num);
void AAS_Reachability_WalkOffLedge(int areanum);
void AAS_JumpReachRunStart(const aas_reachability_t *reach, vec3_t runstart);
int AAS_AgainstLadder(const vec3_t origin);
int AAS_DropToFloor(vec3_t origin, const vec3_t mins, const vec3_t maxs);
float AAS_WeaponJumpZVelocity(const vec3_t origin, float radiusdamage);
float AAS_RocketJumpZVelocity(const vec3_t origin);
float AAS_BFGJumpZVelocity(const vec3_t origin);
int AAS_HorizontalVelocityForJump(float zvel, const vec3_t start, const vec3_t end, float *velocity);
int AAS_OnGround(const vec3_t origin, int presencetype, int passent);
int AAS_Swimming(const vec3_t origin);
int AAS_PredictClientMovement(aas_clientmove_t *move,
                              int entnum,
                              const vec3_t origin,
                              int presencetype,
                              int onground,
                              const vec3_t velocity,
                              const vec3_t cmdmove,
                              int cmdframes,
                              int maxframes,
                              float frametime,
                              int stopevent,
                              int stopareanum,
                              int visualize);
void AAS_TestMovementPrediction(int entnum,
	vec3_t origin,
	vec3_t direction);
void AAS_FreeAllRoutingCaches(void);
void AAS_InvalidateRouteCache(void);
size_t AAS_RetailRoutingCacheSize(int numtraveltimes);
qboolean AAS_InitRetailRoutingCaches(void);
void AAS_FreeRetailRoutingCaches(void);
aas_retailroutingcache_t *AAS_GetRetailAreaRoutingCache(int clusternum,
	int areanum,
	int travelflags);
aas_retailroutingcache_t *AAS_GetRetailPortalRoutingCache(int clusternum,
	int areanum,
	int travelflags);
void AAS_AgeRetailRoutingCaches(void);
int AAS_RetailAreaCacheUpdateCount(void);
int AAS_RetailPortalCacheUpdateCount(void);
int AAS_RetailFrameRoutingUpdateCount(void);
void AAS_BeginFrameRouting(void);
void AAS_ContinueInit(float time);
void AAS_UnlinkInvalidEntities(void);
void AAS_ResetEntityLinks(void);
void AAS_InitAASLinkHeap(void);
void AAS_FreeAASLinkHeap(void);
aas_link_t *AAS_AllocAASLink(void);
void AAS_FreeAASLink(aas_link_t *link);
void AAS_InitBSPLinkHeap(void);
void AAS_FreeBSPLinkHeap(void);
bsp_link_t *AAS_AllocBSPLink(void);
void AAS_FreeBSPLink(bsp_link_t *link);
void AAS_UnlinkEntityFromBSPLeaves(aas_entity_t *entity);
void AAS_InvalidateEntities(void);
void AAS_FrameSynchronise(float time);
void AAS_RunFrameDiagnostics(void);
unsigned short AAS_AreaTravelTime(int areanum, const vec3_t start, const vec3_t end);
int AAS_AreaTravelTimeToGoalArea(int areanum, vec3_t origin, int goalareanum, int travelflags);
int AAS_AreaReachabilityToGoalArea(int areanum, vec3_t origin, int goalareanum, int travelflags);
int AAS_EnableRoutingArea(int areanum, int enable);
void AAS_ReachabilityFromNum(int num, aas_reachability_t *reach);
int AAS_BridgeWalkable(int areanum);
int AAS_RandomGoalArea(int areanum, int travelflags, int *goalareanum, vec3_t goalorigin);
int AAS_AreaVisible(int srcarea, int destarea);
int AAS_PredictRoute(aas_predictroute_t *route,
                     int areanum,
                     vec3_t origin,
                     int goalareanum,
                     int travelflags,
                     int maxareas,
                     int maxtime,
                     int stopevent,
                     int stopcontents,
                     int stoptfl,
                     int stopareanum);
int AAS_AlternativeRouteGoals(vec3_t start,
                              int startareanum,
                              vec3_t goal,
                              int goalareanum,
                              int travelflags,
                              aas_altroutegoal_t *altroutegoals,
                              int maxaltroutegoals,
                              int type);
int AAS_NearestHideArea(int srcnum,
                        vec3_t origin,
                        int areanum,
                        int enemynum,
                        vec3_t enemyorigin,
                        int enemyareanum,
                        int travelflags);
int AAS_NextAreaReachability(int areanum, int reachnum);
void AAS_RouteFrameUpdate(void);
void AAS_RouteFrameResetDiagnostics(void);
int AAS_RouteFrameWorkCounter(void);
int AAS_RouteFrameSkipCounter(void);
int AAS_RouteFrameLastBudget(void);
bool AAS_RouteFrameForceWriteActive(void);
void AAS_ReachabilityFrameUpdate(void);
void AAS_ReachabilityFrameResetDiagnostics(void);
int AAS_ReachabilityFrameWorkCounter(void);
int AAS_ReachabilityFrameSkipCounter(void);
bool AAS_ReachabilityForceReachabilityActive(void);
bool AAS_ReachabilityForceClusteringActive(void);

int AAS_NextModelReachability(int startIndex, int modelnum);
int AAS_ModelNumForEntity(int entnum);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GLADIATOR_BOTLIB_AAS_AAS_LOCAL_H */
