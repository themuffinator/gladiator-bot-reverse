#include "q2bridge/bridge.h"

static bot_import_t *g_q2_imports = NULL;

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
