#include "bot_weight.h"

#include "botlib_common/l_assets.h"
#include "botlib_common/l_log.h"
#include "botlib_common/l_memory.h"
#include "botlib_common/l_struct.h"

#include <stdio.h>
#include <stdbool.h>

#ifndef WEIGHT_TYPE_BALANCE
#define WEIGHT_TYPE_BALANCE 1
#endif

#define BOT_WEIGHT_MAX_HANDLES 32

typedef struct bot_weight_handle_s {
    bot_weight_config_t *config;
} bot_weight_handle_t;

static bot_weight_handle_t *g_weight_handles[BOT_WEIGHT_MAX_HANDLES];

static bool BotWeight_HandleInRange(int handle)
{
    return handle > 0 && handle <= BOT_WEIGHT_MAX_HANDLES;
}

static bot_weight_handle_t *BotWeight_HandleEntry(int handle)
{
    if (!BotWeight_HandleInRange(handle)) {
        return NULL;
    }
    return g_weight_handles[handle - 1];
}

static void BotWeight_DestroyHandle(bot_weight_handle_t *entry)
{
    if (entry == NULL) {
        return;
    }

    if (entry->config != NULL) {
        FreeWeightConfig(entry->config);
        entry->config = NULL;
    }

    FreeMemory(entry);
}

static void BotWeight_LogHandleError(const char *function, int handle)
{
    BotLib_Print(PRT_ERROR, "%s: invalid weight handle %d\n", function, handle);
}

int BotAllocWeightConfig(void)
{
    for (int i = 0; i < BOT_WEIGHT_MAX_HANDLES; ++i) {
        if (g_weight_handles[i] != NULL) {
            continue;
        }

        bot_weight_handle_t *entry = GetClearedMemory(sizeof(*entry));
        if (entry == NULL) {
            BotLib_Print(PRT_FATAL, "BotAllocWeightConfig: allocation failed\n");
            return 0;
        }

        g_weight_handles[i] = entry;
        return i + 1;
    }

    BotLib_Print(PRT_ERROR, "BotAllocWeightConfig: no free handles\n");
    return 0;
}

void BotFreeWeightConfig(int handle)
{
    if (!BotWeight_HandleInRange(handle)) {
        BotWeight_LogHandleError("BotFreeWeightConfig", handle);
        return;
    }

    bot_weight_handle_t *entry = g_weight_handles[handle - 1];
    if (entry == NULL) {
        BotWeight_LogHandleError("BotFreeWeightConfig", handle);
        return;
    }

    BotWeight_DestroyHandle(entry);
    g_weight_handles[handle - 1] = NULL;
}

void BotFreeWeightConfig2(bot_weight_config_t *config)
{
    FreeWeightConfig(config);
}

static bot_weight_handle_t *BotWeight_ResolveHandle(const char *function, int handle)
{
    bot_weight_handle_t *entry = BotWeight_HandleEntry(handle);
    if (entry == NULL) {
        BotWeight_LogHandleError(function, handle);
        return NULL;
    }
    return entry;
}

int BotLoadWeights(int handle, const char *filename)
{
    if (filename == NULL || filename[0] == '\0') {
        BotLib_Print(PRT_ERROR, "BotLoadWeights: filename required\n");
        return 0;
    }

    bot_weight_handle_t *entry = BotWeight_ResolveHandle("BotLoadWeights", handle);
    if (entry == NULL) {
        return 0;
    }

    bot_weight_config_t *config = ReadWeightConfig(filename);
    if (config == NULL) {
        BotLib_Print(PRT_FATAL, "couldn't load weights\n");
        return 0;
    }

    if (entry->config != NULL) {
        FreeWeightConfig(entry->config);
    }

    entry->config = config;
    return 1;
}

static bool BotWeight_WriteFuzzyWeight(FILE *fp, const bot_fuzzy_seperator_t *fs)
{
    if (fp == NULL || fs == NULL) {
        return false;
    }

    if (fs->type == WEIGHT_TYPE_BALANCE) {
        if (fprintf(fp, " return balance(") < 0) {
            return false;
        }
        if (!WriteFloat(fp, fs->weight) || fprintf(fp, ",") < 0) {
            return false;
        }
        if (!WriteFloat(fp, fs->min_weight) || fprintf(fp, ",") < 0) {
            return false;
        }
        if (!WriteFloat(fp, fs->max_weight) || fprintf(fp, ");\n") < 0) {
            return false;
        }
    } else {
        if (fprintf(fp, " return ") < 0) {
            return false;
        }
        if (!WriteFloat(fp, fs->weight) || fprintf(fp, ";\n") < 0) {
            return false;
        }
    }

    return true;
}

