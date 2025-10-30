#pragma once

#include <stddef.h>

#include "../../precomp/l_precomp.h"
#include "../../precomp/l_script.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bot_chatstate_s bot_chatstate_t;

/**
 * Allocates a lightweight chat state that owns the currently loaded chat file
 * and a FIFO queue of console messages. Real implementations will add match
 * contexts, per-event cooldowns, and gender lookups derived from the HLIL.
 */
bot_chatstate_t *BotAllocChatState(void);

/** Releases the resources owned by the chat state, including loaded scripts. */
void BotFreeChatState(bot_chatstate_t *state);

/**
 * Loads a chat script using the precompiler wrappers. The TODO path will grow
 * into the HLIL-observed two-pass loader that builds reply tables before
 * exposing them to BotChat_EnterGame/BotChat_Kill style helpers.
 */
int BotLoadChatFile(bot_chatstate_t *state, const char *chatfile, const char *chatname);

/** Unloads the active chat file without destroying the chat state. */
void BotFreeChatFile(bot_chatstate_t *state);

/**
 * Enqueues a console message for later inspection. The queue lets the interface
 * stubs surface messages during development even before full chat dispatch is
 * available.
 */
void BotQueueConsoleMessage(bot_chatstate_t *state, int type, const char *message);

/**
 * Pops the next queued message. Returns 1 when a message is copied into the
 * caller supplied buffers and 0 when no messages are pending.
 */
int BotNextConsoleMessage(bot_chatstate_t *state, int *type, char *buffer, size_t buffer_size);

/** Removes the first message of the requested type from the queue. */
int BotRemoveConsoleMessage(bot_chatstate_t *state, int type);

/** Returns the number of queued console messages regardless of type. */
size_t BotNumConsoleMessages(const bot_chatstate_t *state);

/**
 * Placeholder for routing text to clients when BotChat_EnterGame and friends
 * eventually transition out of stub form. The TODO captures the HLIL event
 * flow for later implementation.
 */
void BotEnterChat(bot_chatstate_t *state, int client, int sendto);

/**
 * Placeholder for scripted replies. The return value mirrors Quake III's API:
 * non-zero indicates a reply was constructed.
 */
int BotReplyChat(bot_chatstate_t *state, const char *message, unsigned long int context);

/** Utility helper matching the legacy botlib export. */
int BotChatLength(const char *message);

#ifdef __cplusplus
} // extern "C"
#endif

