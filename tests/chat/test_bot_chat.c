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
extern void BotLib_TestResetMessageHistory(void);
extern const char *BotLib_TestGetMessageHistory(void);
extern void BotLib_TestSetLibVar(const char *var_name, float value);
extern void BotLib_TestSetLibVarString(const char *var_name, const char *value);
extern void BotLib_TestResetLibVars(void);
extern void BotLib_TestSetMaxClients(float value);
extern void BotLib_TestSetAASTime(float time);
extern void BotLib_TestResetEAChat(void);
extern int BotLib_TestGetEASayCalls(void);
extern int BotLib_TestGetEASayTeamCalls(void);
extern int BotLib_TestGetEALastClient(void);
extern const char *BotLib_TestGetEALastText(void);

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
assert_chat_load_failure

Pins the retail error code and ordered inner/final diagnostics for a failed
per-client chat load.
=============
*/
static void assert_chat_load_failure(bot_chatstate_t *chat,
	const char *chatfile,
	const char *chatname,
	const char *inner_diagnostic)
{
	char final_diagnostic[1024];
	const int written = snprintf(final_diagnostic,
		sizeof(final_diagnostic),
		"couldn't load chat %s from %s\n",
		chatname,
		chatfile);
	assert(written > 0);
	assert((size_t)written < sizeof(final_diagnostic));

	BotLib_TestResetLastMessage();
	BotLib_TestResetMessageHistory();
	assert(BotLoadChatFile(chat, chatfile, chatname) == BLERR_CANNOTLOADICHAT);
	assert(BotLib_TestGetLastMessageType() == PRT_FATAL);
	assert(strcmp(BotLib_TestGetLastMessage(), final_diagnostic) == 0);

	const char *history = BotLib_TestGetMessageHistory();
	const char *inner = strstr(history, inner_diagnostic);
	assert(inner != NULL);
	const char *fatal = strstr(inner + strlen(inner_diagnostic), final_diagnostic);
	assert(fatal != NULL);
}

/*
=============
seed_retail_chat_ordinal

Finds and restores a C RNG seed whose next draws produce the requested retail
low-15-bit selection ordinal.
=============
*/
static void seed_retail_chat_ordinal(size_t draw_count,
	int available_count,
	int expected_ordinal)
{
	assert(draw_count > 0);
	assert(available_count > 0);
	assert(expected_ordinal >= 0);
	assert(expected_ordinal < available_count);
	for (unsigned int seed = 0; seed < 100000U; ++seed)
	{
		srand(seed);
		int matches = 1;
		for (size_t draw = 0; draw < draw_count; ++draw)
		{
			const float fraction =
				(float)(rand() & 0x7fff) * 0.000030518509f;
			const int ordinal = (int)(fraction * (float)available_count);
			if (ordinal != expected_ordinal)
			{
				matches = 0;
				break;
			}
		}
		if (matches)
		{
			srand(seed);
			return;
		}
	}

	assert(false);
}

/*
=============
seed_retail_reply_ordinal_zero

Selects ordinal zero for the requested sequence of retail reply RNG draws.
=============
*/
static void seed_retail_reply_ordinal_zero(size_t draw_count,
	int available_count)
{
	seed_retail_chat_ordinal(draw_count, available_count, 0);
}

/*
=============
drain_console

Clears queued console messages for deterministic checks.
=============
*/
static void drain_console(bot_chatstate_t *chat)
{
	int type = 0;
	char buffer[256];
	while (BotNextConsoleMessageCopy(chat, &type, buffer, sizeof(buffer)))
	{
		(void)type;
	}
}

/*
=============
retail_console_message_is_deferred

Mirrors Gladiator's console reader and the Q3 successor age gate.
=============
*/
static int retail_console_message_is_deferred(const bot_chatstate_t *chat,
	const bot_console_message_node_t *message,
	float now,
	float random_fraction)
{
	if (message == NULL
		|| BotNumConsoleMessages(chat) >= 10U
		|| message->type != CMS_CHAT)
	{
		return 0;
	}

	return message->time > now - (1.0f + random_fraction);
}

/*
=============
take_pending_chat

Copies and clears constructed chat text without conflating it with the inbound
console-message queue.
=============
*/
static int take_pending_chat(bot_chatstate_t *chat,
	char *buffer,
	size_t buffer_size)
{
	if (chat == NULL || buffer == NULL || buffer_size == 0U
		|| BotChatLength(chat) <= 0)
	{
		return 0;
	}

	BotGetChatMessage(chat, buffer, (int)buffer_size);
	return 1;
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
static bot_import_extended_t g_chat_bridge_imports;

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
write_single_initial_chat_file

Writes one minimal named retail initial-chat block for handoff tests.
=============
*/
static void write_single_initial_chat_file(const char *path,
	const char *chatname,
	const char *type,
	const char *message)
{
	remove(path);
	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	assert(fprintf(fp,
		"chat \"%s\"\n"
		"{\n"
		"type \"%s\"\n"
		"{\n"
		"\"%s\";\n"
		"}\n"
		"}\n",
		chatname,
		type,
		message) > 0);
	assert(fclose(fp) == 0);
}

/*
=============
alloc_chat_with_setup_reply_file

Loads one reply fixture through retail BotSetupChatAI ownership and returns a
fresh per-client adapter that references those shared assets.
=============
*/
static bot_chatstate_t *alloc_chat_with_setup_reply_file(const char *path)
{
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestSetLibVarString("rchatfile", path);
	assert(BotSetupChatAI() == BLERR_NOERROR);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	return chat;
}

/*
=============
free_chat_with_setup_assets

Releases a per-client adapter and the shared retail setup assets used by it.
=============
*/
static void free_chat_with_setup_assets(bot_chatstate_t *chat)
{
	BotDestroyChatState(chat);
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
}

/*
=============
test_retail_enter_chat_empty_pending_is_noop

Pins the retail handoff contract: BotEnterChat never constructs a message when
the pending chat buffer is empty.
=============
*/
static void test_retail_enter_chat_empty_pending_is_noop(void)
{
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	BotLib_TestResetEAChat();
	assert(BotEnterChat(chat, 3, 0) == 0);
	assert(BotLib_TestGetEASayCalls() == 0);
	assert(BotLib_TestGetEASayTeamCalls() == 0);
	assert(BotLib_TestGetEALastClient() == -1);
	assert(BotLib_TestGetEALastText()[0] == '\0');
	assert(BotChatLength(chat) == 0U);

	BotDestroyChatState(chat);
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
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe") == BLERR_NOERROR);

	drain_console(chat);
	BotInitialChat(chat, "enter_game", NULL);
	assert(BotChatLength(chat) > 0U);
	BotLib_TestResetEAChat();
	(void)BotEnterChat(chat, 0, 0);

	assert(BotLib_TestGetEASayCalls() == 1);
	assert(BotLib_TestGetEASayTeamCalls() == 0);
	assert(BotLib_TestGetEALastClient() == 0);
	assert(string_is_one_of(BotLib_TestGetEALastText(),
		expected_enter_messages,
		sizeof(expected_enter_messages) / sizeof(expected_enter_messages[0])));
	assert(BotChatLength(chat) == 0U);

	BotDestroyChatState(chat);
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
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe") == BLERR_NOERROR);

	const int enter_count = BotNumInitialChats(chat, "enter_game");
	const int exit_count = BotNumInitialChats(chat, "exit_game");
	const int start_count = BotNumInitialChats(chat, "start_level");
	const int end_count = BotNumInitialChats(chat, "end_level");

	assert(enter_count > 0);
	assert(exit_count > 0);
	assert(start_count > 0);
	assert(end_count > 0);
	assert(BotNumInitialChats(chat, "level_end_victory") == 0);
	assert(BotNumInitialChats(chat, "missing_type") == 0);

	BotDestroyChatState(chat);
}

/*
=============
test_q3_compat_initial_chat_constructs_from_alias

Exercises the reconstructed BotInitialChat path using the Quake III successor
alias for Gladiator's exit_game bucket.
=============
*/
static void test_q3_compat_initial_chat_constructs_from_alias(void)
{
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe") == BLERR_NOERROR);

	drain_console(chat);
	assert(BotInitialChatWithContext(chat,
		"game_exit",
		0,
		"Babe",
		"Opponent",
		"[invalid]",
		"[invalid]",
		"base1",
		NULL));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(buffer[0] != '\0');
	assert(strstr(buffer, "\\v") == NULL);

	BotDestroyChatState(chat);
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
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe") == BLERR_NOERROR);

	drain_console(chat);
	BotInitialChat(chat,
		"exit_game",
		"Babe",
		"Opponent",
		"[invalid]",
		"[invalid]",
		"base1",
		NULL);
	assert(BotChatLength(chat) > 0);

	BotLib_TestResetEAChat();
	(void)BotEnterChat(chat, 6, 0);

	assert(BotLib_TestGetEASayCalls() == 1);
	assert(BotLib_TestGetEASayTeamCalls() == 0);
	assert(BotLib_TestGetEALastClient() == 6);
	assert(BotLib_TestGetEALastText()[0] != '\0');
	assert(BotChatLength(chat) == 0);

	BotDestroyChatState(chat);
}

/*
=============
test_q3_compat_get_chat_message_copies_and_clears_pending

Exercises the recovered BotGetChatMessage export semantics for pending initial
chat text.
=============
*/
static void test_q3_compat_get_chat_message_copies_and_clears_pending(void)
{
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe") == BLERR_NOERROR);

	drain_console(chat);
	BotInitialChat(chat,
		"exit_game",
		"Babe",
		"Opponent",
		"[invalid]",
		"[invalid]",
		"base1",
		NULL);
	assert(BotChatLength(chat) > 0);

	char buffer[256];
	BotGetChatMessage(chat, buffer, sizeof(buffer));
	assert(buffer[0] != '\0');
	assert(strstr(buffer, "\\v") == NULL);
	assert(BotChatLength(chat) == 0);

	memset(buffer, 'x', sizeof(buffer));
	BotGetChatMessage(chat, buffer, sizeof(buffer));
	assert(buffer[0] == '\0');

	BotDestroyChatState(chat);
}

/*
=============
test_retail_enter_chat_preserves_tildes

Confirms the retail EA handoff forwards the pending buffer byte-for-byte.
=============
*/
static void test_retail_enter_chat_preserves_tildes(void)
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
	assert(BotLoadChatFile(chat, path, "tilde") == BLERR_NOERROR);

	drain_console(chat);
	BotInitialChat(chat, "line", NULL);
	assert(BotChatLength(chat) > 0U);
	BotLib_TestResetEAChat();
	(void)BotEnterChat(chat, 2, 0);
	assert(BotLib_TestGetEASayCalls() == 1);
	assert(BotLib_TestGetEASayTeamCalls() == 0);
	assert(BotLib_TestGetEALastClient() == 2);
	assert(strcmp(BotLib_TestGetEALastText(), "hello~there") == 0);
	assert(BotChatLength(chat) == 0U);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_q3_compat_get_chat_message_removes_tildes

Keeps Q3 BotGetChatMessage tilde cleanup coverage separate from retail
BotEnterChat dispatch semantics.
=============
*/
static void test_q3_compat_get_chat_message_removes_tildes(void)
{
	const char *path = "bot_chat_q3_tilde_handoff_test.c";
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

	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	assert(BotSetupChatAI() == 0);
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "tilde") == BLERR_NOERROR);
	BotInitialChat(chat, "line", NULL);

	char buffer[256];
	BotGetChatMessage(chat, buffer, sizeof(buffer));
	assert(strcmp(buffer, "hellothere") == 0);
	assert(BotChatLength(chat) == 0U);

	BotDestroyChatState(chat);
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	remove(path);
}

