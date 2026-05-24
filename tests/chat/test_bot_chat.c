#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <crtdbg.h>
#endif

#include "botlib/ai_chat/ai_chat.h"
#include "botlib/common/l_log.h"
#include "botlib/precomp/l_precomp.h"
#include "botlib/precomp/l_script.h"
#include "q2bridge/botlib.h"
#include "q2bridge/bridge.h"

extern void BotLib_TestResetLastMessage(void);
extern const char *BotLib_TestGetLastMessage(void);
extern int BotLib_TestGetLastMessageType(void);
extern void BotLib_TestSetLibVar(const char *var_name, float value);
extern void BotLib_TestSetLibVarString(const char *var_name, const char *value);
extern void BotLib_TestResetLibVars(void);
extern void BotLib_TestSetMaxClients(float value);

enum
{
	TEST_MAX_CONSOLE_MESSAGES = 32,
	TEST_MAX_CONSOLE_TEXT = 256
};

typedef struct test_console_message_s
{
	int type;
	char text[TEST_MAX_CONSOLE_TEXT];
} test_console_message_t;

/*
=============
configure_crt_reports

Routes Windows debug CRT asserts to stderr so test failures cannot launch a
modal dialog during automated reconstruction runs.
=============
*/
static void configure_crt_reports(void)
{
#if defined(_WIN32)
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

/*
=============
configure_chat_libvars

Resets and sets the requested mocked libvar values.
=============
*/
static void configure_chat_libvars(float fastchat_value, float nochat_value) {
	BotLib_TestResetLibVars();
	BotLib_TestSetLibVar("fastchat", fastchat_value);
	BotLib_TestSetLibVar("nochat", nochat_value);
}

/*
=============
drain_console

Clears queued console messages for deterministic checks.
=============
*/
static void drain_console(bot_chatstate_t *chat) {
        int type = 0;
        char buffer[256];
        while (BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer))) {
                (void)type;
        }
}

/*
=============
snapshot_console_queue

Copies the pending console messages so tests can search without draining them.
=============
*/
static size_t snapshot_console_queue(bot_chatstate_t *chat,
	test_console_message_t *messages,
	size_t capacity)
{
	size_t count = 0;
	while (count < capacity)
	{
		int type = 0;
		char buffer[TEST_MAX_CONSOLE_TEXT];
		if (!BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)))
		{
			break;
		}
		messages[count].type = type;
		strncpy(messages[count].text,
			buffer,
			sizeof(messages[count].text) - 1);
		messages[count].text[sizeof(messages[count].text) - 1] = '\0';
		++count;
	}
	return count;
}

/*
=============
restore_console_queue

Pushes a snapshot back onto the FIFO for subsequent checks.
=============
*/
static void restore_console_queue(bot_chatstate_t *chat,
        const test_console_message_t *messages,
        size_t count)
{
        for (size_t i = 0; i < count; ++i)
        {
                BotQueueConsoleMessage(chat, messages[i].type, messages[i].text);
        }
}

typedef struct chat_bridge_mock_s
{
	int command_calls;
	int last_client;
	char last_command[256];
	int print_calls;
	int last_print_type;
	char last_print_message[256];
} chat_bridge_mock_t;

static chat_bridge_mock_t g_chat_bridge_mock;
static bot_import_t g_chat_bridge_imports;

/*
=============
ChatBridge_MockBotClientCommand

Captures BotClientCommand invocations for bridge-aware chat tests.
=============
*/
static void ChatBridge_MockBotClientCommand(int client, char *fmt, ...)
{
	if (fmt == NULL)
	{
		return;
	}

	va_list args;
	va_start(args, fmt);
	vsnprintf(g_chat_bridge_mock.last_command,
		sizeof(g_chat_bridge_mock.last_command),
		fmt,
		args);
	va_end(args);

	g_chat_bridge_mock.command_calls += 1;
	g_chat_bridge_mock.last_client = client;
}

/*
=============
ChatBridge_MockPrint

Records bridge print diagnostics for chat dispatch tests.
=============
*/
static void ChatBridge_MockPrint(int type, char *fmt, ...)
{
	if (fmt == NULL)
	{
		return;
	}

	va_list args;
	va_start(args, fmt);
	vsnprintf(g_chat_bridge_mock.last_print_message,
		sizeof(g_chat_bridge_mock.last_print_message),
		fmt,
		args);
	va_end(args);

	g_chat_bridge_mock.print_calls += 1;
	g_chat_bridge_mock.last_print_type = type;
}

