#ifndef BOTLIB_INTERFACE_BOT_INTERFACE_H
#define BOTLIB_INTERFACE_BOT_INTERFACE_H

#include "q2bridge/botlib.h"
#include "shared/platform_export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bot_client_state_s bot_client_state_t;
typedef struct ai_dm_enemy_info_s ai_dm_enemy_info_t;
typedef int (*bot_ai_node_step_fn)(bot_client_state_t *state, void *context);

#define BOT_AI_MAX_NODE_SWITCHES 50

GLADIATOR_API bot_export_t *GetBotAPI(bot_import_t *import_table);
GLADIATOR_API bot_export_t *GetBotAPIEx(bot_import_t *import_table,
	size_t import_size);

/* Internal combat-parity seams; these are not retail export-table entries. */
int BotAI_UpdateEnemyBattleInventory(bot_client_state_t *state,
	int enemy_entity);
int BotAI_SameTeam(const bot_client_state_t *state, int entity);
int BotAI_FindEnemy(bot_client_state_t *state, ai_dm_enemy_info_t *enemy);
void BotAI_UseItems(const bot_client_state_t *state);
void BotAI_BattleUseItems(const bot_client_state_t *state);
int BotAI_CarryingFlag(const bot_client_state_t *state);
float BotAI_Aggression(const bot_client_state_t *state);
int BotAI_WantsToRetreat(const bot_client_state_t *state);
int BotAI_WantsToChase(const bot_client_state_t *state);
int BotAI_CanAndWantsToRocketJump(const bot_client_state_t *state);
int BotAI_RunNodeSwitchLoop(bot_client_state_t *state,
	bot_ai_node_step_fn step,
	void *context);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BOTLIB_INTERFACE_BOT_INTERFACE_H */
