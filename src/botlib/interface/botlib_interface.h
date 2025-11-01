#ifndef GLADIATOR_BOTLIB_INTERFACE_H
#define GLADIATOR_BOTLIB_INTERFACE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error codes returned by the exported botlib entry points.
 * These values mirror the enumerations used by the original Gladiator DLL.
 */
#define BLERR_NOERROR 0
#define BLERR_LIBRARYNOTSETUP 1
#define BLERR_LIBRARYALREADYSETUP 2
#define BLERR_CANNOTLOADWEAPONCONFIG 31
#define BLERR_INVALIDIMPORT 100

#define BOTLIB_MAX_WEAPONCONFIG_PATH 260
#define BOTLIB_MAX_SOUNDCONFIG_PATH 260

/**
 * Import table shared by the Quake II game and the Gladiator bot library.
 * Only the pieces that are currently required by the reverse engineered
 * implementation are modelled here – more will be added as they are
 * understood.
 */
typedef struct botlib_import_table_s {
    void (*Print)(int type, const char *fmt, ...);
    void (*DPrint)(const char *fmt, ...);
    int (*BotLibVarGet)(const char *var_name, char *value, size_t size);
    int (*BotLibVarSet)(const char *var_name, const char *value);
} botlib_import_table_t;

/**
 * Stores the runtime library variables that are queried from the engine.
 * Additional values can be appended as more of the library is recovered.
 */
typedef struct botlib_library_variables_s {
    int maxclients;
    int maxentities;
    float sv_gravity;
    float sv_maxvelocity;
    float sv_airaccelerate;
    float sv_maxwalkvelocity;
    float sv_maxcrouchvelocity;
    float sv_maxswimvelocity;
    float sv_jumpvel;
    float sv_maxacceleration;
    float sv_friction;
    float sv_stopspeed;
    float sv_maxstep;
    float sv_maxbarrier;
    float sv_maxsteepness;
    float sv_maxwaterjump;
    float sv_watergravity;
    float sv_waterfriction;
    int max_weaponinfo;
    int max_projectileinfo;
    char weaponconfig[BOTLIB_MAX_WEAPONCONFIG_PATH];
    int max_soundinfo;
    int max_aassounds;
    char soundconfig[BOTLIB_MAX_SOUNDCONFIG_PATH];
} botlib_library_variables_t;

/**
 * Provides the import table that was supplied by the game when the DLL was
 * loaded.
 */
void BotInterface_SetImportTable(const botlib_import_table_t *import_table);
const botlib_import_table_t *BotInterface_GetImportTable(void);

/**
 * Lifecycle management entry points mirrored from the original Gladiator bot
 * DLL.
 */
int BotSetupLibrary(void);
int BotShutdownLibrary(void);
bool BotLibraryInitialized(void);

/**
 * Accessor for the cached library variable state gathered during setup.
 */
const botlib_library_variables_t *BotInterface_GetLibraryVariables(void);

#ifdef __cplusplus
}
#endif

#endif /* GLADIATOR_BOTLIB_INTERFACE_H */
