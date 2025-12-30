#include "ai_chat.h"

#include <ctype.h>
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
    char **responses;
    size_t response_count;
    size_t response_capacity;
} bot_reply_rule_t;

typedef struct {
    bot_reply_rule_t *rules;
    size_t rule_count;
    size_t rule_capacity;
} bot_reply_table_t;

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

	bot_chat_cooldown_entry_t *cooldowns;
	size_t cooldown_count;
	size_t cooldown_capacity;

	bot_chat_client_cooldown_t *client_cooldowns;
	size_t client_cooldown_count;
	double time_override_seconds;
	int has_time_override;
	int speaking_client;
};

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
		if (slot != NULL)
		{
			slot->next_allowed_time = now_seconds;
		}

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

	slot->next_allowed_time = now_seconds + min_interval;
	return 0;
}

static int BotChat_CooldownBlocks(bot_chatstate_t *state,
		unsigned long context,
		double now_seconds);

/*
=============
BotChat_EventAllowed

Gates chat execution on nochat, client bounds, and cooldowns while logging
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

	if (BotChat_ClientCooldownBlocks(state, (size_t)client, now_seconds))
	{
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
static int BotChat_RandomStringKnown(const char *name)
{
	static const char *kKnownRandomTables[] = {
	"random_misc",
	"random_insult"
	};
	if (name == NULL)
	{
	return 0;
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
BotChat_FindRandomTable

Looks up a built-in random string table by name.
=============
*/
static const bot_chat_random_table_t *BotChat_FindRandomTable(const char *name)
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

	const bot_chat_random_table_t *table = BotChat_FindRandomTable(name);
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