/*
=============
ChatBridge_Reset

Initialises the bridge import table and clears prior mock state.
=============
*/
static void ChatBridge_Reset(void)
{
	memset(&g_chat_bridge_mock, 0, sizeof(g_chat_bridge_mock));
	memset(&g_chat_bridge_imports, 0, sizeof(g_chat_bridge_imports));

	g_chat_bridge_imports.BotClientCommand = ChatBridge_MockBotClientCommand;
	g_chat_bridge_imports.Print = ChatBridge_MockPrint;

	Q2Bridge_SetImportTable(&g_chat_bridge_imports);
}

/*
=============
assert_console_contains_message

Ensures the console queue includes the requested diagnostic string.
=============
*/
static void assert_console_contains_message(bot_chatstate_t *chat,
	int expected_type,
	const char *expected_text)
{
	test_console_message_t snapshot[TEST_MAX_CONSOLE_MESSAGES];
	const size_t count = snapshot_console_queue(chat,
		snapshot,
		TEST_MAX_CONSOLE_MESSAGES);
	int found = 0;
	for (size_t i = 0; i < count; ++i)
	{
		if (snapshot[i].type == expected_type &&
			strcmp(snapshot[i].text, expected_text) == 0)
		{
			found = 1;
			break;
		}
	}
	restore_console_queue(chat, snapshot, count);
	assert(found);
}

/*
=============
string_is_one_of

Checks whether a string matches one of the expected entries.
=============
*/
static bool string_is_one_of(const char *actual,
	const char *const *expected,
	size_t expected_count)
{
	for (size_t i = 0; i < expected_count; ++i)
	{
		if (strcmp(actual, expected[i]) == 0)
		{
			return true;
		}
	}

	return false;
}

/*
=============
test_enter_chat_uses_unit_test_template
=============
*/
static void test_enter_chat_uses_unit_test_template(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/unit_test_chat.c", "unit_enter_valid"));

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 2, 0.0);
	BotEnterChat(chat, 0, 0);

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 2);
	assert(strcmp(buffer, "{NETNAME} triggered the deterministic join message") == 0);

	BotFreeChatState(chat);
}

/*
=============
test_retail_initial_chat_block_drives_enter_event

Loads a retail bot personality chat file and dispatches its enter_game type.
=============
*/
static void test_retail_initial_chat_block_drives_enter_event(void)
{
	static const char *const expected_enter_messages[] = {
		"greetings",
		"who's going to lose to me today?",
		"it's showtime boyz",
		"anyone wanna slap the criminal element around?",
		"lets rock and roll!",
		"g'day mate",
		"let's kick it!",
		"wassup!",
		"Hey",
		"Hi",
	};

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe"));

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 2, 0.0);
	BotEnterChat(chat, 0, 0);

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 2);
	assert(string_is_one_of(buffer,
		expected_enter_messages,
		sizeof(expected_enter_messages) / sizeof(expected_enter_messages[0])));

	BotFreeChatState(chat);
}

/*
=============
test_retail_initial_chat_counts_raw_type_buckets

Verifies the loader preserves Gladiator initial-chat type buckets beyond the
event contexts currently dispatched by the game bridge.
=============
*/
static void test_retail_initial_chat_counts_raw_type_buckets(void)
{
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe"));

	const int enter_count = BotNumInitialChats(chat, "enter_game");
	const int exit_count = BotNumInitialChats(chat, "exit_game");
	const int start_count = BotNumInitialChats(chat, "start_level");
	const int end_count = BotNumInitialChats(chat, "end_level");

	assert(enter_count > 0);
	assert(exit_count > 0);
	assert(start_count > 0);
	assert(end_count > 0);
	assert(BotNumInitialChats(chat, "game_enter") == enter_count);
	assert(BotNumInitialChats(chat, "game_exit") == exit_count);
	assert(BotNumInitialChats(chat, "level_start") == start_count);
	assert(BotNumInitialChats(chat, "level_end") == end_count);
	assert(BotNumInitialChats(chat, "level_end_victory") == 0);
	assert(BotNumInitialChats(chat, "missing_type") == 0);

	BotFreeChatState(chat);
}

