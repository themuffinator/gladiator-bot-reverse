#pragma once

#include <stddef.h>

#include "botlib/precomp/l_precomp.h"
#include "botlib/precomp/l_script.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bot_chatstate_s bot_chatstate_t;

/**
 * Allocates a chat state that owns parsed chat templates, reply tables,
 * random-string tables, cooldown state, and the diagnostic FIFO queue.
 */
bot_chatstate_t *BotAllocChatState(void);

/** Releases the resources owned by the chat state, including loaded scripts. */
void BotFreeChatState(bot_chatstate_t *state);

/**
 * Loads a chat script through the precompiler wrappers, including retail named
 * initial-chat blocks and sibling random/synonym/match assets when present.
 */
int BotLoadChatFile(bot_chatstate_t *state, const char *chatfile, const char *chatname);

/** Loads the shared retail chat AI assets selected by chat-related libvars. */
int BotSetupChatAI(void);

/** Releases shared chat AI setup assets. */
void BotShutdownChatAI(void);

/** Unloads the active chat file without destroying the chat state. */
void BotFreeChatFile(bot_chatstate_t *state);

/**
 * Enqueues a console message for later inspection. The queue lets the interface
 * and regression tests observe diagnostics and constructed chat output.
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
 * Overrides the synthetic clock used for chat cooldowns. Passing a negative
 * value clears the override and resumes using the system clock.
 */
void BotChat_SetTime(bot_chatstate_t *state, double now_seconds);

/** Configures the cooldown duration for the supplied context identifier. */
void BotChat_SetContextCooldown(bot_chatstate_t *state,
unsigned long context,
double cooldown_seconds);

/** Builds and dispatches the MSG_ENTERGAME chat event for the supplied client. */
void BotEnterChat(bot_chatstate_t *state, int client, int sendto);

/** Returns the number of loaded retail initial chats for a type name. */
int BotNumInitialChats(const bot_chatstate_t *state, const char *type);

/**
 * Constructs one retail initial chat line for a type name. Variable arguments
 * are optional string replacements and must end with NULL.
 */
int BotInitialChat(bot_chatstate_t *state,
	const char *type,
	unsigned long context,
	...);

int BotChat_EnterGame(bot_chatstate_t *state, int client, int sendto);
int BotChat_Kill(bot_chatstate_t *state, int client, int sendto);
int BotChat_Death(bot_chatstate_t *state, int client, int sendto);
int BotChat_EnemySuicide(bot_chatstate_t *state, int client, int sendto);
int BotChat_HitTalking(bot_chatstate_t *state, int client, int sendto);
int BotChat_HitNoDeath(bot_chatstate_t *state, int client, int sendto);
int BotChat_HitNoKill(bot_chatstate_t *state, int client, int sendto);
int BotChat_Random(bot_chatstate_t *state, int client, int sendto);
int BotChat_Insult(bot_chatstate_t *state, int client, int sendto);
int BotChat_Praise(bot_chatstate_t *state, int client, int sendto);

/**
 * Constructs a scripted reply. The return value mirrors Quake III's API:
 * non-zero indicates a reply was constructed and dispatched.
 */
int BotReplyChat(bot_chatstate_t *state, const char *message, unsigned long int context);

/** Utility helper matching the legacy botlib export. */
int BotChatLength(const char *message);

/** Returns 1 when the supplied phrase is registered for the synonym context. */
int BotChat_HasSynonymPhrase(const bot_chatstate_t *state, const char *context_name, const char *phrase);

/** Returns 1 when the reply table contains the provided template for the context. */
int BotChat_HasReplyTemplate(const bot_chatstate_t *state, unsigned long int context, const char *template_text);

#ifdef __cplusplus
} // extern "C"
#endif

