#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Maximum number of weight definitions observed in the Gladiator HLIL. The
 * parser aborts once 128 entries are queued, matching the Quake III
 * implementation and the `"too many fuzzy weights"` diagnostic captured in the
 * disassembly.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L42123-L42174】
 */
#define BOTLIB_MAX_WEIGHTS 128

/**
 * Placeholder for the fuzzy separator nodes used by weapon/item weight trees.
 * The field layout mirrors Quake III’s `fuzzyseperator_t` so downstream
 * heuristics can be ported with minimal friction once the HLIL is lifted.
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
 * Runtime representation of a loaded weight configuration. The filename buffer
 * documents the origin path for debugging; its exact size will be refined once
 * filesystem helpers are restored.
 */
typedef struct bot_weight_config_s {
    int num_weights;
    bot_weight_t weights[BOTLIB_MAX_WEIGHTS];
    char source_file[260];
} bot_weight_config_t;

bot_weight_config_t *ReadWeightConfigWithDefines(const char *filename,
                                                 const char *const *global_defines,
                                                 size_t global_define_count);
bot_weight_config_t *ReadWeightConfig(const char *filename);
void FreeWeightConfig(bot_weight_config_t *config);
float FuzzyWeight(const int *inventory, const bot_weight_config_t *config, int weight_index);
int BotWeight_FindIndex(const bot_weight_config_t *config, const char *name);

#ifdef __cplusplus
} // extern "C"
#endif

