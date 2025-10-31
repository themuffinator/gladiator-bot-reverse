#pragma once

#include "q2bridge/botlib.h"

#include "chat/ai_chat.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reset AI state when a new map is loaded.
 */
void BotAI_ResetForNewMap(void);

/**
 * @brief Begin a new simulation frame.
 */
void BotAI_BeginFrame(float time);

/**
 * @brief Ensure a console chat state exists for the requested client.
 *
 * @return Pointer to the chat state on success, or NULL when allocation fails
 *         or the client slot is invalid.
 */
bot_chatstate_t *BotAI_ConsoleState(int client);

/**
 * @brief Record an incoming console message for the specified client.
 */
void BotAI_QueueConsoleMessage(int client, int type, const char *message);

/**
 * @brief Queue a sound event that should be evaluated by the AI subsystems.
 */
int BotAI_AddSoundEvent(const vec3_t origin,
                        int ent,
                        int channel,
                        int soundindex,
                        float volume,
                        float attenuation,
                        float timeofs);

/**
 * @brief Queue a point light event that should be evaluated by the AI subsystems.
 */
int BotAI_AddPointLightEvent(const vec3_t origin,
                             int ent,
                             float radius,
                             float r,
                             float g,
                             float b,
                             float time,
                             float decay);

/**
 * @brief Run the per-frame AI orchestrator for a client.
 */
int BotAI_Think(int client, float thinktime);

/**
 * @brief Dump the queued console messages for a specific client.
 */
void BotAI_DebugDumpConsoleMessages(bot_import_t *imports, int client);

/**
 * @brief Dump the queued console messages for all active clients.
 */
void BotAI_DebugDumpConsoleMessagesAll(bot_import_t *imports);

/**
 * @brief Toggle the debug draw flag for a client and report the new state.
 */
void BotAI_DebugToggleDraw(bot_import_t *imports, int client);

#ifdef __cplusplus
} // extern "C"
#endif
