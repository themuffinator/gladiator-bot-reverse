#ifndef Q2BRIDGE_BRIDGE_H
#define Q2BRIDGE_BRIDGE_H

#include <stdarg.h>
#include <stdbool.h>

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

void Q2_BotInput(int client, bot_input_t *input);
void Q2_BotClientCommand(int client, const char *command, ...);
void Q2_Print(int type, const char *fmt, ...);
void Q2_Error(const char *fmt, ...);
cvar_t *Q2_CvarGet(const char *name, const char *default_value, int flags);
bsp_trace_t Q2_Trace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int passent, int contentmask);
int Q2_PointContents(vec3_t point);
void *Q2_GetMemory(int size);
void Q2_FreeMemory(void *ptr);
int Q2_DebugLineCreate(void);
void Q2_DebugLineDelete(int line);
void Q2_DebugLineShow(int line, vec3_t start, vec3_t end, int color);
void Q2Bridge_SetDebugLinesEnabled(bool enabled);
bool Q2Bridge_DebugLinesEnabled(void);

#ifdef __cplusplus
}
#endif

#endif /* Q2BRIDGE_BRIDGE_H */