/*
=============
test_retail_initial_chat_uses_aas_time_and_low_15_bit_rng

Pins head-inserted retail line order, AAS_Time recency, and low-15-bit random
selection for initial-chat type buckets.
=============
*/
static void test_retail_initial_chat_uses_aas_time_and_low_15_bit_rng(void)
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
	assert(BotLoadChatFile(chat, path, "recent") == BLERR_NOERROR);

	BotLib_TestSetAASTime(100.0f);
	seed_retail_chat_ordinal(1, 2, 0);
	BotInitialChat(chat, "line", NULL);
	char first[256];
	BotGetChatMessage(chat, first, sizeof(first));
	assert(strcmp(first, "second line") == 0);

	BotInitialChat(chat, "line", NULL);
	char second[256];
	BotGetChatMessage(chat, second, sizeof(second));
	assert(strcmp(second, "first line") == 0);

	BotLib_TestSetAASTime(121.0f);
	seed_retail_chat_ordinal(1, 2, 0);
	BotInitialChat(chat, "line", NULL);
	char third[256];
	BotGetChatMessage(chat, third, sizeof(third));
	assert(strcmp(third, "second line") == 0);

	BotDestroyChatState(chat);
	BotLib_TestSetAASTime(0.0f);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"float_initial",
		"couldn't find chat float_initial in "
		"bot_chat_float_initial_component_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"missing_initial_comma",
		"couldn't find chat missing_initial_comma in "
		"bot_chat_missing_initial_component_comma_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"float_reply",
		"couldn't load chat float_reply from "
		"bot_chat_float_reply_component_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"missing_reply_component_comma",
		"couldn't load chat missing_reply_component_comma from "
		"bot_chat_missing_reply_component_comma_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"float_reply_key",
		"couldn't load chat float_reply_key from "
		"bot_chat_float_reply_key_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"float_match",
		"couldn't find chat float_match in "
		"bot_chat_float_match_template_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"float_match_type",
		"couldn't find chat float_match_type in "
		"bot_chat_float_match_type_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"float_match_subtype",
		"couldn't find chat float_match_subtype in "
		"bot_chat_float_match_subtype_test.c\n");

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_match_template_requires_message_subtype

Pins the retail BotLoadMatchTemplates grammar, which always requires a comma
and integer subtype after the message type.
=============
*/
static void test_match_template_requires_message_subtype(void)
{
	const char *path = "bot_chat_missing_match_subtype_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"#include \"match.h\"\n"
		"MTCONTEXT_CLIENTOBITUARY\n"
		"{\n"
		"\"hello\" = (MSG_DEATH);\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"missing_match_subtype",
		"couldn't find chat missing_match_subtype in "
		"bot_chat_missing_match_subtype_test.c\n");

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_match_template_requires_metadata_closing_tokens

Ensures BotLoadMatchTemplates rejects extra metadata tokens instead of scanning
forward to the next semicolon.
=============
*/
static void test_match_template_requires_metadata_closing_tokens(void)
{
	const char *path = "bot_chat_invalid_match_metadata_tail_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"#include \"match.h\"\n"
		"MTCONTEXT_CLIENTOBITUARY\n"
		"{\n"
		"\"hello\" = (MSG_DEATH, ST_DEATH_RAILGUN, 7);\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"invalid_match_metadata_tail",
		"couldn't find chat invalid_match_metadata_tail in "
		"bot_chat_invalid_match_metadata_tail_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"float_match_context",
		"couldn't find chat float_match_context in "
		"bot_chat_float_match_context_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"range_match_variable",
		"couldn't find chat range_match_variable in "
		"bot_chat_range_match_variable_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"adjacent_match_variable",
		"couldn't find chat adjacent_match_variable in "
		"bot_chat_adjacent_match_variable_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"missing_match_piece_comma",
		"couldn't find chat missing_match_piece_comma in "
		"bot_chat_missing_match_piece_comma_test.c\n");

	BotDestroyChatState(chat);
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
		"[(\"value \", 10)] = 16\n"
		"{\n"
		"\"bad\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	configure_chat_libvars(0.0f, 0.0f);
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"range_reply_key",
		"couldn't load chat range_reply_key from "
		"bot_chat_range_reply_key_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"adjacent_reply_key",
		"couldn't load chat adjacent_reply_key from "
		"bot_chat_adjacent_reply_key_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"missing_reply_pattern_comma",
		"couldn't load chat missing_reply_pattern_comma from "
		"bot_chat_missing_reply_pattern_comma_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"empty_reply_pattern",
		"couldn't load chat empty_reply_pattern from "
		"bot_chat_empty_reply_pattern_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"empty_alt_adjacent_reply_key",
		"couldn't load chat empty_alt_adjacent_reply_key from "
		"bot_chat_empty_alt_adjacent_reply_key_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"empty_reply_key",
		"couldn't load chat empty_reply_key from "
		"bot_chat_empty_reply_key_test.c\n");

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_q3_compat_reply_chat_accepts_missing_key_commas

Pins Q3 BotLoadReplyChat's permissive key-list separator handling, where the
comma after each key is optional.
=============
*/
static void test_q3_compat_reply_chat_accepts_missing_key_commas(void)
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
	assert(BotLoadChatFile(chat, path, "missing_reply_key_comma") == BLERR_NOERROR);

	drain_console(chat);
	BotSetChatNameWithClient(chat, "world", 0);
	assert(BotReplyChatWithContext(chat, "world", 16));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "missing comma accepted") == 0);

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"unquoted_plain_reply_key",
		"couldn't load chat unquoted_plain_reply_key from "
		"bot_chat_unquoted_plain_reply_key_test.c\n");

	BotDestroyChatState(chat);
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
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	assert_chat_load_failure(chat,
		path,
		"unquoted_botname_reply_key",
		"couldn't load chat unquoted_botname_reply_key from "
		"bot_chat_unquoted_botname_reply_key_test.c\n");

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_q3_compat_initial_chat_test_libvar_prints_and_suppresses_dispatch

Mirrors Q3's bot_testichat behavior: count probes print diagnostics and
BotEnterChat prints the pending text instead of sending a chat command.
=============
*/
static void test_q3_compat_initial_chat_test_libvar_prints_and_suppresses_dispatch(void)
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
	assert(BotLoadChatFile(chat, path, "testichat") == BLERR_NOERROR);

	BotLib_TestSetLibVar("bot_testichat", 1.0f);
	drain_console(chat);
	BotLib_TestResetLastMessage();

	assert(BotNumInitialChats(chat, "line") == 1);
	assert(BotLib_TestGetLastMessageType() == PRT_MESSAGE);
	assert(strcmp(BotLib_TestGetLastMessage(), "-------------------\n") == 0);

	assert(BotInitialChatWithContext(chat, "line", 0, NULL));
	drain_console(chat);
	ChatBridge_Reset();
	BotLib_TestResetLastMessage();
	BotEnterChat(chat, 2, 0);

	assert(g_chat_bridge_mock.command_calls == 0);
	assert(strcmp(BotLib_TestGetLastMessage(), "debug line\n") == 0);
	assert(BotChatLength(chat) == 0);

	Q2Bridge_ClearImportTable();
	BotLib_TestSetLibVar("bot_testichat", 0.0f);
	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_retail_initial_chat_missing_name_is_rejected

Ensures a missing named block produces the ordered retail missing/fatal load
diagnostics.
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
	assert_chat_load_failure(chat,
		BOT_ASSET_ROOT "/bots/babe_t.c",
		"missing",
		expected_message);
	assert(BotNumConsoleMessages(chat) == 0U);

	configure_chat_libvars(0.0f, 0.0f);
	BotDestroyChatState(chat);
}

/*
=============
test_q3_compat_enter_game_literal_limit_and_cooldown

The Q3-style event helper retains retail constructor limits while committing
its compatibility cooldown.
=============
*/
static void test_q3_compat_enter_game_literal_limit_and_cooldown(void)
{
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/unit_test_chat.c", "unit_enter_invalid") == BLERR_NOERROR);

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 2, 1.0);
	BotChat_SetTime(chat, 10.0);
	ChatBridge_Reset();
	BotLib_TestResetLastMessage();
	assert(BotChat_EnterGame(chat, 0, 0));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(), "too long") != NULL);
	assert(g_chat_bridge_mock.command_calls == 1);
	assert(strncmp(g_chat_bridge_mock.last_command, "say ", 4) == 0);
	assert(strlen(g_chat_bridge_mock.last_command + 4) == 150U);

	assert(!BotChat_EnterGame(chat, 0, 0));
	assert(g_chat_bridge_mock.command_calls == 1);

	drain_console(chat);
	BotChat_SetTime(chat, 12.0);
	assert(BotChat_EnterGame(chat, 0, 0));
	assert(g_chat_bridge_mock.command_calls == 2);
	assert(strlen(g_chat_bridge_mock.last_command + 4) == 150U);

	Q2Bridge_ClearImportTable();
	BotDestroyChatState(chat);
}

