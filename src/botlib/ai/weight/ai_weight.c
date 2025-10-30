#include "bot_weight.h"

#include "../common/l_log.h"
#include "../common/l_memory.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// -----------------------------------------------------------------------------
//  Lexer helpers
// -----------------------------------------------------------------------------

typedef enum weight_token_type_e {
    WEIGHT_TOKEN_EOF,
    WEIGHT_TOKEN_IDENTIFIER,
    WEIGHT_TOKEN_STRING,
    WEIGHT_TOKEN_NUMBER,
    WEIGHT_TOKEN_PUNCTUATION,
    WEIGHT_TOKEN_ERROR,
} weight_token_type_t;

typedef struct weight_token_s {
    weight_token_type_t type;
    char string[256];
    double number;
    char punctuation;
} weight_token_t;

typedef struct weight_lexer_s {
    const char *cursor;
    const char *end;
    int line;
    int column;
} weight_lexer_t;

typedef struct weight_parser_s {
    weight_lexer_t lexer;
    weight_token_t lookahead;
    bool has_lookahead;
    bool had_error;
} weight_parser_t;

#define WEIGHT_MAX_VALUE 999999
#define WEIGHT_TYPE_BALANCE 1

static void WeightLexer_Init(weight_lexer_t *lexer, const char *buffer, size_t length)
{
    lexer->cursor = buffer;
    lexer->end = buffer + length;
    lexer->line = 1;
    lexer->column = 1;
}

static void WeightLexer_Advance(weight_lexer_t *lexer)
{
    if (lexer->cursor >= lexer->end) {
        return;
    }

    if (*lexer->cursor == '\n') {
        lexer->line += 1;
        lexer->column = 1;
    } else {
        lexer->column += 1;
    }

    lexer->cursor += 1;
}

static void WeightLexer_SkipWhitespace(weight_lexer_t *lexer)
{
    while (lexer->cursor < lexer->end) {
        char ch = *lexer->cursor;
        if (isspace((unsigned char)ch)) {
            WeightLexer_Advance(lexer);
            continue;
        }

        if (ch == '/' && (lexer->cursor + 1) < lexer->end) {
            char next = *(lexer->cursor + 1);
            if (next == '/') {
                // Skip to the end of the line.
                lexer->cursor += 2;
                lexer->column += 2;
                while (lexer->cursor < lexer->end && *lexer->cursor != '\n') {
                    WeightLexer_Advance(lexer);
                }
                continue;
            }
            if (next == '*') {
                // Skip block comment.
                lexer->cursor += 2;
                lexer->column += 2;
                while (lexer->cursor < lexer->end) {
                    if (*lexer->cursor == '*' && (lexer->cursor + 1) < lexer->end && *(lexer->cursor + 1) == '/') {
                        lexer->cursor += 2;
                        lexer->column += 2;
                        break;
                    }
                    WeightLexer_Advance(lexer);
                }
                continue;
            }
        }

        break;
    }
}

static weight_token_t WeightLexer_ParseString(weight_lexer_t *lexer)
{
    weight_token_t token = {
        .type = WEIGHT_TOKEN_ERROR,
        .string = "",
        .number = 0.0,
        .punctuation = '\0',
    };

    WeightLexer_Advance(lexer); // Skip opening quote.
    size_t out_index = 0;
    while (lexer->cursor < lexer->end) {
        char ch = *lexer->cursor;
        if (ch == '"') {
            WeightLexer_Advance(lexer);
            token.type = WEIGHT_TOKEN_STRING;
            token.string[out_index] = '\0';
            return token;
        }
        if (ch == '\\' && (lexer->cursor + 1) < lexer->end) {
            char escaped = *(lexer->cursor + 1);
            if (escaped == '"' || escaped == '\\') {
                ch = escaped;
                lexer->cursor += 2;
                lexer->column += 2;
            } else if (escaped == 'n') {
                ch = '\n';
                lexer->cursor += 2;
                lexer->column += 2;
            } else if (escaped == 't') {
                ch = '\t';
                lexer->cursor += 2;
                lexer->column += 2;
            } else {
                lexer->cursor += 2;
                lexer->column += 2;
            }
        } else {
            WeightLexer_Advance(lexer);
        }

        if (out_index + 1 < sizeof(token.string)) {
            token.string[out_index++] = ch;
        }
    }

    token.type = WEIGHT_TOKEN_ERROR;
    return token;
}

