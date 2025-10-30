#include "character/bot_character.h"

#include "../common/l_log.h"
#include "weight/bot_weight.h"

#include <stddef.h>

/*
 * HLIL traces show each bot client owning a packed character profile with
 * handles to item/weapon weight tables and chat state stored at offsets within
 * the 0x11d0-byte bot state buffer:
 *   - *(state + 0x688) : character definition allocation logged via
 *     "%6d bytes character\n" before setup completes.
 *   - *(state + 0xbc0) / *(state + 0xbc4) : item weight config pointer and the
 *     accompanying index mapping, reported as "item weights" and
 *     "item index" bytes respectively.
 *   - *(state + 0x1050) / *(state + 0x1054) : weapon weight config pointer and
 *     index mapping, echoed as "weapon weights" and "weapon index".
 *   - *(state + 0x1044) : chat file workspace tracked by
 *     "%6d bytes chat file\n".
 * The setup routine (sub_10029480) constructs these slots by loading the
 * character file (sub_10029eb0), requesting weight sets named by indices within
 * the character, then binding the chat script before flagging the client as
 * active. Failures unwind by freeing weight configs and chat state in reverse
 * order, matching the cleanup observed during shutdown (sub_10029690).
 *【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32483-L32566】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32514-L32599】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32599-L32656】
 */
typedef struct ai_character_profile_s {
    char character_filename[128];
    float requested_skill;
    bot_weight_config_t *item_weights;
    void *item_weight_index;
    bot_weight_config_t *weapon_weights;
    void *weapon_weight_index;
    void *chat_state;
    void *definition_blob;
} ai_character_profile_t;

/*
 * Future translation of sub_10029eb0 (LoadCharacter) will populate the
 * definition blob by parsing Gladiator .chr files, resolving filesystem paths,
 * and evaluating characteristic blocks (floats, ints, and strings). For now the
 * stub surfaces the intended signature so higher-level code can link.
 */
ai_character_profile_t *AI_LoadCharacter(const char *filename, float skill)
{
    (void)filename;
    (void)skill;

    BotLib_Print(PRT_WARNING, "[ai_character] TODO: implement AI_LoadCharacter.\n");
    return NULL;
}

/*
 * Cleanup must mirror the shutdown flow in sub_10029690, releasing weight
 * configs, chat allocations, and finally the character definition blob before
 * zeroing the bot state. The stub stands in place until the HLIL logic is
 * implemented.
 */
void AI_FreeCharacter(ai_character_profile_t *profile)
{
    (void)profile;

    BotLib_Print(PRT_WARNING, "[ai_character] TODO: implement AI_FreeCharacter.\n");
}

/*
 * Skill-specific character variants (Gladiator loads alternate weight files via
 * indices like 0x1c and 5 inside the profile) will eventually forward to the
 * weight module once parsing lands. Stubbed for now.
 */
bot_weight_config_t *AI_ItemWeightsForCharacter(const ai_character_profile_t *profile)
{
    (void)profile;

    BotLib_Print(PRT_WARNING, "[ai_character] TODO: implement AI_ItemWeightsForCharacter.\n");
    return NULL;
}

bot_weight_config_t *AI_WeaponWeightsForCharacter(const ai_character_profile_t *profile)
{
    (void)profile;

    BotLib_Print(PRT_WARNING, "[ai_character] TODO: implement AI_WeaponWeightsForCharacter.\n");
    return NULL;
}

/*
 * Placeholder for characteristic lookup helpers (float/int/string) that will be
 * required by goal selection and weapon heuristics.
 */
float AI_CharacteristicAsFloat(const ai_character_profile_t *profile, int index)
{
    (void)profile;
    (void)index;

    BotLib_Print(PRT_WARNING, "[ai_character] TODO: implement AI_CharacteristicAsFloat.\n");
    return 0.0f;
}
