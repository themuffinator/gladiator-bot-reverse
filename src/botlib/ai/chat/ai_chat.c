#include "ai_chat.h"

#include <stdlib.h>
#include <string.h>

#include "../../common/l_log.h"

#define BOT_CHAT_MAX_CONSOLE_MESSAGES 16
#define BOT_CHAT_MAX_MESSAGE_CHARS 256

typedef struct bot_console_message_s {
    int type;
    char text[BOT_CHAT_MAX_MESSAGE_CHARS];
} bot_console_message_t;

struct bot_chatstate_s {
    pc_source_t *active_source;
    pc_script_t *active_script;
    char active_chatfile[128];
    char active_chatname[64];
    bot_console_message_t console_queue[BOT_CHAT_MAX_CONSOLE_MESSAGES];
    size_t console_head;
    size_t console_count;
};

static void BotChat_ResetConsoleQueue(bot_chatstate_t *state)
{
    if (state == NULL) {
        return;
    }

    memset(state->console_queue, 0, sizeof(state->console_queue));
    state->console_head = 0;
    state->console_count = 0;
}

static void BotChat_ClearMetadata(bot_chatstate_t *state)
{
    if (state == NULL) {
        return;
    }

    state->active_chatfile[0] = '\0';
    state->active_chatname[0] = '\0';
}

bot_chatstate_t *BotAllocChatState(void)
{
    bot_chatstate_t *state = calloc(1, sizeof(*state));
    if (state == NULL) {
        BotLib_Print(PRT_FATAL, "BotAllocChatState: allocation failed\n");
        return NULL;
    }

    BotChat_ResetConsoleQueue(state);
    BotChat_ClearMetadata(state);
    return state;
}

void BotFreeChatState(bot_chatstate_t *state)
{
    if (state == NULL) {
        return;
    }

    BotFreeChatFile(state);
    free(state);
}

int BotLoadChatFile(bot_chatstate_t *state, const char *chatfile, const char *chatname)
{
    if (state == NULL || chatfile == NULL || chatname == NULL) {
        return 0;
    }

    BotFreeChatFile(state);

    pc_source_t *source = PC_LoadSourceFile(chatfile);
    if (source == NULL) {
        BotLib_Print(PRT_ERROR, "BotLoadChatFile: couldn't load chat %s\n", chatfile);
        return 0;
    }

    pc_script_t *script = PS_CreateScriptFromSource(source);
    if (script == NULL) {
        BotLib_Print(PRT_ERROR, "BotLoadChatFile: script wrapper failed for %s\n", chatfile);
        PC_FreeSource(source);
        return 0;
    }

    state->active_source = source;
    state->active_script = script;
    strncpy(state->active_chatfile, chatfile, sizeof(state->active_chatfile) - 1);
    state->active_chatfile[sizeof(state->active_chatfile) - 1] = '\0';
    strncpy(state->active_chatname, chatname, sizeof(state->active_chatname) - 1);
    state->active_chatname[sizeof(state->active_chatname) - 1] = '\0';

    BotLib_Print(PRT_MESSAGE, "BotLoadChatFile: staged %s (%s) for parsing\n", state->active_chatfile, state->active_chatname);
    // TODO: Mirror the HLIL two-pass loader that fills reply/initial chat tables
    // so BotChat_EnterGame/BotChat_Kill can request chatmessage_t blocks.
    return 1;
}

void BotFreeChatFile(bot_chatstate_t *state)
{
    if (state == NULL) {
        return;
    }

    if (state->active_script != NULL) {
        PS_FreeScript(state->active_script);
        state->active_script = NULL;
    }

    if (state->active_source != NULL) {
        PC_FreeSource(state->active_source);
        state->active_source = NULL;
    }

    BotChat_ClearMetadata(state);
}

void BotQueueConsoleMessage(bot_chatstate_t *state, int type, const char *message)
{
    if (state == NULL || message == NULL) {
        return;
    }

    if (state->console_count == BOT_CHAT_MAX_CONSOLE_MESSAGES) {
        // Drop the oldest message to make room. The real implementation would
        // honour the HLIL eviction rules when the queue overflows.
        state->console_head = (state->console_head + 1) % BOT_CHAT_MAX_CONSOLE_MESSAGES;
        state->console_count--;
    }

    size_t insert_index = (state->console_head + state->console_count) % BOT_CHAT_MAX_CONSOLE_MESSAGES;
    bot_console_message_t *slot = &state->console_queue[insert_index];
    slot->type = type;
    strncpy(slot->text, message, sizeof(slot->text) - 1);
    slot->text[sizeof(slot->text) - 1] = '\0';
    state->console_count++;
}

int BotNextConsoleMessage(bot_chatstate_t *state, int *type, char *buffer, size_t buffer_size)
{
    if (state == NULL || state->console_count == 0) {
        return 0;
    }

    const bot_console_message_t *slot = &state->console_queue[state->console_head];
    if (type != NULL) {
        *type = slot->type;
    }

    if (buffer != NULL && buffer_size > 0) {
        strncpy(buffer, slot->text, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
    }

    state->console_head = (state->console_head + 1) % BOT_CHAT_MAX_CONSOLE_MESSAGES;
    state->console_count--;
    return 1;
}

int BotRemoveConsoleMessage(bot_chatstate_t *state, int type)
{
    if (state == NULL || state->console_count == 0) {
        return 0;
    }

    size_t index = state->console_head;
    for (size_t i = 0; i < state->console_count; ++i) {
        if (state->console_queue[index].type == type) {
            for (size_t j = i; j + 1 < state->console_count; ++j) {
                size_t from = (state->console_head + j + 1) % BOT_CHAT_MAX_CONSOLE_MESSAGES;
                size_t to = (state->console_head + j) % BOT_CHAT_MAX_CONSOLE_MESSAGES;
                state->console_queue[to] = state->console_queue[from];
            }
            state->console_count--;
            return 1;
        }
        index = (index + 1) % BOT_CHAT_MAX_CONSOLE_MESSAGES;
    }

    return 0;
}

size_t BotNumConsoleMessages(const bot_chatstate_t *state)
{
    if (state == NULL) {
        return 0;
    }

    return state->console_count;
}

void BotEnterChat(bot_chatstate_t *state, int client, int sendto)
{
    if (state == NULL) {
        return;
    }

    // TODO: Follow the HLIL event flow: BotChat_EnterGame/BotChat_Kill should
    // pull from the initial/reply chat tables, expand random strings, and push
    // the result through BotConstructChatMessage before routing to clients.
    BotLib_Print(PRT_MESSAGE,
                 "BotEnterChat: stub dispatch (client %d -> %d) using chat '%s'\n",
                 client,
                 sendto,
                 state->active_chatname);
}

int BotReplyChat(bot_chatstate_t *state, const char *message, unsigned long int context)
{
    if (state == NULL || message == NULL) {
        return 0;
    }

    // TODO: Bind to the HLIL-backed reply system that selects from reply chats
    // based on the supplied context and populates bot_match_t structures.
    BotLib_Print(PRT_MESSAGE,
                 "BotReplyChat: stub invoked for context %lu with seed '%s'\n",
                 context,
                 message);
    return 0;
}

int BotChatLength(const char *message)
{
    if (message == NULL) {
        return 0;
    }

    return (int)strlen(message);
}
