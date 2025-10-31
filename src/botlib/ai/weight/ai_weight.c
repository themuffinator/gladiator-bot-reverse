#include "bot_weight.h"

#include "../common/l_log.h"
#include "../common/l_memory.h"
#include "../precomp/l_precomp.h"
#include "../precomp/l_script.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define WEIGHT_MAX_VALUE 999999
#define WEIGHT_TYPE_BALANCE 1

// -----------------------------------------------------------------------------
//  Forward declarations for precompiler helpers that have not been surfaced
//  through public headers yet.
// -----------------------------------------------------------------------------
int PC_ExpectAnyToken(pc_source_t *source, pc_token_t *token);
int PC_ExpectTokenString(pc_source_t *source, char *string);
int PC_ExpectTokenType(pc_source_t *source, int type, int subtype, pc_token_t *token);
int PC_CheckTokenString(pc_source_t *source, char *string);
int PC_AddGlobalDefine(char *string);
int PC_RemoveGlobalDefine(char *name);
void StripDoubleQuotes(char *string);

// -----------------------------------------------------------------------------
//  Helper structures
// -----------------------------------------------------------------------------

typedef struct bot_weight_define_scope_s {
    char **names;
    size_t count;
} bot_weight_define_scope_t;

// -----------------------------------------------------------------------------
//  Internal helpers
// -----------------------------------------------------------------------------

static bool BotWeight_ParseDefineName(const char *define, char *out_name, size_t out_size);
static bool BotWeight_PushGlobalDefines(const char *const *defines,
                                        size_t count,
                                        bot_weight_define_scope_t *scope);
static void BotWeight_PopGlobalDefines(bot_weight_define_scope_t *scope);
static void BotWeight_FreeFuzzySeperators(bot_fuzzy_seperator_t *fs);
static void BotWeight_FreeConfig(bot_weight_config_t *config);
static bool BotWeight_ReadValue(pc_source_t *source, float *value);
static bool BotWeight_ReadFuzzyWeight(pc_source_t *source, bot_fuzzy_seperator_t *fs);
static bot_fuzzy_seperator_t *BotWeight_ReadFuzzySeperators(pc_source_t *source);
static bool BotWeight_ParseWeights(pc_source_t *source, bot_weight_config_t *config);

static bool BotWeight_ParseDefineName(const char *define, char *out_name, size_t out_size)
{
    if (define == NULL || out_name == NULL || out_size == 0) {
        return false;
    }

    const char *cursor = define;
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    if (*cursor != '#') {
        return false;
    }
    cursor++;

    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    const char keyword[] = "define";
    size_t keyword_length = sizeof(keyword) - 1;
    if (strncmp(cursor, keyword, keyword_length) != 0) {
        return false;
    }
    cursor += keyword_length;

    if (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
        return false;
    }
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    size_t length = 0;
    while (*cursor != '\0' && !isspace((unsigned char)*cursor) && *cursor != '(') {
        if (length + 1 >= out_size) {
            return false;
        }
        out_name[length++] = *cursor++;
    }

    if (length == 0) {
        return false;
    }

    out_name[length] = '\0';
    return true;
}

static bool BotWeight_PushGlobalDefines(const char *const *defines,
                                        size_t count,
                                        bot_weight_define_scope_t *scope)
{
    if (scope == NULL) {
        return false;
    }

    scope->names = NULL;
    scope->count = count;

    if (defines == NULL || count == 0) {
        return true;
    }

    scope->names = GetClearedMemory(count * sizeof(char *));
    if (scope->names == NULL) {
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        const char *define = defines[i];
        if (define == NULL) {
            continue;
        }

        char name_buffer[256];
        if (!BotWeight_ParseDefineName(define, name_buffer, sizeof(name_buffer))) {
            BotLib_Print(PRT_ERROR, "invalid global define %s\n", define);
            BotWeight_PopGlobalDefines(scope);
            return false;
        }

        if (!PC_AddGlobalDefine((char *)define)) {
            BotLib_Print(PRT_ERROR, "failed to register global define %s\n", define);
            BotWeight_PopGlobalDefines(scope);
            return false;
        }

        char *name_copy = GetClearedMemory(strlen(name_buffer) + 1);
        if (name_copy == NULL) {
            BotWeight_PopGlobalDefines(scope);
            return false;
        }
        strcpy(name_copy, name_buffer);
        scope->names[i] = name_copy;
    }

    return true;
}

static void BotWeight_PopGlobalDefines(bot_weight_define_scope_t *scope)
{
    if (scope == NULL || scope->count == 0 || scope->names == NULL) {
        return;
    }

    for (size_t i = 0; i < scope->count; ++i) {
        if (scope->names[i] != NULL) {
            PC_RemoveGlobalDefine(scope->names[i]);
            FreeMemory(scope->names[i]);
        }
    }

    FreeMemory(scope->names);
    scope->names = NULL;
    scope->count = 0;
}