/*
=============
test_reply_chat_death_context
=============
*/
static void test_reply_chat_death_context(void) {
	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(
		BOT_ASSET_ROOT "/rchat.c");

	drain_console(chat);
	assert(BotReplyChat(chat, "unit-test"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strlen(buffer) > 0);
	assert(strstr(buffer, "\\r") == NULL);

	free_chat_with_setup_assets(chat);
}

/*
=============
test_reply_chat_falls_back_to_reply_table
=============
*/
static void test_reply_chat_falls_back_to_reply_table(void) {
	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(
		BOT_ASSET_ROOT "/rchat.c");

	drain_console(chat);
	assert(BotReplyChat(chat, "abnormal"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(buffer[0] != '\0');

	free_chat_with_setup_assets(chat);
}

/*
=============
test_retail_reply_chat_name_key_is_inert

Pins the shipped Gladiator evaluator, which parses the bare name key but has no
matching branch for it.
=============
*/
static void test_retail_reply_chat_name_key_is_inert(void)
{
	const char *path = "bot_chat_reply_name_key_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"babe\"] = 3\n"
		"{\n"
		"\"literal name\";\n"
		"}\n"
		"[name] = 7\n"
		"{\n"
		"\"configured name\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(path);

	BotSetChatName(chat, "babe");
	drain_console(chat);
	assert(BotReplyChat(chat, "babe"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "literal name") == 0);

	free_chat_with_setup_assets(chat);
	remove(path);
}

/*
=============
test_q3_compat_reply_chat_name_key_matches_configured_name

Keeps successor name-key matching coverage on the explicitly split Q3 reply
entry point.
=============
*/
static void test_q3_compat_reply_chat_name_key_matches_configured_name(void)
{
	const char *path = "bot_chat_q3_reply_name_key_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"babe\"] = 3\n"
		"{\n"
		"\"literal name\";\n"
		"}\n"
		"[name] = 7\n"
		"{\n"
		"\"configured name\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(path);
	BotSetChatNameWithClient(chat, "babe", 3);
	assert(BotReplyChatWithContexts(chat,
		"babe",
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

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "configured name") == 0);

	free_chat_with_setup_assets(chat);
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

	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(path);

	drain_console(chat);
	assert(BotReplyChat(chat, "priority specific"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "high priority") == 0);

	free_chat_with_setup_assets(chat);
	remove(path);
}

/*
=============
test_reply_chat_priority_uses_integer_best_value

Pins the retail int bestpriority local: reverse traversal stores 1.9 as 1, so
the earlier source rule at 1.5 replaces it despite its lower float priority.
=============
*/
static void test_reply_chat_priority_uses_integer_best_value(void)
{
	const char *path = "bot_chat_reply_integer_priority_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"integer priority\"] = 1.5\n"
		"{\n"
		"\"first lower fractional priority\";\n"
		"}\n"
		"[\"integer priority\"] = 1.9\n"
		"{\n"
		"\"later higher fractional priority\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(path);

	seed_retail_reply_ordinal_zero(2, 1);
	drain_console(chat);
	assert(BotReplyChat(chat, "integer priority"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "first lower fractional priority") == 0);

	free_chat_with_setup_assets(chat);
	remove(path);
}

/*
=============
test_reply_chat_nonpositive_priorities_do_not_win

Pins the retail bestpriority initialization at zero: a matching zero-priority
rule cannot win, and the existing parser rejects a signed negative priority.
=============
*/
static void test_reply_chat_nonpositive_priorities_do_not_win(void)
{
	const char *path = "bot_chat_reply_nonpositive_priority_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"nonpositive\"] = 0\n"
		"{\n"
		"\"zero priority\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "nonpositive_priority") == BLERR_NOERROR);

	seed_retail_reply_ordinal_zero(2, 1);
	drain_console(chat);
	BotLib_TestResetLastMessage();
	assert(!BotReplyChat(chat, "nonpositive"));
	assert(BotNumConsoleMessages(chat) == 0);
	assert(BotChatLength(chat) == 0);
	assert(strcmp(BotLib_TestGetLastMessage(), "") == 0);

	free_chat_with_setup_assets(chat);

	fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"nonpositive\"] = -1\n"
		"{\n"
		"\"negative priority\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "negative_priority") == BLERR_CANNOTLOADICHAT);
	drain_console(chat);
	assert(!BotReplyChat(chat, "nonpositive"));
	assert(BotNumConsoleMessages(chat) == 0);
	assert(BotChatLength(chat) == 0);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_equal_priority_prefers_file_last_rule

Mirrors head insertion of parsed reply rules: reverse traversal visits the
file-last rule first and an equal-priority earlier rule cannot replace it.
=============
*/
static void test_reply_chat_equal_priority_prefers_file_last_rule(void)
{
	const char *path = "bot_chat_reply_equal_priority_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"equal priority\"] = 4\n"
		"{\n"
		"\"file first equal priority\";\n"
		"}\n"
		"[\"equal priority\"] = 4\n"
		"{\n"
		"\"file last equal priority\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(path);

	seed_retail_reply_ordinal_zero(1, 1);
	drain_console(chat);
	assert(BotReplyChat(chat, "equal priority"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "file last equal priority") == 0);

	free_chat_with_setup_assets(chat);
	remove(path);
}

/*
=============
test_reply_chat_all_recent_reuses_retail_head

Pins the retail raw-node walk: with no eligible response, num is zero and the
predecrement selects the linked-list head anyway.
=============
*/
static void test_reply_chat_all_recent_reuses_retail_head(void)
{
	const char *path = "bot_chat_reply_recent_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"recent\"] = 3\n"
		"{\n"
		"\"only reply\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(path);

	BotLib_TestSetAASTime(200.0f);
	seed_retail_reply_ordinal_zero(1, 1);
	drain_console(chat);
	assert(BotReplyChat(chat, "recent"));

	char first[256];
	assert(take_pending_chat(chat, first, sizeof(first)));
	assert(strcmp(first, "only reply") == 0);

	BotLib_TestSetAASTime(205.0f);
	assert(BotReplyChat(chat, "recent"));
	char second[256];
	assert(take_pending_chat(chat, second, sizeof(second)));
	assert(strcmp(second, "only reply") == 0);

	free_chat_with_setup_assets(chat);
	BotLib_TestSetAASTime(0.0f);
	remove(path);
}

/*
=============
test_retail_reply_chat_matches_gender_keys

Exercises the three special gender keys implemented by Gladiator's retail
reply evaluator.
=============
*/
static void test_retail_reply_chat_matches_gender_keys(void)
{
	const char *path = "bot_chat_reply_special_keys_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
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

	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(path);

	char buffer[256];
	drain_console(chat);
	BotSetChatGender(chat, CHAT_GENDERFEMALE);
	assert(BotReplyChat(chat, "anything"));
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "female reply") == 0);

	drain_console(chat);
	BotSetChatGender(chat, CHAT_GENDERMALE);
	assert(BotReplyChat(chat, "anything"));
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "male reply") == 0);

	drain_console(chat);
	BotSetChatGender(chat, CHAT_GENDERLESS);
	assert(BotReplyChat(chat, "anything"));
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "genderless reply") == 0);

	free_chat_with_setup_assets(chat);
	remove(path);
}

/*
=============
test_reply_chat_without_pattern_falls_back_to_reply_table
=============
*/
static void test_reply_chat_without_pattern_falls_back_to_reply_table(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/match_reply.c", "match_reply") == BLERR_NOERROR);

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 2, 0.0);
	assert(BotReplyChat(chat, "fallback"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "fallback reply") == 0);

	BotDestroyChatState(chat);
}

/*
=============
test_q3_compat_enter_game_helper_dispatches_message

Keeps successor event construction separate from retail BotEnterChat handoff.
=============
*/
static void test_q3_compat_enter_game_helper_dispatches_message(void)
{
	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(
		BOT_ASSET_ROOT "/rchat.c");

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 2, 0.0);
	BotLib_TestResetEAChat();
	assert(BotChat_EnterGame(chat, 0, 0));

	assert(BotLib_TestGetEASayCalls() == 1);
	assert(BotLib_TestGetEASayTeamCalls() == 0);
	assert(BotLib_TestGetEALastClient() == 0);
	assert(strstr(BotLib_TestGetEALastText(), "entered the game") != NULL);
	assert(BotNumConsoleMessages(chat) == 0U);

	free_chat_with_setup_assets(chat);
}

/*
=============
test_q3_compat_enter_game_cooldown_blocks_repeated_messages

Pins the successor event helper's fixed client/context cooldown behavior.
=============
*/
static void test_q3_compat_enter_game_cooldown_blocks_repeated_messages(void)
{
	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(
		BOT_ASSET_ROOT "/rchat.c");

	drain_console(chat);
	BotChat_SetContextCooldown(chat, 2, 5.0);
	BotChat_SetTime(chat, 1.0);
	BotLib_TestResetEAChat();
	assert(BotChat_EnterGame(chat, 0, 0));

	assert(BotLib_TestGetEASayCalls() == 1);
	assert(BotLib_TestGetEASayTeamCalls() == 0);
	assert(BotLib_TestGetEALastClient() == 0);
	assert(strstr(BotLib_TestGetEALastText(), "entered the game") != NULL);

	BotChat_SetTime(chat, 2.0);
	assert(!BotChat_EnterGame(chat, 0, 0));

	assert(BotLib_TestGetEASayCalls() == 1);
	assert(BotNumConsoleMessages(chat) == 0U);

	free_chat_with_setup_assets(chat);
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
	assert(BotLoadChatFile(chat, path, "missing_reply") == BLERR_NOERROR);

	drain_console(chat);
	BotLib_TestResetLastMessage();

	assert(BotReplyChat(chat, "known"));
	assert(!BotReplyChat(chat, "unknown"));
	assert(strcmp(BotLib_TestGetLastMessage(), "") == 0);
	assert(BotLib_TestGetLastMessageType() == 0);
	assert(BotNumConsoleMessages(chat) == 0U);

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "known reply") == 0);
	assert(BotChat_HasReplyTemplate(chat, 3, buffer));
	assert(strstr(buffer, "\\r") == NULL);

	assert(!take_pending_chat(chat, buffer, sizeof(buffer)));

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_retail_reply_chat_string_key_uses_raw_substring

Pins Gladiator's ordinary quoted-key branch, which calls case-insensitive
StringContains without word-boundary checks.
=============
*/
static void test_retail_reply_chat_string_key_uses_raw_substring(void)
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

	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(path);

	drain_console(chat);
	BotLib_TestResetLastMessage();
	assert(BotReplyChat(chat, "sHELLoWorld"));
	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "matched hello") == 0);
	assert(!take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(BotNumConsoleMessages(chat) == 0);
	assert(strcmp(BotLib_TestGetLastMessage(), "") == 0);

	free_chat_with_setup_assets(chat);
	remove(path);
}

/*
=============
test_q3_compat_reply_chat_string_key_uses_word_separators

Keeps successor StringContainsWord punctuation behavior on the explicitly
split Q3 reply entry point.
=============
*/
static void test_q3_compat_reply_chat_string_key_uses_word_separators(void)
{
	const char *path = "bot_chat_q3_reply_word_separator_test.c";
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

	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(path);
	assert(!BotReplyChatWithContexts(chat,
		"hello?",
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
	assert(BotReplyChatWithContexts(chat,
		"well, hello!",
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

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "matched hello") == 0);

	free_chat_with_setup_assets(chat);
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
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/unit_test_chat.c", "unit_reply") == BLERR_NOERROR);

	drain_console(chat);
	assert(BotReplyChat(chat, "unit-test-reply-valid"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "Unit test reply constructed successfully") == 0);

	drain_console(chat);
	BotLib_TestResetLastMessage();
	assert(BotReplyChat(chat, "unit-test-reply-invalid"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(), "too long") != NULL);
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strlen(buffer) == 150U);

	free_chat_with_setup_assets(chat);
}

/*
=============
test_reply_chat_ignores_event_gates

Confirms reply selection does not consult the initial-chat cooldown/nochat
gate and therefore emits no gate diagnostics.
=============
*/
static void test_reply_chat_ignores_event_gates(void)
{
	const char *path = "bot_chat_reply_event_gate_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"gate independent\"] = 2\n"
		"{\n"
		"\"reply passed gate\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "event_gate") == BLERR_NOERROR);
	BotChat_SetContextCooldown(chat, 77, 100.0);
	BotChat_SetTime(chat, 10.0);
	seed_retail_reply_ordinal_zero(3, 1);

	for (int attempt = 0; attempt < 2; ++attempt)
	{
		drain_console(chat);
		BotLib_TestResetLastMessage();
		assert(BotReplyChat(chat, "gate independent"));

		char buffer[256];
		assert(take_pending_chat(chat, buffer, sizeof(buffer)));
		assert(strcmp(buffer, "reply passed gate") == 0);
		assert(!take_pending_chat(chat, buffer, sizeof(buffer)));
		assert(strcmp(BotLib_TestGetLastMessage(), "") == 0);
	}

	BotLib_TestSetLibVar("nochat", 1.0f);
	drain_console(chat);
	BotLib_TestResetLastMessage();
	assert(BotReplyChat(chat, "gate independent"));
	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "reply passed gate") == 0);
	assert(!take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(BotLib_TestGetLastMessage(), "") == 0);
	BotLib_TestSetLibVar("nochat", 0.0f);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_selected_constructor_failure_returns_true