static weight_token_t WeightLexer_ParseIdentifier(weight_lexer_t *lexer)
{
    weight_token_t token = {
        .type = WEIGHT_TOKEN_IDENTIFIER,
        .string = "",
        .number = 0.0,
        .punctuation = '\0',
    };

    size_t out_index = 0;
    while (lexer->cursor < lexer->end) {
        char ch = *lexer->cursor;
        if (!isalnum((unsigned char)ch) && ch != '_' && ch != '-') {
            break;
        }
        if (out_index + 1 < sizeof(token.string)) {
            token.string[out_index++] = ch;
        }
        WeightLexer_Advance(lexer);
    }
    token.string[out_index] = '\0';
    return token;
}

static weight_token_t WeightLexer_ParseNumber(weight_lexer_t *lexer)
{
    weight_token_t token = {
        .type = WEIGHT_TOKEN_NUMBER,
        .string = "",
        .number = 0.0,
        .punctuation = '\0',
    };

    const char *start = lexer->cursor;
    while (lexer->cursor < lexer->end) {
        char ch = *lexer->cursor;
        if (!isdigit((unsigned char)ch) && ch != '.' && ch != 'e' && ch != 'E' && ch != '+' && ch != '-') {
            break;
        }
        WeightLexer_Advance(lexer);
    }
    size_t length = (size_t)(lexer->cursor - start);
    if (length >= sizeof(token.string)) {
        length = sizeof(token.string) - 1;
    }
    memcpy(token.string, start, length);
    token.string[length] = '\0';
    token.number = strtod(token.string, NULL);
    return token;
}

static weight_token_t WeightLexer_Next(weight_lexer_t *lexer)
{
    WeightLexer_SkipWhitespace(lexer);

    weight_token_t token = {
        .type = WEIGHT_TOKEN_EOF,
        .string = "",
        .number = 0.0,
        .punctuation = '\0',
    };

    if (lexer->cursor >= lexer->end) {
        return token;
    }

    char ch = *lexer->cursor;
    if (ch == '"') {
        return WeightLexer_ParseString(lexer);
    }

    if (isalpha((unsigned char)ch) || ch == '_') {
        return WeightLexer_ParseIdentifier(lexer);
    }

    if (isdigit((unsigned char)ch) || ((ch == '+' || ch == '-') && (lexer->cursor + 1) < lexer->end && isdigit((unsigned char)*(lexer->cursor + 1)))) {
        return WeightLexer_ParseNumber(lexer);
    }

    WeightLexer_Advance(lexer);
    token.type = WEIGHT_TOKEN_PUNCTUATION;
    token.punctuation = ch;
    token.string[0] = ch;
    token.string[1] = '\0';
    return token;
}

static weight_token_t WeightParser_Next(weight_parser_t *parser)
{
    if (parser->has_lookahead) {
        parser->has_lookahead = false;
        return parser->lookahead;
    }
    return WeightLexer_Next(&parser->lexer);
}

static weight_token_t WeightParser_Peek(weight_parser_t *parser)
{
    if (!parser->has_lookahead) {
        parser->lookahead = WeightLexer_Next(&parser->lexer);
        parser->has_lookahead = true;
    }
    return parser->lookahead;
}

static bool WeightParser_ExpectPunctuation(weight_parser_t *parser, char ch)
{
    weight_token_t token = WeightParser_Next(parser);
    if (token.type == WEIGHT_TOKEN_PUNCTUATION && token.punctuation == ch) {
        return true;
    }

    BotLib_Print(PRT_ERROR, "invalid name %s\n", token.string);
    parser->had_error = true;
    return false;
}

static bool WeightParser_ReadValue(weight_parser_t *parser, float *out_value)
{
    weight_token_t token = WeightParser_Next(parser);
    if (token.type == WEIGHT_TOKEN_PUNCTUATION && token.punctuation == '-') {
        BotLib_Print(PRT_WARNING, "negative value set to zero\n");
        token = WeightParser_Next(parser);
    }

    if (token.type != WEIGHT_TOKEN_NUMBER) {
        BotLib_Print(PRT_ERROR, "invalid return value %s\n", token.string);
        parser->had_error = true;
        return false;
    }

    *out_value = (float)token.number;
    return true;
}

