#include "botlib_interface.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../common/l_libvar.h"
#include "../common/l_log.h"
#include "../common/l_memory.h"
#include "q2bridge/bridge_config.h"

/**
 * Internal bookkeeping for the subsystem lifecycle so shutdown can unwind the
 * setup sequence accurately. Each subsystem will gain real implementations as
 * the reverse engineering effort progresses.
 */
typedef struct botlib_subsystem_state_s {
    bool aas_initialised;
    bool ea_initialised;
    bool ai_initialised;
    bool utilities_initialised;
} botlib_subsystem_state_t;

static const botlib_import_table_t *g_import_table = NULL;
static bool g_library_initialised = false;
static botlib_library_variables_t g_library_variables;
static botlib_subsystem_state_t g_subsystem_state;

static void Botlib_ResetSubsystemState(void)
{
    memset(&g_subsystem_state, 0, sizeof(g_subsystem_state));
}

static void Botlib_ResetLibraryVariables(void)
{
    memset(&g_library_variables, 0, sizeof(g_library_variables));
}

static int Botlib_ReadIntLibVarCached(libvar_t *var, int fallback)
{
    return (var != NULL) ? (int)var->value : fallback;
}

static float Botlib_ReadFloatLibVarCached(libvar_t *var, float fallback)
{
    return (var != NULL) ? var->value : fallback;
}

static void Botlib_CacheLibraryVariables(void)
{
    g_library_variables.maxclients = Botlib_ReadIntLibVarCached(Bridge_MaxClients(), 4);
    g_library_variables.maxentities = Botlib_ReadIntLibVarCached(Bridge_MaxEntities(), 1024);
    g_library_variables.sv_gravity = Botlib_ReadFloatLibVarCached(Bridge_Gravity(), 800.0f);
    g_library_variables.sv_maxvelocity = Botlib_ReadFloatLibVarCached(Bridge_MaxVelocity(), 300.0f);
    g_library_variables.sv_airaccelerate = Botlib_ReadFloatLibVarCached(Bridge_AirAccelerate(), 0.0f);
    g_library_variables.sv_maxwalkvelocity = Botlib_ReadFloatLibVarCached(Bridge_MaxWalkVelocity(), 300.0f);
    g_library_variables.sv_maxcrouchvelocity = Botlib_ReadFloatLibVarCached(Bridge_MaxCrouchVelocity(), 100.0f);
    g_library_variables.sv_maxswimvelocity = Botlib_ReadFloatLibVarCached(Bridge_MaxSwimVelocity(), 150.0f);
    g_library_variables.sv_jumpvel = Botlib_ReadFloatLibVarCached(Bridge_JumpVelocity(), 224.0f);
    g_library_variables.sv_maxacceleration = Botlib_ReadFloatLibVarCached(Bridge_MaxAcceleration(), 2200.0f);
    g_library_variables.sv_friction = Botlib_ReadFloatLibVarCached(Bridge_Friction(), 6.0f);
    g_library_variables.sv_stopspeed = Botlib_ReadFloatLibVarCached(Bridge_StopSpeed(), 100.0f);
    g_library_variables.sv_maxstep = Botlib_ReadFloatLibVarCached(Bridge_MaxStep(), 18.0f);
    g_library_variables.sv_maxbarrier = Botlib_ReadFloatLibVarCached(Bridge_MaxBarrier(), 50.0f);
    g_library_variables.sv_maxsteepness = Botlib_ReadFloatLibVarCached(Bridge_MaxSteepness(), 0.7f);
    g_library_variables.sv_maxwaterjump = Botlib_ReadFloatLibVarCached(Bridge_MaxWaterJump(), 20.0f);
    g_library_variables.sv_watergravity = Botlib_ReadFloatLibVarCached(Bridge_WaterGravity(), 400.0f);
    g_library_variables.sv_waterfriction = Botlib_ReadFloatLibVarCached(Bridge_WaterFriction(), 1.0f);
}

static void Botlib_SetupAASSubsystem(void)
{
    if (g_subsystem_state.aas_initialised) {
        return;
    }

    // TODO: Implement actual AAS subsystem initialisation.
    g_subsystem_state.aas_initialised = true;
}

