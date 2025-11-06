#include "q2bridge/bridge.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q2bridge/bridge_config.h"

static bot_import_t *g_q2_imports = NULL;
static bool g_q2_debug_lines_enabled = false;

static bot_import_t *Q2Bridge_GetImportsInternal(void)
{
    return g_q2_imports;
}

static int Q2Bridge_ClampClientCount(int value)
{
    if (value < 0)
    {
        return 0;
    }
    if (value > MAX_CLIENTS)
    {
        return MAX_CLIENTS;
    }
    return value;
}

static int Q2Bridge_MaxClients(void)
{
    libvar_t *maxclients = Bridge_MaxClients();
    if (maxclients == NULL)
    {
        return MAX_CLIENTS;
    }
    return Q2Bridge_ClampClientCount((int)maxclients->value);
}

static bool Q2Bridge_ValidateClientNumber(const char *context, int client)
{
    if (context == NULL)
    {
        context = "unknown";
    }

    int max_clients = Q2Bridge_MaxClients();
    if (client < 0 || client >= max_clients)
    {
        int upper_bound = (max_clients > 0) ? (max_clients - 1) : -1;
        Q2_Print(PRT_ERROR,
                 "%s: invalid client number %d, [0, %d]\n",
                 context,
                 client,
                 upper_bound);
        return false;
    }

    return true;
}

static char *Q2Bridge_FormatString(const char *fmt, va_list args)
{
    if (fmt == NULL)
    {
        return NULL;
    }

    va_list args_copy;
    va_copy(args_copy, args);
    int required = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    if (required < 0)
    {
        return NULL;
    }

    size_t length = (size_t)required + 1U;
    char *buffer = (char *)malloc(length);
    if (buffer == NULL)
    {
        return NULL;
    }

    int written = vsnprintf(buffer, length, fmt, args);
    if (written < 0)
    {
        free(buffer);
        return NULL;
    }

    return buffer;
}

static void Q2Bridge_OutputFallback(int type, const char *message)
{
    (void)type;

    if (message == NULL)
    {
        return;
    }

    fputs(message, stderr);
}

static void Q2Bridge_PrintFormatted(int type, const char *message)
{
    if (message == NULL)
    {
        return;
    }

    bot_import_t *imports = Q2Bridge_GetImportsInternal();
    if (imports != NULL && imports->Print != NULL)
    {
        imports->Print(type, "%s", message);
        return;
    }

    Q2Bridge_OutputFallback(type, message);
}

static bsp_trace_t Q2Bridge_DefaultTrace(void)
{
    bsp_trace_t trace;
    memset(&trace, 0, sizeof(trace));
    trace.fraction = 1.0f;
    return trace;
}

void Q2Bridge_SetImportTable(bot_import_t *imports)
{
    g_q2_imports = imports;
}

void Q2Bridge_ClearImportTable(void)
{
    g_q2_imports = NULL;
}

bot_import_t *Q2Bridge_GetImportTable(void)
{
    return g_q2_imports;
}

void Q2Bridge_SetDebugLinesEnabled(bool enabled)
{
    g_q2_debug_lines_enabled = enabled;
}

bool Q2Bridge_DebugLinesEnabled(void)
{
    return g_q2_debug_lines_enabled;
}

void Q2_BotInput(int client, bot_input_t *input)
{
    bot_import_t *imports = Q2Bridge_GetImportsInternal();
    if (imports == NULL || imports->BotInput == NULL)
    {
        return;
    }

    if (input == NULL)
    {
        Q2_Print(PRT_ERROR, "BotInput: missing input payload for client %d\n", client);
        return;
    }

    if (!Q2Bridge_ValidateClientNumber("BotInput", client))
    {
        return;
    }

    imports->BotInput(client, input);
}

void Q2_BotClientCommand(int client, const char *command, ...)
{
    bot_import_t *imports = Q2Bridge_GetImportsInternal();
    if (imports == NULL || imports->BotClientCommand == NULL || command == NULL)
    {
        return;
    }

    if (!Q2Bridge_ValidateClientNumber("BotClientCommand", client))
    {
        return;
    }

    va_list args;
    va_start(args, command);
    errno = 0;
    char *buffer = Q2Bridge_FormatString(command, args);
    va_end(args);

    if (buffer == NULL)
    {
        Q2_Print(PRT_ERROR,
                 "BotClientCommand: failed to format command for client %d (%s)\n",
                 client,
                 (errno != 0) ? strerror(errno) : "unknown error");
        return;
    }

    imports->BotClientCommand(client, buffer);
    free(buffer);
}

