#include "bot_weight.h"

#include "botlib/common/l_assets.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/precomp/l_precomp.h"
#include "botlib/precomp/l_script.h"

#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define WEIGHT_MAX_VALUE BOTLIB_WEIGHT_MAX_VALUE
#define WEIGHT_TYPE_BALANCE BOTLIB_WEIGHT_TYPE_BALANCE
#define BOT_WEIGHT_MAX_CACHE_FILES 128

typedef struct bot_weight_cache_entry_s {
	bot_weight_config_t *config;
	char source_file[260];
} bot_weight_cache_entry_t;

static bot_weight_cache_entry_t g_weight_file_list[BOT_WEIGHT_MAX_CACHE_FILES];

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(bot_weight_config_t) == 0x404,
	"retail weight config size");
#endif

// -----------------------------------------------------------------------------
//  Forward declarations for precompiler helpers that have not been surfaced
//  through public headers yet.
// -----------------------------------------------------------------------------
int PC_ExpectAnyToken(pc_source_t *source, pc_token_t *token);
int PC_ExpectTokenString(pc_source_t *source, char *string);
int PC_ExpectTokenType(pc_source_t *source, int type, int subtype, pc_token_t *token);
int PC_CheckTokenString(pc_source_t *source, char *string);
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

static bool BotWeight_ParseDefineName(const char *define,
	char *out_name,
	size_t out_size,
	const char **out_body);
static bool BotWeight_ShouldReloadCharacters(void);
static int BotWeight_FindCachedSlot(const char *source_file);
static int BotWeight_FindFreeCachedSlot(void);
static bot_weight_config_t *BotWeight_FindCachedConfig(const char *source_file);
static bool BotWeight_ConfigIsCached(const bot_weight_config_t *config);
static void BotWeight_RemoveCachedConfig(const bot_weight_config_t *config);
static bool BotWeight_CacheConfig(bot_weight_config_t *config, const char *source_file);
static bool BotWeight_PushGlobalDefines(const char *const *defines,
	size_t count,
	bot_weight_define_scope_t *scope);
static void BotWeight_PopGlobalDefines(bot_weight_define_scope_t *scope);
static void BotWeight_FreeFuzzySeperators(bot_fuzzy_seperator_t *fs);
static void BotWeight_FreeConfig(bot_weight_config_t *config);
static bool BotWeight_ParseFloatToken(const pc_token_t *token, float *value);
static bool BotWeight_ParseIntegerToken(const pc_token_t *token, int *value);
static bool BotWeight_ReadValue(pc_source_t *source, float *value);
static bool BotWeight_ReadFuzzyWeight(pc_source_t *source, bot_fuzzy_seperator_t *fs);
static bot_fuzzy_seperator_t *BotWeight_ReadFuzzySeperators(pc_source_t *source);
static bool BotWeight_ParseWeights(pc_source_t *source, bot_weight_config_t *config);
static bot_weight_config_t *BotWeight_ReadConfig(const char *filename,
	const char *const *global_defines,
	size_t global_define_count,
	bool allow_cache);

