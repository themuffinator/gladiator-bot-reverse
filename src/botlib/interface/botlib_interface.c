#include "botlib_interface.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../common/l_libvar.h"
#include "../common/l_log.h"

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

static bool Botlib_ReadIntLibVar(const char *name, int *dest)
{
    if (dest == NULL) {
        return false;
    }

    libvar_t *var = LibVarGet(name);
    if (var == NULL) {
        return false;
    }

    *dest = (int)var->value;
    return true;
}

static bool Botlib_ReadFloatLibVar(const char *name, float *dest)
{
    if (dest == NULL) {
        return false;
    }

    libvar_t *var = LibVarGet(name);
    if (var == NULL) {
        return false;
    }

    *dest = var->value;
    return true;
}

static void Botlib_CacheLibraryVariables(void)
{
    (void)Botlib_ReadIntLibVar("maxclients", &g_library_variables.maxclients);
    (void)Botlib_ReadIntLibVar("maxentities", &g_library_variables.maxentities);
    (void)Botlib_ReadFloatLibVar("sv_gravity", &g_library_variables.sv_gravity);
    (void)Botlib_ReadFloatLibVar("sv_maxvelocity", &g_library_variables.sv_maxvelocity);
    (void)Botlib_ReadFloatLibVar("sv_airaccelerate", &g_library_variables.sv_airaccelerate);
    (void)Botlib_ReadFloatLibVar("sv_maxwalkvelocity", &g_library_variables.sv_maxwalkvelocity);
    (void)Botlib_ReadFloatLibVar("sv_maxcrouchvelocity", &g_library_variables.sv_maxcrouchvelocity);
    (void)Botlib_ReadFloatLibVar("sv_maxswimvelocity", &g_library_variables.sv_maxswimvelocity);
    (void)Botlib_ReadFloatLibVar("sv_jumpvel", &g_library_variables.sv_jumpvel);
    (void)Botlib_ReadFloatLibVar("sv_maxacceleration", &g_library_variables.sv_maxacceleration);
    (void)Botlib_ReadFloatLibVar("sv_friction", &g_library_variables.sv_friction);
    (void)Botlib_ReadFloatLibVar("sv_stopspeed", &g_library_variables.sv_stopspeed);
    (void)Botlib_ReadFloatLibVar("sv_maxstep", &g_library_variables.sv_maxstep);
    (void)Botlib_ReadFloatLibVar("sv_maxbarrier", &g_library_variables.sv_maxbarrier);
    (void)Botlib_ReadFloatLibVar("sv_maxsteepness", &g_library_variables.sv_maxsteepness);
    (void)Botlib_ReadFloatLibVar("sv_maxwaterjump", &g_library_variables.sv_maxwaterjump);
    (void)Botlib_ReadFloatLibVar("sv_watergravity", &g_library_variables.sv_watergravity);
    (void)Botlib_ReadFloatLibVar("sv_waterfriction", &g_library_variables.sv_waterfriction);
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

    Botlib_ResetSubsystemState();
    Botlib_ResetLibraryVariables();
    LibVar_ResetCache();
    BotLib_LogShutdown();

    BotLib_Print(PRT_MESSAGE, "------- BotLib Initialization -------\n");

    Botlib_CacheLibraryVariables();

    Botlib_SetupAASSubsystem();
    Botlib_SetupEASubsystem();
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
    LibVar_Shutdown();
    BotLib_LogShutdown();

    BotLib_Print(PRT_MESSAGE, "------- BotLib Shutdown -------\n");

    g_library_initialised = false;
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
