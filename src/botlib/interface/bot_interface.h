#ifndef BOTLIB_INTERFACE_BOT_INTERFACE_H
#define BOTLIB_INTERFACE_BOT_INTERFACE_H

#include "q2bridge/botlib.h"
#include "shared/platform_export.h"

#ifdef __cplusplus
extern "C" {
#endif

GLADIATOR_API bot_export_t *GetBotAPI(bot_import_t *import_table);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BOTLIB_INTERFACE_BOT_INTERFACE_H */
