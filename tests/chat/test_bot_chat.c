#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "botlib/ai/chat/ai_chat.h"
#include "botlib/common/l_log.h"

typedef struct pc_script_s pc_script_t;

extern void PS_SetBaseFolder(char *path);
extern pc_script_t *LoadScriptFile(const char *filename);
extern void BotLib_TestResetLastMessage(void);
extern const char *BotLib_TestGetLastMessage(void);
extern int BotLib_TestGetLastMessageType(void);

static void drain_console(bot_chatstate_t *chat) {
    int type = 0;
    char buffer[256];
    while (BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer))) {
        (void)type;
    }
}

static void test_reply_chat_death_context(void) {
    bot_chatstate_t *chat = BotAllocChatState();
    assert(chat != NULL);
    assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/rchat.c", "reply"));

    drain_console(chat);
    assert(BotReplyChat(chat, "unit-test", 1));

    int type = 0;
    char buffer[256];
    assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
    assert(type == 1);
    assert(BotChat_HasReplyTemplate(chat, 1, buffer));

    BotFreeChatState(chat);
}

static void test_reply_chat_falls_back_to_reply_table(void) {
    bot_chatstate_t *chat = BotAllocChatState();
    assert(chat != NULL);
    assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/rchat.c", "reply"));

    drain_console(chat);
    assert(BotReplyChat(chat, "abnormal", 5));

    int type = 0;
    char buffer[256];
    assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
    assert(type == 5);
    assert(BotChat_HasReplyTemplate(chat, 5, buffer));

    BotFreeChatState(chat);
}

static void test_synonym_lookup_contains_nearbyitem_entries(void) {
    bot_chatstate_t *chat = BotAllocChatState();
    assert(chat != NULL);
    assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/rchat.c", "reply"));

    assert(BotChat_HasSynonymPhrase(chat, "CONTEXT_NEARBYITEM", "Quad Damage"));
    assert(BotChat_HasSynonymPhrase(chat, "CONTEXT_NEARBYITEM", "Rocket Launcher"));

    BotFreeChatState(chat);
}

static void test_known_template_is_registered(void) {
    bot_chatstate_t *chat = BotAllocChatState();
    assert(chat != NULL);
    assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/rchat.c", "reply"));

    assert(BotChat_HasReplyTemplate(chat, 1, "{VICTIM} commits suicide"));

    BotFreeChatState(chat);
}

static void test_include_path_too_long_is_rejected(void) {
    BotLib_TestResetLastMessage();

    char overlong_name[1400];
    size_t fill = sizeof(overlong_name) - 5;
    memset(overlong_name, 'a', fill);
    memcpy(&overlong_name[fill], ".cfg", 4);
    overlong_name[fill + 4] = '\0';

    char empty_base[] = "";
    PS_SetBaseFolder(empty_base);
    pc_script_t *script = LoadScriptFile(overlong_name);
    assert(script == NULL);

    const char *message = BotLib_TestGetLastMessage();
    assert(message != NULL);
    assert(strstr(message, "include path") != NULL);
    assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
}

int main(void) {
    test_reply_chat_death_context();
    test_reply_chat_falls_back_to_reply_table();
    test_synonym_lookup_contains_nearbyitem_entries();
    test_known_template_is_registered();
    test_include_path_too_long_is_rejected();

    printf("bot_chat_tests: all checks passed\n");
    return 0;
}
