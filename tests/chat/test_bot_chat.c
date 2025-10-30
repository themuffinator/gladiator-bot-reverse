#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "botlib/ai/chat/ai_chat.h"

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

int main(void) {
    test_reply_chat_death_context();
    test_reply_chat_falls_back_to_reply_table();
    test_synonym_lookup_contains_nearbyitem_entries();
    test_known_template_is_registered();

    printf("bot_chat_tests: all checks passed\n");
    return 0;
}