/*
=============
BotWeight_ParseDefineName

Extracts the macro name and PC_AddGlobalDefine body from a scoped define.
=============
*/
static bool BotWeight_ParseDefineName(const char *define,
	char *out_name,
	size_t out_size,
	const char **out_body)
{
	if (define == NULL || out_name == NULL || out_size == 0) {
		return false;
	}

	const char *cursor = define;
	while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
		cursor++;
	}

	if (*cursor == '#') {
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
	}

	if (out_body != NULL) {
		*out_body = cursor;
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

/*
=============
BotWeight_ShouldReloadCharacters

Returns whether weight configs should bypass the retail cache.
=============
*/
static bool BotWeight_ShouldReloadCharacters(void)
{
	return LibVarGetValue("bot_reloadcharacters") != 0.0f;
}

/*
=============
BotWeight_FindCachedSlot

Finds a cached config by the caller's filename, matching Q3 weightFileList.
=============
*/
static int BotWeight_FindCachedSlot(const char *source_file)
{
	if (source_file == NULL || source_file[0] == '\0') {
		return -1;
	}

	for (int i = 0; i < BOT_WEIGHT_MAX_CACHE_FILES; ++i) {
		const bot_weight_cache_entry_t *entry = &g_weight_file_list[i];
		if (entry->config == NULL) {
			continue;
		}
		if (strcmp(entry->source_file, source_file) == 0) {
			return i;
		}
	}

	return -1;
}

/*
=============
BotWeight_FindFreeCachedSlot

Finds the first unused retail weight cache slot.
=============
*/
static int BotWeight_FindFreeCachedSlot(void)
{
	for (int i = 0; i < BOT_WEIGHT_MAX_CACHE_FILES; ++i) {
		if (g_weight_file_list[i].config == NULL) {
			return i;
		}
	}

	return -1;
}

/*
=============
BotWeight_FindCachedConfig

Returns the cached config for a caller filename when present.
=============
*/
static bot_weight_config_t *BotWeight_FindCachedConfig(const char *source_file)
{
	int slot = BotWeight_FindCachedSlot(source_file);
	if (slot < 0) {
		return NULL;
	}

	return g_weight_file_list[slot].config;
}

/*
=============
BotWeight_ConfigIsCached

Checks whether a config pointer is owned by the retail cache.
=============
*/
static bool BotWeight_ConfigIsCached(const bot_weight_config_t *config)
{
	if (config == NULL) {
		return false;
	}

	for (int i = 0; i < BOT_WEIGHT_MAX_CACHE_FILES; ++i) {
		if (g_weight_file_list[i].config == config) {
			return true;
		}
	}

	return false;
}

/*
=============
BotWeight_RemoveCachedConfig

Unlinks a config from the cache before a forced free.
=============
*/
static void BotWeight_RemoveCachedConfig(const bot_weight_config_t *config)
{
	if (config == NULL) {
		return;
	}

	for (int i = 0; i < BOT_WEIGHT_MAX_CACHE_FILES; ++i) {
		if (g_weight_file_list[i].config == config) {
			memset(&g_weight_file_list[i], 0, sizeof(g_weight_file_list[i]));
		}
	}
}

/*
=============
BotWeight_CacheConfig

Stores a freshly parsed config in the retail cache.
=============
*/
static bool BotWeight_CacheConfig(bot_weight_config_t *config, const char *source_file)
{
	if (config == NULL || source_file == NULL || source_file[0] == '\0') {
		return false;
	}

	int slot = BotWeight_FindCachedSlot(source_file);
	if (slot >= 0) {
		return true;
	}

	slot = BotWeight_FindFreeCachedSlot();
	if (slot < 0) {
		BotLib_Print(PRT_ERROR, "weightFileList was full trying to load %s\n", source_file);
		return false;
	}

	g_weight_file_list[slot].config = config;
	strncpy(g_weight_file_list[slot].source_file,
		source_file,
		sizeof(g_weight_file_list[slot].source_file) - 1U);
	g_weight_file_list[slot].source_file[
		sizeof(g_weight_file_list[slot].source_file) - 1U] = '\0';
	return true;
}

/*
=============
BotWeight_PushGlobalDefines

Registers temporary precompiler defines for one weight source load.
=============
*/
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
		const char *define_body = NULL;
		if (!BotWeight_ParseDefineName(define, name_buffer, sizeof(name_buffer), &define_body)) {
			BotLib_Print(PRT_ERROR, "invalid global define %s\n", define);
			BotWeight_PopGlobalDefines(scope);
			return false;
		}

		if (!PC_AddGlobalDefine(define_body)) {
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

/*
=============
BotWeight_PopGlobalDefines

Removes temporary precompiler defines registered for a weight source load.
=============
*/
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

/*
=============
BotWeight_FreeFuzzySeperators

Frees a fuzzy separator tree.
=============
*/
static void BotWeight_FreeFuzzySeperators(bot_fuzzy_seperator_t *fs)
{
	if (fs == NULL) {
		return;
	}

	BotWeight_FreeFuzzySeperators(fs->child);
	BotWeight_FreeFuzzySeperators(fs->next);
	FreeMemory(fs);
}

/*
=============
BotWeight_FreeConfig

Frees a parsed weight config and its fuzzy trees.
=============
*/
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

/*
=============
BotWeight_ParseFloatToken

Converts a numeric precompiler token to a float without relying on NUMBERVALUE.
=============
*/
static bool BotWeight_ParseFloatToken(const pc_token_t *token, float *value)
{
	if (token == NULL || token->type != TT_NUMBER) {
		return false;
	}

	char *end = NULL;
	double parsed = strtod(token->string, &end);
	if (end == token->string || (end != NULL && *end != '\0')) {
		return false;
	}

	if (value != NULL) {
		*value = (float)parsed;
	}
	return true;
}

/*
=============
BotWeight_ParseIntegerToken

Converts a numeric precompiler token to an int without relying on NUMBERVALUE.
=============
*/
static bool BotWeight_ParseIntegerToken(const pc_token_t *token, int *value)
{
	if (token == NULL || token->type != TT_NUMBER) {
		return false;
	}

	char *end = NULL;
	long parsed = strtol(token->string, &end, 0);
	if (end == token->string || (end != NULL && *end != '\0')) {
		return false;
	}

	if (value != NULL) {
		*value = (int)parsed;
	}
	return true;
}

/*
=============
BotWeight_ReadValue

Reads the numeric value used by fuzzy weights, preserving integer tokens that
the reconstructed lexer does not always mirror into floatvalue.
=============
*/
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

	float parsed_value;
	if (!BotWeight_ParseFloatToken(&token, &parsed_value)) {
		BotLib_Print(PRT_ERROR, "invalid return value %s\n", token.string);
		return false;
	}

	if (value != NULL) {
		*value = parsed_value;
	}
	return true;
}

/*
=============
BotWeight_ReadFuzzyWeight

Parses a `return` fuzzy-weight expression.
=============
*/
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

/*
=============
BotWeight_ReadFuzzySeperators

Parses a switch-case fuzzy separator chain.
=============
*/
static bot_fuzzy_seperator_t *BotWeight_ReadFuzzySeperators(pc_source_t *source)
{
	if (!PC_ExpectTokenString(source, "(")) {
		return NULL;
	}

	pc_token_t token;
	if (!PC_ExpectTokenType(source, TT_NUMBER, TT_INTEGER, &token)) {
		return NULL;
	}

	int index;
	if (!BotWeight_ParseIntegerToken(&token, &index)) {
		BotLib_Print(PRT_ERROR, "invalid switch index %s\n", token.string);
		return NULL;
	}

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
			if (!BotWeight_ParseIntegerToken(&token, &fs->value)) {
				BotLib_Print(PRT_ERROR, "invalid switch case %s\n", token.string);
				BotWeight_FreeFuzzySeperators(first);
				return NULL;
			}
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
		/* Retail appends an implicit zero-valued default separator. */
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

/*
=============
BotWeight_ParseWeights

Parses all top-level `weight` blocks from a precompiled source.
=============
*/
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
			/* Quake III and Gladiator keep the first 128 weights and stop. */
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

/*
=============
BotWeight_ReadConfig

Loads and parses a weight config, optionally participating in the Q3 cache.
=============
*/
static bot_weight_config_t *BotWeight_ReadConfig(const char *filename,
	const char *const *global_defines,
	size_t global_define_count,
	bool allow_cache)
{
	if (filename == NULL) {
		return NULL;
	}

	bool cacheable = allow_cache &&
		global_define_count == 0 &&
		global_defines == NULL &&
		!BotWeight_ShouldReloadCharacters();
	if (cacheable) {
		bot_weight_config_t *cached_config = BotWeight_FindCachedConfig(filename);
		if (cached_config != NULL) {
			return cached_config;
		}

		if (BotWeight_FindFreeCachedSlot() < 0) {
			BotLib_Print(PRT_ERROR, "weightFileList was full trying to load %s\n", filename);
			return NULL;
		}
	}

	bot_weight_define_scope_t define_scope;
	if (!BotWeight_PushGlobalDefines(global_defines, global_define_count, &define_scope)) {
		return NULL;
	}

	char resolved_path[BOTLIB_ASSET_MAX_PATH];
	if (!BotLib_ResolveAssetPath(filename, NULL, resolved_path, sizeof(resolved_path))) {
		BotWeight_PopGlobalDefines(&define_scope);
		BotLib_Print(PRT_ERROR, "couldn't find %s\n",
			resolved_path[0] != '\0' ? resolved_path : filename);
		return NULL;
	}

	pc_source_t *source = PC_LoadSourceFile(filename);
	BotWeight_PopGlobalDefines(&define_scope);
	if (source == NULL) {
		BotLib_Print(PRT_ERROR, "counldn't load %s\n", resolved_path);
		return NULL;
	}

	bot_weight_config_t *config = GetClearedMemory(sizeof(bot_weight_config_t));
	if (config == NULL) {
		PC_FreeSource(source);
		return NULL;
	}

	bool parsed = BotWeight_ParseWeights(source, config);

	PC_FreeSource(source);

	if (!parsed) {
		BotWeight_FreeConfig(config);
		return NULL;
	}

	BotLib_Print(PRT_MESSAGE, "loaded %s\n", resolved_path);

	if (cacheable && !BotWeight_CacheConfig(config, filename)) {
		BotWeight_FreeConfig(config);
		return NULL;
	}

	return config;
}

/*
=============
ReadWeightConfigWithDefines
=============
*/
bot_weight_config_t *ReadWeightConfigWithDefines(const char *filename,
	const char *const *global_defines,
	size_t global_define_count)
{
	return BotWeight_ReadConfig(filename,
		global_defines,
		global_define_count,
		true);
}

/*
=============
ReadWeightConfig
=============
*/
bot_weight_config_t *ReadWeightConfig(const char *filename)
{
	return ReadWeightConfigWithDefines(filename, NULL, 0);
}

/*
=============
ReadWeightConfigUncached

Loads a distinct retail-owned config regardless of bot_reloadcharacters.
=============
*/
bot_weight_config_t *ReadWeightConfigUncached(const char *filename)
{
	return BotWeight_ReadConfig(filename, NULL, 0, false);
}

/*
=============
FreeWeightConfig
=============
*/
void FreeWeightConfig(bot_weight_config_t *config)
{
	if (config == NULL) {
		return;
	}

	if (!BotWeight_ShouldReloadCharacters()) {
		return;
	}

	FreeWeightConfig2(config);
}

/*
=============
FreeWeightConfig2
=============
*/
void FreeWeightConfig2(bot_weight_config_t *config)
{
	if (config == NULL) {
		return;
	}

	BotWeight_RemoveCachedConfig(config);
	BotWeight_FreeConfig(config);
}

/*
=============
BotWeight_ShutdownCachedConfigs
=============
*/
void BotWeight_ShutdownCachedConfigs(void)
{
	for (int i = 0; i < BOT_WEIGHT_MAX_CACHE_FILES; ++i) {
		bot_weight_config_t *config = g_weight_file_list[i].config;
		if (config == NULL) {
			continue;
		}

		memset(&g_weight_file_list[i], 0, sizeof(g_weight_file_list[i]));
		BotWeight_FreeConfig(config);
	}
}

/*
=============
BotWeight_FuzzyWeightRecursive
=============
*/
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
			float scale = (float)((inventory_value - fs->value) / (fs->next->value - fs->value));
			return scale * w1 + (1.0f - scale) * w2;
		}
		return BotWeight_FuzzyWeightRecursive(inventory, fs->next);
	}

	return fs->weight;
}

