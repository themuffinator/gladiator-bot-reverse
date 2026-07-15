#ifndef BOTLIB_INTERFACE_BOT_INTERFACE_H
#define BOTLIB_INTERFACE_BOT_INTERFACE_H

#include "q2bridge/botlib.h"
#include "shared/platform_export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bot_client_state_s bot_client_state_t;

GLADIATOR_API bot_export_t *GetBotAPI(bot_import_t *import_table);
GLADIATOR_API bot_export_t *GetBotAPIEx(bot_import_t *import_table,
	size_t import_size);
int BotAI_UpdateEnemyBattleInventory(bot_client_state_t *state,
	int enemy_entity);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BOTLIB_INTERFACE_BOT_INTERFACE_H */
