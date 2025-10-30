#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/interface/botlib_interface.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void BotLib_Print(int type, const char *fmt, ...) {
    (void)type;
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

void *GetClearedMemory(size_t size) {
    return calloc(1, size);
}

void FreeMemory(void *ptr) {
    free(ptr);
}
