#pragma once

#include <stddef.h>

#include "shared/bot_types.h"
#include "botlib/precomp/l_precomp.h"
#include "botlib/precomp/l_script.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bot_chatstate_s bot_chatstate_t;

#ifndef CHAT_GENDERLESS
#define CHAT_GENDERLESS 0
#endif
#ifndef CHAT_GENDERFEMALE
#define CHAT_GENDERFEMALE 1
#endif
#ifndef CHAT_GENDERMALE
#define CHAT_GENDERMALE 2
#endif

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

/**
 * Constructs a scripted reply with the Quake III split between message and
 * variable synonym contexts plus the fixed var0-var7 replacement slots.
 */
int BotReplyChatWithContexts(bot_chatstate_t *state,
	const char *message,
	unsigned long int mcontext,
	unsigned long int vcontext,
	const char *var0,
	const char *var1,
	const char *var2,
	const char *var3,
	const char *var4,
	const char *var5,
	const char *var6,
	const char *var7);

/** Returns the length of the currently constructed pending chat message. */
int BotChatLength(const bot_chatstate_t *state);

/** Copies and clears the currently constructed pending chat message. */
void BotGetChatMessage(bot_chatstate_t *state, char *buffer, int buffer_size);

/** Sets the chat state's gender metadata for reply-key matching. */
void BotSetChatGender(bot_chatstate_t *state, int gender);

/** Sets the chat state's name metadata and owning client. */
void BotSetChatName(bot_chatstate_t *state, const char *name, int client);

/** Returns the chat persona name used by reply-key matching. */
const char *BotChatName(const bot_chatstate_t *state);

/** Returns the owning client used for chat commands, or -1 when unset. */
int BotChatClient(const bot_chatstate_t *state);

/** Returns the substring index using the retail case-sensitivity flag. */
int StringContains(const char *str1, const char *str2, int casesensitive);

/** Collapses retail whitespace runs in place. */
void UnifyWhiteSpaces(char *string);

/** Replaces synonyms in place using the shared setup synonym cache. */
void BotReplaceSynonyms(char *string, unsigned long int context);

/** Finds a setup match template and fills the retail match result. */
int BotFindMatch(const char *str, bot_match_t *match, unsigned long int context);

/** Copies one captured match variable to the caller buffer. */
void BotMatchVariable(const bot_match_t *match, int variable, char *buffer, int buffer_size);

/** Returns 1 when the supplied phrase is registered for the synonym context. */
int BotChat_HasSynonymPhrase(const bot_chatstate_t *state, const char *context_name, const char *phrase);

/** Returns 1 when the reply table contains the provided template for the context. */
int BotChat_HasReplyTemplate(const bot_chatstate_t *state, unsigned long int context, const char *template_text);

#ifdef __cplusplus
} // extern "C"
#endif