void Q2_Print(int type, const char *fmt, ...)
{
    if (fmt == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    char *message = Q2Bridge_FormatString(fmt, args);
    va_end(args);

    if (message == NULL)
    {
        return;
    }

    Q2Bridge_PrintFormatted(type, message);
    free(message);
}

void Q2_Error(const char *fmt, ...)
{
    if (fmt == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    char *message = Q2Bridge_FormatString(fmt, args);
    va_end(args);

    if (message == NULL)
    {
        return;
    }

    bot_import_t *imports = Q2Bridge_GetImportsInternal();
    if (imports != NULL && imports->Error != NULL)
    {
        imports->Error("%s", message);
    }
    else
    {
        Q2Bridge_PrintFormatted(PRT_ERROR, message);
    }

    free(message);
}

cvar_t *Q2_CvarGet(const char *name, const char *default_value, int flags)
{
    bot_import_t *imports = Q2Bridge_GetImportsInternal();
    if (imports == NULL || imports->CvarGet == NULL)
    {
        return NULL;
    }

    if (name == NULL || *name == '\0')
    {
        Q2_Print(PRT_ERROR, "Q2_CvarGet: invalid cvar name\n");
        return NULL;
    }

    return imports->CvarGet(name, default_value, flags);
}

bsp_trace_t Q2_Trace(vec3_t start,
                     vec3_t mins,
                     vec3_t maxs,
                     vec3_t end,
                     int passent,
                     int contentmask)
{
    bot_import_t *imports = Q2Bridge_GetImportsInternal();
    if (imports == NULL || imports->Trace == NULL)
    {
        Q2_Print(PRT_WARNING, "[q2bridge] Trace: import not available\n");
        return Q2Bridge_DefaultTrace();
    }

    if (start == NULL || end == NULL)
    {
        Q2_Print(PRT_ERROR, "[q2bridge] Trace: invalid endpoints\n");
        return Q2Bridge_DefaultTrace();
    }

    vec3_t zero_mins = {0.0f, 0.0f, 0.0f};
    vec3_t zero_maxs = {0.0f, 0.0f, 0.0f};

    return imports->Trace(start,
                          (mins != NULL) ? mins : zero_mins,
                          (maxs != NULL) ? maxs : zero_maxs,
                          end,
                          passent,
                          contentmask);
}

int Q2_PointContents(vec3_t point)
{
    bot_import_t *imports = Q2Bridge_GetImportsInternal();
    if (imports == NULL || imports->PointContents == NULL)
    {
        Q2_Print(PRT_WARNING, "[q2bridge] PointContents: import not available\n");
        return 0;
    }

    if (point == NULL)
    {
        Q2_Print(PRT_ERROR, "[q2bridge] PointContents: invalid point\n");
        return 0;
    }

    return imports->PointContents(point);
}

void *Q2_GetMemory(int size)
{
    if (size <= 0)
    {
        Q2_Print(PRT_ERROR, "[q2bridge] GetMemory: invalid size %d\n", size);
        return NULL;
    }

    bot_import_t *imports = Q2Bridge_GetImportsInternal();
    if (imports == NULL || imports->GetMemory == NULL)
    {
        Q2_Print(PRT_ERROR, "[q2bridge] GetMemory: allocator not available for %d bytes\n", size);
        return NULL;
    }

    return imports->GetMemory(size);
}

void Q2_FreeMemory(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    bot_import_t *imports = Q2Bridge_GetImportsInternal();
    if (imports == NULL || imports->FreeMemory == NULL)
    {
        Q2_Print(PRT_WARNING, "[q2bridge] FreeMemory: allocator not available\n");
        return;
    }

    imports->FreeMemory(ptr);
}

int Q2_DebugLineCreate(void)
{
    if (!g_q2_debug_lines_enabled)
    {
        return -1;
    }

    bot_import_t *imports = Q2Bridge_GetImportsInternal();
    if (imports == NULL || imports->DebugLineCreate == NULL)
    {
        Q2_Print(PRT_WARNING, "[q2bridge] DebugLineCreate: import not available\n");
        return -1;
    }

    int identifier = imports->DebugLineCreate();
    if (identifier < 0)
    {
        Q2_Print(PRT_WARNING, "[q2bridge] DebugLineCreate: engine returned %d\n", identifier);
    }
    return identifier;
}

void Q2_DebugLineDelete(int line)
{
    if (!g_q2_debug_lines_enabled)
    {
        return;
    }

    if (line < 0)
    {
        Q2_Print(PRT_WARNING, "[q2bridge] DebugLineDelete: invalid line %d\n", line);
        return;
    }

    bot_import_t *imports = Q2Bridge_GetImportsInternal();
    if (imports == NULL || imports->DebugLineDelete == NULL)
    {
        Q2_Print(PRT_WARNING, "[q2bridge] DebugLineDelete: import not available\n");
        return;
    }

    imports->DebugLineDelete(line);
}

void Q2_DebugLineShow(int line, vec3_t start, vec3_t end, int color)
{
    if (!g_q2_debug_lines_enabled)
    {
        return;
    }

    if (line < 0)
    {
        Q2_Print(PRT_WARNING, "[q2bridge] DebugLineShow: invalid line %d\n", line);
        return;
    }

    if (start == NULL || end == NULL)
    {
        Q2_Print(PRT_ERROR, "[q2bridge] DebugLineShow: invalid endpoints\n");
        return;
    }

    bot_import_t *imports = Q2Bridge_GetImportsInternal();
    if (imports == NULL || imports->DebugLineShow == NULL)
    {
        Q2_Print(PRT_WARNING, "[q2bridge] DebugLineShow: import not available\n");
        return;
    }

    imports->DebugLineShow(line, start, end, color);
}