static bot_synonym_context_t *BotChat_AddSynonymContext(bot_chatstate_t *state, const char *name)
{
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
		char *phrase_text = BotChat_StringDuplicate(token.string);
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
BotChat_ParseSynonymContexts

Walks the active script once to collect CONTEXT_* blocks.
=============
*/
static int BotChat_ParseSynonymContexts(bot_chatstate_t *state)
{
	if (state == NULL || state->active_script == NULL)
	{
		return 0;
	}

	ResetScript(state->active_script);
	pc_token_t token;
	while (PS_ReadToken(state->active_script, &token))
	{
		if (token.type != TT_NAME || strncmp(token.string, "CONTEXT_", 8) != 0)
		{
			continue;
		}
		if (!PS_ExpectTokenString(state->active_script, "{"))
		{
			return 0;
		}
		bot_synonym_context_t *context = BotChat_AddSynonymContext(state, token.string);
		if (context == NULL)
		{
			return 0;
		}
		while (1)
		{
			if (PS_CheckTokenString(state->active_script, "}"))
			{
				break;
			}
			if (!PS_ReadToken(state->active_script, &token))
			{
				return 0;
			}
			if (token.type == TT_PUNCTUATION && token.string[0] == '[')
			{
				if (!BotChat_ParseSynonymGroup(context, state->active_script))
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
			if (!BotChat_StringBuilderAppendChar(&builder, ' '))
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			continue;
		}
		if (token.type == TT_STRING)
		{
			if (!BotChat_StringBuilderAppend(&builder, token.string)
				|| !BotChat_StringBuilderAppendChar(&builder, ' '))
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			continue;
		}
		if (token.type == TT_NAME)
		{
			if (!BotChat_StringBuilderAppendIdentifier(&builder, token.string, strlen(token.string))
				|| !BotChat_StringBuilderAppendChar(&builder, ' '))
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			continue;
		}
		if (token.type == TT_NUMBER)
		{
			if (!BotChat_StringBuilderAppend(&builder, token.string)
				|| !BotChat_StringBuilderAppendChar(&builder, ' '))
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
	if (!PS_ExpectTokenType(script, TT_NAME, 0, &type_token))
	{
		BotChat_StringBuilderDestroy(&builder);
		return 0;
	}
	unsigned long message_type = BotChat_MessageTypeFromIdentifier(type_token.string, strlen(type_token.string));
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
	bot_match_context_t *context = BotChat_FindMatchContext(state, message_type);
	if (context == NULL)
	{
		context = BotChat_AddMatchContext(state, message_type);
		if (context == NULL)
		{
			free(template_text);
			return 0;
		}
	}
	char **slot = BotChat_AddTemplate(context);
	if (slot == NULL)
	{
		free(template_text);
		return 0;
	}
	*slot = template_text;
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
				if (!BotChat_StringBuilderAppendChar(&builder, ' '))
				{
					BotChat_StringBuilderDestroy(&builder);
					return 0;
				}
				continue;
			}
		}
		if (token.type == TT_STRING)
		{
			if (!BotChat_StringBuilderAppend(&builder, token.string))
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			continue;
		}
		if (token.type == TT_NAME)
		{
			if (!BotChat_StringBuilderAppendIdentifier(&builder, token.string, strlen(token.string)))
			{
				BotChat_StringBuilderDestroy(&builder);
				return 0;
			}
			continue;
		}
		if (token.type == TT_NUMBER)
		{
			if (!BotChat_StringBuilderAppend(&builder, token.string))
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
BotChat_ParseReplyBlock

Registers a reply context and its associated templates.
=============
*/
static int BotChat_ParseReplyBlock(bot_chatstate_t *state, pc_script_t *script)
{
	if (!BotChat_SkipBalancedBlock(script, '[', ']'))
	{
		return 0;
	}
	if (!PS_ExpectTokenString(script, "="))
	{
		return 0;
	}
	pc_token_t token;
	if (!PS_ExpectTokenType(script, TT_NUMBER, 0, &token))
	{
		return 0;
	}
	unsigned long reply_context = (unsigned long)token.intvalue;
	if (!PS_ExpectTokenString(script, "{"))
	{
		return 0;
	}
	bot_reply_rule_t *rule = BotChat_FindReplyRule(state, reply_context);
	if (rule == NULL)
	{
		rule = BotChat_AddReplyRule(state, reply_context);
		if (rule == NULL)
		{
			return 0;
		}
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
BotChat_ParseMatchPass

Parses match contexts while skipping reply definitions.
=============
*/
static int BotChat_ParseMatchPass(bot_chatstate_t *state)
{
	if (state == NULL || state->active_script == NULL)
	{
		return 0;
	}
	ResetScript(state->active_script);
	pc_token_t token;
	while (PS_ReadToken(state->active_script, &token))
	{
		if (token.type == TT_NAME && strncmp(token.string, "MTCONTEXT_", 10) == 0)
		{
			if (!PS_ExpectTokenString(state->active_script, "{"))
			{
				return 0;
			}
			if (!BotChat_ParseMatchBlock(state, state->active_script))
			{
				return 0;
			}
			continue;
		}
		if (token.type == TT_PUNCTUATION && token.string[0] == '[')
		{
			if (!BotChat_SkipBalancedBlock(state->active_script, '[', ']'))
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

Parses reply contexts while skipping match definitions.
=============
*/
static int BotChat_ParseReplyPass(bot_chatstate_t *state)
{
	if (state == NULL || state->active_script == NULL)
	{
		return 0;
	}
	ResetScript(state->active_script);
	pc_token_t token;
	while (PS_ReadToken(state->active_script, &token))
	{
		if (token.type == TT_NAME && strncmp(token.string, "MTCONTEXT_", 10) == 0)
		{
			if (!BotChat_SkipBalancedBlock(state->active_script, '{', '}'))
			{
				return 0;
			}
			continue;
		}
		if (token.type == TT_PUNCTUATION && token.string[0] == '[')
		{
			if (!BotChat_ParseReplyBlock(state, state->active_script))
			{
				return 0;
			}
		}
	}
	return 1;
}

/*
=============
BotChat_ParseInitialChat

Runs the parsing passes required for initial chat data.
=============
*/
static int BotChat_ParseInitialChat(bot_chatstate_t *state)
{
	if (!BotChat_ParseSynonymContexts(state))
	{
		return 0;
	}
	if (!BotChat_ParseMatchPass(state))
	{
		return 0;
	}
	return 1;
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

	pc_source_t *source = PC_LoadSourceFile(chatfile);
	if (source == NULL)
	{
		BotChat_PrintLegacyDiagnostic(state,
				PRT_FATAL,
				fastchat_enabled,
				"couldn't load chat %s from %s\n",
				chatname,
				chatfile);
		return 0;
	}

	pc_script_t *script = PS_CreateScriptFromSource(source);
	if (script == NULL)
	{
		BotLib_Print(PRT_ERROR, "BotLoadChatFile: script wrapper failed for %s\n", chatfile);
		PC_FreeSource(source);
		BotChat_PrintLegacyDiagnostic(state,
				PRT_ERROR,
				fastchat_enabled,
				"couldn't find chat %s in %s\n",
				chatname,
				chatfile);
		return 0;
	}

	state->active_source = source;
	state->active_script = script;

	if (!BotChat_ParseInitialChat(state))
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
BotConstructChatMessage

Validates a chat template, copies it into the provided buffer, and queues it
when the text passes safety checks.
=============
*/
static int BotConstructChatMessage(bot_chatstate_t *state,
unsigned long context,
const char *template_text,
char *out_message,
size_t out_size)
{
	if (state == NULL || template_text == NULL || out_message == NULL || out_size == 0U)
	{
	return 0;
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
	if (template_text[i] != '\\')
	{
	if (assembled_length >= max_length)
	{
	BotLib_Print(PRT_ERROR, "BotConstructChat: message \"%s\" too long\n", template_text);
	return 0;
	}
	assembled[assembled_length++] = template_text[i++];
	continue;
	}

	const char escape = template_text[i + 1];
	if (escape == '\0' || escape != 'r')
	{
	BotLib_Print(PRT_ERROR, "BotConstructChat: message \"%s\" invalid escape char\n", template_text);
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
	BotLib_Print(PRT_ERROR, "BotConstructChat: message \"%s\" invalid escape char\n", template_text);
	return 0;
	}

	size_t name_length = end_index - start_index;
	if (name_length == 0)
	{
	BotLib_Print(PRT_ERROR, "BotConstructChat: unknown random string %s\n", "<empty>");
	return 0;
	}
	char random_name[64];
	if (name_length >= sizeof(random_name))
	{
	name_length = sizeof(random_name) - 1;
	}
	memcpy(random_name, template_text + start_index, name_length);
	random_name[name_length] = '\0';
	if (!BotChat_RandomStringKnown(random_name))
	{
	BotLib_Print(PRT_ERROR, "BotConstructChat: unknown random string %s\n", random_name);
	return 0;
	}

	const char *replacement = BotChat_SelectRandomString(state, random_name);
	if (replacement == NULL)
	{
	BotLib_Print(PRT_ERROR, "BotConstructChat: unknown random string %s\n", random_name);
	return 0;
	}

	size_t replacement_length = strlen(replacement);
	if (assembled_length + replacement_length > max_length)
	{
	BotLib_Print(PRT_ERROR, "BotConstructChat: message \"%s\" too long\n", template_text);
	return 0;
	}
	if (assembled_length + replacement_length >= out_size)
	{
	BotLib_Print(PRT_ERROR, "BotConstructChat: message \"%s\" too long\n", template_text);
	return 0;
	}

	memcpy(assembled + assembled_length, replacement, replacement_length);
	assembled_length += replacement_length;
	i = end_index + 1;
	}

	assembled[assembled_length] = '\0';
	strncpy(out_message, assembled, out_size - 1U);
	out_message[out_size - 1U] = '\0';
	BotQueueConsoleMessage(state, (int)context, out_message);
	return 1;
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

	BotChat_FreeSynonymContexts(state);
	BotChat_FreeMatchContexts(state);
BotChat_FreeReplies(state);
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

	if (!BotChat_EventAllowed(state, client, context, BotChat_CurrentTimeSeconds(state))) {
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

if (!BotChat_EventAllowed(state, state->speaking_client, context, BotChat_CurrentTimeSeconds(state)))
{
return 0;
}

	const char *template_text = NULL;
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

				if (BotChat_TemplateMatchesMessage(state, candidate, message))
				{
					matching_indices[match_count++] = i;
				}
			}

			if (match_count > 0)
			{
				size_t selected_index = BotChat_SelectIndex(message, match_count);
				template_text = match_context->templates[matching_indices[selected_index]];
			}

			free(matching_indices);
		}
	}

	char constructed[BOT_CHAT_MAX_MESSAGE_CHARS];

	if (template_text != NULL && BotConstructChatMessage(state, context, template_text, constructed, sizeof(constructed)))
	{
		BotChat_DispatchMessage(state, constructed, state->speaking_client, BOT_CHAT_SENDTO_ALL);
		return 1;
	}

	if (!state->has_reply_chats)
	{
		BotLib_Print(PRT_MESSAGE, "no rchats\n");
		return 0;
	}

	bot_reply_rule_t *reply_rule = BotChat_FindReplyRule(state, context);
	if (reply_rule != NULL && reply_rule->response_count > 0)
	{
		size_t index = BotChat_SelectIndex(message, reply_rule->response_count);
		template_text = reply_rule->responses[index];
		if (template_text != NULL && BotConstructChatMessage(state, context, template_text, constructed, sizeof(constructed)))
		{
			BotChat_DispatchMessage(state, constructed, state->speaking_client, BOT_CHAT_SENDTO_ALL);
			return 1;
		}
	}

	BotLib_Print(PRT_MESSAGE, "no rchats\n");
	return 0;
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
            if (match->templates[i] != NULL && strcmp(match->templates[i], template_text) == 0) {
                return 1;
            }
        }
    }

    const bot_reply_rule_t *rule = BotChat_FindReplyRule((bot_chatstate_t *)state, context);
    if (rule != NULL) {
        for (size_t i = 0; i < rule->response_count; ++i) {
            if (rule->responses[i] != NULL && strcmp(rule->responses[i], template_text) == 0) {
                return 1;
            }
        }
    }

    return 0;
}
