#include "l_log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "l_libvar.h"
#include "../interface/botlib_interface.h"

#define BOTLIB_MAX_PRINT_LEN 2048
#define BOTLIB_MAX_LOGNAME   1024

typedef struct botlib_log_state_s {
    FILE *file;
    char filename[BOTLIB_MAX_LOGNAME];
    unsigned int write_count;
} botlib_log_state_t;

static botlib_log_state_t g_botlib_log_state;

static void BotLib_FormatMessage(char *buffer, size_t buffer_size, const char *fmt, va_list args)
{
    if (buffer_size == 0) {
        return;
    }

    int written = vsnprintf(buffer, buffer_size, fmt, args);
    if (written < 0) {
        buffer[0] = '\0';
        return;
    }

    if ((size_t)written >= buffer_size) {
        buffer[buffer_size - 1] = '\0';
    }
}

static void BotLib_OutputToImport(int priority, const char *message)
{
    const botlib_import_table_t *imports = BotInterface_GetImportTable();
    if (imports == NULL || imports->Print == NULL) {
        return;
    }

    imports->Print(priority, "%s", message);
}

static void BotLib_OutputDebug(const char *message)
{
    const botlib_import_table_t *imports = BotInterface_GetImportTable();
    if (imports == NULL || imports->DPrint == NULL) {
        return;
    }

    imports->DPrint("%s", message);
}

static void BotLib_LogAppend(const char *message)
{
    if (g_botlib_log_state.file == NULL || message == NULL) {
        return;
    }

    fputs(message, g_botlib_log_state.file);
    if (message[0] != '\0' && message[strlen(message) - 1] != '\n') {
        fputc('\n', g_botlib_log_state.file);
    }
    fflush(g_botlib_log_state.file);
    g_botlib_log_state.write_count++;
}

void BotLib_LogOpen(const char *filename)
{
    if (LibVarValue("log", "0") == 0.0f) {
        return;
    }

    if (filename == NULL || filename[0] == '\0') {
        BotLib_Print(PRT_MESSAGE, "openlog <filename>\n");
        return;
    }

    if (g_botlib_log_state.file != NULL) {
        BotLib_Print(PRT_ERROR, "log file %s is already opened\n", g_botlib_log_state.filename);
        return;
    }

    FILE *fp = fopen(filename, "wb");
    if (fp == NULL) {
        BotLib_Print(PRT_ERROR, "can't open the log file %s\n", filename);
        return;
    }

    g_botlib_log_state.file = fp;
    strncpy(g_botlib_log_state.filename, filename, sizeof(g_botlib_log_state.filename));
    g_botlib_log_state.filename[sizeof(g_botlib_log_state.filename) - 1] = '\0';
    g_botlib_log_state.write_count = 0;

    BotLib_Print(PRT_MESSAGE, "Opened log %s\n", g_botlib_log_state.filename);
}

void BotLib_LogClose(void)
{
    if (g_botlib_log_state.file == NULL) {
        return;
    }

    if (fclose(g_botlib_log_state.file) != 0) {
        BotLib_Print(PRT_ERROR, "can't close log file %s\n", g_botlib_log_state.filename);
        return;
    }

    g_botlib_log_state.file = NULL;
    BotLib_Print(PRT_MESSAGE, "Closed log %s\n", g_botlib_log_state.filename);
    g_botlib_log_state.filename[0] = '\0';
    g_botlib_log_state.write_count = 0;
}

void BotLib_LogShutdown(void)
{
    if (g_botlib_log_state.file != NULL) {
        BotLib_LogClose();
    }
}

void BotLib_LogWrite(const char *fmt, ...)
{
    if (g_botlib_log_state.file == NULL || fmt == NULL) {
        return;
    }

    char buffer[BOTLIB_MAX_PRINT_LEN];
    va_list args;
    va_start(args, fmt);
    BotLib_FormatMessage(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    BotLib_LogAppend(buffer);
}

void BotLib_LogWriteTimeStamped(const char *fmt, ...)
{
    if (g_botlib_log_state.file == NULL || fmt == NULL) {
        return;
    }

    char message[BOTLIB_MAX_PRINT_LEN];
    va_list args;
    va_start(args, fmt);
    BotLib_FormatMessage(message, sizeof(message), fmt, args);
    va_end(args);

    clock_t ticks = clock();
    double seconds = (double)ticks / (double)CLOCKS_PER_SEC;
    int hours = (int)(seconds / 3600.0);
    int minutes = (int)(seconds / 60.0) % 60;
    int secs = (int)seconds % 60;
    int centis = (int)(seconds * 100.0) % 100;

    char prefix[64];
    snprintf(prefix, sizeof(prefix), "%u   %02d:%02d:%02d:%02d   ",
             g_botlib_log_state.write_count,
             hours,
             minutes,
             secs,
             centis);

    if (g_botlib_log_state.file != NULL) {
        fputs(prefix, g_botlib_log_state.file);
        fputs(message, g_botlib_log_state.file);
        fputc('\n', g_botlib_log_state.file);
        fflush(g_botlib_log_state.file);
        g_botlib_log_state.write_count++;
    }
}

FILE *BotLib_LogFile(void)
{
    return g_botlib_log_state.file;
}

void BotLib_LogFlush(void)
{
    if (g_botlib_log_state.file != NULL) {
        fflush(g_botlib_log_state.file);
    }
}

void BotLib_Print(int priority, const char *fmt, ...)
{
    char buffer[BOTLIB_MAX_PRINT_LEN];

    va_list args;
    va_start(args, fmt);
    BotLib_FormatMessage(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    switch (priority) {
        case PRT_WARNING:
            BotLib_LogWrite("Warning: %s", buffer);
            break;
        case PRT_ERROR:
            BotLib_LogWrite("Error: %s", buffer);
            break;
        case PRT_FATAL:
            BotLib_LogWrite("Fatal: %s", buffer);
            break;
        case PRT_EXIT:
            BotLib_LogWrite("Exit: %s", buffer);
            break;
        default:
            BotLib_LogWrite("%s", buffer);
            break;
    }

    BotLib_OutputToImport(priority, buffer);
}

void BotLib_DPrint(const char *fmt, ...)
{
    char buffer[BOTLIB_MAX_PRINT_LEN];

    va_list args;
    va_start(args, fmt);
    BotLib_FormatMessage(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    BotLib_LogWrite("%s", buffer);
    BotLib_OutputDebug(buffer);
}
