#include "ai_chat.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "botlib/aas/aas_map.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/ea/ea_local.h"

#define BOT_CHAT_MAX_MESSAGE_CHARS BOT_CONSOLE_MESSAGE_STORAGE_CHARS
#define BOT_CHAT_MAX_MATCH_VARIABLES BOT_MATCH_MAX_VARIABLES
#define BOT_CHAT_RETAIL_MATCH_VARIABLES 10U
#define BOT_CHAT_RETAIL_MESSAGE_STORAGE_CHARS BOT_CONSOLE_MESSAGE_STORAGE_CHARS
#define BOT_CHAT_RETAIL_MESSAGE_PAYLOAD_CHARS 0x96U
#define BOT_CHAT_MAX_PATH_CHARS 1024
#define BOT_CHAT_MAX_TOKEN_CHARS 64
#define BOT_CHAT_MAX_TOKENS 64
#define BOT_CHAT_ESCAPE_CHAR '\x01'
#define BOT_CHAT_MATCH_ALT_START '\x1f'
#define BOT_CHAT_MATCH_ALT_SEPARATOR '\x1e'
#define BOT_CHAT_MATCH_ALT_END '\x1d'
#define BOT_CHAT_MATCH_PIECE_SEPARATOR '\x1c'
#define BOT_CHAT_CONTEXT_DEATH 1
#define BOT_CHAT_CONTEXT_ENTERGAME 2
#define BOT_CHAT_CONTEXT_KILL 25
#define BOT_CHAT_CONTEXT_ENEMYSUICIDE 34
#define BOT_CHAT_CONTEXT_HITTALKING 35
#define BOT_CHAT_CONTEXT_HITNODEATH 36
#define BOT_CHAT_CONTEXT_HITNOKILL 37
#define BOT_CHAT_CONTEXT_RANDOM 38
#define BOT_CHAT_CONTEXT_INSULT 39
#define BOT_CHAT_CONTEXT_PRAISE 40

enum
{
	BOT_CHAT_SENDTO_ALL = 0,
	BOT_CHAT_SENDTO_TEAM = 1,
	BOT_CHAT_SENDTO_TELL = 2
};

typedef struct {
    char *text;
    float weight;
} bot_synonym_phrase_t;

typedef struct {
    bot_synonym_phrase_t *phrases;
    size_t phrase_count;
    size_t phrase_capacity;
} bot_synonym_group_t;

typedef struct {
    char *context_name;
	unsigned long context_mask;
    bot_synonym_group_t *groups;
    size_t group_count;
    size_t group_capacity;
} bot_synonym_context_t;

typedef struct {
    unsigned long message_type;
    char **templates;
	unsigned long *template_match_contexts;
	unsigned long *template_subtypes;
    size_t template_count;
    size_t template_capacity;
} bot_match_context_t;

typedef struct {
	char *template_text;
	unsigned long match_context;
	unsigned long message_type;
	unsigned long subtype;
} bot_match_order_entry_t;

typedef struct {
    unsigned long reply_context;
	float priority;
	struct bot_reply_key_s *keys;
	size_t key_count;
	size_t key_capacity;
    char **responses;
	float *response_times;
    size_t response_count;
    size_t response_capacity;
} bot_reply_rule_t;

typedef struct bot_reply_key_s {
	char *pattern;
	int is_pattern;
	int required;
	int negated;
	int special;
} bot_reply_key_t;

typedef struct {
	const char *message;
	size_t message_length;
	const char *variables[BOT_CHAT_MAX_MATCH_VARIABLES];
	size_t lengths[BOT_CHAT_MAX_MATCH_VARIABLES];
} bot_reply_match_t;

enum
{
	BOT_CHAT_REPLY_KEY_TEXT = 0,
	BOT_CHAT_REPLY_KEY_NAME,
	BOT_CHAT_REPLY_KEY_GENDER_FEMALE,
	BOT_CHAT_REPLY_KEY_GENDER_MALE,
	BOT_CHAT_REPLY_KEY_GENDERLESS,
	BOT_CHAT_REPLY_KEY_BOTNAMES
};

typedef struct {
	bot_reply_key_t *keys;
	size_t key_count;
	size_t key_capacity;
} bot_reply_key_list_t;

typedef struct {
    bot_reply_rule_t *rules;
    size_t rule_count;
    size_t rule_capacity;
} bot_reply_table_t;

typedef struct {
	char *name;
	char **entries;
	size_t entry_count;
	size_t entry_capacity;
} bot_random_string_table_t;

typedef struct {
	char *type_name;
	char **templates;
	float *template_times;
	size_t template_count;
	size_t template_capacity;
} bot_initial_chat_type_t;

typedef struct {
	char **names;
	size_t count;
	size_t capacity;
} bot_missing_random_list_t;

typedef struct {
    char *buffer;
    size_t length;
    size_t capacity;
} bot_string_builder_t;

typedef struct {
unsigned long context;
double duration_seconds;
double next_allowed_time;
} bot_chat_cooldown_entry_t;

typedef struct {
double next_allowed_time;
} bot_chat_client_cooldown_t;

typedef struct {
	const char *name;
	const char *const *entries;
	size_t entry_count;
} bot_chat_random_table_t;

/*
=============
BotChat_PrintLegacyDiagnostic

Prints the legacy chat diagnostic.
=============
*/
static void BotChat_PrintLegacyDiagnostic(bot_chatstate_t *state,
		int priority,
		int fastchat_enabled,
		const char *format,
		const char *chatname,
		const char *chatfile)
{
	if (format == NULL || chatname == NULL || chatfile == NULL)
	{
		return;
	}

	(void)state;
	(void)fastchat_enabled;
	BotLib_Print(priority, format, chatname, chatfile);
}

struct bot_chatstate_s {
	pc_source_t *active_source;
	pc_script_t *active_script;
	char active_chatfile[128];
	char active_chatname[64];
	bot_console_message_node_t *console_first;
	bot_console_message_node_t *console_last;
	size_t console_count;
	char chat_message[BOT_CHAT_MAX_MESSAGE_CHARS];
	unsigned long chat_message_context;
	char chat_name[16];
	int chat_client;
	int chat_client_valid;
	int chat_gender;

	bot_synonym_context_t *synonym_contexts;
	size_t synonym_context_count;

	bot_match_context_t *match_contexts;
	size_t match_context_count;
	bot_match_order_entry_t *match_order;
	size_t match_order_count;

	bot_reply_table_t replies;
	int has_reply_chats;

	bot_random_string_table_t *random_tables;
	size_t random_table_count;

	bot_initial_chat_type_t *initial_types;
	size_t initial_type_count;

	bot_chat_cooldown_entry_t *cooldowns;
	size_t cooldown_count;
	size_t cooldown_capacity;

	bot_chat_client_cooldown_t *client_cooldowns;
	size_t client_cooldown_count;
	double time_override_seconds;
	int has_time_override;
	int speaking_client;
	struct bot_chatstate_s *console_registry_prev;
	struct bot_chatstate_s *console_registry_next;
};

static bot_chatstate_t *bot_chat_setup_state;
static bot_console_message_node_t *bot_console_message_heap;
static bot_console_message_node_t *bot_free_console_messages;
static size_t bot_console_message_capacity;
static bot_chatstate_t *bot_console_message_states;
static bot_chatstate_t bot_retail_chat_states[MAX_CLIENTS + 1];
static bool bot_retail_chat_state_used[MAX_CLIENTS + 1];

static void BotChat_ResetConsoleQueue(bot_chatstate_t *state);

/*
=============
BotChat_RetailRandomFloat

Returns Gladiator's inclusive low-fifteen-bit random fraction. A maximum rand
sample is deliberately 1.0f, so count-based retail selections can miss their
list endpoint.
=============
*/
static float BotChat_RetailRandomFloat(void)
{
	return (float)(rand() & 0x7fff) * 3.05185094e-05f;
}

#define BOT_CHAT_MIN_INTERVAL_SECONDS 25.0
#define BOT_CHAT_MESSAGE_RECENT_SECONDS 20.0

/*
=============
BotChat_SetupFallbackState

Returns the shared setup cache when a per-bot chat state needs global assets.
=============
*/
static const bot_chatstate_t *BotChat_SetupFallbackState(const bot_chatstate_t *state)
{
	if (bot_chat_setup_state == NULL || bot_chat_setup_state == state)
	{
		return NULL;
	}

	return bot_chat_setup_state;
}

/*
=============
BotChat_CurrentTimeSeconds

Returns the synthetic clock time for cooldown evaluation.
=============
*/
static double BotChat_CurrentTimeSeconds(const bot_chatstate_t *state)
{
	if (state != NULL && state->has_time_override)
	{
		return state->time_override_seconds;
	}

	/*
	 * The library's time base is the frame clock the host advances through
	 * BotStartFrame, which is what every other timed decision in this module
	 * samples.  Reading a wall clock here made chat cooldowns depend on how
	 * long the host took between frames, so identical frame sequences could
	 * take different chat paths from run to run.
	 */
	return (double)AAS_Time();
}

/*
=============
BotChat_FastChatEnabled

Queries the libvar controlling fast chat timing adjustments.
=============
*/
static int BotChat_FastChatEnabled(void)
{
	return LibVarValue("fastchat", "0") != 0.0f;
}

/*
=============
BotChat_TestInitialChatEnabled

Checks the retail bot_testichat libvar used to print initial chat probes.
=============
*/
static int BotChat_TestInitialChatEnabled(void)
{
	return LibVarValue("bot_testichat", "0") != 0.0f;
}

/*
=============
BotChat_TestReplyChatEnabled

Checks the retail bot_testrchat libvar used to dump reply candidates.
=============
*/
static int BotChat_TestReplyChatEnabled(void)
{
	return LibVarValue("bot_testrchat", "0") != 0.0f;
}

/*
=============
BotChat_PrintTestMessage

Prints a retail chat test-mode line.
=============
*/
static void BotChat_PrintTestMessage(bot_chatstate_t *state, const char *format, ...)
{
	if (format == NULL)
	{
		return;
	}

	char message[BOT_CHAT_MAX_MESSAGE_CHARS];
	va_list args;
	va_start(args, format);
	int written = vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	if (written < 0)
	{
		return;
	}
	message[sizeof(message) - 1] = '\0';

	BotLib_Print(PRT_MESSAGE, "%s", message);
	(void)state;
}

/*
=============
BotChat_MinimumIntervalSeconds

Returns the minimum delay enforced between bot chats, scaling down when
fastchat is enabled to accelerate testing.
=============
*/
static double BotChat_MinimumIntervalSeconds(void)
{
	return BotChat_FastChatEnabled() ? 0.0 : BOT_CHAT_MIN_INTERVAL_SECONDS;
}

/*
=============
BotChat_MaxClients

Looks up the maxclients libvar to bound per-bot cooldown tracking.
=============
*/
static size_t BotChat_MaxClients(void)
{
	const double value = LibVarValue("maxclients", "4");
	if (value < 0.0)
{
	return 0;
}

	return (size_t)value;
}

/*
=============
BotChat_GetClientCooldownSlot

Ensures the per-bot cooldown array can track the supplied client index.
=============
*/
static bot_chat_client_cooldown_t *BotChat_GetClientCooldownSlot(bot_chatstate_t *state,
	size_t client)
{
	if (state == NULL)
{
	return NULL;
}

	if (client >= state->client_cooldown_count)
{
	size_t capacity = state->client_cooldown_count ? state->client_cooldown_count : 4;
	while (capacity <= client)
{
	capacity *= 2;
}

	bot_chat_client_cooldown_t *slots = realloc(state->client_cooldowns, capacity * sizeof(*slots));
	if (slots == NULL)
{
	return NULL;
}

	for (size_t i = state->client_cooldown_count; i < capacity; ++i)
{
	slots[i].next_allowed_time = 0.0;
}

	state->client_cooldowns = slots;
	state->client_cooldown_count = capacity;
}

	return &state->client_cooldowns[client];
}

/*
=============
BotChat_ClientCooldownBlocks

Applies the per-bot cooldown guardrail to prevent rapid consecutive chats.
=============
*/
static int BotChat_ClientCooldownBlocks(bot_chatstate_t *state,
        size_t client,
        double now_seconds)
{
        const double min_interval = BotChat_MinimumIntervalSeconds();
        bot_chat_client_cooldown_t *slot = BotChat_GetClientCooldownSlot(state, client);
        if (slot == NULL || min_interval <= 0.0)
	{
		return 0;
	}

	if (slot->next_allowed_time > now_seconds)
	{
		return 1;
	}

	return 0;
}

/*
=============
BotChat_CommitClientCooldown

Reserves the per-client chat interval after a message is constructed.
=============
*/
static void BotChat_CommitClientCooldown(bot_chatstate_t *state,
	size_t client,
	double now_seconds)
{
	const double min_interval = BotChat_MinimumIntervalSeconds();
	bot_chat_client_cooldown_t *slot = BotChat_GetClientCooldownSlot(state, client);
	if (slot == NULL)
	{
		return;
	}

	slot->next_allowed_time = (min_interval > 0.0) ?
		now_seconds + min_interval :
		now_seconds;
}

static int BotChat_CooldownBlocks(bot_chatstate_t *state,
		unsigned long context,
		double now_seconds);

/*
=============
BotChat_EventAllowed

Gates chat execution on nochat, client bounds, and context cooldowns while logging
failures for diagnostics.
=============
*/
static int BotChat_EventAllowed(bot_chatstate_t *state,
	int client,
	unsigned long context,
	double now_seconds)
{
	if (LibVarValue("nochat", "0") != 0.0f)
	{
		const char *message = "chatting disabled by nochat\n";
		BotLib_Print(PRT_MESSAGE, "%s", message);
		return 0;
	}

	const size_t max_clients = BotChat_MaxClients();
	if (client < 0 || (max_clients > 0 && (size_t)client >= max_clients))
	{
		char buffer[BOT_CHAT_MAX_MESSAGE_CHARS];
		snprintf(buffer,
			sizeof(buffer),
			"client %d outside chat bounds (max %zu)\n",
			client,
			max_clients);
		BotLib_Print(PRT_WARNING, "%s", buffer);
		return 0;
	}

	if (BotChat_CooldownBlocks(state, context, now_seconds))
	{
		return 0;
	}

	return 1;
}

/*
=============
BotChat_FindCooldownEntry

Finds (or optionally creates) the cooldown entry for a context.
=============
*/
static bot_chat_cooldown_entry_t *BotChat_FindCooldownEntry(bot_chatstate_t *state,
unsigned long context,
int create)
{
	if (state == NULL)
	{
		return NULL;
	}

	for (size_t i = 0; i < state->cooldown_count; ++i)
	{
		bot_chat_cooldown_entry_t *entry = &state->cooldowns[i];
		if (entry->context == context)
		{
			return entry;
		}
	}

	if (!create)
	{
		return NULL;
	}

	size_t capacity = state->cooldown_capacity ? state->cooldown_capacity : 4;
	while (capacity <= state->cooldown_count)
	{
		capacity *= 2;
	}
	bot_chat_cooldown_entry_t *entries = realloc(state->cooldowns, capacity * sizeof(*entries));
	if (entries == NULL)
	{
		return NULL;
	}
	state->cooldowns = entries;
	state->cooldown_capacity = capacity;
	bot_chat_cooldown_entry_t *entry = &state->cooldowns[state->cooldown_count++];
	memset(entry, 0, sizeof(*entry));
	entry->context = context;
	return entry;
}

/*
=============
BotChat_ReportCooldown

Retains the compatibility cooldown hook without injecting synthetic inbound
console messages.
=============
*/
static void BotChat_ReportCooldown(bot_chatstate_t *state,
unsigned long context,
double seconds_remaining)
{
	(void)state;
	(void)context;
	(void)seconds_remaining;
}

/*
=============
BotChat_CooldownBlocks

Updates and evaluates cooldown timers for a context.
=============
*/
static int BotChat_CooldownBlocks(bot_chatstate_t *state,
unsigned long context,
	double now_seconds)
{
	bot_chat_cooldown_entry_t *entry = BotChat_FindCooldownEntry(state, context, 0);
	if (entry == NULL || entry->duration_seconds <= 0.0)
	{
		return 0;
	}

	if (entry->next_allowed_time > now_seconds)
	{
		BotChat_ReportCooldown(state, context, entry->next_allowed_time - now_seconds);
		return 1;
	}

	entry->next_allowed_time = now_seconds + entry->duration_seconds;
	return 0;
}

/*
=============
BotChat_SelectRandomTemplate

Picks a template from the context using the hashing helper.
=============
*/
static size_t BotChat_SelectIndex(const char *seed, size_t count);
static size_t BotChat_SelectRecentIndex(const char *seed,
	const float *times,
	size_t count,
	double now_seconds,
	int fallback_oldest,
	int *selected_available);
static void BotChat_MarkRecent(float *times,
	size_t index,
	size_t count,
	double now_seconds);
static const char *BotChat_SelectRandomTemplate(const bot_chatstate_t *state,
	const bot_match_context_t *context,
	const char *seed)
{
	(void)state;
	if (context == NULL || context->template_count == 0)
	{
		return NULL;
	}

	size_t index = BotChat_SelectIndex(seed, context->template_count);
	if (index >= context->template_count)
	{
		return NULL;
	}
	return context->templates[index];
}

/*
=============
BotChat_TokenizeText

Splits the provided text into lower-case tokens separated by non-alphanumeric
characters.
=============
*/
static size_t BotChat_TokenizeText(const char *text,
char tokens[][BOT_CHAT_MAX_TOKEN_CHARS],
size_t max_tokens)
{
	if (text == NULL || max_tokens == 0)
	{
		return 0;
	}

	size_t count = 0;
	size_t length = 0;
	char buffer[BOT_CHAT_MAX_TOKEN_CHARS];

	for (const char *ptr = text; *ptr != '\0'; ++ptr)
	{
		if (isalnum((unsigned char)*ptr) || *ptr == '_')
		{
			if (length + 1 < sizeof(buffer))
			{
				buffer[length++] = (char)tolower((unsigned char)*ptr);
			}
			continue;
		}

		if (length == 0)
		{
			continue;
		}

		buffer[length] = '\0';
		strncpy(tokens[count], buffer, BOT_CHAT_MAX_TOKEN_CHARS - 1);
		tokens[count][BOT_CHAT_MAX_TOKEN_CHARS - 1] = '\0';
		length = 0;
		if (++count == max_tokens)
		{
			return count;
		}
	}

	if (length > 0 && count < max_tokens)
	{
		buffer[length] = '\0';
		strncpy(tokens[count], buffer, BOT_CHAT_MAX_TOKEN_CHARS - 1);
		tokens[count][BOT_CHAT_MAX_TOKEN_CHARS - 1] = '\0';
		++count;
	}

	return count;
}

/*
=============
BotChat_FindSynonymContextByToken

Locates the synonym context whose suffix matches the provided identifier.
=============
*/
static const bot_synonym_context_t *BotChat_FindSynonymContextByToken(
	const bot_chatstate_t *state,
	const char *token)
{
	if (state == NULL || token == NULL)
	{
		return NULL;
	}

	char token_upper[BOT_CHAT_MAX_TOKEN_CHARS];
	size_t token_index = 0;
	for (; token[token_index] != '\0' && token_index + 1 < sizeof(token_upper); ++token_index)
	{
		token_upper[token_index] = (char)toupper((unsigned char)token[token_index]);
	}
	token_upper[token_index] = '\0';

	const bot_chatstate_t *states[2] = {
		state,
		BotChat_SetupFallbackState(state)
	};

	for (size_t state_index = 0; state_index < sizeof(states) / sizeof(states[0]); ++state_index)
	{
		const bot_chatstate_t *source = states[state_index];
		if (source == NULL)
		{
			continue;
		}

		for (size_t i = 0; i < source->synonym_context_count; ++i)
		{
			const bot_synonym_context_t *context = &source->synonym_contexts[i];
			if (context->context_name == NULL)
			{
				continue;
			}

			const char *name = context->context_name;
			if (strncmp(name, "CONTEXT_", 8) == 0)
			{
				name += 8;
			}

			char context_upper[BOT_CHAT_MAX_TOKEN_CHARS];
			size_t context_index = 0;
			for (; name[context_index] != '\0' && context_index + 1 < sizeof(context_upper); ++context_index)
			{
				context_upper[context_index] = (char)toupper((unsigned char)name[context_index]);
			}
			context_upper[context_index] = '\0';

			if (strcmp(context_upper, token_upper) == 0)
			{
				return context;
			}
		}
	}

	return NULL;
}

/*
=============
BotChat_MessageContainsPhrase

Verifies that the message tokens contain the provided phrase starting at or
after the supplied index.
=============
*/
static int BotChat_MessageContainsPhrase(
	const char message_tokens[][BOT_CHAT_MAX_TOKEN_CHARS],
size_t message_count,
size_t start_index,
	const char phrase_tokens[][BOT_CHAT_MAX_TOKEN_CHARS],
size_t phrase_count,
size_t *next_index)
{
	if (phrase_count == 0 || message_count == 0)
	{
		return 0;
	}

	for (size_t i = start_index; i + phrase_count <= message_count; ++i)
	{
		int matches = 1;
		for (size_t j = 0; j < phrase_count; ++j)
		{
			if (strcmp(message_tokens[i + j], phrase_tokens[j]) != 0)
			{
				matches = 0;
				break;
			}
		}
		if (matches)
		{
			if (next_index != NULL)
			{
				*next_index = i + phrase_count;
			}
			return 1;
		}
	}

	return 0;
}

/*
=============
BotChat_TemplateMatchesMessage

Returns non-zero when the supplied message satisfies the match template.
=============
*/
static int BotChat_TemplateMatchesMessage(const bot_chatstate_t *state,
	const char *template_text,
	const char *message)
{
	char template_tokens[BOT_CHAT_MAX_TOKENS][BOT_CHAT_MAX_TOKEN_CHARS];
	char message_tokens[BOT_CHAT_MAX_TOKENS][BOT_CHAT_MAX_TOKEN_CHARS];

	const size_t template_count = BotChat_TokenizeText(template_text,
		template_tokens,
		BOT_CHAT_MAX_TOKENS);
	const size_t message_count = BotChat_TokenizeText(message,
		message_tokens,
		BOT_CHAT_MAX_TOKENS);

	if (template_count == 0 || message_count == 0)
	{
		return 0;
	}

	size_t message_index = 0;
	for (size_t i = 0; i < template_count; ++i)
	{
		const bot_synonym_context_t *context =
		BotChat_FindSynonymContextByToken(state, template_tokens[i]);
		if (context == NULL)
		{
			size_t match_position = message_index;
			int found = 0;
			while (match_position < message_count)
			{
				if (strcmp(template_tokens[i], message_tokens[match_position]) == 0)
				{
					message_index = match_position + 1;
					found = 1;
					break;
				}
				++match_position;
			}
			if (!found)
			{
				return 0;
			}
			continue;
		}

		int matched_synonym = 0;
		for (size_t group_index = 0; group_index < context->group_count; ++group_index)
		{
			const bot_synonym_group_t *group = &context->groups[group_index];
			for (size_t phrase_index = 0; phrase_index < group->phrase_count; ++phrase_index)
			{
				const bot_synonym_phrase_t *phrase = &group->phrases[phrase_index];
				if (phrase->text == NULL)
				{
					continue;
				}

				char phrase_tokens[BOT_CHAT_MAX_TOKENS][BOT_CHAT_MAX_TOKEN_CHARS];
				size_t phrase_count = BotChat_TokenizeText(phrase->text,
				phrase_tokens,
				BOT_CHAT_MAX_TOKENS);
				if (phrase_count == 0)
				{
					continue;
				}

				size_t next_index = message_index;
				if (BotChat_MessageContainsPhrase(message_tokens,
				message_count,
				next_index,
				phrase_tokens,
				phrase_count,
				&next_index))
				{
					message_index = next_index;
					matched_synonym = 1;
					break;
				}
			}
			if (matched_synonym)
			{
				break;
			}
		}

		if (!matched_synonym)
		{
			return 0;
		}
	}

	return 1;
}

/*
=============
BotChat_RandomStringKnown

Checks if a referenced random table identifier is recognised.
=============
*/
static char *BotChat_StringDuplicate(const char *text);
static const bot_random_string_table_t *BotChat_FindStateRandomTable(
	const bot_chatstate_t *state,
	const char *name);

static int BotChat_RandomStringKnown(const bot_chatstate_t *state, const char *name)
{
	return name != NULL && BotChat_FindStateRandomTable(state, name) != NULL;
}

static const char *const kBotChatRandomMisc[] = {
	"woohoo",
	"whoopass",
	"hmmmm"
};

static const char *const kBotChatRandomInsult[] = {
	"lamer",
	"loser",
	"sucker"
};

static const bot_chat_random_table_t kBotChatRandomTables[] = {
	{ "random_misc", kBotChatRandomMisc, sizeof(kBotChatRandomMisc) / sizeof(kBotChatRandomMisc[0]) },
	{ "random_insult", kBotChatRandomInsult, sizeof(kBotChatRandomInsult) / sizeof(kBotChatRandomInsult[0]) }
};

/*
=============
BotChat_FindBuiltinRandomTable

Looks up a built-in random string table by name.
=============
*/
static const bot_chat_random_table_t *BotChat_FindBuiltinRandomTable(const char *name)
{
	if (name == NULL)
	{
	return NULL;
	}
	
	for (size_t i = 0; i < sizeof(kBotChatRandomTables) / sizeof(kBotChatRandomTables[0]); ++i)
	{
	if (strcmp(kBotChatRandomTables[i].name, name) == 0)
	{
	return &kBotChatRandomTables[i];
	}
	}
	
	return NULL;
	}

/*
=============
BotChat_FindStateRandomTable

Looks up a random string table parsed from the active chat assets.
=============
*/
static const bot_random_string_table_t *BotChat_FindStateRandomTable(
	const bot_chatstate_t *state,
	const char *name)
{
	(void)state;
	if (bot_chat_setup_state == NULL || name == NULL)
	{
		return NULL;
	}

	const bot_chatstate_t *source = bot_chat_setup_state;

	for (size_t i = 0; i < source->random_table_count; ++i)
	{
		const bot_random_string_table_t *table = &source->random_tables[i];
		if (table->name != NULL && strcmp(table->name, name) == 0)
		{
			return table;
		}
	}

	return NULL;
}

/*
=============
BotChat_SelectRandomFromTable

Chooses a random entry from the provided random string table.
=============
*/
static const char *BotChat_SelectRandomFromTable(const bot_chat_random_table_t *table)
	{
	if (table == NULL)
	{
	return NULL;
	}
	
	const size_t index = (size_t)(BotChat_RetailRandomFloat()
		* (float)table->entry_count);
	if (table->entries == NULL || index >= table->entry_count)
	{
		return NULL;
	}
	return table->entries[table->entry_count - index - 1U];
	}

/*
=============
BotChat_SelectRandomFromStateTable

Chooses a random entry from a parsed random string table.
=============
*/
static const char *BotChat_SelectRandomFromStateTable(
	const bot_random_string_table_t *table)
{
	if (table == NULL)
	{
		return NULL;
	}

	const size_t index = (size_t)(BotChat_RetailRandomFloat()
		* (float)table->entry_count);
	if (table->entries == NULL || index >= table->entry_count)
	{
		return NULL;
	}
	return table->entries[table->entry_count - index - 1U];
}
	
/*
=============
BotChat_SelectWeightedSynonym

Returns a synonym from the specified context using weighted selection.
=============
*/
static const char *BotChat_SelectWeightedSynonym(const bot_synonym_context_t *context)
	{
	if (context == NULL)
	{
	return NULL;
	}

	double total_weight = 0.0;
	for (size_t group_index = 0; group_index < context->group_count; ++group_index)
	{
	const bot_synonym_group_t *group = &context->groups[group_index];
	for (size_t phrase_index = 0; phrase_index < group->phrase_count; ++phrase_index)
	{
	const bot_synonym_phrase_t *phrase = &group->phrases[phrase_index];
	if (phrase->text == NULL)
	{
	continue;
	}

	double weight = phrase->weight;
	if (weight <= 0.0)
	{
	weight = 1.0;
	}
	total_weight += weight;
	}
	}

	if (total_weight <= 0.0)
	{
	return NULL;
	}

	double roll = ((double)rand() / ((double)RAND_MAX + 1.0)) * total_weight;
	for (size_t group_index = 0; group_index < context->group_count; ++group_index)
	{
	const bot_synonym_group_t *group = &context->groups[group_index];
	for (size_t phrase_index = 0; phrase_index < group->phrase_count; ++phrase_index)
	{
	const bot_synonym_phrase_t *phrase = &group->phrases[phrase_index];
	if (phrase->text == NULL)
	{
	continue;
	}

	double weight = phrase->weight;
	if (weight <= 0.0)
	{
	weight = 1.0;
	}
	if (roll < weight)
	{
	return phrase->text;
	}
	roll -= weight;
	}
	}

	return NULL;
}

/*
=============
BotChat_SelectRandomString

Expands a random string reference from the setup-owned retail table.
=============
*/
static const char *BotChat_SelectRandomString(const bot_chatstate_t *state,
	const char *name)
{
	return BotChat_SelectRandomFromStateTable(
		BotChat_FindStateRandomTable(state, name));
}

/*
=============
BotChat_ShutdownConsoleMessageHeap

Releases the shared retail console-message pool.
=============
*/
static void BotChat_ShutdownConsoleMessageHeap(void)
{
	for (bot_chatstate_t *state = bot_console_message_states;
		state != NULL;
		state = state->console_registry_next)
	{
		BotChat_ResetConsoleQueue(state);
	}

	if (bot_console_message_heap != NULL)
	{
		FreeMemory(bot_console_message_heap);
	}

	bot_console_message_heap = NULL;
	bot_free_console_messages = NULL;
	bot_console_message_capacity = 0U;
}

