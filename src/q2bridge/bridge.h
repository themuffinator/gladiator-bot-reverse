#ifndef Q2BRIDGE_BRIDGE_H
#define Q2BRIDGE_BRIDGE_H

#include <stdarg.h>
#include <stdio.h>

#include "q2bridge/botlib.h"
#include "shared/q_shared.h"

typedef struct cvar_s cvar_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Stores the bot import table for subsequent bridge calls.
 */
void Q2Bridge_SetImportTable(bot_import_t *imports);

/**
 * @brief Clears the cached bot import table.
 */
void Q2Bridge_ClearImportTable(void);

/**
 * @brief Fetches the cached bot import table.
 */
bot_import_t *Q2Bridge_GetImportTable(void);

static inline bot_import_t *Q2Bridge_GetImportsChecked(void)
{
    return Q2Bridge_GetImportTable();
}

static inline void Q2_BotInput(int client, bot_input_t *input)
{
    bot_import_t *imports = Q2Bridge_GetImportsChecked();
    if (imports == NULL || imports->BotInput == NULL)
    {
        return;
    }

    // TODO: Validate the bot input structure before forwarding to the engine.
    imports->BotInput(client, input);
}

static inline void Q2_BotClientCommand(int client, const char *command, ...)
{
    bot_import_t *imports = Q2Bridge_GetImportsChecked();
    if (imports == NULL || imports->BotClientCommand == NULL || command == NULL)
    {
        return;
    }

    // TODO: Replace the temporary formatting buffer with a streaming solution.
    va_list args;
    va_start(args, command);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), command, args);

    va_end(args);

    imports->BotClientCommand(client, buffer);
}

static inline void Q2_Print(int type, const char *fmt, ...)
{
    bot_import_t *imports = Q2Bridge_GetImportsChecked();
    if (imports == NULL || imports->Print == NULL || fmt == NULL)
    {
        return;
    }

    // TODO: Replace the temporary formatting buffer with validation logic.
    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    imports->Print(type, buffer);
}

static inline void Q2_Error(const char *fmt, ...)
{
    bot_import_t *imports = Q2Bridge_GetImportsChecked();
    if (imports == NULL || imports->Print == NULL || fmt == NULL)
    {
        return;
    }

    // TODO: Route to a dedicated error callback once exposed by the engine.
    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    imports->Print(PRT_ERROR, buffer);
}

static inline cvar_t *Q2_CvarGet(const char *name, const char *default_value, int flags)
{
    (void)name;
    (void)default_value;
    (void)flags;

    // TODO: Wire through to the real cvar system once it is exposed to the bridge.
    return NULL;
}

static inline bsp_trace_t Q2_Trace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int passent, int contentmask)
{
    bot_import_t *imports = Q2Bridge_GetImportsChecked();
    if (imports != NULL && imports->Trace != NULL)
    {
        // TODO: Validate trace parameters before dispatching.
        return imports->Trace(start, mins, maxs, end, passent, contentmask);
    }

    const bsp_trace_t empty = {0};
    return empty;
}

static inline int Q2_PointContents(vec3_t point)
{
    bot_import_t *imports = Q2Bridge_GetImportsChecked();
    if (imports != NULL && imports->PointContents != NULL)
    {
        // TODO: Validate the point vector before dispatching.
        return imports->PointContents(point);
    }

    return 0;
}

static inline void *Q2_GetMemory(int size)
{
    bot_import_t *imports = Q2Bridge_GetImportsChecked();
    if (imports == NULL || imports->GetMemory == NULL)
    {
        return NULL;
    }

    // TODO: Add range checking for allocation sizes.
    return imports->GetMemory(size);
}

static inline void Q2_FreeMemory(void *ptr)
{
    bot_import_t *imports = Q2Bridge_GetImportsChecked();
    if (imports == NULL || imports->FreeMemory == NULL)
    {
        return;
    }

    // TODO: Consider tracking ownership before freeing memory.
    imports->FreeMemory(ptr);
}

static inline int Q2_DebugLineCreate(void)
{
    bot_import_t *imports = Q2Bridge_GetImportsChecked();
    if (imports == NULL || imports->DebugLineCreate == NULL)
    {
        return -1;
    }

    // TODO: Add diagnostics around debug line allocation failures.
    return imports->DebugLineCreate();
}

static inline void Q2_DebugLineDelete(int line)
{
    bot_import_t *imports = Q2Bridge_GetImportsChecked();
    if (imports == NULL || imports->DebugLineDelete == NULL)
    {
        return;
    }

    // TODO: Validate debug line identifiers before issuing deletes.
    imports->DebugLineDelete(line);
}

static inline void Q2_DebugLineShow(int line, vec3_t start, vec3_t end, int color)
{
    bot_import_t *imports = Q2Bridge_GetImportsChecked();
    if (imports == NULL || imports->DebugLineShow == NULL)
    {
        return;
    }

    // TODO: Validate vector inputs and color codes before dispatching.
    imports->DebugLineShow(line, start, end, color);
}

#ifdef __cplusplus
}
#endif

#endif /* Q2BRIDGE_BRIDGE_H */