/*
=============
BotWeight_RandomFloat
=============
*/
static float BotWeight_RandomFloat(void)
{
	return (float)(rand() & 0x7fff) / (float)0x7fff;
}

/*
=============
BotWeight_CRandom
=============
*/
static float BotWeight_CRandom(void)
{
	return BotWeight_RandomFloat() * 2.0f - 1.0f;
}

/*
=============
BotWeight_FuzzyWeightUndecidedRecursive
=============
*/
static float BotWeight_FuzzyWeightUndecidedRecursive(const int *inventory, const bot_fuzzy_seperator_t *fs)
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
			return BotWeight_FuzzyWeightUndecidedRecursive(inventory, fs->child);
		}
		return fs->min_weight + BotWeight_RandomFloat() * (fs->max_weight - fs->min_weight);
	}

	if (fs->next != NULL) {
		if (inventory_value < fs->next->value) {
			float w1 = fs->child
				? BotWeight_FuzzyWeightUndecidedRecursive(inventory, fs->child)
				: fs->min_weight + BotWeight_RandomFloat() * (fs->max_weight - fs->min_weight);
			float w2 = fs->next->child
				? BotWeight_FuzzyWeightRecursive(inventory, fs->next->child)
				: fs->next->min_weight + BotWeight_RandomFloat() * (fs->next->max_weight - fs->next->min_weight);
			float scale = (float)((inventory_value - fs->value) / (fs->next->value - fs->value));
			return scale * w1 + (1.0f - scale) * w2;
		}
		return BotWeight_FuzzyWeightUndecidedRecursive(inventory, fs->next);
	}

	return fs->weight;
}