/*
=============
BotChat_InitConsoleMessageHeap

Builds the shared max_messages pool and its doubly linked free list.
=============
*/
static int BotChat_InitConsoleMessageHeap(void)
{
	BotChat_ShutdownConsoleMessageHeap();

	const float configured_messages = LibVarValue("max_messages", "1024");
	/* HOST SAFETY: retail trusts this count and can overflow its allocation. */
	if (!(configured_messages >= 1.0f)
		|| (double)configured_messages >= (double)INT_MAX)
	{
		return 0;
	}

	const size_t message_count = (size_t)((int)configured_messages);
	if (message_count == 0U
		|| message_count > (size_t)-1 / sizeof(*bot_console_message_heap))
	{
		return 0;
	}

	bot_console_message_heap = GetClearedMemory(message_count
		* sizeof(*bot_console_message_heap));
	if (bot_console_message_heap == NULL)
	{
		return 0;
	}

	for (size_t i = 0; i < message_count; ++i)
	{
		bot_console_message_heap[i].prev = i > 0U
			? &bot_console_message_heap[i - 1U]
			: NULL;
		bot_console_message_heap[i].next = i + 1U < message_count
			? &bot_console_message_heap[i + 1U]
			: NULL;
	}

	bot_free_console_messages = bot_console_message_heap;
	bot_console_message_capacity = message_count;
	return 1;
}

/*
=============
BotChat_AllocConsoleMessage

Removes and returns the first free retail console-message node.
=============
*/
static bot_console_message_node_t *BotChat_AllocConsoleMessage(void)
{
	bot_console_message_node_t *message = bot_free_console_messages;
	if (message == NULL)
	{
		return NULL;
	}

	bot_free_console_messages = message->next;
	if (bot_free_console_messages != NULL)
	{
		bot_free_console_messages->prev = NULL;
	}

	return message;
}

/*
=============
BotChat_FreeConsoleMessage

Returns a console-message node to the head of the shared free list.
=============
*/
static void BotChat_FreeConsoleMessage(bot_console_message_node_t *message)
{
	if (message == NULL)
	{
		return;
	}

	if (bot_free_console_messages != NULL)
	{
		bot_free_console_messages->prev = message;
	}
	message->prev = NULL;
	message->next = bot_free_console_messages;
	bot_free_console_messages = message;
}

/*
=============
BotChat_UnlinkConsoleMessage

Unlinks one active node from a chat state and recycles its pool slot.
=============
*/
static void BotChat_UnlinkConsoleMessage(bot_chatstate_t *state,
	bot_console_message_node_t *message)
{
	if (state == NULL || message == NULL)
	{
		return;
	}

	if (message->next != NULL)
	{
		message->next->prev = message->prev;
	}
	else
	{
		state->console_last = message->prev;
	}

	if (message->prev != NULL)
	{
		message->prev->next = message->next;
	}
	else
	{
		state->console_first = message->next;
	}

	BotChat_FreeConsoleMessage(message);
	if (state->console_count > 0U)
	{
		--state->console_count;
	}
}

/*
=============
BotChat_ResetConsoleQueue

Returns every queued node owned by a chat state to the shared pool.
=============
*/
static void BotChat_ResetConsoleQueue(bot_chatstate_t *state)
{
	if (state == NULL)
	{
		return;
	}

	while (state->console_first != NULL)
	{
		BotChat_UnlinkConsoleMessage(state, state->console_first);
	}
	state->console_last = NULL;
	state->console_count = 0U;
}

/*
=============
BotChat_RegisterConsoleMessageState

Tracks a live chat state so pool shutdown can detach every queued node before
the shared storage is released.
=============
*/
static void BotChat_RegisterConsoleMessageState(bot_chatstate_t *state)
{
	if (state == NULL)
	{
		return;
	}

	state->console_registry_prev = NULL;
	state->console_registry_next = bot_console_message_states;
	if (bot_console_message_states != NULL)
	{
		bot_console_message_states->console_registry_prev = state;
	}
	bot_console_message_states = state;
}

/*
=============
BotChat_UnregisterConsoleMessageState

Removes a chat state from the live pool-owner registry before it is freed.
=============
*/
static void BotChat_UnregisterConsoleMessageState(bot_chatstate_t *state)
{
	if (state == NULL)
	{
		return;
	}

	if (state->console_registry_prev != NULL)
	{
		state->console_registry_prev->console_registry_next =
			state->console_registry_next;
	}
	else if (bot_console_message_states == state)
	{
		bot_console_message_states = state->console_registry_next;
	}

	if (state->console_registry_next != NULL)
	{
		state->console_registry_next->console_registry_prev =
			state->console_registry_prev;
	}
	state->console_registry_prev = NULL;
	state->console_registry_next = NULL;
}

/*
=============
BotChat_ClearPendingMessage

Clears the constructed chat text that retail keeps until BotEnterChat.
=============
*/
static void BotChat_ClearPendingMessage(bot_chatstate_t *state)
{
	if (state == NULL)
	{
		return;
	}

	state->chat_message[0] = '\0';
	state->chat_message_context = 0;
}

/*
=============
BotChat_RemoveTildes

Removes the retail chat spacing marker before text leaves the chat state.
=============
*/
static void BotChat_RemoveTildes(char *message)
{
	if (message == NULL)
	{
		return;
	}

	for (size_t i = 0; message[i] != '\0';)
	{
		if (message[i] != '~')
		{
			++i;
			continue;
		}

		memmove(&message[i], &message[i + 1], strlen(&message[i + 1]) + 1);
	}
}

/*
=============
BotChat_IsRetailWhitespace

Classifies separators using Q3's chat whitespace rules.
=============
*/
static int BotChat_IsRetailWhitespace(char character)
{
	if ((character >= 'a' && character <= 'z')
		|| (character >= 'A' && character <= 'Z')
		|| (character >= '0' && character <= '9')
		|| character == '(' || character == ')'
		|| character == '?' || character == ':'
		|| character == '\''
		|| character == '[' || character == ']'
		|| character == '-' || character == '_'
		|| character == '+' || character == '=')
	{
		return 0;
	}

	return 1;
}

/*
=============
UnifyWhiteSpaces

Collapses chat whitespace runs in place using retail token rules.
=============
*/
void UnifyWhiteSpaces(void *string)
{
	if (string == NULL)
	{
		return;
	}

	char *text = string;
	for (char *ptr = text, *oldptr = text; *ptr != '\0'; oldptr = ptr)
	{
		while (*ptr != '\0' && BotChat_IsRetailWhitespace(*ptr))
		{
			++ptr;
		}
		if (ptr > oldptr)
		{
			if (oldptr > text && *ptr != '\0')
			{
				*oldptr++ = ' ';
			}
			if (ptr > oldptr)
			{
				memmove(oldptr, ptr, strlen(ptr) + 1U);
			}
		}
		while (*ptr != '\0' && !BotChat_IsRetailWhitespace(*ptr))
		{
			++ptr;
		}
	}
}

/*
=============
StringContains

Returns the first retail substring match, or NULL.
=============
*/
const char *StringContains(const char *str1, const char *str2, int casesensitive)
{
	/* HOST SAFETY: retail assumes both pointers are valid. */
	if (str1 == NULL || str2 == NULL)
	{
		return NULL;
	}

	const size_t haystack_length = strlen(str1);
	const size_t needle_length = strlen(str2);
	if (needle_length > haystack_length)
	{
		return NULL;
	}

	for (size_t i = 0; i + needle_length <= haystack_length; ++i)
	{
		size_t j = 0;
		for (; j < needle_length; ++j)
		{
			const int left = (unsigned char)str1[i + j];
			const int right = (unsigned char)str2[j];
			if (casesensitive)
			{
				if (left != right)
				{
					break;
				}
			}
			else if (toupper(left) != toupper(right))
			{
				break;
			}
		}
		if (j == needle_length)
		{
			return str1 + i;
		}
	}

	return NULL;
}

/*
=============
StringContainsWord

Returns the first match delimited only by literal ASCII spaces, matching
Gladiator rather than Quake III's punctuation-aware successor helper.
=============
*/
const char *StringContainsWord(const char *str1, const char *str2,
	int casesensitive)
{
	/* HOST SAFETY: retail assumes both pointers are valid. */
	if (str1 == NULL || str2 == NULL)
	{
		return NULL;
	}

	const int length = (int)(strlen(str1) - strlen(str2));
	for (int i = 0; i <= length; ++i, ++str1)
	{
		if (i != 0)
		{
			while (*str1 != '\0' && *str1 != ' ')
			{
				++str1;
			}
			if (*str1 == '\0')
			{
				return NULL;
			}
			++str1;
		}

		int j = 0;
		for (; str2[j] != '\0'; ++j)
		{
			const int left = (unsigned char)str1[j];
			const int right = (unsigned char)str2[j];
			if ((casesensitive && left != right)
				|| (!casesensitive && toupper(left) != toupper(right)))
			{
				break;
			}
		}
		if (str2[j] == '\0' && (str1[j] == '\0' || str1[j] == ' '))
		{
			return str1;
		}
	}

	return NULL;
}

/*
=============
StringReplaceWords

Replaces literal-space-delimited words with the retail overlap checks and
copy lengths.
=============
*/
void StringReplaceWords(const char *string, const char *synonym,
	const char *replacement)
{
	char *match = (char *)StringContainsWord(string, synonym, 0);
	while (match != NULL)
	{
		char *replacement_match =
			(char *)StringContainsWord(string, replacement, 0);
		while (replacement_match != NULL)
		{
			if (replacement_match <= match
				&& match < replacement_match + strlen(replacement))
			{
				break;
			}
			replacement_match = (char *)StringContainsWord(
				replacement_match + 1,
				replacement,
				0);
		}
		if (replacement_match == NULL)
		{
			memmove(match + strlen(replacement),
				match + strlen(synonym),
				strlen(match + strlen(synonym)));
			memcpy(match, replacement, strlen(replacement));
		}
		match = (char *)StringContainsWord(match + strlen(replacement),
			synonym,
			0);
	}
}

/*
=============
StringContainsIndex

Compatibility adapter for the Q3-shaped bridge API.
=============
*/
int StringContainsIndex(const char *str1, const char *str2, int casesensitive)
{
	const char *match = StringContains(str1, str2, casesensitive);
	return match != NULL ? (int)(match - str1) : -1;
}

static char *BotChat_StringDuplicate(const char *text)
{
    if (text == NULL) {
        return NULL;
    }

    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy != NULL) {
        memcpy(copy, text, length);
    }

    return copy;
}

/*
=============
BotChat_MissingRandomWasReported

Checks the temporary retail load-time report list for a random identifier.
=============
*/
static int BotChat_MissingRandomWasReported(
	const bot_missing_random_list_t *missing_randoms,
	const char *name)
{
	if (missing_randoms == NULL || name == NULL)
	{
		return 0;
	}

	for (size_t i = 0; i < missing_randoms->count; ++i)
	{
		if (strcmp(missing_randoms->names[i], name) == 0)
		{
			return 1;
		}
	}

	return 0;
}

/*
=============
BotChat_RecordMissingRandom

Adds one unrecognised random identifier to the temporary retail report list.
=============
*/
static void BotChat_RecordMissingRandom(bot_missing_random_list_t *missing_randoms,
	const char *name)
{
	if (missing_randoms == NULL || name == NULL
		|| BotChat_MissingRandomWasReported(missing_randoms, name))
	{
		return;
	}

	if (missing_randoms->count == missing_randoms->capacity)
	{
		const size_t capacity = missing_randoms->capacity == 0U
			? 4U
			: missing_randoms->capacity * 2U;
		char **names = realloc(missing_randoms->names,
			capacity * sizeof(*names));
		if (names == NULL)
		{
			return;
		}

		missing_randoms->names = names;
		missing_randoms->capacity = capacity;
	}

	char *duplicate = BotChat_StringDuplicate(name);
	if (duplicate == NULL)
	{
		return;
	}

	missing_randoms->names[missing_randoms->count++] = duplicate;
}

/*
=============
BotChat_FreeMissingRandoms

Releases the temporary duplicate-suppression list used during one load pass.
=============
*/
static void BotChat_FreeMissingRandoms(bot_missing_random_list_t *missing_randoms)
{
	if (missing_randoms == NULL)
	{
		return;
	}

	for (size_t i = 0; i < missing_randoms->count; ++i)
	{
		free(missing_randoms->names[i]);
	}
	free(missing_randoms->names);
	memset(missing_randoms, 0, sizeof(*missing_randoms));
}

/*
=============
BotChat_CheckMessageIntegrity

Mirrors sub_1002cb40 by checking escaped random references after chat parsing.
=============
*/
static void BotChat_CheckMessageIntegrity(const bot_chatstate_t *state,
	const char *message,
	bot_missing_random_list_t *missing_randoms)
{
	const char *cursor = message;

	if (message == NULL)
	{
		return;
	}

	while (*cursor != '\0')
	{
		if (*cursor != BOT_CHAT_ESCAPE_CHAR)
		{
			++cursor;
			continue;
		}

		++cursor;
		if (*cursor == 'r')
		{
			char random_name[0x98];
			size_t name_length = 0U;

			++cursor;
			while (cursor[name_length] != '\0'
				&& cursor[name_length] != BOT_CHAT_ESCAPE_CHAR)
			{
				++name_length;
			}

			if (name_length < sizeof(random_name))
			{
				memcpy(random_name, cursor, name_length);
				random_name[name_length] = '\0';
				if (BotChat_SelectRandomString(state, random_name) == NULL
					&& !BotChat_MissingRandomWasReported(missing_randoms,
						random_name))
				{
					BotLib_LogWrite("%s = {\"%s\"} //MISSING RANDOM",
						random_name,
						random_name);
					BotChat_RecordMissingRandom(missing_randoms, random_name);
				}
			}

			cursor += name_length;
			if (*cursor == BOT_CHAT_ESCAPE_CHAR)
			{
				++cursor;
			}
			continue;
		}

		if (*cursor == 'v')
		{
			++cursor;
			while (*cursor != '\0' && *cursor != BOT_CHAT_ESCAPE_CHAR)
			{
				++cursor;
			}
			if (*cursor == BOT_CHAT_ESCAPE_CHAR)
			{
				++cursor;
			}
			continue;
		}

		BotLib_Print(PRT_FATAL,
			"BotCheckChatMessageIntegrety: message \"%s\" invalid escape char\n",
			message);
	}
}

/*
=============
BotChat_CheckInitialChatIntegrity

Applies the retail load-time random-reference check to all initial templates.
=============
*/
static void BotChat_CheckInitialChatIntegrity(const bot_chatstate_t *state)
{
	bot_missing_random_list_t missing_randoms = {0};

	if (state == NULL)
	{
		return;
	}

	for (size_t i = state->initial_type_count; i > 0; --i)
	{
		const bot_initial_chat_type_t *type = &state->initial_types[i - 1U];
		for (size_t j = type->template_count; j > 0; --j)
		{
			BotChat_CheckMessageIntegrity(state,
				type->templates[j - 1U],
				&missing_randoms);
		}
	}

	BotChat_FreeMissingRandoms(&missing_randoms);
}

/*
=============
BotChat_CheckReplyChatIntegrity

Applies the retail load-time random-reference check to all reply responses.
=============
*/
static void BotChat_CheckReplyChatIntegrity(const bot_chatstate_t *state)
{
	bot_missing_random_list_t missing_randoms = {0};

	if (state == NULL)
	{
		return;
	}

	for (size_t i = state->replies.rule_count; i > 0; --i)
	{
		const bot_reply_rule_t *rule = &state->replies.rules[i - 1U];
		for (size_t j = rule->response_count; j > 0; --j)
		{
			BotChat_CheckMessageIntegrity(state,
				rule->responses[j - 1U],
				&missing_randoms);
		}
	}

	BotChat_FreeMissingRandoms(&missing_randoms);
}

static int BotChat_StringEqualsIgnoreCase(const char *lhs, const char *rhs);

/*
=============
BotChat_InitialTypeNamesMatch

Compares raw Gladiator type names case-insensitively.
=============
*/
static int BotChat_InitialTypeNamesMatch(const char *stored_name,
	const char *query_name)
{
	if (stored_name == NULL || query_name == NULL)
	{
		return 0;
	}

	return BotChat_StringEqualsIgnoreCase(stored_name, query_name);
}

/*
=============
BotChat_CompatibilityInitialTypeName

Maps the Q3-shaped host adapter's successor names onto retail type buckets.
=============
*/
static const char *BotChat_CompatibilityInitialTypeName(const char *type_name)
{
	static const struct
	{
		const char *successor_name;
		const char *retail_name;
	} aliases[] = {
		{ "game_enter", "enter_game" },
		{ "game_exit", "exit_game" },
		{ "level_start", "start_level" },
		{ "level_end", "end_level" },
	};

	for (size_t i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i)
	{
		if (BotChat_StringEqualsIgnoreCase(type_name, aliases[i].successor_name))
		{
			return aliases[i].retail_name;
		}
	}
	return type_name;
}

/*
=============
BotChat_FindInitialType

Looks up a stored initial-chat bucket by exact retail type name. Reverse array
order mirrors the head-prepended retail linked list.
=============
*/
static bot_initial_chat_type_t *BotChat_FindInitialType(bot_chatstate_t *state,
	const char *type_name)
{
	if (state == NULL || type_name == NULL)
	{
		return NULL;
	}

	for (size_t i = state->initial_type_count; i > 0; --i)
	{
		bot_initial_chat_type_t *type = &state->initial_types[i - 1U];
		if (BotChat_InitialTypeNamesMatch(type->type_name, type_name))
		{
			return type;
		}
	}

	return NULL;
}

/*
=============
BotChat_AddInitialType

Allocates a distinct raw type bucket for one parsed retail type block.
=============
*/
static bot_initial_chat_type_t *BotChat_AddInitialType(bot_chatstate_t *state,
	const char *type_name)
{
	if (state == NULL || type_name == NULL)
	{
		return NULL;
	}

	bot_initial_chat_type_t *types = realloc(state->initial_types,
		(state->initial_type_count + 1) * sizeof(*types));
	if (types == NULL)
	{
		return NULL;
	}

	state->initial_types = types;
	bot_initial_chat_type_t *type =
		&state->initial_types[state->initial_type_count++];
	memset(type, 0, sizeof(*type));
	type->type_name = BotChat_StringDuplicate(type_name);
	if (type->type_name == NULL)
	{
		state->initial_type_count--;
		return NULL;
	}

	return type;
}

/*
=============
BotChat_AddInitialTypeTemplate

Stores a parsed initial-chat line in its original retail type bucket.
=============
*/
static int BotChat_AddInitialTypeTemplate(bot_chatstate_t *state,
	const char *type_name,
	const char *template_text)
{
	if (state == NULL || type_name == NULL || template_text == NULL)
	{
		return 0;
	}

	bot_initial_chat_type_t *type = BotChat_FindInitialType(state, type_name);
	if (type == NULL)
	{
		return 0;
	}

	if (type->template_count == type->template_capacity)
	{
		size_t capacity = type->template_capacity ? type->template_capacity * 2U : 4U;
		char **templates = malloc(capacity * sizeof(*templates));
		float *times = malloc(capacity * sizeof(*times));
		if (templates == NULL || times == NULL)
		{
			free(templates);
			free(times);
			return 0;
		}
		if (type->template_count > 0)
		{
			memcpy(templates,
				type->templates,
				type->template_count * sizeof(*templates));
			memcpy(times,
				type->template_times,
				type->template_count * sizeof(*times));
		}
		free(type->templates);
		free(type->template_times);
		type->templates = templates;
		type->template_times = times;
		type->template_capacity = capacity;
	}

	type->templates[type->template_count] = BotChat_StringDuplicate(template_text);
	if (type->templates[type->template_count] == NULL)
	{
		return 0;
	}
		type->template_times[type->template_count] = -40.0f;
	type->template_count++;
	return 1;
}

/*
=============
BotChat_FreeInitialTypes

Releases the raw initial-chat buckets parsed from a retail bot chat block.
=============
*/
static void BotChat_FreeInitialTypes(bot_chatstate_t *state)
{
	if (state == NULL)
	{
		return;
	}

	for (size_t i = 0; i < state->initial_type_count; ++i)
	{
		bot_initial_chat_type_t *type = &state->initial_types[i];
		free(type->type_name);
		for (size_t j = 0; j < type->template_count; ++j)
		{
			free(type->templates[j]);
		}
		free(type->templates);
		free(type->template_times);
	}

	free(state->initial_types);
	state->initial_types = NULL;
	state->initial_type_count = 0;
}

/*
=============
BotChat_ChooseInitialTemplate

Selects a stored initial-chat message in retail linked-list order.
=============
*/
static const char *BotChat_ChooseInitialTemplate(bot_chatstate_t *state,
	const char *type_name)
{
	bot_initial_chat_type_t *type = BotChat_FindInitialType(state, type_name);
	if (type == NULL || type->template_count == 0)
	{
		return NULL;
	}

	int available_count = 0;
	for (size_t i = type->template_count; i > 0; --i)
	{
		if (AAS_Time() >= type->template_times[i - 1U])
		{
			++available_count;
		}
	}

	if (available_count > 0)
	{
		int selected = (int)(BotChat_RetailRandomFloat()
			* (float)available_count);
		for (size_t i = type->template_count; i > 0; --i)
		{
			const size_t index = i - 1U;
			if (AAS_Time() < type->template_times[index])
			{
				continue;
			}
			if (--selected < 0)
			{
				type->template_times[index] = AAS_Time() + 20.0f;
				return type->templates[index];
			}
		}
		return NULL;
	}

	float best_time = 0.0f;
	const char *best = NULL;
	for (size_t i = type->template_count; i > 0; --i)
	{
		const size_t index = i - 1U;
		if (best_time == 0.0f || type->template_times[index] < best_time)
		{
			best_time = type->template_times[index];
			best = type->templates[index];
		}
	}
	return best;
}

/*
=============
BotChat_TokenText

Returns token text with script string quotes removed.
=============
*/
static const char *BotChat_TokenText(const pc_token_t *token, size_t *length)
{
	const char *text = (token != NULL) ? token->string : "";
	size_t text_length = strlen(text);

	if (token != NULL
		&& (token->type == TT_STRING || token->type == TT_LITERAL)
		&& text_length >= 2)
	{
		const char quote = text[0];
		if ((quote == '"' || quote == '\'') && text[text_length - 1] == quote)
		{
			++text;
			text_length -= 2;
		}
	}

	if (length != NULL)
	{
		*length = text_length;
	}
	return text;
}

/*
=============
BotChat_TokenTextDuplicate

Copies token text after applying script string quote handling.
=============
*/
static char *BotChat_TokenTextDuplicate(const pc_token_t *token)
{
	size_t length = 0;
	const char *text = BotChat_TokenText(token, &length);
	char *copy = (char *)malloc(length + 1);
	if (copy == NULL)
	{
		return NULL;
	}

	memcpy(copy, text, length);
	copy[length] = '\0';
	return copy;
}

/*
=============
BotChat_CheckTokenString

Peeks one token from a precompiler-backed script without consuming it when the
token does not match the requested punctuation or keyword.
=============
*/
static int BotChat_CheckTokenString(pc_script_t *script, const char *text)
{
	if (script == NULL || text == NULL)
	{
		return 0;
	}

	pc_token_t token;
	if (!PS_ReadToken(script, &token))
	{
		return 0;
	}

	if (strcmp(token.string, text) == 0)
	{
		return 1;
	}

	PS_UnreadToken(script, &token);
	return 0;
}

/*
=============
BotChat_FindMutableRandomTable

Finds a parsed random string table that can be extended.
=============
*/
static bot_random_string_table_t *BotChat_FindMutableRandomTable(
	bot_chatstate_t *state,
	const char *name)
{
	if (state == NULL || name == NULL)
	{
		return NULL;
	}

	for (size_t i = 0; i < state->random_table_count; ++i)
	{
		bot_random_string_table_t *table = &state->random_tables[i];
		if (table->name != NULL && strcmp(table->name, name) == 0)
		{
			return table;
		}
	}

	return NULL;
}

/*
=============
BotChat_AddRandomTable

Creates one parsed random string table in source order.
=============
*/
static bot_random_string_table_t *BotChat_AddRandomTable(
	bot_chatstate_t *state,
	const char *name)
{
	bot_random_string_table_t *tables = realloc(state->random_tables,
		(state->random_table_count + 1) * sizeof(*tables));
	if (tables == NULL)
	{
		return NULL;
	}

	state->random_tables = tables;
	bot_random_string_table_t *table =
		&state->random_tables[state->random_table_count++];
	memset(table, 0, sizeof(*table));
	table->name = BotChat_StringDuplicate(name);
	if (table->name == NULL)
	{
		state->random_table_count--;
		return NULL;
	}

	return table;
}

/*
=============
BotChat_AddRandomEntry

Appends one string entry to a parsed random string table.
=============
*/
static int BotChat_AddRandomEntry(bot_random_string_table_t *table, const char *text)
{
	if (table == NULL || text == NULL)
	{
		return 0;
	}

	if (table->entry_count >= table->entry_capacity)
	{
		size_t capacity = table->entry_capacity ? table->entry_capacity * 2 : 8;
		char **entries = realloc(table->entries, capacity * sizeof(*entries));
		if (entries == NULL)
		{
			return 0;
		}
		table->entries = entries;
		table->entry_capacity = capacity;
	}

	table->entries[table->entry_count] = BotChat_StringDuplicate(text);
	if (table->entries[table->entry_count] == NULL)
	{
		return 0;
	}
	table->entry_count++;
	return 1;
}

/*
=============
BotChat_CopyRandomEntries

Expands table references inside another random string table.
=============
*/
static int BotChat_CopyRandomEntries(bot_random_string_table_t *table,
	const bot_random_string_table_t *source)
{
	if (table == NULL || source == NULL)
	{
		return 0;
	}

	for (size_t i = 0; i < source->entry_count; ++i)
	{
		if (!BotChat_AddRandomEntry(table, source->entries[i]))
		{
			return 0;
		}
	}

	return 1;
}

/*
=============
BotChat_FreeRandomTables

Releases random string tables parsed from chat assets.
=============
*/
static void BotChat_FreeRandomTables(bot_chatstate_t *state)
{
	if (state == NULL)
	{
		return;
	}

	for (size_t i = 0; i < state->random_table_count; ++i)
	{
		bot_random_string_table_t *table = &state->random_tables[i];
		free(table->name);
		for (size_t j = 0; j < table->entry_count; ++j)
		{
			free(table->entries[j]);
		}
		free(table->entries);
	}

	free(state->random_tables);
	state->random_tables = NULL;
	state->random_table_count = 0;
}

static void BotChat_FreeSynonymGroup(bot_synonym_group_t *group)
{
    if (group == NULL) {
        return;
    }

    for (size_t i = 0; i < group->phrase_count; ++i) {
        free(group->phrases[i].text);
    }
    free(group->phrases);
    group->phrases = NULL;
    group->phrase_count = 0;
    group->phrase_capacity = 0;
}

static void BotChat_FreeSynonymContexts(bot_chatstate_t *state)
{
    if (state->synonym_contexts == NULL) {
        return;
    }

    for (size_t i = 0; i < state->synonym_context_count; ++i) {
        bot_synonym_context_t *context = &state->synonym_contexts[i];
        for (size_t j = 0; j < context->group_count; ++j) {
            BotChat_FreeSynonymGroup(&context->groups[j]);
        }
        free(context->groups);
        free(context->context_name);
    }

    free(state->synonym_contexts);
    state->synonym_contexts = NULL;
    state->synonym_context_count = 0;
}

static void BotChat_FreeMatchContexts(bot_chatstate_t *state)
{
	if (state == NULL)
	{
		return;
	}

	for (size_t i = 0; i < state->match_context_count; ++i) {
        bot_match_context_t *context = &state->match_contexts[i];
        for (size_t j = 0; j < context->template_count; ++j) {
            free(context->templates[j]);
        }
		free(context->templates);
		free(context->template_match_contexts);
		free(context->template_subtypes);
    }

    free(state->match_contexts);
    state->match_contexts = NULL;
    state->match_context_count = 0;
	free(state->match_order);
	state->match_order = NULL;
	state->match_order_count = 0;
}

static void BotChat_FreeReplies(bot_chatstate_t *state)
{
    for (size_t i = 0; i < state->replies.rule_count; ++i) {
        bot_reply_rule_t *rule = &state->replies.rules[i];
		for (size_t j = 0; j < rule->key_count; ++j) {
			free(rule->keys[j].pattern);
		}
		free(rule->keys);
		rule->keys = NULL;
		rule->key_count = 0;
		rule->key_capacity = 0;
        for (size_t j = 0; j < rule->response_count; ++j) {
            free(rule->responses[j]);
        }
        free(rule->responses);
		free(rule->response_times);
    }

    free(state->replies.rules);
    state->replies.rules = NULL;
    state->replies.rule_count = 0;
    state->replies.rule_capacity = 0;
    state->has_reply_chats = 0;
}

static void BotChat_ClearMetadata(bot_chatstate_t *state)
{
	if (state == NULL) {
		return;
	}

	state->active_chatfile[0] = '\0';
	state->active_chatname[0] = '\0';
	state->speaking_client = 0;
	BotChat_ClearPendingMessage(state);
}

/*
=============
BotChat_AddSynonymContext

Creates one synonym node in file order. Equal context masks remain distinct,
matching the retail global linked list.
=============
*/
static bot_synonym_context_t *BotChat_AddSynonymContext(bot_chatstate_t *state, const char *name)
{
	if (state == NULL || name == NULL)
	{
		return NULL;
	}

    bot_synonym_context_t *contexts = realloc(state->synonym_contexts,
                                              (state->synonym_context_count + 1) * sizeof(*contexts));
    if (contexts == NULL) {
        return NULL;
    }

    state->synonym_contexts = contexts;
    bot_synonym_context_t *context = &state->synonym_contexts[state->synonym_context_count++];
    memset(context, 0, sizeof(*context));
    context->context_name = BotChat_StringDuplicate(name);
    if (context->context_name == NULL) {
        state->synonym_context_count--;
        return NULL;
    }
    return context;
}