/*
=============
test_retail_initial_chat_constructs_from_alias

Exercises the reconstructed BotInitialChat path using the Quake III successor
alias for Gladiator's exit_game bucket.
=============
*/
static void test_retail_initial_chat_constructs_from_alias(void)
{
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe"));

	drain_console(chat);
	assert(BotInitialChat(chat,
		"game_exit",
		0,
		"Babe",
		"Opponent",
		"[invalid]",
		"[invalid]",
		"base1",
		NULL));

	int type = -1;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 0);
	assert(buffer[0] != '\0');
	assert(strstr(buffer, "\\v") == NULL);

	BotFreeChatState(chat);
}

/*
=============
test_initial_chat_pending_message_exports_and_enters

Pins the Q3-style split where BotInitialChat constructs pending text and
BotEnterChat sends that text through the bridge.
=============
*/
static void test_initial_chat_pending_message_exports_and_enters(void)
{
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe"));

	drain_console(chat);
	assert(BotInitialChat(chat,
		"game_exit",
		0,
		"Babe",
		"Opponent",
		"[invalid]",
		"[invalid]",
		"base1",
		NULL));
	assert(BotChatLength(chat) > 0);

	int type = -1;
	char expected[256];
	assert(BotNextConsoleMessage(chat, &type, expected, sizeof(expected)));
	assert(type == 0);
	assert(expected[0] != '\0');

	ChatBridge_Reset();
	BotLib_TestSetMaxClients(8.0f);
	BotEnterChat(chat, 6, 0);

	char expected_command[320];
	const int written = snprintf(expected_command,
		sizeof(expected_command),
		"say %s",
		expected);
	assert(written > 0);
	assert((size_t)written < sizeof(expected_command));
	assert(g_chat_bridge_mock.command_calls == 1);
	assert(g_chat_bridge_mock.last_client == 6);
	assert(strcmp(g_chat_bridge_mock.last_command, expected_command) == 0);
	assert(BotChatLength(chat) == 0);

	Q2Bridge_ClearImportTable();
	BotFreeChatState(chat);
}

/*
=============
test_get_chat_message_copies_and_clears_pending

Exercises the recovered BotGetChatMessage export semantics for pending initial
chat text.
=============
*/
static void test_get_chat_message_copies_and_clears_pending(void)
{
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe"));

	drain_console(chat);
	assert(BotInitialChat(chat,
		"game_exit",
		0,
		"Babe",
		"Opponent",
		"[invalid]",
		"[invalid]",
		"base1",
		NULL));
	assert(BotChatLength(chat) > 0);

	char buffer[256];
	BotGetChatMessage(chat, buffer, sizeof(buffer));
	assert(buffer[0] != '\0');
	assert(strstr(buffer, "\\v") == NULL);
	assert(BotChatLength(chat) == 0);

	memset(buffer, 'x', sizeof(buffer));
	BotGetChatMessage(chat, buffer, sizeof(buffer));
	assert(buffer[0] == '\0');

	BotFreeChatState(chat);
}

/*
=============
test_retail_initial_chat_missing_name_is_rejected

Ensures named chat blocks preserve the HLIL-observed not-found diagnostic.
=============
*/
static void test_retail_initial_chat_missing_name_is_rejected(void)
{
	char expected_message[256];
	const int written = snprintf(expected_message,
		sizeof(expected_message),
		"couldn't find chat missing in %s\n",
		BOT_ASSET_ROOT "/bots/babe_t.c");
	assert(written > 0);
	assert((size_t)written < sizeof(expected_message));

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	configure_chat_libvars(1.0f, 0.0f);
	BotLib_TestResetLastMessage();
	assert(!BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "missing"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strcmp(BotLib_TestGetLastMessage(), expected_message) == 0);
	assert_console_contains_message(chat, PRT_ERROR, expected_message);

	configure_chat_libvars(0.0f, 0.0f);
	BotFreeChatState(chat);
}