static bool WeightParser_ReadFuzzyWeight(weight_parser_t *parser, bot_fuzzy_seperator_t *fs)
{
    weight_token_t token = WeightParser_Peek(parser);
    if (token.type == WEIGHT_TOKEN_IDENTIFIER && strcmp(token.string, "balance") == 0) {
        (void)WeightParser_Next(parser);
        fs->type = WEIGHT_TYPE_BALANCE;
        if (!WeightParser_ExpectPunctuation(parser, '(')) {
            return false;
        }
        if (!WeightParser_ReadValue(parser, &fs->weight)) {
            return false;
        }
        if (!WeightParser_ExpectPunctuation(parser, ',')) {
            return false;
        }
        if (!WeightParser_ReadValue(parser, &fs->min_weight)) {
            return false;
        }
        if (!WeightParser_ExpectPunctuation(parser, ',')) {
            return false;
        }
        if (!WeightParser_ReadValue(parser, &fs->max_weight)) {
            return false;
        }
        if (!WeightParser_ExpectPunctuation(parser, ')')) {
            return false;
        }
    } else {
        fs->type = 0;
        if (!WeightParser_ReadValue(parser, &fs->weight)) {
            return false;
        }
        fs->min_weight = fs->weight;
        fs->max_weight = fs->weight;
    }

    if (!WeightParser_ExpectPunctuation(parser, ';')) {
        return false;
    }

    return true;
}

static void BotWeight_FreeFuzzySeperators(bot_fuzzy_seperator_t *fs)
{
    while (fs != NULL) {
        bot_fuzzy_seperator_t *next = fs->next;
        if (fs->child != NULL) {
            BotWeight_FreeFuzzySeperators(fs->child);
        }
        FreeMemory(fs);
        fs = next;
    }
}

static bot_fuzzy_seperator_t *WeightParser_ReadFuzzySeperators(weight_parser_t *parser)
{
    if (!WeightParser_ExpectPunctuation(parser, '(')) {
        return NULL;
    }

    weight_token_t token = WeightParser_Next(parser);
    if (token.type != WEIGHT_TOKEN_NUMBER) {
        BotLib_Print(PRT_ERROR, "invalid name %s\n", token.string);
        parser->had_error = true;
        return NULL;
    }
    int index = (int)token.number;

    if (!WeightParser_ExpectPunctuation(parser, ')')) {
        return NULL;
    }
    if (!WeightParser_ExpectPunctuation(parser, '{')) {
        return NULL;
    }

    bot_fuzzy_seperator_t *first = NULL;
    bot_fuzzy_seperator_t *last = NULL;
    bool found_default = false;

    token = WeightParser_Next(parser);
    while (token.type != WEIGHT_TOKEN_EOF) {
        bool is_default = false;
        if (token.type == WEIGHT_TOKEN_IDENTIFIER) {
            if (strcmp(token.string, "default") == 0) {
                is_default = true;
            } else if (strcmp(token.string, "case") != 0) {
                BotLib_Print(PRT_ERROR, "invalid name %s\n", token.string);
                parser->had_error = true;
                BotWeight_FreeFuzzySeperators(first);
                return NULL;
            }
        } else {
            BotLib_Print(PRT_ERROR, "invalid name %s\n", token.string);
            parser->had_error = true;
            BotWeight_FreeFuzzySeperators(first);
            return NULL;
        }

        bot_fuzzy_seperator_t *fs = GetClearedMemory(sizeof(bot_fuzzy_seperator_t));
        if (fs == NULL) {
            parser->had_error = true;
            BotWeight_FreeFuzzySeperators(first);
            return NULL;
        }
        fs->index = index;

        if (last != NULL) {
            last->next = fs;
        } else {
            first = fs;
        }
        last = fs;

        if (is_default) {
            if (found_default) {
                BotLib_Print(PRT_ERROR, "switch already has a default\n");
                parser->had_error = true;
                BotWeight_FreeFuzzySeperators(first);
                return NULL;
            }
            fs->value = WEIGHT_MAX_VALUE;
            found_default = true;
        } else {
            weight_token_t value_token = WeightParser_Next(parser);
            if (value_token.type != WEIGHT_TOKEN_NUMBER) {
                BotLib_Print(PRT_ERROR, "invalid name %s\n", value_token.string);
                parser->had_error = true;
                BotWeight_FreeFuzzySeperators(first);
                return NULL;
            }
            fs->value = (int)value_token.number;
        }

        if (!WeightParser_ExpectPunctuation(parser, ':')) {
            BotWeight_FreeFuzzySeperators(first);
            return NULL;
        }

        weight_token_t next_token = WeightParser_Next(parser);
        bool needs_closing_brace = false;
        if (next_token.type == WEIGHT_TOKEN_PUNCTUATION && next_token.punctuation == '{') {
            needs_closing_brace = true;
            next_token = WeightParser_Next(parser);
        }

        if (next_token.type == WEIGHT_TOKEN_IDENTIFIER && strcmp(next_token.string, "return") == 0) {
            if (!WeightParser_ReadFuzzyWeight(parser, fs)) {
                BotWeight_FreeFuzzySeperators(first);
                return NULL;
            }
        } else if (next_token.type == WEIGHT_TOKEN_IDENTIFIER && strcmp(next_token.string, "switch") == 0) {
            fs->child = WeightParser_ReadFuzzySeperators(parser);
            if (fs->child == NULL) {
                BotWeight_FreeFuzzySeperators(first);
                return NULL;
            }
        } else {
            BotLib_Print(PRT_ERROR, "invalid name %s\n", next_token.string);
            parser->had_error = true;
            BotWeight_FreeFuzzySeperators(first);
            return NULL;
        }

        if (needs_closing_brace) {
            if (!WeightParser_ExpectPunctuation(parser, '}')) {
                BotWeight_FreeFuzzySeperators(first);
                return NULL;
            }
        }

        token = WeightParser_Next(parser);
        if (token.type == WEIGHT_TOKEN_PUNCTUATION && token.punctuation == '}') {
            break;
        }
    }

    if (!found_default) {
        BotLib_Print(PRT_WARNING, "switch without default\n");
        bot_fuzzy_seperator_t *fs = GetClearedMemory(sizeof(bot_fuzzy_seperator_t));
        if (fs == NULL) {
            parser->had_error = true;
            BotWeight_FreeFuzzySeperators(first);
            return NULL;
        }
        fs->index = index;
        fs->value = WEIGHT_MAX_VALUE;
        if (last != NULL) {
            last->next = fs;
        } else {
            first = fs;
        }
    }

    return first;
}