static bot_synonym_group_t *BotChat_AddSynonymGroup(bot_synonym_context_t *context)
{
    bot_synonym_group_t *groups = realloc(context->groups,
                                          (context->group_count + 1) * sizeof(*groups));
    if (groups == NULL) {
        return NULL;
    }

    context->groups = groups;
    bot_synonym_group_t *group = &context->groups[context->group_count++];
    memset(group, 0, sizeof(*group));
    return group;
}

static bot_synonym_phrase_t *BotChat_AddSynonymPhrase(bot_synonym_group_t *group)
{
    bot_synonym_phrase_t *phrases = realloc(group->phrases,
                                            (group->phrase_count + 1) * sizeof(*phrases));
    if (phrases == NULL) {
        return NULL;
    }

    group->phrases = phrases;
    bot_synonym_phrase_t *phrase = &group->phrases[group->phrase_count++];
    phrase->text = NULL;
    phrase->weight = 0.0f;
    return phrase;
}

static bot_match_context_t *BotChat_AddMatchContext(bot_chatstate_t *state, unsigned long message_type)
{
    bot_match_context_t *contexts = realloc(state->match_contexts,
                                            (state->match_context_count + 1) * sizeof(*contexts));
    if (contexts == NULL) {
        return NULL;
    }

    state->match_contexts = contexts;
    bot_match_context_t *context = &state->match_contexts[state->match_context_count++];
    memset(context, 0, sizeof(*context));
    context->message_type = message_type;
    return context;
}

/*
=============
BotChat_AddTemplate

Appends a match template while preserving MTCONTEXT and subtype metadata.
=============
*/
static char **BotChat_AddTemplate(bot_match_context_t *context,
	unsigned long match_context,
	unsigned long subtype)
{
	const size_t next_count = context->template_count + 1U;
	char **templates = malloc(
		next_count * sizeof(*templates));
	unsigned long *match_contexts = malloc(
		next_count * sizeof(*match_contexts));
	unsigned long *subtypes = malloc(
		next_count * sizeof(*subtypes));
	if (templates == NULL || match_contexts == NULL || subtypes == NULL)
	{
		free(templates);
		free(match_contexts);
		free(subtypes);
		return NULL;
	}

	if (context->template_count > 0)
	{
		memcpy(templates,
			context->templates,
			context->template_count * sizeof(*templates));
		memcpy(match_contexts,
			context->template_match_contexts,
			context->template_count * sizeof(*match_contexts));
		memcpy(subtypes,
			context->template_subtypes,
			context->template_count * sizeof(*subtypes));
	}
	free(context->templates);
	free(context->template_match_contexts);
	free(context->template_subtypes);
	context->templates = templates;
	context->template_match_contexts = match_contexts;
	context->template_subtypes = subtypes;
	context->templates[context->template_count] = NULL;
	context->template_match_contexts[context->template_count] = match_context;
	context->template_subtypes[context->template_count] = subtype;
	return &context->templates[context->template_count++];
}

/*
=============
BotChat_AddMatchOrderEntry

Records the global source order that retail's linked template list preserves.
=============
*/
static int BotChat_AddMatchOrderEntry(bot_chatstate_t *state,
	char *template_text,
	unsigned long match_context,
	unsigned long message_type,
	unsigned long subtype)
{
	bot_match_order_entry_t *entries = realloc(state->match_order,
		(state->match_order_count + 1U) * sizeof(*entries));
	if (entries == NULL)
	{
		return 0;
	}

	state->match_order = entries;
	bot_match_order_entry_t *entry =
		&state->match_order[state->match_order_count++];
	entry->template_text = template_text;
	entry->match_context = match_context;
	entry->message_type = message_type;
	entry->subtype = subtype;
	return 1;
}

static bot_match_context_t *BotChat_FindMatchContext(bot_chatstate_t *state, unsigned long message_type);
static bot_match_context_t *BotChat_FindResolvedMatchContext(bot_chatstate_t *state,
	unsigned long message_type);

/*
=============
BotChat_AddTemplateForContext

Registers a chat template under the supplied event context.
=============
*/
static int BotChat_AddTemplateForContext(bot_chatstate_t *state,
	unsigned long context,
	const char *template_text)
{
	if (state == NULL || context == 0 || template_text == NULL)
	{
		return 1;
	}

	bot_match_context_t *match_context = BotChat_FindMatchContext(state, context);
	if (match_context == NULL)
	{
		match_context = BotChat_AddMatchContext(state, context);
		if (match_context == NULL)
		{
			return 0;
		}
	}

	char **slot = BotChat_AddTemplate(match_context, context, 0UL);
	if (slot == NULL)
	{
		return 0;
	}

	*slot = BotChat_StringDuplicate(template_text);
	return *slot != NULL;
}

/*
=============
BotChat_AddReplyRule

Adds a parsed reply block to the state's reply table.
=============
*/
static bot_reply_rule_t *BotChat_AddReplyRule(bot_chatstate_t *state,
	unsigned long reply_context)
{
	bot_reply_rule_t *rules = realloc(state->replies.rules,
		(state->replies.rule_count + 1) * sizeof(*rules));
	if (rules == NULL)
	{
		return NULL;
	}

	state->replies.rules = rules;
	bot_reply_rule_t *rule = &state->replies.rules[state->replies.rule_count++];
	memset(rule, 0, sizeof(*rule));
	rule->reply_context = reply_context;
	rule->priority = (float)reply_context;
	state->has_reply_chats = 1;
	return rule;
}

/*
=============
BotChat_AddReply

Appends one response template and its recent-use timer to a reply rule.
=============
*/
static char **BotChat_AddReply(bot_reply_rule_t *rule)
{
	const size_t next_count = rule->response_count + 1U;
	char **responses = malloc(next_count * sizeof(*responses));
	float *times = malloc(next_count * sizeof(*times));
	if (responses == NULL || times == NULL)
	{
		free(responses);
		free(times);
		return NULL;
	}

	if (rule->response_count > 0)
	{
		memcpy(responses,
			rule->responses,
			rule->response_count * sizeof(*responses));
		memcpy(times,
			rule->response_times,
			rule->response_count * sizeof(*times));
	}
	free(rule->responses);
	free(rule->response_times);
	rule->responses = responses;
	rule->response_times = times;
	rule->responses[rule->response_count] = NULL;
	rule->response_times[rule->response_count] = -40.0f;
	return &rule->responses[rule->response_count++];
}

/*
=============
BotChat_AddReplyKeyToList

Appends a parsed reply key to a temporary key list.
=============
*/
static bot_reply_key_t *BotChat_AddReplyKeyToList(bot_reply_key_list_t *list)
{
	if (list == NULL)
	{
		return NULL;
	}

	if (list->key_count >= list->key_capacity)
	{
		size_t capacity = list->key_capacity ? list->key_capacity * 2 : 4;
		bot_reply_key_t *keys = realloc(list->keys, capacity * sizeof(*keys));
		if (keys == NULL)
		{
			return NULL;
		}
		list->keys = keys;
		list->key_capacity = capacity;
	}

	bot_reply_key_t *key = &list->keys[list->key_count++];
	memset(key, 0, sizeof(*key));
	return key;
}

/*
=============
BotChat_FreeReplyKeyList

Releases a temporary reply-key list that was not transferred to a rule.
=============
*/
static void BotChat_FreeReplyKeyList(bot_reply_key_list_t *list)
{
	if (list == NULL)
	{
		return;
	}

	for (size_t i = 0; i < list->key_count; ++i)
	{
		free(list->keys[i].pattern);
	}
	free(list->keys);
	list->keys = NULL;
	list->key_count = 0;
	list->key_capacity = 0;
}

/*
=============
BotChat_MoveReplyKeysToRule

Transfers parsed key ownership onto a reply rule.
=============
*/
static int BotChat_MoveReplyKeysToRule(bot_reply_rule_t *rule,
	bot_reply_key_list_t *list)
{
	if (rule == NULL || list == NULL || list->key_count == 0)
	{
		return 1;
	}

	const size_t new_count = rule->key_count + list->key_count;
	bot_reply_key_t *keys = realloc(rule->keys, new_count * sizeof(*keys));
	if (keys == NULL)
	{
		return 0;
	}

	rule->keys = keys;
	memcpy(&rule->keys[rule->key_count],
		list->keys,
		list->key_count * sizeof(*keys));
	rule->key_count = new_count;
	if (rule->key_capacity < new_count)
	{
		rule->key_capacity = new_count;
	}

	free(list->keys);
	list->keys = NULL;
	list->key_count = 0;
	list->key_capacity = 0;
	return 1;
}

static bot_match_context_t *BotChat_FindMatchContext(bot_chatstate_t *state, unsigned long message_type)
{
    for (size_t i = 0; i < state->match_context_count; ++i) {
        if (state->match_contexts[i].message_type == message_type) {
            return &state->match_contexts[i];
        }
    }
    return NULL;
}

/*
=============
BotChat_FindResolvedMatchContext

Locates a per-state match context, falling back to the shared setup cache.
=============
*/
static bot_match_context_t *BotChat_FindResolvedMatchContext(bot_chatstate_t *state,
	unsigned long message_type)
{
	if (state == NULL)
	{
		return NULL;
	}

	bot_match_context_t *context = BotChat_FindMatchContext(state, message_type);
	if (context != NULL)
	{
		return context;
	}

	const bot_chatstate_t *fallback_state = BotChat_SetupFallbackState(state);
	if (fallback_state == NULL)
	{
		return NULL;
	}

	return BotChat_FindMatchContext((bot_chatstate_t *)fallback_state, message_type);
}

static bot_reply_rule_t *BotChat_FindReplyRule(bot_chatstate_t *state, unsigned long reply_context)
{
    for (size_t i = 0; i < state->replies.rule_count; ++i) {
        if (state->replies.rules[i].reply_context == reply_context) {
            return &state->replies.rules[i];
        }
    }
    return NULL;
}

static void BotChat_StringBuilderDestroy(bot_string_builder_t *builder)
{
    free(builder->buffer);
    builder->buffer = NULL;
    builder->length = 0;
    builder->capacity = 0;
}

static int BotChat_StringBuilderReserve(bot_string_builder_t *builder, size_t required)
{
    if (required <= builder->capacity) {
        return 1;
    }
    size_t capacity = builder->capacity ? builder->capacity : 64;
    while (capacity < required) {
        capacity *= 2;
    }
    char *buffer = realloc(builder->buffer, capacity);
    if (buffer == NULL) {
        return 0;
    }
    builder->buffer = buffer;
    builder->capacity = capacity;
    return 1;
}

static int BotChat_StringBuilderAppend(bot_string_builder_t *builder, const char *text)
{
    size_t length = strlen(text);
    if (!BotChat_StringBuilderReserve(builder, builder->length + length + 1)) {
        return 0;
    }
    memcpy(builder->buffer + builder->length, text, length);
    builder->length += length;
    builder->buffer[builder->length] = '\0';
    return 1;
}

/*
=============
BotChat_StringBuilderAppendSpan

Appends a non-null-terminated text span.
=============
*/
static int BotChat_StringBuilderAppendSpan(bot_string_builder_t *builder,
	const char *text,
	size_t length)
{
	if (text == NULL)
	{
		return 0;
	}

	if (!BotChat_StringBuilderReserve(builder, builder->length + length + 1))
	{
		return 0;
	}

	memcpy(builder->buffer + builder->length, text, length);
	builder->length += length;
	builder->buffer[builder->length] = '\0';
	return 1;
}

/*
=============
BotChat_StringBuilderAppendTokenText

Appends a script token's visible text.
=============
*/
static int BotChat_StringBuilderAppendChar(bot_string_builder_t *builder, char character);

static int BotChat_StringBuilderAppendTokenText(bot_string_builder_t *builder,
	const pc_token_t *token)
{
	size_t length = 0;
	const char *text = BotChat_TokenText(token, &length);
	return BotChat_StringBuilderAppendSpan(builder, text, length);
}

/*
=============
BotChat_StringBuilderAppendStringAlternativePiece

Consumes Q3 string-piece alternatives and appends the internal match encoding.
=============
*/
static int BotChat_StringBuilderAppendStringAlternativePiece(
	bot_string_builder_t *builder,
	pc_script_t *script,
	const pc_token_t *first_token,
	int *contains_empty)
{
	if (builder == NULL || script == NULL || first_token == NULL)
	{
		return 0;
	}

	bot_string_builder_t alternatives = {0};
	pc_token_t token = *first_token;
	size_t alternative_count = 0;
	int found_empty = 0;

	for (;;)
	{
		size_t text_length = 0;
		const char *text = BotChat_TokenText(&token, &text_length);
		if (text_length == 0)
		{
			found_empty = 1;
		}
		if (alternative_count > 0
			&& !BotChat_StringBuilderAppendChar(&alternatives,
				BOT_CHAT_MATCH_ALT_SEPARATOR))
		{
			BotChat_StringBuilderDestroy(&alternatives);
			return 0;
		}
		if (!BotChat_StringBuilderAppendSpan(&alternatives, text, text_length))
		{
			BotChat_StringBuilderDestroy(&alternatives);
			return 0;
		}
		++alternative_count;

		if (!BotChat_CheckTokenString(script, "|"))
		{
			break;
		}
		if (!PS_ReadToken(script, &token) || token.type != TT_STRING)
		{
			BotChat_StringBuilderDestroy(&alternatives);
			return 0;
		}
	}

	if (contains_empty != NULL)
	{
		*contains_empty = found_empty;
	}

	if (alternative_count == 1 && !found_empty)
	{
		BotChat_StringBuilderDestroy(&alternatives);
		return BotChat_StringBuilderAppendTokenText(builder, first_token);
	}

	if (!BotChat_StringBuilderAppendChar(builder, BOT_CHAT_MATCH_ALT_START)
		|| !BotChat_StringBuilderAppendSpan(builder,
			alternatives.buffer != NULL ? alternatives.buffer : "",
			alternatives.length)
		|| !BotChat_StringBuilderAppendChar(builder, BOT_CHAT_MATCH_ALT_END))
	{
		BotChat_StringBuilderDestroy(&alternatives);
		return 0;
	}

	BotChat_StringBuilderDestroy(&alternatives);
	return 1;
}

static int BotChat_StringBuilderAppendChar(bot_string_builder_t *builder, char character)
{
    if (!BotChat_StringBuilderReserve(builder, builder->length + 2)) {
        return 0;
    }
    builder->buffer[builder->length++] = character;
    builder->buffer[builder->length] = '\0';
    return 1;
}

static char *BotChat_StringBuilderDetach(bot_string_builder_t *builder)
{
    if (!BotChat_StringBuilderReserve(builder, builder->length + 1)) {
        return NULL;
    }
    char *result = builder->buffer;
    builder->buffer = NULL;
    builder->length = 0;
    builder->capacity = 0;
    return result;
}

static int BotChat_StringBuilderAppendIdentifier(bot_string_builder_t *builder,
                                                 const char *identifier,
                                                 size_t length)
{
    if (!BotChat_StringBuilderAppendChar(builder, '{')) {
        return 0;
    }
    for (size_t i = 0; i < length; ++i) {
        if (!BotChat_StringBuilderAppendChar(builder, (char)toupper((unsigned char)identifier[i]))) {
            return 0;
        }
    }
    if (!BotChat_StringBuilderAppendChar(builder, '}')) {
        return 0;
    }
    return 1;
}

/*
=============
BotChat_IsKnownPlaceholderName

Identifies match variables that should remain visible as placeholders.
=============
*/
static int BotChat_IsKnownPlaceholderName(const char *identifier)
{
	static const char *const names[] = {
		"VICTIM",
		"KILLER",
		"GENDER_HE",
		"GENDER_HIS",
		"GENDER_HIM",
		"GENDER_GOD",
		"THE_ENEMY",
		"THE_TEAM",
		"TEAM",
		"NETNAME",
		"ADDRESSEE",
		"ITEM",
		"TEAMMATE",
		"TEAMNAME",
		"KEYAREA",
		"FORMATION",
		"POSITION",
		"NUMBER",
		"TIME",
		"NAME",
		"MORE"
	};

	if (identifier == NULL)
	{
		return 0;
	}

	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
	{
		if (strcmp(identifier, names[i]) == 0)
		{
			return 1;
		}
	}

	return 0;
}

/*
=============
BotChat_StringBuilderAppendNameToken

Appends identifiers as placeholders only when they are match variables.
=============
*/
static int BotChat_StringBuilderAppendNameToken(bot_string_builder_t *builder,
	const pc_token_t *token)
{
	if (token == NULL)
	{
		return 0;
	}

	if (BotChat_IsKnownPlaceholderName(token->string))
	{
		return BotChat_StringBuilderAppendIdentifier(builder,
			token->string,
			strlen(token->string));
	}

	return BotChat_StringBuilderAppendTokenText(builder, token);
}

/*
=============
BotChat_IsEscapeChar

Returns true only for the retail byte-one ESCAPE_CHAR marker.
=============
*/
static int BotChat_IsEscapeChar(char character)
{
	return character == BOT_CHAT_ESCAPE_CHAR;
}

/*
=============
BotChat_StringBuilderAppendRandomReference

Stores a preprocessor-expanded random table reference for construction time.
=============
*/
static int BotChat_StringBuilderAppendRandomReference(bot_string_builder_t *builder,
	const char *identifier)
{
	return BotChat_StringBuilderAppendChar(builder, BOT_CHAT_ESCAPE_CHAR)
		&& BotChat_StringBuilderAppendChar(builder, 'r')
		&& BotChat_StringBuilderAppend(builder, identifier)
		&& BotChat_StringBuilderAppendChar(builder, BOT_CHAT_ESCAPE_CHAR);
}

/*
=============
BotChat_StringBuilderAppendVariableReference

Stores a preprocessor-expanded match variable for later context-aware naming.
=============
*/
static int BotChat_StringBuilderAppendVariableReference(bot_string_builder_t *builder,
	unsigned long value)
{
	char buffer[32];
	int written = snprintf(buffer, sizeof(buffer), "v%lu", value);
	if (written <= 0 || (size_t)written >= sizeof(buffer))
	{
		return 0;
	}

	return BotChat_StringBuilderAppendChar(builder, BOT_CHAT_ESCAPE_CHAR)
		&& BotChat_StringBuilderAppend(builder, buffer)
		&& BotChat_StringBuilderAppendChar(builder, BOT_CHAT_ESCAPE_CHAR);
}

static unsigned long BotChat_MessageTypeFromIdentifier(const char *identifier, size_t length)
{
	if (length == 0) {
		return 0;
	}
	char buffer[64];
	if (length >= sizeof(buffer)) {
		return 0;
	}
	for (size_t i = 0; i < length; ++i) {
		buffer[i] = (char)toupper((unsigned char)identifier[i]);
	}
	buffer[length] = '\0';

	if (strcmp(buffer, "MSG_DEATH") == 0) {
		return BOT_CHAT_CONTEXT_DEATH;
	}
	if (strcmp(buffer, "MSG_ENTERGAME") == 0) {
		return BOT_CHAT_CONTEXT_ENTERGAME;
	}
	if (strcmp(buffer, "MSG_HELP") == 0) {
		return 3;
	}
	if (strcmp(buffer, "MSG_ACCOMPANY") == 0) {
		return 4;
	}
	if (strcmp(buffer, "MSG_DEFENDKEYAREA") == 0) {
		return 5;
	}
	if (strcmp(buffer, "MSG_RUSHBASE") == 0) {
		return 6;
	}
	if (strcmp(buffer, "MSG_GETFLAG") == 0) {
		return 7;
	}
	if (strcmp(buffer, "MSG_STARTTEAMLEADERSHIP") == 0) {
		return 8;
	}
	if (strcmp(buffer, "MSG_STOPTEAMLEADERSHIP") == 0) {
		return 9;
	}
	if (strcmp(buffer, "MSG_WAIT") == 0) {
		return 10;
	}
	if (strcmp(buffer, "MSG_WHATAREYOUDOING") == 0) {
		return 11;
	}
	if (strcmp(buffer, "MSG_JOINSUBTEAM") == 0) {
		return 12;
	}
	if (strcmp(buffer, "MSG_LEAVESUBTEAM") == 0) {
		return 13;
	}
	if (strcmp(buffer, "MSG_CREATENEWFORMATION") == 0) {
		return 14;
	}
	if (strcmp(buffer, "MSG_FORMATIONPOSITION") == 0) {
		return 15;
	}
	if (strcmp(buffer, "MSG_FORMATIONSPACE") == 0) {
		return 16;
	}
	if (strcmp(buffer, "MSG_DOFORMATION") == 0) {
		return 17;
	}
	if (strcmp(buffer, "MSG_DISMISS") == 0) {
		return 18;
	}
	if (strcmp(buffer, "MSG_CAMP") == 0) {
		return 19;
	}
	if (strcmp(buffer, "MSG_CHECKPOINT") == 0) {
		return 20;
	}
	if (strcmp(buffer, "MSG_PATROL") == 0) {
		return 21;
	}
	if (strcmp(buffer, "MSG_ME") == 0) {
		return 100;
	}
	if (strcmp(buffer, "MSG_EVERYONE") == 0) {
		return 101;
	}
	if (strcmp(buffer, "MSG_MULTIPLENAMES") == 0) {
		return 102;
	}
	if (strcmp(buffer, "MSG_NAME") == 0) {
		return 103;
	}
	if (strcmp(buffer, "MSG_PATROLKEYAREA") == 0) {
		return 104;
	}
	if (strcmp(buffer, "MSG_MINUTES") == 0) {
		return 105;
	}
	if (strcmp(buffer, "MSG_SECONDS") == 0) {
		return 106;
	}
	if (strcmp(buffer, "MSG_LEADTHEWAY") == 0) {
		return 23;
	}
	if (strcmp(buffer, "MSG_GETITEM") == 0) {
		return 24;
	}
	if (strcmp(buffer, "MSG_KILL") == 0) {
		return BOT_CHAT_CONTEXT_KILL;
	}
	if (strcmp(buffer, "MSG_WHEREAREYOU") == 0) {
		return 26;
	}
	if (strcmp(buffer, "MSG_RETURNFLAG") == 0) {
		return 27;
	}
	if (strcmp(buffer, "MSG_WHATISMYCOMMAND") == 0) {
		return 28;
	}
	if (strcmp(buffer, "MSG_WHICHTEAM") == 0) {
		return 29;
	}
	if (strcmp(buffer, "MSG_TASKPREFERENCE") == 0) {
		return 30;
	}
	if (strcmp(buffer, "MSG_ATTACKENEMYBASE") == 0) {
		return 31;
	}
	if (strcmp(buffer, "MSG_HARVEST") == 0) {
		return 32;
	}
	if (strcmp(buffer, "MSG_SUICIDE") == 0) {
		return 33;
	}
	if (strcmp(buffer, "MSG_ENEMYSUICIDE") == 0) {
		return BOT_CHAT_CONTEXT_ENEMYSUICIDE;
	}
	if (strcmp(buffer, "MSG_HITTALKING") == 0) {
		return BOT_CHAT_CONTEXT_HITTALKING;
	}
	if (strcmp(buffer, "MSG_HITNODEATH") == 0) {
		return BOT_CHAT_CONTEXT_HITNODEATH;
	}
	if (strcmp(buffer, "MSG_HITNOKILL") == 0) {
		return BOT_CHAT_CONTEXT_HITNOKILL;
	}
	if (strcmp(buffer, "MSG_RANDOM") == 0) {
		return BOT_CHAT_CONTEXT_RANDOM;
	}
	if (strcmp(buffer, "MSG_INSULT") == 0) {
		return BOT_CHAT_CONTEXT_INSULT;
	}
	if (strcmp(buffer, "MSG_PRAISE") == 0) {
		return BOT_CHAT_CONTEXT_PRAISE;
	}
	return 0;
}

/*
=============
BotChat_NumberTokenValue

Reads the numeric text because script-wrapper tokens may not carry intvalue.
=============
*/
static unsigned long BotChat_NumberTokenValue(const pc_token_t *token)
{
	if (token == NULL || token->type != TT_NUMBER)
	{
		return 0;
	}

	char *end = NULL;
	unsigned long value = strtoul(token->string, &end, 0);
	if (end == token->string)
	{
		return token->intvalue;
	}

	return value;
}

/*
=============
BotChat_NumberTokenIsInteger

Checks numeric chat and match parser fields the same way Q3's loader does:
only integer number tokens are accepted for variables and match metadata.
=============
*/
static int BotChat_NumberTokenIsInteger(const pc_token_t *token)
{
	return token != NULL
		&& token->type == TT_NUMBER
		&& (token->subtype & TT_INTEGER) != 0;
}

/*
=============
BotChat_NumberTokenMatchVariableValue

Reads a Q3 match-piece variable token and validates the loader-time variable
range before the pattern is registered.
=============
*/
static int BotChat_NumberTokenMatchVariableValue(const pc_token_t *token,
	unsigned long *value)
{
	if (!BotChat_NumberTokenIsInteger(token))
	{
		return 0;
	}

	const unsigned long parsed = BotChat_NumberTokenValue(token);
	if (parsed >= BOT_CHAT_RETAIL_MATCH_VARIABLES)
	{
		return 0;
	}

	if (value != NULL)
	{
		*value = parsed;
	}
	return 1;
}

/*
=============
BotChat_MessageTypeFromToken

Resolves message type tokens before and after preprocessor macro expansion.
=============
*/
static unsigned long BotChat_MessageTypeFromToken(const pc_token_t *token)
{
	if (token == NULL)
	{
		return 0;
	}

	if (token->type == TT_NUMBER)
	{
		return BotChat_NumberTokenIsInteger(token)
			? BotChat_NumberTokenValue(token)
			: 0UL;
	}

	if (token->type == TT_NAME)
	{
		return BotChat_MessageTypeFromIdentifier(token->string, strlen(token->string));
	}

	return 0;
}

/*
=============
BotChat_MatchContextFromIdentifier

Maps MTCONTEXT_* labels onto the retail match-template filter masks.
=============
*/
static unsigned long BotChat_MatchContextFromIdentifier(const char *identifier)
{
	if (identifier == NULL)
	{
		return 0UL;
	}

	if (BotChat_StringEqualsIgnoreCase(identifier, "MTCONTEXT_CLIENTOBITUARY"))
	{
		return 1UL;
	}
	if (BotChat_StringEqualsIgnoreCase(identifier, "MTCONTEXT_ENTERGAME"))
	{
		return 2UL;
	}
	if (BotChat_StringEqualsIgnoreCase(identifier, "MTCONTEXT_INITIALTEAMCHAT"))
	{
		return 4UL;
	}
	if (BotChat_StringEqualsIgnoreCase(identifier, "MTCONTEXT_TIME"))
	{
		return 8UL;
	}
	if (BotChat_StringEqualsIgnoreCase(identifier, "MTCONTEXT_TEAMMATE"))
	{
		return 16UL;
	}
	if (BotChat_StringEqualsIgnoreCase(identifier, "MTCONTEXT_ADDRESSEE"))
	{
		return 32UL;
	}
	if (BotChat_StringEqualsIgnoreCase(identifier, "MTCONTEXT_PATROLKEYAREA"))
	{
		return 64UL;
	}

	return 0UL;
}

/*
=============
BotChat_MatchContextFromToken

Resolves an MTCONTEXT token before or after preprocessor expansion.
=============
*/
static unsigned long BotChat_MatchContextFromToken(const pc_token_t *token)
{
	if (token == NULL)
	{
		return 0UL;
	}
	if (token->type == TT_NUMBER)
	{
		return BotChat_NumberTokenIsInteger(token)
			? BotChat_NumberTokenValue(token)
			: 0UL;
	}
	if (token->type == TT_NAME)
	{
		return BotChat_MatchContextFromIdentifier(token->string);
	}

	return 0UL;
}

/*
=============
BotChat_MessageSubtypeFromIdentifier

Maps preserved subtype names used by Gladiator and Q3 match scripts.
=============
*/
static unsigned long BotChat_MessageSubtypeFromIdentifier(const char *identifier)
{
	static const struct {
		const char *name;
		unsigned long value;
	} subtypes[] = {
		{"ST_DEATH_SUICIDE", 1UL},
		{"ST_DEATH_BLASTER", 2UL},
		{"ST_DEATH_SHOTGUN", 3UL},
		{"ST_DEATH_SUPERSHOTGUN", 4UL},
		{"ST_DEATH_MACHINEGUN", 5UL},
		{"ST_DEATH_CHAINGUN", 6UL},
		{"ST_DEATH_GRENADES", 7UL},
		{"ST_DEATH_GRENADELAUNCHER", 8UL},
		{"ST_DEATH_ROCKETLAUNCHER", 9UL},
		{"ST_DEATH_HYPERBLASTER", 10UL},
		{"ST_DEATH_RAILGUN", 11UL},
		{"ST_DEATH_BFG", 12UL},
		{"ST_DEATH_TELEFRAG", 13UL},
		{"ST_DEATH_GRAPPLE", 14UL},
		{"ST_DEATH_RIPPER", 15UL},
		{"ST_DEATH_PHALANX", 16UL},
		{"ST_DEATH_TRAP", 17UL},
		{"ST_DEATH_CHAINFIST", 18UL},
		{"ST_DEATH_DISRUPTOR", 19UL},
		{"ST_DEATH_ETFRIFLE", 20UL},
		{"ST_DEATH_HEATBEAM", 21UL},
		{"ST_DEATH_TESLA", 22UL},
		{"ST_DEATH_PROX", 23UL},
		{"ST_DEATH_NUKE", 24UL},
		{"ST_DEATH_VENGEANCESPHERE", 25UL},
		{"ST_DEATH_DEFENDER_SPHERE", 26UL},
		{"ST_DEATH_HUNTERSPHERE", 27UL},
		{"ST_DEATH_TRACKER", 28UL},
		{"ST_DEATH_DOPPLEGANGER", 29UL},
		{"ST_SOMEWHERE", 0UL},
		{"ST_NEARITEM", 1UL},
		{"ST_ADDRESSED", 2UL},
		{"ST_METER", 4UL},
		{"ST_FEET", 8UL},
		{"ST_TIME", 16UL},
		{"ST_HERE", 32UL},
		{"ST_THERE", 64UL},
		{"ST_I", 128UL},
		{"ST_MORE", 256UL},
		{"ST_BACK", 512UL},
		{"ST_REVERSE", 1024UL}
	};

	for (size_t i = 0; i < sizeof(subtypes) / sizeof(subtypes[0]); ++i)
	{
		if (BotChat_StringEqualsIgnoreCase(identifier, subtypes[i].name))
		{
			return subtypes[i].value;
		}
	}

	return 0UL;
}

