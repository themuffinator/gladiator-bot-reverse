#ifndef GLADIATOR_SHARED_Q_SHARED_H
#define GLADIATOR_SHARED_Q_SHARED_H

/*
 * Minimal subset of Quake II's q_shared.h definitions required for the bot interface.
 * Based on dev_tools/game_source/q_shared.h from the original id Software sources.
 *
 * Deviations from the original:
 * - qboolean uses qfalse/qtrue enumerators to avoid clashes with C++ bool keywords.
 * - Platform-specific pragmas, filesystem helpers, and unrelated gameplay constants
 *   are intentionally omitted.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* basic types */
typedef unsigned char byte;               /* q_shared.h: typedef unsigned char byte; */
typedef enum { qfalse = 0, qtrue = 1 } qboolean; /* q_shared.h: typedef enum {false, true} qboolean; */

typedef float vec_t;                      /* q_shared.h: typedef float vec_t; */
typedef vec_t vec3_t[3];                  /* q_shared.h: typedef vec_t vec3_t[3]; */

/* angle indexes */
#define PITCH   0       /* up / down */
#define YAW     1       /* left / right */
#define ROLL    2       /* fall over */

/* per-level limits */
#define MAX_CLIENTS     256     /* absolute limit */
#define MAX_ITEMS       256

/* game print flags */
#define PRINT_LOW       0       /* pickup messages */
#define PRINT_MEDIUM    1       /* death messages */
#define PRINT_HIGH      2       /* critical messages */
#define PRINT_CHAT      3       /* chat messages */
#define PRINT_ALL       0
#define PRINT_DEVELOPER 1       /* only print when "developer 1" */
#define PRINT_ALERT     2

/* MATHLIB helpers */
#define DotProduct(x,y)         ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#define VectorSubtract(a,b,c)   ((c)[0]=(a)[0]-(b)[0], (c)[1]=(a)[1]-(b)[1], (c)[2]=(a)[2]-(b)[2])
#define VectorAdd(a,b,c)        ((c)[0]=(a)[0]+(b)[0], (c)[1]=(a)[1]+(b)[1], (c)[2]=(a)[2]+(b)[2])
#define VectorCopy(a,b)         ((b)[0]=(a)[0], (b)[1]=(a)[1], (b)[2]=(a)[2])
#define VectorClear(a)          ((a)[0]=(a)[1]=(a)[2]=0)
#define VectorNegate(a,b)       ((b)[0]=-(a)[0], (b)[1]=-(a)[1], (b)[2]=-(a)[2])
#define VectorSet(v,x,y,z)      ((v)[0]=(x), (v)[1]=(y), (v)[2]=(z))

/* player movement */
typedef enum
{
    /* can accelerate and turn */
    PM_NORMAL,
    PM_SPECTATOR,
    /* no acceleration or turning */
    PM_DEAD,
    PM_GIB,        /* different bounding box */
    PM_FREEZE
} pmtype_t;

/* pmove->pm_flags */
#define PMF_DUCKED              1
#define PMF_JUMP_HELD           2
#define PMF_ON_GROUND           4
#define PMF_TIME_WATERJUMP      8       /* pm_time is waterjump */
#define PMF_TIME_LAND           16      /* pm_time is time before rejump */
#define PMF_TIME_TELEPORT       32      /* pm_time is non-moving time */
#define PMF_NO_PREDICTION       64      /* temporarily disables prediction (used for grappling hook) */

/* button bits */
#define BUTTON_ATTACK           1
#define BUTTON_USE              2
#define BUTTON_ANY              128     /* any key whatsoever */

/* collision contents */
#define CONTENTS_SOLID          1       /* an eye is never valid in a solid */
#define CONTENTS_WINDOW         2       /* translucent, but not watery */
#define CONTENTS_AUX            4
#define CONTENTS_LAVA           8
#define CONTENTS_SLIME          16
#define CONTENTS_WATER          32
#define CONTENTS_MIST           64
#define CONTENTS_AREAPORTAL     0x8000
#define CONTENTS_PLAYERCLIP     0x10000
#define CONTENTS_MONSTERCLIP    0x20000
#define CONTENTS_CURRENT_0      0x40000
#define CONTENTS_CURRENT_90     0x80000
#define CONTENTS_CURRENT_180    0x100000
#define CONTENTS_CURRENT_270    0x200000
#define CONTENTS_CURRENT_UP     0x400000
#define CONTENTS_CURRENT_DOWN   0x800000
#define CONTENTS_ORIGIN         0x1000000        /* removed before bsping an entity */
#define CONTENTS_MONSTER        0x2000000        /* should never be on a brush, only in game */
#define CONTENTS_DEADMONSTER    0x4000000
#define CONTENTS_DETAIL         0x8000000        /* brushes to be added after vis leafs */
#define CONTENTS_TRANSLUCENT    0x10000000       /* auto set if any surface has trans */
#define CONTENTS_LADDER         0x20000000

/* content masks */
#define MASK_ALL                (-1)
#define MASK_SOLID              (CONTENTS_SOLID|CONTENTS_WINDOW)
#define MASK_PLAYERSOLID        (CONTENTS_SOLID|CONTENTS_PLAYERCLIP|CONTENTS_WINDOW|CONTENTS_MONSTER)
#define MASK_DEADSOLID          (CONTENTS_SOLID|CONTENTS_PLAYERCLIP|CONTENTS_WINDOW)
#define MASK_MONSTERSOLID       (CONTENTS_SOLID|CONTENTS_MONSTERCLIP|CONTENTS_WINDOW|CONTENTS_MONSTER)
#define MASK_WATER              (CONTENTS_WATER|CONTENTS_LAVA|CONTENTS_SLIME)
#define MASK_OPAQUE             (CONTENTS_SOLID|CONTENTS_SLIME|CONTENTS_LAVA)
#define MASK_SHOT               (CONTENTS_SOLID|CONTENTS_MONSTER|CONTENTS_WINDOW|CONTENTS_DEADMONSTER)
#define MASK_CURRENT            (CONTENTS_CURRENT_0|CONTENTS_CURRENT_90|CONTENTS_CURRENT_180|CONTENTS_CURRENT_270|CONTENTS_CURRENT_UP|CONTENTS_CURRENT_DOWN)