Pins Gladiator's post-selection contract: a selected and marked malformed
response reports reply success, and direct in-state construction preserves the
unwritten suffix from the previous pending buffer.
=============
*/
static void test_reply_chat_selected_constructor_failure_returns_true(void)
{
	const char *path = "bot_chat_reply_constructor_failure_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"seed response\"] = 2\n"
		"{\n"
		"\"ABCDEFGHIJKLMN\";\n"
		"}\n"
		"[\"malformed response\"] = 2\n"
		"{\n"
		"\"broken \1rmissing_random\1\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(path);
	seed_retail_reply_ordinal_zero(2, 1);
	drain_console(chat);
	assert(BotReplyChat(chat, "seed response"));
	assert(BotChatLength(chat) > 0U);
	BotLib_TestResetLastMessage();

	assert(BotReplyChat(chat, "malformed response"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(), "unknown random string") != NULL);
	assert(BotNumConsoleMessages(chat) == 0);
	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "broken HIJKLMN") == 0);

	free_chat_with_setup_assets(chat);
	remove(path);
}

/*
=============
test_constructor_retail_escape_and_variable_semantics

Covers byte-one escape parsing, missing-variable omission, literal legacy
syntax, safe unterminated random/variable paths, the raw non-digit index
diagnostic, and the Gladiator 0..10 bounds.
=============
*/
static void test_constructor_retail_escape_and_variable_semantics(void)
{
	const char *path = "bot_chat_constructor_retail_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"known = {\"omega\"}\n"
		"chat \"constructor_retail\"\n"
		"{\n"
		"type \"missing\"\n"
		"{\n"
		"\"A\", 0, \"B\";\n"
		"}\n"
		"type \"literal\"\n"
		"{\n"
		"\"A \\\\v0\\\\ {VICTIM} B\";\n"
		"}\n"
		"type \"unterminated\"\n"
		"{\n"
		"\"A\\1v0\";\n"
		"}\n"
		"type \"unterminated_random\"\n"
		"{\n"
		"\"A\\1rknown\";\n"
		"}\n"
		"type \"nondigit\"\n"
		"{\n"
		"\"A\\1vA\";\n"
		"}\n"
		"type \"unknown\"\n"
		"{\n"
		"\"A\\1xB\";\n"
		"}\n"
		"type \"ten\"\n"
		"{\n"
		"\"slot \", 10;\n"
		"}\n"
		"type \"variable_overflow\"\n"
		"{\n"
		"\"\1v0\1\";\n"
		"}\n"
		"type \"eleven\"\n"
		"{\n"
		"\"slot \", 11;\n"
		"}\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "constructor_retail") == BLERR_NOERROR);

	drain_console(chat);
	assert(BotInitialChatWithContext(chat, "missing", 0, NULL));
	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "AB") == 0);

	drain_console(chat);
	assert(BotInitialChatWithContext(chat, "literal", 0, "Alice", NULL));
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "A \\v0\\ {VICTIM} B") == 0);

	drain_console(chat);
	assert(BotInitialChatWithContext(chat, "unterminated", 0, "Alice", NULL));
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "AAlice") == 0);

	drain_console(chat);
	assert(BotInitialChatWithContext(chat, "unterminated_random", 0, NULL));
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "Aomega") == 0);

	drain_console(chat);
	BotLib_TestResetLastMessage();
	assert(!BotInitialChatWithContext(chat, "nondigit", 0, NULL));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(), "variable 17 out of range") != NULL);
	assert(BotNumConsoleMessages(chat) == 0U);

	drain_console(chat);
	BotLib_TestResetLastMessage();
	assert(BotInitialChatWithContext(chat, "unknown", 0, NULL));
	assert(BotLib_TestGetLastMessageType() == PRT_FATAL);
	assert(strstr(BotLib_TestGetLastMessage(), "invalid escape char") != NULL);
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "AxB") == 0);

	drain_console(chat);
	assert(BotInitialChatWithContext(chat,
		"ten",
		0,
		"zero",
		"one",
		"two",
		"three",
		"four",
		"five",
		"six",
		"seven",
		"eight",
		"nine",
		"ten"));
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "slot ten") == 0);

	char oversized_variable[151];
	memset(oversized_variable, 'v', sizeof(oversized_variable) - 1U);
	oversized_variable[sizeof(oversized_variable) - 1U] = '\0';
	drain_console(chat);
	BotLib_TestResetLastMessage();
	assert(!BotInitialChatWithContext(chat,
		"variable_overflow",
		0,
		oversized_variable,
		NULL));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strcmp(BotLib_TestGetLastMessage(),
		"BotConstructChat: message \1v0\1 too long\n") == 0);
	assert(BotNumConsoleMessages(chat) == 0U);

	drain_console(chat);
	BotLib_TestResetLastMessage();
	assert(!BotInitialChatWithContext(chat, "eleven", 0, NULL));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(), "variable 11 out of range") != NULL);
	assert(BotNumConsoleMessages(chat) == 0);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_constructor_retail_payload_boundary

Pins the 0x98-byte retail storage with the observed 0x96 payload guard:
149 literal characters fit quietly; the 150th is copied and diagnosed, and the
complete 150-character result is still delivered.
=============
*/
static void test_constructor_retail_payload_boundary(void)
{
	const char *path = "bot_chat_constructor_limit_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"chat \"constructor_limit\"\n"
		"{\n"
		"type \"fits\"\n"
		"{\n"
		"\"",
		fp);
	for (size_t i = 0; i < 149U; ++i)
	{
		assert(fputc('a', fp) != EOF);
	}
	fputs(
		"\";\n"
		"}\n"
		"type \"guard\"\n"
		"{\n"
		"\"",
		fp);
	for (size_t i = 0; i < 150U; ++i)
	{
		assert(fputc('b', fp) != EOF);
	}
	fputs(
		"\";\n"
		"}\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "constructor_limit") == BLERR_NOERROR);

	drain_console(chat);
	assert(BotInitialChatWithContext(chat, "fits", 0, NULL));
	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strlen(buffer) == 149U);

	drain_console(chat);
	BotLib_TestResetLastMessage();
	assert(BotInitialChatWithContext(chat, "guard", 0, NULL));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(), "too long") != NULL);
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strlen(buffer) == 150U);
	for (size_t i = 0; i < 150U; ++i)
	{
		assert(buffer[i] == 'b');
	}

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_legacy_backslash_random_is_literal

Confirms the Gladiator constructor treats the old readable backslash marker as
ordinary message text instead of a random reference.
=============
*/
static void test_reply_chat_legacy_backslash_random_is_literal(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/unit_test_chat.c", "unit_random_valid") == BLERR_NOERROR);

	seed_retail_reply_ordinal_zero(1, 1);
	drain_console(chat);
	assert(BotReplyChat(chat, "unit-test-random-valid"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer,
		"Random string placeholder: \\rrandom_misc\\.") == 0);

	BotDestroyChatState(chat);
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
	assert(BotLoadChatFile(chat, path, "random_table") == BLERR_NOERROR);
	assert(BotChat_HasReplyTemplate(chat, 9300, "Pick alpha"));
	drain_console(chat);
	assert(BotReplyChat(chat, "unit"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "Pick alpha") == 0);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_random_expansion_runs_once

Pins Gladiator's single constructor pass: a selected random entry is copied,
but its legacy readable marker is not expanded again.
=============
*/
static void test_reply_chat_random_expansion_runs_once(void)
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
	assert(BotLoadChatFile(chat, path, "nested_random") == BLERR_NOERROR);
	drain_console(chat);
	assert(BotReplyChat(chat, "nested"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "Nested \\rinner\\") == 0);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_q3_compat_initial_chat_weighted_synonym_context

Pins the unconditional weighted-synonym pass at the end of Gladiator's single
constructor pass. A readable marker produced by the synonym remains literal.
=============
*/
static void test_q3_compat_initial_chat_weighted_synonym_context(void)
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
	assert(BotLoadChatFile(chat, path, "synonym_expansion") == BLERR_NOERROR);

	drain_console(chat);
	assert(BotInitialChatWithContext(chat, "line", 1, NULL));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "\\rinner\\") == 0);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_q3_compat_initial_chat_applies_weighted_synonym_context

Pins Gladiator's unconditional post-expansion weighted synonym pass for both
initial-chat and compatibility reply-chat construction paths.
=============
*/
static void test_q3_compat_initial_chat_applies_weighted_synonym_context(void)
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
		"}\n"
		"[\"weighted reply\"] = 1\n"
		"{\n"
		"\"frag and zap\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "weighted") == BLERR_NOERROR);

	drain_console(chat);
	assert(BotInitialChatWithContext(chat, "synonym", 0, NULL));
	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "frag and zap") == 0);

	drain_console(chat);
	assert(BotInitialChatWithContext(chat, "synonym", 1, NULL));
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "frag and frag") == 0
		|| strcmp(buffer, "zap and zap") == 0);

	seed_retail_reply_ordinal_zero(1, 1);
	drain_console(chat);
	assert(BotReplyChat(chat, "weighted reply"));
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "frag and zap") == 0);

	BotDestroyChatState(chat);
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
	assert(BotLoadChatFile(chat, path, "reply_capture") == BLERR_NOERROR);
	drain_console(chat);
	assert(BotReplyChat(chat, "I am testing captures"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "you are testing captures") == 0);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_key_capture_uses_file_first_value

Pins reverse linked-list key traversal: the file-first pattern runs last and
therefore supplies the final value when two matching keys capture var0.
=============
*/
static void test_reply_chat_key_capture_uses_file_first_value(void)
{
	const char *path = "bot_chat_reply_key_order_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[(0, \" tail\"), (\"prefix \", 0)] = 9405\n"
		"{\n"
		"\"captured \", 0;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "reply_key_order") == BLERR_NOERROR);

	seed_retail_reply_ordinal_zero(1, 1);
	drain_console(chat);
	assert(BotReplyChat(chat, "prefix middle tail"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "captured prefix middle") == 0);

	BotDestroyChatState(chat);
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
	assert(BotLoadChatFile(chat, path, "reply_alternative_capture") == BLERR_NOERROR);
	drain_console(chat);
	assert(!BotReplyChat(chat, "prefix warning Bravo"));
	assert(BotReplyChat(chat, "WARNING Bravo"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "handled Bravo") == 0);

	BotDestroyChatState(chat);
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
	assert(BotLoadChatFile(chat, path, "reply_empty_string_piece") == BLERR_NOERROR);
	drain_console(chat);
	assert(!BotReplyChat(chat, "not empty"));
	assert(BotReplyChat(chat, ""));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "empty") == 0);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_q3_compat_reply_chat_split_vcontext_canonicalizes_variables