/*
=============
BotChat_MessageSubtypeFromToken

Resolves the optional match-template subtype token.
=============
*/
static unsigned long BotChat_MessageSubtypeFromToken(const pc_token_t *token)
{
	if (token == NULL)
	{
		return 0UL;
	}
	if (token->type == TT_NUMBER)
	{
		return BotChat_NumberTokenIsInteger(token)
			? BotChat_NumberTokenValue(token)
			: 0UL;
	}
	if (token->type == TT_NAME)
	{
		return BotChat_MessageSubtypeFromIdentifier(token->string);
	}

	return 0UL;
}

/*
=============
BotChat_VariableNameForNumber

Maps expanded match variable indices back to the readable retail placeholder.
=============
*/
static const char *BotChat_VariableNameForNumber(unsigned long message_type,
	unsigned long value)
{
	switch (value)
	{
		case 0:
			return (message_type == BOT_CHAT_CONTEXT_ENTERGAME) ? "NETNAME" : "VICTIM";
		case 1:
			return (message_type == BOT_CHAT_CONTEXT_ENTERGAME) ? "ADDRESSEE" : "KILLER";
		case 2:
			return "ITEM";
		case 3:
			return "TEAMMATE";
		case 4:
			return "GENDER";
		case 5:
			return "TIME";
		case 7:
			return "THE_ENEMY";
		case 8:
			return "THE_TEAM";
		case 9:
			return "TEAM";
		default:
			break;
	}

	return NULL;
}

/*
=============
BotChat_AppendMappedVariable

Appends the named placeholder for a preprocessor-expanded variable reference.
=============
*/
static int BotChat_AppendMappedVariable(bot_string_builder_t *builder,
	unsigned long message_type,
	unsigned long value)
{
	const char *name = BotChat_VariableNameForNumber(message_type, value);
	if (name == NULL)
	{
		char fallback[32];
		int written = snprintf(fallback, sizeof(fallback), "VAR%lu", value);
		if (written <= 0 || (size_t)written >= sizeof(fallback))
		{
			return 0;
		}
		name = fallback;
		return BotChat_StringBuilderAppendIdentifier(builder, name, strlen(name));
	}

	return BotChat_StringBuilderAppendIdentifier(builder, name, strlen(name));
}

/*
=============
BotChat_RewriteVariablesForMessageType

Converts temporary retail variable markers into readable placeholders once
MSG_* is known.
=============
*/
static char *BotChat_RewriteVariablesForMessageType(const char *template_text,
	unsigned long message_type)
{
	bot_string_builder_t builder = {0};

	for (size_t i = 0; template_text != NULL && template_text[i] != '\0';)
	{
		if (!BotChat_IsEscapeChar(template_text[i]) || template_text[i + 1] != 'v')
		{
			if (!BotChat_StringBuilderAppendChar(&builder, template_text[i++]))
			{
				BotChat_StringBuilderDestroy(&builder);
				return NULL;
			}
			continue;
		}

		i += 2;
		unsigned long value = 0;
		int saw_digit = 0;
		while (isdigit((unsigned char)template_text[i]))
		{
			value = value * 10UL + (unsigned long)(template_text[i] - '0');
			saw_digit = 1;
			++i;
		}

		if (!saw_digit || !BotChat_IsEscapeChar(template_text[i]))
		{
			BotChat_StringBuilderDestroy(&builder);
			return NULL;
		}
		++i;

		if (!BotChat_AppendMappedVariable(&builder, message_type, value))
		{
			BotChat_StringBuilderDestroy(&builder);
			return NULL;
		}
	}

	return BotChat_StringBuilderDetach(&builder);
}

/*
=============
BotChat_VariableNumberForName

Maps readable match placeholders back to their retail variable slots.
=============
*/
static int BotChat_VariableNumberForName(const char *name,
	unsigned long context,
	unsigned long *value)
{
	(void)context;
	if (name == NULL || value == NULL)
	{
		return 0;
	}

	if (strcmp(name, "VICTIM") == 0 || strcmp(name, "NETNAME") == 0)
	{
		*value = 0;
		return 1;
	}
	if (strcmp(name, "KILLER") == 0 || strcmp(name, "ADDRESSEE") == 0)
	{
		*value = 1;
		return 1;
	}
	if (strcmp(name, "ITEM") == 0)
	{
		*value = 2;
		return 1;
	}
	if (strcmp(name, "TEAMMATE") == 0 || strcmp(name, "TEAMNAME") == 0)
	{
		*value = 3;
		return 1;
	}
	if (strcmp(name, "GENDER") == 0 || strcmp(name, "GENDER_HE") == 0
		|| strcmp(name, "GENDER_HIS") == 0 || strcmp(name, "GENDER_HIM") == 0
		|| strcmp(name, "GENDER_GOD") == 0 || strcmp(name, "KEYAREA") == 0
		|| strcmp(name, "FORMATION") == 0 || strcmp(name, "POSITION") == 0
		|| strcmp(name, "NUMBER") == 0)
	{
		*value = 4;
		return 1;
	}
	if (strcmp(name, "TIME") == 0 || strcmp(name, "NAME") == 0
		|| strcmp(name, "MORE") == 0)
	{
		*value = 5;
		return 1;
	}
	if (strcmp(name, "THE_ENEMY") == 0)
	{
		*value = 7;
		return 1;
	}
	if (strcmp(name, "THE_TEAM") == 0)
	{
		*value = 8;
		return 1;
	}
	if (strcmp(name, "TEAM") == 0)
	{
		*value = 9;
		return 1;
	}
	return 0;
}

/*
=============
BotChat_AppendEscapedVariable

Appends an internal retail capture marker to a pattern builder.
=============
*/
static int BotChat_AppendEscapedVariable(bot_string_builder_t *builder,
	unsigned long value)
{
	char buffer[32];
	int written = snprintf(buffer, sizeof(buffer), "v%lu", value);
	if (written <= 0 || (size_t)written >= sizeof(buffer))
	{
		return 0;
	}
	return BotChat_StringBuilderAppendChar(builder, BOT_CHAT_ESCAPE_CHAR)
		&& BotChat_StringBuilderAppend(builder, buffer)
		&& BotChat_StringBuilderAppendChar(builder, BOT_CHAT_ESCAPE_CHAR);
}

/*
=============
BotChat_TemplateVariablePattern

Converts readable {PLACEHOLDER} spans into internal \vN\ markers.
=============
*/
static char *BotChat_TemplateVariablePattern(const char *template_text,
	unsigned long context)
{
	if (template_text == NULL)
	{
		return NULL;
	}

	bot_string_builder_t builder = {0};
	for (size_t i = 0; template_text[i] != '\0';)
	{
		if (template_text[i] != '{')
		{
			if (!BotChat_StringBuilderAppendChar(&builder, template_text[i++]))
			{
				BotChat_StringBuilderDestroy(&builder);
				return NULL;
			}
			continue;
		}

		size_t end = i + 1;
		while (template_text[end] != '\0' && template_text[end] != '}')
		{
			++end;
		}
		if (template_text[end] != '}')
		{
			if (!BotChat_StringBuilderAppendChar(&builder, template_text[i++]))
			{
				BotChat_StringBuilderDestroy(&builder);
				return NULL;
			}
			continue;
		}

		char name[64];
		size_t name_length = end - i - 1;
		if (name_length >= sizeof(name))
		{
			name_length = sizeof(name) - 1;
		}
		memcpy(name, template_text + i + 1, name_length);
		name[name_length] = '\0';

		unsigned long value = 0;
		if (!BotChat_VariableNumberForName(name, context, &value)
			|| !BotChat_AppendEscapedVariable(&builder, value))
		{
			if (!BotChat_StringBuilderAppendSpan(&builder,
					template_text + i,
					end - i + 1))
			{
				BotChat_StringBuilderDestroy(&builder);
				return NULL;
			}
		}
		i = end + 1;
	}

	return BotChat_StringBuilderDetach(&builder);
}

/*
=============
BotChat_CharEqualsIgnoreCase

Compares ASCII characters without case sensitivity.
=============
*/
static int BotChat_CharEqualsIgnoreCase(char lhs, char rhs)
{
	return tolower((unsigned char)lhs) == tolower((unsigned char)rhs);
}

/*
=============
BotChat_FindCaseInsensitiveSpan

Finds a literal span in text at or after the supplied offset.
=============
*/
static int BotChat_FindCaseInsensitiveSpan(const char *text,
	size_t start,
	const char *needle,
	size_t needle_length,
	size_t *match_start,
	size_t *match_end)
{
	if (text == NULL || needle == NULL)
	{
		return 0;
	}
	if (needle_length == 0)
	{
		if (match_start != NULL)
		{
			*match_start = start;
		}
		if (match_end != NULL)
		{
			*match_end = start;
		}
		return 1;
	}

	const size_t text_length = strlen(text);
	if (start > text_length || needle_length > text_length)
	{
		return 0;
	}

	for (size_t i = start; i + needle_length <= text_length; ++i)
	{
		int matches = 1;
		for (size_t j = 0; j < needle_length; ++j)
		{
			if (!BotChat_CharEqualsIgnoreCase(text[i + j], needle[j]))
			{
				matches = 0;
				break;
			}
		}
		if (!matches)
		{
			continue;
		}

		if (match_start != NULL)
		{
			*match_start = i;
		}
		if (match_end != NULL)
		{
			*match_end = i + needle_length;
		}
		return 1;
	}

	return 0;
}

/*
=============
BotChat_PatternAtVariableReference

Checks whether an internal match pattern points at a variable capture marker.
=============
*/
static int BotChat_PatternAtVariableReference(const char *pattern, size_t offset)
{
	return pattern != NULL
		&& BotChat_IsEscapeChar(pattern[offset])
		&& pattern[offset + 1] == 'v';
}

/*
=============
BotChat_MatchAlternativePatternPiece

Selects the first Q3 string-piece alternative that occurs in the message.
=============
*/
static int BotChat_MatchAlternativePatternPiece(const char *pattern,
	size_t pattern_offset,
	const char *message,
	size_t message_offset,
	size_t *next_pattern_offset,
	size_t *match_start,
	size_t *match_end)
{
	if (pattern == NULL || message == NULL
		|| pattern[pattern_offset] != BOT_CHAT_MATCH_ALT_START)
	{
		return 0;
	}

	size_t alternative_start = pattern_offset + 1;
	for (size_t offset = alternative_start;; ++offset)
	{
		if (pattern[offset] == '\0')
		{
			return 0;
		}
		if (pattern[offset] != BOT_CHAT_MATCH_ALT_SEPARATOR
			&& pattern[offset] != BOT_CHAT_MATCH_ALT_END)
		{
			continue;
		}

		const size_t alternative_length = offset - alternative_start;
		size_t candidate_start = 0;
		size_t candidate_end = 0;
		if (BotChat_FindCaseInsensitiveSpan(message,
				message_offset,
				pattern + alternative_start,
				alternative_length,
				&candidate_start,
				&candidate_end))
		{
			size_t end_offset = offset;
			while (pattern[end_offset] != BOT_CHAT_MATCH_ALT_END)
			{
				if (pattern[end_offset] == '\0')
				{
					return 0;
				}
				++end_offset;
			}
			if (next_pattern_offset != NULL)
			{
				*next_pattern_offset = end_offset + 1;
			}
			if (match_start != NULL)
			{
				*match_start = candidate_start;
			}
			if (match_end != NULL)
			{
				*match_end = candidate_end;
			}
			return 1;
		}

		if (pattern[offset] == BOT_CHAT_MATCH_ALT_END)
		{
			return 0;
		}
		alternative_start = offset + 1;
	}
}

/*
=============
BotChat_CopyCapturedVariable

Copies one captured span into the variable table used by construction.
=============
*/
static int BotChat_CopyCapturedVariable(const char *message,
	size_t offset,
	size_t length,
	unsigned long variable,
	char storage[][BOT_CHAT_MAX_MESSAGE_CHARS],
	const char *variables[BOT_CHAT_MAX_MATCH_VARIABLES])
{
	if (message == NULL || storage == NULL || variables == NULL
		|| variable >= BOT_CHAT_MAX_MATCH_VARIABLES)
	{
		return 0;
	}

	if (length >= BOT_CHAT_MAX_MESSAGE_CHARS)
	{
		length = BOT_CHAT_MAX_MESSAGE_CHARS - 1;
	}
	memcpy(storage[variable], message + offset, length);
	storage[variable][length] = '\0';
	variables[variable] = storage[variable];
	return 1;
}

/*
=============
BotChat_ClearCapturedVariables

Resets construction variable pointers.
=============
*/
static void BotChat_ClearCapturedVariables(
	const char *variables[BOT_CHAT_MAX_MATCH_VARIABLES])
{
	if (variables == NULL)
	{
		return;
	}

	for (size_t i = 0; i < BOT_CHAT_MAX_MATCH_VARIABLES; ++i)
	{
		variables[i] = NULL;
	}
}

/*
=============
BotChat_CopyCapturedVariables

Copies captured variables between stack-backed capture tables.
=============
*/
static void BotChat_CopyCapturedVariables(
	char destination_storage[][BOT_CHAT_MAX_MESSAGE_CHARS],
	const char *destination[BOT_CHAT_MAX_MATCH_VARIABLES],
	const char *const source[BOT_CHAT_MAX_MATCH_VARIABLES])
{
	BotChat_ClearCapturedVariables(destination);
	if (destination_storage == NULL || source == NULL)
	{
		return;
	}

	for (size_t i = 0; i < BOT_CHAT_MAX_MATCH_VARIABLES; ++i)
	{
		if (source[i] == NULL)
		{
			continue;
		}
		snprintf(destination_storage[i],
			BOT_CHAT_MAX_MESSAGE_CHARS,
			"%s",
			source[i]);
		destination[i] = destination_storage[i];
	}
}

/*
=============
BotChat_MatchVariablePattern

Matches a retail-style pattern containing \vN\ captures against a message.
=============
*/
static int BotChat_MatchVariablePattern(const char *pattern,
	const char *message,
	char storage[][BOT_CHAT_MAX_MESSAGE_CHARS],
	const char *variables[BOT_CHAT_MAX_MATCH_VARIABLES])
{
	if (pattern == NULL || message == NULL)
	{
		return 0;
	}

	BotChat_ClearCapturedVariables(variables);

	size_t pattern_offset = 0;
	size_t message_offset = 0;
	long last_variable = -1;

	while (pattern[pattern_offset] != '\0')
	{
		if (pattern[pattern_offset] == BOT_CHAT_MATCH_PIECE_SEPARATOR)
		{
			++pattern_offset;
			continue;
		}

		if (BotChat_PatternAtVariableReference(pattern, pattern_offset))
		{
			size_t value_offset = pattern_offset + 2;
			unsigned long value = 0;
			int saw_digit = 0;
			while (isdigit((unsigned char)pattern[value_offset]))
			{
				value = value * 10UL + (unsigned long)(pattern[value_offset] - '0');
				saw_digit = 1;
				++value_offset;
			}
			if (!saw_digit || !BotChat_IsEscapeChar(pattern[value_offset])
				|| value >= BOT_CHAT_MAX_MATCH_VARIABLES)
			{
				return 0;
			}

			last_variable = (long)value;
			pattern_offset = value_offset + 1;
			continue;
		}

		if (pattern[pattern_offset] == BOT_CHAT_MATCH_ALT_START)
		{
			size_t next_pattern_offset = 0;
			size_t match_start = 0;
			size_t match_end = 0;
			if (!BotChat_MatchAlternativePatternPiece(pattern,
					pattern_offset,
					message,
					message_offset,
					&next_pattern_offset,
					&match_start,
					&match_end))
			{
				return 0;
			}

			if (last_variable >= 0)
			{
				if (!BotChat_CopyCapturedVariable(message,
						message_offset,
						match_start - message_offset,
						(unsigned long)last_variable,
						storage,
						variables))
				{
					return 0;
				}
				last_variable = -1;
			}
			else if (match_start != message_offset)
			{
				return 0;
			}
			message_offset = match_end;
			pattern_offset = next_pattern_offset;
			continue;
		}

		const size_t literal_start = pattern_offset;
		while (pattern[pattern_offset] != '\0'
			&& !BotChat_PatternAtVariableReference(pattern, pattern_offset)
			&& pattern[pattern_offset] != BOT_CHAT_MATCH_ALT_START
			&& pattern[pattern_offset] != BOT_CHAT_MATCH_PIECE_SEPARATOR)
		{
			++pattern_offset;
		}

		const size_t literal_length = pattern_offset - literal_start;
		size_t match_start = 0;
		size_t match_end = 0;
		if (!BotChat_FindCaseInsensitiveSpan(message,
				message_offset,
				pattern + literal_start,
				literal_length,
				&match_start,
				&match_end))
		{
			return 0;
		}

		if (last_variable >= 0)
		{
			if (!BotChat_CopyCapturedVariable(message,
					message_offset,
					match_start - message_offset,
					(unsigned long)last_variable,
					storage,
					variables))
			{
				return 0;
			}
			last_variable = -1;
		}
		else if (match_start != message_offset)
		{
			return 0;
		}
		message_offset = match_end;
	}

	if (last_variable >= 0)
	{
		return BotChat_CopyCapturedVariable(message,
			message_offset,
			strlen(message + message_offset),
			(unsigned long)last_variable,
			storage,
			variables);
	}

	return message[message_offset] == '\0';
}

/*
=============
BotChat_ClearMatchResultVariables

Resets only public match variable pointers before trying a template. Retail
leaves stale lengths untouched until the corresponding pointer is captured.
=============
*/
static void BotChat_ClearMatchResultVariables(bot_match_t *match)
{
	if (match == NULL)
	{
		return;
	}

	for (size_t i = 0; i < BOT_MATCH_MAX_VARIABLES; ++i)
	{
		match->variables[i].ptr = NULL;
	}
}

/*
=============
BotChat_SetMatchResultVariable

Stores a captured variable span in a public BotFindMatch result.
=============
*/
static int BotChat_SetMatchResultVariable(bot_match_t *match,
	unsigned long variable,
	size_t offset,
	size_t length)
{
	if (match == NULL || variable >= BOT_MATCH_MAX_VARIABLES)
	{
		return 0;
	}
	/*
	 * Retail `StringsMatch` measures a variable from where it started to where
	 * the following literal was found, so the separators around a capture are
	 * decided by the literal's own leading and trailing spaces.  Every shipped
	 * obituary template in `bots/match.c` spaces its literals (`VICTIM,
	 * " was railed by ", KILLER`), which leaves the capture already trimmed.
	 * The obituary context resolves to the unspaced alternates later in that
	 * file, so a raw span keeps the separator on both sides of a capture.
	 * Every consumer here resolves captures against client names, so both runs
	 * are dropped.  Two parity expectations in test_bot_interface.c want the
	 * victim's trailing space preserved; honouring that needs the name
	 * consumers trimming instead, which is tracked separately rather than
	 * changed speculatively here.
	 */
	while (length > 0U && match->string[offset] == ' ')
	{
		++offset;
		--length;
	}
	while (length > 0U && match->string[offset + length - 1U] == ' ')
	{
		--length;
	}

	match->variables[variable].ptr = match->string + offset;
	match->variables[variable].length = (int)length;
	return 1;
}

/*
=============
BotChat_MatchVariablePatternResult

Matches an internal variable-capture pattern and records public offset/length spans.
=============
*/
static int BotChat_MatchVariablePatternResult(const char *pattern,
	bot_match_t *match)
{
	if (pattern == NULL || match == NULL)
	{
		return 0;
	}

	const char *message = match->string;
	size_t pattern_offset = 0;
	size_t message_offset = 0;
	long last_variable = -1;

	BotChat_ClearMatchResultVariables(match);
	while (pattern[pattern_offset] != '\0')
	{
		if (pattern[pattern_offset] == BOT_CHAT_MATCH_PIECE_SEPARATOR)
		{
			++pattern_offset;
			continue;
		}

		if (BotChat_PatternAtVariableReference(pattern, pattern_offset))
		{
			size_t value_offset = pattern_offset + 2;
			unsigned long value = 0;
			int saw_digit = 0;
			while (isdigit((unsigned char)pattern[value_offset]))
			{
				value = value * 10UL + (unsigned long)(pattern[value_offset] - '0');
				saw_digit = 1;
				++value_offset;
			}
			if (!saw_digit || !BotChat_IsEscapeChar(pattern[value_offset])
				|| value >= BOT_MATCH_MAX_VARIABLES)
			{
				return 0;
			}

			last_variable = (long)value;
			pattern_offset = value_offset + 1;
			continue;
		}

		if (pattern[pattern_offset] == BOT_CHAT_MATCH_ALT_START)
		{
			size_t next_pattern_offset = 0;
			size_t match_start = 0;
			size_t match_end = 0;
			if (!BotChat_MatchAlternativePatternPiece(pattern,
					pattern_offset,
					message,
					message_offset,
					&next_pattern_offset,
					&match_start,
					&match_end))
			{
				return 0;
			}

			if (last_variable >= 0)
			{
				if (!BotChat_SetMatchResultVariable(match,
						(unsigned long)last_variable,
						message_offset,
						match_start - message_offset))
				{
					return 0;
				}
				last_variable = -1;
			}
			else if (match_start != message_offset)
			{
				return 0;
			}
			message_offset = match_end;
			pattern_offset = next_pattern_offset;
			continue;
		}

		const size_t literal_start = pattern_offset;
		while (pattern[pattern_offset] != '\0'
			&& !BotChat_PatternAtVariableReference(pattern, pattern_offset)
			&& pattern[pattern_offset] != BOT_CHAT_MATCH_ALT_START
			&& pattern[pattern_offset] != BOT_CHAT_MATCH_PIECE_SEPARATOR)
		{
			++pattern_offset;
		}

		const size_t literal_length = pattern_offset - literal_start;
		size_t match_start = 0;
		size_t match_end = 0;
		if (!BotChat_FindCaseInsensitiveSpan(message,
				message_offset,
				pattern + literal_start,
				literal_length,
				&match_start,
				&match_end))
		{
			return 0;
		}

		if (last_variable >= 0)
		{
			if (!BotChat_SetMatchResultVariable(match,
					(unsigned long)last_variable,
					message_offset,
					match_start - message_offset))
			{
				return 0;
			}
			last_variable = -1;
		}
		else if (match_start != message_offset)
		{
			return 0;
		}
		message_offset = match_end;
	}

	if (last_variable >= 0)
	{
		return BotChat_SetMatchResultVariable(match,
			(unsigned long)last_variable,
			message_offset,
			strlen(message + message_offset));
	}

	return message[message_offset] == '\0';
}

/*
=============
BotChat_IsReplyWordSeparator

Returns true only for Gladiator's literal-space word separator.
=============
*/
static int BotChat_IsReplyWordSeparator(char character)
{
	return character == ' ';
}

/*
=============
BotChat_IsCompatibilityReplyWordSeparator

Recognizes the four Q3 compatibility word separators.
=============
*/
static int BotChat_IsCompatibilityReplyWordSeparator(char character)
{
	return character == ' ' || character == '.'
		|| character == ',' || character == '!';
}

/*
=============
BotChat_CompatibilityStringContainsWord

Performs Q3-shaped reply word matching without changing retail raw-string
key behavior.
=============
*/
static int BotChat_CompatibilityStringContainsWord(const char *text,
	const char *needle)
{
	if (text == NULL || needle == NULL)
	{
		return 0;
	}

	const size_t needle_length = strlen(needle);
	for (const char *cursor = text; *cursor != '\0'; ++cursor)
	{
		const char *match = StringContains(cursor, needle, 0);
		if (match == NULL)
		{
			return 0;
		}
		if ((match == text
				|| BotChat_IsCompatibilityReplyWordSeparator(match[-1]))
			&& (match[needle_length] == '\0'
				|| BotChat_IsCompatibilityReplyWordSeparator(
					match[needle_length])))
		{
			return 1;
		}
		cursor = match;
	}
	return 0;
}

/*
=============
BotChat_StringContainsWordCaseInsensitive

Checks whether a reply key string occurs as a retail reply-chat word.
=============
*/
static int BotChat_StringContainsWordCaseInsensitive(const char *text,
	const char *needle)
{
	if (text == NULL || needle == NULL)
	{
		return 0;
	}

	const size_t needle_length = strlen(needle);
	if (needle_length == 0U)
	{
		return 1;
	}

	size_t start = 0;
	while (text[start] != '\0')
	{
		size_t index = 0;
		while (needle[index] != '\0')
		{
			if (text[start + index] == '\0')
			{
				break;
			}
			const int left = toupper((unsigned char)text[start + index]);
			const int right = toupper((unsigned char)needle[index]);
			if (left != right)
			{
				break;
			}
			++index;
		}
		if (index == needle_length
			&& (text[start + index] == '\0'
				|| BotChat_IsReplyWordSeparator(text[start + index])))
		{
			return 1;
		}

		while (text[start] != '\0'
			&& !BotChat_IsReplyWordSeparator(text[start]))
		{
			++start;
		}
		if (text[start] == '\0')
		{
			break;
		}
		++start;
	}

	return 0;
}

/*
=============
BotChat_StringContainsCaseInsensitive

Checks for a case-insensitive substring without word-boundary filtering.
=============
*/
static int BotChat_StringContainsCaseInsensitive(const char *text,
	const char *needle)
{
	if (text == NULL || needle == NULL)
	{
		return 0;
	}

	return BotChat_FindCaseInsensitiveSpan(text,
		0,
		needle,
		strlen(needle),
		NULL,
		NULL);
}

/*
=============
BotChat_SynonymContextMaskFromName

Maps parsed CONTEXT_* names back to the bitmask values consumed by Q3 chat
construction.
=============
*/
static unsigned long BotChat_SynonymContextMaskFromName(const char *context_name)
{
	if (context_name == NULL)
	{
		return 0UL;
	}

	if (BotChat_StringEqualsIgnoreCase(context_name, "CONTEXT_ALL"))
	{
		return 0xFFFFFFFFUL;
	}
	if (BotChat_StringEqualsIgnoreCase(context_name, "CONTEXT_NORMAL"))
	{
		return 1UL;
	}
	if (BotChat_StringEqualsIgnoreCase(context_name, "CONTEXT_NEARBYITEM"))
	{
		return 2UL;
	}
	if (BotChat_StringEqualsIgnoreCase(context_name, "CONTEXT_CTFREDTEAM"))
	{
		return 4UL;
	}
	if (BotChat_StringEqualsIgnoreCase(context_name, "CONTEXT_CTFBLUETEAM"))
	{
		return 8UL;
	}
	if (BotChat_StringEqualsIgnoreCase(context_name, "CONTEXT_REPLY"))
	{
		return 16UL;
	}
	if (BotChat_StringEqualsIgnoreCase(context_name, "CONTEXT_OBELISKREDTEAM"))
	{
		return 32UL;
	}
	if (BotChat_StringEqualsIgnoreCase(context_name, "CONTEXT_OBELISKBLUETEAM"))
	{
		return 64UL;
	}
	if (BotChat_StringEqualsIgnoreCase(context_name, "CONTEXT_HARVESTERREDTEAM"))
	{
		return 128UL;
	}
	if (BotChat_StringEqualsIgnoreCase(context_name, "CONTEXT_HARVESTERBLUETEAM"))
	{
		return 256UL;
	}
	if (BotChat_StringEqualsIgnoreCase(context_name, "CONTEXT_NAMES"))
	{
		return 1024UL;
	}

	if (strncmp(context_name, "CONTEXT_", 8) == 0
		&& isdigit((unsigned char)context_name[8]))
	{
		char *end = NULL;
		const unsigned long value = strtoul(context_name + 8, &end, 0);
		if (end != NULL && *end == '\0')
		{
			return value;
		}
	}

	return 0UL;
}

/*
=============
BotChat_SynonymContextApplies

Returns true when a parsed synonym context participates in the requested
message context mask.
=============
*/
static int BotChat_SynonymContextApplies(const bot_synonym_context_t *context,
	unsigned long message_context)
{
	if (context == NULL || message_context == 0UL)
	{
		return 0;
	}

	const unsigned long context_mask = context->context_mask != 0UL
		? context->context_mask
		: BotChat_SynonymContextMaskFromName(context->context_name);
	return (context_mask & message_context) != 0UL;
}

/*
=============
BotChat_SelectWeightedSynonymFromGroup

Chooses the replacement phrase for one retail synonym group.
=============
*/
static const bot_synonym_phrase_t *BotChat_SelectWeightedSynonymFromGroup(
	const bot_synonym_group_t *group)
{
	if (group == NULL || group->phrase_count == 0)
	{
		return NULL;
	}

	float total_weight = 0.0f;
	for (size_t i = 0; i < group->phrase_count; ++i)
	{
		const bot_synonym_phrase_t *phrase = &group->phrases[i];
		if (phrase->text == NULL)
		{
			continue;
		}
		total_weight += phrase->weight;
	}

	const float roll = BotChat_RetailRandomFloat() * total_weight;
	float cumulative = 0.0f;
	for (size_t i = 0; i < group->phrase_count; ++i)
	{
		const bot_synonym_phrase_t *phrase = &group->phrases[i];
		if (phrase->text == NULL)
		{
			continue;
		}
		cumulative += phrase->weight;
		if (roll < cumulative)
		{
			return phrase;
		}
	}

	/* HOST SAFETY: retail dereferences NULL on the inclusive endpoint. */
	return NULL;
}