static void BotWeight_FreeConfig(bot_weight_config_t *config)
{
    if (config == NULL) {
        return;
    }

    for (int i = 0; i < config->num_weights; ++i) {
        if (config->weights[i].first_seperator != NULL) {
            BotWeight_FreeFuzzySeperators(config->weights[i].first_seperator);
            config->weights[i].first_seperator = NULL;
        }
        if (config->weights[i].name != NULL) {
            FreeMemory(config->weights[i].name);
            config->weights[i].name = NULL;
        }
    }

    FreeMemory(config);
}

static char *BotWeight_LoadFile(const char *filename, size_t *out_size)
{
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    char *buffer = (char *)malloc((size_t)size + 1);
    if (buffer == NULL) {
        fclose(fp);
        return NULL;
    }

    size_t read_size = fread(buffer, 1, (size_t)size, fp);
    fclose(fp);
    if (read_size != (size_t)size) {
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    if (out_size != NULL) {
        *out_size = (size_t)size;
    }

    return buffer;
}

bot_weight_config_t *ReadWeightConfig(const char *filename)
{
    if (filename == NULL) {
        BotLib_Print(PRT_ERROR, "couldn't load weights\n");
        return NULL;
    }

    size_t file_size = 0;
    char *file_buffer = BotWeight_LoadFile(filename, &file_size);
    if (file_buffer == NULL) {
        BotLib_Print(PRT_ERROR, "couldn't load weights\n");
        return NULL;
    }

    bot_weight_config_t *config = GetClearedMemory(sizeof(bot_weight_config_t));
    if (config == NULL) {
        free(file_buffer);
        return NULL;
    }
    config->num_weights = 0;
    if (filename != NULL) {
        strncpy(config->source_file, filename, sizeof(config->source_file) - 1);
        config->source_file[sizeof(config->source_file) - 1] = '\0';
    }

    weight_parser_t parser;
    WeightLexer_Init(&parser.lexer, file_buffer, file_size);
    parser.has_lookahead = false;
    parser.had_error = false;

    while (true) {
        weight_token_t token = WeightParser_Next(&parser);
        if (token.type == WEIGHT_TOKEN_EOF) {
            break;
        }

        if (token.type != WEIGHT_TOKEN_IDENTIFIER || strcmp(token.string, "weight") != 0) {
            BotLib_Print(PRT_ERROR, "invalid name %s\n", token.string);
            parser.had_error = true;
            BotWeight_FreeConfig(config);
            free(file_buffer);
            return NULL;
        }

        if (config->num_weights >= BOTLIB_MAX_WEIGHTS) {
            BotLib_Print(PRT_WARNING, "too many fuzzy weights\n");
            break;
        }

        weight_token_t name_token = WeightParser_Next(&parser);
        if (name_token.type != WEIGHT_TOKEN_STRING) {
            BotLib_Print(PRT_ERROR, "invalid name %s\n", name_token.string);
            parser.had_error = true;
            BotWeight_FreeConfig(config);
            free(file_buffer);
            return NULL;
        }

        size_t name_length = strlen(name_token.string);
        config->weights[config->num_weights].name = GetClearedMemory(name_length + 1);
        if (config->weights[config->num_weights].name == NULL) {
            parser.had_error = true;
            BotWeight_FreeConfig(config);
            free(file_buffer);
            return NULL;
        }
        memcpy(config->weights[config->num_weights].name, name_token.string, name_length);

        bool needs_closing_brace = false;
        weight_token_t next = WeightParser_Next(&parser);
        if (next.type == WEIGHT_TOKEN_PUNCTUATION && next.punctuation == '{') {
            needs_closing_brace = true;
            next = WeightParser_Next(&parser);
        }

        bot_fuzzy_seperator_t *root = NULL;
        if (next.type == WEIGHT_TOKEN_IDENTIFIER && strcmp(next.string, "switch") == 0) {
            root = WeightParser_ReadFuzzySeperators(&parser);
            if (root == NULL) {
                BotWeight_FreeConfig(config);
                free(file_buffer);
                return NULL;
            }
        } else if (next.type == WEIGHT_TOKEN_IDENTIFIER && strcmp(next.string, "return") == 0) {
            bot_fuzzy_seperator_t *fs = GetClearedMemory(sizeof(bot_fuzzy_seperator_t));
            if (fs == NULL) {
                parser.had_error = true;
                BotWeight_FreeConfig(config);
                free(file_buffer);
                return NULL;
            }
            fs->index = 0;
            fs->value = WEIGHT_MAX_VALUE;
            fs->next = NULL;
            fs->child = NULL;
            if (!WeightParser_ReadFuzzyWeight(&parser, fs)) {
                FreeMemory(fs);
                BotWeight_FreeConfig(config);
                free(file_buffer);
                return NULL;
            }
            root = fs;
        } else {
            BotLib_Print(PRT_ERROR, "invalid name %s\n", next.string);
            parser.had_error = true;
            BotWeight_FreeConfig(config);
            free(file_buffer);
            return NULL;
        }

        if (needs_closing_brace) {
            if (!WeightParser_ExpectPunctuation(&parser, '}')) {
                BotWeight_FreeFuzzySeperators(root);
                BotWeight_FreeConfig(config);
                free(file_buffer);
                return NULL;
            }
        }

        config->weights[config->num_weights].first_seperator = root;
        config->num_weights += 1;
    }

    free(file_buffer);
    if (parser.had_error) {
        BotWeight_FreeConfig(config);
        return NULL;
    }

    return config;
}

void FreeWeightConfig(bot_weight_config_t *config)
{
    BotWeight_FreeConfig(config);
}

static float BotWeight_FuzzyWeightRecursive(const int *inventory, const bot_fuzzy_seperator_t *fs)
{
    if (fs == NULL) {
        return 0.0f;
    }

    int inventory_value = 0;
    if (inventory != NULL) {
        inventory_value = inventory[fs->index];
    }

    if (inventory_value < fs->value) {
        if (fs->child != NULL) {
            return BotWeight_FuzzyWeightRecursive(inventory, fs->child);
        }
        return fs->weight;
    }

    if (fs->next != NULL) {
        if (inventory_value < fs->next->value) {
            float w1 = fs->child ? BotWeight_FuzzyWeightRecursive(inventory, fs->child) : fs->weight;
            float w2 = fs->next->child ? BotWeight_FuzzyWeightRecursive(inventory, fs->next->child) : fs->next->weight;
            float denominator = (float)(fs->next->value - fs->value);
            if (denominator <= 0.0f) {
                return w2;
            }
            float scale = (float)(inventory_value - fs->value) / denominator;
            if (scale < 0.0f) {
                scale = 0.0f;
            } else if (scale > 1.0f) {
                scale = 1.0f;
            }
            return scale * w2 + (1.0f - scale) * w1;
        }
        return BotWeight_FuzzyWeightRecursive(inventory, fs->next);
    }

    return fs->weight;
}

float FuzzyWeight(const int *inventory, const bot_weight_config_t *config, int weight_index)
{
    if (config == NULL || weight_index < 0 || weight_index >= config->num_weights) {
        return 0.0f;
    }

    const bot_fuzzy_seperator_t *fs = config->weights[weight_index].first_seperator;
    if (fs == NULL) {
        return 0.0f;
    }

    return BotWeight_FuzzyWeightRecursive(inventory, fs);
}