/*
=============
FuzzyWeight
=============
*/
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

/*
=============
FuzzyWeightUndecided
=============
*/
float FuzzyWeightUndecided(const int *inventory, const bot_weight_config_t *config, int weight_index)
{
	if (config == NULL || weight_index < 0 || weight_index >= config->num_weights) {
		return 0.0f;
	}

	const bot_fuzzy_seperator_t *fs = config->weights[weight_index].first_seperator;
	if (fs == NULL) {
		return 0.0f;
	}

	return BotWeight_FuzzyWeightUndecidedRecursive(inventory, fs);
}

/*
=============
BotWeight_FindIndex
=============
*/
int BotWeight_FindIndex(const bot_weight_config_t *config, const char *name)
{
	if (config == NULL || name == NULL || name[0] == '\0') {
		return -1;
	}

	for (int i = 0; i < config->num_weights; ++i) {
		const char *entry_name = config->weights[i].name;
		if (entry_name != NULL && strcmp(entry_name, name) == 0) {
			return i;
		}
	}

	return -1;
}

/*
=============
FindFuzzyWeight
=============
*/
int FindFuzzyWeight(bot_weight_config_t *config, const char *name)
{
	return BotWeight_FindIndex(config, name);
}

/*
=============
BotWeight_EvolveFuzzySeperator
=============
*/
static void BotWeight_EvolveFuzzySeperator(bot_fuzzy_seperator_t *fs)
{
	if (fs == NULL) {
		return;
	}

	if (fs->child != NULL) {
		BotWeight_EvolveFuzzySeperator(fs->child);
	} else if (fs->type == WEIGHT_TYPE_BALANCE) {
		float range = fs->max_weight - fs->min_weight;
		if (BotWeight_RandomFloat() < 0.01f) {
			fs->weight += BotWeight_CRandom() * range;
		} else {
			fs->weight += BotWeight_CRandom() * range * 0.5f;
		}

		if (fs->weight < fs->min_weight) {
			fs->weight = fs->min_weight;
		} else if (fs->weight > fs->max_weight) {
			fs->weight = fs->max_weight;
		}
	}

	if (fs->next != NULL) {
		BotWeight_EvolveFuzzySeperator(fs->next);
	}
}

