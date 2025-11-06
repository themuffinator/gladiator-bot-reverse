#include "botlib_common/l_assets.h"
#include "botlib_common/l_log.h"
#include "botlib_common/l_memory.h"
#include "botlib_interface/botlib_interface.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static int g_last_botlib_message_type = 0;
static char g_last_botlib_message[1024];

void BotLib_TestResetLastMessage(void) {
    g_last_botlib_message_type = 0;
    g_last_botlib_message[0] = '\0';
}

const char *BotLib_TestGetLastMessage(void) {
    return g_last_botlib_message;
}

int BotLib_TestGetLastMessageType(void) {
    return g_last_botlib_message_type;
}

void BotLib_Print(int type, const char *fmt, ...) {
    g_last_botlib_message_type = type;

    va_list args;
    va_start(args, fmt);

    va_list copy;
    va_copy(copy, args);
    vsnprintf(g_last_botlib_message, sizeof(g_last_botlib_message), fmt, copy);
    va_end(copy);

    vfprintf(stderr, fmt, args);
    va_end(args);
}

void BotLib_LogWrite(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void BotLib_Error(int level, const char *fmt, ...) {
    (void)level;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

const botlib_import_table_t *BotInterface_GetImportTable(void) {
    return NULL;
}

float LibVarValue(const char *var_name, const char *default_value) {
    (void)var_name;
    (void)default_value;
    return 0.0f;
}

void *GetMemory(size_t size) {
    return malloc(size);
}

void *GetClearedMemory(size_t size) {
    return calloc(1, size);
}

void *GetMemory(size_t size) {
    return malloc(size);
}

void FreeMemory(void *ptr) {
    free(ptr);
}

bool BotLib_LocateAssetRoot(char *buffer, size_t size) {
    (void)buffer;
    (void)size;
    return false;
}
