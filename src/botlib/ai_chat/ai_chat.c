#include "ai_chat.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/interface/botlib_interface.h"
#include "q2bridge/bridge.h"

#define BOT_CHAT_MAX_CONSOLE_MESSAGES 16
#define BOT_CHAT_MAX_MESSAGE_CHARS 256
#define BOT_CHAT_MAX_MATCH_VARIABLES 11
#define BOT_CHAT_MAX_PATH_CHARS 1024
#define BOT_CHAT_MAX_TOKEN_CHARS 64
#define BOT_CHAT_MAX_TOKENS 64
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

typedef struct bot_console_message_s {
    int type;
    char text[BOT_CHAT_MAX_MESSAGE_CHARS];
} bot_console_message_t;

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
    bot_synonym_group_t *groups;
    size_t group_count;
    size_t group_capacity;
} bot_synonym_context_t;

typedef struct {
    unsigned long message_type;
    char **templates;
    size_t template_count;
    size_t template_capacity;
} bot_match_context_t;

typedef struct {
    unsigned long reply_context;
	struct bot_reply_key_s *keys;
	size_t key_count;
	size_t key_capacity;
    char **responses;
    size_t response_count;
    size_t response_capacity;
} bot_reply_rule_t;

typedef struct bot_reply_key_s {
	char *pattern;
	int is_pattern;
	int required;
	int negated;
} bot_reply_key_t;

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
	size_t template_count;
	size_t template_capacity;
} bot_initial_chat_type_t;

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

Prints the legacy chat diagnostic and optionally queues it for fastchat tests.
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

	BotLib_Print(priority, format, chatname, chatfile);
	if (!fastchat_enabled || state == NULL)
	{
		return;
	}

	char message[BOT_CHAT_MAX_MESSAGE_CHARS];
	int written = snprintf(message, sizeof(message), format, chatname, chatfile);
	if (written < 0)
	{
		return;
	}

	BotQueueConsoleMessage(state, priority, message);
}

struct bot_chatstate_s {
	pc_source_t *active_source;
	pc_script_t *active_script;
	char active_chatfile[128];
	char active_chatname[64];
	bot_console_message_t console_queue[BOT_CHAT_MAX_CONSOLE_MESSAGES];
	size_t console_head;
	size_t console_count;

	bot_synonym_context_t *synonym_contexts;
	size_t synonym_context_count;

	bot_match_context_t *match_contexts;
	size_t match_context_count;

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
};

static bot_chatstate_t *bot_chat_setup_state;

#define BOT_CHAT_MIN_INTERVAL_SECONDS 25.0

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

	#if defined(CLOCK_MONOTONIC)
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
{
	return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}
	#endif

	return (double)time(NULL);
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
		char buffer[BOT_CHAT_MAX_MESSAGE_CHARS];
		const double remaining = slot->next_allowed_time - now_seconds;
		snprintf(buffer,
			sizeof(buffer),
			"client %zu blocked by chat cooldown (%.2fs remaining)\n",
			client,
			remaining > 0.0 ? remaining : 0.0);
		BotQueueConsoleMessage(state, (int)client, buffer);
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
		BotQueueConsoleMessage(state, PRT_MESSAGE, message);
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
		BotQueueConsoleMessage(state, PRT_WARNING, buffer);
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