/*
=============
EvolveWeightConfig
=============
*/
void EvolveWeightConfig(bot_weight_config_t *config)
{
	if (config == NULL) {
		return;
	}

	for (int i = 0; i < config->num_weights; ++i) {
		BotWeight_EvolveFuzzySeperator(config->weights[i].first_seperator);
	}
}

/*
=============
BotWeight_ScaleFuzzySeperator
=============
*/
static void BotWeight_ScaleFuzzySeperator(bot_fuzzy_seperator_t *fs, float scale)
{
	if (fs == NULL) {
		return;
	}

	if (fs->child != NULL) {
		BotWeight_ScaleFuzzySeperator(fs->child, scale);
	} else if (fs->type == WEIGHT_TYPE_BALANCE) {
		fs->weight = (fs->max_weight + fs->min_weight) * scale;
		if (fs->weight < fs->min_weight) {
			fs->weight = fs->min_weight;
		} else if (fs->weight > fs->max_weight) {
			fs->weight = fs->max_weight;
		}
	}

	if (fs->next != NULL) {
		BotWeight_ScaleFuzzySeperator(fs->next, scale);
	}
}

/*
=============
ScaleWeight
=============
*/
void ScaleWeight(bot_weight_config_t *config, const char *name, float scale)
{
	if (config == NULL || name == NULL || name[0] == '\0') {
		return;
	}

	if (scale < 0.0f) {
		scale = 0.0f;
	} else if (scale > 1.0f) {
		scale = 1.0f;
	}

	for (int i = 0; i < config->num_weights; ++i) {
		if (config->weights[i].name == NULL) {
			continue;
		}

		if (strcmp(name, config->weights[i].name) == 0) {
			BotWeight_ScaleFuzzySeperator(config->weights[i].first_seperator, scale);
			break;
		}
	}
}