/*
=============
test_enter_chat_construct_message_failure_respects_cooldown_reset
=============
*/
static void test_enter_chat_construct_message_failure_respects_cooldown_reset(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/unit_test_chat.c", "unit_enter_invalid"));

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 2, 1.0);
	BotChat_SetTime(chat, 10.0);
	BotLib_TestResetLastMessage();
	BotEnterChat(chat, 0, 0);
	assert(BotNumConsoleMessages(chat) == 0);
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(), "too long") != NULL);

	BotEnterChat(chat, 0, 0);
	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 2);
	assert(strstr(buffer, "blocked by cooldown") != NULL);

	drain_console(chat);
	BotChat_SetTime(chat, 12.0);
	BotEnterChat(chat, 0, 0);
	assert(BotNumConsoleMessages(chat) == 0);

	BotFreeChatState(chat);
}

/*
=============
test_reply_chat_death_context
=============
*/
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
	assert(strlen(buffer) > 0);
	assert(strstr(buffer, "\\r") == NULL);

	BotFreeChatState(chat);
}

/*
=============
test_reply_chat_falls_back_to_reply_table
=============
*/
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

/*
=============
test_reply_chat_name_key_matches_configured_name

Verifies unquoted reply key name maps to the configured bot identity instead
of the literal token text.
=============
*/
static void test_reply_chat_name_key_matches_configured_name(void)
{
	static const char *const expected_damn_replies[] = {
		"please don't swear",
		"damn damn damn",
		"you have a flithy mouth",
		"dams are for beavers",
		"damn you!",
	};

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/rchat.c", "reply"));

	BotSetChatName(chat, "babe", 3);
	drain_console(chat);
	assert(BotReplyChat(chat, "babe", 7));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 7);
	assert(string_is_one_of(buffer,
		expected_damn_replies,
		sizeof(expected_damn_replies) / sizeof(expected_damn_replies[0])));

	BotFreeChatState(chat);
}

/*
=============
test_reply_chat_matches_synonym_pattern
=============
*/
static void test_reply_chat_matches_synonym_pattern(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/match_reply.c", "match_reply"));

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 2, 0.0);
	assert(BotReplyChat(chat, "Quad Damage acquired", 2));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 2);
	assert(strcmp(buffer, "NEARBYITEM acquired") == 0);

	BotFreeChatState(chat);
}

/*
=============
test_reply_chat_without_pattern_falls_back_to_reply_table
=============
*/
static void test_reply_chat_without_pattern_falls_back_to_reply_table(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/match_reply.c", "match_reply"));

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 2, 0.0);
	assert(BotReplyChat(chat, "fallback", 2));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 2);
	assert(strcmp(buffer, "fallback reply") == 0);

	BotFreeChatState(chat);
}

/*
=============
test_enter_chat_enqueues_message
=============
*/
static void test_enter_chat_enqueues_message(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/match.c", "match"));

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 2, 0.0);
	BotEnterChat(chat, 0, 0);

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 2);
	assert(strcmp(buffer, "{NETNAME} entered the game") == 0);

	BotFreeChatState(chat);
}

/*
=============
test_enter_chat_cooldown_blocks_repeated_messages
=============
*/
static void test_enter_chat_cooldown_blocks_repeated_messages(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/match.c", "match"));

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 2, 5.0);
	BotChat_SetTime(chat, 1.0);
	BotEnterChat(chat, 0, 0);

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 2);
	assert(strcmp(buffer, "{NETNAME} entered the game") == 0);

	BotChat_SetTime(chat, 2.0);
	BotEnterChat(chat, 0, 0);

	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 2);
	assert(strcmp(buffer,
			"context 2 blocked by cooldown (4.00s remaining)\n") == 0);

	BotFreeChatState(chat);
}


/*
=============
test_reply_chat_logs_missing_contexts
=============
*/
static void test_reply_chat_logs_missing_contexts(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/rchat.c", "reply"));

	drain_console(chat);
	BotLib_TestResetLastMessage();

	assert(BotReplyChat(chat, "unit-test", 1));
	assert(!BotReplyChat(chat, "unit-test", 9999));
	assert(strcmp(BotLib_TestGetLastMessage(), "no rchats\n") == 0);
	assert(BotLib_TestGetLastMessageType() == PRT_MESSAGE);
	assert(BotNumConsoleMessages(chat) == 2);

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 1);
	assert(strlen(buffer) > 0);
	assert(strstr(buffer, "\\r") == NULL);

	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == PRT_MESSAGE);
	assert(strcmp(buffer, "no rchats\n") == 0);
	assert(!BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));

	BotFreeChatState(chat);
}