/*
=============
BotChat_SpanIsWordMatch

Checks Q3 StringContainsWord boundaries around a candidate replacement span.
=============
*/
static int BotChat_SpanIsWordMatch(const char *text, size_t start, size_t end)
{
	if (text == NULL || start > end)
	{
		return 0;
	}

	if (start > 0 && !BotChat_IsReplyWordSeparator(text[start - 1]))
	{
		return 0;
	}
	if (text[end] != '\0' && !BotChat_IsReplyWordSeparator(text[end]))
	{
		return 0;
	}
	return 1;
}

/*
=============
BotChat_ReplaceWeightedSynonymsFromState

Applies weighted synonym groups from one parsed chat state.
=============
*/
static int BotChat_ReplaceWeightedSynonymsFromState(const bot_chatstate_t *source,
	unsigned long message_context,
	char *message,
	size_t message_size,
	const char *source_template,
	int *applied_context)
{
	(void)message_size;
	(void)source_template;
	if (source == NULL || message == NULL || message_context == 0UL)
	{
		return 1;
	}

	for (size_t context_index = 0;
		context_index < source->synonym_context_count;
		++context_index)
	{
		const bot_synonym_context_t *context =
			&source->synonym_contexts[context_index];
		if (!BotChat_SynonymContextApplies(context, message_context))
		{
			continue;
		}
		if (applied_context != NULL)
		{
			*applied_context = 1;
		}

		for (size_t group_index = 0;
			group_index < context->group_count;
			++group_index)
		{
			const bot_synonym_group_t *group = &context->groups[group_index];
			const bot_synonym_phrase_t *replacement =
				BotChat_SelectWeightedSynonymFromGroup(group);
			if (replacement == NULL || replacement->text == NULL)
			{
				continue;
			}

			for (size_t phrase_index = 0;
				phrase_index < group->phrase_count;
				++phrase_index)
			{
				const bot_synonym_phrase_t *phrase = &group->phrases[phrase_index];
				if (phrase == replacement || phrase->text == NULL)
				{
					continue;
				}
				StringReplaceWords(message,
					phrase->text,
					replacement->text);
			}
		}
	}

	return 1;
}

/*
=============
BotChat_ReplaceWeightedSynonyms

Applies the retail post-expansion weighted synonym replacement pass.
=============
*/
static int BotChat_ReplaceWeightedSynonyms(bot_chatstate_t *state,
	unsigned long message_context,
	char *message,
	size_t message_size,
	const char *source_template)
{
	(void)state;
	int applied_context = 0;
	return BotChat_ReplaceWeightedSynonymsFromState(bot_chat_setup_state,
		message_context,
		message,
		message_size,
		source_template,
		&applied_context);
}

/*
=============
BotChat_ReplaceSynonymsFromState

Applies the non-random retail synonym canonicalization pass.
=============
*/
static int BotChat_ReplaceSynonymsFromState(const bot_chatstate_t *source,
	unsigned long message_context,
	char *message,
	size_t message_size)
{
	(void)message_size;
	if (source == NULL || message == NULL || message_context == 0UL)
	{
		return 1;
	}

	for (size_t context_index = 0;
		context_index < source->synonym_context_count;
		++context_index)
	{
		const bot_synonym_context_t *context =
			&source->synonym_contexts[context_index];
		if (!BotChat_SynonymContextApplies(context, message_context))
		{
			continue;
		}

		for (size_t group_index = 0;
			group_index < context->group_count;
			++group_index)
		{
			const bot_synonym_group_t *group = &context->groups[group_index];
			if (group->phrase_count == 0 || group->phrases[0].text == NULL)
			{
				continue;
			}

			const char *replacement = group->phrases[0].text;
			for (size_t phrase_index = 1;
				phrase_index < group->phrase_count;
				++phrase_index)
			{
				const bot_synonym_phrase_t *phrase = &group->phrases[phrase_index];
				if (phrase->text == NULL)
				{
					continue;
				}
				StringReplaceWords(message, phrase->text, replacement);
			}
		}
	}

	return 1;
}

/*
=============
BotChat_PhraseStartsWord

Checks whether a phrase starts at the current word using the retail word-boundary
rules used by reply synonym replacement.
=============
*/
static int BotChat_PhraseStartsWord(const char *text, const char *phrase)
{
	if (text == NULL || phrase == NULL || phrase[0] == '\0')
	{
		return 0;
	}

	const size_t phrase_length = strlen(phrase);
	for (size_t i = 0; i < phrase_length; ++i)
	{
		if (text[i] == '\0')
		{
			return 0;
		}
		if (toupper((unsigned char)text[i])
			!= toupper((unsigned char)phrase[i]))
		{
			return 0;
		}
	}

	return BotChat_SpanIsWordMatch(text, 0, phrase_length);
}

/*
=============
BotChat_ReplaceReplySynonymsFromState

Applies Q3's reply-variable synonym canonicalization pass to one state.
=============
*/
static int BotChat_ReplaceReplySynonymsFromState(const bot_chatstate_t *source,
	unsigned long message_context,
	char *message,
	size_t message_size,
	int *applied_context)
{
	if (source == NULL || message == NULL || message_context == 0UL)
	{
		return 1;
	}

	for (char *cursor = message; *cursor != '\0';)
	{
		while (*cursor != '\0' && isspace((unsigned char)*cursor))
		{
			++cursor;
		}
		if (*cursor == '\0')
		{
			break;
		}

		int replaced = 0;
		for (size_t context_index = 0;
			context_index < source->synonym_context_count && !replaced;
			++context_index)
		{
			const bot_synonym_context_t *context =
				&source->synonym_contexts[context_index];
			if (!BotChat_SynonymContextApplies(context, message_context))
			{
				continue;
			}
			if (applied_context != NULL)
			{
				*applied_context = 1;
			}

			for (size_t group_index = 0;
				group_index < context->group_count && !replaced;
				++group_index)
			{
				const bot_synonym_group_t *group =
					&context->groups[group_index];
				if (group->phrase_count < 2
					|| group->phrases[0].text == NULL)
				{
					continue;
				}

				const char *replacement = group->phrases[0].text;
				for (size_t phrase_index = 1;
					phrase_index < group->phrase_count;
					++phrase_index)
				{
					const bot_synonym_phrase_t *phrase =
						&group->phrases[phrase_index];
					if (phrase->text == NULL
						|| !BotChat_PhraseStartsWord(cursor, phrase->text)
						|| BotChat_PhraseStartsWord(cursor, replacement))
					{
						continue;
					}

					const size_t synonym_length = strlen(phrase->text);
					const size_t replacement_length = strlen(replacement);
					const size_t current_length = strlen(message);
					const size_t new_length =
						current_length - synonym_length + replacement_length;
					if (new_length >= message_size
						|| new_length >= BOT_CHAT_MAX_MESSAGE_CHARS)
					{
						BotLib_Print(PRT_ERROR,
							"BotConstructChat: message \"%s\" too long\n",
							message);
						return 0;
					}

					memmove(cursor + replacement_length,
						cursor + synonym_length,
						strlen(cursor + synonym_length) + 1U);
					memcpy(cursor, replacement, replacement_length);
					replaced = 1;
					break;
				}
			}
		}

		while (*cursor != '\0' && !isspace((unsigned char)*cursor))
		{
			++cursor;
		}
	}

	return 1;
}

/*
=============
BotChat_ReplaceReplySynonyms

Applies reply synonym canonicalization using the active chat state and setup
fallback, matching Q3's separate vcontext pass for reply variables.
=============
*/
static int BotChat_ReplaceReplySynonyms(bot_chatstate_t *state,
	unsigned long variable_context,
	char *message,
	size_t message_size)
{
	int applied_context = 0;
	if (!BotChat_ReplaceReplySynonymsFromState(state,
		variable_context,
		message,
		message_size,
		&applied_context))
	{
		return 0;
	}

	const bot_chatstate_t *fallback_state = BotChat_SetupFallbackState(state);
	if (!applied_context
		&& !BotChat_ReplaceReplySynonymsFromState(fallback_state,
			variable_context,
			message,
			message_size,
			&applied_context))
	{
		return 0;
	}

	return 1;
}

/*
=============
BotChat_ReplaceVariableSynonyms

Applies the non-reply vcontext synonym pass to a copied variable value.
=============
*/
static int BotChat_ReplaceVariableSynonyms(bot_chatstate_t *state,
	unsigned long variable_context,
	char *message,
	size_t message_size)
{
	(void)state;
	if (variable_context == 0UL)
	{
		return 1;
	}
	return BotChat_ReplaceSynonymsFromState(bot_chat_setup_state,
		variable_context,
		message,
		message_size);
}

/*
=============
BotReplaceSynonyms

Public chat export that canonicalizes synonyms through the setup cache.
=============
*/
void BotReplaceSynonyms(char *string, unsigned long int context)
{
	if (string == NULL)
	{
		return;
	}

	(void)BotChat_ReplaceSynonymsFromState(bot_chat_setup_state,
		context,
		string,
		BOT_CHAT_MAX_MESSAGE_CHARS);
}

/*
=============
BotChat_CaptureMatchTemplateVariables

Captures readable match-template placeholders from an incoming message.
=============
*/
static int BotChat_CaptureMatchTemplateVariables(const char *template_text,
	const char *message,
	unsigned long context,
	char storage[][BOT_CHAT_MAX_MESSAGE_CHARS],
	const char *variables[BOT_CHAT_MAX_MATCH_VARIABLES])
{
	char *pattern = BotChat_TemplateVariablePattern(template_text, context);
	if (pattern == NULL)
	{
		return 0;
	}

	const int matched = BotChat_MatchVariablePattern(pattern,
		message,
		storage,
		variables);
	free(pattern);
	return matched;
}

/*
=============
BotChat_StringEqualsIgnoreCase

Compares two ASCII script identifiers without case sensitivity.
=============
*/
static int BotChat_StringEqualsIgnoreCase(const char *lhs, const char *rhs)
{
	if (lhs == NULL || rhs == NULL)
	{
		return 0;
	}

	while (*lhs != '\0' && *rhs != '\0')
	{
		const int left = toupper((unsigned char)*lhs);
		const int right = toupper((unsigned char)*rhs);
		if (left != right)
		{
			return 0;
		}
		++lhs;
		++rhs;
	}

	return *lhs == '\0' && *rhs == '\0';
}

/*
=============
BotChat_InitialTypeContexts

Maps retail initial-chat type names onto the reconstructed event contexts.
=============
*/
static size_t BotChat_InitialTypeContexts(const char *type_name,
	unsigned long *contexts,
	size_t context_capacity)
{
	size_t count = 0;

#define BOT_CHAT_APPEND_CONTEXT(value) \
	do \
	{ \
		if (count < context_capacity) \
		{ \
			contexts[count] = (value); \
		} \
		++count; \
	} while (0)

	if (BotChat_StringEqualsIgnoreCase(type_name, "enter_game")
		|| BotChat_StringEqualsIgnoreCase(type_name, "game_enter"))
	{
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_ENTERGAME);
	}
	else if (BotChat_StringEqualsIgnoreCase(type_name, "death_bfg"))
	{
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_DEATH);
	}
	else if (BotChat_StringEqualsIgnoreCase(type_name, "death_insult"))
	{
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_DEATH);
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_INSULT);
	}
	else if (BotChat_StringEqualsIgnoreCase(type_name, "death_praise"))
	{
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_DEATH);
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_PRAISE);
	}
	else if (BotChat_StringEqualsIgnoreCase(type_name, "kill_insult"))
	{
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_KILL);
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_INSULT);
	}
	else if (BotChat_StringEqualsIgnoreCase(type_name, "kill_praise"))
	{
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_KILL);
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_PRAISE);
	}
	else if (BotChat_StringEqualsIgnoreCase(type_name, "kill_telefrag"))
	{
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_KILL);
	}
	else if (BotChat_StringEqualsIgnoreCase(type_name, "enemy_suicide"))
	{
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_ENEMYSUICIDE);
	}
	else if (BotChat_StringEqualsIgnoreCase(type_name, "hit_talking"))
	{
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_HITTALKING);
	}
	else if (BotChat_StringEqualsIgnoreCase(type_name, "hit_nodeath"))
	{
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_HITNODEATH);
	}
	else if (BotChat_StringEqualsIgnoreCase(type_name, "hit_nokill"))
	{
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_HITNOKILL);
	}
	else if (BotChat_StringEqualsIgnoreCase(type_name, "random_insult"))
	{
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_RANDOM);
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_INSULT);
	}
	else if (BotChat_StringEqualsIgnoreCase(type_name, "random_misc"))
	{
		BOT_CHAT_APPEND_CONTEXT(BOT_CHAT_CONTEXT_RANDOM);
	}

#undef BOT_CHAT_APPEND_CONTEXT

	return count <= context_capacity ? count : context_capacity;
}

/*
=============
BotChat_AddInitialTemplate

Stores one retail initial-chat message under every matching event context.
=============
*/
static int BotChat_AddInitialTemplate(bot_chatstate_t *state,
	const char *type_name,
	const char *template_text)
{
	unsigned long contexts[2] = {0, 0};
	const size_t context_count = BotChat_InitialTypeContexts(type_name,
		contexts,
		sizeof(contexts) / sizeof(contexts[0]));

	for (size_t i = 0; i < context_count; ++i)
	{
		if (!BotChat_AddTemplateForContext(state, contexts[i], template_text))
		{
			return 0;
		}
	}

	return 1;
}

static size_t BotChat_SelectIndex(const char *seed, size_t count)
{
	(void)seed;
	if (count == 0U)
	{
		return 0U;
	}
	return (size_t)(BotChat_RetailRandomFloat() * (float)count);
}

/*
=============
BotChat_SelectRecentIndex

Selects a chat-message slot while avoiding recently used entries when possible.
=============
*/
static size_t BotChat_SelectRecentIndex(const char *seed,
	const float *times,
	size_t count,
	double now_seconds,
	int fallback_oldest,
	int *selected_available)
{
	if (selected_available != NULL)
	{
		*selected_available = 0;
	}
	if (count == 0)
	{
		return 0;
	}
	if (times == NULL)
	{
		if (selected_available != NULL)
		{
			*selected_available = 1;
		}
		return BotChat_SelectIndex(seed, count);
	}

	size_t available_count = 0;
	for (size_t i = 0; i < count; ++i)
	{
		if (times[i] <= now_seconds)
		{
			++available_count;
		}
	}
	if (available_count > 0)
	{
		size_t selected = BotChat_SelectIndex(seed, available_count);
		for (size_t i = 0; i < count; ++i)
		{
			if (times[i] > now_seconds)
			{
				continue;
			}
			if (selected-- == 0)
			{
				if (selected_available != NULL)
				{
					*selected_available = 1;
				}
				return i;
			}
		}
	}

	if (!fallback_oldest)
	{
		return 0;
	}

	size_t oldest = 0;
	for (size_t i = 1; i < count; ++i)
	{
		if (times[i] < times[oldest])
		{
			oldest = i;
		}
	}
	return oldest;
}

/*
=============
BotChat_MarkRecent

Marks one chat-message slot as recently used.
=============
*/
static void BotChat_MarkRecent(float *times,
	size_t index,
	size_t count,
	double now_seconds)
{
	if (times == NULL || index >= count)
	{
		return;
	}

	times[index] = now_seconds + BOT_CHAT_MESSAGE_RECENT_SECONDS;
}

/*
=============
BotChat_SynonymContextNameFromValue

Maps preprocessor-expanded syn.h context constants back to retail names.
=============
*/
static const char *BotChat_SynonymContextNameFromValue(unsigned long context)
{
	switch (context)
	{
		case 0xFFFFFFFFUL:
			return "CONTEXT_ALL";
		case 1UL:
			return "CONTEXT_NORMAL";
		case 2UL:
			return "CONTEXT_NEARBYITEM";
		case 4UL:
			return "CONTEXT_CTFREDTEAM";
		case 8UL:
			return "CONTEXT_CTFBLUETEAM";
		case 16UL:
			return "CONTEXT_REPLY";
		default:
			return NULL;
	}
}

/*
=============
BotChat_SynonymContextNameFromToken

Canonicalizes a synonym context token from either source or expanded form.
=============
*/
static int BotChat_SynonymContextNameFromToken(const pc_token_t *token,
	char *out_name,
	size_t out_size)
{
	if (token == NULL || out_name == NULL || out_size == 0)
	{
		return 0;
	}

	if (token->type == TT_NAME && strncmp(token->string, "CONTEXT_", 8) == 0)
	{
		const int written = snprintf(out_name, out_size, "%s", token->string);
		return written >= 0 && (size_t)written < out_size;
	}

	if (token->type == TT_NUMBER)
	{
		const unsigned long context = BotChat_NumberTokenValue(token);
		const char *name = BotChat_SynonymContextNameFromValue(context);
		int written;
		if (name != NULL)
		{
			written = snprintf(out_name, out_size, "%s", name);
		}
		else
		{
			written = snprintf(out_name, out_size, "CONTEXT_%lu", context);
		}
		return written >= 0 && (size_t)written < out_size;
	}

	return 0;
}

/*
=============
BotChat_SkipBalancedBlock

Advances the script until the provided closing punctuation balances the opening
character. Returns 1 on success and 0 when EOF is reached first.
=============
*/
static int BotChat_SkipBalancedBlock(pc_script_t *script, char open, char close)
{
	pc_token_t token;
	int depth = 1;
	while (PS_ReadToken(script, &token))
	{
		if (token.type != TT_PUNCTUATION || token.string[0] == '\0')
		{
			continue;
		}
		if (token.string[0] == open)
		{
			depth++;
			continue;
		}
		if (token.string[0] == close)
		{
			if (--depth == 0)
			{
				return 1;
			}
		}
	}
	return 0;
}

/*
=============
BotChat_ParseSynonymGroup

Parses a single synonym group within a CONTEXT_* block.
=============
*/
static int BotChat_ParseSynonymGroup(bot_synonym_context_t *context, pc_script_t *script)
{
	bot_synonym_group_t *group = BotChat_AddSynonymGroup(context);
	if (group == NULL)
	{
		return 0;
	}

	while (1)
	{
		if (!PS_ExpectTokenString(script, "("))
		{
			return 0;
		}
		pc_token_t token;
		if (!PS_ExpectTokenType(script, TT_STRING, 0, &token))
		{
			return 0;
		}
		char *phrase_text = BotChat_TokenTextDuplicate(&token);
		if (phrase_text == NULL || phrase_text[0] == '\0')
		{
			free(phrase_text);
			return 0;
		}
		if (!PS_ExpectTokenString(script, ","))
		{
			free(phrase_text);
			return 0;
		}
		if (!PS_ExpectTokenType(script, TT_NUMBER, 0, &token))
		{
			free(phrase_text);
			return 0;
		}
		float weight = token.floatvalue;
		if (!PS_ExpectTokenString(script, ")"))
		{
			free(phrase_text);
			return 0;
		}

		bot_synonym_phrase_t *phrase = BotChat_AddSynonymPhrase(group);
		if (phrase == NULL)
		{
			free(phrase_text);
			return 0;
		}
		phrase->text = phrase_text;
		phrase->weight = weight;

		if (PS_CheckTokenString(script, "]"))
		{
			if (group->phrase_count < 2U)
			{
				BotLib_Print(PRT_ERROR,
					"synonym must have at least to entries\n");
				return 0;
			}
			return 1;
		}
		if (!PS_ExpectTokenString(script, ","))
		{
			return 0;
		}
	}
}

/*
=============
BotChat_ParseSynonymContextsFromScript

Walks Gladiator's nested integer-context synonym grammar.
=============
*/
static int BotChat_ParseSynonymContextsFromScript(bot_chatstate_t *state,
	pc_script_t *script,
	int allow_numeric_contexts)
{
	if (state == NULL || script == NULL)
	{
		return 0;
	}

	(void)allow_numeric_contexts;
	ResetScript(script);
	unsigned long context_mask = 0UL;
	unsigned long context_stack[32];
	size_t context_level = 0U;
	pc_token_t token;
	while (PS_ReadToken(script, &token))
	{
		if (token.type == TT_NUMBER && BotChat_NumberTokenIsInteger(&token))
		{
			if (context_level >= 31U)
			{
				return 0;
			}
			const unsigned long value = BotChat_NumberTokenValue(&token);
			context_stack[context_level++] = value;
			context_mask |= value;
			if (!PS_ExpectTokenString(script, "{"))
			{
				return 0;
			}
			continue;
		}

		if (token.type != TT_PUNCTUATION)
		{
			return 0;
		}
		if (strcmp(token.string, "}") == 0)
		{
			if (context_level == 0U)
			{
				return 0;
			}
			context_mask &= ~context_stack[--context_level];
			continue;
		}
		if (strcmp(token.string, "[") != 0)
		{
			return 0;
		}

		char context_name[BOT_CHAT_MAX_TOKEN_CHARS];
		snprintf(context_name, sizeof(context_name), "%lu", context_mask);
		bot_synonym_context_t *context =
			BotChat_AddSynonymContext(state, context_name);
		if (context == NULL)
		{
			return 0;
		}
		context->context_mask = context_mask;
		if (!BotChat_ParseSynonymGroup(context, script))
		{
			return 0;
		}
	}
	return context_level == 0U;
}

/*
=============
BotChat_ParseMatchTemplate

Extracts the template left-hand side and registers it under the message type.
=============
*/
static int BotChat_ParseMatchTemplate(bot_chatstate_t *state,
	pc_script_t *script,
	unsigned long match_context)
{
	bot_string_builder_t builder = {0};
	pc_token_t token;
	int last_was_variable = 0;
	int need_separator = 0;
	while (PS_ReadToken(script, &token))
	{
		if (need_separator)
		{
			if (token.type == TT_PUNCTUATION && token.string[0] == '=')
			{
				break;
			}
			if (token.type == TT_PUNCTUATION && token.string[0] == ',')
			{
				need_separator = 0;
				continue;
			}
			BotChat_StringBuilderDestroy(&builder);
			return 0;
		}

		if (token.type == TT_PUNCTUATION)
		{
			BotChat_StringBuilderDestroy(&builder);
			return 0;
		}
		if (token.type == TT_STRING)
		{
			int contains_empty = 0;
			if (!BotChat_StringBuilderAppendStringAlternativePiece(&builder,
					script,
					&token,
					&contains_empty))
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			if (!contains_empty)
			{
				last_was_variable = 0;
			}
			need_separator = 1;
			continue;
		}
		if (token.type == TT_NAME)
		{
			BotChat_StringBuilderDestroy(&builder);
			return 0;
		}
		if (token.type == TT_NUMBER)
		{
			unsigned long variable = 0UL;
			if (!BotChat_NumberTokenMatchVariableValue(&token, &variable)
				|| last_was_variable)
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			if (!BotChat_StringBuilderAppendVariableReference(&builder,
					variable))
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			last_was_variable = 1;
			need_separator = 1;
			continue;
		}

		BotChat_StringBuilderDestroy(&builder);
		return 0;
	}
	if (token.type != TT_PUNCTUATION || token.string[0] != '=')
	{
		BotChat_StringBuilderDestroy(&builder);
		return 0;
	}
	if (!PS_ExpectTokenString(script, "("))
	{
		BotChat_StringBuilderDestroy(&builder);
		return 0;
	}
	pc_token_t type_token;
	if (!PS_ExpectTokenType(script, TT_NUMBER, TT_INTEGER, &type_token)
		|| !BotChat_NumberTokenIsInteger(&type_token))
	{
		BotChat_StringBuilderDestroy(&builder);
		return 0;
	}
	unsigned long message_type = BotChat_NumberTokenValue(&type_token);
	if (!PS_ExpectTokenString(script, ","))
	{
		BotChat_StringBuilderDestroy(&builder);
		return 0;
	}
	pc_token_t subtype_token;
	if (!PS_ExpectTokenType(script, TT_NUMBER, TT_INTEGER, &subtype_token)
		|| !BotChat_NumberTokenIsInteger(&subtype_token))
	{
		BotChat_StringBuilderDestroy(&builder);
		return 0;
	}
	const unsigned long subtype = BotChat_NumberTokenValue(&subtype_token);
	if (!PS_ExpectTokenString(script, ")")
		|| !PS_ExpectTokenString(script, ";"))
	{
		BotChat_StringBuilderDestroy(&builder);
		return 0;
	}
	if (builder.buffer == NULL || builder.length == 0)
	{
		BotChat_StringBuilderDestroy(&builder);
		return 1;
	}
	char *template_text = BotChat_StringBuilderDetach(&builder);
	BotChat_StringBuilderDestroy(&builder);
	if (template_text == NULL)
	{
		return 0;
	}
	char *mapped_template = template_text;
	bot_match_context_t *context = BotChat_FindMatchContext(state, message_type);
	if (context == NULL)
	{
		context = BotChat_AddMatchContext(state, message_type);
		if (context == NULL)
		{
			free(mapped_template);
			return 0;
		}
	}
	char **slot = BotChat_AddTemplate(context, match_context, subtype);
	if (slot == NULL)
	{
		free(mapped_template);
		return 0;
	}
	*slot = mapped_template;
	return BotChat_AddMatchOrderEntry(state,
		mapped_template,
		match_context,
		message_type,
		subtype);
}

/*
=============
BotChat_ParseMatchBlock

Iterates over the statements inside an MTCONTEXT_* block.
=============
*/
static int BotChat_ParseMatchBlock(bot_chatstate_t *state,
	pc_script_t *script,
	unsigned long match_context)
{
	pc_token_t token;
	while (PS_ReadToken(script, &token))
	{
		if (token.type == TT_PUNCTUATION && token.string[0] == '}')
		{
			return 1;
		}
		PS_UnreadToken(script, &token);
		if (!BotChat_ParseMatchTemplate(state, script, match_context))
		{
			return 0;
		}
	}
	return 0;
}

/*
=============
BotChat_IsKnownMatchContextNumber

Recognizes preprocessor-expanded MTCONTEXT_* block labels.
=============
*/
static int BotChat_IsKnownMatchContextNumber(unsigned long value)
{
	switch (value)
	{
		case 1:
		case 2:
		case 4:
		case 8:
		case 16:
		case 32:
		case 64:
			return 1;
		default:
			break;
	}

	return 0;
}

/*
=============
BotChat_StringBuilderAppendChatMessageComponent

Appends one strict retail BotLoadChatMessage component to a template builder.
=============
*/
static int BotChat_StringBuilderAppendChatMessageComponent(
	bot_string_builder_t *builder,
	const pc_token_t *token)
{
	if (builder == NULL || token == NULL)
	{
		return 0;
	}

	if (token->type == TT_STRING)
	{
		return BotChat_StringBuilderAppendTokenText(builder, token);
	}

	if (token->type == TT_NAME)
	{
		return BotChat_StringBuilderAppendRandomReference(builder, token->string);
	}

	if (token->type == TT_NUMBER)
	{
		return BotChat_NumberTokenIsInteger(token)
			&& BotChat_StringBuilderAppendVariableReference(builder,
				BotChat_NumberTokenValue(token));
	}

	return 0;
}

/*
=============
BotChat_ParseReplyTemplate

Builds a single reply text entry from the token stream.
=============
*/
static int BotChat_ParseReplyTemplate(bot_chatstate_t *state, bot_reply_rule_t *rule, pc_script_t *script)
{
	bot_string_builder_t builder = {0};
	pc_token_t token;
	while (1)
	{
		if (!PS_ReadToken(script, &token)
			|| !BotChat_StringBuilderAppendChatMessageComponent(&builder,
				&token))
		{
			BotChat_StringBuilderDestroy(&builder);
			return 0;
		}

		if (BotChat_CheckTokenString(script, ";"))
		{
			break;
		}
		if (!PS_ExpectTokenString(script, ","))
		{
			BotChat_StringBuilderDestroy(&builder);
			return 0;
		}
	}

	char *reply_text = BotChat_StringBuilderDetach(&builder);
	BotChat_StringBuilderDestroy(&builder);
	if (reply_text == NULL)
	{
		return 0;
	}
	char **slot = BotChat_AddReply(rule);
	if (slot == NULL)
	{
		free(reply_text);
		return 0;
	}
	*slot = reply_text;
	state->has_reply_chats = 1;
	return 1;
}

/*
=============
BotChat_ParseReplyKeyPattern

Parses a parenthesized reply key into the internal capture-pattern format.
=============
*/
static char *BotChat_ParseReplyKeyPattern(pc_script_t *script)
{
	bot_string_builder_t builder = {0};
	pc_token_t token;
	int last_was_variable = 0;
	int need_separator = 0;
	int parsed_piece = 0;

	while (PS_ReadToken(script, &token))
	{
		if (need_separator)
		{
			if (token.type == TT_PUNCTUATION && token.string[0] == ')')
			{
				return BotChat_StringBuilderDetach(&builder);
			}
			if (token.type == TT_PUNCTUATION && token.string[0] == ',')
			{
				need_separator = 0;
				continue;
			}
			BotChat_StringBuilderDestroy(&builder);
			return NULL;
		}

		if (token.type == TT_PUNCTUATION)
		{
			BotChat_StringBuilderDestroy(&builder);
			return NULL;
		}
		if (token.type == TT_STRING)
		{
			int contains_empty = 0;
			if ((parsed_piece
					&& !BotChat_StringBuilderAppendChar(&builder,
						BOT_CHAT_MATCH_PIECE_SEPARATOR))
				|| !BotChat_StringBuilderAppendStringAlternativePiece(&builder,
					script,
					&token,
					&contains_empty))
			{
				BotChat_StringBuilderDestroy(&builder);
				return NULL;
			}
			if (!contains_empty)
			{
				last_was_variable = 0;
			}
			parsed_piece = 1;
			need_separator = 1;
			continue;
		}
		if (token.type == TT_NUMBER)
		{
			unsigned long variable = 0UL;
			if (!BotChat_NumberTokenMatchVariableValue(&token, &variable)
				|| last_was_variable
				|| (parsed_piece
					&& !BotChat_StringBuilderAppendChar(&builder,
						BOT_CHAT_MATCH_PIECE_SEPARATOR))
				|| !BotChat_StringBuilderAppendVariableReference(&builder,
					variable))
			{
				BotChat_StringBuilderDestroy(&builder);
				return NULL;
			}
			last_was_variable = 1;
			parsed_piece = 1;
			need_separator = 1;
			continue;
		}
		if (token.type == TT_NAME)
		{
			BotChat_StringBuilderDestroy(&builder);
			return NULL;
		}

		BotChat_StringBuilderDestroy(&builder);
		return NULL;
	}

	BotChat_StringBuilderDestroy(&builder);
	return NULL;
}

