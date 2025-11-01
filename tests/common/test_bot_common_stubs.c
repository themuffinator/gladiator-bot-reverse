#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/interface/botlib_interface.h"

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

const botlib_import_table_t *BotInterface_GetImportTable(void) {
    return NULL;
}
