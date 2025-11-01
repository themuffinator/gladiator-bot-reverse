#include "l_precomp.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/l_log.h"
#include "l_script.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

#ifndef QDECL
#define QDECL
#endif

struct pc_source_s {
    pc_token_t *tokens;
    size_t count;
    size_t capacity;
    size_t cursor;
    pc_token_t last_token;
    bool has_last_token;
    pc_diagnostic_t *diagnostics_head;
    pc_diagnostic_t *diagnostics_tail;
};

void QDECL SourceError(pc_source_t *source, char *str, ...);
void QDECL SourceWarning(pc_source_t *source, char *str, ...);

static void PC_ClearToken(pc_token_t *token)
{
    if (token == NULL)
    {
        return;
    }

    memset(token, 0, sizeof(*token));
    token->string[0] = '\0';
}

static bool PC_EnsureCapacity(pc_source_t *source, size_t additional)
{
    if (source == NULL)
    {
        return false;
    }

    if (source->count + additional <= source->capacity)
    {
        return true;
    }

    size_t new_capacity = (source->capacity == 0) ? 64 : source->capacity;
    while (new_capacity < source->count + additional)
    {
        new_capacity *= 2;
    }

    pc_token_t *tokens = (pc_token_t *)realloc(source->tokens, new_capacity * sizeof(pc_token_t));
    if (tokens == NULL)
    {
        return false;
    }

    source->tokens = tokens;
    source->capacity = new_capacity;
    return true;
}

static void PC_PushToken(pc_source_t *source, const pc_token_t *token)
{
    if (source == NULL || token == NULL)
    {
        return;
    }

    if (!PC_EnsureCapacity(source, 1))
    {
        return;
    }

    source->tokens[source->count++] = *token;
}

static void PC_AddDiagnostic(pc_source_t *source, pc_error_level_t level, int line, int column, const char *message)
{
    if (source == NULL || message == NULL)
    {
        return;
    }

    pc_diagnostic_t *node = (pc_diagnostic_t *)calloc(1, sizeof(*node));
    if (node == NULL)
    {
        return;
    }

    size_t length = strlen(message) + 1;
    char *copy = (char *)malloc(length);
    if (copy == NULL)
    {
        free(node);
        return;
    }
    memcpy(copy, message, length);

    node->level = level;
    node->line = line;
    node->column = column;
    node->message = copy;
    node->next = NULL;

    if (source->diagnostics_tail != NULL)
    {
        source->diagnostics_tail->next = node;
    }
    else
    {
        source->diagnostics_head = node;
    }
    source->diagnostics_tail = node;
}

static void PC_EmitPunctuation(pc_source_t *source, const char *lexeme, int line)
{
    if (lexeme == NULL)
    {
        return;
    }

    pc_token_t token;
    PC_ClearToken(&token);
    strncpy(token.string, lexeme, sizeof(token.string) - 1);
    token.type = TT_PUNCTUATION;
    token.subtype = 0;
    token.line = line;
    PC_PushToken(source, &token);
}

static bool PC_ParseNumber(const char *lexeme, int line, pc_token_t *token)
{
    if (lexeme == NULL || token == NULL)
    {
        return false;
    }

    char *end = NULL;
    long value = strtol(lexeme, &end, 0);
    if (end != NULL && *end == '\0')
    {
        PC_ClearToken(token);
        token->type = TT_NUMBER;
        token->subtype = TT_INTEGER;
        token->intvalue = (unsigned long)value;
        token->floatvalue = (long double)value;
        strncpy(token->string, lexeme, sizeof(token->string) - 1);
        token->line = line;
        return true;
    }

    double float_value = strtod(lexeme, &end);
    if (end != NULL && *end == '\0')
    {
        PC_ClearToken(token);
        token->type = TT_NUMBER;
        token->subtype = TT_FLOAT;
        token->intvalue = (unsigned long)float_value;
        token->floatvalue = (long double)float_value;
        strncpy(token->string, lexeme, sizeof(token->string) - 1);
        token->line = line;
        return true;
    }

    return false;
}

static bool PC_IsNameChar(int ch)
{
    return isalnum(ch) || ch == '_' || ch == '/' || ch == '-' || ch == '.';
}

