#ifndef Q2BRIDGE_BOTLIB_H
#define Q2BRIDGE_BOTLIB_H

#include "shared/bot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Version constant reconstructed from the HLIL dump.
 */
#define BOTLIB_API_VERSION 2

struct bot_import_s {
    int api_version;
};
typedef struct bot_import_s bot_import_t;

struct bot_export_s {
    int api_version;
    bot_status_t (*BotLibSetup)(void);
    bot_status_t (*BotLibShutdown)(void);
    bot_status_t (*BotLibLoadMap)(const char *mapname);
    bot_status_t (*BotLibFreeMap)(void);
};
typedef struct bot_export_s bot_export_t;

bot_export_t *GetBotAPI(int api_version, bot_import_t *import_table);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* Q2BRIDGE_BOTLIB_H */