/*
=============
test_reply_chat_construct_message_paths
=============
*/
static void test_reply_chat_construct_message_paths(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/unit_test_chat.c", "unit_reply"));

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 9100, 1.0);
	BotChat_SetTime(chat, 1.0);
	assert(BotReplyChat(chat, "unit-test-reply-valid", 9100));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 9100);
	assert(strcmp(buffer, "Unit test reply constructed successfully") == 0);

	assert(!BotReplyChat(chat, "unit-test-reply-valid", 9100));
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 9100);
	assert(strstr(buffer, "blocked by cooldown") != NULL);

	drain_console(chat);
	BotChat_SetTime(chat, 3.0);
	assert(BotReplyChat(chat, "unit-test-reply-valid", 9100));
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 9100);
	assert(strcmp(buffer, "Unit test reply constructed successfully") == 0);

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 9101, 0.5);
	BotChat_SetTime(chat, 4.0);
	BotLib_TestResetLastMessage();
	assert(!BotReplyChat(chat, "unit-test-reply-invalid", 9101));
	assert(BotNumConsoleMessages(chat) == 0);
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(), "too long") != NULL);

	assert(!BotReplyChat(chat, "unit-test-reply-invalid", 9101));
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 9101);
	assert(strstr(buffer, "blocked by cooldown") != NULL);

	drain_console(chat);
	BotChat_SetTime(chat, 5.0);
	assert(!BotReplyChat(chat, "unit-test-reply-invalid", 9101));
	assert(BotNumConsoleMessages(chat) == 0);

	BotFreeChatState(chat);
}

/*
=============
test_reply_chat_known_random_string_context_enqueues_message
=============
*/
static void test_reply_chat_known_random_string_context_enqueues_message(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/unit_test_chat.c", "unit_random_valid"));

	drain_console(chat);
	assert(BotReplyChat(chat, "unit-test-random-valid", 9200));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 9200);
	assert(strncmp(buffer,
		"Random string placeholder: ",
		strlen("Random string placeholder: ")) == 0);
	assert(buffer[strlen(buffer) - 1] == '.');
	assert(strstr(buffer, "\\r") == NULL);

	BotFreeChatState(chat);
}

/*
=============
test_reply_chat_expands_named_random_table
=============
*/
static void test_reply_chat_expands_named_random_table(void)
{
	const char *path = "bot_chat_random_table_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"custom = {\"alpha\"}\n"
		"[\"unit\"] = 9300\n"
		"{\n"
		"\"Pick \", custom;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "random_table"));
	drain_console(chat);
	assert(BotReplyChat(chat, "unit", 9300));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 9300);
	assert(strcmp(buffer, "Pick alpha") == 0);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_expands_nested_random_table

Ensures constructor random expansion repeats when a selected entry contains
another random reference.
=============
*/
static void test_reply_chat_expands_nested_random_table(void)
{
	const char *path = "bot_chat_nested_random_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"outer = {\"\\\\rinner\\\\\"}\n"
		"inner = {\"omega\"}\n"
		"[\"nested\"] = 9301\n"
		"{\n"
		"\"Nested \", outer;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "nested_random"));
	drain_console(chat);
	assert(BotReplyChat(chat, "nested", 9301));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 9301);
	assert(strcmp(buffer, "Nested omega") == 0);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_initial_chat_applies_weighted_synonym_context

Mirrors the Q3 BotConstructChatMessage post-expansion synonym pass for initial
chat messages that receive a CONTEXT_* mask from the game wrapper.
=============
*/
static void test_initial_chat_applies_weighted_synonym_context(void)
{
	const char *path = "bot_chat_weighted_synonym_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"CONTEXT_NORMAL\n"
		"{\n"
		"[\n"
		"(\"frag\", 1.0),\n"
		"(\"zap\", 1.0)\n"
		"]\n"
		"}\n"
		"chat \"weighted\"\n"
		"{\n"
		"type \"synonym\"\n"
		"{\n"
		"\"frag and zap\";\n"
		"}\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "weighted"));

	drain_console(chat);
	assert(BotInitialChat(chat, "synonym", 0, NULL));
	int type = -1;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 0);
	assert(strcmp(buffer, "frag and zap") == 0);

	drain_console(chat);
	assert(BotInitialChat(chat, "synonym", 1, NULL));
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 1);
	assert(strcmp(buffer, "frag and frag") == 0
		|| strcmp(buffer, "zap and zap") == 0);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_captures_key_variable

Ensures parenthesized reply keys capture text into numeric response variables.
=============
*/
static void test_reply_chat_captures_key_variable(void)
{
	const char *path = "bot_chat_reply_capture_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[(\"i am \", 0)] = 9400\n"
		"{\n"
		"\"you are \", 0;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "reply_capture"));
	drain_console(chat);
	assert(BotReplyChat(chat, "I am testing captures", 9400));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 9400);
	assert(strcmp(buffer, "you are testing captures") == 0);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_captures_match_template_variables

Checks that readable match-template placeholders are captured from incoming
messages and substituted during construction.
=============
*/
static void test_reply_chat_captures_match_template_variables(void)
{
	const char *path = "bot_chat_match_capture_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"#include \"match.h\"\n"
		"MTCONTEXT_CLIENTOBITUARY\n"
		"{\n"
		"VICTIM, \" was railed by \", KILLER = (MSG_DEATH, ST_DEATH_RAILGUN);\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "match_capture"));
	drain_console(chat);
	assert(BotReplyChat(chat, "Alice was railed by Bob", 1));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 1);
	assert(strcmp(buffer, "Alice was railed by Bob") == 0);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_unknown_random_string_context_logs_error
=============
*/
static void test_reply_chat_unknown_random_string_context_logs_error(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/unit_test_chat.c", "unit_random_invalid"));

	drain_console(chat);
	BotLib_TestResetLastMessage();
	assert(!BotReplyChat(chat, "unit-test-random-invalid", 9201));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
	"unknown random string unit_test_missing") != NULL);
	assert(BotNumConsoleMessages(chat) == 0);

	BotFreeChatState(chat);
}