static void PC_ParseScript(pc_source_t *source, const char *script)
{
    if (source == NULL || script == NULL)
    {
        return;
    }

    size_t length = strlen(script);
    size_t index = 0;
    int line = 1;

    while (index < length)
    {
        unsigned char ch = (unsigned char)script[index];
        if (ch == '\n')
        {
            line++;
            index++;
            continue;
        }
        if (isspace(ch))
        {
            index++;
            continue;
        }

        if (ch == '{' || ch == '}' || ch == ',' || ch == ';' || ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == ':')
        {
            char lexeme[2] = {(char)ch, '\0'};
            PC_EmitPunctuation(source, lexeme, line);
            index++;
            continue;
        }

        if (ch == '"')
        {
            size_t start = ++index;
            while (index < length && script[index] != '"')
            {
                if (script[index] == '\\' && index + 1 < length)
                {
                    index += 2;
                }
                else
                {
                    index++;
                }
            }
            size_t span = (index > start) ? (index - start) : 0;
            pc_token_t token;
            PC_ClearToken(&token);
            token.type = TT_STRING;
            token.subtype = (int)span;
            token.line = line;
            if (span >= sizeof(token.string))
            {
                span = sizeof(token.string) - 1;
            }
            memcpy(token.string, &script[start], span);
            token.string[span] = '\0';
            PC_PushToken(source, &token);
            if (index < length && script[index] == '"')
            {
                index++;
            }
            continue;
        }

        size_t start = index;
        while (index < length && PC_IsNameChar(script[index]))
        {
            index++;
        }

        if (index > start)
        {
            size_t span = index - start;
            char lexeme[1024];
            if (span >= sizeof(lexeme))
            {
                span = sizeof(lexeme) - 1;
            }
            memcpy(lexeme, &script[start], span);
            lexeme[span] = '\0';

            pc_token_t token;
            if (PC_ParseNumber(lexeme, line, &token))
            {
                PC_PushToken(source, &token);
            }
            else
            {
                PC_ClearToken(&token);
                token.type = TT_NAME;
                token.subtype = (int)span;
                token.line = line;
                strncpy(token.string, lexeme, sizeof(token.string) - 1);
                PC_PushToken(source, &token);
            }
            continue;
        }

        char lexeme[2] = {(char)ch, '\0'};
        PC_EmitPunctuation(source, lexeme, line);
        index++;
    }
}

void PC_InitLexer(void) {}

void PC_ShutdownLexer(void) {}

static pc_source_t *PC_AllocateSource(void)
{
    pc_source_t *source = (pc_source_t *)calloc(1, sizeof(pc_source_t));
    if (source == NULL)
    {
        return NULL;
    }
    return source;
}

static void PC_FreeDiagnostics(pc_source_t *source)
{
    if (source == NULL)
    {
        return;
    }

    pc_diagnostic_t *node = source->diagnostics_head;
    while (node != NULL)
    {
        pc_diagnostic_t *next = node->next;
        free((void *)node->message);
        free(node);
        node = next;
    }
    source->diagnostics_head = NULL;
    source->diagnostics_tail = NULL;
}