/*
=============
BotChat_ParseReplyKeyText

Reads one reply-key expression after optional ! or & modifiers.
=============
*/
static char *BotChat_ParseReplyKeyText(pc_script_t *script,
	int *is_pattern,
	int *special)
{
	pc_token_t token;
	if (!PS_ReadToken(script, &token))
	{
		return NULL;
	}

	if (token.type == TT_PUNCTUATION && token.string[0] == '(')
	{
		if (is_pattern != NULL)
		{
			*is_pattern = 1;
		}
		return BotChat_ParseReplyKeyPattern(script);
	}
	if (token.type == TT_PUNCTUATION && token.string[0] == '<')
	{
		return NULL;
	}

	bot_string_builder_t builder = {0};
	if (token.type == TT_NAME)
	{
		if (strcmp(token.string, "name") == 0)
		{
			if (is_pattern != NULL)
			{
				*is_pattern = 0;
			}
			if (special != NULL)
			{
				*special = BOT_CHAT_REPLY_KEY_NAME;
			}
			return BotChat_StringDuplicate(token.string);
		}
		if (strcmp(token.string, "female") == 0)
		{
			if (is_pattern != NULL)
			{
				*is_pattern = 0;
			}
			if (special != NULL)
			{
				*special = BOT_CHAT_REPLY_KEY_GENDER_FEMALE;
			}
			return BotChat_StringDuplicate(token.string);
		}
		if (strcmp(token.string, "male") == 0)
		{
			if (is_pattern != NULL)
			{
				*is_pattern = 0;
			}
			if (special != NULL)
			{
				*special = BOT_CHAT_REPLY_KEY_GENDER_MALE;
			}
			return BotChat_StringDuplicate(token.string);
		}
		if (strcmp(token.string, "it") == 0)
		{
			if (is_pattern != NULL)
			{
				*is_pattern = 0;
			}
			if (special != NULL)
			{
				*special = BOT_CHAT_REPLY_KEY_GENDERLESS;
			}
			return BotChat_StringDuplicate(token.string);
		}
	}

	if (token.type == TT_STRING)
	{
		if (!BotChat_StringBuilderAppendTokenText(&builder, &token))
		{
			BotChat_StringBuilderDestroy(&builder);
			return NULL;
		}
		if (is_pattern != NULL)
		{
			*is_pattern = 0;
		}
		return BotChat_StringBuilderDetach(&builder);
	}

	BotChat_StringBuilderDestroy(&builder);
	return NULL;
}

/*
=============
BotChat_ParseReplyKeys

Parses the key list inside a reply chat's square brackets. Q3 accepts optional
commas between keys but still requires at least one key before the closing
bracket.
=============
*/
static int BotChat_ParseReplyKeys(pc_script_t *script,
	bot_reply_key_list_t *keys)
{
	if (script == NULL || keys == NULL)
	{
		return 0;
	}

	int parsed_key = 0;
	while (1)
	{
		if (BotChat_CheckTokenString(script, "]"))
		{
			return parsed_key;
		}

		int negated = 0;
		int required = 0;
		if (BotChat_CheckTokenString(script, "!"))
		{
			negated = 1;
		}
		else if (BotChat_CheckTokenString(script, "&"))
		{
			required = 1;
		}

		int is_pattern = 0;
		int special = BOT_CHAT_REPLY_KEY_TEXT;
		char *pattern = BotChat_ParseReplyKeyText(script, &is_pattern, &special);
		if (pattern == NULL)
		{
			return 0;
		}

		bot_reply_key_t *key = BotChat_AddReplyKeyToList(keys);
		if (key == NULL)
		{
			free(pattern);
			return 0;
		}
		key->pattern = pattern;
		key->is_pattern = is_pattern;
		key->special = special;
		key->required = required;
		key->negated = negated;
		parsed_key = 1;

		(void)BotChat_CheckTokenString(script, ",");
	}
}

/*
=============
BotChat_ParseReplyBlock

Registers a reply context and its associated templates.
=============
*/
static int BotChat_ParseReplyBlock(bot_chatstate_t *state, pc_script_t *script)
{
	bot_reply_key_list_t keys = {0};
	if (!BotChat_ParseReplyKeys(script, &keys))
	{
		BotChat_FreeReplyKeyList(&keys);
		return 0;
	}
	if (!PS_ExpectTokenString(script, "="))
	{
		BotChat_FreeReplyKeyList(&keys);
		return 0;
	}
	pc_token_t token;
	if (!PS_ExpectTokenType(script, TT_NUMBER, 0, &token))
	{
		BotChat_FreeReplyKeyList(&keys);
		return 0;
	}
	unsigned long reply_context = BotChat_NumberTokenValue(&token);
	float priority = token.floatvalue;
	if (!PS_ExpectTokenString(script, "{"))
	{
		BotChat_FreeReplyKeyList(&keys);
		return 0;
	}
	bot_reply_rule_t *rule = BotChat_AddReplyRule(state, reply_context);
	if (rule == NULL)
	{
		BotChat_FreeReplyKeyList(&keys);
		return 0;
	}
	rule->priority = priority;
	if (!BotChat_MoveReplyKeysToRule(rule, &keys))
	{
		BotChat_FreeReplyKeyList(&keys);
		return 0;
	}
	while (1)
	{
		if (PS_CheckTokenString(script, "}"))
		{
			break;
		}
		if (!BotChat_ParseReplyTemplate(state, rule, script))
		{
			return 0;
		}
	}
	return 1;
}

/*
=============
BotChat_ParseSynonymContexts

Walks the active script once to collect CONTEXT_* blocks.
=============
*/
static int BotChat_ParseSynonymContexts(bot_chatstate_t *state)
{
	return BotChat_ParseSynonymContextsFromScript(state,
		state != NULL ? state->active_script : NULL,
		0);
}

/*
=============
BotChat_ParseRandomStringBlock

Reads a named random string table body from a rnd.c-style block.
=============
*/
static int BotChat_ParseRandomStringBlock(bot_chatstate_t *state,
	bot_random_string_table_t *table,
	pc_script_t *script)
{
	(void)state;
	while (1)
	{
		if (PS_CheckTokenString(script, "}"))
		{
			return 1;
		}

		pc_token_t token;
		if (!PS_ExpectTokenType(script, TT_STRING, 0, &token))
		{
			return 0;
		}
		char *entry_text = BotChat_TokenTextDuplicate(&token);
		if (entry_text == NULL)
		{
			return 0;
		}
		const int added = BotChat_AddRandomEntry(table, entry_text);
		free(entry_text);
		if (!added)
		{
			return 0;
		}

		if (PS_CheckTokenString(script, "}"))
		{
			return 1;
		}
		if (!PS_ExpectTokenString(script, ","))
		{
			return 0;
		}
	}
}

/*
=============
BotChat_ParseRandomStringTables

Collects top-level name = { ... } random string tables from a script pass.
=============
*/
static int BotChat_ParseRandomStringTables(bot_chatstate_t *state, pc_script_t *script)
{
	if (state == NULL || script == NULL)
	{
		return 0;
	}

	ResetScript(script);
	pc_token_t token;
	while (PS_ReadToken(script, &token))
	{
		if (token.type != TT_NAME)
		{
			return 0;
		}
		if (!PS_ExpectTokenString(script, "=")
			|| !PS_ExpectTokenString(script, "{"))
		{
			return 0;
		}

		bot_random_string_table_t *table = BotChat_AddRandomTable(state, token.string);
		if (table == NULL)
		{
			return 0;
		}
		if (!BotChat_ParseRandomStringBlock(state, table, script))
		{
			return 0;
		}
	}

	return 1;
}

/*
=============
BotChat_BuildSiblingAssetPath

Builds a path for a chat asset that lives beside the requested chat file.
=============
*/
static int BotChat_BuildSiblingAssetPath(const char *source_path,
	const char *leaf_name,
	char *out_path,
	size_t out_size)
{
	if (source_path == NULL || leaf_name == NULL || out_path == NULL || out_size == 0)
	{
		return 0;
	}

	const char *slash = strrchr(source_path, '/');
	const char *backslash = strrchr(source_path, '\\');
	const char *separator = slash;
	if (separator == NULL || (backslash != NULL && backslash > separator))
	{
		separator = backslash;
	}

	int written;
	if (separator == NULL)
	{
		written = snprintf(out_path, out_size, "%s", leaf_name);
		return written >= 0 && (size_t)written < out_size;
	}

	const size_t prefix_length = (size_t)(separator - source_path) + 1U;
	if (prefix_length >= out_size)
	{
		return 0;
	}
	memcpy(out_path, source_path, prefix_length);
	out_path[prefix_length] = '\0';
	written = snprintf(out_path + prefix_length,
		out_size - prefix_length,
		"%s",
		leaf_name);
	return written >= 0 && (size_t)written < out_size - prefix_length;
}

/*
=============
BotChat_LoadSiblingRandomStrings

Loads rnd.c beside the active chat file when that retail asset is available.
=============
*/
static int BotChat_LoadSiblingRandomStrings(bot_chatstate_t *state, const char *chatfile)
{
	char path[BOT_CHAT_MAX_PATH_CHARS];
	if (!BotChat_BuildSiblingAssetPath(chatfile, "rnd.c", path, sizeof(path)))
	{
		return 0;
	}

	FILE *probe = fopen(path, "rb");
	if (probe == NULL)
	{
		return 1;
	}
	fclose(probe);

	pc_source_t *source = PC_LoadSourceFile(path);
	if (source == NULL)
	{
		return 0;
	}

	pc_script_t *script = PS_CreateScriptFromSource(source);
	if (script == NULL)
	{
		PC_FreeSource(source);
		return 0;
	}

	const int parsed = BotChat_ParseRandomStringTables(state, script);
	PS_FreeScript(script);
	PC_FreeSource(source);
	return parsed;
}

/*
=============
BotChat_LoadSiblingSynonyms

Loads syn.c beside the active chat file when the retail synonym asset exists.
=============
*/
static int BotChat_LoadSiblingSynonyms(bot_chatstate_t *state, const char *chatfile)
{
	char path[BOT_CHAT_MAX_PATH_CHARS];
	if (!BotChat_BuildSiblingAssetPath(chatfile, "syn.c", path, sizeof(path)))
	{
		return 0;
	}

	FILE *probe = fopen(path, "rb");
	if (probe == NULL)
	{
		return 1;
	}
	fclose(probe);

	pc_source_t *source = PC_LoadSourceFile(path);
	if (source == NULL)
	{
		return 0;
	}

	pc_script_t *script = PS_CreateScriptFromSource(source);
	if (script == NULL)
	{
		PC_FreeSource(source);
		return 0;
	}

	const int parsed = BotChat_ParseSynonymContextsFromScript(state, script, 1);
	PS_FreeScript(script);
	PC_FreeSource(source);
	return parsed;
}

/*
=============
BotChat_SourceLeafEquals

Checks the leaf filename of a source path without allocating.
=============
*/
static int BotChat_SourceLeafEquals(const char *source_path, const char *leaf_name)
{
	if (source_path == NULL || leaf_name == NULL)
	{
		return 0;
	}

	const char *slash = strrchr(source_path, '/');
	const char *backslash = strrchr(source_path, '\\');
	const char *separator = slash;
	if (separator == NULL || (backslash != NULL && backslash > separator))
	{
		separator = backslash;
	}

	const char *source_leaf = (separator != NULL) ? separator + 1 : source_path;
	return BotChat_StringEqualsIgnoreCase(source_leaf, leaf_name);
}

/*
=============
BotChat_ParseTextTemplate

Builds one semicolon-terminated chat message from the token stream.
=============
*/
static char *BotChat_ParseTextTemplate(pc_script_t *script)
{
	bot_string_builder_t builder = {0};
	pc_token_t token;

	while (1)
	{
		if (!PS_ReadToken(script, &token)
			|| !BotChat_StringBuilderAppendChatMessageComponent(&builder,
				&token))
		{
			BotChat_StringBuilderDestroy(&builder);
			return NULL;
		}

		if (BotChat_CheckTokenString(script, ";"))
		{
			return BotChat_StringBuilderDetach(&builder);
		}
		if (!PS_ExpectTokenString(script, ","))
		{
			BotChat_StringBuilderDestroy(&builder);
			return NULL;
		}
	}
}

/*
=============
BotChat_ParseInitialTypeBlock

Parses the messages inside one retail initial-chat type block.
=============
*/
static int BotChat_ParseInitialTypeBlock(bot_chatstate_t *state,
	pc_script_t *script,
	const char *type_name)
{
	while (1)
	{
		if (PS_CheckTokenString(script, "}"))
		{
			return 1;
		}

		char *template_text = BotChat_ParseTextTemplate(script);
		if (template_text == NULL)
		{
			return 0;
		}

		const int added = BotChat_AddInitialTypeTemplate(state,
			type_name,
			template_text);
		free(template_text);
		if (!added)
		{
			return 0;
		}
	}
}

/*
=============
BotChat_ParseSelectedChatBlock

Parses the selected chat "name" block and ignores unknown type names.
=============
*/
static int BotChat_ParseSelectedChatBlock(bot_chatstate_t *state, pc_script_t *script)
{
	pc_token_t token;

	while (PS_ReadToken(script, &token))
	{
		if (token.type == TT_PUNCTUATION && token.string[0] == '}')
		{
			return 1;
		}
		if (token.type != TT_NAME || strcmp(token.string, "type") != 0)
		{
			return 0;
		}

		pc_token_t type_token;
		if (!PS_ExpectTokenType(script, TT_STRING, 0, &type_token)
			|| !PS_ExpectTokenString(script, "{"))
		{
			return 0;
		}
		char *type_name = BotChat_TokenTextDuplicate(&type_token);
		if (type_name == NULL)
		{
			return 0;
		}
		const int parsed = BotChat_AddInitialType(state, type_name) != NULL
			&& BotChat_ParseInitialTypeBlock(state, script, type_name);
		free(type_name);
		if (!parsed)
		{
			return 0;
		}
	}

	return 0;
}

/*
=============
BotChat_ParseInitialChatBlocks

Finds and parses strict retail chat "name" blocks.
=============
*/
static int BotChat_ParseInitialChatBlocks(bot_chatstate_t *state,
	const char *chatname,
	int *saw_chat_block,
	int *found_chat_block)
{
	if (state == NULL || state->active_script == NULL)
	{
		return 0;
	}

	if (saw_chat_block != NULL)
	{
		*saw_chat_block = 0;
	}
	if (found_chat_block != NULL)
	{
		*found_chat_block = 0;
	}

	ResetScript(state->active_script);
	pc_token_t token;
	while (PS_ReadToken(state->active_script, &token))
	{
		if (token.type != TT_NAME || strcmp(token.string, "chat") != 0)
		{
			return 0;
		}

		if (saw_chat_block != NULL)
		{
			*saw_chat_block = 1;
		}

		pc_token_t name_token;
		if (!PS_ExpectTokenType(state->active_script, TT_STRING, 0, &name_token)
			|| !PS_ExpectTokenString(state->active_script, "{"))
		{
			return 0;
		}

		char *block_name = BotChat_TokenTextDuplicate(&name_token);
		if (block_name == NULL)
		{
			return 0;
		}

		if (strcmp(block_name, chatname) != 0)
		{
			free(block_name);
			if (!BotChat_SkipBalancedBlock(state->active_script, '{', '}'))
			{
				return 0;
			}
			continue;
		}
		free(block_name);

		if (found_chat_block != NULL)
		{
			*found_chat_block = 1;
		}
		if (!BotChat_ParseSelectedChatBlock(state, state->active_script))
		{
			return 0;
		}
		/* Retail continues scanning so duplicate named blocks accumulate. */
	}

	return 1;
}

/*
=============
BotChat_ParseMatchScript

Parses match contexts while skipping reply definitions.
=============
*/
static int BotChat_ParseMatchScript(bot_chatstate_t *state, pc_script_t *script)
{
	if (state == NULL || script == NULL)
	{
		return 0;
	}
	ResetScript(script);
	pc_token_t token;
	while (PS_ReadToken(script, &token))
	{
		if (token.type != TT_NUMBER || !BotChat_NumberTokenIsInteger(&token))
		{
			return 0;
		}
		const unsigned long match_context = BotChat_NumberTokenValue(&token);
		if (!PS_ExpectTokenString(script, "{")
			|| !BotChat_ParseMatchBlock(state, script, match_context))
		{
			return 0;
		}
	}
	return 1;
}

/*
=============
BotChat_ParseReplyPass

Parses reply contexts from the supplied script while skipping match
definitions.
=============
*/
static int BotChat_ParseReplyScript(bot_chatstate_t *state, pc_script_t *script)
{
	if (state == NULL || script == NULL)
	{
		return 0;
	}
	ResetScript(script);
	pc_token_t token;
	while (PS_ReadToken(script, &token))
	{
		if (token.type != TT_PUNCTUATION || token.string[0] != '[')
		{
			return 0;
		}
		if (!BotChat_ParseReplyBlock(state, script))
		{
			return 0;
		}
	}
	return 1;
}

/*
=============
BotChat_ParseReplyPass

Parses reply contexts from the active script.
=============
*/
static int BotChat_ParseReplyPass(bot_chatstate_t *state)
{
	return BotChat_ParseReplyScript(state,
		state != NULL ? state->active_script : NULL);
}

/*
=============
BotChat_ParseInitialChat

Parses named retail initial-chat blocks from the active script.
=============
*/
static int BotChat_ParseInitialChat(bot_chatstate_t *state,
	const char *chatname,
	int *saw_chat_block,
	int *found_chat_block)
{
	return BotChat_ParseInitialChatBlocks(state,
		chatname,
		saw_chat_block,
		found_chat_block);
}

/*
=============
BotChat_ParseReplyChats

Parses reply tables from the active chat script.
=============
*/
static int BotChat_ParseReplyChats(bot_chatstate_t *state)
{
	if (!BotChat_ParseReplyPass(state))
	{
		return 0;
	}
	return 1;
}

/*
=============
BotChat_ParseMatchPass

Parses match contexts while skipping reply definitions.
=============
*/
static int BotChat_ParseMatchPass(bot_chatstate_t *state)
{
	return BotChat_ParseMatchScript(state,
		state != NULL ? state->active_script : NULL);
}

/*
=============
BotChat_LoadSiblingMatchTemplates

Loads match.c beside rchat.c when reconstructing the shared retail reply setup.
=============
*/
static int BotChat_LoadSiblingMatchTemplates(bot_chatstate_t *state, const char *chatfile)
{
	if (!BotChat_SourceLeafEquals(chatfile, "rchat.c"))
	{
		return 1;
	}

	const char *slash = strrchr(chatfile != NULL ? chatfile : "", '/');
	const char *backslash = strrchr(chatfile != NULL ? chatfile : "", '\\');
	const char *leaf = slash;
	if (leaf == NULL || (backslash != NULL && backslash > leaf))
	{
		leaf = backslash;
	}
	leaf = (leaf != NULL) ? leaf + 1 : chatfile;
	if (leaf == NULL || !BotChat_StringEqualsIgnoreCase(leaf, "rchat.c"))
	{
		return 1;
	}

	char path[BOT_CHAT_MAX_PATH_CHARS];
	if (!BotChat_BuildSiblingAssetPath(chatfile, "match.c", path, sizeof(path)))
	{
		return 0;
	}

	FILE *probe = fopen(path, "rb");
	if (probe == NULL)
	{
		return 1;
	}
	fclose(probe);

	pc_source_t *source = PC_LoadSourceFile(path);
	if (source == NULL)
	{
		return 0;
	}

	pc_script_t *script = PS_CreateScriptFromSource(source);
	if (script == NULL)
	{
		PC_FreeSource(source);
		return 0;
	}

	const size_t previous_count = state->match_context_count;
	const int parsed = BotChat_ParseMatchScript(state, script);
	const int accepted = parsed || state->match_context_count > previous_count;
	PS_FreeScript(script);
	PC_FreeSource(source);
	return accepted;
}

typedef int (*bot_chat_script_parser_t)(bot_chatstate_t *state,
	pc_script_t *script);

/*
=============
BotChat_ParseSetupSynonymScript

Loads retail setup synonym files, which use numeric CONTEXT_* values after
preprocessing.
=============
*/
static int BotChat_ParseSetupSynonymScript(bot_chatstate_t *state,
	pc_script_t *script)
{
	const int parsed = BotChat_ParseSynonymContextsFromScript(state, script, 1);
	if (!parsed)
	{
		BotChat_FreeSynonymContexts(state);
	}
	return parsed;
}

/*
=============
BotChat_ParseSetupRandomScript

Loads retail setup random string files.
=============
*/
static int BotChat_ParseSetupRandomScript(bot_chatstate_t *state,
	pc_script_t *script)
{
	const int parsed = BotChat_ParseRandomStringTables(state, script);
	if (!parsed)
	{
		BotChat_FreeRandomTables(state);
	}
	return parsed;
}

/*
=============
BotChat_ParseSetupMatchScript

Loads retail setup match template files.
=============
*/
static int BotChat_ParseSetupMatchScript(bot_chatstate_t *state,
	pc_script_t *script)
{
	const int parsed = BotChat_ParseMatchScript(state, script);
	if (!parsed)
	{
		BotChat_FreeMatchContexts(state);
	}
	return parsed;
}

/*
=============
BotChat_ParseSetupReplyScript

Loads retail setup reply chat files.
=============
*/
static int BotChat_ParseSetupReplyScript(bot_chatstate_t *state,
	pc_script_t *script)
{
	if (!BotChat_ParseReplyScript(state, script))
	{
		BotChat_FreeReplies(state);
		return 0;
	}

	BotChat_CheckReplyChatIntegrity(state);
	return 1;
}

/*
=============
BotChat_LoadSetupScript

Loads one libvar-selected setup asset and applies the requested parser pass.
=============
*/
static int BotChat_LoadSetupScript(bot_chatstate_t *state,
	const char *file_name,
	const char *asset_label,
	bot_chat_script_parser_t parser)
{
	(void)asset_label;
	if (state == NULL || file_name == NULL || file_name[0] == '\0'
		|| parser == NULL)
	{
		return 0;
	}

	pc_source_t *source = PC_LoadSourceFile(file_name);
	if (source == NULL)
	{
		BotLib_Print(PRT_ERROR, "couldn't find %s\n", file_name);
		return 0;
	}

	pc_script_t *script = PS_CreateScriptFromSource(source);
	if (script == NULL)
	{
		PC_FreeSource(source);
		return 0;
	}

	const int parsed = parser(state, script);
	PS_FreeScript(script);
	PC_FreeSource(source);
	return parsed;
}

/*
=============
BotChat_ClearSetupState

Releases the shared setup cache before setup reloads or shutdown completes.
=============
*/
static void BotChat_ClearSetupState(void)
{
	if (bot_chat_setup_state != NULL)
	{
		/* Retail global ownership: these assets never belong to a bot state. */
		BotChat_FreeMatchContexts(bot_chat_setup_state);
		BotChat_FreeRandomTables(bot_chat_setup_state);
		BotChat_FreeSynonymContexts(bot_chat_setup_state);
		BotChat_FreeReplies(bot_chat_setup_state);
		BotDestroyChatState(bot_chat_setup_state);
		bot_chat_setup_state = NULL;
	}
}

/*
=============
BotChat_CloseActiveScript

Releases only the current parser source/script without clearing parsed chat
tables.
=============
*/
static void BotChat_CloseActiveScript(bot_chatstate_t *state)
{
	if (state == NULL)
	{
		return;
	}

	if (state->active_script != NULL)
	{
		PS_FreeScript(state->active_script);
		state->active_script = NULL;
	}

	if (state->active_source != NULL)
	{
		PC_FreeSource(state->active_source);
		state->active_source = NULL;
	}
}

/*
=============
BotChat_OpenActiveScript

Creates a fresh precompiler source for a chat parsing pass.
=============
*/
static int BotChat_OpenActiveScript(bot_chatstate_t *state, const char *chatfile)
{
	if (state == NULL || chatfile == NULL)
	{
		return 0;
	}

	BotChat_CloseActiveScript(state);

	pc_source_t *source = PC_LoadSourceFile(chatfile);
	if (source == NULL)
	{
		return 0;
	}

	pc_script_t *script = PS_CreateScriptFromSource(source);
	if (script == NULL)
	{
		PC_FreeSource(source);
		return -1;
	}

	state->active_source = source;
	state->active_script = script;
	return 1;
}

/*
=============
BotAllocChatState

Allocates an empty chat state and initialises metadata used by parsed chat
assets.
=============
*/
bot_chatstate_t *BotAllocChatState(void)
{
    bot_chatstate_t *state = GetClearedMemory(sizeof(*state));
    if (state == NULL) {
        BotLib_Print(PRT_FATAL, "BotAllocChatState: allocation failed\n");
        return NULL;
    }

    BotChat_ResetConsoleQueue(state);
    BotChat_ClearMetadata(state);
	BotChat_RegisterConsoleMessageState(state);
    return state;
}

/*
=============
BotAllocRetailChatState

Claims a preallocated, untracked host sidecar for retail's slab-embedded chat
core. With one entry per physical client plus the safe endpoint sentinel, a
valid setup path cannot exhaust this pool.
=============
*/
bot_chatstate_t *BotAllocRetailChatState(void)
{
	for (int index = 0; index <= MAX_CLIENTS; ++index)
	{
		if (bot_retail_chat_state_used[index])
		{
			continue;
		}

		bot_chatstate_t *state = &bot_retail_chat_states[index];
		memset(state, 0, sizeof(*state));
		bot_retail_chat_state_used[index] = true;
		BotChat_ResetConsoleQueue(state);
		BotChat_ClearMetadata(state);
		BotChat_RegisterConsoleMessageState(state);
		return state;
	}

	return NULL;
}

/*
=============
BotFreeChatState

Releases the retail initial-chat tree and queued console messages while
leaving the caller-owned state storage intact.
=============
*/
int BotFreeChatState(bot_chatstate_t *state)
{
	if (state == NULL)
	{
		/* HOST SAFETY: retail dereferences the supplied embedded state. */
		return 0;
	}

	(void)BotFreeChatFile(state);
	BotChat_ResetConsoleQueue(state);
	return 0;
}

/*
=============
BotDestroyChatState

Destroys storage allocated by the BotAllocChatState host adapter.
=============
*/
void BotDestroyChatState(bot_chatstate_t *state)
{
	if (state == NULL)
	{
		return;
	}

	BotChat_UnregisterConsoleMessageState(state);
	(void)BotFreeChatState(state);
	/* HOST ADAPTER: these diagnostic cooldowns are not retail chat state. */
	free(state->cooldowns);
	state->cooldowns = NULL;
	state->cooldown_count = 0;
	state->cooldown_capacity = 0;
	free(state->client_cooldowns);
	state->client_cooldowns = NULL;
	state->client_cooldown_count = 0;
	for (int index = 0; index <= MAX_CLIENTS; ++index)
	{
		if (state == &bot_retail_chat_states[index])
		{
			memset(state, 0, sizeof(*state));
			bot_retail_chat_state_used[index] = false;
			return;
		}
	}
	FreeMemory(state);
}

/*
=============
BotForgetRetailChatStates

Releases untracked host-only metadata and forgets retail chat sidecars without
freeing their static core storage or touching arena-owned parser allocations.
=============
*/
void BotForgetRetailChatStates(void)
{
	for (int index = 0; index <= MAX_CLIENTS; ++index)
	{
		if (!bot_retail_chat_state_used[index])
		{
			continue;
		}

		bot_chatstate_t *state = &bot_retail_chat_states[index];
		BotChat_UnregisterConsoleMessageState(state);
		BotChat_FreeInitialTypes(state);
		free(state->cooldowns);
		free(state->client_cooldowns);
		memset(state, 0, sizeof(*state));
		bot_retail_chat_state_used[index] = false;
	}
}

/*
=============
BotSetupChatAI

Loads the shared retail chat assets selected by synfile, rndfile, matchfile,
and optionally rchatfile when nochat is disabled.
=============
*/
int BotSetupChatAI(void)
{
	/* HOST SAFETY: tolerate a repeated setup call without leaking globals. */
	BotChat_ShutdownConsoleMessageHeap();
	BotChat_ClearSetupState();

	bot_chat_setup_state = BotAllocChatState();
	if (bot_chat_setup_state == NULL)
	{
		/* Retail has no recoverable allocation path and still returns success. */
		return BLERR_NOERROR;
	}

	const char *file = LibVarString("synfile", "syn.c");
	(void)BotChat_LoadSetupScript(bot_chat_setup_state,
		file,
		"synonyms",
		BotChat_ParseSetupSynonymScript);

	file = LibVarString("rndfile", "rnd.c");
	(void)BotChat_LoadSetupScript(bot_chat_setup_state,
		file,
		"random strings",
		BotChat_ParseSetupRandomScript);

	file = LibVarString("matchfile", "match.c");
	(void)BotChat_LoadSetupScript(bot_chat_setup_state,
		file,
		"match templates",
		BotChat_ParseSetupMatchScript);

	if (LibVarValue("nochat", "0") == 0.0f)
	{
		file = LibVarString("rchatfile", "rchat.c");
		(void)BotChat_LoadSetupScript(bot_chat_setup_state,
			file,
			"reply chats",
			BotChat_ParseSetupReplyScript);
	}
	(void)BotChat_InitConsoleMessageHeap();

	return BLERR_NOERROR;
}

/*
=============
BotShutdownChatAI

Frees shared setup assets loaded by BotSetupChatAI.
=============
*/
void BotShutdownChatAI(void)
{
	BotChat_ShutdownConsoleMessageHeap();
	BotChat_ClearSetupState();
}