/*
=============
test_synonym_lookup_contains_nearbyitem_entries
=============
*/
static void test_synonym_lookup_contains_nearbyitem_entries(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/rchat.c", "reply"));

	assert(BotChat_HasSynonymPhrase(chat, "CONTEXT_NEARBYITEM", "Quad Damage"));
	assert(BotChat_HasSynonymPhrase(chat, "CONTEXT_NEARBYITEM",
				"Rocket Launcher"));

	BotFreeChatState(chat);
}

/*
=============
test_known_template_is_registered
=============
*/
static void test_known_template_is_registered(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/rchat.c", "reply"));

	assert(BotChat_HasReplyTemplate(chat, 1, "{VICTIM} commits suicide"));

	BotFreeChatState(chat);
}

/*
=============
test_include_path_too_long_is_rejected
=============
*/
static void test_include_path_too_long_is_rejected(void) {
	const size_t segment_length = 256;
	const size_t segment_count = 5;
	const size_t fragment_length =
		segment_count * segment_length + (segment_count - 1);

	char include_fragment[fragment_length + 1];
	size_t offset = 0;
	for (size_t segment = 0; segment < segment_count; ++segment) {
		if (segment > 0) {
			include_fragment[offset++] = '/';
		}
		memset(include_fragment + offset, (int)('a' + (int)segment),
			   segment_length);
		offset += segment_length;
	}
	include_fragment[offset] = '\0';

	char script[sizeof(include_fragment) + 16];
	int written =
		snprintf(script, sizeof(script), "#include <%s>\n", include_fragment);
	assert(written > 0);
	assert((size_t)written < sizeof(script));

	pc_source_t *source =
		PC_LoadSourceMemory("unit-test", script, (size_t)written);
	assert(source != NULL);

	pc_token_t token;
	assert(!PC_ReadToken(source, &token));

	const pc_diagnostic_t *diagnostic = PC_GetDiagnostics(source);
	bool found_error = false;
	while (diagnostic != NULL) {
		if (diagnostic->level == PC_ERROR_LEVEL_ERROR &&
			diagnostic->message != NULL &&
			strstr(diagnostic->message, "path too long") != NULL) {
			found_error = true;
			break;
		}
		diagnostic = diagnostic->next;
	}
	assert(found_error);

	PC_FreeSource(source);
}

