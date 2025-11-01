#ifndef Q2BRIDGE_BOTLIB_H
#define Q2BRIDGE_BOTLIB_H

#include "shared/bot_types.h"
#include "../shared/q_shared.h"

struct bot_goal_s;
typedef struct bot_goal_s bot_goal_t;

struct bot_levelitem_setup_s;
typedef struct bot_levelitem_setup_s bot_levelitem_setup_t;

#ifdef __cplusplus
extern "C" {
#endif

#define BOTLIB_API_VERSION 2

// Debug line colors
#define LINECOLOR_NONE     -1
#define LINECOLOR_RED      0xf2f2f0f0L
#define LINECOLOR_GREEN    0xd0d1d2d3L
#define LINECOLOR_BLUE     0xf3f3f1f1L
#define LINECOLOR_YELLOW   0xdcdddedfL
#define LINECOLOR_ORANGE   0xe0e1e2e3L

// Print types
#define PRT_MESSAGE 1
#define PRT_WARNING 2
#define PRT_ERROR   3
#define PRT_FATAL   4
#define PRT_EXIT    5

// Console message types
#define CMS_NORMAL 0
#define CMS_CHAT   1

// Maximum lengths used by the bot library
#define MAX_NETNAME        16
#define MAX_CLIENTSKINNAME 128
#define MAX_FILEPATH       144
#define MAX_CHARACTERNAME  144

// Action flags
#define ACTION_ATTACK      1
#define ACTION_USE         2
#define ACTION_RESPAWN     4
#define ACTION_JUMP        8
#define ACTION_MOVEUP      8
#define ACTION_CROUCH      16
#define ACTION_MOVEDOWN    16
#define ACTION_MOVEFORWARD 32
#define ACTION_MOVEBACK    64
#define ACTION_MOVELEFT    128
#define ACTION_MOVERIGHT   256
#define ACTION_DELAYEDJUMP 512

// Botlib error codes
#define BLERR_NOERROR                    0
#define BLERR_LIBRARYNOTSETUP            1
#define BLERR_LIBRARYALREADYSETUP        2
#define BLERR_INVALIDCLIENTNUMBER        3
#define BLERR_INVALIDENTITYNUMBER        4
#define BLERR_NOAASFILE                  5
#define BLERR_CANNOTOPENAASFILE          6
#define BLERR_CANNOTSEEKTOAASFILE        7
#define BLERR_CANNOTREADAASHEADER        8
#define BLERR_WRONGAASFILEID             9
#define BLERR_WRONGAASFILEVERSION        10
#define BLERR_CANNOTREADAASLUMP          11
#define BLERR_NOBSPFILE                  12
#define BLERR_CANNOTOPENBSPFILE          13
#define BLERR_CANNOTSEEKTOBSPFILE        14
#define BLERR_CANNOTREADBSPHEADER        15
#define BLERR_WRONGBSPFILEID             16
#define BLERR_WRONGBSPFILEVERSION        17
#define BLERR_CANNOTREADBSPLUMP          18
#define BLERR_AICLIENTNOTSETUP           19
#define BLERR_AICLIENTALREADYSETUP       20
#define BLERR_AIMOVEINACTIVECLIENT       21
#define BLERR_AIMOVETOACTIVECLIENT       22
#define BLERR_AICLIENTALREADYSHUTDOWN    23
#define BLERR_AIUPDATEINACTIVECLIENT     24
#define BLERR_AICMFORINACTIVECLIENT      25
#define BLERR_SETTINGSINACTIVECLIENT     26
#define BLERR_CANNOTLOADICHAT            27
#define BLERR_CANNOTLOADITEMWEIGHTS      28
#define BLERR_CANNOTLOADITEMCONFIG       29
#define BLERR_CANNOTLOADWEAPONWEIGHTS    30
#define BLERR_CANNOTLOADWEAPONCONFIG     31
#define BLERR_INVALIDIMPORT              100
#define BLERR_INVALIDSOUNDINDEX          32

// BSP surface description returned by traces
typedef struct bsp_surface_s {
    char name[16];
    int  flags;
    int  value;
} bsp_surface_t;

// Sweep-trace result structure
typedef struct bsp_trace_s {
    qboolean      allsolid;
    qboolean      startsolid;
    float         fraction;
    vec3_t        endpos;
    cplane_t      plane;
    float         exp_dist;
    int           sidenum;
    bsp_surface_t surface;
    int           contents;
    int           ent;
} bsp_trace_t;

// Bot configuration
typedef struct bot_settings_s {
    char characterfile[MAX_FILEPATH];
    char charactername[MAX_CHARACTERNAME];
    char ailibrary[MAX_FILEPATH];
} bot_settings_t;

// Client presentation settings
typedef struct bot_clientsettings_s {
    char netname[MAX_NETNAME];
    char skin[MAX_CLIENTSKINNAME];
} bot_clientsettings_t;

// Bot input (converted to a usercmd_t)
typedef struct bot_input_s {
    float thinktime;
    vec3_t dir;
    float speed;
    vec3_t viewangles;
    int   weapon;
    int   actionflags;
} bot_input_t;

// Client state update fed into the botlib
typedef struct bot_updateclient_s {
    pmtype_t pm_type;
    vec3_t   origin;
    vec3_t   velocity;
    byte     pm_flags;
    byte     pm_time;
    float    gravity;
    vec3_t   delta_angles;

    vec3_t   viewangles;
    vec3_t   viewoffset;
    vec3_t   kick_angles;
    vec3_t   gunangles;
    vec3_t   gunoffset;
    int      gunindex;
    int      gunframe;

    float    blend[4];
    float    fov;
    int      rdflags;
    short    stats[MAX_STATS];

    int      inventory[MAX_ITEMS];
} bot_updateclient_t;

// Entity snapshot provided to the botlib
typedef struct bot_updateentity_s {
    vec3_t origin;
    vec3_t angles;
    vec3_t old_origin;
    vec3_t mins;
    vec3_t maxs;
    int    solid;
    int    modelindex;
    int    modelindex2;
    int    modelindex3;
    int    modelindex4;
    int    frame;
    int    skinnum;
    int    effects;
    int    renderfx;
    int    sound;
    int    event;
} bot_updateentity_t;

// Bot library exported functions
typedef struct bot_export_s {
    char *(*BotVersion)(void);
    int (*BotSetupLibrary)(void);
    int (*BotShutdownLibrary)(void);
    int (*BotLibraryInitialized)(void);
    int (*BotLibVarSet)(char *var_name, char *value);
    int (*BotDefine)(char *string);
    int (*BotLoadMap)(char *mapname, int modelindexes, char *modelindex[],
                      int soundindexes, char *soundindex[],
                      int imageindexes, char *imageindex[]);
    int (*BotSetupClient)(int client, bot_settings_t *settings);
    int (*BotShutdownClient)(int client);
    int (*BotMoveClient)(int oldclnum, int newclnum);
    int (*BotClientSettings)(int client, bot_clientsettings_t *settings);
    int (*BotSettings)(int client, bot_settings_t *settings);
    int (*BotStartFrame)(float time);
    int (*BotUpdateClient)(int client, bot_updateclient_t *buc);
    int (*BotUpdateEntity)(int ent, bot_updateentity_t *bue);
    int (*BotAddSound)(vec3_t origin, int ent, int channel, int soundindex,
                       float volume, float attenuation, float timeofs);
    int (*BotAddPointLight)(vec3_t origin, int ent, float radius,
                            float r, float g, float b, float time, float decay);
    int (*BotAI)(int client, float thinktime);
    int (*BotConsoleMessage)(int client, int type, char *message);
    int (*Test)(int parm0, char *parm1, vec3_t parm2, vec3_t parm3);
    int (*BotAllocGoalState)(int client);
    void (*BotFreeGoalState)(int handle);
    void (*BotResetGoalState)(int handle);
    int (*BotLoadItemWeights)(int handle, const char *filename);
    void (*BotFreeItemWeights)(int handle);
    int (*BotPushGoal)(int handle, const bot_goal_t *goal);
    int (*BotPopGoal)(int handle);
    int (*BotGetTopGoal)(int handle, bot_goal_t *goal);
    int (*BotGetSecondGoal)(int handle, bot_goal_t *goal);
    int (*BotChooseLTGItem)(int handle, vec3_t origin, int *inventory, int travelflags);
    int (*BotChooseNBGItem)(int handle, vec3_t origin, int *inventory, int travelflags, bot_goal_t *ltg, float maxtime);
    void (*BotResetAvoidGoals)(int handle);
    void (*BotAddAvoidGoal)(int handle, int number, float avoidtime);
    int (*BotUpdateGoalState)(int handle, vec3_t origin, int *inventory, int travelflags, float now, float nearby_time);
    int (*BotRegisterLevelItem)(const bot_levelitem_setup_t *setup);
    void (*BotUnregisterLevelItem)(int number);
    void (*BotMarkLevelItemTaken)(int number, float respawn_delay);
    int (*BotLoadCharacter)(const char *character_file, float skill);
    void (*BotFreeCharacter)(int handle);
    int (*BotLoadCharacterSkill)(const char *character_file, float skill);
    float (*Characteristic_Float)(int handle, int index);
    float (*Characteristic_BFloat)(int handle, int index, float minimum, float maximum);
    int (*Characteristic_Integer)(int handle, int index);
    int (*Characteristic_BInteger)(int handle, int index, int minimum, int maximum);
    void (*Characteristic_String)(int handle, int index, char *buffer, int buffer_size);
} bot_export_t;

// Bot library imported functions
typedef struct bot_import_s {
    void (*BotInput)(int client, bot_input_t *bi);
    void (*BotClientCommand)(int client, char *str, ...);
    void (*Print)(int type, char *fmt, ...);
    bsp_trace_t (*Trace)(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end,
                         int passent, int contentmask);
    int  (*PointContents)(vec3_t point);
    void *(*GetMemory)(int size);
    void (*FreeMemory)(void *ptr);
    int  (*DebugLineCreate)(void);
    void (*DebugLineDelete)(int line);
    void (*DebugLineShow)(int line, vec3_t start, vec3_t end, int color);
} bot_import_t;

bot_export_t *GetBotAPI(bot_import_t *import);

#ifdef __cplusplus
}
#endif

#endif /* Q2BRIDGE_BOTLIB_H */