Mirrors Q3 BotReplyChat's separate vcontext pass, where captured reply
variables are canonicalized through CONTEXT_REPLY synonyms before expansion.
=============
*/
static void test_q3_compat_reply_chat_split_vcontext_canonicalizes_variables(void)
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
		"[(\"i like \", 0)] = 1\n"
		"{\n"
		"\"you like \", 0;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "reply_vcontext") == BLERR_NOERROR);

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
	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
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
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "you like Quad Damage") == 0);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_q3_compat_reply_chat_split_vcontext_uses_word_separators

Pins Gladiator's normal variable canonicalization and retail word boundaries:
only spaces, periods, commas, and exclamation marks terminate synonym words.
=============
*/
static void test_q3_compat_reply_chat_split_vcontext_uses_word_separators(void)
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
		"[(\"i like \", 0)] = 1\n"
		"{\n"
		"\"you like \", 0;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "reply_vcontext_separator") == BLERR_NOERROR);

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
	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
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
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "you like Quad Damage!") == 0);

	drain_console(chat);
	assert(BotReplyChatWithContexts(chat,
		"I like prefix,quad!",
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
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "you like prefix,Quad Damage!") == 0);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_q3_compat_reply_chat_split_context_accepts_fixed_var_slots

Pins Q3's fixed var0-var7 reply arguments, including the game-side var6 and
var7 bot/player name slots used by reply chat wiring.
=============
*/
static void test_q3_compat_reply_chat_split_context_accepts_fixed_var_slots(void)
{
	const char *path = "bot_chat_reply_fixed_vars_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"hello\"] = 1\n"
		"{\n"
		"\"names \", 6, \" \", 7;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "reply_fixed_vars") == BLERR_NOERROR);

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

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "names botname speaker") == 0);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_legacy_uses_fixed_retail_contexts

Pins Gladiator's context-free reply export: weighted output synonyms use
mcontext 0 while captured variables are canonicalized with vcontext 16.
=============
*/
static void test_reply_chat_legacy_uses_fixed_retail_contexts(void)
{
	const char *path = "bot_chat_reply_legacy_context_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"CONTEXT_NORMAL\n"
		"{\n"
		"[\n"
		"(\"zap\", 10000000000.0),\n"
		"(\"frag\", 1.0)\n"
		"]\n"
		"}\n"
		"CONTEXT_REPLY\n"
		"{\n"
		"[\n"
		"(\"Quad Damage\", 1.0),\n"
		"(\"quad\", 1.0)\n"
		"]\n"
		"}\n"
		"[(\"i like \", 0)] = 1\n"
		"{\n"
		"\"frag \", 0;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "legacy_context") == BLERR_NOERROR);
	seed_retail_reply_ordinal_zero(1, 1);
	drain_console(chat);
	assert(BotReplyChat(chat, "I like quad"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "frag Quad Damage") == 0);
	assert(BotNumConsoleMessages(chat) == 0U);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_retail_capture_leaks_after_selected_rule

Gladiator passes its one scan-wide match table to construction, so a later
failed lower-priority pattern can replace a selected rule's capture. The Q3
extension snapshots captures when it selects the best rule.
=============
*/
static void test_reply_chat_retail_capture_leaks_after_selected_rule(void)
{
	const char *path = "bot_chat_reply_capture_leak_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[(0, \" tail\", \" missing\")] = 1\n"
		"{\n"
		"\"lower\";\n"
		"}\n"
		"[(\"prefix \", 0)] = 9\n"
		"{\n"
		"\"winner \", 0;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "capture_leak") == BLERR_NOERROR);
	seed_retail_reply_ordinal_zero(1, 1);
	drain_console(chat);
	assert(BotReplyChat(chat, "prefix chosen tail"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "winner prefix chosen") == 0);

	BotDestroyChatState(chat);
	chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "capture_leak") == BLERR_NOERROR);
	seed_retail_reply_ordinal_zero(1, 1);
	assert(BotReplyChatWithContexts(chat,
		"prefix chosen tail",
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
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "winner chosen tail") == 0);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_console_message_pool_uses_retail_capacity_and_fifo

Pins the shared max_messages limit, FIFO insertion, pool exhaustion, slot
reuse, and Gladiator's 150-byte incoming-message copy.
=============
*/
static void test_console_message_pool_uses_retail_capacity_and_fifo(void)
{
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestSetLibVar("max_messages", 17.0f);
	assert(BotSetupChatAI() == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	for (int i = 0; i < 17; ++i)
	{
		char message[32];
		const int written = snprintf(message,
			sizeof(message),
			"message-%02d",
			i);
		assert(written > 0 && (size_t)written < sizeof(message));
		BotQueueConsoleMessage(chat, 100 + i, message);
	}
	assert(BotNumConsoleMessages(chat) == 17U);

	BotLib_TestResetLastMessage();
	BotQueueConsoleMessage(chat, 999, "overflow");
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strcmp(BotLib_TestGetLastMessage(),
		"empty console message heap\n") == 0);
	assert(BotNumConsoleMessages(chat) == 17U);

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessageCopy(chat, &type, buffer, sizeof(buffer)));
	assert(type == 100);
	assert(strcmp(buffer, "message-00") == 0);
	BotQueueConsoleMessage(chat, 999, "reused-slot");
	assert(BotNumConsoleMessages(chat) == 17U);

	for (int i = 1; i < 17; ++i)
	{
		char expected[32];
		const int written = snprintf(expected,
			sizeof(expected),
			"message-%02d",
			i);
		assert(written > 0 && (size_t)written < sizeof(expected));
		assert(BotNextConsoleMessageCopy(chat, &type, buffer, sizeof(buffer)));
		assert(type == 100 + i);
		assert(strcmp(buffer, expected) == 0);
	}
	assert(BotNextConsoleMessageCopy(chat, &type, buffer, sizeof(buffer)));
	assert(type == 999);
	assert(strcmp(buffer, "reused-slot") == 0);
	assert(BotNumConsoleMessages(chat) == 0U);

	char long_message[192];
	memset(long_message, 'x', sizeof(long_message) - 1U);
	long_message[sizeof(long_message) - 1U] = '\0';
	BotQueueConsoleMessage(chat, 42, long_message);
	assert(BotNextConsoleMessageCopy(chat, &type, buffer, sizeof(buffer)));
	assert(type == 42);
	assert(strlen(buffer) == 150U);
	for (size_t i = 0; i < strlen(buffer); ++i)
	{
		assert(buffer[i] == 'x');
	}

	BotDestroyChatState(chat);
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
}

/*
=============
test_console_message_retail_node_identity_and_age

Pins the non-destructive retail peek, timestamp visibility, exact-node
removal, and the caller's strict one-to-two-second chat-reading delay.
=============
*/
static void test_console_message_retail_node_identity_and_age(void)
{
	assert(offsetof(bot_console_message_node_t, time) == 0U);
	assert(offsetof(bot_console_message_node_t, type) == 4U);
	assert(offsetof(bot_console_message_node_t, message) == 8U);
	assert(offsetof(bot_console_message_node_t, prev) == 0xa0U);
	if (sizeof(void *) == 4U)
	{
		assert(offsetof(bot_console_message_node_t, next) == 0xa4U);
		assert(sizeof(bot_console_message_node_t) == 0xa8U);
	}

	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestSetLibVar("max_messages", 12.0f);
	assert(BotSetupChatAI() == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	bot_chatstate_t *other_chat = BotAllocChatState();
	assert(chat != NULL);
	assert(other_chat != NULL);

	BotLib_TestSetAASTime(100.0f);
	BotQueueConsoleMessage(chat, CMS_CHAT, "first chat");
	bot_console_message_node_t *first = BotNextConsoleMessage(chat);
	assert(first != NULL);
	assert(first == BotNextConsoleMessage(chat));
	assert(BotNumConsoleMessages(chat) == 1U);
	assert(first->time == 100.0f);
	assert(first->type == CMS_CHAT);
	assert(strcmp(first->message, "first chat") == 0);
	assert(first->prev == NULL);
	assert(first->next == NULL);

	assert(retail_console_message_is_deferred(chat,
		first,
		101.49f,
		0.5f));
	assert(!retail_console_message_is_deferred(chat,
		first,
		101.5f,
		0.5f));

	BotLib_TestSetAASTime(101.0f);
	BotQueueConsoleMessage(chat, CMS_CHAT, "second chat");
	bot_console_message_node_t *second = first->next;
	assert(second != NULL);
	assert(second->prev == first);
	assert(second->time == 101.0f);
	assert(strcmp(second->message, "second chat") == 0);
	assert(BotRemoveConsoleMessage(chat, second) == 1);
	assert(BotNextConsoleMessage(chat) == first);
	assert(first->next == NULL);

	BotQueueConsoleMessage(other_chat, CMS_CHAT, "foreign node");
	bot_console_message_node_t *foreign = BotNextConsoleMessage(other_chat);
	assert(foreign != NULL);
	assert(BotRemoveConsoleMessage(chat, foreign) == 1);
	assert(BotNumConsoleMessages(chat) == 1U);
	assert(BotNumConsoleMessages(other_chat) == 1U);

	for (int i = 0; i < 9; ++i)
	{
		BotQueueConsoleMessage(chat, CMS_CHAT, "flood message");
	}
	assert(BotNumConsoleMessages(chat) == 10U);
	assert(!retail_console_message_is_deferred(chat,
		first,
		100.25f,
		0.5f));
	assert(BotRemoveConsoleMessage(chat, first) == 9);

	assert(BotRemoveConsoleMessage(other_chat, foreign) == 0);
	assert(BotNextConsoleMessage(other_chat) == NULL);
	BotDestroyChatState(chat);
	BotDestroyChatState(other_chat);
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
}

/*
=============
test_console_message_pool_is_shared_and_reclaims_nodes

Verifies that chat states contend for one retail pool and that type removal,
destructive compatibility reads, state destruction, and pool shutdown all
recycle or detach slots safely.
=============
*/
static void test_console_message_pool_is_shared_and_reclaims_nodes(void)
{
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestSetLibVar("max_messages", 3.0f);
	assert(BotSetupChatAI() == 0);

	bot_chatstate_t *first_state = BotAllocChatState();
	bot_chatstate_t *second_state = BotAllocChatState();
	assert(first_state != NULL);
	assert(second_state != NULL);

	BotQueueConsoleMessage(first_state, 7, "first-match");
	BotQueueConsoleMessage(first_state, 7, "retained-match");
	BotQueueConsoleMessage(second_state, 9, "other-state");
	assert(BotNumConsoleMessages(first_state) == 2U);
	assert(BotNumConsoleMessages(second_state) == 1U);

	BotLib_TestResetLastMessage();
	BotQueueConsoleMessage(second_state, 10, "global-overflow");
	assert(strcmp(BotLib_TestGetLastMessage(),
		"empty console message heap\n") == 0);
	assert(BotRemoveConsoleMessageType(first_state, 7));
	assert(BotNumConsoleMessages(first_state) == 1U);

	int type = 0;
	char buffer[64];
	assert(BotNextConsoleMessageCopy(first_state,
		&type,
		buffer,
		sizeof(buffer)));
	assert(type == 7);
	assert(strcmp(buffer, "retained-match") == 0);

	BotQueueConsoleMessage(first_state, 11, "released-by-state-free");
	BotQueueConsoleMessage(second_state, 12, "after-remove");
	assert(BotNumConsoleMessages(first_state) == 1U);
	assert(BotNumConsoleMessages(second_state) == 2U);
	BotDestroyChatState(first_state);

	BotQueueConsoleMessage(second_state, 13, "after-state-free");
	assert(BotNumConsoleMessages(second_state) == 3U);
	assert(BotNextConsoleMessageCopy(second_state,
		&type,
		buffer,
		sizeof(buffer)));
	assert(type == 9);
	assert(strcmp(buffer, "other-state") == 0);
	assert(BotNextConsoleMessageCopy(second_state,
		&type,
		buffer,
		sizeof(buffer)));
	assert(type == 12);
	assert(strcmp(buffer, "after-remove") == 0);
	assert(BotNextConsoleMessageCopy(second_state,
		&type,
		buffer,
		sizeof(buffer)));
	assert(type == 13);
	assert(strcmp(buffer, "after-state-free") == 0);

	BotQueueConsoleMessage(second_state, 14, "pending-at-pool-shutdown");
	assert(BotNumConsoleMessages(second_state) == 1U);
	BotShutdownChatAI();
	assert(BotNumConsoleMessages(second_state) == 0U);
	BotDestroyChatState(second_state);
	configure_chat_libvars(0.0f, 0.0f);
}

/*
=============
test_constructed_chat_stays_out_of_console_queue

Ensures generated initial text remains pending while the console queue contains
only explicitly supplied inbound messages.
=============
*/
static void test_constructed_chat_stays_out_of_console_queue(void)
{
	const char *path = "bot_chat_pending_queue_separation_test.c";
	remove(path);
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	assert(BotSetupChatAI() == 0);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"chat \"queue_separation\"\n"
		"{\n"
		"type \"line\"\n"
		"{\n"
		"\"initial pending\";\n"
		"}\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "queue_separation") == BLERR_NOERROR);
	drain_console(chat);

	BotQueueConsoleMessage(chat, 7, "incoming");
	BotInitialChat(chat, "line", NULL);
	assert(BotNumConsoleMessages(chat) == 1U);
	assert(BotChatLength(chat) > 0);

	int type = 0;
	char buffer[256];
	assert(BotNextConsoleMessageCopy(chat, &type, buffer, sizeof(buffer)));
	assert(type == 7);
	assert(strcmp(buffer, "incoming") == 0);
	assert(BotNumConsoleMessages(chat) == 0U);
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "initial pending") == 0);

	BotDestroyChatState(chat);
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	remove(path);
}