/*
=============
test_botloadchatfile_fastchat_nochat_combinations

Ensures the mocked libvars gate chat loading and diagnostics correctly.
=============
*/
static void test_botloadchatfile_fastchat_nochat_combinations(void) {
const char *expected_message =
"couldn't load chat reply from " BOT_ASSET_ROOT "/rchat.c\n";
bot_chatstate_t *chat = BotAllocChatState();
assert(chat != NULL);

	configure_chat_libvars(0.0f, 0.0f);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/rchat.c", "reply"));
	drain_console(chat);

	configure_chat_libvars(0.0f, 1.0f);
	BotLib_TestResetLastMessage();
	assert(!BotLoadChatFile(chat, BOT_ASSET_ROOT "/rchat.c", "reply"));
	assert(BotLib_TestGetLastMessageType() == PRT_FATAL);
	assert(strcmp(BotLib_TestGetLastMessage(), expected_message) == 0);
	assert(BotNumConsoleMessages(chat) == 0);

	configure_chat_libvars(1.0f, 1.0f);
	BotLib_TestResetLastMessage();
	drain_console(chat);
	assert(!BotLoadChatFile(chat, BOT_ASSET_ROOT "/rchat.c", "reply"));
	assert(BotLib_TestGetLastMessageType() == PRT_FATAL);
	assert(strcmp(BotLib_TestGetLastMessage(), expected_message) == 0);
	assert(BotNumConsoleMessages(chat) == 1);
	assert_console_contains_message(chat, PRT_FATAL, expected_message);

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == PRT_FATAL);
	assert(strcmp(buffer, expected_message) == 0);
	assert(!BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));

	configure_chat_libvars(0.0f, 0.0f);
	BotFreeChatState(chat);
}

/*
=============
test_botloadchatfile_reports_missing_chat_context

Forces the script wrapper creation to fail so the legacy "couldn't find chat"
diagnostic is enqueued when fastchat is enabled.
=============
*/
static void test_botloadchatfile_reports_missing_chat_context(void)
{
char expected_message[256];
const int written = snprintf(expected_message,
sizeof(expected_message),
		"couldn't find chat %s in %s\n",
		"reply",
		BOT_ASSET_ROOT "/rchat.c");
	assert(written > 0);
	assert((size_t)written < sizeof(expected_message));

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	configure_chat_libvars(1.0f, 0.0f);
	drain_console(chat);
	BotLib_TestResetLastMessage();
	PS_TestForceCreateFailure(1);
	assert(!BotLoadChatFile(chat, BOT_ASSET_ROOT "/rchat.c", "reply"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strcmp(BotLib_TestGetLastMessage(), expected_message) == 0);
	assert_console_contains_message(chat, PRT_ERROR, expected_message);
	drain_console(chat);

configure_chat_libvars(0.0f, 0.0f);
BotFreeChatState(chat);
}

/*
=============
test_setup_chat_ai_loads_default_assets

Exercises the retail setup/shutdown exports against the default chat asset
libvars.
=============
*/
static void test_setup_chat_ai_loads_default_assets(void)
{
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	assert(BotSetupChatAI() == 0);
	assert(BotSetupChatAI() == 0);
	BotShutdownChatAI();
	BotShutdownChatAI();
}

/*
=============
test_setup_chat_ai_skips_reply_when_nochat_enabled

Mirrors the HLIL branch that still loads shared synonym, random, and match
assets but does not read rchatfile when nochat is non-zero.
=============
*/
static void test_setup_chat_ai_skips_reply_when_nochat_enabled(void)
{
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 1.0f);
	BotLib_TestSetLibVarString("rchatfile", "definitely_missing_rchat.c");
	BotLib_TestResetLastMessage();
	assert(BotSetupChatAI() == 0);
	assert(strstr(BotLib_TestGetLastMessage(), "definitely_missing_rchat.c") == NULL);
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
}

/*
=============
test_setup_chat_ai_supplies_shared_reply_fallback

Confirms setup-loaded reply chats remain available to personality chat states
that only load initial-chat blocks.
=============
*/
static void test_setup_chat_ai_supplies_shared_reply_fallback(void)
{
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	assert(BotSetupChatAI() == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe"));

	drain_console(chat);
	assert(BotReplyChat(chat, "abnormal", 5));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 5);
	assert(BotChat_HasReplyTemplate(chat, 5, buffer));

	BotFreeChatState(chat);
	BotShutdownChatAI();
}