static bool BotWeight_WriteFuzzySeperators(FILE *fp, const bot_fuzzy_seperator_t *fs, int indent)
{
    if (fp == NULL || fs == NULL) {
        return false;
    }

    if (!WriteIndent(fp, indent) || fprintf(fp, "switch(%d)\n", fs->index) < 0) {
        return false;
    }
    if (!WriteIndent(fp, indent) || fprintf(fp, "{\n") < 0) {
        return false;
    }

    indent += 1;

    const bot_fuzzy_seperator_t *cursor = fs;
    while (cursor != NULL) {
        if (!WriteIndent(fp, indent)) {
            return false;
        }

        if (cursor->next != NULL) {
            if (fprintf(fp, "case %d:", cursor->value) < 0) {
                return false;
            }
        } else {
            if (fprintf(fp, "default:") < 0) {
                return false;
            }
        }

        if (cursor->child != NULL) {
            if (fprintf(fp, "\n") < 0) {
                return false;
            }
            if (!WriteIndent(fp, indent) || fprintf(fp, "{\n") < 0) {
                return false;
            }
            if (!BotWeight_WriteFuzzySeperators(fp, cursor->child, indent + 1)) {
                return false;
            }
            if (!WriteIndent(fp, indent)) {
                return false;
            }
            if (cursor->next != NULL) {
                if (fprintf(fp, "} //end case\n") < 0) {
                    return false;
                }
            } else {
                if (fprintf(fp, "} //end default\n") < 0) {
                    return false;
                }
            }
        } else {
            if (!BotWeight_WriteFuzzyWeight(fp, cursor)) {
                return false;
            }
        }

        cursor = cursor->next;
    }

    indent -= 1;
    if (!WriteIndent(fp, indent) || fprintf(fp, "} //end switch\n") < 0) {
        return false;
    }

    return true;
}

static bool BotWeight_WriteConfig(FILE *fp, const bot_weight_config_t *config)
{
    if (fp == NULL || config == NULL) {
        return false;
    }

    for (int i = 0; i < config->num_weights; ++i) {
        const bot_weight_t *weight = &config->weights[i];
        if (weight->name == NULL || weight->first_seperator == NULL) {
            return false;
        }

        if (fprintf(fp, "\nweight \"%s\"\n", weight->name) < 0) {
            return false;
        }
        if (fprintf(fp, "{\n") < 0) {
            return false;
        }

        if (weight->first_seperator->index > 0) {
            if (!BotWeight_WriteFuzzySeperators(fp, weight->first_seperator, 1)) {
                return false;
            }
        } else {
            if (!WriteIndent(fp, 1) || !BotWeight_WriteFuzzyWeight(fp, weight->first_seperator)) {
                return false;
            }
        }

        if (fprintf(fp, "} //end weight\n") < 0) {
            return false;
        }
    }

    return true;
}

int BotWriteWeights(int handle, const char *filename)
{
    if (filename == NULL || filename[0] == '\0') {
        BotLib_Print(PRT_ERROR, "BotWriteWeights: filename required\n");
        return 0;
    }

    bot_weight_handle_t *entry = BotWeight_ResolveHandle("BotWriteWeights", handle);
    if (entry == NULL) {
        return 0;
    }

    if (entry->config == NULL) {
        BotLib_Print(PRT_ERROR, "BotWriteWeights: handle %d has no configuration\n", handle);
        return 0;
    }

    char resolved_path[BOTLIB_ASSET_MAX_PATH];
    BotLib_ResolveAssetPath(filename, NULL, resolved_path, sizeof(resolved_path));

    const char *target_path = resolved_path[0] != '\0' ? resolved_path : filename;

    FILE *fp = fopen(target_path, "wb");
    if (fp == NULL) {
        BotLib_Print(PRT_ERROR, "couldn't write %s\n", target_path);
        return 0;
    }

    bool wrote = BotWeight_WriteConfig(fp, entry->config);
    if (fclose(fp) != 0) {
        wrote = false;
    }

    if (!wrote) {
        BotLib_Print(PRT_ERROR, "couldn't write %s\n", target_path);
        return 0;
    }

    BotLib_Print(PRT_MESSAGE, "%s written succesfully\n", target_path);
    return 1;
}

static void BotWeight_AssignValue(bot_fuzzy_seperator_t *fs, float value)
{
    if (fs == NULL) {
        return;
    }

    fs->weight = value;
    fs->min_weight = value;
    fs->max_weight = value;

    BotWeight_AssignValue(fs->child, value);
    BotWeight_AssignValue(fs->next, value);
}

int BotSetWeight(int handle, const char *name, float value)
{
    if (name == NULL || name[0] == '\0') {
        BotLib_Print(PRT_ERROR, "BotSetWeight: name required\n");
        return 0;
    }

    bot_weight_handle_t *entry = BotWeight_ResolveHandle("BotSetWeight", handle);
    if (entry == NULL) {
        return 0;
    }

    if (entry->config == NULL) {
        BotLib_Print(PRT_ERROR, "BotSetWeight: handle %d has no configuration\n", handle);
        return 0;
    }

    int index = BotWeight_FindIndex(entry->config, name);
    if (index < 0) {
        BotLib_Print(PRT_WARNING,
                     "BotSetWeight: unknown weight '%s' in %s\n",
                     name,
                     entry->config->source_file);
        return 0;
    }

    bot_weight_t *weight = &entry->config->weights[index];
    BotWeight_AssignValue(weight->first_seperator, value);
    return 1;
}

int BotFindFuzzyWeight(int handle, const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return -1;
    }

    const bot_weight_handle_t *entry = BotWeight_HandleEntry(handle);
    if (entry == NULL || entry->config == NULL) {
        return -1;
    }

    return BotWeight_FindIndex(entry->config, name);
}

float BotFuzzyWeightHandle(int handle, const int *inventory, int weight_index)
{
    const bot_weight_handle_t *entry = BotWeight_HandleEntry(handle);
    if (entry == NULL || entry->config == NULL) {
        return 0.0f;
    }

    return FuzzyWeight(inventory, entry->config, weight_index);
}

const bot_weight_config_t *BotGetWeightConfig(int handle)
{
    const bot_weight_handle_t *entry = BotWeight_HandleEntry(handle);
    if (entry == NULL) {
        return NULL;
    }

    return entry->config;
}

bot_weight_config_t *BotReadWeightsFile(const char *filename)
{
    return ReadWeightConfig(filename);
}