Queues a diagnostic when a cooldown prevents sending.
=============
*/
static void BotChat_ReportCooldown(bot_chatstate_t *state,
unsigned long context,
double seconds_remaining)
{
	if (state == NULL)
	{
		return;
	}

	if (seconds_remaining < 0.0)
	{
		seconds_remaining = 0.0;
	}

	char message[BOT_CHAT_MAX_MESSAGE_CHARS];
	snprintf(message,
	sizeof(message),
	"context %lu blocked by cooldown (%.2fs remaining)\n",
	context,
	seconds_remaining);
	BotQueueConsoleMessage(state, (int)context, message);
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

	for (size_t i = 0; i < state->synonym_context_count; ++i)
	{
		const bot_synonym_context_t *context = &state->synonym_contexts[i];
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
	static const char *kKnownRandomTables[] = {
	"random_misc",
	"random_insult"
	};
	if (name == NULL)
	{
	return 0;
	}

	if (BotChat_FindSynonymContextByToken(state, name) != NULL ||
		BotChat_FindStateRandomTable(state, name) != NULL)
	{
		return 1;
	}
	
	for (size_t i = 0; i < sizeof(kKnownRandomTables) / sizeof(kKnownRandomTables[0]); ++i)
	{
	if (strcmp(kKnownRandomTables[i], name) == 0)
	{
	return 1;
	}
	}
	return 0;
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
	if (state == NULL || name == NULL)
	{
		return NULL;
	}

	for (size_t i = 0; i < state->random_table_count; ++i)
	{
		const bot_random_string_table_t *table = &state->random_tables[i];
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
	if (table == NULL || table->entries == NULL || table->entry_count == 0)
	{
	return NULL;
	}
	
	size_t index = (size_t)(rand() % table->entry_count);
	return table->entries[index];
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
	if (table == NULL || table->entries == NULL || table->entry_count == 0)
	{
		return NULL;
	}

	size_t index = (size_t)(rand() % table->entry_count);
	return table->entries[index];
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

Expands a random string reference using synonym contexts or built-in tables.
=============
*/
static const char *BotChat_SelectRandomString(const bot_chatstate_t *state, const char *name)
	{
	const bot_synonym_context_t *context = BotChat_FindSynonymContextByToken(state, name);
	const char *selection = BotChat_SelectWeightedSynonym(context);
	if (selection != NULL)
	{
	return selection;
	}

	const bot_random_string_table_t *state_table = BotChat_FindStateRandomTable(state, name);
	selection = BotChat_SelectRandomFromStateTable(state_table);
	if (selection != NULL)
	{
		return selection;
	}

	const bot_chat_random_table_t *table = BotChat_FindBuiltinRandomTable(name);
	if (table != NULL)
	{
	return BotChat_SelectRandomFromTable(table);
	}

	return NULL;
	}

static void BotChat_ResetConsoleQueue(bot_chatstate_t *state)
{
    if (state == NULL) {
        return;
    }

    memset(state->console_queue, 0, sizeof(state->console_queue));
    state->console_head = 0;
    state->console_count = 0;
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

static int BotChat_StringEqualsIgnoreCase(const char *lhs, const char *rhs);

/*
=============
BotChat_InitialTypeNamesMatch

Compares raw Gladiator type names while accepting Quake III successor aliases.
=============
*/
static int BotChat_InitialTypeNamesMatch(const char *stored_name,
	const char *query_name)
{
	static const struct {
		const char *gladiator_name;
		const char *quake3_name;
	} aliases[] = {
		{ "enter_game", "game_enter" },
		{ "exit_game", "game_exit" },
		{ "start_level", "level_start" },
		{ "end_level", "level_end" },
	};

	if (stored_name == NULL || query_name == NULL)
	{
		return 0;
	}

	if (BotChat_StringEqualsIgnoreCase(stored_name, query_name))
	{
		return 1;
	}

	for (size_t i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i)
	{
		if ((BotChat_StringEqualsIgnoreCase(stored_name, aliases[i].gladiator_name)
				&& BotChat_StringEqualsIgnoreCase(query_name, aliases[i].quake3_name))
			|| (BotChat_StringEqualsIgnoreCase(stored_name, aliases[i].quake3_name)
				&& BotChat_StringEqualsIgnoreCase(query_name, aliases[i].gladiator_name)))
		{
			return 1;
		}
	}

	return 0;
}

/*
=============
BotChat_FindInitialType

Looks up a stored initial-chat bucket by exact name or successor alias.
=============
*/
static bot_initial_chat_type_t *BotChat_FindInitialType(bot_chatstate_t *state,
	const char *type_name)
{
	if (state == NULL || type_name == NULL)
	{
		return NULL;
	}

	for (size_t i = 0; i < state->initial_type_count; ++i)
	{
		bot_initial_chat_type_t *type = &state->initial_types[i];
		if (BotChat_InitialTypeNamesMatch(type->type_name, type_name))
		{
			return type;
		}
	}

	return NULL;
}

/*
=============
BotChat_GetOrCreateInitialType

Finds or allocates the raw type bucket used by BotNumInitialChats.
=============
*/
static bot_initial_chat_type_t *BotChat_GetOrCreateInitialType(bot_chatstate_t *state,
	const char *type_name)
{
	if (state == NULL || type_name == NULL)
	{
		return NULL;
	}

	bot_initial_chat_type_t *type = BotChat_FindInitialType(state, type_name);
	if (type != NULL)
	{
		return type;
	}

	bot_initial_chat_type_t *types = realloc(state->initial_types,
		(state->initial_type_count + 1) * sizeof(*types));
	if (types == NULL)
	{
		return NULL;
	}

	state->initial_types = types;
	type = &state->initial_types[state->initial_type_count++];
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

	bot_initial_chat_type_t *type = BotChat_GetOrCreateInitialType(state, type_name);
	if (type == NULL)
	{
		return 0;
	}

	if (type->template_count == type->template_capacity)
	{
		size_t capacity = type->template_capacity ? type->template_capacity * 2U : 4U;
		char **templates = realloc(type->templates, capacity * sizeof(*templates));
		if (templates == NULL)
		{
			return 0;
		}
		type->templates = templates;
		type->template_capacity = capacity;
	}

	type->templates[type->template_count] = BotChat_StringDuplicate(template_text);
	if (type->templates[type->template_count] == NULL)
	{
		return 0;
	}
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
	}

	free(state->initial_types);
	state->initial_types = NULL;
	state->initial_type_count = 0;
}

/*
=============
BotChat_ChooseInitialTemplate

Selects a stored initial-chat message using the same deterministic helper as
context template dispatch.
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

	size_t index = BotChat_SelectIndex(type_name, type->template_count);
	return type->templates[index];
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

Creates or reuses a parsed random string table.
=============
*/
static bot_random_string_table_t *BotChat_AddRandomTable(
	bot_chatstate_t *state,
	const char *name)
{
	bot_random_string_table_t *table = BotChat_FindMutableRandomTable(state, name);
	if (table != NULL)
	{
		return table;
	}

	bot_random_string_table_t *tables = realloc(state->random_tables,
		(state->random_table_count + 1) * sizeof(*tables));
	if (tables == NULL)
	{
		return NULL;
	}

	state->random_tables = tables;
	table = &state->random_tables[state->random_table_count++];
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
    if (state->match_contexts == NULL) {
        return;
    }

    for (size_t i = 0; i < state->match_context_count; ++i) {
        bot_match_context_t *context = &state->match_contexts[i];
        for (size_t j = 0; j < context->template_count; ++j) {
            free(context->templates[j]);
        }
        free(context->templates);
    }

    free(state->match_contexts);
    state->match_contexts = NULL;
    state->match_context_count = 0;
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
}

/*
=============
BotChat_AddSynonymContext

Creates or reuses a synonym context by canonical context name.
=============
*/
static bot_synonym_context_t *BotChat_AddSynonymContext(bot_chatstate_t *state, const char *name)
{
	if (state == NULL || name == NULL)
	{
		return NULL;
	}

	for (size_t i = 0; i < state->synonym_context_count; ++i)
	{
		bot_synonym_context_t *context = &state->synonym_contexts[i];
		if (context->context_name != NULL && strcmp(context->context_name, name) == 0)
		{
			return context;
		}
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

static char **BotChat_AddTemplate(bot_match_context_t *context)
{
    char **templates = realloc(context->templates, (context->template_count + 1) * sizeof(*templates));
    if (templates == NULL) {
        return NULL;
    }

    context->templates = templates;
    context->templates[context->template_count] = NULL;
    return &context->templates[context->template_count++];
}

static bot_match_context_t *BotChat_FindMatchContext(bot_chatstate_t *state, unsigned long message_type);

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

	char **slot = BotChat_AddTemplate(match_context);
	if (slot == NULL)
	{
		return 0;
	}

	*slot = BotChat_StringDuplicate(template_text);
	return *slot != NULL;
}

static bot_reply_rule_t *BotChat_AddReplyRule(bot_chatstate_t *state, unsigned long reply_context)
{
    bot_reply_rule_t *rules = realloc(state->replies.rules,
                                      (state->replies.rule_count + 1) * sizeof(*rules));
    if (rules == NULL) {
        return NULL;
    }

    state->replies.rules = rules;
    bot_reply_rule_t *rule = &state->replies.rules[state->replies.rule_count++];
    memset(rule, 0, sizeof(*rule));
    rule->reply_context = reply_context;
    return rule;
}

static char **BotChat_AddReply(bot_reply_rule_t *rule)
{
    char **responses = realloc(rule->responses, (rule->response_count + 1) * sizeof(*responses));
    if (responses == NULL) {
        return NULL;
    }

    rule->responses = responses;
    rule->responses[rule->response_count] = NULL;
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
BotChat_MatchTemplateNeedsSpace

Returns true when adjacent match pieces need an explicit word boundary.
=============
*/
static int BotChat_MatchTemplateNeedsSpace(const bot_string_builder_t *builder,
	char next_character)
{
	if (builder == NULL || builder->buffer == NULL || builder->length == 0
		|| next_character == '\0')
	{
		return 0;
	}

	const unsigned char previous =
		(unsigned char)builder->buffer[builder->length - 1];
	const unsigned char next = (unsigned char)next_character;
	if (isspace(previous) || isspace(next))
	{
		return 0;
	}
	if (next == '\'' || next == ')' || next == ',' || next == '.'
		|| next == '?' || next == '!' || next == ':' || next == ';')
	{
		return 0;
	}
	if (previous == '(' || previous == '[' || previous == '{')
	{
		return 0;
	}

	return (isalnum(previous) || previous == '}' || previous == '\\'
			|| previous == ')' || previous == ']')
		&& (isalnum(next) || next == '{' || next == '(' || next == '\\');
}

/*
=============
BotChat_StringBuilderAppendMatchSeparator

Inserts the display space implied by comma-separated match pieces.
=============
*/
static int BotChat_StringBuilderAppendMatchSeparator(bot_string_builder_t *builder,
	char next_character)
{
	if (!BotChat_MatchTemplateNeedsSpace(builder, next_character))
	{
		return 1;
	}
	return BotChat_StringBuilderAppendChar(builder, ' ');
}

/*
=============
BotChat_StringBuilderAppendMatchTokenText

Appends match-piece text with retail token-boundary spacing.
=============
*/
static int BotChat_StringBuilderAppendMatchTokenText(bot_string_builder_t *builder,
	const pc_token_t *token)
{
	size_t length = 0;
	const char *text = BotChat_TokenText(token, &length);
	if (length == 0)
	{
		return 1;
	}
	return BotChat_StringBuilderAppendMatchSeparator(builder, text[0])
		&& BotChat_StringBuilderAppendSpan(builder, text, length);
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
BotChat_StringBuilderAppendRandomReference

Stores a preprocessor-expanded random table reference for construction time.
=============
*/
static int BotChat_StringBuilderAppendRandomReference(bot_string_builder_t *builder,
	const char *identifier)
{
	return BotChat_StringBuilderAppend(builder, "\\r")
		&& BotChat_StringBuilderAppend(builder, identifier)
		&& BotChat_StringBuilderAppendChar(builder, '\\');
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
	int written = snprintf(buffer, sizeof(buffer), "\\v%lu\\", value);
	if (written <= 0 || (size_t)written >= sizeof(buffer))
	{
		return 0;
	}

	return BotChat_StringBuilderAppend(builder, buffer);
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
		return BotChat_NumberTokenValue(token);
	}

	if (token->type == TT_NAME)
	{
		return BotChat_MessageTypeFromIdentifier(token->string, strlen(token->string));
	}

	return 0;
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

Converts temporary \vN\ markers into readable placeholders once MSG_* is known.
=============
*/
static char *BotChat_RewriteVariablesForMessageType(const char *template_text,
	unsigned long message_type)
{
	bot_string_builder_t builder = {0};

	for (size_t i = 0; template_text != NULL && template_text[i] != '\0';)
	{
		if (template_text[i] != '\\' || template_text[i + 1] != 'v')
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

		if (!saw_digit || template_text[i] != '\\')
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
	if (strcmp(name, "TIME") == 0 || strcmp(name, "NAME") == 0)
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
	if (strcmp(name, "MORE") == 0)
	{
		*value = 10;
		return 1;
	}

	return 0;
}

/*
=============
BotChat_AppendEscapedVariable

Appends an internal \vN\ capture marker to a pattern builder.
=============
*/
static int BotChat_AppendEscapedVariable(bot_string_builder_t *builder,
	unsigned long value)
{
	char buffer[32];
	int written = snprintf(buffer, sizeof(buffer), "\\v%lu\\", value);
	if (written <= 0 || (size_t)written >= sizeof(buffer))
	{
		return 0;
	}
	return BotChat_StringBuilderAppend(builder, buffer);
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
		if (pattern[pattern_offset] == '\\' && pattern[pattern_offset + 1] == 'v')
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
			if (!saw_digit || pattern[value_offset] != '\\'
				|| value >= BOT_CHAT_MAX_MATCH_VARIABLES)
			{
				return 0;
			}

			last_variable = (long)value;
			pattern_offset = value_offset + 1;
			continue;
		}

		const size_t literal_start = pattern_offset;
		while (pattern[pattern_offset] != '\0'
			&& !(pattern[pattern_offset] == '\\'
				&& pattern[pattern_offset + 1] == 'v'))
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
BotChat_WordChar

Returns true for word characters used by reply key word matching.
=============
*/
static int BotChat_WordChar(char character)
{
	return isalnum((unsigned char)character) || character == '_';
}

/*
=============
BotChat_StringContainsWordCaseInsensitive

Checks whether a reply key string occurs as a word, falling back to substring
matching for punctuation-only keys.
=============
*/
static int BotChat_StringContainsWordCaseInsensitive(const char *text,
	const char *needle)
{
	if (text == NULL || needle == NULL)
	{
		return 0;
	}
	if (needle[0] == '\0')
	{
		return 1;
	}

	int has_word_char = 0;
	for (size_t i = 0; needle[i] != '\0'; ++i)
	{
		if (BotChat_WordChar(needle[i]))
		{
			has_word_char = 1;
			break;
		}
	}

	size_t start = 0;
	size_t match_start = 0;
	size_t match_end = 0;
	const size_t needle_length = strlen(needle);
	while (BotChat_FindCaseInsensitiveSpan(text,
		start,
		needle,
		needle_length,
		&match_start,
		&match_end))
	{
		if (!has_word_char
			|| ((match_start == 0 || !BotChat_WordChar(text[match_start - 1]))
				&& !BotChat_WordChar(text[match_end])))
		{
			return 1;
		}
		start = match_start + 1;
	}

	return 0;
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

	const unsigned long context_mask =
		BotChat_SynonymContextMaskFromName(context->context_name);
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

	double total_weight = 0.0;
	const bot_synonym_phrase_t *fallback = NULL;
	for (size_t i = 0; i < group->phrase_count; ++i)
	{
		const bot_synonym_phrase_t *phrase = &group->phrases[i];
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
		fallback = phrase;
	}

	if (total_weight <= 0.0)
	{
		return NULL;
	}

	double roll = ((double)rand() / ((double)RAND_MAX + 1.0)) * total_weight;
	for (size_t i = 0; i < group->phrase_count; ++i)
	{
		const bot_synonym_phrase_t *phrase = &group->phrases[i];
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
			return phrase;
		}
		roll -= weight;
	}

	return fallback;
}

/*
=============
BotChat_SpanIsWordMatch

Checks retail-style word boundaries around a candidate replacement span.
=============
*/
static int BotChat_SpanIsWordMatch(const char *text, size_t start, size_t end)
{
	if (text == NULL || start > end)
	{
		return 0;
	}

	if (start > 0 && BotChat_WordChar(text[start - 1]))
	{
		return 0;
	}
	if (BotChat_WordChar(text[end]))
	{
		return 0;
	}
	return 1;
}

/*
=============
BotChat_SpanInsideReplacement

Prevents a synonym pass from rewriting text that is already part of the chosen
replacement phrase.
=============
*/
static int BotChat_SpanInsideReplacement(const char *text,
	size_t match_start,
	const char *replacement)
{
	if (text == NULL || replacement == NULL || replacement[0] == '\0')
	{
		return 0;
	}

	size_t start = 0;
	const size_t replacement_length = strlen(replacement);
	size_t replacement_start = 0;
	size_t replacement_end = 0;
	while (BotChat_FindCaseInsensitiveSpan(text,
		start,
		replacement,
		replacement_length,
		&replacement_start,
		&replacement_end))
	{
		if (BotChat_SpanIsWordMatch(text, replacement_start, replacement_end)
			&& replacement_start <= match_start
			&& match_start < replacement_end)
		{
			return 1;
		}
		start = replacement_start + 1U;
	}

	return 0;
}

/*
=============
BotChat_ReplaceSynonymWord

Replaces each word-bound occurrence of one synonym phrase with the selected
replacement phrase.
=============
*/
static int BotChat_ReplaceSynonymWord(char *message,
	size_t message_size,
	const char *synonym,
	const char *replacement,
	const char *source_template)
{
	if (message == NULL || synonym == NULL || replacement == NULL
		|| synonym[0] == '\0')
	{
		return 1;
	}
	if (BotChat_StringEqualsIgnoreCase(synonym, replacement))
	{
		return 1;
	}

	const size_t synonym_length = strlen(synonym);
	const size_t replacement_length = strlen(replacement);
	size_t start = 0;
	size_t match_start = 0;
	size_t match_end = 0;
	while (BotChat_FindCaseInsensitiveSpan(message,
		start,
		synonym,
		synonym_length,
		&match_start,
		&match_end))
	{
		if (!BotChat_SpanIsWordMatch(message, match_start, match_end)
			|| BotChat_SpanInsideReplacement(message, match_start, replacement))
		{
			start = match_start + 1U;
			continue;
		}

		const size_t current_length = strlen(message);
		const size_t new_length =
			current_length - synonym_length + replacement_length;
		if (new_length >= message_size || new_length >= BOT_CHAT_MAX_MESSAGE_CHARS)
		{
			BotLib_Print(PRT_ERROR,
				"BotConstructChat: message \"%s\" too long\n",
				source_template != NULL ? source_template : message);
			return 0;
		}

		memmove(message + match_start + replacement_length,
			message + match_end,
			current_length - match_end + 1U);
		memcpy(message + match_start, replacement, replacement_length);
		start = match_start + replacement_length;
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
	if (state == NULL || message == NULL || message_context == 0UL)
	{
		return 1;
	}

	for (size_t context_index = 0;
		context_index < state->synonym_context_count;
		++context_index)
	{
		const bot_synonym_context_t *context =
			&state->synonym_contexts[context_index];
		if (!BotChat_SynonymContextApplies(context, message_context))
		{
			continue;
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
				if (!BotChat_ReplaceSynonymWord(message,
					message_size,
					phrase->text,
					replacement->text,
					source_template))
				{
					return 0;
				}
			}
		}
	}

	return 1;
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
    if (count == 0) {
        return 0;
    }
    unsigned long hash = 5381;
    if (seed != NULL) {
        for (const unsigned char *ptr = (const unsigned char *)seed; *ptr != '\0'; ++ptr) {
            hash = ((hash << 5) + hash) + *ptr;
        }
    }
    return (size_t)(hash % count);
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
		if (PS_CheckTokenString(script, "]"))
		{
			return 1;
		}
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
		if (phrase_text == NULL)
		{
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

Walks a script once to collect CONTEXT_* synonym blocks.
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

	ResetScript(script);
	pc_token_t token;
	while (PS_ReadToken(script, &token))
	{
		char context_name[BOT_CHAT_MAX_TOKEN_CHARS];
		if (token.type == TT_NUMBER && !allow_numeric_contexts)
		{
			continue;
		}
		if (!BotChat_SynonymContextNameFromToken(&token,
				context_name,
				sizeof(context_name)))
		{
			continue;
		}
		if (!PS_ExpectTokenString(script, "{"))
		{
			return 0;
		}
		bot_synonym_context_t *context = BotChat_AddSynonymContext(state, context_name);
		if (context == NULL)
		{
			return 0;
		}
		while (1)
		{
			if (PS_CheckTokenString(script, "}"))
			{
				break;
			}
			if (!PS_ReadToken(script, &token))
			{
				return 0;
			}
			if (token.type == TT_PUNCTUATION && token.string[0] == '[')
			{
				if (!BotChat_ParseSynonymGroup(context, script))
				{
					return 0;
				}
			}
		}
	}
	return 1;
}

/*
=============
BotChat_TrimBuilderWhitespace

Removes trailing spaces from the builder contents.
=============
*/
static void BotChat_TrimBuilderWhitespace(bot_string_builder_t *builder)
{
	while (builder->length > 0 && builder->buffer[builder->length - 1] == ' ')
	{
		builder->buffer[--builder->length] = '\0';
	}
}

/*
=============
BotChat_ParseMatchTemplate

Extracts the template left-hand side and registers it under the message type.
=============
*/
static int BotChat_ParseMatchTemplate(bot_chatstate_t *state, pc_script_t *script)
{
	bot_string_builder_t builder = {0};
	pc_token_t token;
	while (PS_ReadToken(script, &token))
	{
		if (token.type == TT_PUNCTUATION && token.string[0] == '=')
		{
			break;
		}
		if (token.type == TT_PUNCTUATION && token.string[0] == ',')
		{
			continue;
		}
		if (token.type == TT_STRING)
		{
			if (!BotChat_StringBuilderAppendMatchTokenText(&builder, &token))
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			continue;
		}
		if (token.type == TT_NAME)
		{
			const char next_character =
				BotChat_IsKnownPlaceholderName(token.string) ? '{' : token.string[0];
			if (!BotChat_StringBuilderAppendMatchSeparator(&builder, next_character))
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			if (!BotChat_StringBuilderAppendNameToken(&builder, &token))
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			continue;
		}
		if (token.type == TT_NUMBER)
		{
			if (!BotChat_StringBuilderAppendMatchSeparator(&builder, '{'))
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			if (!BotChat_StringBuilderAppendVariableReference(&builder,
					BotChat_NumberTokenValue(&token)))
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			continue;
		}
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
	if (!PS_ReadToken(script, &type_token))
	{
		BotChat_StringBuilderDestroy(&builder);
		return 0;
	}
	unsigned long message_type = BotChat_MessageTypeFromToken(&type_token);
	if (message_type == 0)
	{
		BotChat_StringBuilderDestroy(&builder);
		return 0;
	}
	while (PS_ReadToken(script, &type_token))
	{
		if (type_token.type == TT_PUNCTUATION && type_token.string[0] == ';')
		{
			break;
		}
	}
	if (type_token.type != TT_PUNCTUATION || type_token.string[0] != ';')
	{
		BotChat_StringBuilderDestroy(&builder);
		return 0;
	}
	if (builder.buffer == NULL || builder.length == 0)
	{
		BotChat_StringBuilderDestroy(&builder);
		return 1;
	}
	BotChat_TrimBuilderWhitespace(&builder);
	char *template_text = BotChat_StringBuilderDetach(&builder);
	BotChat_StringBuilderDestroy(&builder);
	if (template_text == NULL)
	{
		return 0;
	}
	char *mapped_template = BotChat_RewriteVariablesForMessageType(template_text, message_type);
	free(template_text);
	if (mapped_template == NULL)
	{
		return 0;
	}
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
	char **slot = BotChat_AddTemplate(context);
	if (slot == NULL)
	{
		free(mapped_template);
		return 0;
	}
	*slot = mapped_template;
	return 1;
}

/*
=============
BotChat_ParseMatchBlock

Iterates over the statements inside an MTCONTEXT_* block.
=============
*/
static int BotChat_ParseMatchBlock(bot_chatstate_t *state, pc_script_t *script)
{
	pc_token_t token;
	while (PS_ReadToken(script, &token))
	{
		if (token.type == TT_PUNCTUATION && token.string[0] == '}')
		{
			return 1;
		}
		PS_UnreadToken(script, &token);
		if (!BotChat_ParseMatchTemplate(state, script))
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
BotChat_ParseReplyTemplate

Builds a single reply text entry from the token stream.
=============
*/
static int BotChat_ParseReplyTemplate(bot_chatstate_t *state, bot_reply_rule_t *rule, pc_script_t *script)
{
	bot_string_builder_t builder = {0};
	pc_token_t token;
	while (PS_ReadToken(script, &token))
	{
		if (token.type == TT_PUNCTUATION)
		{
			if (token.string[0] == ';')
			{
				break;
			}
			if (token.string[0] == ',')
			{
				continue;
			}
		}
		if (token.type == TT_STRING)
		{
			if (!BotChat_StringBuilderAppendTokenText(&builder, &token))
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			continue;
		}
		if (token.type == TT_NAME)
		{
			int appended = 0;
			if (BotChat_IsKnownPlaceholderName(token.string))
			{
				appended = BotChat_StringBuilderAppendNameToken(&builder, &token);
			}
			else
			{
				appended = BotChat_StringBuilderAppendRandomReference(&builder, token.string);
			}
			if (!appended)
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			continue;
		}
		if (token.type == TT_NUMBER)
		{
			if (!BotChat_StringBuilderAppendVariableReference(&builder, token.intvalue))
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			continue;
		}
	}
	if (token.type != TT_PUNCTUATION || token.string[0] != ';')
	{
		BotChat_StringBuilderDestroy(&builder);
		return 0;
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

	while (PS_ReadToken(script, &token))
	{
		if (token.type == TT_PUNCTUATION && token.string[0] == ')')
		{
			return BotChat_StringBuilderDetach(&builder);
		}
		if (token.type == TT_PUNCTUATION && token.string[0] == ',')
		{
			continue;
		}
		if (token.type == TT_STRING)
		{
			if (!BotChat_StringBuilderAppendTokenText(&builder, &token))
			{
				BotChat_StringBuilderDestroy(&builder);
				return NULL;
			}
			continue;
		}
		if (token.type == TT_NUMBER)
		{
			if (!BotChat_StringBuilderAppendVariableReference(&builder,
					BotChat_NumberTokenValue(&token)))
			{
				BotChat_StringBuilderDestroy(&builder);
				return NULL;
			}
			continue;
		}
		if (token.type == TT_NAME)
		{
			if (!BotChat_StringBuilderAppendTokenText(&builder, &token))
			{
				BotChat_StringBuilderDestroy(&builder);
				return NULL;
			}
			continue;
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
static char *BotChat_ParseReplyKeyText(pc_script_t *script, int *is_pattern)
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

	bot_string_builder_t builder = {0};
	if (token.type == TT_STRING || token.type == TT_NAME)
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

	if (token.type == TT_NUMBER)
	{
		if (!BotChat_StringBuilderAppendVariableReference(&builder,
				BotChat_NumberTokenValue(&token)))
		{
			BotChat_StringBuilderDestroy(&builder);
			return NULL;
		}
		if (is_pattern != NULL)
		{
			*is_pattern = 1;
		}
		return BotChat_StringBuilderDetach(&builder);
	}

	BotChat_StringBuilderDestroy(&builder);
	return NULL;
}

/*
=============
BotChat_ParseReplyKeys

Parses the key list inside a reply chat's square brackets.
=============
*/
static int BotChat_ParseReplyKeys(pc_script_t *script,
	bot_reply_key_list_t *keys)
{
	if (script == NULL || keys == NULL)
	{
		return 0;
	}

	while (1)
	{
		if (PS_CheckTokenString(script, "]"))
		{
			return 1;
		}

		int negated = 0;
		int required = 0;
		if (PS_CheckTokenString(script, "!"))
		{
			negated = 1;
		}
		else if (PS_CheckTokenString(script, "&"))
		{
			required = 1;
		}

		int is_pattern = 0;
		char *pattern = BotChat_ParseReplyKeyText(script, &is_pattern);
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
		key->required = required;
		key->negated = negated;

		if (PS_CheckTokenString(script, "]"))
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
	pc_token_t token;

	while (PS_ReadToken(script, &token))
	{
		if (token.type == TT_PUNCTUATION)
		{
			if (token.string[0] == '}')
			{
				return 1;
			}
			if (token.string[0] == ',')
			{
				continue;
			}
			return 0;
		}

		if (token.type == TT_STRING || token.type == TT_NUMBER)
		{
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
			continue;
		}

		if (token.type == TT_NAME)
		{
			const bot_random_string_table_t *source =
				BotChat_FindStateRandomTable(state, token.string);
			if (source != NULL)
			{
				if (!BotChat_CopyRandomEntries(table, source))
				{
					return 0;
				}
				continue;
			}
			if (!BotChat_AddRandomEntry(table, token.string))
			{
				return 0;
			}
			continue;
		}
	}

	return 0;
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
			continue;
		}

		pc_token_t equals_token;
		if (!PS_ReadToken(script, &equals_token))
		{
			return 1;
		}
		if (equals_token.type != TT_PUNCTUATION || equals_token.string[0] != '=')
		{
			PS_UnreadToken(script, &equals_token);
			continue;
		}

		pc_token_t open_token;
		if (!PS_ReadToken(script, &open_token))
		{
			return 0;
		}
		if (open_token.type != TT_PUNCTUATION || open_token.string[0] != '{')
		{
			continue;
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

	while (PS_ReadToken(script, &token))
	{
		if (token.type == TT_PUNCTUATION)
		{
			if (token.string[0] == ';')
			{
				return BotChat_StringBuilderDetach(&builder);
			}
			if (token.string[0] == ',')
			{
				continue;
			}
		}

		if (token.type == TT_STRING)
		{
			if (!BotChat_StringBuilderAppendTokenText(&builder, &token))
			{
				BotChat_StringBuilderDestroy(&builder);
				return NULL;
			}
			continue;
		}

		if (token.type == TT_NAME)
		{
			int appended = 0;
			if (BotChat_IsKnownPlaceholderName(token.string))
			{
				appended = BotChat_StringBuilderAppendNameToken(&builder, &token);
			}
			else
			{
				appended = BotChat_StringBuilderAppendRandomReference(&builder, token.string);
			}
			if (!appended)
			{
				BotChat_StringBuilderDestroy(&builder);
				return NULL;
			}
			continue;
		}

		if (token.type == TT_NUMBER)
		{
			if (!BotChat_StringBuilderAppendVariableReference(&builder, token.intvalue))
			{
				BotChat_StringBuilderDestroy(&builder);
				return NULL;
			}
			continue;
		}
	}

	BotChat_StringBuilderDestroy(&builder);
	return NULL;
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

		const int added = BotChat_AddInitialTypeTemplate(state, type_name, template_text)
			&& BotChat_AddInitialTemplate(state, type_name, template_text);
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
		if (token.type != TT_NAME || !BotChat_StringEqualsIgnoreCase(token.string, "type"))
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
		const int parsed = BotChat_ParseInitialTypeBlock(state, script, type_name);
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

Finds and parses retail chat "name" blocks, mirroring the HLIL loader's
chatname gate while allowing match/reply-only files to load.
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
		if (token.type != TT_NAME || !BotChat_StringEqualsIgnoreCase(token.string, "chat"))
		{
			continue;
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

		if (!BotChat_StringEqualsIgnoreCase(block_name, chatname))
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
	int previous_was_assign = 0;
	while (PS_ReadToken(script, &token))
	{
		if (token.type == TT_PUNCTUATION && token.string[0] == '=')
		{
			previous_was_assign = 1;
			continue;
		}
		if (token.type == TT_NAME && strncmp(token.string, "MTCONTEXT_", 10) == 0)
		{
			if (!PS_ExpectTokenString(script, "{"))
			{
				return 0;
			}
			if (!BotChat_ParseMatchBlock(state, script))
			{
				return 0;
			}
			previous_was_assign = 0;
			continue;
		}
		if (!previous_was_assign
			&& token.type == TT_NUMBER
			&& BotChat_IsKnownMatchContextNumber(BotChat_NumberTokenValue(&token)))
		{
			if (!PS_CheckTokenString(script, "{"))
			{
				previous_was_assign = 0;
				continue;
			}
			if (!BotChat_ParseMatchBlock(state, script))
			{
				return 0;
			}
			previous_was_assign = 0;
			continue;
		}
		if (token.type == TT_PUNCTUATION && token.string[0] == '[')
		{
			if (!BotChat_SkipBalancedBlock(script, '[', ']'))
			{
				return 0;
			}
		}
		previous_was_assign = 0;
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
		if (token.type == TT_NAME && strncmp(token.string, "MTCONTEXT_", 10) == 0)
		{
			if (!PS_ExpectTokenString(script, "{")
				|| !BotChat_SkipBalancedBlock(script, '{', '}'))
			{
				return 0;
			}
			continue;
		}
		if (token.type == TT_NUMBER
			&& BotChat_IsKnownMatchContextNumber(BotChat_NumberTokenValue(&token)))
		{
			pc_token_t open_token;
			if (!PS_ReadToken(script, &open_token))
			{
				return 0;
			}
			if (open_token.type != TT_PUNCTUATION || open_token.string[0] != '{')
			{
				PS_UnreadToken(script, &open_token);
				continue;
			}
			if (!BotChat_SkipBalancedBlock(script, '{', '}'))
			{
				return 0;
			}
			continue;
		}
		char context_name[BOT_CHAT_MAX_TOKEN_CHARS];
		if (BotChat_SynonymContextNameFromToken(&token,
				context_name,
				sizeof(context_name)))
		{
			pc_token_t open_token;
			if (!PS_ReadToken(script, &open_token))
			{
				return 0;
			}
			if (open_token.type != TT_PUNCTUATION || open_token.string[0] != '{')
			{
				PS_UnreadToken(script, &open_token);
				continue;
			}
			if (!BotChat_SkipBalancedBlock(script, '{', '}'))
			{
				return 0;
			}
			continue;
		}
		if (token.type == TT_PUNCTUATION && token.string[0] == '[')
		{
			if (!BotChat_ParseReplyBlock(state, script))
			{
				return 0;
			}
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
	return BotChat_ParseSynonymContextsFromScript(state, script, 1);
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
	return BotChat_ParseRandomStringTables(state, script);
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
	return BotChat_ParseMatchScript(state, script);
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
	return BotChat_ParseReplyScript(state, script);
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
	if (state == NULL || file_name == NULL || file_name[0] == '\0'
		|| parser == NULL)
	{
		return 0;
	}

	pc_source_t *source = PC_LoadSourceFile(file_name);
	if (source == NULL)
	{
		BotLib_Print(PRT_WARNING,
			"BotSetupChatAI: couldn't load %s %s\n",
			asset_label != NULL ? asset_label : "asset",
			file_name);
		return 0;
	}

	pc_script_t *script = PS_CreateScriptFromSource(source);
	if (script == NULL)
	{
		BotLib_Print(PRT_WARNING,
			"BotSetupChatAI: couldn't create script for %s %s\n",
			asset_label != NULL ? asset_label : "asset",
			file_name);
		PC_FreeSource(source);
		return 0;
	}

	const int parsed = parser(state, script);
	if (!parsed)
	{
		BotLib_Print(PRT_WARNING,
			"BotSetupChatAI: couldn't parse %s %s\n",
			asset_label != NULL ? asset_label : "asset",
			file_name);
	}

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
		BotFreeChatState(bot_chat_setup_state);
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
    return state;
}

/*
=============
BotFreeChatState

Releases a chat state and all parsed resources it owns.
=============
*/
void BotFreeChatState(bot_chatstate_t *state)
{
if (state == NULL) {
return;
}

    BotFreeChatFile(state);
BotChat_FreeSynonymContexts(state);
BotChat_FreeMatchContexts(state);
BotChat_FreeReplies(state);
free(state->cooldowns);
state->cooldowns = NULL;
state->cooldown_count = 0;
state->cooldown_capacity = 0;
	free(state->client_cooldowns);
	state->client_cooldowns = NULL;
state->client_cooldown_count = 0;
FreeMemory(state);
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
	BotChat_ClearSetupState();

	bot_chat_setup_state = BotAllocChatState();
	if (bot_chat_setup_state == NULL)
	{
		return BLERR_INVALIDIMPORT;
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
	BotChat_ClearSetupState();
}

/*
=============
BotLoadChatFile

Loads the requested chat assets and surfaces legacy diagnostics when failures
occur.
=============
*/
int BotLoadChatFile(bot_chatstate_t *state, const char *chatfile, const char *chatname)
{
	if (state == NULL || chatfile == NULL || chatname == NULL)
	{
		return 0;
	}

	const int fastchat_enabled = LibVarValue("fastchat", "0") != 0.0f;
	if (LibVarValue("nochat", "0") != 0.0f)
	{
		BotChat_PrintLegacyDiagnostic(state,
				PRT_FATAL,
				fastchat_enabled,
				"couldn't load chat %s from %s\n",
				chatname,
				chatfile);
		return 0;
	}

	BotFreeChatFile(state);

	int open_status = BotChat_OpenActiveScript(state, chatfile);
	if (open_status == 0)
	{
		BotChat_PrintLegacyDiagnostic(state,
				PRT_FATAL,
				fastchat_enabled,
				"couldn't load chat %s from %s\n",
				chatname,
				chatfile);
		return 0;
	}
	if (open_status < 0)
	{
		BotLib_Print(PRT_ERROR, "BotLoadChatFile: script wrapper failed for %s\n", chatfile);
		BotChat_PrintLegacyDiagnostic(state,
				PRT_ERROR,
				fastchat_enabled,
				"couldn't find chat %s in %s\n",
				chatname,
				chatfile);
		return 0;
	}

	if (!BotChat_LoadSiblingRandomStrings(state, chatfile)
		|| !BotChat_LoadSiblingSynonyms(state, chatfile)
		|| !BotChat_LoadSiblingMatchTemplates(state, chatfile)
		|| !BotChat_ParseRandomStringTables(state, state->active_script))
	{
		BotChat_PrintLegacyDiagnostic(state,
				PRT_ERROR,
				fastchat_enabled,
				"couldn't load chat %s from %s\n",
				chatname,
				chatfile);
		BotFreeChatFile(state);
		return 0;
	}

	int saw_chat_block = 0;
	int found_chat_block = 0;
	if (!BotChat_ParseInitialChat(state, chatname, &saw_chat_block, &found_chat_block)
		|| (saw_chat_block && !found_chat_block))
	{
		BotChat_PrintLegacyDiagnostic(state,
				PRT_ERROR,
				fastchat_enabled,
				"couldn't find chat %s in %s\n",
				chatname,
				chatfile);
		BotFreeChatFile(state);
		return 0;
	}
	BotChat_CloseActiveScript(state);

	open_status = BotChat_OpenActiveScript(state, chatfile);
	if (open_status <= 0 || !BotChat_ParseSynonymContexts(state))
	{
		if (open_status < 0)
		{
			BotLib_Print(PRT_ERROR, "BotLoadChatFile: script wrapper failed for %s\n", chatfile);
		}
		BotChat_PrintLegacyDiagnostic(state,
				PRT_ERROR,
				fastchat_enabled,
				"couldn't find chat %s in %s\n",
				chatname,
				chatfile);
		BotFreeChatFile(state);
		return 0;
	}
	BotChat_CloseActiveScript(state);

	open_status = BotChat_OpenActiveScript(state, chatfile);
	if (open_status <= 0 || !BotChat_ParseMatchPass(state))
	{
		if (open_status < 0)
		{
			BotLib_Print(PRT_ERROR, "BotLoadChatFile: script wrapper failed for %s\n", chatfile);
		}
		BotChat_PrintLegacyDiagnostic(state,
				PRT_ERROR,
				fastchat_enabled,
				"couldn't find chat %s in %s\n",
				chatname,
				chatfile);
		BotFreeChatFile(state);
		return 0;
	}
	BotChat_CloseActiveScript(state);

	open_status = BotChat_OpenActiveScript(state, chatfile);
	if (open_status <= 0)
	{
		if (open_status < 0)
		{
			BotLib_Print(PRT_ERROR, "BotLoadChatFile: script wrapper failed for %s\n", chatfile);
		}
		BotChat_PrintLegacyDiagnostic(state,
				PRT_ERROR,
				fastchat_enabled,
				"couldn't load chat %s from %s\n",
				chatname,
				chatfile);
		BotFreeChatFile(state);
		return 0;
	}
	if (!BotChat_ParseReplyChats(state))
	{
		BotChat_PrintLegacyDiagnostic(state,
				PRT_ERROR,
				fastchat_enabled,
				"couldn't load chat %s from %s\n",
				chatname,
				chatfile);
		BotFreeChatFile(state);
		return 0;
	}
	BotChat_CloseActiveScript(state);

	strncpy(state->active_chatfile, chatfile, sizeof(state->active_chatfile) - 1);
	state->active_chatfile[sizeof(state->active_chatfile) - 1] = '\0';
	strncpy(state->active_chatname, chatname, sizeof(state->active_chatname) - 1);
	state->active_chatname[sizeof(state->active_chatname) - 1] = '\0';

	if (!state->has_reply_chats)
	{
		BotLib_Print(PRT_MESSAGE, "no rchats\n");
	}

	BotLib_Print(PRT_MESSAGE,
			"BotLoadChatFile: loaded assets for %s (%s)\n",
			state->active_chatfile,
			state->active_chatname);
	return 1;
}

/*
=============
BotChat_ExpandChatMessageOnce

Runs one retail chat-constructor expansion pass over variables and random
references.
=============
*/
static int BotChat_ExpandChatMessageOnce(bot_chatstate_t *state,
	unsigned long context,
	const char *template_text,
	const char *const variables[BOT_CHAT_MAX_MATCH_VARIABLES],
	char *out_message,
	size_t out_size,
	int *expanded_random)
{
	if (state == NULL || template_text == NULL || out_message == NULL || out_size == 0U)
	{
		return 0;
	}
	if (expanded_random != NULL)
	{
		*expanded_random = 0;
	}

	const size_t max_length = BOT_CHAT_MAX_MESSAGE_CHARS - 1;
	const size_t template_length = strlen(template_text);
	if (template_length > max_length)
	{
		BotLib_Print(PRT_ERROR, "BotConstructChat: message \"%s\" too long\n", template_text);
		return 0;
	}

	char assembled[BOT_CHAT_MAX_MESSAGE_CHARS];
	size_t assembled_length = 0;

	for (size_t i = 0; template_text[i] != '\0';)
	{
		if (template_text[i] == '{' && variables != NULL)
		{
			size_t end_index = i + 1;
			while (template_text[end_index] != '\0' && template_text[end_index] != '}')
			{
				++end_index;
			}
			if (template_text[end_index] == '}')
			{
				char variable_name[64];
				size_t name_length = end_index - i - 1;
				if (name_length >= sizeof(variable_name))
				{
					name_length = sizeof(variable_name) - 1;
				}
				memcpy(variable_name, template_text + i + 1, name_length);
				variable_name[name_length] = '\0';

				unsigned long variable = 0;
				if (BotChat_VariableNumberForName(variable_name, context, &variable)
					&& variable < BOT_CHAT_MAX_MATCH_VARIABLES
					&& variables[variable] != NULL)
				{
					const size_t replacement_length = strlen(variables[variable]);
					if (assembled_length + replacement_length > max_length
						|| assembled_length + replacement_length >= out_size)
					{
						BotLib_Print(PRT_ERROR,
							"BotConstructChat: message \"%s\" too long\n",
							template_text);
						return 0;
					}
					memcpy(assembled + assembled_length,
						variables[variable],
						replacement_length);
					assembled_length += replacement_length;
					i = end_index + 1;
					continue;
				}
			}
		}

		if (template_text[i] != '\\')
		{
			if (assembled_length >= max_length)
			{
				BotLib_Print(PRT_ERROR,
					"BotConstructChat: message \"%s\" too long\n",
					template_text);
				return 0;
			}
			assembled[assembled_length++] = template_text[i++];
			continue;
		}

		const char escape = template_text[i + 1];
		if (escape == '\0')
		{
			BotLib_Print(PRT_ERROR,
				"BotConstructChat: message \"%s\" invalid escape char\n",
				template_text);
			return 0;
		}

		size_t start_index = i + 2;
		size_t end_index = start_index;
		while (template_text[end_index] != '\0' && template_text[end_index] != '\\')
		{
			++end_index;
		}
		if (template_text[end_index] != '\\')
		{
			BotLib_Print(PRT_ERROR,
				"BotConstructChat: message \"%s\" invalid escape char\n",
				template_text);
			return 0;
		}

		const char *replacement = NULL;
		char replacement_buffer[64];
		if (escape == 'r')
		{
			size_t name_length = end_index - start_index;
			if (name_length == 0)
			{
				BotLib_Print(PRT_ERROR,
					"BotConstructChat: unknown random string %s\n",
					"<empty>");
				return 0;
			}
			char random_name[64];
			if (name_length >= sizeof(random_name))
			{
				name_length = sizeof(random_name) - 1;
			}
			memcpy(random_name, template_text + start_index, name_length);
			random_name[name_length] = '\0';
			if (!BotChat_RandomStringKnown(state, random_name))
			{
				BotLib_Print(PRT_ERROR,
					"BotConstructChat: unknown random string %s\n",
					random_name);
				return 0;
			}

			replacement = BotChat_SelectRandomString(state, random_name);
			if (replacement == NULL)
			{
				BotLib_Print(PRT_ERROR,
					"BotConstructChat: unknown random string %s\n",
					random_name);
				return 0;
			}
			if (expanded_random != NULL)
			{
				*expanded_random = 1;
			}
		}
		else if (escape == 'v')
		{
			unsigned long value = 0;
			int saw_digit = 0;
			for (size_t j = start_index; j < end_index; ++j)
			{
				if (!isdigit((unsigned char)template_text[j]))
				{
					BotLib_Print(PRT_ERROR,
						"BotConstructChat: message \"%s\" invalid escape char\n",
						template_text);
					return 0;
				}
				value = value * 10UL + (unsigned long)(template_text[j] - '0');
				saw_digit = 1;
			}
			if (!saw_digit)
			{
				BotLib_Print(PRT_ERROR,
					"BotConstructChat: message \"%s\" invalid escape char\n",
					template_text);
				return 0;
			}
			if (value > 10UL)
			{
				BotLib_Print(PRT_ERROR,
					"BotConstructChat: message %s variable %lu out of range\n",
					template_text,
					value);
				return 0;
			}

			if (variables != NULL
				&& value < BOT_CHAT_MAX_MATCH_VARIABLES
				&& variables[value] != NULL)
			{
				replacement = variables[value];
			}
			else
			{
				const char *variable_name = BotChat_VariableNameForNumber(context, value);
				int written;
				if (variable_name != NULL)
				{
					written = snprintf(replacement_buffer,
						sizeof(replacement_buffer),
						"{%s}",
						variable_name);
				}
				else
				{
					written = snprintf(replacement_buffer,
						sizeof(replacement_buffer),
						"{VAR%lu}",
						value);
				}
				if (written <= 0 || (size_t)written >= sizeof(replacement_buffer))
				{
					return 0;
				}
				replacement = replacement_buffer;
			}
		}
		else
		{
			BotLib_Print(PRT_ERROR,
				"BotConstructChat: message \"%s\" invalid escape char\n",
				template_text);
			return 0;
		}

		const size_t replacement_length = strlen(replacement);
		if (assembled_length + replacement_length > max_length
			|| assembled_length + replacement_length >= out_size)
		{
			BotLib_Print(PRT_ERROR,
				"BotConstructChat: message \"%s\" too long\n",
				template_text);
			return 0;
		}

		memcpy(assembled + assembled_length, replacement, replacement_length);
		assembled_length += replacement_length;
		i = end_index + 1;
	}

	assembled[assembled_length] = '\0';
	snprintf(out_message, out_size, "%s", assembled);
	return 1;
}

/*
=============
BotConstructChatMessageWithVariables

Validates a chat template, repeats random-string expansion, copies it into the
provided buffer, and queues it when the text passes safety checks.
=============
*/
static int BotConstructChatMessageWithVariables(bot_chatstate_t *state,
	unsigned long context,
	const char *template_text,
	const char *const variables[BOT_CHAT_MAX_MATCH_VARIABLES],
	char *out_message,
	size_t out_size,
	int replace_synonyms)
{
	if (state == NULL || template_text == NULL || out_message == NULL || out_size == 0U)
	{
		return 0;
	}

	const size_t template_length = strlen(template_text);
	if (template_length >= BOT_CHAT_MAX_MESSAGE_CHARS)
	{
		BotLib_Print(PRT_ERROR, "BotConstructChat: message \"%s\" too long\n", template_text);
		return 0;
	}

	char source[BOT_CHAT_MAX_MESSAGE_CHARS];
	char expanded[BOT_CHAT_MAX_MESSAGE_CHARS];
	snprintf(source, sizeof(source), "%s", template_text);
	expanded[0] = '\0';

	int i = 0;
	for (i = 0; i < 10; ++i)
	{
		int expanded_random = 0;
		if (!BotChat_ExpandChatMessageOnce(state,
			context,
			source,
			variables,
			expanded,
			sizeof(expanded),
			&expanded_random))
		{
			return 0;
		}

		snprintf(source, sizeof(source), "%s", expanded);
		if (!expanded_random)
		{
			break;
		}
	}

	if (i >= 10)
	{
		BotLib_Print(PRT_WARNING, "too many expansions in chat message\n");
		BotLib_Print(PRT_WARNING, "%s\n", expanded);
	}

	if (replace_synonyms
		&& !BotChat_ReplaceWeightedSynonyms(state,
			context,
			source,
			sizeof(source),
			template_text))
	{
		return 0;
	}

	if (strlen(source) >= out_size)
	{
		BotLib_Print(PRT_ERROR, "BotConstructChat: message \"%s\" too long\n", template_text);
		return 0;
	}

	snprintf(out_message, out_size, "%s", source);
	BotQueueConsoleMessage(state, (int)context, out_message);
	return 1;
}

/*
=============
BotConstructChatMessage

Constructs a message using readable placeholder names for variable references.
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
		template_text,
		NULL,
		out_message,
		out_size,
		0);
}

/*
=============
BotChat_DispatchMessage

Formats and emits a chat command through the Quake II bridge while preserving
the console queue used for diagnostics.
=============
*/
static void BotChat_DispatchMessage(bot_chatstate_t *state,
	const char *message,
int client,
int sendto)
{
	if (state == NULL || message == NULL || message[0] == '\0')
	{
		return;
	}

	switch (sendto)
	{
		case BOT_CHAT_SENDTO_TEAM:
			Q2_BotClientCommand(client, "%s %s", "say_team", message);
			return;
		case BOT_CHAT_SENDTO_TELL:
			Q2_BotClientCommand(client, "tell %d %s", client, message);
			return;
		default:
			Q2_BotClientCommand(client, "%s %s", "say", message);
			return;
	}
}

/*
=============
BotFreeChatFile

Releases chat resources, cooldown storage, and deterministic clock overrides.
=============
*/
void BotFreeChatFile(bot_chatstate_t *state)
{
	if (state == NULL)
	{
		return;
	}

	BotChat_CloseActiveScript(state);
	BotChat_FreeSynonymContexts(state);
	BotChat_FreeMatchContexts(state);
	BotChat_FreeReplies(state);
	BotChat_FreeRandomTables(state);
	BotChat_FreeInitialTypes(state);
	BotChat_ClearMetadata(state);

free(state->cooldowns);
state->cooldowns = NULL;
state->cooldown_count = 0;
state->cooldown_capacity = 0;
	free(state->client_cooldowns);
	state->client_cooldowns = NULL;
state->client_cooldown_count = 0;
state->has_time_override = 0;
state->time_override_seconds = 0.0;
}

/*
=============
BotChat_ConsoleQueueReady
=============
*/
static int BotChat_ConsoleQueueReady(const bot_chatstate_t *state)
{
	if (!BotLibraryInitialized())
	{
		return 0;
	}

	if (state == NULL)
	{
		return 0;
	}

	if (state->console_head >= BOT_CHAT_MAX_CONSOLE_MESSAGES
		|| state->console_count > BOT_CHAT_MAX_CONSOLE_MESSAGES)
	{
		return 0;
	}

	return 1;
}

/*
=============
BotQueueConsoleMessage
=============
*/
void BotQueueConsoleMessage(bot_chatstate_t *state, int type, const char *message)
{
	if (!BotChat_ConsoleQueueReady(state) || message == NULL)
	{
		return;
	}

	if (state->console_count == BOT_CHAT_MAX_CONSOLE_MESSAGES)
	{
		BotLib_Print(PRT_ERROR, "empty console message heap\n");
		return;
	}

	size_t insert_index = (state->console_head + state->console_count) % BOT_CHAT_MAX_CONSOLE_MESSAGES;
	bot_console_message_t *slot = &state->console_queue[insert_index];
	slot->type = type;
	strncpy(slot->text, message, sizeof(slot->text) - 1);
	slot->text[sizeof(slot->text) - 1] = '\0';
	state->console_count++;
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
=============
*/
int BotNextConsoleMessage(bot_chatstate_t *state, int *type, char *buffer, size_t buffer_size)
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

	if (state->console_count == 0)
	{
		return 0;
	}

	const bot_console_message_t *slot = &state->console_queue[state->console_head];
	if (type != NULL)
	{
		*type = slot->type;
	}

	if (buffer != NULL && buffer_size > 0)
	{
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

/*
=============
BotNumConsoleMessages
=============
*/
size_t BotNumConsoleMessages(const bot_chatstate_t *state)
{
	if (!BotChat_ConsoleQueueReady(state))
	{
		return 0;
	}

	return state->console_count;
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

	return (int)initial_type->template_count;
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

	bot_match_context_t *match_context = BotChat_FindMatchContext(state, context);
	const char *template_text = BotChat_SelectRandomTemplate(state, match_context, seed);
	if (template_text == NULL) {
		if (missing_context_message != NULL) {
			BotLib_Print(PRT_MESSAGE, "%s", missing_context_message);
			BotQueueConsoleMessage(state, PRT_MESSAGE, missing_context_message);
		}
		return 0;
	}

	char message[BOT_CHAT_MAX_MESSAGE_CHARS];
	if (!BotConstructChatMessage(state, context, template_text, message, sizeof(message))) {
		return 0;
	}

	BotChat_DispatchMessage(state, message, client, sendto);
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

Builds and dispatches the MSG_ENTERGAME template while respecting cooldowns.
=============
*/
void BotEnterChat(bot_chatstate_t *state, int client, int sendto)
{
	BotChat_EnterGame(state, client, sendto);
}

/*
=============
BotInitialChat

Constructs one raw initial-chat message for the requested type and queues it
for retrieval, matching Quake III's construction stage before BotEnterChat.
=============
*/
int BotInitialChat(bot_chatstate_t *state,
	const char *type,
	unsigned long context,
	...)
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
	va_list args;
	va_start(args, context);
	for (size_t i = 0; i < BOT_CHAT_MAX_MATCH_VARIABLES; ++i)
	{
		const char *value = va_arg(args, const char *);
		if (value == NULL)
		{
			break;
		}
		variables[i] = value;
	}
	va_end(args);

	char message[BOT_CHAT_MAX_MESSAGE_CHARS];
	return BotConstructChatMessageWithVariables(state,
		context,
		template_text,
		variables,
		message,
		sizeof(message),
		1);
}

/*
=============
BotChat_MergeCapturedVariables

Copies newly matched variables into an accumulated capture table.
=============
*/
static void BotChat_MergeCapturedVariables(
	char destination_storage[][BOT_CHAT_MAX_MESSAGE_CHARS],
	const char *destination[BOT_CHAT_MAX_MATCH_VARIABLES],
	const char *const source[BOT_CHAT_MAX_MATCH_VARIABLES])
{
	if (destination_storage == NULL || destination == NULL || source == NULL)
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
BotChat_ReplyKeyMatches

Evaluates one parsed reply key and captures variables when present.
=============
*/
static int BotChat_ReplyKeyMatches(const bot_reply_key_t *key,
	const char *message,
	char storage[][BOT_CHAT_MAX_MESSAGE_CHARS],
	const char *variables[BOT_CHAT_MAX_MATCH_VARIABLES])
{
	if (key == NULL || key->pattern == NULL || message == NULL)
	{
		return 0;
	}

	if (key->is_pattern)
	{
		return BotChat_MatchVariablePattern(key->pattern,
			message,
			storage,
			variables);
	}

	BotChat_ClearCapturedVariables(variables);
	return BotChat_StringContainsWordCaseInsensitive(message, key->pattern);
}

/*
=============
BotChat_ReplyRuleMatches

Applies retail-style reply-key AND, NOT, and optional-key semantics.
=============
*/
static int BotChat_ReplyRuleMatches(const bot_reply_rule_t *rule,
	const char *message,
	char storage[][BOT_CHAT_MAX_MESSAGE_CHARS],
	const char *variables[BOT_CHAT_MAX_MATCH_VARIABLES])
{
	if (rule == NULL || message == NULL)
	{
		return 0;
	}

	BotChat_ClearCapturedVariables(variables);
	if (rule->key_count == 0)
	{
		return 1;
	}

	int found = 0;
	for (size_t i = 0; i < rule->key_count; ++i)
	{
		char key_storage[BOT_CHAT_MAX_MATCH_VARIABLES][BOT_CHAT_MAX_MESSAGE_CHARS];
		const char *key_variables[BOT_CHAT_MAX_MATCH_VARIABLES] = {0};
		const bot_reply_key_t *key = &rule->keys[i];
		const int matched = BotChat_ReplyKeyMatches(key,
			message,
			key_storage,
			key_variables);

		if (key->required)
		{
			if (!matched)
			{
				return 0;
			}
			BotChat_MergeCapturedVariables(storage, variables, key_variables);
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
			BotChat_MergeCapturedVariables(storage, variables, key_variables);
		}
	}

	return found;
}

/*
=============
BotReplyChat

Constructs a reply by preferring match templates and falling back to reply
tables, emitting diagnostics when no response can be generated.
=============
*/
int BotReplyChat(bot_chatstate_t *state, const char *message, unsigned long int context)
{
if (state == NULL || message == NULL)
{
return 0;
}

const double now_seconds = BotChat_CurrentTimeSeconds(state);
if (!BotChat_EventAllowed(state, state->speaking_client, context, now_seconds))
{
return 0;
}

	const char *template_text = NULL;
	char captured_storage[BOT_CHAT_MAX_MATCH_VARIABLES][BOT_CHAT_MAX_MESSAGE_CHARS];
	const char *captured_variables[BOT_CHAT_MAX_MATCH_VARIABLES] = {0};
	int has_captured_variables = 0;
	bot_match_context_t *match_context = BotChat_FindMatchContext(state, context);
	if (match_context != NULL && match_context->template_count > 0)
	{
		size_t *matching_indices = malloc(match_context->template_count * sizeof(size_t));
		size_t match_count = 0;
		if (matching_indices != NULL)
		{
			for (size_t i = 0; i < match_context->template_count; ++i)
			{
				const char *candidate = match_context->templates[i];
				if (candidate == NULL)
				{
					continue;
				}

				char candidate_storage[BOT_CHAT_MAX_MATCH_VARIABLES][BOT_CHAT_MAX_MESSAGE_CHARS];
				const char *candidate_variables[BOT_CHAT_MAX_MATCH_VARIABLES] = {0};
				const int has_placeholders = strchr(candidate, '{') != NULL;
				const int matches = has_placeholders ?
					BotChat_CaptureMatchTemplateVariables(candidate,
						message,
						context,
						candidate_storage,
						candidate_variables) :
					BotChat_TemplateMatchesMessage(state, candidate, message);
				if (matches)
				{
					matching_indices[match_count++] = i;
				}
			}

			if (match_count > 0)
			{
				size_t selected_index = BotChat_SelectIndex(message, match_count);
				template_text = match_context->templates[matching_indices[selected_index]];
				if (template_text != NULL && strchr(template_text, '{') != NULL
					&& BotChat_CaptureMatchTemplateVariables(template_text,
						message,
						context,
						captured_storage,
						captured_variables))
				{
					has_captured_variables = 1;
				}
			}

			free(matching_indices);
		}
	}

	char constructed[BOT_CHAT_MAX_MESSAGE_CHARS];

	if (template_text != NULL)
	{
		if (!BotConstructChatMessageWithVariables(state,
				context,
				template_text,
				has_captured_variables ? captured_variables : NULL,
				constructed,
				sizeof(constructed),
				0))
		{
			return 0;
		}
		BotChat_DispatchMessage(state, constructed, state->speaking_client, BOT_CHAT_SENDTO_ALL);
		BotChat_CommitClientCooldown(state, (size_t)state->speaking_client, now_seconds);
		return 1;
	}

	if (!state->has_reply_chats)
	{
		const char *diagnostic = "no rchats\n";
		BotLib_Print(PRT_MESSAGE, "%s", diagnostic);
		BotQueueConsoleMessage(state, PRT_MESSAGE, diagnostic);
		return 0;
	}

	const bot_reply_rule_t *reply_rule = NULL;
	BotChat_ClearCapturedVariables(captured_variables);
	has_captured_variables = 0;
	for (size_t i = 0; i < state->replies.rule_count; ++i)
	{
		bot_reply_rule_t *candidate_rule = &state->replies.rules[i];
		if (candidate_rule->reply_context != context
			|| candidate_rule->response_count == 0)
		{
			continue;
		}

		char rule_storage[BOT_CHAT_MAX_MATCH_VARIABLES][BOT_CHAT_MAX_MESSAGE_CHARS];
		const char *rule_variables[BOT_CHAT_MAX_MATCH_VARIABLES] = {0};
		if (!BotChat_ReplyRuleMatches(candidate_rule,
				message,
				rule_storage,
				rule_variables))
		{
			continue;
		}

		reply_rule = candidate_rule;
		BotChat_CopyCapturedVariables(captured_storage,
			captured_variables,
			rule_variables);
		has_captured_variables = 1;
		break;
	}

	if (reply_rule != NULL && reply_rule->response_count > 0)
	{
		size_t index = BotChat_SelectIndex(message, reply_rule->response_count);
		template_text = reply_rule->responses[index];
		if (template_text != NULL)
		{
			if (!BotConstructChatMessageWithVariables(state,
					context,
					template_text,
					has_captured_variables ? captured_variables : NULL,
					constructed,
					sizeof(constructed),
					0))
			{
				return 0;
			}
			BotChat_DispatchMessage(state, constructed, state->speaking_client, BOT_CHAT_SENDTO_ALL);
			BotChat_CommitClientCooldown(state, (size_t)state->speaking_client, now_seconds);
			return 1;
		}
	}

	const char *diagnostic = "no rchats\n";
	BotLib_Print(PRT_MESSAGE, "%s", diagnostic);
	BotQueueConsoleMessage(state, PRT_MESSAGE, diagnostic);
	return 0;
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
		if (template_text[template_offset] != '\\')
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
		while (template_text[name_end] != '\0' && template_text[name_end] != '\\')
		{
			++name_end;
		}
		if (template_text[name_end] != '\\')
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

int BotChatLength(const char *message)
{
    if (message == NULL) {
        return 0;
    }

    return (int)strlen(message);
}

int BotChat_HasSynonymPhrase(const bot_chatstate_t *state, const char *context_name, const char *phrase)
{
    if (state == NULL || context_name == NULL || phrase == NULL) {
        return 0;
    }

    for (size_t i = 0; i < state->synonym_context_count; ++i) {
        const bot_synonym_context_t *context = &state->synonym_contexts[i];
        if (context->context_name == NULL) {
            continue;
        }
        if (strcmp(context->context_name, context_name) != 0) {
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

    return 0;
}

int BotChat_HasReplyTemplate(const bot_chatstate_t *state, unsigned long int context, const char *template_text)
{
    if (state == NULL || template_text == NULL) {
        return 0;
    }

    const bot_match_context_t *match = BotChat_FindMatchContext((bot_chatstate_t *)state, context);
    if (match != NULL) {
        for (size_t i = 0; i < match->template_count; ++i) {
			const char *stored_template = match->templates[i];
			if (stored_template == NULL) {
				continue;
			}
			if (strcmp(stored_template, template_text) == 0) {
				return 1;
			}
			if (strchr(stored_template, '\\') != NULL
				&& BotChat_TemplateCanConstructText(state,
					context,
					stored_template,
					template_text)) {
				return 1;
			}
        }
    }

	for (size_t rule_index = 0; rule_index < state->replies.rule_count; ++rule_index) {
		const bot_reply_rule_t *rule = &state->replies.rules[rule_index];
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
			if (strchr(stored_template, '\\') != NULL
				&& BotChat_TemplateCanConstructText(state,
					context,
					stored_template,
					template_text)) {
				return 1;
			}
        }
    }

    return 0;
}
