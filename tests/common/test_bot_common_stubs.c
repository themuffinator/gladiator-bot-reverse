#include "botlib_common/l_log.h"
#include "botlib_common/l_memory.h"
#include "botlib_interface/botlib_interface.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void BotLib_Error(int level, const char *fmt, ...) {
    (void)level;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static void Stub_Print(int type, const char *fmt, ...) {
    (void)type;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static void Stub_DPrint(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static const botlib_import_table_t g_stub_imports = {
    .Print = Stub_Print,
    .DPrint = Stub_DPrint,
    .BotLibVarGet = NULL,
    .BotLibVarSet = NULL,
};

const botlib_import_table_t *BotInterface_GetImportTable(void) {
    return &g_stub_imports;
}