/*
=============
BotShutdownChatStateAdapters

Drains per-state host adapters at whole-library shutdown, after ordinary
client owners and shared chat assets have already been released.
=============
*/
void BotShutdownChatStateAdapters(void)
{
	for (;;)
	{
		bot_chatstate_t *state = bot_console_message_states;
		while (state != NULL && state == bot_chat_setup_state)
		{
			state = state->console_registry_next;
		}
		if (state == NULL)
		{
			break;
		}
		BotDestroyChatState(state);
	}
}

/*
=============
BotLoadChatFile

Loads the requested chat assets and surfaces legacy diagnostics when failures
occur.
=============
*/
int BotLoadChatFile(bot_chatstate_t *state, char *chatfile, char *chatname)
{
	if (state == NULL || chatfile == NULL || chatname == NULL)
	{
		return BLERR_CANNOTLOADICHAT;
	}

	(void)BotFreeChatFile(state);
	const int open_status = BotChat_OpenActiveScript(state, chatfile);
	if (open_status <= 0)
	{
		if (open_status < 0)
		{
			BotLib_Print(PRT_ERROR,
				"BotLoadChatFile: script wrapper failed for %s\n",
				chatfile);
			BotChat_PrintLegacyDiagnostic(state,
				PRT_ERROR,
				0,
				"couldn't find chat %s in %s\n",
				chatname,
				chatfile);
		}
		goto load_failed;
	}

	int saw_chat_block = 0;
	int found_chat_block = 0;
	const int parsed = BotChat_ParseInitialChat(state,
			chatname,
			&saw_chat_block,
			&found_chat_block);
	if (!parsed || !found_chat_block)
	{
		BotChat_PrintLegacyDiagnostic(state,
			PRT_ERROR,
			0,
			"couldn't find chat %s in %s\n",
			chatname,
			chatfile);
		goto load_failed;
	}
	(void)saw_chat_block;
	BotChat_CheckInitialChatIntegrity(state);
	BotChat_CloseActiveScript(state);
	strncpy(state->active_chatfile, chatfile, sizeof(state->active_chatfile) - 1);
	state->active_chatfile[sizeof(state->active_chatfile) - 1] = '\0';
	strncpy(state->active_chatname, chatname, sizeof(state->active_chatname) - 1);
	state->active_chatname[sizeof(state->active_chatname) - 1] = '\0';

	return BLERR_NOERROR;

load_failed:
	(void)BotFreeChatFile(state);
	BotChat_PrintLegacyDiagnostic(state,
		PRT_FATAL,
		0,
		"couldn't load chat %s from %s\n",
		chatname,
		chatfile);
	return BLERR_CANNOTLOADICHAT;
}

/*
=============
BotChat_ExpandChatMessageOnce

Runs the single Gladiator retail chat-constructor expansion pass over byte-one
variable and random references.
=============
*/
static int BotChat_ExpandChatMessageOnce(bot_chatstate_t *state,
	unsigned long message_context,
	unsigned long variable_context,
	const char *template_text,
	const char *const variables[BOT_CHAT_MAX_MATCH_VARIABLES],
	const char *source_template)
{
	if (state == NULL || template_text == NULL)
	{
		return 0;
	}

	char *assembled = state->chat_message;
	size_t assembled_length = 0;
	state->chat_message_context = message_context;

	for (size_t i = 0; template_text[i] != '\0';)
	{
		if (template_text[i] != BOT_CHAT_ESCAPE_CHAR)
		{
			assembled[assembled_length++] = template_text[i++];
			if (assembled_length >= BOT_CHAT_RETAIL_MESSAGE_PAYLOAD_CHARS)
			{
				BotLib_Print(PRT_ERROR,
					"BotConstructChat: message \"%s\" too long\n",
					template_text);
				break;
			}
			continue;
		}

		++i;
		const char escape = template_text[i];
		const char *replacement = NULL;
		char replacement_buffer[BOT_CHAT_RETAIL_MESSAGE_STORAGE_CHARS];
		if (escape == 'r')
		{
			char random_name[BOT_CHAT_RETAIL_MESSAGE_STORAGE_CHARS];
			const size_t start_index = ++i;
			while (template_text[i] != '\0'
				&& template_text[i] != BOT_CHAT_ESCAPE_CHAR)
			{
				++i;
			}
			const size_t end_index = i;
			if (template_text[i] == BOT_CHAT_ESCAPE_CHAR)
			{
				++i;
			}

			size_t name_length = end_index - start_index;
			if (name_length >= sizeof(random_name))
			{
				BotLib_Print(PRT_ERROR,
					"BotConstructChat: message \"%s\" too long\n",
					template_text);
				return 0;
			}
			memcpy(random_name, template_text + start_index, name_length);
			random_name[name_length] = '\0';
			replacement = BotChat_SelectRandomString(state, random_name);
			if (replacement == NULL)
			{
				BotLib_Print(PRT_ERROR,
					"BotConstructChat: unknown random string %s\n",
					random_name);
				return 0;
			}
		}
		else if (escape == 'v')
		{
			const size_t start_index = ++i;
			while (template_text[i] != '\0'
				&& template_text[i] != BOT_CHAT_ESCAPE_CHAR)
			{
				++i;
			}
			const size_t end_index = i;
			if (template_text[i] == BOT_CHAT_ESCAPE_CHAR)
			{
				++i;
			}

			/*
			 * sub_1002e060 does not validate decimal digits here.  It sign-extends
			 * each byte and performs x86 32-bit base-ten arithmetic before testing
			 * the resulting index.  Keep that observable diagnostic for malformed
			 * but non-dangerous input, while refusing the negative values that would
			 * make the retail pointer arithmetic undefined in this reconstruction.
			 */
			uint32_t raw_value = 0U;
			for (size_t j = start_index; j < end_index; ++j)
			{
				const int32_t signed_character = (int32_t)(int8_t)template_text[j];
				raw_value = raw_value * 10U
					+ (uint32_t)(signed_character - (int32_t)'0');
			}
			const int32_t value = (int32_t)raw_value;
			if (value < 0 || value > 10)
			{
				BotLib_Print(PRT_ERROR,
					"BotConstructChat: message %s variable %d out of range\n",
					template_text,
					value);
				return 0;
			}

			if (variables != NULL
				&& (size_t)value < BOT_CHAT_MAX_MATCH_VARIABLES
				&& variables[value] != NULL)
			{
				const int written = snprintf(replacement_buffer,
					sizeof(replacement_buffer),
					"%s",
					variables[value]);
				if (written < 0 || (size_t)written >= sizeof(replacement_buffer))
				{
					BotLib_Print(PRT_ERROR,
						"BotConstructChat: message %s too long\n",
						template_text);
					return 0;
				}
				if (!BotChat_ReplaceVariableSynonyms(state,
					variable_context,
					replacement_buffer,
					sizeof(replacement_buffer)))
				{
					return 0;
				}
				replacement = replacement_buffer;
			}
			else
			{
				replacement = "";
			}
		}
		else
		{
			BotLib_Print(PRT_FATAL,
				"BotConstructChat: message \"%s\" invalid escape char\n",
				template_text);
			/* Retail leaves i on the escape type so it is copied next pass. */
			continue;
		}

		const size_t replacement_length = strlen(replacement);
		if (assembled_length + replacement_length
			>= BOT_CHAT_RETAIL_MESSAGE_PAYLOAD_CHARS)
		{
			if (escape == 'v')
			{
				BotLib_Print(PRT_ERROR,
					"BotConstructChat: message %s too long\n",
					template_text);
			}
			else
			{
				BotLib_Print(PRT_ERROR,
					"BotConstructChat: message \"%s\" too long\n",
					template_text);
			}
			return 0;
		}

		memcpy(assembled + assembled_length,
			replacement,
			replacement_length + 1U);
		assembled_length += replacement_length;
	}

	assembled[assembled_length] = '\0';
	if (!BotChat_ReplaceWeightedSynonyms(state,
			message_context,
			assembled,
			sizeof(state->chat_message),
			source_template != NULL ? source_template : template_text))
	{
		return 0;
	}
	return 1;
}

/*
=============
BotConstructChatMessageWithVariables

Runs the single Gladiator constructor pass and stores the resulting pending
chat text when it passes the retail payload guard.
=============
*/
static int BotConstructChatMessageWithVariables(bot_chatstate_t *state,
	unsigned long message_context,
	unsigned long variable_context,
	const char *template_text,
	const char *const variables[BOT_CHAT_MAX_MATCH_VARIABLES],
	char *out_message,
	size_t out_size,
	int replace_synonyms,
	int reply)
{
	if (state == NULL || template_text == NULL || out_message == NULL || out_size == 0U)
	{
		return 0;
	}

	(void)replace_synonyms;
	(void)reply;
	const int expanded = BotChat_ExpandChatMessageOnce(state,
		message_context,
		variable_context,
		template_text,
		variables,
		template_text);

	const size_t pending_length = strlen(state->chat_message);
	if (pending_length >= out_size)
	{
		/* HOST SAFETY: retail has no secondary caller-owned output buffer. */
		memcpy(out_message, state->chat_message, out_size - 1U);
		out_message[out_size - 1U] = '\0';
		return 0;
	}
	memcpy(out_message, state->chat_message, pending_length + 1U);
	return expanded;
}

/*
=============
BotConstructChatMessage

Constructs a message through the strict retail byte-one expansion path.
=============
*/
static int BotConstructChatMessage(bot_chatstate_t *state,
	unsigned long context,
	const char *template_text,
	char *out_message,
	size_t out_size)
{
	return BotConstructChatMessageWithVariables(state,
		context,
		0,
		template_text,
		NULL,
		out_message,
		out_size,
		0,
		0);
}

/*
=============
BotChat_DispatchMessage

Emits chat through the same elementary-action route as retail BotEnterChat.
=============
*/
static void BotChat_DispatchMessage(bot_chatstate_t *state,
	const char *message,
	int client,
	int sendto)
{
	(void)state;
	if (state == NULL || message == NULL || message[0] == '\0')
	{
		return;
	}

	if (sendto == BOT_CHAT_SENDTO_TEAM)
	{
		EA_SayTeam(client, message);
	}
	else
	{
		EA_Say(client, message);
	}
}

/*
=============
BotFreeChatFile

Releases only the per-state initial chat tree, matching retail ownership.
=============
*/
int BotFreeChatFile(bot_chatstate_t *state)
{
	if (state == NULL)
	{
		/* HOST SAFETY: retail dereferences the supplied embedded state. */
		return 0;
	}

	BotChat_CloseActiveScript(state);
	BotChat_FreeInitialTypes(state);
	state->active_chatfile[0] = '\0';
	state->active_chatname[0] = '\0';
	return 0;
}

/*
=============
BotChat_ConsoleQueueReady
=============
*/
static int BotChat_ConsoleQueueReady(const bot_chatstate_t *state)
{
	return state != NULL;
}

/*
=============
BotQueueConsoleMessage
=============
*/
int BotQueueConsoleMessage(bot_chatstate_t *state, int type, char *message)
{
	if (!BotChat_ConsoleQueueReady(state) || message == NULL)
	{
		return 0;
	}

	if (bot_console_message_heap == NULL
		|| bot_console_message_capacity == 0U)
	{
		BotLib_Print(PRT_ERROR, "empty console message heap\n");
		return 0;
	}

	bot_console_message_node_t *slot = BotChat_AllocConsoleMessage();
	if (slot == NULL)
	{
		BotLib_Print(PRT_ERROR, "empty console message heap\n");
		return 0;
	}

	slot->time = AAS_Time();
	slot->type = type;
	strncpy(slot->message, message, BOT_CHAT_RETAIL_MESSAGE_PAYLOAD_CHARS);
	slot->next = NULL;

	if (state->console_last != NULL)
	{
		state->console_last->next = slot;
		slot->prev = state->console_last;
		state->console_last = slot;
	}
	else
	{
		state->console_first = slot;
		state->console_last = slot;
		slot->prev = NULL;
	}
	++state->console_count;
	return (int)(intptr_t)state;
}

/*
=============
BotChat_SetTime

Overrides the cooldown clock for deterministic testing. Pass a negative value
to resume real-time sampling.
=============
*/
void BotChat_SetTime(bot_chatstate_t *state, double now_seconds)
{
	if (state == NULL)
	{
		return;
	}

	if (now_seconds < 0.0)
	{
		state->has_time_override = 0;
		state->time_override_seconds = 0.0;
		return;
	}

	state->time_override_seconds = now_seconds;
	state->has_time_override = 1;
}

/*
=============
BotChat_SetContextCooldown

Configures the cooldown duration for the supplied context identifier.
=============
*/
void BotChat_SetContextCooldown(bot_chatstate_t *state,
	unsigned long context,
	double cooldown_seconds)
{
	if (state == NULL)
	{
		return;
	}

	bot_chat_cooldown_entry_t *entry = BotChat_FindCooldownEntry(state, context, 1);
	if (entry == NULL)
	{
		return;
	}

	if (cooldown_seconds < 0.0)
	{
		cooldown_seconds = 0.0;
	}

	entry->duration_seconds = cooldown_seconds;
	entry->next_allowed_time = 0.0;
}

/*
=============
BotNextConsoleMessage

Returns the oldest retail console-message node without unlinking it.
=============
*/
bot_console_message_node_t *BotNextConsoleMessage(bot_chatstate_t *state)
{
	if (!BotChat_ConsoleQueueReady(state))
	{
		return NULL;
	}

	return state->console_first;
}

/*
=============
BotRemoveConsoleMessage

Removes one exact retail console-message node identity.
=============
*/
int BotRemoveConsoleMessage(bot_chatstate_t *state,
	bot_console_message_node_t *message)
{
	if (!BotChat_ConsoleQueueReady(state))
	{
		return 0;
	}

	for (bot_console_message_node_t *candidate = state->console_first;
		candidate != NULL;
		candidate = candidate->next)
	{
		if (candidate == message)
		{
			BotChat_UnlinkConsoleMessage(state, candidate);
			return (int)state->console_count;
		}
	}

	return (int)state->console_count;
}

/*
=============
BotNextConsoleMessageCopy
=============
*/
int BotNextConsoleMessageCopy(bot_chatstate_t *state,
	int *type,
	char *buffer,
	size_t buffer_size)
{
	if (!BotChat_ConsoleQueueReady(state))
	{
		if (type != NULL)
		{
			*type = 0;
		}
		if (buffer != NULL && buffer_size > 0)
		{
			buffer[0] = '\0';
		}
		return 0;
	}

	bot_console_message_node_t *slot = BotNextConsoleMessage(state);
	if (slot == NULL)
	{
		return 0;
	}

	if (type != NULL)
	{
		*type = slot->type;
	}

	if (buffer != NULL && buffer_size > 0)
	{
		size_t copy_length = 0U;
		while (copy_length < sizeof(slot->message)
			&& slot->message[copy_length] != '\0')
		{
			++copy_length;
		}
		if (copy_length >= buffer_size)
		{
			copy_length = buffer_size - 1U;
		}
		memcpy(buffer, slot->message, copy_length);
		buffer[copy_length] = '\0';
	}

	(void)BotRemoveConsoleMessage(state, slot);
	return 1;
}

/*
=============
BotRemoveConsoleMessageType

Removes the first queued node of the requested compatibility type.
=============
*/
int BotRemoveConsoleMessageType(bot_chatstate_t *state, int type)
{
	if (!BotChat_ConsoleQueueReady(state))
	{
		return 0;
	}

	for (bot_console_message_node_t *message = state->console_first;
		message != NULL;
		message = message->next)
	{
		if (message->type == type)
		{
			(void)BotRemoveConsoleMessage(state, message);
			return 1;
		}
	}

	return 0;
}

/*
=============
BotNumConsoleMessages
=============
*/
int BotNumConsoleMessages(bot_chatstate_t *state)
{
	if (!BotChat_ConsoleQueueReady(state))
	{
		return 0;
	}

	return (int)state->console_count;
}

/*
=============
BotNumInitialChats

Returns the number of parsed retail initial-chat lines for the requested type.
=============
*/
int BotNumInitialChats(const bot_chatstate_t *state, const char *type)
{
	const bot_initial_chat_type_t *initial_type =
		BotChat_FindInitialType((bot_chatstate_t *)state, type);
	if (initial_type == NULL)
	{
		return 0;
	}

	if (BotChat_TestInitialChatEnabled())
	{
		BotChat_PrintTestMessage((bot_chatstate_t *)state,
			"%s has %d chat lines\n",
			type,
			(int)initial_type->template_count);
		BotChat_PrintTestMessage((bot_chatstate_t *)state,
			"-------------------\n");
	}

	return (int)initial_type->template_count;
}

/*
=============
BotNumInitialChatsWithAliases

Maps Q3 successor type aliases before invoking the retail count entry point.
=============
*/
int BotNumInitialChatsWithAliases(const bot_chatstate_t *state,
	const char *type)
{
	return BotNumInitialChats(state,
		BotChat_CompatibilityInitialTypeName(type));
}

/*
=============
BotChat_ConstructAndDispatchContext

Selects, assembles, and emits a chat template for the provided context,
queuing the constructed text before forwarding it to the bridge.
=============
*/
static int BotChat_ConstructAndDispatchContext(bot_chatstate_t *state,
		unsigned long context,
		const char *seed,
		int client,
		int sendto,
		const char *missing_context_message)
{
	if (state == NULL) {
		return 0;
	}

	state->speaking_client = client;

	const double now_seconds = BotChat_CurrentTimeSeconds(state);
	if (!BotChat_EventAllowed(state, client, context, now_seconds)) {
		return 0;
	}

	bot_match_context_t *match_context = BotChat_FindResolvedMatchContext(state, context);
	const char *template_text = BotChat_SelectRandomTemplate(state, match_context, seed);
	if (template_text == NULL) {
		if (missing_context_message != NULL) {
			BotLib_Print(PRT_MESSAGE, "%s", missing_context_message);
		}
		return 0;
	}

	char message[BOT_CHAT_MAX_MESSAGE_CHARS];
	if (!BotConstructChatMessage(state, context, template_text, message, sizeof(message))) {
		return 0;
	}

	BotChat_DispatchMessage(state, message, client, sendto);
	BotChat_ClearPendingMessage(state);
	BotChat_CommitClientCooldown(state, (size_t)client, now_seconds);
	return 1;
}

/*
=============
BotChat_EnterGame

Triggers the MSG_ENTERGAME match context through the shared construction
helper and returns success when a message is dispatched.
=============
*/
int BotChat_EnterGame(bot_chatstate_t *state, int client, int sendto)
{
	return BotChat_ConstructAndDispatchContext(state,
			BOT_CHAT_CONTEXT_ENTERGAME,
			state != NULL ? state->active_chatname : NULL,
			client,
			sendto,
			"BotEnterChat: no templates loaded for enter game context\n");
}

/*
=============
BotChat_Kill

Emits the MSG_KILL template after cooldown validation.
=============
*/
int BotChat_Kill(bot_chatstate_t *state, int client, int sendto)
{
	return BotChat_ConstructAndDispatchContext(state,
			BOT_CHAT_CONTEXT_KILL,
			state != NULL ? state->active_chatname : NULL,
			client,
			sendto,
			"BotChat_Kill: no templates loaded for kill context\n");
}

/*
=============
BotChat_Death

Emits the MSG_DEATH template after cooldown validation.
=============
*/
int BotChat_Death(bot_chatstate_t *state, int client, int sendto)
{
	return BotChat_ConstructAndDispatchContext(state,
			BOT_CHAT_CONTEXT_DEATH,
			state != NULL ? state->active_chatname : NULL,
			client,
			sendto,
			"BotChat_Death: no templates loaded for death context\n");
}

/*
=============
BotChat_EnemySuicide

Emits the MSG_ENEMYSUICIDE template after cooldown validation.
=============
*/
int BotChat_EnemySuicide(bot_chatstate_t *state, int client, int sendto)
{
	return BotChat_ConstructAndDispatchContext(state,
			BOT_CHAT_CONTEXT_ENEMYSUICIDE,
			state != NULL ? state->active_chatname : NULL,
			client,
			sendto,
			"BotChat_EnemySuicide: no templates loaded for enemy suicide context\n");
}

/*
=============
BotChat_HitTalking

Emits the MSG_HITTALKING template after cooldown validation.
=============
*/
int BotChat_HitTalking(bot_chatstate_t *state, int client, int sendto)
{
	return BotChat_ConstructAndDispatchContext(state,
			BOT_CHAT_CONTEXT_HITTALKING,
			state != NULL ? state->active_chatname : NULL,
			client,
			sendto,
			"BotChat_HitTalking: no templates loaded for hit talking context\n");
}

/*
=============
BotChat_HitNoDeath

Emits the MSG_HITNODEATH template after cooldown validation.
=============
*/
int BotChat_HitNoDeath(bot_chatstate_t *state, int client, int sendto)
{
	return BotChat_ConstructAndDispatchContext(state,
			BOT_CHAT_CONTEXT_HITNODEATH,
			state != NULL ? state->active_chatname : NULL,
			client,
			sendto,
			"BotChat_HitNoDeath: no templates loaded for hit no death context\n");
}

/*
=============
BotChat_HitNoKill

Emits the MSG_HITNOKILL template after cooldown validation.
=============
*/
int BotChat_HitNoKill(bot_chatstate_t *state, int client, int sendto)
{
	return BotChat_ConstructAndDispatchContext(state,
			BOT_CHAT_CONTEXT_HITNOKILL,
			state != NULL ? state->active_chatname : NULL,
			client,
			sendto,
			"BotChat_HitNoKill: no templates loaded for hit no kill context\n");
}

/*
=============
BotChat_Random

Emits the MSG_RANDOM template after cooldown validation.
=============
*/
int BotChat_Random(bot_chatstate_t *state, int client, int sendto)
{
	return BotChat_ConstructAndDispatchContext(state,
			BOT_CHAT_CONTEXT_RANDOM,
			state != NULL ? state->active_chatname : NULL,
			client,
			sendto,
			"BotChat_Random: no templates loaded for random context\n");
}

/*
=============
BotChat_Insult

Emits the MSG_INSULT template after cooldown validation.
=============
*/
int BotChat_Insult(bot_chatstate_t *state, int client, int sendto)
{
	return BotChat_ConstructAndDispatchContext(state,
			BOT_CHAT_CONTEXT_INSULT,
			state != NULL ? state->active_chatname : NULL,
			client,
			sendto,
			"BotChat_Insult: no templates loaded for insult context\n");
}

/*
=============
BotChat_Praise

Emits the MSG_PRAISE template after cooldown validation.
=============
*/
int BotChat_Praise(bot_chatstate_t *state, int client, int sendto)
{
	return BotChat_ConstructAndDispatchContext(state,
			BOT_CHAT_CONTEXT_PRAISE,
			state != NULL ? state->active_chatname : NULL,
			client,
			sendto,
			"BotChat_Praise: no templates loaded for praise context\n");
}

/*
=============
BotEnterChat

Dispatches the pending chat through the retail elementary-action route.
=============
*/
char BotEnterChat(bot_chatstate_t *state, int client, int sendto)
{
	if (state == NULL)
	{
		/* HOST SAFETY: retail dereferences the supplied chat state. */
		return 0;
	}

	if (state->chat_message[0] == '\0')
	{
		return 0;
	}

	if (sendto == BOT_CHAT_SENDTO_TEAM)
	{
		EA_SayTeam(client, state->chat_message);
	}
	else
	{
		EA_Say(client, state->chat_message);
	}
	state->chat_message[0] = '\0';
	return 0;
}

/*
=============
BotChat_InitialChatV

Selects and constructs one initial chat from a caller-owned variable list.
=============
*/
static int BotChat_InitialChatV(bot_chatstate_t *state,
	const char *type,
	unsigned long context,
	va_list args)
{
	if (state == NULL || type == NULL)
	{
		return 0;
	}

	const char *template_text = BotChat_ChooseInitialTemplate(state, type);
	if (template_text == NULL)
	{
		return 0;
	}

	const char *variables[BOT_CHAT_MAX_MATCH_VARIABLES] = {0};
	for (size_t i = 0; i < BOT_CHAT_MAX_MATCH_VARIABLES; ++i)
	{
		const char *value = va_arg(args, const char *);
		if (value == NULL)
		{
			break;
		}
		variables[i] = value;
	}

	char message[BOT_CHAT_MAX_MESSAGE_CHARS];
	return BotConstructChatMessageWithVariables(state,
		context,
		0,
		template_text,
		variables,
		message,
		sizeof(message),
		1,
		0);
}

/*
=============
BotInitialChat

Retail entry point. The retail call has no explicit synonym context.
=============
*/
void BotInitialChat(bot_chatstate_t *state, char *type, ...)
{
	va_list args;
	va_start(args, type);
	(void)BotChat_InitialChatV(state, type, 0UL, args);
	va_end(args);
}

/*
=============
BotInitialChatWithContext

Compatibility adapter retaining the Q3-shaped explicit message context.
=============
*/
int BotInitialChatWithContext(bot_chatstate_t *state,
	const char *type,
	unsigned long context,
	...)
{
	va_list args;
	va_start(args, context);
	const int result = BotChat_InitialChatV(state,
		BotChat_CompatibilityInitialTypeName(type),
		context,
		args);
	va_end(args);
	return result;
}

/*
=============
BotChat_MatchReplyPattern

Matches one reply key against the scan-wide retail capture table. Variable
pointers are written as soon as their pieces are encountered and lengths are
left untouched on later failures, matching Gladiator's StringsMatch state.
=============
*/
static int BotChat_MatchReplyPattern(const char *pattern,
	bot_reply_match_t *match)
{
	if (pattern == NULL || match == NULL || match->message == NULL)
	{
		return 0;
	}

	const char *message = match->message;
	size_t pattern_offset = 0;
	size_t message_offset = 0;
	long last_variable = -1;

	while (pattern[pattern_offset] != '\0')
	{
		if (pattern[pattern_offset] == BOT_CHAT_MATCH_PIECE_SEPARATOR)
		{
			++pattern_offset;
			continue;
		}

		if (BotChat_PatternAtVariableReference(pattern, pattern_offset))
		{
			size_t value_offset = pattern_offset + 2U;
			unsigned long value = 0UL;
			int saw_digit = 0;
			while (isdigit((unsigned char)pattern[value_offset]))
			{
				value = value * 10UL
					+ (unsigned long)(pattern[value_offset] - '0');
				saw_digit = 1;
				++value_offset;
			}
			if (!saw_digit || !BotChat_IsEscapeChar(pattern[value_offset])
				|| value >= BOT_CHAT_MAX_MATCH_VARIABLES)
			{
				return 0;
			}

			match->variables[value] = message + message_offset;
			last_variable = (long)value;
			pattern_offset = value_offset + 1U;
			continue;
		}

		if (pattern[pattern_offset] == BOT_CHAT_MATCH_ALT_START)
		{
			size_t next_pattern_offset = 0;
			size_t match_start = 0;
			size_t match_end = 0;
			if (!BotChat_MatchAlternativePatternPiece(pattern,
					pattern_offset,
					message,
					message_offset,
					&next_pattern_offset,
					&match_start,
					&match_end))
			{
				return 0;
			}

			if (last_variable >= 0)
			{
				match->lengths[last_variable] = match_start - message_offset;
				last_variable = -1;
			}
			else if (match_start != message_offset)
			{
				return 0;
			}
			message_offset = match_end;
			pattern_offset = next_pattern_offset;
			continue;
		}

		const size_t literal_start = pattern_offset;
		while (pattern[pattern_offset] != '\0'
			&& !BotChat_PatternAtVariableReference(pattern, pattern_offset)
			&& pattern[pattern_offset] != BOT_CHAT_MATCH_ALT_START
			&& pattern[pattern_offset] != BOT_CHAT_MATCH_PIECE_SEPARATOR)
		{
			++pattern_offset;
		}

		const size_t literal_length = pattern_offset - literal_start;
		size_t match_start = 0;
		size_t match_end = 0;
		if (!BotChat_FindCaseInsensitiveSpan(message,
				message_offset,
				pattern + literal_start,
				literal_length,
				&match_start,
				&match_end))
		{
			return 0;
		}

		if (last_variable >= 0)
		{
			match->lengths[last_variable] = match_start - message_offset;
			last_variable = -1;
		}
		else if (match_start != message_offset)
		{
			return 0;
		}
		message_offset = match_end;
	}

	if (last_variable >= 0)
	{
		match->lengths[last_variable] = strlen(message + message_offset);
		return 1;
	}

	return message[message_offset] == '\0';
}

/*
=============
BotChat_CopyReplyMatchVariables

Copies the pointer-and-length capture table into constructor-owned strings.
=============
*/
static void BotChat_CopyReplyMatchVariables(const bot_reply_match_t *match,
	char storage[][BOT_CHAT_MAX_MESSAGE_CHARS],
	const char *variables[BOT_CHAT_MAX_MATCH_VARIABLES])
{
	BotChat_ClearCapturedVariables(variables);
	if (match == NULL || match->message == NULL || storage == NULL)
	{
		return;
	}

	const char *message_end = match->message + match->message_length;
	for (size_t i = 0; i < BOT_CHAT_MAX_MATCH_VARIABLES; ++i)
	{
		const char *start = match->variables[i];
		if (start == NULL || start < match->message || start > message_end)
		{
			continue;
		}

		size_t length = match->lengths[i];
		const size_t available = (size_t)(message_end - start);
		if (length > available)
		{
			length = available;
		}
		if (length >= BOT_CHAT_MAX_MESSAGE_CHARS)
		{
			length = BOT_CHAT_MAX_MESSAGE_CHARS - 1U;
		}
		memcpy(storage[i], start, length);
		storage[i][length] = '\0';
		variables[i] = storage[i];
	}
}