/*
=============
test_reply_chat_test_libvar_dumps_responses_without_dispatch

Pins Q3 bot_testrchat behavior: the matching reply block is expanded in
linked-list order without sending a chat command or marking a response recent.
=============
*/
static void test_reply_chat_test_libvar_dumps_responses_without_dispatch(void)
{
	const char *path = "bot_chat_testrchat_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"debug\"] = 1\n"
		"{\n"
		"\"~first reply~\";\n"
		"\"second reply\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "testrchat") == BLERR_NOERROR);

	BotChat_SetTime(chat, 100.0);
	seed_retail_reply_ordinal_zero(2, 2);
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
	assert(BotNumConsoleMessages(chat) == 0U);
	assert(strcmp(BotLib_TestGetLastMessage(), "first reply\n") == 0);
	assert(BotChatLength(chat) > 0);

	drain_console(chat);
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
	assert(BotNumConsoleMessages(chat) == 0U);
	assert(strcmp(BotLib_TestGetLastMessage(), "first reply\n") == 0);

	Q2Bridge_ClearImportTable();
	BotLib_TestSetLibVar("bot_testrchat", 0.0f);
	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_match_template_cannot_override_reply_rule

Confirms MT match-context templates are not reply candidates when a reply rule
matches the same incoming text.
=============
*/
static void test_reply_chat_match_template_cannot_override_reply_rule(void)
{
	const char *path = "bot_chat_match_reply_precedence_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"#include \"match.h\"\n"
		"MTCONTEXT_ENTERGAME\n"
		"{\n"
		"\"shared trigger\" = (MSG_ENTERGAME, 0);\n"
		"}\n"
		"[\"shared trigger\"] = 2\n"
		"{\n"
		"\"reply rule selected\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "match_reply_precedence") == BLERR_NOERROR);
	seed_retail_reply_ordinal_zero(1, 1);
	drain_console(chat);
	assert(BotReplyChat(chat, "shared trigger"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "reply rule selected") == 0);
	assert(!take_pending_chat(chat, buffer, sizeof(buffer)));

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_match_only_file_returns_false

Confirms match contexts alone never produce a reply-chat response.
=============
*/
static void test_reply_chat_match_only_file_returns_false(void)
{
	const char *path = "bot_chat_match_only_reply_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"#include \"match.h\"\n"
		"MTCONTEXT_ENTERGAME\n"
		"{\n"
		"\"match only trigger\" = (MSG_ENTERGAME, 0);\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "match_only_reply") == BLERR_NOERROR);
	drain_console(chat);
	BotLib_TestResetLastMessage();

	assert(!BotReplyChat(chat, "match only trigger"));
	assert(BotNumConsoleMessages(chat) == 0);
	assert(BotChatLength(chat) == 0);
	assert(strcmp(BotLib_TestGetLastMessage(), "") == 0);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_reply_chat_legacy_unknown_random_is_literal

An unknown name inside the legacy readable backslash syntax is ordinary text
and therefore does not trigger the retail random-table lookup or an error.
=============
*/
static void test_reply_chat_legacy_unknown_random_is_literal(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/unit_test_chat.c", "unit_random_invalid") == BLERR_NOERROR);

	seed_retail_reply_ordinal_zero(1, 1);
	drain_console(chat);
	BotLib_TestResetLastMessage();
	assert(BotReplyChat(chat, "unit-test-random-invalid"));
	assert(strcmp(BotLib_TestGetLastMessage(), "") == 0);
	assert(BotLib_TestGetLastMessageType() == 0);
	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer,
		"Random string placeholder: \\runit_test_missing\\.") == 0);

	BotDestroyChatState(chat);
}

/*
=============
test_synonym_lookup_contains_nearbyitem_entries
=============
*/
static void test_synonym_lookup_contains_nearbyitem_entries(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/rchat.c", "reply") == BLERR_NOERROR);

	assert(BotChat_HasSynonymPhrase(chat, "CONTEXT_NEARBYITEM", "Quad Damage"));
	assert(BotChat_HasSynonymPhrase(chat, "CONTEXT_NEARBYITEM",
				"Rocket Launcher"));

	BotDestroyChatState(chat);
}

/*
=============
test_known_template_is_registered
=============
*/
static void test_known_template_is_registered(void) {
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/rchat.c", "reply") == BLERR_NOERROR);

	assert(BotChat_HasReplyTemplate(chat, 1, "{VICTIM} commits suicide"));

	BotDestroyChatState(chat);
}

/*
=============
test_include_path_too_long_is_rejected
=============
*/
static void test_include_path_too_long_is_rejected(void) {
	enum {
		segment_length = 256,
		segment_count = 5,
		fragment_length = segment_count * segment_length + (segment_count - 1)
	};

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
test_retail_botloadchatfile_ignores_fastchat_and_nochat

Pins that the retail personality loader is independent of the shared chat
suppression settings.
=============
*/
static void test_retail_botloadchatfile_ignores_fastchat_and_nochat(void)
{
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	configure_chat_libvars(0.0f, 0.0f);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe") ==
		BLERR_NOERROR);
	drain_console(chat);

	configure_chat_libvars(0.0f, 1.0f);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe") ==
		BLERR_NOERROR);
	assert(BotNumConsoleMessages(chat) == 0);

	configure_chat_libvars(1.0f, 1.0f);
	drain_console(chat);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe") ==
		BLERR_NOERROR);
	assert(BotNumConsoleMessages(chat) == 0U);

	configure_chat_libvars(0.0f, 0.0f);
	free_chat_with_setup_assets(chat);
}

/*
=============
test_retail_botloadchatfile_reports_script_failure

Forces script-wrapper creation to fail and pins the ordered wrapper, missing,
and fatal load diagnostics.
=============
*/
static void test_retail_botloadchatfile_reports_script_failure(void)
{
	char expected_wrapper[256];
	char expected_inner[256];
	char expected_fatal[256];
	int written = snprintf(expected_wrapper,
		sizeof(expected_wrapper),
		"BotLoadChatFile: script wrapper failed for %s\n",
		BOT_ASSET_ROOT "/bots/babe_t.c");
	assert(written > 0);
	assert((size_t)written < sizeof(expected_wrapper));
	written = snprintf(expected_inner,
		sizeof(expected_inner),
		"couldn't find chat %s in %s\n",
		"babe",
		BOT_ASSET_ROOT "/bots/babe_t.c");
	assert(written > 0);
	assert((size_t)written < sizeof(expected_inner));
	written = snprintf(expected_fatal,
		sizeof(expected_fatal),
		"couldn't load chat %s from %s\n",
		"babe",
		BOT_ASSET_ROOT "/bots/babe_t.c");
	assert(written > 0);
	assert((size_t)written < sizeof(expected_fatal));

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	configure_chat_libvars(1.0f, 0.0f);
	drain_console(chat);
	BotLib_TestResetLastMessage();
	BotLib_TestResetMessageHistory();
	PS_TestForceCreateFailure(1);
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe") ==
		BLERR_CANNOTLOADICHAT);
	assert(BotLib_TestGetLastMessageType() == PRT_FATAL);
	assert(strcmp(BotLib_TestGetLastMessage(), expected_fatal) == 0);

	const char *history = BotLib_TestGetMessageHistory();
	const char *wrapper = strstr(history, expected_wrapper);
	assert(wrapper != NULL);
	const char *inner = strstr(wrapper + strlen(expected_wrapper), expected_inner);
	assert(inner != NULL);
	const char *fatal = strstr(inner + strlen(expected_inner), expected_fatal);
	assert(fatal != NULL);
	assert(BotNumConsoleMessages(chat) == 0U);

	configure_chat_libvars(0.0f, 0.0f);
	BotDestroyChatState(chat);
}

