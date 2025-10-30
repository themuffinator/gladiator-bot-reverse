#include "bot_weight.h"

#include "../common/l_log.h"

/*
 * Stub skeleton for the Gladiator weight configuration loader. The structure
 * layout and API mirror Quake III’s be_ai_weight module so the surrounding AI
 * systems can compile while the HLIL parsing work continues. See
 * docs/weight_config_analysis.md for the lifted string references and control
 * flow we plan to reproduce.
 */

bot_weight_config_t *ReadWeightConfig(const char *filename)
{
    (void)filename;

    BotLib_Print(PRT_WARNING, "[ai_weight] TODO: implement ReadWeightConfig.\n");
    return NULL;
}

void FreeWeightConfig(bot_weight_config_t *config)
{
    (void)config;

    BotLib_Print(PRT_WARNING, "[ai_weight] TODO: implement FreeWeightConfig.\n");
}

float FuzzyWeight(const int *inventory, const bot_weight_config_t *config, int weight_index)
{
    (void)inventory;
    (void)config;
    (void)weight_index;

    BotLib_Print(PRT_WARNING, "[ai_weight] TODO: implement FuzzyWeight.\n");
    return 0.0f;
}