/*
=============
BotWeight_ScaleFuzzyBalanceRange
=============
*/
static void BotWeight_ScaleFuzzyBalanceRange(bot_fuzzy_seperator_t *fs, float scale)
{
	if (fs == NULL) {
		return;
	}

	if (fs->child != NULL) {
		BotWeight_ScaleFuzzyBalanceRange(fs->child, scale);
	} else if (fs->type == WEIGHT_TYPE_BALANCE) {
		float mid = (fs->min_weight + fs->max_weight) * 0.5f;
		fs->max_weight = mid + (fs->max_weight - mid) * scale;
		fs->min_weight = mid + (fs->min_weight - mid) * scale;
		if (fs->max_weight < fs->min_weight) {
			fs->max_weight = fs->min_weight;
		}
	}

	if (fs->next != NULL) {
		BotWeight_ScaleFuzzyBalanceRange(fs->next, scale);
	}
}

/*
=============
ScaleBalanceRange
=============
*/
void ScaleBalanceRange(bot_weight_config_t *config, float scale)
{
	if (config == NULL) {
		return;
	}

	if (scale < 0.0f) {
		scale = 0.0f;
	} else if (scale > 100.0f) {
		scale = 100.0f;
	}

	for (int i = 0; i < config->num_weights; ++i) {
		BotWeight_ScaleFuzzyBalanceRange(config->weights[i].first_seperator, scale);
	}
}

/*
=============
BotWeight_MergeFuzzySeperator
=============
*/
static bool BotWeight_MergeFuzzySeperator(bot_fuzzy_seperator_t *fs1,
	bot_fuzzy_seperator_t *fs2)
{
	if (fs1 == NULL || fs2 == NULL) {
		BotLib_Print(PRT_ERROR, "can't merge weight configs\n");
		return false;
	}

	for (;;) {
		if (fs1->child != NULL) {
			if (fs2->child == NULL) {
				BotLib_Print(PRT_ERROR, "can't merge weight configs\n");
				return false;
			}
			/* Retail Gladiator recurses into the second config child twice here. */
			if (!BotWeight_MergeFuzzySeperator(fs2->child, fs2->child)) {
				return false;
			}
		} else if (fs1->type == WEIGHT_TYPE_BALANCE) {
			if (fs2->type != WEIGHT_TYPE_BALANCE) {
				BotLib_Print(PRT_ERROR, "can't merge weight configs\n");
				return false;
			}
			fs1->weight = (fs2->weight + fs1->weight) * 0.5f;
		}

		fs1 = fs1->next;
		if (fs1 == NULL) {
			return true;
		}
		/* Retail Gladiator checks the second config sibling in the wrong direction. */
		if (fs2->next != NULL) {
			BotLib_Print(PRT_ERROR, "can't merge weight configs\n");
			return false;
		}
		fs2 = fs2->next;
	}
}

