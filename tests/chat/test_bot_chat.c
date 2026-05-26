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
test_pending_chat_handoff_removes_tildes

Confirms BotGetChatMessage and BotEnterChat strip retail tilde markers at the
same public handoff points as Q3.
=============
*/
static void test_pending_chat_handoff_removes_tildes(void)
{
	const char *path = "bot_chat_tilde_handoff_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"chat \"tilde\"\n"
		"{\n"
		"type \"line\"\n"
		"{\n"
		"\"hello~there\";\n"
		"}\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "tilde"));

	drain_console(chat);
	assert(BotInitialChat(chat, "line", 0, NULL));
	char buffer[256];
	BotGetChatMessage(chat, buffer, sizeof(buffer));
	assert(strcmp(buffer, "hellothere") == 0);
	assert(BotChatLength(chat) == 0);

	drain_console(chat);
	assert(BotInitialChat(chat, "line", 0, NULL));
	ChatBridge_Reset();
	BotLib_TestSetMaxClients(4.0f);
	BotEnterChat(chat, 2, 0);
	assert(g_chat_bridge_mock.command_calls == 1);
	assert(g_chat_bridge_mock.last_client == 2);
	assert(strcmp(g_chat_bridge_mock.last_command, "say hellothere") == 0);

	Q2Bridge_ClearImportTable();
	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_initial_chat_recent_lines_rotate

Pins retail recent-message timestamps for initial-chat type buckets.
=============
*/
static void test_initial_chat_recent_lines_rotate(void)
{
	const char *path = "bot_chat_recent_initial_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"chat \"recent\"\n"
		"{\n"
		"type \"line\"\n"
		"{\n"
		"\"first line\";\n"
		"\"second line\";\n"
		"}\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "recent"));

	BotChat_SetTime(chat, 100.0);
	assert(BotInitialChat(chat, "line", 0, NULL));
	char first[256];
	BotGetChatMessage(chat, first, sizeof(first));
	assert(first[0] != '\0');

	assert(BotInitialChat(chat, "line", 0, NULL));
	char second[256];
	BotGetChatMessage(chat, second, sizeof(second));
	assert(second[0] != '\0');
	assert(strcmp(first, second) != 0);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_initial_chat_rejects_float_message_component

Pins Q3 BotLoadChatMessage behavior: numeric message components are variable
references only when the lexer classified them as integers.
=============
*/
static void test_initial_chat_rejects_float_message_component(void)
{
	const char *path = "bot_chat_float_initial_component_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"chat \"float_initial\"\n"
		"{\n"
		"type \"line\"\n"
		"{\n"
		"\"bad \", 1.5;\n"
		"}\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "float_initial"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't find chat float_initial in bot_chat_float_initial_component_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_initial_chat_rejects_missing_message_component_comma

Pins Q3 BotLoadChatMessage delimiter handling for named initial chat blocks:
each component must be followed by a comma or semicolon.
=============
*/
static void test_initial_chat_rejects_missing_message_component_comma(void)
{
	const char *path = "bot_chat_missing_initial_component_comma_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"chat \"missing_initial_comma\"\n"
		"{\n"
		"type \"line\"\n"
		"{\n"
		"\"bad \" 0;\n"
		"}\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "missing_initial_comma"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't find chat missing_initial_comma in "
		"bot_chat_missing_initial_component_comma_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_rejects_float_message_component

Matches Q3 reply loading by rejecting float tokens inside response message
templates instead of truncating them to variable indices.
=============
*/
static void test_reply_chat_rejects_float_message_component(void)
{
	const char *path = "bot_chat_float_reply_component_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"float\"] = 16\n"
		"{\n"
		"\"bad \", 1.5;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "float_reply"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't load chat float_reply from bot_chat_float_reply_component_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_rejects_missing_message_component_comma

Matches Q3 reply loading by requiring commas between response message
components before the terminating semicolon.
=============
*/
static void test_reply_chat_rejects_missing_message_component_comma(void)
{
	const char *path = "bot_chat_missing_reply_component_comma_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"missing\"] = 16\n"
		"{\n"
		"\"bad \" 0;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "missing_reply_component_comma"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't load chat missing_reply_component_comma from "
		"bot_chat_missing_reply_component_comma_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_rejects_float_pattern_variable

Keeps parenthesized reply-key match variables on the same integer-only path as
Q3 BotLoadMatchPieces.
=============
*/
static void test_reply_chat_rejects_float_pattern_variable(void)
{
	const char *path = "bot_chat_float_reply_key_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[(\"value \", 1.5)] = 16\n"
		"{\n"
		"\"bad\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "float_reply_key"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't load chat float_reply_key from bot_chat_float_reply_key_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_match_template_rejects_float_variable

Confirms match.c-style templates also reject non-integer numeric variable
pieces like Q3 BotLoadMatchPieces.
=============
*/
static void test_match_template_rejects_float_variable(void)
{
	const char *path = "bot_chat_float_match_template_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"#include \"match.h\"\n"
		"MTCONTEXT_CLIENTOBITUARY\n"
		"{\n"
		"\"hello \", 1.5 = (MSG_DEATH);\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "float_match"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't find chat float_match in bot_chat_float_match_template_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_match_template_rejects_float_message_type

Pins Q3 BotLoadMatchTemplates metadata parsing, where match message types are
integer tokens after macro expansion.
=============
*/
static void test_match_template_rejects_float_message_type(void)
{
	const char *path = "bot_chat_float_match_type_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"#include \"match.h\"\n"
		"MTCONTEXT_CLIENTOBITUARY\n"
		"{\n"
		"\"hello\" = (1.5, ST_DEATH_RAILGUN);\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "float_match_type"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't find chat float_match_type in bot_chat_float_match_type_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_match_template_rejects_float_message_subtype

Pins Q3 BotLoadMatchTemplates metadata parsing, where optional match subtypes
are also integer tokens.
=============
*/
static void test_match_template_rejects_float_message_subtype(void)
{
	const char *path = "bot_chat_float_match_subtype_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"#include \"match.h\"\n"
		"MTCONTEXT_CLIENTOBITUARY\n"
		"{\n"
		"\"hello\" = (MSG_DEATH, 11.5);\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "float_match_subtype"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't find chat float_match_subtype in bot_chat_float_match_subtype_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_match_template_rejects_float_context_label

Ensures preprocessor-expanded numeric match context blocks remain integer-only
like Q3 BotLoadMatchTemplates.
=============
*/
static void test_match_template_rejects_float_context_label(void)
{
	const char *path = "bot_chat_float_match_context_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"#include \"match.h\"\n"
		"1.5\n"
		"{\n"
		"\"hello\" = (MSG_DEATH, ST_DEATH_RAILGUN);\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "float_match_context"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't find chat float_match_context in bot_chat_float_match_context_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_match_template_rejects_out_of_range_variable

Pins Q3 BotLoadMatchPieces range validation for numeric match variables.
=============
*/
static void test_match_template_rejects_out_of_range_variable(void)
{
	const char *path = "bot_chat_range_match_variable_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"#include \"match.h\"\n"
		"MTCONTEXT_CLIENTOBITUARY\n"
		"{\n"
		"\"hello \", 99 = (MSG_DEATH, ST_DEATH_RAILGUN);\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "range_match_variable"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't find chat range_match_variable in bot_chat_range_match_variable_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_match_template_rejects_adjacent_variables

Mirrors Q3 BotLoadMatchPieces rejecting variable pieces with no non-empty string
between them.
=============
*/
static void test_match_template_rejects_adjacent_variables(void)
{
	const char *path = "bot_chat_adjacent_match_variable_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"#include \"match.h\"\n"
		"MTCONTEXT_CLIENTOBITUARY\n"
		"{\n"
		"0, 1 = (MSG_DEATH, ST_DEATH_RAILGUN);\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "adjacent_match_variable"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't find chat adjacent_match_variable in bot_chat_adjacent_match_variable_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_match_template_rejects_missing_piece_comma

Pins Q3 BotLoadMatchPieces delimiter handling for setup-loaded match.c
templates: each match piece must be followed by a comma or '='.
=============
*/
static void test_match_template_rejects_missing_piece_comma(void)
{
	const char *path = "bot_chat_missing_match_piece_comma_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"#include \"match.h\"\n"
		"MTCONTEXT_CLIENTOBITUARY\n"
		"{\n"
		"0 \" was railed by \", 1 = (MSG_DEATH, ST_DEATH_RAILGUN);\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "missing_match_piece_comma"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't find chat missing_match_piece_comma in "
		"bot_chat_missing_match_piece_comma_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_rejects_out_of_range_pattern_variable

Ensures reply-key match pieces reject variable indices outside the capture
table during load.
=============
*/
static void test_reply_chat_rejects_out_of_range_pattern_variable(void)
{
	const char *path = "bot_chat_range_reply_key_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[(\"value \", 99)] = 16\n"
		"{\n"
		"\"bad\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "range_reply_key"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't load chat range_reply_key from bot_chat_range_reply_key_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_rejects_adjacent_pattern_variables

Pins Q3 BotLoadMatchPieces adjacency validation for parenthesized reply keys.
=============
*/
static void test_reply_chat_rejects_adjacent_pattern_variables(void)
{
	const char *path = "bot_chat_adjacent_reply_key_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[(0, 1)] = 16\n"
		"{\n"
		"\"bad\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "adjacent_reply_key"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't load chat adjacent_reply_key from bot_chat_adjacent_reply_key_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_rejects_missing_pattern_piece_comma

Pins Q3 BotLoadMatchPieces delimiter handling for parenthesized reply keys.
=============
*/
static void test_reply_chat_rejects_missing_pattern_piece_comma(void)
{
	const char *path = "bot_chat_missing_reply_pattern_comma_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[(0 \" tail\")] = 16\n"
		"{\n"
		"\"bad\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "missing_reply_pattern_comma"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't load chat missing_reply_pattern_comma from "
		"bot_chat_missing_reply_pattern_comma_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_rejects_empty_pattern_key

Mirrors Q3 BotLoadMatchPieces rejecting an immediately closed parenthesized
reply key.
=============
*/
static void test_reply_chat_rejects_empty_pattern_key(void)
{
	const char *path = "bot_chat_empty_reply_pattern_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[()] = 16\n"
		"{\n"
		"\"bad\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "empty_reply_pattern"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't load chat empty_reply_pattern from "
		"bot_chat_empty_reply_pattern_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_rejects_empty_alternative_between_variables

Pins Q3's empty-string match-piece guard: an optional string still leaves the
following variable adjacent to the previous variable.
=============
*/
static void test_reply_chat_rejects_empty_alternative_between_variables(void)
{
	const char *path = "bot_chat_empty_alt_adjacent_reply_key_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[(0, \"\" | \" and \", 1)] = 9411\n"
		"{\n"
		"\"bad\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "empty_alt_adjacent_reply_key"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't load chat empty_alt_adjacent_reply_key from "
		"bot_chat_empty_alt_adjacent_reply_key_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_rejects_empty_key_list

Mirrors Q3 BotLoadReplyChat requiring at least one key between '[' and ']'.
=============
*/
static void test_reply_chat_rejects_empty_key_list(void)
{
	const char *path = "bot_chat_empty_reply_key_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[] = 16\n"
		"{\n"
		"\"bad\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "empty_reply_key"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't load chat empty_reply_key from bot_chat_empty_reply_key_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_accepts_missing_key_commas

Pins Q3 BotLoadReplyChat's permissive key-list separator handling, where the
comma after each key is optional.
=============
*/
static void test_reply_chat_accepts_missing_key_commas(void)
{
	const char *path = "bot_chat_missing_reply_key_comma_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[!\"hello\" name] = 16\n"
		"{\n"
		"\"missing comma accepted\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "missing_reply_key_comma"));

	drain_console(chat);
	BotSetChatName(chat, "world", 0);
	assert(BotReplyChat(chat, "world", 16));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 16);
	assert(strcmp(buffer, "missing comma accepted") == 0);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_rejects_unquoted_plain_key

Matches Q3 BotLoadReplyChat's normal-key branch, which expects a string token
after the special-key checks.
=============
*/
static void test_reply_chat_rejects_unquoted_plain_key(void)
{
	const char *path = "bot_chat_unquoted_plain_reply_key_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[plain] = 16\n"
		"{\n"
		"\"bad\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "unquoted_plain_reply_key"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't load chat unquoted_plain_reply_key from bot_chat_unquoted_plain_reply_key_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_rejects_unquoted_botname_list_entry

Keeps Q3's '<...>' reply-key form string-token only instead of accepting bare
names inside the bot-name list.
=============
*/
static void test_reply_chat_rejects_unquoted_botname_list_entry(void)
{
	const char *path = "bot_chat_unquoted_botname_reply_key_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[<babe>] = 16\n"
		"{\n"
		"\"bad\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestResetLastMessage();
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert(!BotLoadChatFile(chat, path, "unquoted_botname_reply_key"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"couldn't load chat unquoted_botname_reply_key from bot_chat_unquoted_botname_reply_key_test.c\n")
		!= NULL);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_initial_chat_test_libvar_prints_and_suppresses_dispatch

Mirrors Q3's bot_testichat behavior: count probes print diagnostics and
BotEnterChat prints the pending text instead of sending a chat command.
=============
*/
static void test_initial_chat_test_libvar_prints_and_suppresses_dispatch(void)
{
	const char *path = "bot_chat_testichat_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"chat \"testichat\"\n"
		"{\n"
		"type \"line\"\n"
		"{\n"
		"\"~debug line~\";\n"
		"}\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "testichat"));

	BotLib_TestSetLibVar("bot_testichat", 1.0f);
	drain_console(chat);
	BotLib_TestResetLastMessage();

	assert(BotNumInitialChats(chat, "line") == 1);
	assert_console_contains_message(chat,
		PRT_MESSAGE,
		"line has 1 chat lines\n");
	assert_console_contains_message(chat,
		PRT_MESSAGE,
		"-------------------\n");

	assert(BotInitialChat(chat, "line", 0, NULL));
	drain_console(chat);
	ChatBridge_Reset();
	BotLib_TestResetLastMessage();
	BotEnterChat(chat, 2, 0);

	assert(g_chat_bridge_mock.command_calls == 0);
	assert(strcmp(BotLib_TestGetLastMessage(), "debug line\n") == 0);
	assert(BotChatLength(chat) == 0);

	Q2Bridge_ClearImportTable();
	BotLib_TestSetLibVar("bot_testichat", 0.0f);
	BotFreeChatState(chat);
	remove(path);
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
	const char *path = "bot_chat_reply_name_key_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"name\"] = 3\n"
		"{\n"
		"\"literal name\";\n"
		"}\n"
		"[name] = 7\n"
		"{\n"
		"\"configured name\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "name_key"));

	BotSetChatName(chat, "babe", 3);
	drain_console(chat);
	assert(BotReplyChat(chat, "babe", 16));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 16);
	assert(strcmp(buffer, "configured name") == 0);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_chooses_highest_priority_match

Mirrors retail reply-chat selection where the number after the key list is a
priority and the highest-priority matching rule wins.
=============
*/
static void test_reply_chat_chooses_highest_priority_match(void)
{
	const char *path = "bot_chat_reply_priority_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"priority\"] = 1\n"
		"{\n"
		"\"low priority\";\n"
		"}\n"
		"[\"priority\", \"specific\"] = 9\n"
		"{\n"
		"\"high priority\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "reply_priority"));

	drain_console(chat);
	assert(BotReplyChat(chat, "priority specific", 16));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 16);
	assert(strcmp(buffer, "high priority") == 0);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_recent_responses_rotate

Checks reply-chat response timestamps avoid immediate repeats when alternatives
are available.
=============
*/
static void test_reply_chat_recent_responses_rotate(void)
{
	const char *path = "bot_chat_reply_recent_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"recent\"] = 3\n"
		"{\n"
		"\"first reply\";\n"
		"\"second reply\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "reply_recent"));

	BotChat_SetTime(chat, 200.0);
	drain_console(chat);
	assert(BotReplyChat(chat, "recent", 16));

	int type = 0;
	char first[256];
	assert(BotNextConsoleMessage(chat, &type, first, sizeof(first)));
	assert(type == 16);
	assert(first[0] != '\0');

	assert(BotReplyChat(chat, "recent", 16));
	char second[256];
	assert(BotNextConsoleMessage(chat, &type, second, sizeof(second)));
	assert(type == 16);
	assert(second[0] != '\0');
	assert(strcmp(first, second) != 0);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_matches_botname_list_and_gender_keys

Exercises the Q3/HLIL special reply keys for <bot name> lists and chat gender
metadata.
=============
*/
static void test_reply_chat_matches_botname_list_and_gender_keys(void)
{
	const char *path = "bot_chat_reply_special_keys_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[<\"babe\", \"major\">] = 5\n"
		"{\n"
		"\"listed bot name\";\n"
		"}\n"
		"[female] = 4\n"
		"{\n"
		"\"female reply\";\n"
		"}\n"
		"[male] = 4\n"
		"{\n"
		"\"male reply\";\n"
		"}\n"
		"[it] = 4\n"
		"{\n"
		"\"genderless reply\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "special_keys"));

	drain_console(chat);
	BotSetChatName(chat, "major", 2);
	assert(BotReplyChat(chat, "anything", 16));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 16);
	assert(strcmp(buffer, "listed bot name") == 0);

	drain_console(chat);
	BotSetChatName(chat, "visor", 2);
	BotSetChatGender(chat, CHAT_GENDERFEMALE);
	assert(BotReplyChat(chat, "anything", 16));
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 16);
	assert(strcmp(buffer, "female reply") == 0);

	drain_console(chat);
	BotSetChatGender(chat, CHAT_GENDERMALE);
	assert(BotReplyChat(chat, "anything", 16));
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 16);
	assert(strcmp(buffer, "male reply") == 0);

	drain_console(chat);
	BotSetChatGender(chat, CHAT_GENDERLESS);
	assert(BotReplyChat(chat, "anything", 16));
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 16);
	assert(strcmp(buffer, "genderless reply") == 0);

	BotFreeChatState(chat);
	remove(path);
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
test_reply_chat_unmatched_rule_returns_false_quietly
=============
*/
static void test_reply_chat_unmatched_rule_returns_false_quietly(void) {
	const char *path = "bot_chat_missing_reply_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"known\"] = 3\n"
		"{\n"
		"\"known reply\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "missing_reply"));

	drain_console(chat);
	BotLib_TestResetLastMessage();

	assert(BotReplyChat(chat, "known", 1));
	assert(!BotReplyChat(chat, "unknown", 9999));
	assert(strcmp(BotLib_TestGetLastMessage(), "") == 0);
	assert(BotLib_TestGetLastMessageType() == 0);
	assert(BotNumConsoleMessages(chat) == 1);

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 1);
	assert(strcmp(buffer, "known reply") == 0);
	assert(BotChat_HasReplyTemplate(chat, 3, buffer));
	assert(strstr(buffer, "\\r") == NULL);

	assert(!BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_string_key_uses_q3_word_separators

Pins Q3 StringContainsWord behavior for plain reply keys. Only spaces, periods,
commas, and exclamation marks terminate reply words.
=============
*/
static void test_reply_chat_string_key_uses_q3_word_separators(void)
{
	const char *path = "bot_chat_reply_word_separator_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"hello\"] = 1\n"
		"{\n"
		"\"matched hello\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "word_separator"));

	drain_console(chat);
	BotLib_TestResetLastMessage();
	assert(!BotReplyChat(chat, "hello?", 1));
	assert(!BotReplyChat(chat, "hello-there", 1));
	assert(BotNumConsoleMessages(chat) == 0);
	assert(strcmp(BotLib_TestGetLastMessage(), "") == 0);

	assert(BotReplyChat(chat, "well, hello!", 1));
	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 1);
	assert(strcmp(buffer, "matched hello") == 0);
	assert(!BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));

	BotFreeChatState(chat);
	remove(path);
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
	assert(BotChat_HasReplyTemplate(chat, 9300, "Pick alpha"));
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
test_initial_chat_weighted_synonym_runs_each_expansion_pass

Pins Q3 BotExpandChatMessage timing: weighted synonyms run at the end of each
random expansion pass, so a synonym result can feed the next random pass.
=============
*/
static void test_initial_chat_weighted_synonym_runs_each_expansion_pass(void)
{
	const char *path = "bot_chat_synonym_expansion_pass_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"CONTEXT_NORMAL\n"
		"{\n"
		"[\n"
		"(\"\\\\rinner\\\\\", 10000000000.0),\n"
		"(\"bridge\", 1.0)\n"
		"]\n"
		"}\n"
		"outer = {\"bridge\"}\n"
		"inner = {\"omega\"}\n"
		"chat \"synonym_expansion\"\n"
		"{\n"
		"type \"line\"\n"
		"{\n"
		"outer;\n"
		"}\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "synonym_expansion"));

	drain_console(chat);
	assert(BotInitialChat(chat, "line", 1, NULL));

	int type = -1;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 1);
	assert(strcmp(buffer, "omega") == 0);

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
test_reply_chat_captures_key_variable_after_string_alternative

Pins Q3 BotLoadMatchPieces string alternatives inside parenthesized reply keys.
=============
*/
static void test_reply_chat_captures_key_variable_after_string_alternative(void)
{
	const char *path = "bot_chat_reply_alternative_capture_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[(\"alert \" | \"warning \", 0)] = 9410\n"
		"{\n"
		"\"handled \", 0;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "reply_alternative_capture"));
	drain_console(chat);
	assert(!BotReplyChat(chat, "prefix warning Bravo", 9410));
	assert(BotReplyChat(chat, "WARNING Bravo", 9410));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 9410);
	assert(strcmp(buffer, "handled Bravo") == 0);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_empty_string_piece_closes_variable_capture

Ensures a single empty string match piece behaves like Q3's real string piece
instead of being dropped and turning the previous variable into a catch-all.
=============
*/
static void test_reply_chat_empty_string_piece_closes_variable_capture(void)
{
	const char *path = "bot_chat_reply_empty_string_piece_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[(0, \"\")] = 9412\n"
		"{\n"
		"\"empty\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "reply_empty_string_piece"));
	drain_console(chat);
	assert(!BotReplyChat(chat, "not empty", 9412));
	assert(BotReplyChat(chat, "", 9412));

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 9412);
	assert(strcmp(buffer, "empty") == 0);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_split_vcontext_canonicalizes_variables

Mirrors Q3 BotReplyChat's separate vcontext pass, where captured reply
variables are canonicalized through CONTEXT_REPLY synonyms before expansion.
=============
*/
static void test_reply_chat_split_vcontext_canonicalizes_variables(void)
{
	const char *path = "bot_chat_reply_vcontext_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"CONTEXT_REPLY\n"
		"{\n"
		"[\n"
		"(\"Quad Damage\", 1.0),\n"
		"(\"quad\", 1.0)\n"
		"]\n"
		"}\n"
		"[(\"i like \", 0)] = 0\n"
		"{\n"
		"\"you like \", 0;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "reply_vcontext"));

	drain_console(chat);
	assert(BotReplyChatWithContexts(chat,
		"I like quad",
		0,
		0,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL));
	int type = -1;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 0);
	assert(strcmp(buffer, "you like quad") == 0);

	drain_console(chat);
	assert(BotReplyChatWithContexts(chat,
		"I like quad",
		0,
		16,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL));
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 0);
	assert(strcmp(buffer, "you like Quad Damage") == 0);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_split_vcontext_uses_q3_word_separators

Pins Q3 BotReplaceReplySynonyms word boundaries: only spaces, periods, commas,
and exclamation marks terminate synonym words.
=============
*/
static void test_reply_chat_split_vcontext_uses_q3_word_separators(void)
{
	const char *path = "bot_chat_reply_vcontext_separator_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"CONTEXT_REPLY\n"
		"{\n"
		"[\n"
		"(\"Quad Damage\", 1.0),\n"
		"(\"quad\", 1.0)\n"
		"]\n"
		"}\n"
		"[(\"i like \", 0)] = 0\n"
		"{\n"
		"\"you like \", 0;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "reply_vcontext_separator"));

	drain_console(chat);
	assert(BotReplyChatWithContexts(chat,
		"I like quad?",
		0,
		16,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL));
	int type = -1;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 0);
	assert(strcmp(buffer, "you like quad?") == 0);

	drain_console(chat);
	assert(BotReplyChatWithContexts(chat,
		"I like quad!",
		0,
		16,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL));
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 0);
	assert(strcmp(buffer, "you like Quad Damage!") == 0);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_split_context_accepts_fixed_var_slots

Pins Q3's fixed var0-var7 reply arguments, including the game-side var6 and
var7 bot/player name slots used by reply chat wiring.
=============
*/
static void test_reply_chat_split_context_accepts_fixed_var_slots(void)
{
	const char *path = "bot_chat_reply_fixed_vars_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"hello\"] = 0\n"
		"{\n"
		"\"names \", 6, \" \", 7;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "reply_fixed_vars"));

	drain_console(chat);
	assert(BotReplyChatWithContexts(chat,
		"hello",
		0,
		0,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		"botname",
		"speaker"));

	int type = -1;
	char buffer[256];
	assert(BotNextConsoleMessage(chat, &type, buffer, sizeof(buffer)));
	assert(type == 0);
	assert(strcmp(buffer, "names botname speaker") == 0);

	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_test_libvar_dumps_responses_without_dispatch

Pins Q3 bot_testrchat behavior: the matching reply block is expanded and
printed line-by-line without sending a chat command.
=============
*/
static void test_reply_chat_test_libvar_dumps_responses_without_dispatch(void)
{
	const char *path = "bot_chat_testrchat_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"debug\"] = 0\n"
		"{\n"
		"\"~first reply~\";\n"
		"\"second reply\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "testrchat"));

	BotLib_TestSetLibVar("bot_testrchat", 1.0f);
	drain_console(chat);
	ChatBridge_Reset();
	BotLib_TestResetLastMessage();

	assert(BotReplyChatWithContexts(chat,
		"debug",
		0,
		0,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL));

	assert(g_chat_bridge_mock.command_calls == 0);
	assert_console_contains_message(chat, PRT_MESSAGE, "first reply\n");
	assert_console_contains_message(chat, PRT_MESSAGE, "second reply\n");
	assert(strcmp(BotLib_TestGetLastMessage(), "second reply\n") == 0);
	assert(BotChatLength(chat) > 0);

	Q2Bridge_ClearImportTable();
	BotLib_TestSetLibVar("bot_testrchat", 0.0f);
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
test_setup_chat_ai_exports_match_and_synonym_utilities

Verifies the Q3 chat utility exports are backed by setup-loaded match and
synonym data.
=============
*/
static void test_setup_chat_ai_exports_match_and_synonym_utilities(void)
{
	enum
	{
		MTCONTEXT_CLIENTOBITUARY_TEST = 1,
		CONTEXT_NORMAL_TEST = 1,
		MSG_DEATH_TEST = 1,
		ST_DEATH_RAILGUN_TEST = 11
	};

	char whitespace[] = "  alpha\t\tbeta\n gamma  ";
	UnifyWhiteSpaces(whitespace);
	assert(strcmp(whitespace, "alpha beta gamma") == 0);
	assert(StringContains("Alpha Beta", "beta", 0) == 6);
	assert(StringContains("Alpha Beta", "beta", 1) == -1);

	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	assert(BotSetupChatAI() == 0);

	char synonym_text[256] = "I can't stay";
	BotReplaceSynonyms(synonym_text, CONTEXT_NORMAL_TEST);
	assert(strcmp(synonym_text, "I can not stay") == 0);

	char synonym_question[256] = "I can't?";
	BotReplaceSynonyms(synonym_question, CONTEXT_NORMAL_TEST);
	assert(strcmp(synonym_question, "I can't?") == 0);

	bot_match_t match;
	assert(BotFindMatch("Alice was railed by Bob\n",
		&match,
		MTCONTEXT_CLIENTOBITUARY_TEST));
	assert(match.type == MSG_DEATH_TEST);
	assert(match.subtype == ST_DEATH_RAILGUN_TEST);

	char variable[256];
	BotMatchVariable(&match, 0, variable, sizeof(variable));
	assert(strcmp(variable, "Alice") == 0);
	BotMatchVariable(&match, 1, variable, sizeof(variable));
	assert(strcmp(variable, "Bob") == 0);
	BotMatchVariable(&match, 5, variable, sizeof(variable));
	assert(variable[0] == '\0');

	assert(!BotFindMatch("Alice was railed by Bob",
		&match,
		MTCONTEXT_CLIENTOBITUARY_TEST << 8));

	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
}

/*
=============
test_setup_chat_ai_match_string_alternatives_capture_variables

Exercises setup-loaded match.c string alternatives through BotFindMatch.
=============
*/
static void test_setup_chat_ai_match_string_alternatives_capture_variables(void)
{
	enum
	{
		MTCONTEXT_CLIENTOBITUARY_TEST = 1,
		MSG_DEATH_TEST = 1,
		ST_DEATH_TEST = 12
	};

	const char *path = "bot_chat_match_alternative_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"1\n"
		"{\n"
		"0, \" was railed by \" | \" got fried by \", 1 = (1, 12);\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 1.0f);
	BotLib_TestSetLibVarString("synfile", "definitely_missing_syn.c");
	BotLib_TestSetLibVarString("rndfile", "definitely_missing_rnd.c");
	BotLib_TestSetLibVarString("matchfile", path);
	assert(BotSetupChatAI() == 0);

	bot_match_t match;
	assert(BotFindMatch("Alice got fried by Bob\n",
		&match,
		MTCONTEXT_CLIENTOBITUARY_TEST));
	assert(match.type == MSG_DEATH_TEST);
	assert(match.subtype == ST_DEATH_TEST);

	char variable[256];
	BotMatchVariable(&match, 0, variable, sizeof(variable));
	assert(strcmp(variable, "Alice") == 0);
	BotMatchVariable(&match, 1, variable, sizeof(variable));
	assert(strcmp(variable, "Bob") == 0);

	assert(BotFindMatch("Alice was railed by Bob\n",
		&match,
		MTCONTEXT_CLIENTOBITUARY_TEST));
	BotMatchVariable(&match, 0, variable, sizeof(variable));
	assert(strcmp(variable, "Alice") == 0);
	BotMatchVariable(&match, 1, variable, sizeof(variable));
	assert(strcmp(variable, "Bob") == 0);

	assert(!BotFindMatch("Alice got frozen by Bob\n",
		&match,
		MTCONTEXT_CLIENTOBITUARY_TEST));

	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	remove(path);
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
test_enter_chat_tell_uses_owner_client_and_target

Pins Q3 BotEnterChat wiring: BotSetChatName supplies the command source client
while the BotEnterChat client argument remains the tell target.
=============
*/
static void test_enter_chat_tell_uses_owner_client_and_target(void)
{
	const char *path = "bot_chat_tell_owner_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"chat \"tell_owner\"\n"
		"{\n"
		"type \"line\"\n"
		"{\n"
		"\"private hello\";\n"
		"}\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	ChatBridge_Reset();
	BotLib_TestSetMaxClients(8.0f);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "tell_owner"));
	BotSetChatName(chat, "babe", 5);
	assert(BotInitialChat(chat, "line", 0, NULL));

	BotEnterChat(chat, 3, 2);

	assert(g_chat_bridge_mock.command_calls == 1);
	assert(g_chat_bridge_mock.last_client == 5);
	assert(strcmp(g_chat_bridge_mock.last_command,
		"tell 3 private hello") == 0);

	Q2Bridge_ClearImportTable();
	BotFreeChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_enter_handoff_uses_bridge_speaker

Checks reply construction leaves pending text for BotEnterChat, which forwards
the command through the bridge using the entering client.
=============
*/
static void test_reply_chat_enter_handoff_uses_bridge_speaker(void)
{
	ChatBridge_Reset();
	BotLib_TestSetMaxClients(8.0f);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/match_reply.c", "match_reply"));

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 2, 0.0);

	assert(BotReplyChat(chat, "Quad Damage acquired", 2));
	assert(g_chat_bridge_mock.command_calls == 0);
	assert(BotChatLength(chat) > 0);

	BotEnterChat(chat, 4, 0);
	assert(g_chat_bridge_mock.command_calls == 1);
	assert(g_chat_bridge_mock.last_client == 4);
	assert(strcmp(g_chat_bridge_mock.last_command, "say NEARBYITEM acquired") == 0);
	assert(BotChatLength(chat) == 0);

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
	test_pending_chat_handoff_removes_tildes();
	test_initial_chat_recent_lines_rotate();
	test_initial_chat_rejects_float_message_component();
	test_initial_chat_rejects_missing_message_component_comma();
	test_reply_chat_rejects_float_message_component();
	test_reply_chat_rejects_missing_message_component_comma();
	test_reply_chat_rejects_float_pattern_variable();
	test_match_template_rejects_float_variable();
	test_match_template_rejects_float_message_type();
	test_match_template_rejects_float_message_subtype();
	test_match_template_rejects_float_context_label();
	test_match_template_rejects_out_of_range_variable();
	test_match_template_rejects_adjacent_variables();
	test_match_template_rejects_missing_piece_comma();
	test_reply_chat_rejects_out_of_range_pattern_variable();
	test_reply_chat_rejects_adjacent_pattern_variables();
	test_reply_chat_rejects_missing_pattern_piece_comma();
	test_reply_chat_rejects_empty_pattern_key();
	test_reply_chat_rejects_empty_alternative_between_variables();
	test_reply_chat_rejects_empty_key_list();
	test_reply_chat_accepts_missing_key_commas();
	test_reply_chat_rejects_unquoted_plain_key();
	test_reply_chat_rejects_unquoted_botname_list_entry();
	test_initial_chat_test_libvar_prints_and_suppresses_dispatch();
	test_retail_initial_chat_missing_name_is_rejected();
	test_enter_chat_construct_message_failure_respects_cooldown_reset();
	test_reply_chat_death_context();
	test_reply_chat_falls_back_to_reply_table();
	test_reply_chat_name_key_matches_configured_name();
	test_reply_chat_chooses_highest_priority_match();
	test_reply_chat_recent_responses_rotate();
	test_reply_chat_matches_botname_list_and_gender_keys();
	test_reply_chat_construct_message_paths();
	test_reply_chat_known_random_string_context_enqueues_message();
	test_reply_chat_expands_named_random_table();
	test_reply_chat_expands_nested_random_table();
	test_initial_chat_weighted_synonym_runs_each_expansion_pass();
	test_initial_chat_applies_weighted_synonym_context();
	test_reply_chat_captures_key_variable();
	test_reply_chat_captures_key_variable_after_string_alternative();
	test_reply_chat_empty_string_piece_closes_variable_capture();
	test_reply_chat_split_vcontext_canonicalizes_variables();
	test_reply_chat_split_vcontext_uses_q3_word_separators();
	test_reply_chat_split_context_accepts_fixed_var_slots();
	test_reply_chat_test_libvar_dumps_responses_without_dispatch();
	test_reply_chat_captures_match_template_variables();
	test_reply_chat_unknown_random_string_context_logs_error();
	test_synonym_lookup_contains_nearbyitem_entries();
	test_known_template_is_registered();
	test_include_path_too_long_is_rejected();
	test_enter_chat_enqueues_message();
	test_enter_chat_cooldown_blocks_repeated_messages();
	test_reply_chat_unmatched_rule_returns_false_quietly();
	test_reply_chat_string_key_uses_q3_word_separators();
	test_botloadchatfile_fastchat_nochat_combinations();
	test_botloadchatfile_reports_missing_chat_context();
	test_setup_chat_ai_loads_default_assets();
	test_setup_chat_ai_skips_reply_when_nochat_enabled();
	test_setup_chat_ai_supplies_shared_reply_fallback();
	test_setup_chat_ai_exports_match_and_synonym_utilities();
	test_setup_chat_ai_match_string_alternatives_capture_variables();
	test_enter_chat_sends_command_via_bridge();
	test_enter_chat_team_command_uses_say_team();
	test_enter_chat_tell_uses_owner_client_and_target();
	test_reply_chat_enter_handoff_uses_bridge_speaker();

	printf("bot_chat_tests: all checks passed\n");
	return 0;
}