/*
=============
test_retail_free_chat_state_preserves_embedded_state

Pins the retail embedded-state contract: freeing a state releases its initial
tree and console queue without destroying metadata, pending text, or access to
setup-owned reply assets.
=============
*/
static void test_retail_free_chat_state_preserves_embedded_state(void)
{
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	assert(BotSetupChatAI() == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat,
		BOT_ASSET_ROOT "/bots/babe_t.c",
		"babe") == BLERR_NOERROR);
	BotSetChatName(chat, "babe");
	BotQueueConsoleMessage(chat, CMS_CHAT, "released by state free");
	BotInitialChat(chat, "enter_game", NULL);
	const unsigned int pending_length = BotChatLength(chat);
	assert(pending_length > 0U);

	assert(BotFreeChatState(chat) == 0);
	assert(BotNumInitialChats(chat, "enter_game") == 0);
	assert(BotNumConsoleMessages(chat) == 0U);
	assert(BotChatLength(chat) == pending_length);
	assert(strcmp(BotChatName(chat), "babe") == 0);

	char buffer[256];
	BotGetChatMessage(chat, buffer, sizeof(buffer));
	assert(buffer[0] != '\0');
	assert(BotReplyChat(chat, "abnormal"));
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(buffer[0] != '\0');

	BotDestroyChatState(chat);
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
}

/*
=============
test_retail_free_chat_file_releases_only_initial_tree

Pins retail ownership: unloading a personality removes only its initial-chat
tree while pending text, the console queue, metadata, and setup-owned replies
remain available.
=============
*/
static void test_retail_free_chat_file_releases_only_initial_tree(void)
{
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	assert(BotSetupChatAI() == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat,
		BOT_ASSET_ROOT "/bots/babe_t.c",
		"babe") == BLERR_NOERROR);
	BotSetChatName(chat, "babe");
	BotLib_TestSetAASTime(42.0f);
	BotQueueConsoleMessage(chat, CMS_CHAT, "queued before free");
	bot_console_message_node_t *queued = BotNextConsoleMessage(chat);
	assert(queued != NULL);
	assert(queued->time == 42.0f);

	BotInitialChat(chat, "enter_game", NULL);
	const unsigned int pending_length = BotChatLength(chat);
	assert(pending_length > 0U);
	(void)BotFreeChatFile(chat);

	assert(BotNumInitialChats(chat, "enter_game") == 0);
	assert(BotNextConsoleMessage(chat) == queued);
	assert(BotNumConsoleMessages(chat) == 1U);
	assert(BotChatLength(chat) == pending_length);
	assert(strcmp(BotChatName(chat), "babe") == 0);

	char buffer[256];
	BotGetChatMessage(chat, buffer, sizeof(buffer));
	assert(buffer[0] != '\0');
	assert(BotReplyChat(chat, "abnormal"));
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(buffer[0] != '\0');
	assert(BotRemoveConsoleMessage(chat, queued) == 0);

	BotDestroyChatState(chat);
	BotShutdownChatAI();
	BotLib_TestSetAASTime(0.0f);
	configure_chat_libvars(0.0f, 0.0f);
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
	assert(BotLoadChatFile(chat, BOT_ASSET_ROOT "/bots/babe_t.c", "babe") ==
		BLERR_NOERROR);

	drain_console(chat);
	assert(BotReplyChat(chat, "abnormal"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(buffer[0] != '\0');

	BotDestroyChatState(chat);
	BotShutdownChatAI();
}

/*
=============
test_reply_chat_uses_setup_owned_global_list

Pins the retail setup ownership: the global rchat table is the only reply
source after BotSetupChatAI; allocating a chat state does not create a
per-state reply list.
=============
*/
static void test_reply_chat_uses_setup_owned_global_list(void)
{
	const char *setup_path = "bot_chat_setup_priority_test.c";
	FILE *fp = fopen(setup_path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"global priority\"] = 1\n"
		"{\n"
		"\"setup owned\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestSetLibVarString("rchatfile", setup_path);
	assert(BotSetupChatAI() == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	seed_retail_reply_ordinal_zero(2, 1);
	drain_console(chat);
	assert(BotReplyChat(chat, "global priority"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "setup owned") == 0);

	BotDestroyChatState(chat);
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	remove(setup_path);
}

/*
=============
test_retail_unify_whitespaces_classifies_punctuation

Pins Gladiator's IsWhiteSpace table: slash, comma, and period collapse while
the explicitly enumerated punctuation remains part of a word.
=============
*/
static void test_retail_unify_whitespaces_classifies_punctuation(void)
{
	char text[] =
		"alpha/beta,gamma.delta\t(x)?'y':z[a]_b-c+d=e";
	UnifyWhiteSpaces(text);
	assert(strcmp(text,
		"alpha beta gamma delta (x)?'y':z[a]_b-c+d=e") == 0);

	char pointer_quirk[] = "a   b   c";
	UnifyWhiteSpaces(pointer_quirk);
	assert(strcmp(pointer_quirk, "a b  c") == 0);
}

/*
=============
test_retail_string_contains_word_pointer_quirks

Pins the retail pointer walk: offset zero and repeated spaces are skipped in
ways the successor's ordinary word-boundary helper does not reproduce.
=============
*/
static void test_retail_string_contains_word_pointer_quirks(void)
{
	const char *leading = " alpha";
	const char *repeated = "a  b";
	const char *single = "a b";
	const char *prefixed = "x alpha";

	assert(StringContainsWord(leading, "alpha", 0) == NULL);
	assert(StringContainsWord(repeated, "b", 0) == NULL);
	assert(StringContainsWord(single, "b", 0) == single + 2);
	assert(StringContainsWord(prefixed, "alpha", 0) == prefixed + 2);
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
	const char *contains_text = "Alpha Beta";
	assert(StringContains(contains_text, "beta", 0) == contains_text + 6);
	assert(StringContains(contains_text, "beta", 1) == NULL);
	assert(StringContainsIndex(contains_text, "beta", 0) == 6);
	assert(StringContainsIndex(contains_text, "beta", 1) == -1);

	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	assert(BotSetupChatAI() == 0);
	bot_chatstate_t *synonym_state = BotAllocChatState();
	assert(synonym_state != NULL);
	assert(BotChat_HasSynonymPhrase(synonym_state,
		"CONTEXT_NEARBYITEM",
		"Shotgun"));
	BotDestroyChatState(synonym_state);

	char synonym_text[256] = "I can't stay";
	BotReplaceSynonyms(synonym_text, CONTEXT_NORMAL_TEST);
	assert(strcmp(synonym_text, "I can not stay") == 0);

	char synonym_question[256] = "I can't?";
	BotReplaceSynonyms(synonym_question, CONTEXT_NORMAL_TEST);
	assert(strcmp(synonym_question, "I can't?") == 0);

	char leading_synonym[256] = " can't";
	BotReplaceSynonyms(leading_synonym, CONTEXT_NORMAL_TEST);
	assert(strcmp(leading_synonym, " can't") == 0);

	bot_match_t match;
	assert(BotFindMatch("Alice was railed by Bob\n",
		&match,
		MTCONTEXT_CLIENTOBITUARY_TEST));
	assert(match.type == MSG_DEATH_TEST);
	assert(match.subtype == ST_DEATH_RAILGUN_TEST);

	char variable[256];
	assert(BotMatchVariable(&match, 0, variable) == variable);
	assert(strcmp(variable, "Alice") == 0);
	assert(BotMatchVariable(&match, 1, variable) == variable);
	assert(strcmp(variable, "Bob") == 0);
	assert(BotMatchVariable(&match, 5, variable) == variable);
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
	assert(BotMatchVariable(&match, 0, variable) == variable);
	assert(strcmp(variable, "Alice") == 0);
	assert(BotMatchVariable(&match, 1, variable) == variable);
	assert(strcmp(variable, "Bob") == 0);

	assert(BotFindMatch("Alice was railed by Bob\n",
		&match,
		MTCONTEXT_CLIENTOBITUARY_TEST));
	assert(BotMatchVariable(&match, 0, variable) == variable);
	assert(strcmp(variable, "Alice") == 0);
	assert(BotMatchVariable(&match, 1, variable) == variable);
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
test_retail_enter_chat_emits_all_chat_token

Verifies a non-team retail handoff emits an EA_Say token with the passed client
and the unmodified pending message.
=============
*/
static void test_retail_enter_chat_emits_all_chat_token(void)
{
	const char *path = "bot_chat_retail_all_handoff_test.c";
	write_single_initial_chat_file(path, "all_handoff", "line", "hello all");

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "all_handoff") == BLERR_NOERROR);
	BotInitialChat(chat, "line", NULL);
	assert(BotChatLength(chat) > 0U);

	BotLib_TestResetEAChat();
	(void)BotEnterChat(chat, 2, 0);

	assert(BotLib_TestGetEASayCalls() == 1);
	assert(BotLib_TestGetEASayTeamCalls() == 0);
	assert(BotLib_TestGetEALastClient() == 2);
	assert(strcmp(BotLib_TestGetEALastText(), "hello all") == 0);
	assert(BotChatLength(chat) == 0U);

	free_chat_with_setup_assets(chat);
	remove(path);
}

/*
=============
test_setup_chat_ai_match_adjacent_string_pieces_concatenate

Pins Gladiator's match-piece parser: comma-separated string pieces are
concatenated directly, without an inserted separator.
=============
*/
static void test_setup_chat_ai_match_adjacent_string_pieces_concatenate(void)
{
	enum
	{
		MATCH_CONTEXT_TEST = 1,
		MATCH_TYPE_TEST = 9010,
		MATCH_SUBTYPE_TEST = 9011
	};

	const char *path = "bot_chat_match_adjacent_string_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"1\n"
		"{\n"
		"\"foo\", \"bar\" = (9010, 9011);\n"
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
	assert(BotFindMatch("foobar", &match, MATCH_CONTEXT_TEST));
	assert(match.type == MATCH_TYPE_TEST);
	assert(match.subtype == MATCH_SUBTYPE_TEST);
	assert(!BotFindMatch("foo bar", &match, MATCH_CONTEXT_TEST));

	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	remove(path);
}

/*
=============
test_retail_enter_chat_emits_team_chat_token

Ensures only sendto value one emits an EA_SayTeam token.
=============
*/
static void test_retail_enter_chat_emits_team_chat_token(void)
{
	const char *path = "bot_chat_retail_team_handoff_test.c";
	write_single_initial_chat_file(path, "team_handoff", "line", "hello team");

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "team_handoff") == BLERR_NOERROR);
	BotInitialChat(chat, "line", NULL);
	assert(BotChatLength(chat) > 0U);

	BotLib_TestResetEAChat();
	(void)BotEnterChat(chat, 3, 1);

	assert(BotLib_TestGetEASayCalls() == 0);
	assert(BotLib_TestGetEASayTeamCalls() == 1);
	assert(BotLib_TestGetEALastClient() == 3);
	assert(strcmp(BotLib_TestGetEALastText(), "hello team") == 0);
	assert(BotChatLength(chat) == 0U);

	free_chat_with_setup_assets(chat);
	remove(path);
}