/*
=============
MergeWeightConfigs
=============
*/
int MergeWeightConfigs(bot_weight_config_t *config1, bot_weight_config_t *config2)
{
	if (config1 == NULL || config2 == NULL) {
		BotLib_Print(PRT_ERROR, "can't merge weight configs\n");
		return 0;
	}

	if (config1->num_weights != config2->num_weights) {
		BotLib_Print(PRT_ERROR, "can't merge weight configs\n");
		return 0;
	}

	for (int i = 0; i < config1->num_weights; ++i) {
		BotWeight_MergeFuzzySeperator(
			config1->weights[i].first_seperator,
			config2->weights[i].first_seperator);
	}

	return config1->num_weights;
}

/*
=============
BotWeight_InterbreedFuzzySeperator
=============
*/
static bool BotWeight_InterbreedFuzzySeperator(const bot_fuzzy_seperator_t *fs1,
	const bot_fuzzy_seperator_t *fs2,
	bot_fuzzy_seperator_t *fsout)
{
	if (fs1 == NULL || fs2 == NULL || fsout == NULL) {
		return false;
	}

	if (fs1->child != NULL) {
		if (fs2->child == NULL || fsout->child == NULL) {
			BotLib_Print(PRT_ERROR, "cannot interbreed weight configs, unequal child\n");
			return false;
		}
		/* Q3 carries the same child self-cross quirk used by Gladiator merge. */
		if (!BotWeight_InterbreedFuzzySeperator(fs2->child, fs2->child, fsout->child)) {
			return false;
		}
	} else if (fs1->type == WEIGHT_TYPE_BALANCE) {
		if (fs2->type != WEIGHT_TYPE_BALANCE || fsout->type != WEIGHT_TYPE_BALANCE) {
			BotLib_Print(PRT_ERROR, "cannot interbreed weight configs, unequal balance\n");
			return false;
		}
		fsout->weight = (fs1->weight + fs2->weight) / 2.0f;
		if (fsout->weight > fsout->max_weight) {
			fsout->max_weight = fsout->weight;
		}
		if (fsout->weight > fsout->min_weight) {
			fsout->min_weight = fsout->weight;
		}
	}

	if (fs1->next != NULL) {
		if (fs2->next == NULL || fsout->next == NULL) {
			BotLib_Print(PRT_ERROR, "cannot interbreed weight configs, unequal next\n");
			return false;
		}
		if (!BotWeight_InterbreedFuzzySeperator(fs1->next, fs2->next, fsout->next)) {
			return false;
		}
	}

	return true;
}

/*
=============
InterbreedWeightConfigs
=============
*/
void InterbreedWeightConfigs(bot_weight_config_t *config1,
	bot_weight_config_t *config2,
	bot_weight_config_t *configout)
{
	if (config1 == NULL || config2 == NULL || configout == NULL) {
		return;
	}

	if (config1->num_weights != config2->num_weights
		|| config1->num_weights != configout->num_weights) {
		BotLib_Print(PRT_ERROR, "cannot interbreed weight configs, unequal numweights\n");
		return;
	}

	for (int i = 0; i < config1->num_weights; ++i) {
		BotWeight_InterbreedFuzzySeperator(
			config1->weights[i].first_seperator,
			config2->weights[i].first_seperator,
			configout->weights[i].first_seperator);
	}
}
