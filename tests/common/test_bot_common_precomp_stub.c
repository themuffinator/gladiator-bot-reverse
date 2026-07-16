#include "botlib/precomp/l_precomp.h"
#include "botlib/precomp/l_script.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

struct pc_source_s {
    pc_token_t tokens[128];
    size_t count;
    size_t cursor;
    pc_token_t last_token;
    bool has_last_token;
};

static void stub_token_clear(pc_token_t *token) {
    if (token == NULL) {
        return;
    }

    memset(token, 0, sizeof(*token));
    token->string[0] = '\0';
}

static void stub_push_token(pc_source_t *source, const pc_token_t *token) {
    if (source == NULL || token == NULL || source->count >= ARRAY_SIZE(source->tokens)) {
        return;
    }

    source->tokens[source->count++] = *token;
}

static void stub_emit_punctuation(pc_source_t *source, const char *punc, int line) {
    pc_token_t token;
    stub_token_clear(&token);
    strncpy(token.string, punc, sizeof(token.string) - 1);
    token.type = TT_PUNCTUATION;
    token.subtype = 0;
    token.line = line;
    stub_push_token(source, &token);
}

/*
=============
stub_parse_number

Mirrors the retail token-value cache used by the structure reader tests.
=============
*/
static bool stub_parse_number(const char *lexeme, pc_token_t *token)
{
	if (lexeme == NULL || token == NULL || lexeme[0] == '\0')
	{
		return false;
	}

	const char *cursor = lexeme;
	unsigned long intvalue = 0;
	long double floatvalue = 0.0;
	int subtype = 0;
	bool has_digits = false;

	if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X'))
	{
		subtype = TT_HEX;
		cursor += 2;
		while (*cursor != '\0')
		{
			unsigned int digit;
			if (*cursor >= '0' && *cursor <= '9')
			{
				digit = (unsigned int)(*cursor - '0');
			}
			else if (*cursor >= 'a' && *cursor <= 'f')
			{
				digit = (unsigned int)(*cursor - 'a' + 10);
			}
			else if (*cursor == 'A')
			{
				digit = 10;
			}
			else
			{
				return false;
			}
			intvalue = (intvalue << 4) + digit;
			has_digits = true;
			++cursor;
		}
		floatvalue = (long double)intvalue;
	}
	else if (cursor[0] == '0' && (cursor[1] == 'b' || cursor[1] == 'B'))
	{
		subtype = TT_BINARY;
		cursor += 2;
		while (*cursor != '\0')
		{
			if (*cursor != '0' && *cursor != '1')
			{
				return false;
			}
			intvalue = (intvalue << 1) + (unsigned long)(*cursor - '0');
			has_digits = true;
			++cursor;
		}
		floatvalue = (long double)intvalue;
	}
	else
	{
		bool octal = cursor[0] == '0';
		bool floating = false;
		unsigned long divisor = 0;
		while (*cursor != '\0')
		{
			if (*cursor == '.')
			{
				if (floating)
				{
					return false;
				}
				floating = true;
				divisor = 10;
				++cursor;
				continue;
			}
			if (*cursor < '0' || *cursor > '9')
			{
				return false;
			}
			if (*cursor == '8' || *cursor == '9')
			{
				octal = false;
			}
			if (floating)
			{
				floatvalue += (long double)(*cursor - '0') / divisor;
				divisor *= 10;
			}
			else
			{
				floatvalue = floatvalue * 10.0 + (*cursor - '0');
			}
			has_digits = true;
			++cursor;
		}

		if (floating)
		{
			subtype = (octal ? TT_OCTAL : TT_DECIMAL) | TT_FLOAT;
			intvalue = (unsigned long)floatvalue;
		}
		else
		{
			subtype = octal ? TT_OCTAL : TT_DECIMAL;
			const char *digits = lexeme + (octal ? 1 : 0);
			while (*digits != '\0')
			{
				intvalue = octal ?
					((intvalue << 3) + (unsigned long)(*digits - '0')) :
					(intvalue * 10 + (unsigned long)(*digits - '0'));
				++digits;
			}
			floatvalue = (long double)intvalue;
		}
	}

	if (!has_digits)
	{
		return false;
	}

	stub_token_clear(token);
	token->type = TT_NUMBER;
	token->subtype = subtype;
	if ((subtype & TT_FLOAT) == 0)
	{
		token->subtype |= TT_INTEGER;
	}
	token->intvalue = intvalue;
	token->floatvalue = floatvalue;
	strncpy(token->string, lexeme, sizeof(token->string) - 1);
	return true;
}