/*
=============
test_retail_enter_chat_nonteam_ignores_compat_owner

Pins the retail sendto switch: every value other than one uses EA_Say with the
passed client, regardless of compatibility owner metadata.
=============
*/
static void test_retail_enter_chat_nonteam_ignores_compat_owner(void)
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

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "tell_owner") == BLERR_NOERROR);
	BotSetChatNameWithClient(chat, "babe", 5);
	BotInitialChat(chat, "line", NULL);
	assert(BotChatLength(chat) > 0U);

	BotLib_TestResetEAChat();
	(void)BotEnterChat(chat, 3, 2);

	assert(BotLib_TestGetEASayCalls() == 1);
	assert(BotLib_TestGetEASayTeamCalls() == 0);
	assert(BotLib_TestGetEALastClient() == 3);
	assert(strcmp(BotLib_TestGetEALastText(), "private hello") == 0);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_retail_reply_chat_enter_handoff_uses_passed_client

Checks reply construction leaves pending text for BotEnterChat, which forwards
the EA token using the entering client.
=============
*/
static void test_retail_reply_chat_enter_handoff_uses_passed_client(void)
{
	const char *path = "bot_chat_reply_handoff_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"handoff trigger\"] = 2\n"
		"{\n"
		"\"reply handoff\";\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(path);

	seed_retail_reply_ordinal_zero(1, 1);
	drain_console(chat);

	assert(BotReplyChat(chat, "handoff trigger"));
	assert(BotChatLength(chat) > 0);

	BotLib_TestResetEAChat();
	(void)BotEnterChat(chat, 4, 0);
	assert(BotLib_TestGetEASayCalls() == 1);
	assert(BotLib_TestGetEASayTeamCalls() == 0);
	assert(BotLib_TestGetEALastClient() == 4);
	assert(strcmp(BotLib_TestGetEALastText(), "reply handoff") == 0);
	assert(BotChatLength(chat) == 0);

	free_chat_with_setup_assets(chat);
	remove(path);
}

/*
=============
test_retail_initial_loader_merges_duplicate_matching_blocks

Pins the retail whole-file scan: every matching chat block contributes its
types instead of the loader returning after the first match.
=============
*/
static void test_retail_initial_loader_merges_duplicate_matching_blocks(void)
{
	const char *path = "bot_chat_duplicate_initial_blocks_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"chat \"duplicate\"\n"
		"{\n"
		"type \"first\"\n"
		"{\n"
		"\"first block\";\n"
		"}\n"
		"}\n"
		"chat \"duplicate\"\n"
		"{\n"
		"type \"second\"\n"
		"{\n"
		"\"second block\";\n"
		"}\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	assert(BotLoadChatFile(chat, path, "duplicate") == BLERR_NOERROR);
	assert(BotNumInitialChats(chat, "first") == 1);
	assert(BotNumInitialChats(chat, "second") == 1);

	char buffer[256];
	BotInitialChat(chat, "first", NULL);
	BotGetChatMessage(chat, buffer, sizeof(buffer));
	assert(strcmp(buffer, "first block") == 0);
	BotInitialChat(chat, "second", NULL);
	BotGetChatMessage(chat, buffer, sizeof(buffer));
	assert(strcmp(buffer, "second block") == 0);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_retail_initial_loader_rejects_malformed_trailing_definition

Pins that finding the requested chat does not hide a malformed later
top-level definition; the complete two-pass retail load fails atomically.
=============
*/
static void test_retail_initial_loader_rejects_malformed_trailing_definition(void)
{
	const char *path = "bot_chat_trailing_definition_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"chat \"trailing\"\n"
		"{\n"
		"type \"line\"\n"
		"{\n"
		"\"valid prefix\";\n"
		"}\n"
		"}\n"
		"malformed\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	BotLib_TestResetLastMessage();
	assert(BotLoadChatFile(chat, path, "trailing") == BLERR_CANNOTLOADICHAT);
	assert(BotLib_TestGetLastMessageType() == PRT_FATAL);
	assert(BotNumInitialChats(chat, "line") == 0);
	assert(BotChatLength(chat) == 0U);

	BotDestroyChatState(chat);
	remove(path);
}

/*
=============
test_retail_empty_reply_rule_leaks_capture_side_effects

Pins Gladiator's scan-wide match table: a later matching rule with no
responses still replaces captures used by the already selected response.
=============
*/
static void test_retail_empty_reply_rule_leaks_capture_side_effects(void)
{
	const char *path = "bot_chat_empty_reply_capture_test.c";
	remove(path);

	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"[(0, \" tail\")] = 1\n"
		"{\n"
		"}\n"
		"[(\"prefix \", 0)] = 9\n"
		"{\n"
		"\"winner \", 0;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	bot_chatstate_t *chat = alloc_chat_with_setup_reply_file(path);
	seed_retail_reply_ordinal_zero(1, 1);
	assert(BotReplyChat(chat, "prefix chosen tail"));

	char buffer[256];
	assert(take_pending_chat(chat, buffer, sizeof(buffer)));
	assert(strcmp(buffer, "winner prefix chosen") == 0);

	free_chat_with_setup_assets(chat);
	remove(path);
}

/*
=============
test_retail_empty_random_table_still_consumes_rng

Pins RandomString's endpoint behavior: a present table with zero entries
still takes its low-15-bit RNG draw before returning no expansion.
=============
*/
static void test_retail_empty_random_table_still_consumes_rng(void)
{
	const char *random_path = "bot_chat_empty_random_table_test.c";
	const char *reply_path = "bot_chat_empty_random_reply_test.c";
	remove(random_path);
	remove(reply_path);

	FILE *fp = fopen(random_path, "wb");
	assert(fp != NULL);
	fputs("empty_random = {}\n", fp);
	assert(fclose(fp) == 0);

	fp = fopen(reply_path, "wb");
	assert(fp != NULL);
	fputs(
		"[\"empty random\"] = 2\n"
		"{\n"
		"empty_random;\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	BotLib_TestSetLibVarString("rndfile", random_path);
	BotLib_TestSetLibVarString("rchatfile", reply_path);
	assert(BotSetupChatAI() == 0);
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);

	const unsigned int seed = 37U;
	srand(seed);
	(void)rand();
	(void)rand();
	const int expected_next = rand();
	srand(seed);
	BotLib_TestResetLastMessage();
	assert(BotReplyChat(chat, "empty random"));
	assert(BotLib_TestGetLastMessageType() == PRT_ERROR);
	assert(strstr(BotLib_TestGetLastMessage(),
		"unknown random string empty_random") != NULL);
	assert(rand() == expected_next);
	assert(BotChatLength(chat) == 0U);

	BotDestroyChatState(chat);
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	remove(random_path);
	remove(reply_path);
}

/*
=============
test_retail_initial_loader_reports_unique_missing_randoms

Pins the retail initial-chat load-time integrity scan and duplicate
suppression.
=============
*/
static void test_retail_initial_loader_reports_unique_missing_randoms(void)
{
	const char *path = "bot_chat_integrity_test.c";
	const char *initial_report =
		"initial_missing = {\"initial_missing\"} //MISSING RANDOM";

	remove(path);
	FILE *fp = fopen(path, "wb");
	assert(fp != NULL);
	fputs(
		"chat \"integrity\"\n"
		"{\n"
		"type \"line\"\n"
		"{\n"
		"initial_missing;\n"
		"initial_missing;\n"
		"}\n"
		"}\n",
		fp);
	assert(fclose(fp) == 0);

	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	assert(BotSetupChatAI() == 0);
	bot_chatstate_t *chat = BotAllocChatState();
	assert(chat != NULL);
	BotLib_TestResetMessageHistory();
	assert(BotLoadChatFile(chat, path, "integrity") == BLERR_NOERROR);

	const char *history = BotLib_TestGetMessageHistory();
	const char *initial = strstr(history, initial_report);
	assert(initial != NULL);
	assert(strstr(initial + strlen(initial_report), initial_report) == NULL);

	BotDestroyChatState(chat);
	BotShutdownChatAI();
	configure_chat_libvars(0.0f, 0.0f);
	remove(path);
}

/*
=============
main
=============
*/
int main(void)
{
	configure_crt_reports();
	configure_chat_libvars(0.0f, 0.0f);
	test_include_path_too_long_is_rejected();
	test_retail_enter_chat_empty_pending_is_noop();
	test_retail_initial_chat_block_drives_enter_event();
	test_retail_initial_chat_counts_raw_type_buckets();
	test_q3_compat_initial_chat_constructs_from_alias();
	test_initial_chat_pending_message_exports_and_enters();
	test_q3_compat_get_chat_message_copies_and_clears_pending();
	test_retail_enter_chat_preserves_tildes();
	test_q3_compat_get_chat_message_removes_tildes();
	test_retail_initial_chat_uses_aas_time_and_low_15_bit_rng();
	test_retail_initial_chat_missing_name_is_rejected();
	test_reply_chat_death_context();
	test_reply_chat_falls_back_to_reply_table();
	test_retail_reply_chat_name_key_is_inert();
	test_reply_chat_chooses_highest_priority_match();
	test_reply_chat_priority_uses_integer_best_value();
	test_reply_chat_equal_priority_prefers_file_last_rule();
	test_reply_chat_all_recent_reuses_retail_head();
	test_retail_reply_chat_matches_gender_keys();
	test_constructor_retail_payload_boundary();
	test_console_message_pool_uses_retail_capacity_and_fifo();
	test_console_message_retail_node_identity_and_age();
	test_console_message_pool_is_shared_and_reclaims_nodes();
	test_constructed_chat_stays_out_of_console_queue();
	test_q3_compat_enter_game_helper_dispatches_message();
	test_q3_compat_enter_game_cooldown_blocks_repeated_messages();
	test_retail_reply_chat_string_key_uses_raw_substring();
	test_q3_compat_reply_chat_string_key_uses_word_separators();
	test_retail_botloadchatfile_ignores_fastchat_and_nochat();
	test_retail_free_chat_state_preserves_embedded_state();
	test_retail_free_chat_file_releases_only_initial_tree();
	test_setup_chat_ai_loads_default_assets();
	test_setup_chat_ai_skips_reply_when_nochat_enabled();
	test_setup_chat_ai_supplies_shared_reply_fallback();
	test_reply_chat_uses_setup_owned_global_list();
	test_retail_unify_whitespaces_classifies_punctuation();
	test_retail_string_contains_word_pointer_quirks();
	test_setup_chat_ai_exports_match_and_synonym_utilities();
	test_setup_chat_ai_match_string_alternatives_capture_variables();
	test_setup_chat_ai_match_adjacent_string_pieces_concatenate();
	test_retail_enter_chat_emits_all_chat_token();
	test_retail_enter_chat_emits_team_chat_token();
	test_retail_enter_chat_nonteam_ignores_compat_owner();
	test_retail_reply_chat_enter_handoff_uses_passed_client();
	test_retail_initial_loader_merges_duplicate_matching_blocks();
	test_retail_initial_loader_rejects_malformed_trailing_definition();
	test_retail_empty_reply_rule_leaks_capture_side_effects();
	test_retail_empty_random_table_still_consumes_rng();
	test_retail_initial_loader_reports_unique_missing_randoms();
	test_reply_chat_selected_constructor_failure_returns_true();
	test_q3_compat_reply_chat_name_key_matches_configured_name();

	printf("bot_chat_tests: all checks passed\n");
	return 0;
}

