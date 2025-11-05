#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "botlib/ai/chat/ai_chat.h"
#include "botlib/precomp/l_precomp.h"
#include "botlib/precomp/l_script.h"

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
    const size_t segment_length = 256;
    const size_t segment_count = 5;
    const size_t fragment_length = segment_count * segment_length + (segment_count - 1);

    char include_fragment[fragment_length + 1];
    size_t offset = 0;
    for (size_t segment = 0; segment < segment_count; ++segment) {
        if (segment > 0) {
            include_fragment[offset++] = '/';
        }
        memset(include_fragment + offset, (int)('a' + (int)segment), segment_length);
        offset += segment_length;
    }
    include_fragment[offset] = '\0';

    char script[sizeof(include_fragment) + 16];
    int written = snprintf(script, sizeof(script), "#include <%s>\n", include_fragment);
    assert(written > 0);
    assert((size_t)written < sizeof(script));

    pc_source_t *source = PC_LoadSourceMemory("unit-test", script, (size_t)written);
    assert(source != NULL);

    pc_token_t token;
    assert(!PC_ReadToken(source, &token));

    const pc_diagnostic_t *diagnostic = PC_GetDiagnostics(source);
    bool found_error = false;
    while (diagnostic != NULL) {
        if (diagnostic->level == PC_ERROR_LEVEL_ERROR && diagnostic->message != NULL
            && strstr(diagnostic->message, "path too long") != NULL) {
            found_error = true;
            break;
        }
        diagnostic = diagnostic->next;
    }
    assert(found_error);

    PC_FreeSource(source);
}

int main(void) {
    test_include_path_too_long_is_rejected();
    test_reply_chat_death_context();
    test_reply_chat_falls_back_to_reply_table();
    test_synonym_lookup_contains_nearbyitem_entries();
    test_known_template_is_registered();

    printf("bot_chat_tests: all checks passed\n");
    return 0;
}
