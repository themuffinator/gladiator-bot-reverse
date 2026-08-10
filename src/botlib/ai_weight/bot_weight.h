#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOTLIB_WEIGHT_TYPE_BALANCE 1
#define BOTLIB_WEIGHT_MAX_VALUE 999999

/**
 * Maximum number of parsed weight definitions. Matches the Quake III
 * MAX_WEIGHTS constant and Gladiator's "too many fuzzy weights" path.
 */
#define BOTLIB_MAX_WEIGHTS 128

/**
 * Fuzzy separator node used by item and weapon weight trees. The field order
 * mirrors Quake III's fuzzyseperator_t layout.
 */
typedef struct bot_fuzzy_seperator_s {
	int index;
	int value;
	int type;
	float weight;
	float min_weight;
	float max_weight;
	struct bot_fuzzy_seperator_s *child;
	struct bot_fuzzy_seperator_s *next;
} bot_fuzzy_seperator_t;

/**
 * Named weight entry combining the source identifier with the root fuzzy tree.
 */
typedef struct bot_weight_s {
	char *name;
	bot_fuzzy_seperator_t *first_seperator;
} bot_weight_t;

/**
 * Runtime representation of a loaded weight configuration.
 */
typedef struct bot_weight_config_s {
	int num_weights;
	bot_weight_t weights[BOTLIB_MAX_WEIGHTS];
} bot_weight_config_t;

bot_weight_config_t *ReadWeightConfigWithDefines(const char *filename,
	const char *const *global_defines,
	size_t global_define_count);
bot_weight_config_t *ReadWeightConfig(const char *filename);
bot_weight_config_t *ReadWeightConfigUncached(const char *filename);
void FreeWeightConfig(bot_weight_config_t *config);
void FreeWeightConfig2(bot_weight_config_t *config);
int WriteWeightConfig(const char *filename, bot_weight_config_t *config);
int FindFuzzyWeight(bot_weight_config_t *config, const char *name);
float FuzzyWeight(const int *inventory, const bot_weight_config_t *config, int weight_index);
float FuzzyWeightUndecided(const int *inventory, const bot_weight_config_t *config, int weight_index);
int BotWeight_FindIndex(const bot_weight_config_t *config, const char *name);
void ScaleWeight(bot_weight_config_t *config, const char *name, float scale);
void ScaleBalanceRange(bot_weight_config_t *config, float scale);
void EvolveWeightConfig(bot_weight_config_t *config);
int MergeWeightConfigs(bot_weight_config_t *config1, bot_weight_config_t *config2);
void InterbreedWeightConfigs(bot_weight_config_t *config1,
	bot_weight_config_t *config2,
	bot_weight_config_t *configout);
void BotWeight_ShutdownCachedConfigs(void);
void BotShutdownWeights(void);

int BotAllocWeightConfig(void);
void BotFreeWeightConfig(int handle);
void BotFreeWeightConfig2(bot_weight_config_t *config);
int BotLoadWeights(int handle, const char *filename);
int BotWriteWeights(int handle, const char *filename);
int BotSetWeight(int handle, const char *name, float value);
int BotFindFuzzyWeight(int handle, const char *name);
float BotFuzzyWeightHandle(int handle, const int *inventory, int weight_index);
const bot_weight_config_t *BotGetWeightConfig(int handle);

bot_weight_config_t *BotReadWeightsFile(const char *filename);

#ifdef __cplusplus
} // extern "C"
#endif