/* plane structure used by bsp tracing */
typedef struct cplane_s
{
    vec3_t  normal;
    float   dist;
    byte    type;           /* for fast side tests */
    byte    signbits;       /* signx + (signy<<1) + (signz<<1) */
    byte    pad[2];
} cplane_t;

/* entity_state_t->effects */
#define EF_ROTATE               0x00000001      /* rotate (bonus items) */
#define EF_GIB                  0x00000002      /* leave a trail */
#define EF_BLASTER              0x00000008      /* redlight + trail */
#define EF_ROCKET               0x00000010      /* redlight + trail */
#define EF_GRENADE              0x00000020
#define EF_HYPERBLASTER         0x00000040
#define EF_BFG                  0x00000080
#define EF_COLOR_SHELL          0x00000100
#define EF_POWERSCREEN          0x00000200
#define EF_ANIM01               0x00000400      /* automatically cycle between frames 0 and 1 at 2 hz */
#define EF_ANIM23               0x00000800      /* automatically cycle between frames 2 and 3 at 2 hz */
#define EF_ANIM_ALL             0x00001000      /* automatically cycle through all frames at 2hz */
#define EF_ANIM_ALLFAST         0x00002000      /* automatically cycle through all frames at 10hz */
#define EF_FLIES                0x00004000
#define EF_QUAD                 0x00008000
#define EF_PENT                 0x00010000
#define EF_TELEPORTER           0x00020000      /* particle fountain */
#define EF_FLAG1                0x00040000
#define EF_FLAG2                0x00080000
#define EF_IONRIPPER            0x00100000
#define EF_GREENGIB             0x00200000
#define EF_BLUEHYPERBLASTER     0x00400000
#define EF_SPINNINGLIGHTS       0x00800000
#define EF_PLASMA               0x01000000
#define EF_TRAP                 0x02000000
#define EF_TRACKER              0x04000000
#define EF_DOUBLE               0x08000000
#define EF_SPHERETRANS          0x10000000
#define EF_TAGTRAIL             0x20000000
#define EF_HALF_DAMAGE          0x40000000
#define EF_TRACKERTRAIL         0x80000000

/* entity_state_t->renderfx flags */
#define RF_MINLIGHT             1       /* always have some light (viewmodel) */
#define RF_VIEWERMODEL          2       /* don't draw through eyes, only mirrors */
#define RF_WEAPONMODEL          4       /* only draw through eyes */
#define RF_FULLBRIGHT           8       /* always draw full intensity */
#define RF_DEPTHHACK            16      /* for view weapon Z crunching */
#define RF_TRANSLUCENT          32
#define RF_FRAMELERP            64
#define RF_BEAM                 128
#define RF_CUSTOMSKIN           256     /* skin is an index in image_precache */
#define RF_GLOW                 512     /* pulse lighting for bonus items */
#define RF_SHELL_RED            1024
#define RF_SHELL_GREEN          2048
#define RF_SHELL_BLUE           4096
#define RF_IR_VISIBLE           0x00008000      /* 32768 */
#define RF_SHELL_DOUBLE         0x00010000      /* 65536 */
#define RF_SHELL_HALF_DAM       0x00020000
#define RF_USE_DISGUISE         0x00040000

/* player_state_t->refdef flags */
#define RDF_UNDERWATER          1       /* warp the screen as appropriate */
#define RDF_NOWORLDMODEL        2       /* used for player configuration screen */
#define RDF_IRGOGGLES           4
#define RDF_UVGOGGLES           8

/* sound channels */
#define CHAN_AUTO               0
#define CHAN_WEAPON             1
#define CHAN_VOICE              2
#define CHAN_ITEM               3
#define CHAN_BODY               4
#define CHAN_NO_PHS_ADD         8       /* send to all clients, not just ones in PHS */
#define CHAN_RELIABLE           16      /* send by reliable message, not datagram */

/* sound attenuation values */
#define ATTN_NONE               0       /* full volume the entire level */
#define ATTN_NORM               1
#define ATTN_IDLE               2
#define ATTN_STATIC             3       /* diminish very rapidly with distance */

/* player_state->stats[] indexes */
#define STAT_HEALTH_ICON        0
#define STAT_HEALTH             1
#define STAT_AMMO_ICON          2
#define STAT_AMMO               3
#define STAT_ARMOR_ICON         4
#define STAT_ARMOR              5
#define STAT_SELECTED_ICON      6
#define STAT_PICKUP_ICON        7
#define STAT_PICKUP_STRING      8
#define STAT_TIMER_ICON         9
#define STAT_TIMER              10
#define STAT_HELPICON           11
#define STAT_SELECTED_ITEM      12
#define STAT_LAYOUTS            13
#define STAT_FRAGS              14
#define STAT_FLASHES            15      /* cleared each frame, 1 = health, 2 = armor */
#define STAT_CHASE              16
#define STAT_SPECTATOR          17

#define MAX_STATS               32

#ifdef __cplusplus
}
#endif

#endif /* GLADIATOR_SHARED_Q_SHARED_H */
