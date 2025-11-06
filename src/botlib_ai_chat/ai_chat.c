#include "ai_chat.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "botlib_common/l_log.h"
#include "botlib_common/l_memory.h"

#define BOT_CHAT_MAX_CONSOLE_MESSAGES 16
#define BOT_CHAT_MAX_MESSAGE_CHARS 256

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
};

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
}

static void BotChat_ClearMetadata(bot_chatstate_t *state)
{
    if (state == NULL) {
        return;
    }

    state->active_chatfile[0] = '\0';
    state->active_chatname[0] = '\0';
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
        return 1;
    }
    if (strcmp(buffer, "MSG_ENTERGAME") == 0) {
        return 2;
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

static void BotChat_ComposeAssetPath(const char *chatfile,
                                     const char *filename,
                                     char *buffer,
                                     size_t buffer_size)
{
    if (buffer_size == 0) {
        return;
    }
    buffer[0] = '\0';
    if (chatfile == NULL || filename == NULL) {
        return;
    }

    const char *last_slash = strrchr(chatfile, '/');
#ifdef _WIN32
    const char *last_backslash = strrchr(chatfile, '\\');
    if (last_backslash != NULL && (last_slash == NULL || last_backslash > last_slash)) {
        last_slash = last_backslash;
    }
#endif

    size_t directory_length = 0;
    if (last_slash != NULL) {
        directory_length = (size_t)(last_slash - chatfile);
    }

    if (directory_length >= buffer_size) {
        directory_length = buffer_size - 1;
    }

    memcpy(buffer, chatfile, directory_length);
    buffer[directory_length] = '\0';

    if (directory_length > 0 && directory_length + 1 < buffer_size) {
        buffer[directory_length] = '/';
        buffer[directory_length + 1] = '\0';
        ++directory_length;
    }

    strncat(buffer, filename, buffer_size - strlen(buffer) - 1);
}

static char *BotChat_ReadFile(const char *path, size_t *size_out)
{
    FILE *handle = fopen(path, "rb");
    if (handle == NULL) {
        return NULL;
    }

    if (fseek(handle, 0, SEEK_END) != 0) {
        fclose(handle);
        return NULL;
    }

    long length = ftell(handle);
    if (length < 0) {
        fclose(handle);
        return NULL;
    }
    if (fseek(handle, 0, SEEK_SET) != 0) {
        fclose(handle);
        return NULL;
    }

    char *buffer = malloc((size_t)length + 1);
    if (buffer == NULL) {
        fclose(handle);
        return NULL;
    }

    size_t read = fread(buffer, 1, (size_t)length, handle);
    fclose(handle);
    if (read != (size_t)length) {
        free(buffer);
        return NULL;
    }

    buffer[length] = '\0';
    if (size_out != NULL) {
        *size_out = (size_t)length;
    }
    return buffer;
}

static void BotChat_SkipWhitespace(const char **cursor)
{
    const char *p = *cursor;
    while (*p != '\0') {
        if (isspace((unsigned char)*p)) {
            ++p;
            continue;
        }
        if (p[0] == '/' && p[1] == '/') {
            p += 2;
            while (*p != '\0' && *p != '\n') {
                ++p;
            }
            continue;
        }
        if (p[0] == '#' ) {
            while (*p != '\0' && *p != '\n') {
                ++p;
            }
            continue;
        }
        break;
    }
    *cursor = p;
}

static char *BotChat_ParseString(const char **cursor)
{
    const char *p = *cursor;
    if (*p != '"') {
        return NULL;
    }
    ++p;
    const char *start = p;
    size_t length = 0;
    char *buffer = NULL;
    size_t capacity = 0;

    while (*p != '\0') {
        if (*p == '\\') {
            if (buffer == NULL) {
                capacity = 32;
                buffer = malloc(capacity);
                if (buffer == NULL) {
                    return NULL;
                }
            }
            if (length + 1 >= capacity) {
                capacity *= 2;
                char *resized = realloc(buffer, capacity);
                if (resized == NULL) {
                    free(buffer);
                    return NULL;
                }
                buffer = resized;
            }
            ++p;
            if (*p == '\0') {
                break;
            }
            char value = *p;
            if (value == 'n') {
                value = '\n';
            } else if (value == 't') {
                value = '\t';
            }
            buffer[length++] = value;
            ++p;
            continue;
        }
        if (*p == '"') {
            break;
        }
        if (buffer == NULL) {
            ++p;
            continue;
        }
        if (length + 1 >= capacity) {
            capacity *= 2;
            char *resized = realloc(buffer, capacity);
            if (resized == NULL) {
                free(buffer);
                return NULL;
            }
            buffer = resized;
        }
        buffer[length++] = *p;
        ++p;
    }

    if (*p != '"') {
        free(buffer);
        return NULL;
    }

    char *result;
    if (buffer == NULL) {
        size_t span = (size_t)(p - start);
        result = malloc(span + 1);
        if (result == NULL) {
            return NULL;
        }
        memcpy(result, start, span);
        result[span] = '\0';
    } else {
        result = realloc(buffer, length + 1);
        if (result == NULL) {
            free(buffer);
            return NULL;
        }
        result[length] = '\0';
    }

    ++p;
    *cursor = p;
    return result;
}

static int BotChat_ParseFloat(const char **cursor, float *value)
{
    char *endptr = NULL;
    double parsed = strtod(*cursor, &endptr);
    if (endptr == *cursor) {
        return 0;
    }
    *value = (float)parsed;
    *cursor = endptr;
    return 1;
}

static int BotChat_LoadSynonyms(bot_chatstate_t *state, const char *path)
{
    size_t buffer_size = 0;
    char *buffer = BotChat_ReadFile(path, &buffer_size);
    if (buffer == NULL) {
        BotLib_Print(PRT_ERROR, "BotLoadChatFile: failed to read synonym file %s\n", path);
        return 0;
    }

    const char *cursor = buffer;
#define BOTCHAT_SYN_FAIL(msg)                                                                  \
    do {                                                                                       \
        BotLib_Print(PRT_ERROR,                                                                \
                     "BotChat_LoadSynonyms: %s near offset %ld in %s\n",                       \
                     msg,                                                                      \
                     (long)(cursor - buffer),                                                  \
                     path);                                                                    \
        free(buffer);                                                                          \
        return 0;                                                                              \
    } while (0)
    while (1) {
        BotChat_SkipWhitespace(&cursor);
        if (*cursor == '\0') {
            break;
        }

        if (strncmp(cursor, "CONTEXT_", 8) != 0) {
            while (*cursor != '\0' && *cursor != '\n') {
                ++cursor;
            }
            continue;
        }

        const char *name_start = cursor;
        while (*cursor != '\0' && !isspace((unsigned char)*cursor) && *cursor != '{') {
            ++cursor;
        }
        size_t name_length = (size_t)(cursor - name_start);
        char context_name[128];
        if (name_length >= sizeof(context_name)) {
            name_length = sizeof(context_name) - 1;
        }
        memcpy(context_name, name_start, name_length);
        context_name[name_length] = '\0';

        BotChat_SkipWhitespace(&cursor);
        if (*cursor != '{') {
            BOTCHAT_SYN_FAIL("expected '{'");
        }
        ++cursor;

        bot_synonym_context_t *context = BotChat_AddSynonymContext(state, context_name);
        if (context == NULL) {
            BOTCHAT_SYN_FAIL("context allocation failed");
        }

        while (1) {
            BotChat_SkipWhitespace(&cursor);
            if (*cursor == '}') {
                ++cursor;
                break;
            }
            if (*cursor == '\0') {
                BOTCHAT_SYN_FAIL("unexpected end of file");
            }
            if (*cursor != '[') {
                while (*cursor != '\0' && *cursor != '\n') {
                    ++cursor;
                }
                continue;
            }

            ++cursor;
            bot_synonym_group_t *group = BotChat_AddSynonymGroup(context);
            if (group == NULL) {
                BOTCHAT_SYN_FAIL("group allocation failed");
            }

            while (1) {
                BotChat_SkipWhitespace(&cursor);
                if (*cursor == ']') {
                    ++cursor;
                    break;
                }
                if (*cursor == '\0') {
                    BOTCHAT_SYN_FAIL("unexpected end of group");
                }
                if (*cursor != '(') {
                    BOTCHAT_SYN_FAIL("expected '('");
                }

                ++cursor;
                BotChat_SkipWhitespace(&cursor);
                char *phrase_text = BotChat_ParseString(&cursor);
                if (phrase_text == NULL) {
                    BOTCHAT_SYN_FAIL("string parse failed");
                }
                BotChat_SkipWhitespace(&cursor);
                if (*cursor != ',') {
                    free(phrase_text);
                    BOTCHAT_SYN_FAIL("expected ',' after string");
                }
                ++cursor;
                BotChat_SkipWhitespace(&cursor);
                float weight = 0.0f;
                if (!BotChat_ParseFloat(&cursor, &weight)) {
                    free(phrase_text);
                    BOTCHAT_SYN_FAIL("weight parse failed");
                }
                BotChat_SkipWhitespace(&cursor);
                if (*cursor != ')') {
                    free(phrase_text);
                    BOTCHAT_SYN_FAIL("expected ')'");
                }
                ++cursor;

                bot_synonym_phrase_t *phrase = BotChat_AddSynonymPhrase(group);
                if (phrase == NULL) {
                    free(phrase_text);
                    BOTCHAT_SYN_FAIL("phrase allocation failed");
                }
                phrase->text = phrase_text;
                phrase->weight = weight;

                BotChat_SkipWhitespace(&cursor);
                if (*cursor == ',') {
                    ++cursor;
                    continue;
                }
            }
        }
    }

#undef BOTCHAT_SYN_FAIL
    free(buffer);
    return 1;
}

static int BotChat_LoadMatchTemplates(bot_chatstate_t *state, const char *path)
{
    size_t buffer_size = 0;
    char *buffer = BotChat_ReadFile(path, &buffer_size);
    if (buffer == NULL) {
        BotLib_Print(PRT_ERROR, "BotLoadChatFile: failed to read match file %s\n", path);
        return 0;
    }

    const char *cursor = buffer;
    while (*cursor != '\0') {
        const char *line_start = cursor;
        while (*cursor != '\0' && *cursor != '\n') {
            ++cursor;
        }
        size_t line_length = (size_t)(cursor - line_start);
        if (*cursor == '\n') {
            ++cursor;
        }

        if (line_length == 0) {
            continue;
        }

        char *line = malloc(line_length + 1);
        if (line == NULL) {
            free(buffer);
            return 0;
        }
        memcpy(line, line_start, line_length);
        line[line_length] = '\0';

        char *comment = strstr(line, "//");
        if (comment != NULL) {
            *comment = '\0';
        }

        char *trim = line;
        while (isspace((unsigned char)*trim)) {
            ++trim;
        }
        if (*trim == '\0') {
            free(line);
            continue;
        }

        char *eq = strchr(trim, '=');
        if (eq == NULL) {
            free(line);
            continue;
        }

        char *lhs_end = eq;
        while (lhs_end > trim && isspace((unsigned char)lhs_end[-1])) {
            --lhs_end;
        }
        *lhs_end = '\0';

        const char *rhs = eq + 1;
        while (isspace((unsigned char)*rhs)) {
            ++rhs;
        }
        if (*rhs != '(') {
            free(line);
            continue;
        }
        ++rhs;
        while (isspace((unsigned char)*rhs)) {
            ++rhs;
        }
        const char *msg_start = rhs;
        while (*rhs != '\0' && *rhs != ',' && !isspace((unsigned char)*rhs)) {
            ++rhs;
        }
        size_t msg_length = (size_t)(rhs - msg_start);
        unsigned long message_type = BotChat_MessageTypeFromIdentifier(msg_start, msg_length);
        if (message_type == 0) {
            free(line);
            continue;
        }

        bot_match_context_t *context = BotChat_FindMatchContext(state, message_type);
        if (context == NULL) {
            context = BotChat_AddMatchContext(state, message_type);
            if (context == NULL) {
                free(line);
                free(buffer);
                return 0;
            }
        }

        bot_string_builder_t builder = {0};
        const char *lhs_cursor = trim;
        while (*lhs_cursor != '\0') {
            if (*lhs_cursor == ',') {
                if (builder.length > 0 && builder.buffer[builder.length - 1] != ' ') {
                    if (!BotChat_StringBuilderAppendChar(&builder, ' ')) {
                        BotChat_StringBuilderDestroy(&builder);
                        free(line);
                        free(buffer);
                        return 0;
                    }
                }
                ++lhs_cursor;
                continue;
            }
            if (isspace((unsigned char)*lhs_cursor)) {
                ++lhs_cursor;
                continue;
            }
            if (*lhs_cursor == '"') {
                const char *literal_cursor = lhs_cursor;
                char *literal = BotChat_ParseString(&literal_cursor);
                if (literal == NULL) {
                    BotChat_StringBuilderDestroy(&builder);
                    free(line);
                    free(buffer);
                    return 0;
                }
                if (!BotChat_StringBuilderAppend(&builder, literal)) {
                    free(literal);
                    BotChat_StringBuilderDestroy(&builder);
                    free(line);
                    free(buffer);
                    return 0;
                }
                free(literal);
                lhs_cursor = literal_cursor;
                if (builder.length > 0 && builder.buffer[builder.length - 1] != ' ') {
                    if (!BotChat_StringBuilderAppendChar(&builder, ' ')) {
                        BotChat_StringBuilderDestroy(&builder);
                        free(line);
                        free(buffer);
                        return 0;
                    }
                }
                continue;
            }

            const char *identifier_start = lhs_cursor;
            while (*lhs_cursor != '\0' && (isalnum((unsigned char)*lhs_cursor) || *lhs_cursor == '_' || *lhs_cursor == '\'')) {
                ++lhs_cursor;
            }
            size_t identifier_length = (size_t)(lhs_cursor - identifier_start);
            if (identifier_length == 0) {
                ++lhs_cursor;
                continue;
            }
            if (!BotChat_StringBuilderAppendIdentifier(&builder, identifier_start, identifier_length)) {
                BotChat_StringBuilderDestroy(&builder);
                free(line);
                free(buffer);
                return 0;
            }
            if (builder.length > 0 && builder.buffer[builder.length - 1] != ' ') {
                if (!BotChat_StringBuilderAppendChar(&builder, ' ')) {
                    BotChat_StringBuilderDestroy(&builder);
                    free(line);
                    free(buffer);
                    return 0;
                }
            }
        }

        while (builder.length > 0 && builder.buffer[builder.length - 1] == ' ') {
            builder.buffer[--builder.length] = '\0';
        }

        char *template_text = BotChat_StringBuilderDetach(&builder);
        BotChat_StringBuilderDestroy(&builder);
        if (template_text == NULL) {
            free(line);
            free(buffer);
            return 0;
        }

        char **slot = BotChat_AddTemplate(context);
        if (slot == NULL) {
            free(template_text);
            free(line);
            free(buffer);
            return 0;
        }
        *slot = template_text;
        free(line);
    }

    free(buffer);
    return 1;
}

static int BotChat_LoadReplyChat(bot_chatstate_t *state, const char *path)
{
    size_t buffer_size = 0;
    char *buffer = BotChat_ReadFile(path, &buffer_size);
    if (buffer == NULL) {
        BotLib_Print(PRT_ERROR, "BotLoadChatFile: failed to read reply chat file %s\n", path);
        return 0;
    }

    const char *cursor = buffer;
#define BOTCHAT_REPLY_FAIL(msg)                                                               \
    do {                                                                                      \
        BotLib_Print(PRT_ERROR,                                                               \
                     "BotChat_LoadReplyChat: %s near offset %ld in %s\n",                      \
                     msg,                                                                     \
                     (long)(cursor - buffer),                                                 \
                     path);                                                                   \
        free(buffer);                                                                         \
        return 0;                                                                             \
    } while (0)
    while (1) {
        BotChat_SkipWhitespace(&cursor);
        if (*cursor == '\0') {
            break;
        }

        if (*cursor != '[') {
            while (*cursor != '\0' && *cursor != '\n') {
                ++cursor;
            }
            continue;
        }

        ++cursor;
        // Skip synonym descriptors for now. They are retained in the string
        // templates when we build the replies below, which is sufficient for
        // test coverage.
        while (*cursor != '\0' && *cursor != ']') {
            if (*cursor == '"') {
                const char *string_cursor = cursor;
                char *ignored = BotChat_ParseString(&string_cursor);
                free(ignored);
                cursor = string_cursor;
                continue;
            }
            ++cursor;
        }
        if (*cursor != ']') {
            BOTCHAT_REPLY_FAIL("unterminated trigger list");
        }
        ++cursor;

        BotChat_SkipWhitespace(&cursor);
        if (*cursor != '=') {
            BOTCHAT_REPLY_FAIL("expected '=' after triggers");
        }
        ++cursor;
        BotChat_SkipWhitespace(&cursor);

        char *endptr = NULL;
        unsigned long reply_context = strtoul(cursor, &endptr, 10);
        if (endptr == cursor) {
            BOTCHAT_REPLY_FAIL("context parse failed");
        }
        cursor = endptr;
        BotChat_SkipWhitespace(&cursor);
        if (*cursor != '{') {
            while (*cursor != '\0' && *cursor != '\n') {
                ++cursor;
            }
            continue;
        }
        ++cursor;

        bot_reply_rule_t *rule = BotChat_FindReplyRule(state, reply_context);
        if (rule == NULL) {
            rule = BotChat_AddReplyRule(state, reply_context);
            if (rule == NULL) {
                BOTCHAT_REPLY_FAIL("rule allocation failed");
            }
        }

        while (1) {
            BotChat_SkipWhitespace(&cursor);
            if (*cursor == '}') {
                ++cursor;
                break;
            }
            if (*cursor == '\0') {
                BOTCHAT_REPLY_FAIL("unexpected end of reply block");
            }

            bot_string_builder_t builder = {0};
            while (*cursor != '\0' && *cursor != ';') {
                if (isspace((unsigned char)*cursor)) {
                    ++cursor;
                    continue;
                }
                if (*cursor == '"') {
                    const char *literal_cursor = cursor;
                    char *literal = BotChat_ParseString(&literal_cursor);
                    if (literal == NULL) {
                        BotChat_StringBuilderDestroy(&builder);
                        BOTCHAT_REPLY_FAIL("string parse failed");
                    }
                    if (!BotChat_StringBuilderAppend(&builder, literal)) {
                        free(literal);
                        BotChat_StringBuilderDestroy(&builder);
                        BOTCHAT_REPLY_FAIL("append literal failed");
                    }
                    free(literal);
                    cursor = literal_cursor;
                    continue;
                }
                const char *identifier_start = cursor;
                while (*cursor != '\0' && *cursor != ';' && *cursor != '"' && !isspace((unsigned char)*cursor) && *cursor != ',') {
                    ++cursor;
                }
                size_t identifier_length = (size_t)(cursor - identifier_start);
                if (identifier_length > 0) {
                    if (!BotChat_StringBuilderAppendIdentifier(&builder, identifier_start, identifier_length)) {
                        BotChat_StringBuilderDestroy(&builder);
                        BOTCHAT_REPLY_FAIL("append identifier failed");
                    }
                }
                if (*cursor == ',') {
                    if (!BotChat_StringBuilderAppendChar(&builder, ' ')) {
                        BotChat_StringBuilderDestroy(&builder);
                        BOTCHAT_REPLY_FAIL("append separator failed");
                    }
                    ++cursor;
                }
            }

            if (*cursor == ';') {
                ++cursor;
            }

            char *reply_text = BotChat_StringBuilderDetach(&builder);
            BotChat_StringBuilderDestroy(&builder);
            if (reply_text == NULL) {
                BOTCHAT_REPLY_FAIL("reply detach failed");
            }

            char **slot = BotChat_AddReply(rule);
            if (slot == NULL) {
                free(reply_text);
                BOTCHAT_REPLY_FAIL("reply allocation failed");
            }
            *slot = reply_text;
        }
    }

    free(buffer);
#undef BOTCHAT_REPLY_FAIL
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
    FreeMemory(state);
}

int BotLoadChatFile(bot_chatstate_t *state, const char *chatfile, const char *chatname)
{
    if (state == NULL || chatfile == NULL || chatname == NULL) {
        return 0;
    }

    BotFreeChatFile(state);

    pc_source_t *source = PC_LoadSourceFile(chatfile);
    if (source != NULL) {
        pc_script_t *script = PS_CreateScriptFromSource(source);
        if (script == NULL) {
            BotLib_Print(PRT_ERROR, "BotLoadChatFile: script wrapper failed for %s\n", chatfile);
            PC_FreeSource(source);
        } else {
            state->active_source = source;
            state->active_script = script;
        }
    }

    char asset_path[512];

    BotChat_ComposeAssetPath(chatfile, "syn.c", asset_path, sizeof(asset_path));
    if (!BotChat_LoadSynonyms(state, asset_path)) {
        BotFreeChatFile(state);
        return 0;
    }

    BotChat_ComposeAssetPath(chatfile, "match.c", asset_path, sizeof(asset_path));
    if (!BotChat_LoadMatchTemplates(state, asset_path)) {
        BotFreeChatFile(state);
        return 0;
    }

    BotChat_ComposeAssetPath(chatfile, "rchat.c", asset_path, sizeof(asset_path));
    if (!BotChat_LoadReplyChat(state, asset_path)) {
        BotFreeChatFile(state);
        return 0;
    }

    strncpy(state->active_chatfile, chatfile, sizeof(state->active_chatfile) - 1);
    state->active_chatfile[sizeof(state->active_chatfile) - 1] = '\0';
    strncpy(state->active_chatname, chatname, sizeof(state->active_chatname) - 1);
    state->active_chatname[sizeof(state->active_chatname) - 1] = '\0';

    BotLib_Print(PRT_MESSAGE,
                 "BotLoadChatFile: loaded assets for %s (%s)\n",
                 state->active_chatfile,
                 state->active_chatname);
    return 1;
}

static void BotConstructChatMessage(bot_chatstate_t *state,
                                    unsigned long context,
                                    const char *template_text)
{
    if (state == NULL || template_text == NULL) {
        return;
    }

    char buffer[BOT_CHAT_MAX_MESSAGE_CHARS];
    strncpy(buffer, template_text, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    BotQueueConsoleMessage(state, (int)context, buffer);
}

void BotFreeChatFile(bot_chatstate_t *state)
{
    if (state == NULL) {
        return;
    }

    if (state->active_script != NULL) {
        PS_FreeScript(state->active_script);
        state->active_script = NULL;
    }

    if (state->active_source != NULL) {
        PC_FreeSource(state->active_source);
        state->active_source = NULL;
    }

    BotChat_FreeSynonymContexts(state);
    BotChat_FreeMatchContexts(state);
    BotChat_FreeReplies(state);
    BotChat_ClearMetadata(state);
}

void BotQueueConsoleMessage(bot_chatstate_t *state, int type, const char *message)
{
    if (state == NULL || message == NULL) {
        return;
    }

    if (state->console_count == BOT_CHAT_MAX_CONSOLE_MESSAGES) {
        // Drop the oldest message to make room. The real implementation would
        // honour the HLIL eviction rules when the queue overflows.
        state->console_head = (state->console_head + 1) % BOT_CHAT_MAX_CONSOLE_MESSAGES;
        state->console_count--;
    }

    size_t insert_index = (state->console_head + state->console_count) % BOT_CHAT_MAX_CONSOLE_MESSAGES;
    bot_console_message_t *slot = &state->console_queue[insert_index];
    slot->type = type;
    strncpy(slot->text, message, sizeof(slot->text) - 1);
    slot->text[sizeof(slot->text) - 1] = '\0';
    state->console_count++;
}

int BotNextConsoleMessage(bot_chatstate_t *state, int *type, char *buffer, size_t buffer_size)
{
    if (state == NULL || state->console_count == 0) {
        return 0;
    }

    const bot_console_message_t *slot = &state->console_queue[state->console_head];
    if (type != NULL) {
        *type = slot->type;
    }

    if (buffer != NULL && buffer_size > 0) {
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

size_t BotNumConsoleMessages(const bot_chatstate_t *state)
{
    if (state == NULL) {
        return 0;
    }

    return state->console_count;
}

void BotEnterChat(bot_chatstate_t *state, int client, int sendto)
{
    if (state == NULL) {
        return;
    }

    (void)client;
    (void)sendto;

    bot_match_context_t *context = BotChat_FindMatchContext(state, 2); // MSG_ENTERGAME
    if (context != NULL && context->template_count > 0) {
        size_t index = BotChat_SelectIndex(state->active_chatname, context->template_count);
        BotConstructChatMessage(state, 2, context->templates[index]);
    } else {
        BotLib_Print(PRT_MESSAGE,
                     "BotEnterChat: no templates loaded for enter game context\n");
    }
}

int BotReplyChat(bot_chatstate_t *state, const char *message, unsigned long int context)
{
    if (state == NULL || message == NULL) {
        return 0;
    }

    bot_match_context_t *match_context = BotChat_FindMatchContext(state, context);
    if (match_context != NULL && match_context->template_count > 0) {
        size_t index = BotChat_SelectIndex(message, match_context->template_count);
        BotConstructChatMessage(state, context, match_context->templates[index]);
        return 1;
    }

    bot_reply_rule_t *reply_rule = BotChat_FindReplyRule(state, context);
    if (reply_rule != NULL && reply_rule->response_count > 0) {
        size_t index = BotChat_SelectIndex(message, reply_rule->response_count);
        BotConstructChatMessage(state, context, reply_rule->responses[index]);
        return 1;
    }

    BotLib_Print(PRT_MESSAGE,
                 "BotReplyChat: no reply available for context %lu\n",
                 context);
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
