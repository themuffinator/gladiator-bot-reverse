#include "botlib_contract_loader.h"

#include "jsmn.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAILURE_SEVERITY_THRESHOLD 3

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
    return copy;
}

static bool token_equals(const char *json, const jsmntok_t *token, const char *text)
{
    size_t length = (size_t)(token->end - token->start);
    return (strlen(text) == length) && (strncmp(json + token->start, text, length) == 0);
}

static int skip_token(const jsmntok_t *tokens, int index)
{
    const jsmntok_t *token = &tokens[index];
    int next = index + 1;

    switch (token->type)
    {
        case JSMN_OBJECT:
            for (int i = 0; i < token->size; ++i)
            {
                next = skip_token(tokens, next);
                next = skip_token(tokens, next);
            }
            break;
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
        for (int field = 0; field < object->size; ++field)
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
        for (int field = 0; field < object->size; ++field)
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
            for (size_t i = 0; i < return_count; ++i)
            {
                if (returns[i].value != 0)
                {
                    scenario->return_codes[cursor++] = returns[i];
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
            for (size_t i = 0; i < return_count; ++i)
            {
                if (returns[i].value == 0)
                {
                    scenario->return_codes[cursor++] = returns[i];
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

        for (int field = 0; field < object->size; ++field)
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

int BotlibContract_Load(const char *path, botlib_contract_catalogue_t *catalogue)
{
    if (catalogue == NULL)
    {
        return -1;
    }

    memset(catalogue, 0, sizeof(*catalogue));

    FILE *file = fopen(path, "rb");
    if (file == NULL)
    {
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return -1;
    }

    long length = ftell(file);
    if (length < 0)
    {
        fclose(file);
        return -1;
    }
    if (fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return -1;
    }

    char *buffer = (char *)malloc((size_t)length + 1U);
    if (buffer == NULL)
    {
        fclose(file);
        return -1;
    }

    size_t read_count = fread(buffer, 1U, (size_t)length, file);
    fclose(file);
    if (read_count != (size_t)length)
    {
        free(buffer);
        return -1;
    }
    buffer[length] = '\0';

    jsmn_parser parser;
    jsmn_init(&parser);
    int token_count = jsmn_parse(&parser, buffer, (size_t)length, NULL, 0);
    if (token_count < 0)
    {
        free(buffer);
        return -1;
    }

    jsmntok_t *tokens = (jsmntok_t *)calloc((size_t)token_count, sizeof(*tokens));
    if (tokens == NULL)
    {
        free(buffer);
        return -1;
    }

    jsmn_init(&parser);
    int parsed = jsmn_parse(&parser, buffer, (size_t)length, tokens, (unsigned int)token_count);
    if (parsed < 0)
    {
        free(tokens);
        free(buffer);
        return -1;
    }

    if (token_count < 1 || tokens[0].type != JSMN_OBJECT)
    {
        free(tokens);
        free(buffer);
        return -1;
    }

    int index = 1;
    for (int i = 0; i < tokens[0].size; ++i)
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
                return -1;
            }
        }
    }

    free(tokens);
    free(buffer);

    if (catalogue->exports == NULL)
    {
        BotlibContract_Free(catalogue);
        return -1;
    }

    return 0;
}

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