static void stub_parse_script(pc_source_t *source, const char *script) {
    size_t length = strlen(script);
    size_t i = 0;
    int line = 1;

    while (i < length) {
        unsigned char ch = (unsigned char)script[i];
        if (ch == '\n') {
            line++;
            i++;
            continue;
        }
        if (isspace(ch)) {
            i++;
            continue;
        }

        if (ch == '{' || ch == '}' || ch == ',') {
            char buf[2] = {(char)ch, '\0'};
            stub_emit_punctuation(source, buf, line);
            i++;
            continue;
        }

        if (ch == '"') {
            size_t start = ++i;
            while (i < length && script[i] != '"') {
                i++;
            }
            size_t span = (i > start) ? (i - start) : 0;
            pc_token_t token;
            stub_token_clear(&token);
            token.type = TT_STRING;
            token.subtype = (int)span;
            token.line = line;
            if (span >= sizeof(token.string) - 1) {
                span = sizeof(token.string) - 2;
            }
            memcpy(token.string, &script[start], span);
            token.string[span] = '\0';
            stub_push_token(source, &token);
            if (i < length && script[i] == '"') {
                i++;
            }
            continue;
        }

        size_t start = i;
        while (i < length && !isspace((unsigned char)script[i]) && script[i] != '{' && script[i] != '}' && script[i] != ',') {
            i++;
        }
        size_t span = i - start;
        if (span == 0) {
            continue;
        }

        char lexeme[128];
        if (span >= sizeof(lexeme)) {
            span = sizeof(lexeme) - 1;
        }
        memcpy(lexeme, &script[start], span);
        lexeme[span] = '\0';

        pc_token_t token;
        if (stub_parse_number(lexeme, &token)) {
            token.line = line;
            stub_push_token(source, &token);
        } else {
            stub_token_clear(&token);
            token.type = TT_NAME;
            token.subtype = (int)span;
            token.line = line;
            strncpy(token.string, lexeme, sizeof(token.string) - 1);
            stub_push_token(source, &token);
        }
    }
}

void PC_InitLexer(void) {}
void PC_ShutdownLexer(void) {}

pc_source_t *PC_LoadSourceFile(const char *path) {
    (void)path;
    return NULL;
}

pc_source_t *PC_LoadSourceMemory(const char *name, const char *buffer, size_t buffer_size) {
    (void)name;
    if (buffer == NULL) {
        return NULL;
    }

    pc_source_t *source = (pc_source_t *)calloc(1, sizeof(pc_source_t));
    if (source == NULL) {
        return NULL;
    }

    char *copy = (char *)malloc(buffer_size + 1);
    if (copy == NULL) {
        free(source);
        return NULL;
    }
    memcpy(copy, buffer, buffer_size);
    copy[buffer_size] = '\0';

    stub_parse_script(source, copy);
    free(copy);

    source->cursor = 0;
    source->has_last_token = false;
    return source;
}

void PC_FreeSource(pc_source_t *source) {
    free(source);
}

static int stub_read_raw_token(pc_source_t *source, pc_token_t *token) {
    if (source == NULL || token == NULL) {
        return 0;
    }

    if (source->cursor >= source->count) {
        return 0;
    }

    *token = source->tokens[source->cursor++];
    return 1;
}

int PC_ReadToken(pc_source_t *source, pc_token_t *token) {
    if (source == NULL || token == NULL) {
        return 0;
    }

    int status = stub_read_raw_token(source, token);
    if (status == 1) {
        source->last_token = *token;
        source->has_last_token = true;
    }
    return status;
}

int PC_PeekToken(pc_source_t *source, pc_token_t *token) {
    if (source == NULL || token == NULL) {
        return 0;
    }

    if (source->cursor >= source->count) {
        return 0;
    }

    *token = source->tokens[source->cursor];
    return 1;
}

void PC_UnreadToken(pc_source_t *source, pc_token_t *token) {
    if (source == NULL || token == NULL || source->cursor == 0) {
        return;
    }

    source->tokens[--source->cursor] = *token;
}

void PC_UnreadLastToken(pc_source_t *source) {
    if (source == NULL || !source->has_last_token || source->cursor == 0) {
        return;
    }

    source->tokens[--source->cursor] = source->last_token;
    source->has_last_token = false;
}

int PC_ExpectAnyToken(pc_source_t *source, pc_token_t *token) {
    return PC_ReadToken(source, token);
}

int PC_ExpectTokenString(pc_source_t *source, char *string) {
    pc_token_t token;
    if (!PC_ReadToken(source, &token)) {
        return 0;
    }

    if (strcmp(token.string, string) != 0) {
        PC_UnreadToken(source, &token);
        return 0;
    }

    return 1;
}

int PC_ExpectTokenType(pc_source_t *source, int type, int subtype, pc_token_t *token) {
    if (!PC_ReadToken(source, token)) {
        return 0;
    }

    if (token->type != type) {
        PC_UnreadToken(source, token);
        return 0;
    }

    if (subtype != 0 && token->subtype != subtype) {
        PC_UnreadToken(source, token);
        return 0;
    }

    return 1;
}

int PC_CheckTokenString(pc_source_t *source, char *string) {
    pc_token_t token;
    if (!PC_ReadToken(source, &token)) {
        return 0;
    }

    if (strcmp(token.string, string) == 0) {
        return 1;
    }

    PC_UnreadToken(source, &token);
    return 0;
}

const pc_diagnostic_t *PC_GetDiagnostics(const pc_source_t *source) {
    (void)source;
    return NULL;
}

void SourceError(pc_source_t *source, char *fmt, ...) {
    (void)source;
    (void)fmt;
}

void SourceWarning(pc_source_t *source, char *fmt, ...) {
    (void)source;
    (void)fmt;
}

void StripDoubleQuotes(char *string) {
    if (string == NULL) {
        return;
    }

    size_t length = strlen(string);
    if (length >= 2 && string[0] == '"' && string[length - 1] == '"') {
        memmove(string, string + 1, length - 1);
        string[length - 2] = '\0';
    }
}

void StripSingleQuotes(char *string) {
    if (string == NULL) {
        return;
    }

    size_t length = strlen(string);
    if (length >= 2 && string[0] == '\'' && string[length - 1] == '\'') {
        memmove(string, string + 1, length - 1);
        string[length - 2] = '\0';
    }
}