/*
=============
BotChat_ReplyKeyMatches

Evaluates one parsed reply key and captures variables when present.
=============
*/
static int BotChat_ReplyKeyMatches(const bot_chatstate_t *state,
	const bot_reply_key_t *key,
	const char *message,
	bot_reply_match_t *match,
	int compatibility_matching)
{
	if (key == NULL || key->pattern == NULL || message == NULL)
	{
		return 0;
	}

	if (key->is_pattern)
	{
		return BotChat_MatchReplyPattern(key->pattern, match);
	}

	switch (key->special)
	{
		case BOT_CHAT_REPLY_KEY_NAME:
			/* Retail parses RCKFL_NAME but never evaluates it. */
			return compatibility_matching && state != NULL
				&& state->chat_name[0] != '\0'
				&& StringContains(message, state->chat_name, 0) != NULL;
		case BOT_CHAT_REPLY_KEY_BOTNAMES:
			return state != NULL
				&& state->chat_name[0] != '\0'
				&& BotChat_StringContainsCaseInsensitive(key->pattern, state->chat_name);
		case BOT_CHAT_REPLY_KEY_GENDER_FEMALE:
			return state != NULL && state->chat_gender == CHAT_GENDERFEMALE;
		case BOT_CHAT_REPLY_KEY_GENDER_MALE:
			return state != NULL && state->chat_gender == CHAT_GENDERMALE;
		case BOT_CHAT_REPLY_KEY_GENDERLESS:
			return state == NULL || state->chat_gender == CHAT_GENDERLESS;
		default:
			break;
	}

	if (compatibility_matching)
	{
		return BotChat_CompatibilityStringContainsWord(message, key->pattern);
	}
	return StringContains(message, key->pattern, 0) != NULL;
}

/*
=============
BotChat_ReplyRuleMatches

Applies retail-style reply-key AND, NOT, and optional-key semantics.
=============
*/
static int BotChat_ReplyRuleMatches(const bot_chatstate_t *state,
	const bot_reply_rule_t *rule,
	const char *message,
	bot_reply_match_t *match,
	int compatibility_matching)
{
	if (rule == NULL || message == NULL)
	{
		return 0;
	}

	if (rule->key_count == 0)
	{
		return 1;
	}

	int found = 0;
	for (size_t i = rule->key_count; i > 0; --i)
	{
		const bot_reply_key_t *key = &rule->keys[i - 1U];
		const int matched = BotChat_ReplyKeyMatches(state,
			key,
			message,
			match,
			compatibility_matching);
		if (key->required)
		{
			if (!matched)
			{
				return 0;
			}
			found = 1;
			continue;
		}
		if (key->negated)
		{
			if (matched)
			{
				return 0;
			}
			continue;
		}
		if (matched)
		{
			found = 1;
		}
	}

	return found;
}

/*
=============
BotChat_SelectRetailReplyResponse

Selects a reply response with the retail eligible-count/raw-list traversal.
The predecrement intentionally happens before the recency check, so an
all-recent rule selects its linked-list head and the RNG endpoint can miss.
=============
*/
static int BotChat_SelectRetailReplyResponse(const bot_reply_rule_t *rule,
	size_t *response_index)
{
	if (rule == NULL || response_index == NULL)
	{
		return 0;
	}

	int available_count = 0;
	for (size_t i = rule->response_count; i > 0; --i)
	{
		const size_t index = i - 1U;
		if (rule->response_times == NULL
			|| rule->response_times[index] <= AAS_Time())
		{
			++available_count;
		}
	}

	int num = (int)(BotChat_RetailRandomFloat() * (float)available_count);
	for (size_t i = rule->response_count; i > 0; --i)
	{
		const size_t index = i - 1U;
		if (--num < 0)
		{
			*response_index = index;
			return 1;
		}
		(void)AAS_Time();
	}

	return 0;
}

/*
=============
BotChat_FindMatchingReplyRule

Searches one retail reply table through its priority traversal. Gladiator
construction receives the final mutable scan table, while the Q3-shaped
extension receives the best-rule snapshot.
=============
*/
static const bot_reply_rule_t *BotChat_FindMatchingReplyRule(
	bot_chatstate_t *source,
	const bot_chatstate_t *matcher_state,
	const char *message,
	int snapshot_best_match,
	int compatibility_matching,
	char captured_storage[][BOT_CHAT_MAX_MESSAGE_CHARS],
	const char *captured_variables[BOT_CHAT_MAX_MATCH_VARIABLES],
	size_t *response_index)
{
	if (source == NULL || message == NULL)
	{
		return NULL;
	}
	if (matcher_state == NULL)
	{
		matcher_state = source;
	}

	const bot_reply_rule_t *best_rule = NULL;
	int best_priority = 0;
	BotChat_ClearCapturedVariables(captured_variables);
	bot_reply_match_t working_match = {0};
	bot_reply_match_t best_match = {0};
	working_match.message = message;
	working_match.message_length = strlen(message);

	for (size_t i = source->replies.rule_count; i > 0; --i)
	{
		bot_reply_rule_t *candidate_rule = &source->replies.rules[i - 1U];
		if (!BotChat_ReplyRuleMatches(matcher_state,
				candidate_rule,
				message,
				&working_match,
				compatibility_matching))
		{
			continue;
		}

		if (candidate_rule->priority <= (float)best_priority)
		{
			continue;
		}

		size_t candidate_response_index = 0;
		if (!BotChat_SelectRetailReplyResponse(candidate_rule,
			&candidate_response_index))
		{
			continue;
		}

		best_rule = candidate_rule;
		best_priority = (int)candidate_rule->priority;
		if (response_index != NULL)
		{
			*response_index = candidate_response_index;
		}
		if (snapshot_best_match)
		{
			best_match = working_match;
		}
	}
	if (best_rule != NULL)
	{
		BotChat_CopyReplyMatchVariables(snapshot_best_match
				? &best_match
				: &working_match,
			captured_storage,
			captured_variables);
	}

	return best_rule;
}

/*
=============
BotChat_ApplyExplicitReplyVariables

Overlays fixed Q3 var0-var7 reply arguments onto captured match variables.
=============
*/
static int BotChat_ApplyExplicitReplyVariables(
	char storage[][BOT_CHAT_MAX_MESSAGE_CHARS],
	const char *variables[BOT_CHAT_MAX_MATCH_VARIABLES],
	const char *const explicit_variables[BOT_CHAT_MAX_MATCH_VARIABLES])
{
	if (storage == NULL || variables == NULL || explicit_variables == NULL)
	{
		return 0;
	}

	int applied = 0;
	for (size_t i = 0; i < BOT_CHAT_MAX_MATCH_VARIABLES && i < 8U; ++i)
	{
		if (explicit_variables[i] == NULL)
		{
			continue;
		}
		snprintf(storage[i],
			BOT_CHAT_MAX_MESSAGE_CHARS,
			"%s",
			explicit_variables[i]);
		variables[i] = storage[i];
		applied = 1;
	}

	return applied;
}

/*
=============
BotChat_PrintReplyTestMessages

Constructs and prints every response in a matched reply rule for bot_testrchat.
=============
*/
static int BotChat_PrintReplyTestMessages(bot_chatstate_t *state,
	const bot_reply_rule_t *reply_rule,
	unsigned long int mcontext,
	unsigned long int vcontext,
	const char *const variables[BOT_CHAT_MAX_MATCH_VARIABLES],
	int snapshot_best_match)
{
	if (state == NULL || reply_rule == NULL || reply_rule->response_count == 0)
	{
		return 0;
	}

	for (size_t i = reply_rule->response_count; i > 0; --i)
	{
		const char *template_text = reply_rule->responses[i - 1U];
		if (template_text == NULL)
		{
			continue;
		}

		char constructed[BOT_CHAT_MAX_MESSAGE_CHARS];
		if (!BotConstructChatMessageWithVariables(state,
			mcontext,
			vcontext,
			template_text,
			variables,
			constructed,
			sizeof(constructed),
			snapshot_best_match,
			snapshot_best_match))
		{
			return 0;
		}

		BotChat_RemoveTildes(constructed);
		BotChat_RemoveTildes(state->chat_message);
		BotChat_PrintTestMessage(state, "%s\n", constructed);
	}

	return 1;
}

/*
=============
BotReplyChatInternal

Constructs a reply from the loaded reply tables, with optional Q3 split
mcontext/vcontext construction semantics.
=============
*/
static int BotReplyChatInternal(bot_chatstate_t *state,
	const char *message,
	unsigned long int mcontext,
	unsigned long int vcontext,
	const char *const explicit_variables[BOT_CHAT_MAX_MATCH_VARIABLES],
	int snapshot_best_match,
	int compatibility_matching)
{
	if (state == NULL || message == NULL)
	{
		return 0;
	}
	const char *template_text = NULL;
	char captured_storage[BOT_CHAT_MAX_MATCH_VARIABLES][BOT_CHAT_MAX_MESSAGE_CHARS];
	const char *captured_variables[BOT_CHAT_MAX_MATCH_VARIABLES] = {0};
	int has_captured_variables = 0;

	char constructed[BOT_CHAT_MAX_MESSAGE_CHARS];

	/*
	 * Retail BotReplyChat traverses only data_10064380, populated by
	 * BotSetupChatAI's rchatfile load.
	 */
	bot_chatstate_t *reply_source = bot_chat_setup_state;
	if (reply_source == NULL)
	{
		return 0;
	}
	if (!reply_source->has_reply_chats)
	{
		return 0;
	}

	const bot_reply_rule_t *reply_rule = NULL;
	size_t reply_index = 0;
	BotChat_ClearCapturedVariables(captured_variables);
	has_captured_variables = 0;
	char retail_message[BOT_CHAT_MAX_MESSAGE_CHARS];
	/* HOST SAFETY: retail uses an unbounded strcpy into this 0x98-byte object. */
	strncpy(retail_message, message, sizeof(retail_message) - 1U);
	retail_message[sizeof(retail_message) - 1U] = '\0';
	reply_rule = BotChat_FindMatchingReplyRule(reply_source,
		state,
		retail_message,
		snapshot_best_match,
		compatibility_matching,
		captured_storage,
		captured_variables,
		&reply_index);
	if (reply_rule != NULL)
	{
		has_captured_variables = 1;
	}

	if (reply_rule != NULL && reply_rule->response_count > 0)
	{
		if (BotChat_TestReplyChatEnabled())
		{
			char construction_storage[BOT_CHAT_MAX_MATCH_VARIABLES][BOT_CHAT_MAX_MESSAGE_CHARS];
			const char *construction_variables[BOT_CHAT_MAX_MATCH_VARIABLES] = {0};
			if (has_captured_variables)
			{
				BotChat_CopyCapturedVariables(construction_storage,
					construction_variables,
					captured_variables);
			}
			(void)BotChat_ApplyExplicitReplyVariables(construction_storage,
				construction_variables,
				explicit_variables);
			return BotChat_PrintReplyTestMessages(state,
				reply_rule,
				mcontext,
				vcontext,
				construction_variables,
				snapshot_best_match);
		}

		BotChat_MarkRecent(reply_rule->response_times,
			reply_index,
			reply_rule->response_count,
			(double)AAS_Time());
		template_text = reply_rule->responses[reply_index];
		if (template_text != NULL)
		{
			char construction_storage[BOT_CHAT_MAX_MATCH_VARIABLES][BOT_CHAT_MAX_MESSAGE_CHARS];
			const char *construction_variables[BOT_CHAT_MAX_MATCH_VARIABLES] = {0};
			if (has_captured_variables)
			{
				BotChat_CopyCapturedVariables(construction_storage,
					construction_variables,
					captured_variables);
			}
			const int has_explicit_variables =
				BotChat_ApplyExplicitReplyVariables(construction_storage,
					construction_variables,
					explicit_variables);
			(void)BotConstructChatMessageWithVariables(state,
					mcontext,
					vcontext,
					template_text,
					(has_captured_variables || has_explicit_variables)
						? construction_variables
						: NULL,
					constructed,
					sizeof(constructed),
					snapshot_best_match,
					snapshot_best_match);
		}
		return 1;
	}

	return 0;
}

/*
=============
BotReplyChat

Retail reply entry point with fixed message/variable contexts 0 and 16.
=============
*/
int BotReplyChat(bot_chatstate_t *state, const char *message)
{
	const char *explicit_variables[BOT_CHAT_MAX_MATCH_VARIABLES] = {0};
	return BotReplyChatInternal(state,
		message,
		0UL,
		16UL,
		explicit_variables,
		0,
		0);
}

/*
=============
BotReplyChatWithContext

Compatibility adapter for callers carrying the unused Q3 context argument.
=============
*/
int BotReplyChatWithContext(bot_chatstate_t *state,
	const char *message,
	unsigned long int context)
{
	(void)context;
	return BotReplyChat(state, message);
}

/*
=============
BotReplyChatWithContexts

Q3-shaped reply entry point with split message and variable synonym contexts.
=============
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
	const char *var7)
{
	const char *explicit_variables[BOT_CHAT_MAX_MATCH_VARIABLES] = {0};
	explicit_variables[0] = var0;
	explicit_variables[1] = var1;
	explicit_variables[2] = var2;
	explicit_variables[3] = var3;
	explicit_variables[4] = var4;
	explicit_variables[5] = var5;
	explicit_variables[6] = var6;
	explicit_variables[7] = var7;

	return BotReplyChatInternal(state,
		message,
		mcontext,
		vcontext,
		explicit_variables,
		1,
		1);
}

/*
=============
BotChat_TextStartsWith

Checks whether a constructed message segment starts with the supplied text.
=============
*/
static int BotChat_TextStartsWith(const char *text,
	size_t offset,
	const char *prefix,
	size_t *next_offset)
{
	if (text == NULL || prefix == NULL)
	{
		return 0;
	}

	const size_t prefix_length = strlen(prefix);
	if (strncmp(text + offset, prefix, prefix_length) != 0)
	{
		return 0;
	}

	if (next_offset != NULL)
	{
		*next_offset = offset + prefix_length;
	}
	return 1;
}

/*
=============
BotChat_TemplateRandomExpansionMatches

Tests every known expansion for a random table reference against constructed text.
=============
*/
static int BotChat_TemplateRandomExpansionMatches(const bot_chatstate_t *state,
	unsigned long context,
	const char *template_text,
	size_t template_offset,
	const char *constructed_text,
	size_t constructed_offset,
	const char *name,
	size_t name_length);

/*
=============
BotChat_TemplateCanConstructTextAt

Recursively matches a stored template against an already constructed message.
=============
*/
static int BotChat_TemplateCanConstructTextAt(const bot_chatstate_t *state,
	unsigned long context,
	const char *template_text,
	size_t template_offset,
	const char *constructed_text,
	size_t constructed_offset)
{
	while (template_text[template_offset] != '\0')
	{
		if (!BotChat_IsEscapeChar(template_text[template_offset]))
		{
			if (constructed_text[constructed_offset] != template_text[template_offset])
			{
				return 0;
			}
			++template_offset;
			++constructed_offset;
			continue;
		}

		const char escape = template_text[template_offset + 1];
		if (escape == '\0')
		{
			return 0;
		}

		size_t name_start = template_offset + 2;
		size_t name_end = name_start;
		while (template_text[name_end] != '\0'
			&& !BotChat_IsEscapeChar(template_text[name_end]))
		{
			++name_end;
		}
		if (!BotChat_IsEscapeChar(template_text[name_end]))
		{
			return 0;
		}

		if (escape == 'r')
		{
			return BotChat_TemplateRandomExpansionMatches(state,
				context,
				template_text,
				name_end + 1,
				constructed_text,
				constructed_offset,
				template_text + name_start,
				name_end - name_start);
		}

		if (escape == 'v')
		{
			unsigned long value = 0;
			for (size_t i = name_start; i < name_end; ++i)
			{
				if (!isdigit((unsigned char)template_text[i]))
				{
					return 0;
				}
				value = value * 10UL + (unsigned long)(template_text[i] - '0');
			}

			const char *variable_name = BotChat_VariableNameForNumber(context, value);
			char replacement[64];
			int written;
			if (variable_name != NULL)
			{
				written = snprintf(replacement,
					sizeof(replacement),
					"{%s}",
					variable_name);
			}
			else
			{
				written = snprintf(replacement,
					sizeof(replacement),
					"{VAR%lu}",
					value);
			}
			if (written <= 0 || (size_t)written >= sizeof(replacement))
			{
				return 0;
			}

			size_t next_offset;
			if (!BotChat_TextStartsWith(constructed_text,
					constructed_offset,
					replacement,
					&next_offset))
			{
				return 0;
			}
			template_offset = name_end + 1;
			constructed_offset = next_offset;
			continue;
		}

		return 0;
	}

	return constructed_text[constructed_offset] == '\0';
}

/*
=============
BotChat_TemplateRandomCandidateMatches

Tests one random expansion candidate before continuing template matching.
=============
*/
static int BotChat_TemplateRandomCandidateMatches(const bot_chatstate_t *state,
	unsigned long context,
	const char *template_text,
	size_t template_offset,
	const char *constructed_text,
	size_t constructed_offset,
	const char *candidate)
{
	size_t next_offset;
	if (!BotChat_TextStartsWith(constructed_text,
			constructed_offset,
			candidate,
			&next_offset))
	{
		return 0;
	}

	return BotChat_TemplateCanConstructTextAt(state,
		context,
		template_text,
		template_offset,
		constructed_text,
		next_offset);
}

/*
=============
BotChat_TemplateRandomExpansionMatches

Tests every known expansion for a random table reference against constructed text.
=============
*/
static int BotChat_TemplateRandomExpansionMatches(const bot_chatstate_t *state,
	unsigned long context,
	const char *template_text,
	size_t template_offset,
	const char *constructed_text,
	size_t constructed_offset,
	const char *name,
	size_t name_length)
{
	char random_name[64];
	if (name_length == 0 || name_length >= sizeof(random_name))
	{
		return 0;
	}
	memcpy(random_name, name, name_length);
	random_name[name_length] = '\0';

	const bot_synonym_context_t *synonym_context =
		BotChat_FindSynonymContextByToken(state, random_name);
	if (synonym_context != NULL)
	{
		for (size_t group_index = 0;
			group_index < synonym_context->group_count;
			++group_index)
		{
			const bot_synonym_group_t *group = &synonym_context->groups[group_index];
			for (size_t phrase_index = 0;
				phrase_index < group->phrase_count;
				++phrase_index)
			{
				const char *candidate = group->phrases[phrase_index].text;
				if (candidate != NULL
					&& BotChat_TemplateRandomCandidateMatches(state,
						context,
						template_text,
						template_offset,
						constructed_text,
						constructed_offset,
						candidate))
				{
					return 1;
				}
			}
		}
	}

	const bot_random_string_table_t *state_table =
		BotChat_FindStateRandomTable(state, random_name);
	if (state_table != NULL)
	{
		for (size_t i = 0; i < state_table->entry_count; ++i)
		{
			if (state_table->entries[i] != NULL
				&& BotChat_TemplateRandomCandidateMatches(state,
					context,
					template_text,
					template_offset,
					constructed_text,
					constructed_offset,
					state_table->entries[i]))
			{
				return 1;
			}
		}
	}

	const bot_chat_random_table_t *builtin_table =
		BotChat_FindBuiltinRandomTable(random_name);
	if (builtin_table != NULL)
	{
		for (size_t i = 0; i < builtin_table->entry_count; ++i)
		{
			if (builtin_table->entries[i] != NULL
				&& BotChat_TemplateRandomCandidateMatches(state,
					context,
					template_text,
					template_offset,
					constructed_text,
					constructed_offset,
					builtin_table->entries[i]))
			{
				return 1;
			}
		}
	}

	return 0;
}

/*
=============
BotChat_TemplateCanConstructText

Checks whether a stored template can produce the supplied constructed text.
=============
*/
static int BotChat_TemplateCanConstructText(const bot_chatstate_t *state,
	unsigned long context,
	const char *template_text,
	const char *constructed_text)
{
	if (state == NULL || template_text == NULL || constructed_text == NULL)
	{
		return 0;
	}

	return BotChat_TemplateCanConstructTextAt(state,
		context,
		template_text,
		0,
		constructed_text,
		0);
}

/*
=============
BotChat_TemplateContainsEscape

Checks whether a stored template contains an expandable internal marker.
=============
*/
static int BotChat_TemplateContainsEscape(const char *template_text)
{
	if (template_text == NULL)
	{
		return 0;
	}

	for (size_t i = 0; template_text[i] != '\0'; ++i)
	{
		if (BotChat_IsEscapeChar(template_text[i]))
		{
			return 1;
		}
	}

	return 0;
}

/*
=============
BotChatLength

Returns the length of the pending constructed chat message.
=============
*/
unsigned int BotChatLength(bot_chatstate_t *state)
{
	if (state == NULL)
	{
		return 0;
	}

	return (unsigned int)strlen(state->chat_message);
}

/*
=============
BotGetChatMessage

Copies the pending chat text into the caller buffer and clears it.
=============
*/
void BotGetChatMessage(bot_chatstate_t *state, char *buffer, int buffer_size)
{
	if (buffer != NULL && buffer_size > 0)
	{
		buffer[0] = '\0';
	}
	if (state == NULL || buffer == NULL || buffer_size <= 0)
	{
		return;
	}

	snprintf(buffer, (size_t)buffer_size, "%s", state->chat_message);
	BotChat_RemoveTildes(buffer);
	BotChat_ClearPendingMessage(state);
}

/*
=============
BotSetChatGender

Stores the reply-chat gender metadata using Quake III's gender constants.
=============
*/
void BotSetChatGender(bot_chatstate_t *state, int gender)
{
	if (state == NULL)
	{
		return;
	}

	switch (gender)
	{
		case CHAT_GENDERFEMALE:
			state->chat_gender = CHAT_GENDERFEMALE;
			break;
		case CHAT_GENDERMALE:
			state->chat_gender = CHAT_GENDERMALE;
			break;
		default:
			state->chat_gender = CHAT_GENDERLESS;
			break;
	}
}

/*
=============
BotSetChatName

Stores the retail 15-byte bot name and returns the destination slot.
=============
*/
char *BotSetChatName(bot_chatstate_t *state, char *name)
{
	if (state == NULL)
	{
		/* HOST SAFETY: retail dereferences the supplied chat state. */
		return NULL;
	}

	if (name == NULL)
	{
		/* HOST SAFETY: retail passes the pointer directly to strncpy. */
		name = "";
	}

	char *result = strncpy(state->chat_name, name, sizeof(state->chat_name) - 1U);
	state->chat_name[sizeof(state->chat_name) - 1U] = '\0';
	return result;
}

/*
=============
BotSetChatNameWithClient

Compatibility adapter retaining host-side owner metadata. Retail chat
dispatch deliberately ignores that metadata and uses BotEnterChat's client.
=============
*/
void BotSetChatNameWithClient(bot_chatstate_t *state,
	const char *name,
	int client)
{
	if (state == NULL)
	{
		return;
	}

	state->chat_client = client;
	state->chat_client_valid = 1;
	(void)BotSetChatName(state, name);
}

/*
=============
BotResetChatAI

Clears all shared reply-message recency timestamps.
=============
*/
void BotResetChatAI(void)
{
	if (bot_chat_setup_state == NULL)
	{
		return;
	}

	for (size_t i = 0; i < bot_chat_setup_state->replies.rule_count; ++i)
	{
		bot_reply_rule_t *rule = &bot_chat_setup_state->replies.rules[i];
		for (size_t j = 0; j < rule->response_count; ++j)
		{
			rule->response_times[j] = 0.0f;
		}
	}
}

/*
=============
BotChatName

Returns the chat persona name used by reply-key matching.
=============
*/
const char *BotChatName(const bot_chatstate_t *state)
{
	if (state == NULL)
	{
		return "";
	}

	return state->chat_name;
}

/*
=============
BotChatClient

Returns the owning client used for chat commands.
=============
*/
int BotChatClient(const bot_chatstate_t *state)
{
	if (state == NULL || !state->chat_client_valid)
	{
		return -1;
	}

	return state->chat_client;
}

/*
=============
BotFindMatch

Finds the first setup match template that applies to the requested context.
=============
*/
int BotFindMatch(char *str, bot_match_t *match, int context)
{
	if (str == NULL || match == NULL || bot_chat_setup_state == NULL)
	{
		return 0;
	}

	strncpy(match->string, str, BOT_CHAT_RETAIL_MESSAGE_PAYLOAD_CHARS);
	/* HOST SAFETY: retail does not guarantee termination after this strncpy. */
	match->string[BOT_CHAT_RETAIL_MESSAGE_PAYLOAD_CHARS] = '\0';
	while (match->string[0] != '\0')
	{
		const size_t length = strlen(match->string);
		if (match->string[length - 1] != '\n')
		{
			break;
		}
		match->string[length - 1] = '\0';
	}

	for (size_t i = 0; i < bot_chat_setup_state->match_order_count; ++i)
	{
		const bot_match_order_entry_t *entry =
			&bot_chat_setup_state->match_order[i];
		if ((entry->match_context & context) == 0UL)
		{
			continue;
		}
		if (!BotChat_MatchVariablePatternResult(entry->template_text, match))
		{
			continue;
		}

		match->type = (int)entry->message_type;
		match->subtype = (int)entry->subtype;
		return 1;
	}

	return 0;
}

/*
=============
BotMatchVariable

Copies a captured public match variable into the caller buffer.
=============
*/
char *BotMatchVariable(bot_match_t *match, int variable, char *buffer)
{
	if (buffer == NULL)
	{
		/* HOST SAFETY: retail always writes through the caller's buffer. */
		return NULL;
	}
	buffer[0] = '\0';
	if (variable < 0 || variable >= BOT_MATCH_MAX_VARIABLES)
	{
		BotLib_Print(PRT_FATAL, "BotMatchVariable: variable out of range\n");
		return buffer;
	}
	if (match == NULL)
	{
		/* HOST SAFETY: retail dereferences the supplied match. */
		return buffer;
	}

	const bot_matchvariable_t *span = &match->variables[variable];
	if (span->ptr == NULL || span->length <= 0)
	{
		return buffer;
	}

	memcpy(buffer, span->ptr, (size_t)span->length);
	buffer[span->length] = '\0';
	return buffer;
}

/*
=============
BotMatchVariableSized

Bounds-checked compatibility adapter around retail BotMatchVariable.
=============
*/
void BotMatchVariableSized(const bot_match_t *match,
	int variable,
	char *buffer,
	int buffer_size)
{
	if (buffer == NULL || buffer_size <= 0)
	{
		return;
	}
	buffer[0] = '\0';
	if (match == NULL)
	{
		return;
	}
	if (variable < 0 || variable >= BOT_MATCH_MAX_VARIABLES)
	{
		BotLib_Print(PRT_FATAL, "BotMatchVariable: variable out of range\n");
		return;
	}

	const bot_matchvariable_t *span = &match->variables[variable];
	if (span->ptr == NULL || span->length <= 0)
	{
		return;
	}

	size_t length = (size_t)span->length;
	if (length >= (size_t)buffer_size)
	{
		length = (size_t)buffer_size - 1U;
	}
	memcpy(buffer, span->ptr, length);
	buffer[length] = '\0';
}

/*
=============
BotChat_HasSynonymPhrase

Reports whether a parsed synonym context contains the requested phrase.
=============
*/
int BotChat_HasSynonymPhrase(const bot_chatstate_t *state, const char *context_name, const char *phrase)
{
    if (state == NULL || context_name == NULL || phrase == NULL) {
        return 0;
    }
	const unsigned long requested_context_mask =
		BotChat_SynonymContextMaskFromName(context_name);
	const bot_chatstate_t *states[2] = {
		state,
		BotChat_SetupFallbackState(state)
	};
	for (size_t state_index = 0; state_index < sizeof(states) / sizeof(states[0]); ++state_index) {
		const bot_chatstate_t *source = states[state_index];
		if (source == NULL) {
			continue;
		}
		for (size_t i = 0; i < source->synonym_context_count; ++i) {
			const bot_synonym_context_t *context = &source->synonym_contexts[i];
			if (context->context_name == NULL) {
				continue;
			}
			if (strcmp(context->context_name, context_name) != 0 &&
				(requested_context_mask == 0UL ||
					!BotChat_SynonymContextApplies(context,
						requested_context_mask))) {
				continue;
			}
			for (size_t j = 0; j < context->group_count; ++j) {
				const bot_synonym_group_t *group = &context->groups[j];
				for (size_t k = 0; k < group->phrase_count; ++k) {
					const bot_synonym_phrase_t *entry = &group->phrases[k];
					if (entry->text != NULL && strcmp(entry->text, phrase) == 0) {
						return 1;
					}
				}
			}
		}
	}

    return 0;
}

int BotChat_HasReplyTemplate(const bot_chatstate_t *state, unsigned long int context, const char *template_text)
{
    if (state == NULL || template_text == NULL) {
        return 0;
    }

	const bot_chatstate_t *states[2] = {
		state,
		BotChat_SetupFallbackState(state)
	};
	for (size_t state_index = 0; state_index < sizeof(states) / sizeof(states[0]); ++state_index) {
		const bot_chatstate_t *source = states[state_index];
		if (source == NULL) {
			continue;
		}

		const bot_match_context_t *match =
			BotChat_FindMatchContext((bot_chatstate_t *)source, context);
		if (match != NULL) {
			for (size_t i = 0; i < match->template_count; ++i) {
				const char *stored_template = match->templates[i];
				if (stored_template == NULL) {
					continue;
				}
				if (strcmp(stored_template, template_text) == 0) {
					return 1;
				}
				if (BotChat_TemplateContainsEscape(stored_template)
					&& BotChat_TemplateCanConstructText(source,
						context,
						stored_template,
						template_text)) {
					return 1;
				}
			}
		}

		for (size_t rule_index = 0; rule_index < source->replies.rule_count; ++rule_index) {
			const bot_reply_rule_t *rule = &source->replies.rules[rule_index];
			if (rule->reply_context != context) {
				continue;
			}
			for (size_t i = 0; i < rule->response_count; ++i) {
				const char *stored_template = rule->responses[i];
				if (stored_template == NULL) {
					continue;
				}
				if (strcmp(stored_template, template_text) == 0) {
					return 1;
				}
				if (BotChat_TemplateContainsEscape(stored_template)
					&& BotChat_TemplateCanConstructText(source,
						context,
						stored_template,
						template_text)) {
					return 1;
				}
			}
		}
	}

    return 0;
}