/*
=============
test_enter_chat_sends_command_via_bridge

Verifies BotEnterChat formats the say command and forwards it through the
bridge import table.
=============
*/
static void test_enter_chat_sends_command_via_bridge(void)
{
	ChatBridge_Reset();
	BotLib_TestSetMaxClients(8.0f);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/unit_test_chat.c", "unit_enter_valid"));

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 2, 0.0);
	BotEnterChat(chat, 2, 0);

	assert(g_chat_bridge_mock.command_calls == 1);
	assert(g_chat_bridge_mock.last_client == 2);
	assert(strcmp(g_chat_bridge_mock.last_command,
		"say {NETNAME} triggered the deterministic join message") == 0);

	Q2Bridge_ClearImportTable();
	BotFreeChatState(chat);
}

/*
=============
test_enter_chat_team_command_uses_say_team

Ensures team sendto values use say_team when dispatching chat text.
=============
*/
static void test_enter_chat_team_command_uses_say_team(void)
{
	ChatBridge_Reset();
	BotLib_TestSetMaxClients(8.0f);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/unit_test_chat.c", "unit_enter_valid"));

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 2, 0.0);
	BotEnterChat(chat, 3, 1);

	assert(g_chat_bridge_mock.command_calls == 1);
	assert(g_chat_bridge_mock.last_client == 3);
	assert(strcmp(g_chat_bridge_mock.last_command,
		"say_team {NETNAME} triggered the deterministic join message") == 0);

	Q2Bridge_ClearImportTable();
	BotFreeChatState(chat);
}

/*
=============
test_reply_chat_dispatches_using_bridge_speaker

Checks reply construction is forwarded to the bridge using the active speaking
client binding.
=============
*/
static void test_reply_chat_dispatches_using_bridge_speaker(void)
{
	ChatBridge_Reset();
	BotLib_TestSetMaxClients(8.0f);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/match_reply.c", "match_reply"));

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 2, 0.0);

	BotEnterChat(chat, 4, 0);
	ChatBridge_Reset();
	drain_console(chat);

	assert(BotReplyChat(chat, "Quad Damage acquired", 2));
	assert(g_chat_bridge_mock.command_calls == 1);
	assert(g_chat_bridge_mock.last_client == 4);
	assert(strcmp(g_chat_bridge_mock.last_command, "say NEARBYITEM acquired") == 0);

	Q2Bridge_ClearImportTable();
	BotFreeChatState(chat);
}

/*
=============
main
=============
*/
int main(void) {
	configure_crt_reports();
	configure_chat_libvars(0.0f, 0.0f);
	test_include_path_too_long_is_rejected();
	test_enter_chat_uses_unit_test_template();
	test_retail_initial_chat_block_drives_enter_event();
	test_retail_initial_chat_counts_raw_type_buckets();
	test_retail_initial_chat_constructs_from_alias();
	test_initial_chat_pending_message_exports_and_enters();
	test_get_chat_message_copies_and_clears_pending();
	test_retail_initial_chat_missing_name_is_rejected();
	test_enter_chat_construct_message_failure_respects_cooldown_reset();
	test_reply_chat_death_context();
	test_reply_chat_falls_back_to_reply_table();
	test_reply_chat_name_key_matches_configured_name();
	test_reply_chat_construct_message_paths();
	test_reply_chat_known_random_string_context_enqueues_message();
	test_reply_chat_expands_named_random_table();
	test_reply_chat_expands_nested_random_table();
	test_initial_chat_applies_weighted_synonym_context();
	test_reply_chat_captures_key_variable();
	test_reply_chat_captures_match_template_variables();
	test_reply_chat_unknown_random_string_context_logs_error();
	test_synonym_lookup_contains_nearbyitem_entries();
	test_known_template_is_registered();
	test_include_path_too_long_is_rejected();
	test_enter_chat_enqueues_message();
	test_enter_chat_cooldown_blocks_repeated_messages();
	test_reply_chat_logs_missing_contexts();
	test_botloadchatfile_fastchat_nochat_combinations();
	test_botloadchatfile_reports_missing_chat_context();
	test_setup_chat_ai_loads_default_assets();
	test_setup_chat_ai_skips_reply_when_nochat_enabled();
	test_setup_chat_ai_supplies_shared_reply_fallback();
	test_enter_chat_sends_command_via_bridge();
	test_enter_chat_team_command_uses_say_team();
	test_reply_chat_dispatches_using_bridge_speaker();

	printf("bot_chat_tests: all checks passed\n");
	return 0;
}