static void BotWeight_FreeFuzzySeperators(bot_fuzzy_seperator_t *fs)
{
    if (fs == NULL) {
        return;
    }

    BotWeight_FreeFuzzySeperators(fs->child);
    BotWeight_FreeFuzzySeperators(fs->next);
    FreeMemory(fs);
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

static bool BotWeight_ReadValue(pc_source_t *source, float *value)
{
    pc_token_t token;
    if (!PC_ExpectAnyToken(source, &token)) {
        return false;
    }

    if (strcmp(token.string, "-") == 0) {
        BotLib_Print(PRT_WARNING, "negative value set to zero\n");
        if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &token)) {
            return false;
        }
    }

    if (token.type != TT_NUMBER) {
        BotLib_Print(PRT_ERROR, "invalid return value %s\n", token.string);
        return false;
    }

    if (value != NULL) {
        *value = (float)token.floatvalue;
    }
    return true;
}

static bool BotWeight_ReadFuzzyWeight(pc_source_t *source, bot_fuzzy_seperator_t *fs)
{
    if (PC_CheckTokenString(source, "balance")) {
        fs->type = WEIGHT_TYPE_BALANCE;
        if (!PC_ExpectTokenString(source, "(")) {
            return false;
        }
        if (!BotWeight_ReadValue(source, &fs->weight)) {
            return false;
        }
        if (!PC_ExpectTokenString(source, ",")) {
            return false;
        }
        if (!BotWeight_ReadValue(source, &fs->min_weight)) {
            return false;
        }
        if (!PC_ExpectTokenString(source, ",")) {
            return false;
        }
        if (!BotWeight_ReadValue(source, &fs->max_weight)) {
            return false;
        }
        if (!PC_ExpectTokenString(source, ")")) {
            return false;
        }
    } else {
        fs->type = 0;
        if (!BotWeight_ReadValue(source, &fs->weight)) {
            return false;
        }
        fs->min_weight = fs->weight;
        fs->max_weight = fs->weight;
    }

    if (!PC_ExpectTokenString(source, ";")) {
        return false;
    }

    return true;
}

