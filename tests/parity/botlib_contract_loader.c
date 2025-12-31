#include "botlib_contract_loader.h"

#define JSMN_PARENT_LINKS
#include "jsmn.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAILURE_SEVERITY_THRESHOLD 3
#define CONTRACT_DEBUG_LOG(enabled, ...) \
	do { \
		if ((enabled)) { \
			fprintf(stderr, __VA_ARGS__); \
		} \
	} while (0)

static char *duplicate_string(const char *text)
{
    if (text == NULL)
    {
        return NULL;
    }

    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1U);
    if (copy != NULL)
    {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

static char *duplicate_range(const char *json, const jsmntok_t *token)
{
	size_t length = (size_t)(token->end - token->start);
	char *copy = (char *)malloc(length + 1U);
	if (copy == NULL)
	{
		return NULL;
	}

	memcpy(copy, json + token->start, length);
	copy[length] = '\0';

	if (token->type != JSMN_STRING || length == 0U)
	{
		return copy;
	}

	size_t read_index = 0;
	size_t write_index = 0;
	while (read_index < length)
	{
		char ch = copy[read_index++];
		if (ch == '\\' && read_index < length)
		{
			char esc = copy[read_index++];
			switch (esc)
			{
				case 'n':
					copy[write_index++] = '\n';
					break;
				case 'r':
					copy[write_index++] = '\r';
					break;
				case 't':
					copy[write_index++] = '\t';
					break;
				case 'b':
					copy[write_index++] = '\b';
					break;
				case 'f':
					copy[write_index++] = '\f';
					break;
				case '\\':
					copy[write_index++] = '\\';
					break;
				case '/':
					copy[write_index++] = '/';
					break;
				case '"':
					copy[write_index++] = '"';
					break;
				case 'u':
				{
					unsigned int codepoint = 0U;
					bool valid = true;
					for (int i = 0; i < 4; ++i)
					{
						if (read_index >= length)
						{
							valid = false;
							break;
						}
						char hex = copy[read_index++];
						codepoint <<= 4;
						if (hex >= '0' && hex <= '9')
						{
							codepoint |= (unsigned int)(hex - '0');
						}
						else if (hex >= 'a' && hex <= 'f')
						{
							codepoint |= (unsigned int)(hex - 'a' + 10U);
						}
						else if (hex >= 'A' && hex <= 'F')
						{
							codepoint |= (unsigned int)(hex - 'A' + 10U);
						}
						else
						{
							valid = false;
							break;
						}
					}
					if (valid && codepoint <= 0x7FU)
					{
						copy[write_index++] = (char)codepoint;
					}
					else
					{
						copy[write_index++] = '?';
					}
					break;
				}
				default:
					copy[write_index++] = esc;
					break;
			}
		}
		else
		{
			copy[write_index++] = ch;
		}
	}
	copy[write_index] = '\0';
	return copy;
}

static bool token_equals(const char *json, const jsmntok_t *token, const char *text)
{
    size_t length = (size_t)(token->end - token->start);
    return (strlen(text) == length) && (strncmp(json + token->start, text, length) == 0);
}

/*
=============
skip_token
=============
*/
static int skip_token(const jsmntok_t *tokens, int index)
{
	const jsmntok_t *token = &tokens[index];
	const char *debug_env = getenv("BOTLIB_CONTRACT_DEBUG_TOKENS");
	if (debug_env != NULL)
	{
		fprintf(stderr,
		        "skip_token: index %d type %d size %d\n",
		        index,
		        token->type,
		        token->size);
	}
	int next = index + 1;

	switch (token->type)
	{
		case JSMN_OBJECT:
		{
			int pair_count = token->size / 2;
			for (int i = 0; i < pair_count; ++i)
			{
				next = skip_token(tokens, next);
				next = skip_token(tokens, next);
			}
			break;
		}
		case JSMN_ARRAY:
			for (int i = 0; i < token->size; ++i)
			{
				next = skip_token(tokens, next);
			}
			break;
		default:
			break;
	}

	return next;
}

static int parse_int(const char *json, const jsmntok_t *token, int *out_value)
{
    char *text = duplicate_range(json, token);
    if (text == NULL)
    {
        return -1;
    }

    char *endptr = NULL;
    long value = strtol(text, &endptr, 10);
    bool ok = (endptr != NULL && *endptr == '\0');
    free(text);

    if (!ok)
    {
        return -1;
    }

    *out_value = (int)value;
    return 0;
}

static int parse_messages(
    const char *json,
    jsmntok_t *tokens,
    int index,
    botlib_contract_message_t **messages_out,
    size_t *count_out,
    int *next_index_out)
{
    const jsmntok_t *array = &tokens[index];
    size_t count = (size_t)array->size;
    int cursor = index + 1;

    botlib_contract_message_t *messages = NULL;
    if (count > 0)
    {
        messages = (botlib_contract_message_t *)calloc(count, sizeof(*messages));
        if (messages == NULL)
        {
            return -1;
        }
    }

    for (size_t i = 0; i < count; ++i)
    {
        const jsmntok_t *object = &tokens[cursor];
        if (object->type != JSMN_OBJECT)
        {
            free(messages);
            return -1;
        }
        cursor += 1;

        botlib_contract_message_t *message = &messages[i];
		int field_count = object->size / 2;
        for (int field = 0; field < field_count; ++field)
        {
            int key_index = cursor;
            cursor = skip_token(tokens, cursor);
            int value_index = cursor;
            cursor = skip_token(tokens, cursor);

            const jsmntok_t *key = &tokens[key_index];
            const jsmntok_t *value = &tokens[value_index];

            if (key->type != JSMN_STRING)
            {
                continue;
            }

            if (token_equals(json, key, "severity") && value->type == JSMN_PRIMITIVE)
            {
                if (parse_int(json, value, &message->severity) != 0)
                {
                    free(messages);
                    return -1;
                }
            }
            else if (token_equals(json, key, "text") && value->type == JSMN_STRING)
            {
                message->text = duplicate_range(json, value);
                if (message->text == NULL)
                {
                    free(messages);
                    return -1;
                }
            }
        }
    }

    *messages_out = messages;
    *count_out = count;
    *next_index_out = cursor;
    return 0;
}

static int parse_return_codes(
    const char *json,
    jsmntok_t *tokens,
    int index,
    botlib_contract_return_code_t **returns_out,
    size_t *count_out,
    int *next_index_out)
{
    const jsmntok_t *array = &tokens[index];
    size_t count = (size_t)array->size;
    int cursor = index + 1;

    botlib_contract_return_code_t *values = NULL;
    if (count > 0)
    {
        values = (botlib_contract_return_code_t *)calloc(count, sizeof(*values));
        if (values == NULL)
        {
            return -1;
        }
    }

    for (size_t i = 0; i < count; ++i)
    {
        const jsmntok_t *object = &tokens[cursor];
        if (object->type != JSMN_OBJECT)
        {
            free(values);
            return -1;
        }
        cursor += 1;

        botlib_contract_return_code_t *record = &values[i];
		int field_count = object->size / 2;
        for (int field = 0; field < field_count; ++field)
        {
            int key_index = cursor;
            cursor = skip_token(tokens, cursor);
            int value_index = cursor;
            cursor = skip_token(tokens, cursor);

            const jsmntok_t *key = &tokens[key_index];
            const jsmntok_t *value = &tokens[value_index];

            if (key->type != JSMN_STRING)
            {
                continue;
            }

            if (token_equals(json, key, "value") && value->type == JSMN_PRIMITIVE)
            {
                if (parse_int(json, value, &record->value) != 0)
                {
                    free(values);
                    return -1;
                }
            }
        }
    }

    *returns_out = values;
    *count_out = count;
    *next_index_out = cursor;
    return 0;
}

static int build_scenarios(
    botlib_contract_export_t *entry,
    botlib_contract_message_t *messages,
    size_t message_count,
    botlib_contract_return_code_t *returns,
    size_t return_count)
{
    int result = 0;
    size_t failure_messages = 0;
    size_t success_messages = 0;
    for (size_t i = 0; i < message_count; ++i)
    {
        if (messages[i].severity >= FAILURE_SEVERITY_THRESHOLD)
        {
            failure_messages += 1U;
        }
        else
        {
            success_messages += 1U;
        }
    }

	size_t failure_returns = 0;
	size_t success_returns = 0;
	bool split_returns = false;
	bool all_returns_failure = false;
	bool all_returns_success = false;
	if (return_count > 0U)
	{
		if (failure_messages > 0U && success_messages > 0U)
		{
			split_returns = true;
		}
		else if (failure_messages > 0U)
		{
			all_returns_failure = true;
		}
		else if (success_messages > 0U)
		{
			all_returns_success = true;
		}
		else
		{
			split_returns = true;
		}

		if (split_returns)
		{
			for (size_t i = 0; i < return_count; ++i)
			{
				if (returns[i].value == 0)
				{
					success_returns += 1U;
				}
				else
				{
					failure_returns += 1U;
				}
			}
		}
		else if (all_returns_failure)
		{
			failure_returns = return_count;
		}
		else if (all_returns_success)
		{
			success_returns = return_count;
		}
	}

    size_t scenario_count = 0;
	bool has_failure = (failure_messages > 0U) || (failure_returns > 0U);
	bool has_success = (success_messages > 0U) || (success_returns > 0U);

    if (has_failure)
    {
        scenario_count += 1U;
    }
    if (has_success)
    {
        scenario_count += 1U;
    }

    if (scenario_count == 0U)
    {
        scenario_count = 1U;
    }

    entry->scenario_count = scenario_count;
    entry->scenarios = (botlib_contract_scenario_t *)calloc(scenario_count, sizeof(*entry->scenarios));
    if (entry->scenarios == NULL)
    {
        result = -1;
        goto cleanup;
    }

    size_t scenario_index = 0U;
    if (has_failure)
    {
        botlib_contract_scenario_t *scenario = &entry->scenarios[scenario_index++];
        scenario->name = duplicate_string("failure");
        if (scenario->name == NULL)
        {
            result = -1;
            goto cleanup;
        }

        scenario->message_count = failure_messages;
        if (failure_messages > 0U)
        {
            scenario->messages = (botlib_contract_message_t *)calloc(failure_messages, sizeof(*scenario->messages));
            if (scenario->messages == NULL)
            {
                result = -1;
                goto cleanup;
            }
            size_t cursor = 0U;
            for (size_t i = 0; i < message_count; ++i)
            {
                if (messages[i].severity >= FAILURE_SEVERITY_THRESHOLD)
                {
                    scenario->messages[cursor++] = messages[i];
                    messages[i].text = NULL;
                }
            }
        }

        scenario->return_count = failure_returns;
        if (failure_returns > 0U)
        {
            scenario->return_codes = (botlib_contract_return_code_t *)calloc(failure_returns, sizeof(*scenario->return_codes));
            if (scenario->return_codes == NULL)
            {
                result = -1;
                goto cleanup;
            }
            size_t cursor = 0U;
			if (all_returns_failure)
			{
				for (size_t i = 0; i < return_count; ++i)
				{
					scenario->return_codes[cursor++] = returns[i];
				}
			}
			else
			{
				for (size_t i = 0; i < return_count; ++i)
				{
					if (returns[i].value != 0)
					{
						scenario->return_codes[cursor++] = returns[i];
					}
				}
			}
        }
    }

    if (has_success)
    {
        botlib_contract_scenario_t *scenario = &entry->scenarios[scenario_index++];
        scenario->name = duplicate_string("success");
        if (scenario->name == NULL)
        {
            result = -1;
            goto cleanup;
        }

        scenario->message_count = success_messages;
        if (success_messages > 0U)
        {
            scenario->messages = (botlib_contract_message_t *)calloc(success_messages, sizeof(*scenario->messages));
            if (scenario->messages == NULL)
            {
                result = -1;
                goto cleanup;
            }
            size_t cursor = 0U;
            for (size_t i = 0; i < message_count; ++i)
            {
                if (messages[i].severity < FAILURE_SEVERITY_THRESHOLD)
                {
                    scenario->messages[cursor++] = messages[i];
                    messages[i].text = NULL;
                }
            }
        }

        scenario->return_count = success_returns;
        if (success_returns > 0U)
        {
            scenario->return_codes = (botlib_contract_return_code_t *)calloc(success_returns, sizeof(*scenario->return_codes));
            if (scenario->return_codes == NULL)
            {
                result = -1;
                goto cleanup;
            }
            size_t cursor = 0U;
			if (all_returns_success)
			{
				for (size_t i = 0; i < return_count; ++i)
				{
					scenario->return_codes[cursor++] = returns[i];
				}
			}
			else
			{
				for (size_t i = 0; i < return_count; ++i)
				{
					if (returns[i].value == 0)
					{
						scenario->return_codes[cursor++] = returns[i];
					}
				}
			}
        }
    }

    if (scenario_index == 0U)
    {
        botlib_contract_scenario_t *scenario = &entry->scenarios[0];
        scenario->name = duplicate_string("default");
        if (scenario->name == NULL)
        {
            result = -1;
            goto cleanup;
        }
        scenario->message_count = message_count;
        if (message_count > 0U)
        {
            scenario->messages = (botlib_contract_message_t *)calloc(message_count, sizeof(*scenario->messages));
            if (scenario->messages == NULL)
            {
                result = -1;
                goto cleanup;
            }
            for (size_t i = 0; i < message_count; ++i)
            {
                scenario->messages[i] = messages[i];
                messages[i].text = NULL;
            }
        }
        scenario->return_count = return_count;
        if (return_count > 0U)
        {
            scenario->return_codes = (botlib_contract_return_code_t *)calloc(return_count, sizeof(*scenario->return_codes));
            if (scenario->return_codes == NULL)
            {
                result = -1;
                goto cleanup;
            }
            for (size_t i = 0; i < return_count; ++i)
            {
                scenario->return_codes[i] = returns[i];
            }
        }
    }

cleanup:
    if (messages != NULL)
    {
        for (size_t i = 0; i < message_count; ++i)
        {
            free(messages[i].text);
        }
        free(messages);
    }
    free(returns);
    return result;
}

static void free_scenario(botlib_contract_scenario_t *scenario)
{
    if (scenario == NULL)
    {
        return;
    }

    free(scenario->name);
    if (scenario->messages != NULL)
    {
        for (size_t i = 0; i < scenario->message_count; ++i)
        {
            free(scenario->messages[i].text);
        }
        free(scenario->messages);
    }
    free(scenario->return_codes);
}

void BotlibContract_Free(botlib_contract_catalogue_t *catalogue)
{
    if (catalogue == NULL || catalogue->exports == NULL)
    {
        return;
    }

    for (size_t i = 0; i < catalogue->export_count; ++i)
    {
        botlib_contract_export_t *entry = &catalogue->exports[i];
        free(entry->name);
        if (entry->scenarios != NULL)
        {
            for (size_t j = 0; j < entry->scenario_count; ++j)
            {
                free_scenario(&entry->scenarios[j]);
            }
            free(entry->scenarios);
        }
    }

    free(catalogue->exports);
    catalogue->exports = NULL;
    catalogue->export_count = 0U;
}

static int parse_exports(const char *json, jsmntok_t *tokens, int index, botlib_contract_catalogue_t *catalogue)
{
    const jsmntok_t *array = &tokens[index];
    size_t count = (size_t)array->size;
    int cursor = index + 1;

    catalogue->exports = (botlib_contract_export_t *)calloc(count, sizeof(*catalogue->exports));
    if (catalogue->exports == NULL)
    {
        return -1;
    }
    catalogue->export_count = count;

    for (size_t i = 0; i < count; ++i)
    {
        const jsmntok_t *object = &tokens[cursor];
        if (object->type != JSMN_OBJECT)
        {
            return -1;
        }
        cursor += 1;

        botlib_contract_export_t *entry = &catalogue->exports[i];
        botlib_contract_message_t *messages = NULL;
        size_t message_count = 0U;
        botlib_contract_return_code_t *returns = NULL;
        size_t return_count = 0U;

		int field_count = object->size / 2;
        for (int field = 0; field < field_count; ++field)
        {
            int key_index = cursor;
            cursor = skip_token(tokens, cursor);
            int value_index = cursor;
            cursor = skip_token(tokens, cursor);

            const jsmntok_t *key = &tokens[key_index];
            const jsmntok_t *value = &tokens[value_index];
            if (key->type != JSMN_STRING)
            {
                continue;
            }

            if (token_equals(json, key, "name") && value->type == JSMN_STRING)
            {
                entry->name = duplicate_range(json, value);
                if (entry->name == NULL)
                {
                    return -1;
                }
            }
            else if (token_equals(json, key, "messages") && value->type == JSMN_ARRAY)
            {
                if (parse_messages(json, tokens, value_index, &messages, &message_count, &cursor) != 0)
                {
                    return -1;
                }
            }
            else if (token_equals(json, key, "return_codes") && value->type == JSMN_ARRAY)
            {
                if (parse_return_codes(json, tokens, value_index, &returns, &return_count, &cursor) != 0)
                {
                    return -1;
                }
            }
        }

        if (entry->name == NULL)
        {
            entry->name = duplicate_string("");
            if (entry->name == NULL)
            {
                return -1;
            }
        }

        if (build_scenarios(entry, messages, message_count, returns, return_count) != 0)
        {
            return -1;
        }
    }

    return 0;
}

/*
=============
BotlibContract_Load
=============
*/
int BotlibContract_Load(const char *path, botlib_contract_catalogue_t *catalogue)
{
	const bool debug_enabled = (getenv("BOTLIB_CONTRACT_DEBUG") != NULL);
	CONTRACT_DEBUG_LOG(debug_enabled,
	                   "BotlibContract_Load: begin '%s'\n",
	                   path != NULL ? path : "(null)");
	if (catalogue == NULL)
	{
		CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: catalogue is NULL\n");
		return -1;
	}

	memset(catalogue, 0, sizeof(*catalogue));

	botlib_contract_catalogue_t helper_catalogue;
	memset(&helper_catalogue, 0, sizeof(helper_catalogue));

	FILE *file = fopen(path, "rb");
	if (file == NULL)
	{
		CONTRACT_DEBUG_LOG(debug_enabled,
		                   "BotlibContract_Load: fopen failed for '%s' (errno %d)\n",
		                   path != NULL ? path : "(null)",
		                   errno);
		return -1;
	}

	if (fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: fseek end failed\n");
		return -1;
	}

	long length = ftell(file);
	if (length < 0)
	{
		fclose(file);
		CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: ftell failed\n");
		return -1;
	}
	CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: file length %ld\n", length);
	if (fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: fseek set failed\n");
		return -1;
	}

	char *buffer = (char *)malloc((size_t)length + 1U);
	if (buffer == NULL)
	{
		fclose(file);
		CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: malloc failed for %ld bytes\n", length + 1L);
		return -1;
	}

	size_t read_count = fread(buffer, 1U, (size_t)length, file);
	fclose(file);
	if (read_count != (size_t)length)
	{
		free(buffer);
		CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: fread failed (%zu/%ld)\n", read_count, length);
		return -1;
	}
	buffer[length] = '\0';

	jsmn_parser parser;
	jsmn_init(&parser);
	int token_count = jsmn_parse(&parser, buffer, (size_t)length, NULL, 0);
	if (token_count < 0)
	{
		free(buffer);
		CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: token count parse failed (%d)\n", token_count);
		return -1;
	}
	CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: token count %d\n", token_count);

	jsmntok_t *tokens = (jsmntok_t *)calloc((size_t)token_count, sizeof(*tokens));
	if (tokens == NULL)
	{
		free(buffer);
		CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: token allocation failed (%d)\n", token_count);
		return -1;
	}

	jsmn_init(&parser);
	int parsed = jsmn_parse(&parser, buffer, (size_t)length, tokens, (unsigned int)token_count);
	if (parsed < 0)
	{
		free(tokens);
		free(buffer);
		CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: token parse failed (%d)\n", parsed);
		return -1;
	}
	CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: token parse success (%d)\n", parsed);

	if (token_count < 1 || tokens[0].type != JSMN_OBJECT)
	{
		free(tokens);
		free(buffer);
		CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: root is not object\n");
		return -1;
	}
	CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: root size %d\n", tokens[0].size);

	int index = 1;
	int field_count = tokens[0].size / 2;
	for (int i = 0; i < field_count; ++i)
	{
		int key_index = index;
		index = skip_token(tokens, index);
		int value_index = index;
		index = skip_token(tokens, index);

		const jsmntok_t *key = &tokens[key_index];
		const jsmntok_t *value = &tokens[value_index];
		if (key->type != JSMN_STRING)
		{
			continue;
		}

		if (token_equals(buffer, key, "exports") && value->type == JSMN_ARRAY)
		{
			if (parse_exports(buffer, tokens, value_index, catalogue) != 0)
			{
				free(tokens);
				free(buffer);
				BotlibContract_Free(catalogue);
				BotlibContract_Free(&helper_catalogue);
				CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: export parse failed\n");
				return -1;
			}
		}
		else if (token_equals(buffer, key, "helpers") && value->type == JSMN_ARRAY)
		{
			if (parse_exports(buffer, tokens, value_index, &helper_catalogue) != 0)
			{
				free(tokens);
				free(buffer);
				BotlibContract_Free(catalogue);
				BotlibContract_Free(&helper_catalogue);
				CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: helper parse failed\n");
				return -1;
			}
		}
	}

	if (helper_catalogue.exports != NULL)
	{
		size_t combined = catalogue->export_count + helper_catalogue.export_count;
		botlib_contract_export_t *combined_exports =
			(botlib_contract_export_t *)calloc(combined, sizeof(*combined_exports));
		if (combined_exports == NULL)
		{
			free(tokens);
			free(buffer);
			BotlibContract_Free(catalogue);
			BotlibContract_Free(&helper_catalogue);
			CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: helper merge failed\n");
			return -1;
		}

		size_t cursor = 0U;
		for (size_t i = 0; i < catalogue->export_count; ++i)
		{
			combined_exports[cursor++] = catalogue->exports[i];
		}
		for (size_t i = 0; i < helper_catalogue.export_count; ++i)
		{
			combined_exports[cursor++] = helper_catalogue.exports[i];
		}

		free(catalogue->exports);
		free(helper_catalogue.exports);
		helper_catalogue.exports = NULL;
		helper_catalogue.export_count = 0U;
		catalogue->exports = combined_exports;
		catalogue->export_count = combined;
	}

	free(tokens);
	free(buffer);

	if (catalogue->exports == NULL)
	{
		BotlibContract_Free(catalogue);
		CONTRACT_DEBUG_LOG(debug_enabled, "BotlibContract_Load: no exports found\n");
		return -1;
	}

	CONTRACT_DEBUG_LOG(debug_enabled,
	                   "BotlibContract_Load: loaded %zu exports\n",
	                   catalogue->export_count);
	return 0;
}

/*
=============
BotlibContract_FindExport
=============
*/
const botlib_contract_export_t *BotlibContract_FindExport(const botlib_contract_catalogue_t *catalogue, const char *name)
{
	if (catalogue == NULL || name == NULL)
	{
		return NULL;
	}

	for (size_t i = 0; i < catalogue->export_count; ++i)
	{
		const botlib_contract_export_t *entry = &catalogue->exports[i];
		if (entry->name != NULL && strcmp(entry->name, name) == 0)
		{
			return entry;
		}
	}

	const char *debug_env = getenv("BOTLIB_CONTRACT_DEBUG_EXPORTS");
	if (debug_env != NULL)
	{
		fprintf(stderr,
		        "BotlibContract_FindExport: missing '%s' (have %zu exports)\n",
		        name,
		        catalogue->export_count);
		for (size_t i = 0; i < catalogue->export_count; ++i)
		{
			const botlib_contract_export_t *entry = &catalogue->exports[i];
			fprintf(stderr,
			        "  export[%zu] = '%s'\n",
			        i,
			        entry->name != NULL ? entry->name : "(null)");
		}
	}

	return NULL;
}

const botlib_contract_scenario_t *BotlibContract_FindScenario(const botlib_contract_export_t *entry, const char *scenario_name)
{
    if (entry == NULL)
    {
        return NULL;
    }

    if (scenario_name == NULL)
    {
        return entry->scenario_count > 0U ? &entry->scenarios[0] : NULL;
    }

    for (size_t i = 0; i < entry->scenario_count; ++i)
    {
        if (entry->scenarios[i].name != NULL && strcmp(entry->scenarios[i].name, scenario_name) == 0)
        {
            return &entry->scenarios[i];
        }
    }

    return NULL;
}

const botlib_contract_message_t *BotlibContract_FindMessageWithSeverity(const botlib_contract_scenario_t *scenario, int severity)
{
    if (scenario == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i < scenario->message_count; ++i)
    {
        if (scenario->messages[i].severity == severity)
        {
            return &scenario->messages[i];
        }
    }

    return NULL;
}

const botlib_contract_return_code_t *BotlibContract_FindReturnCode(const botlib_contract_scenario_t *scenario, int value)
{
    if (scenario == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i < scenario->return_count; ++i)
    {
        if (scenario->return_codes[i].value == value)
        {
            return &scenario->return_codes[i];
        }
    }

    return NULL;
}

const botlib_contract_message_t *BotlibContract_FindMessageContaining(const botlib_contract_scenario_t *scenario, const char *needle)
{
    if (scenario == NULL || needle == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i < scenario->message_count; ++i)
    {
        const botlib_contract_message_t *message = &scenario->messages[i];
        if (message->text != NULL && strstr(message->text, needle) != NULL)
        {
            return message;
        }
    }

    return NULL;
}