pc_source_t *PC_LoadSourceFile(const char *path)
{
    if (path == NULL)
    {
        return NULL;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
    {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0)
    {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return NULL;
    }

    char *buffer = (char *)malloc((size_t)size + 1);
    if (buffer == NULL)
    {
        fclose(fp);
        return NULL;
    }

    size_t read = fread(buffer, 1, (size_t)size, fp);
    fclose(fp);
    if (read != (size_t)size)
    {
        free(buffer);
        return NULL;
    }
    buffer[size] = '\0';

    pc_source_t *source = PC_LoadSourceMemory(path, buffer, (size_t)size);
    free(buffer);
    return source;
}

pc_source_t *PC_LoadSourceMemory(const char *name, const char *buffer, size_t buffer_size)
{
    (void)name;
    if (buffer == NULL)
    {
        return NULL;
    }

    char *copy = (char *)malloc(buffer_size + 1);
    if (copy == NULL)
    {
        return NULL;
    }

    memcpy(copy, buffer, buffer_size);
    copy[buffer_size] = '\0';

    pc_source_t *source = PC_AllocateSource();
    if (source == NULL)
    {
        free(copy);
        return NULL;
    }

    PC_ParseScript(source, copy);
    free(copy);

    source->cursor = 0;
    source->has_last_token = false;
    return source;
}

void PC_FreeSource(pc_source_t *source)
{
    if (source == NULL)
    {
        return;
    }

    free(source->tokens);
    PC_FreeDiagnostics(source);
    free(source);
}

static int PC_ReadRawToken(pc_source_t *source, pc_token_t *token)
{
    if (source == NULL || token == NULL)
    {
        return 0;
    }

    if (source->cursor >= source->count)
    {
        return 0;
    }

    *token = source->tokens[source->cursor++];
    return 1;
}

int PC_ReadToken(pc_source_t *source, pc_token_t *token)
{
    if (source == NULL || token == NULL)
    {
        return 0;
    }

    int status = PC_ReadRawToken(source, token);
    if (status == 1)
    {
        source->last_token = *token;
        source->has_last_token = true;
    }
    return status;
}

int PC_PeekToken(pc_source_t *source, pc_token_t *token)
{
    if (source == NULL || token == NULL)
    {
        return 0;
    }

    if (source->cursor >= source->count)
    {
        return 0;
    }

    *token = source->tokens[source->cursor];
    return 1;
}

void PC_UnreadToken(pc_source_t *source, pc_token_t *token)
{
    if (source == NULL || token == NULL || source->cursor == 0)
    {
        return;
    }

    source->cursor--;
    source->tokens[source->cursor] = *token;
    source->has_last_token = false;
}

void PC_UnreadLastToken(pc_source_t *source)
{
    if (source == NULL || !source->has_last_token || source->cursor == 0)
    {
        return;
    }

    source->cursor--;
    source->tokens[source->cursor] = source->last_token;
    source->has_last_token = false;
}

int PC_ExpectAnyToken(pc_source_t *source, pc_token_t *token)
{
    if (PC_ReadToken(source, token))
    {
        return 1;
    }

    SourceError(source, "couldn't read expected token");
    return 0;
}

int PC_ExpectTokenString(pc_source_t *source, char *string)
{
    if (source == NULL || string == NULL)
    {
        return 0;
    }

    pc_token_t token;
    if (!PC_ReadToken(source, &token))
    {
        SourceError(source, "couldn't find expected %s", string);
        return 0;
    }

    if (strcmp(token.string, string) != 0)
    {
        SourceError(source, "expected %s, found %s", string, token.string);
        return 0;
    }

    return 1;
}

int PC_CheckTokenString(pc_source_t *source, char *string)
{
    if (source == NULL || string == NULL)
    {
        return 0;
    }

    pc_token_t token;
    if (!PC_PeekToken(source, &token))
    {
        return 0;
    }

    if (strcmp(token.string, string) != 0)
    {
        return 0;
    }

    PC_ReadToken(source, &token);
    return 1;
}

int PC_ExpectTokenType(pc_source_t *source, int type, int subtype, pc_token_t *token)
{
    pc_token_t local;
    if (token == NULL)
    {
        token = &local;
    }

    if (!PC_ReadToken(source, token))
    {
        SourceError(source, "couldn't read expected token");
        return 0;
    }

    if (token->type != type)
    {
        SourceError(source, "expected a different token type");
        return 0;
    }

    if (subtype != 0 && token->subtype != subtype)
    {
        SourceError(source, "expected a different token subtype");
        return 0;
    }

    return 1;
}

const pc_diagnostic_t *PC_GetDiagnostics(const pc_source_t *source)
{
    if (source == NULL)
    {
        return NULL;
    }
    return source->diagnostics_head;
}

int PC_AddGlobalDefine(char *string)
{
    (void)string;
    return 1;
}

void StripSingleQuotes(char *string)
{
    if (string == NULL)
    {
        return;
    }

    size_t length = strlen(string);
    if (length >= 2 && string[0] == '\'' && string[length - 1] == '\'')
    {
        memmove(string, string + 1, length - 2);
        string[length - 2] = '\0';
    }
}

void StripDoubleQuotes(char *string)
{
    if (string == NULL)
    {
        return;
    }

    size_t length = strlen(string);
    if (length >= 2 && string[0] == '"' && string[length - 1] == '"')
    {
        memmove(string, string + 1, length - 2);
        string[length - 2] = '\0';
    }
}

static void PC_LogMessage(pc_source_t *source, pc_error_level_t level, const char *prefix, const char *fmt, va_list ap)
{
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, ap);

    int priority = (level == PC_ERROR_LEVEL_ERROR) ? PRT_ERROR : PRT_WARNING;
    if (prefix != NULL)
    {
        BotLib_Print(priority, "%s%s\n", prefix, buffer);
    }
    else
    {
        BotLib_Print(priority, "%s\n", buffer);
    }

    int line = 0;
    if (source != NULL && source->cursor > 0 && source->cursor <= source->count)
    {
        line = source->tokens[source->cursor - 1].line;
    }
    PC_AddDiagnostic(source, level, line, 0, buffer);
}

void QDECL SourceError(pc_source_t *source, char *str, ...)
{
    va_list ap;
    va_start(ap, str);
    PC_LogMessage(source, PC_ERROR_LEVEL_ERROR, "error: ", str, ap);
    va_end(ap);
}

void QDECL SourceWarning(pc_source_t *source, char *str, ...)
{
    va_list ap;
    va_start(ap, str);
    PC_LogMessage(source, PC_ERROR_LEVEL_WARNING, "warning: ", str, ap);
    va_end(ap);
}