static bot_fuzzy_seperator_t *BotWeight_ReadFuzzySeperators(pc_source_t *source)
{
    if (!PC_ExpectTokenString(source, "(")) {
        return NULL;
    }

    pc_token_t token;
    if (!PC_ExpectTokenType(source, TT_NUMBER, TT_INTEGER, &token)) {
        return NULL;
    }
    int index = (int)token.intvalue;

    if (!PC_ExpectTokenString(source, ")")) {
        return NULL;
    }
    if (!PC_ExpectTokenString(source, "{")) {
        return NULL;
    }
    if (!PC_ExpectAnyToken(source, &token)) {
        return NULL;
    }

    bool found_default = false;
    bot_fuzzy_seperator_t *first = NULL;
    bot_fuzzy_seperator_t *last = NULL;

    do {
        bool is_default = strcmp(token.string, "default") == 0;
        if (!is_default && strcmp(token.string, "case") != 0) {
            BotLib_Print(PRT_ERROR, "invalid name %s\n", token.string);
            BotWeight_FreeFuzzySeperators(first);
            return NULL;
        }

        bot_fuzzy_seperator_t *fs = GetClearedMemory(sizeof(bot_fuzzy_seperator_t));
        if (fs == NULL) {
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
                BotWeight_FreeFuzzySeperators(first);
                return NULL;
            }
            fs->value = WEIGHT_MAX_VALUE;
            found_default = true;
        } else {
            if (!PC_ExpectTokenType(source, TT_NUMBER, TT_INTEGER, &token)) {
                BotWeight_FreeFuzzySeperators(first);
                return NULL;
            }
            fs->value = (int)token.intvalue;
        }

        if (!PC_ExpectTokenString(source, ":")) {
            BotWeight_FreeFuzzySeperators(first);
            return NULL;
        }
        if (!PC_ExpectAnyToken(source, &token)) {
            BotWeight_FreeFuzzySeperators(first);
            return NULL;
        }

        bool needs_closing_brace = false;
        if (strcmp(token.string, "{") == 0) {
            needs_closing_brace = true;
            if (!PC_ExpectAnyToken(source, &token)) {
                BotWeight_FreeFuzzySeperators(first);
                return NULL;
            }
        }

        if (strcmp(token.string, "return") == 0) {
            if (!BotWeight_ReadFuzzyWeight(source, fs)) {
                BotWeight_FreeFuzzySeperators(first);
                return NULL;
            }
        } else if (strcmp(token.string, "switch") == 0) {
            fs->child = BotWeight_ReadFuzzySeperators(source);
            if (fs->child == NULL) {
                BotWeight_FreeFuzzySeperators(first);
                return NULL;
            }
        } else {
            BotLib_Print(PRT_ERROR, "invalid name %s\n", token.string);
            BotWeight_FreeFuzzySeperators(first);
            return NULL;
        }

        if (needs_closing_brace) {
            if (!PC_ExpectTokenString(source, "}")) {
                BotWeight_FreeFuzzySeperators(first);
                return NULL;
            }
        }

        if (!PC_ExpectAnyToken(source, &token)) {
            BotWeight_FreeFuzzySeperators(first);
            return NULL;
        }
    } while (strcmp(token.string, "}") != 0);

    if (!found_default) {
        BotLib_Print(PRT_WARNING, "switch without default\n");
        bot_fuzzy_seperator_t *fs = GetClearedMemory(sizeof(bot_fuzzy_seperator_t));
        if (fs == NULL) {
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

static bool BotWeight_ParseWeights(pc_source_t *source, bot_weight_config_t *config)
{
    if (source == NULL || config == NULL) {
        return false;
    }

    pc_token_t token;
    while (PC_ReadToken(source, &token)) {
        if (strcmp(token.string, "weight") != 0) {
            BotLib_Print(PRT_ERROR, "invalid name %s\n", token.string);
            return false;
        }

        if (config->num_weights >= BOTLIB_MAX_WEIGHTS) {
            BotLib_Print(PRT_WARNING, "too many fuzzy weights\n");
            return true;
        }

        if (!PC_ExpectTokenType(source, TT_STRING, 0, &token)) {
            return false;
        }
        StripDoubleQuotes(token.string);

        bot_weight_t *weight = &config->weights[config->num_weights];
        bot_fuzzy_seperator_t *root = NULL;
        weight->name = NULL;
        weight->first_seperator = NULL;

        weight->name = GetClearedMemory(strlen(token.string) + 1);
        if (weight->name == NULL) {
            goto parse_failure;
        }
        strcpy(weight->name, token.string);

        if (!PC_ExpectAnyToken(source, &token)) {
            goto parse_failure;
        }

        bool requires_closing_brace = false;
        if (strcmp(token.string, "{") == 0) {
            requires_closing_brace = true;
            if (!PC_ExpectAnyToken(source, &token)) {
                goto parse_failure;
            }
        }

        if (strcmp(token.string, "switch") == 0) {
            root = BotWeight_ReadFuzzySeperators(source);
            if (root == NULL) {
                goto parse_failure;
            }
        } else if (strcmp(token.string, "return") == 0) {
            root = GetClearedMemory(sizeof(bot_fuzzy_seperator_t));
            if (root == NULL) {
                goto parse_failure;
            }
            root->index = 0;
            root->value = WEIGHT_MAX_VALUE;
            if (!BotWeight_ReadFuzzyWeight(source, root)) {
                goto parse_failure;
            }
        } else {
            BotLib_Print(PRT_ERROR, "invalid name %s\n", token.string);
            goto parse_failure;
        }

        if (requires_closing_brace) {
            if (!PC_ExpectTokenString(source, "}")) {
                goto parse_failure;
            }
        }

        weight->first_seperator = root;
        config->num_weights += 1;
        continue;

    parse_failure:
        BotWeight_FreeFuzzySeperators(root);
        if (weight->name != NULL) {
            FreeMemory(weight->name);
            weight->name = NULL;
        }
        return false;
    }

    return true;
}

bot_weight_config_t *ReadWeightConfigWithDefines(const char *filename,
                                                 const char *const *global_defines,
                                                 size_t global_define_count)
{
    if (filename == NULL) {
        return NULL;
    }

    bot_weight_define_scope_t define_scope;
    if (!BotWeight_PushGlobalDefines(global_defines, global_define_count, &define_scope)) {
        return NULL;
    }

    pc_source_t *source = PC_LoadSourceFile(filename);
    BotWeight_PopGlobalDefines(&define_scope);
    if (source == NULL) {
        BotLib_Print(PRT_ERROR, "couldn't load %s\n", filename);
        return NULL;
    }

    pc_script_t *script = PS_CreateScriptFromSource(source);
    if (script == NULL) {
        BotLib_Print(PRT_ERROR, "script wrapper failed for %s\n", filename);
        PC_FreeSource(source);
        return NULL;
    }

    bot_weight_config_t *config = GetClearedMemory(sizeof(bot_weight_config_t));
    if (config == NULL) {
        PS_FreeScript(script);
        PC_FreeSource(source);
        return NULL;
    }

    strncpy(config->source_file, filename, sizeof(config->source_file) - 1);
    config->source_file[sizeof(config->source_file) - 1] = '\0';

    bool parsed = BotWeight_ParseWeights(source, config);

    PS_FreeScript(script);
    PC_FreeSource(source);

    if (!parsed) {
        BotWeight_FreeConfig(config);
        return NULL;
    }

    return config;
}

bot_weight_config_t *ReadWeightConfig(const char *filename)
{
    return ReadWeightConfigWithDefines(filename, NULL, 0);
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