static void Botlib_SetupEASubsystem(void)
{
    if (g_subsystem_state.ea_initialised) {
        return;
    }

    // TODO: Implement actual EA subsystem initialisation.
    g_subsystem_state.ea_initialised = true;
}

static void Botlib_SetupAISubsystem(void)
{
    if (g_subsystem_state.ai_initialised) {
        return;
    }

    // TODO: Implement actual AI subsystem initialisation.
    g_subsystem_state.ai_initialised = true;
}

static void Botlib_SetupUtilities(void)
{
    if (g_subsystem_state.utilities_initialised) {
        return;
    }

    // TODO: Implement actual utility subsystem initialisation.
    g_subsystem_state.utilities_initialised = true;
}

static void Botlib_ShutdownUtilities(void)
{
    if (!g_subsystem_state.utilities_initialised) {
        return;
    }

    // TODO: Implement actual utility subsystem shutdown.
    g_subsystem_state.utilities_initialised = false;
}

static void Botlib_ShutdownAISubsystem(void)
{
    if (!g_subsystem_state.ai_initialised) {
        return;
    }

    // TODO: Implement actual AI subsystem shutdown.
    g_subsystem_state.ai_initialised = false;
}

static void Botlib_ShutdownEASubsystem(void)
{
    if (!g_subsystem_state.ea_initialised) {
        return;
    }

    // TODO: Implement actual EA subsystem shutdown.
    g_subsystem_state.ea_initialised = false;
}

static void Botlib_ShutdownAASSubsystem(void)
{
    if (!g_subsystem_state.aas_initialised) {
        return;
    }

    // TODO: Implement actual AAS subsystem shutdown.
    g_subsystem_state.aas_initialised = false;
}

void BotInterface_SetImportTable(const botlib_import_table_t *import_table)
{
    g_import_table = import_table;
}

const botlib_import_table_t *BotInterface_GetImportTable(void)
{
    return g_import_table;
}

int BotSetupLibrary(void)
{
    if (g_library_initialised) {
        return BLERR_LIBRARYALREADYSETUP;
    }

    if (g_import_table == NULL || g_import_table->BotLibVarGet == NULL) {
        return BLERR_INVALIDIMPORT;
    }

    if (!BotMemory_Init(BOT_MEMORY_DEFAULT_HEAP_SIZE)) {
        return BLERR_INVALIDIMPORT;
    }

    Botlib_ResetSubsystemState();
    Botlib_ResetLibraryVariables();
    LibVar_ResetCache();
    BotLib_LogShutdown();

    BridgeConfig_Init();
    Botlib_CacheLibraryVariables();

    Botlib_SetupAASSubsystem();
    Botlib_SetupEASubsystem();

    /*
     * The Gladiator HLIL initialises shared data before exposing the bot state
     * table: item configuration is loaded via sub_100309d0 ("itemconfig") and
     * the weapon library comes online through sub_10035680 ("weaponconfig")
     * prior to character setup routines that request per-bot weight handles and
     * chat files. When the AI subsystem grows beyond stubs, its setup function
     * will need to honour that order so movement, weight, and character modules
     * can acquire their dependencies deterministically.
     *【F:dev_tools/gladiator.dll.bndb_hlil.txt†L38344-L38405】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L41398-L41415】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32483-L32552】
     */
    Botlib_SetupAISubsystem();
    Botlib_SetupUtilities();

    g_library_initialised = true;
    return BLERR_NOERROR;
}

int BotShutdownLibrary(void)
{
    if (!g_library_initialised) {
        return BLERR_LIBRARYNOTSETUP;
    }

    Botlib_ShutdownUtilities();
    Botlib_ShutdownAISubsystem();
    Botlib_ShutdownEASubsystem();
    Botlib_ShutdownAASSubsystem();

    Botlib_ResetLibraryVariables();
    Botlib_ResetSubsystemState();
    BridgeConfig_Shutdown();
    LibVar_Shutdown();

    g_library_initialised = false;

    BotMemory_Shutdown();
    return BLERR_NOERROR;
}

bool BotLibraryInitialized(void)
{
    return g_library_initialised;
}

const botlib_library_variables_t *BotInterface_GetLibraryVariables(void)
{
    return &g_library_variables;
}
