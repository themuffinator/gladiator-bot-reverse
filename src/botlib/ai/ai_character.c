#include "character/bot_character.h"

#include "chat/ai_chat.h"
#include "../common/l_assets.h"
#include "../common/l_log.h"
#include "../common/l_memory.h"
#include "weapon/bot_weapon.h"
#include "weight/bot_weight.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * HLIL traces show each bot client owning a packed character profile with
 * handles to item/weapon weight tables and chat state stored at offsets within
 * the 0x11d0-byte bot state buffer:
 *   - *(state + 0x688) : character definition allocation logged via
 *     "%6d bytes character\n" before setup completes.
 *   - *(state + 0xbc0) / *(state + 0xbc4) : item weight config pointer and the
 *     accompanying index mapping, reported as "item weights" and
 *     "item index" bytes respectively (the reconstructed profile still tracks
 *     only the configuration until the goal module wires in the compiled index).
 *   - *(state + 0x1050) : weapon weight handle combining the configuration and
 *     compiled index table, echoed as "weapon weights" and "weapon index".
 *   - *(state + 0x1044) : chat file workspace tracked by
 *     "%6d bytes chat file\n".
 * The setup routine (sub_10029480) constructs these slots by loading the
 * character file (sub_10029eb0), requesting weight sets named by indices within
 * the character, then binding the chat script before flagging the client as
 * active. Failures unwind by freeing weight configs and chat state in reverse
 * order, matching the cleanup observed during shutdown (sub_10029690).
 *【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32483-L32566】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32514-L32599】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32599-L32656】
 */
/**
 * Maximum number of characteristics surfaced by chars.h. The Gladiator assets
 * define indices up to 53 so a 64 entry table comfortably mirrors the HLIL
 * buffer sizing with room for future discoveries.
 */
#define AI_MAX_CHARACTERISTICS 64

typedef enum ai_character_value_type_e {
    AI_CHARACTER_VALUE_NONE = 0,
    AI_CHARACTER_VALUE_INTEGER = 1,
    AI_CHARACTER_VALUE_FLOAT = 2,
    AI_CHARACTER_VALUE_STRING = 3,
} ai_character_value_type_t;

typedef struct ai_characteristic_s {
    ai_character_value_type_t type;
    union {
        int integer_value;
        float float_value;
        char *string_value;
    } data;
} ai_characteristic_t;

typedef struct ai_character_definition_s {
    char identifier[64];
    ai_characteristic_t characteristics[AI_MAX_CHARACTERISTICS];
} ai_character_definition_t;

typedef struct macro_entry_s {
    char name[64];
    int value;
    ai_character_value_type_t type;
    bool has_type;
} macro_entry_t;

typedef struct macro_table_s {
    macro_entry_t entries[256];
    size_t count;
} macro_table_t;

enum {
    AI_CHARACTER_INDEX_WEAPON_WEIGHTS = 5,
    AI_CHARACTER_INDEX_CHAT_FILE = 12,
    AI_CHARACTER_INDEX_CHAT_NAME = 13,
    AI_CHARACTER_INDEX_ITEM_WEIGHTS = 28,
};

static void ai_free_definition(ai_character_definition_t *definition)
{
    if (!definition) {
        return;
    }

    for (size_t i = 0; i < AI_MAX_CHARACTERISTICS; ++i) {
        ai_characteristic_t *slot = &definition->characteristics[i];
        if (slot->type == AI_CHARACTER_VALUE_STRING && slot->data.string_value) {
            free(slot->data.string_value);
            slot->data.string_value = NULL;
        }
        slot->type = AI_CHARACTER_VALUE_NONE;
    }

    FreeMemory(definition);
}

static char *ai_trim_whitespace(char *text)
{
    if (!text) {
        return NULL;
    }

    while (*text && isspace((unsigned char)*text)) {
        ++text;
    }

    if (!*text) {
        return text;
    }

    char *end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }

    return text;
}

static char *ai_duplicate_string(const char *text)
{
    if (!text) {
        return NULL;
    }

    size_t length = strlen(text) + 1;
    char *copy = (char *)malloc(length);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, text, length);
    return copy;
}

static ai_character_value_type_t ai_type_from_comment(const char *comment)
{
    if (!comment) {
        return AI_CHARACTER_VALUE_NONE;
    }

    char buffer[128];
    size_t length = strlen(comment);
    if (length >= sizeof(buffer)) {
        length = sizeof(buffer) - 1;
    }
    memcpy(buffer, comment, length);
    buffer[length] = '\0';

    for (size_t i = 0; buffer[i]; ++i) {
        buffer[i] = (char)tolower((unsigned char)buffer[i]);
    }

    if (strstr(buffer, "string")) {
        return AI_CHARACTER_VALUE_STRING;
    }
    if (strstr(buffer, "integer")) {
        return AI_CHARACTER_VALUE_INTEGER;
    }
    if (strstr(buffer, "float")) {
        return AI_CHARACTER_VALUE_FLOAT;
    }

    return AI_CHARACTER_VALUE_NONE;
}

static void ai_macro_table_set(macro_table_t *table, const char *name, int value,
                               ai_character_value_type_t type, bool has_type)
{
    if (!table || !name) {
        return;
    }

    for (size_t i = 0; i < table->count; ++i) {
        if (strcmp(table->entries[i].name, name) == 0) {
            table->entries[i].value = value;
            if (has_type) {
                table->entries[i].type = type;
                table->entries[i].has_type = true;
            }
            return;
        }
    }

    if (table->count >= sizeof(table->entries) / sizeof(table->entries[0])) {
        BotLib_Print(PRT_WARNING,
                     "[ai_character] macro table full, ignoring %s.\n", name);
        return;
    }

    macro_entry_t *entry = &table->entries[table->count++];
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->value = value;
    entry->type = type;
    entry->has_type = has_type;
}

static const macro_entry_t *ai_macro_table_lookup(const macro_table_t *table,
                                                  const char *name)
{
    if (!table || !name) {
        return NULL;
    }

    for (size_t i = 0; i < table->count; ++i) {
        if (strcmp(table->entries[i].name, name) == 0) {
            return &table->entries[i];
        }
    }

    return NULL;
}

static bool ai_parse_include(const char *base_dir, const char *include_name,
                             macro_table_t *table)
{
    if (!include_name || !table) {
        return false;
    }

    char path[512];
    if (include_name[0] == '/' || (include_name[0] && include_name[1] == ':')) {
        strncpy(path, include_name, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else {
        snprintf(path, sizeof(path), "%s/%s", base_dir, include_name);
    }

    FILE *file = fopen(path, "r");
    if (!file) {
        BotLib_Print(PRT_WARNING,
                     "[ai_character] failed to open include %s: %s\n",
                     path, strerror(errno));
        return false;
    }

    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char *trimmed = ai_trim_whitespace(line);
        if (!trimmed || !*trimmed) {
            continue;
        }

        char *comment = strstr(trimmed, "//");
        if (comment) {
            *comment = '\0';
            comment = ai_trim_whitespace(comment + 2);
        }

        if (strncmp(trimmed, "#define", 7) != 0) {
            continue;
        }

        trimmed += 7;
        trimmed = ai_trim_whitespace(trimmed);
        if (!trimmed || !*trimmed) {
            continue;
        }

        char *name_end = trimmed;
        while (*name_end && !isspace((unsigned char)*name_end)) {
            ++name_end;
        }
        if (*name_end) {
            *name_end = '\0';
            ++name_end;
        }

        char *value_str = ai_trim_whitespace(name_end);
        if (!value_str || !*value_str) {
            continue;
        }

        char *value_end = value_str;
        while (*value_end && !isspace((unsigned char)*value_end)) {
            ++value_end;
        }
        if (*value_end) {
            *value_end = '\0';
        }

        errno = 0;
        long parsed_value = strtol(value_str, NULL, 0);
        if (errno != 0) {
            continue;
        }

        ai_character_value_type_t type = AI_CHARACTER_VALUE_NONE;
        bool has_type = false;
        if (comment) {
            type = ai_type_from_comment(comment);
            has_type = type != AI_CHARACTER_VALUE_NONE;
        }

        if (strncmp(trimmed, "CHARACTERISTIC_", 15) == 0) {
            ai_macro_table_set(table, trimmed, (int)parsed_value, type, has_type);
        }
    }

    fclose(file);
    return true;
}

static bool ai_store_characteristic(ai_character_definition_t *definition, int index,
                                    ai_character_value_type_t type, const char *value_str)
{
    if (!definition || index < 0 || index >= AI_MAX_CHARACTERISTICS) {
        BotLib_Print(PRT_WARNING,
                     "[ai_character] characteristic index %d out of range.\n",
                     index);
        return false;
    }

    ai_characteristic_t *slot = &definition->characteristics[index];
    if (slot->type == AI_CHARACTER_VALUE_STRING && slot->data.string_value) {
        free(slot->data.string_value);
        slot->data.string_value = NULL;
    }

    switch (type) {
    case AI_CHARACTER_VALUE_STRING:
        slot->type = AI_CHARACTER_VALUE_STRING;
        slot->data.string_value = ai_duplicate_string(value_str);
        if (!slot->data.string_value) {
            BotLib_Print(PRT_WARNING,
                         "[ai_character] failed to allocate string value.\n");
            slot->type = AI_CHARACTER_VALUE_NONE;
            return false;
        }
        return true;
    case AI_CHARACTER_VALUE_INTEGER:
        slot->type = AI_CHARACTER_VALUE_INTEGER;
        slot->data.integer_value = (int)strtol(value_str, NULL, 0);
        return true;
    case AI_CHARACTER_VALUE_FLOAT:
        slot->type = AI_CHARACTER_VALUE_FLOAT;
        slot->data.float_value = strtof(value_str, NULL);
        return true;
    case AI_CHARACTER_VALUE_NONE:
    default:
        break;
    }

    if (value_str && strchr(value_str, '.')) {
        slot->type = AI_CHARACTER_VALUE_FLOAT;
        slot->data.float_value = strtof(value_str, NULL);
    } else {
        slot->type = AI_CHARACTER_VALUE_INTEGER;
        slot->data.integer_value = (int)strtol(value_str, NULL, 0);
    }

    return true;
}

static bool ai_store_characteristic_string(ai_character_definition_t *definition,
                                           int index, const char *value)
{
    return ai_store_characteristic(definition, index,
                                   AI_CHARACTER_VALUE_STRING, value);
}

static bool ai_parse_character_file(const char *full_path,
                                    const char *base_dir,
                                    macro_table_t *macros,
                                    ai_character_definition_t *definition)
{
    FILE *file = fopen(full_path, "r");
    if (!file) {
        BotLib_Print(PRT_ERROR,
                     "[ai_character] failed to open %s: %s\n",
                     full_path, strerror(errno));
        return false;
    }

    char line[1024];
    bool inside_block = false;

    while (fgets(line, sizeof(line), file)) {
        char *trimmed = ai_trim_whitespace(line);
        if (!trimmed) {
            continue;
        }

        char *comment = strstr(trimmed, "//");
        if (comment) {
            *comment = '\0';
        }

        trimmed = ai_trim_whitespace(trimmed);
        if (!trimmed || !*trimmed) {
            continue;
        }

        if (strncmp(trimmed, "#include", 8) == 0) {
            char *start = strchr(trimmed, '"');
            char *end = start ? strchr(start + 1, '"') : NULL;
            if (start && end && end > start + 1) {
                char include_name[256];
                size_t len = (size_t)(end - start - 1);
                if (len >= sizeof(include_name)) {
                    len = sizeof(include_name) - 1;
                }
                memcpy(include_name, start + 1, len);
                include_name[len] = '\0';
                ai_parse_include(base_dir, include_name, macros);
            }
            continue;
        }

        if (!inside_block) {
            if (strncmp(trimmed, "character", 9) == 0) {
                char *start = strchr(trimmed, '"');
                char *end = start ? strchr(start + 1, '"') : NULL;
                if (start && end && end > start + 1) {
                    size_t len = (size_t)(end - start - 1);
                    if (len >= sizeof(definition->identifier)) {
                        len = sizeof(definition->identifier) - 1;
                    }
                    memcpy(definition->identifier, start + 1, len);
                    definition->identifier[len] = '\0';
                }
            }

            if (strchr(trimmed, '{')) {
                inside_block = true;
            }
            continue;
        }

        if (strchr(trimmed, '}')) {
            break;
        }

        char *token_end = trimmed;
        while (*token_end && !isspace((unsigned char)*token_end)) {
            ++token_end;
        }
        if (*token_end) {
            *token_end = '\0';
            ++token_end;
        }

        const macro_entry_t *macro = ai_macro_table_lookup(macros, trimmed);
        if (!macro) {
            continue;
        }

        char *value_part = ai_trim_whitespace(token_end);
        if (!value_part || !*value_part) {
            continue;
        }

        if (*value_part == '"') {
            char *end = strrchr(value_part + 1, '"');
            if (!end) {
                continue;
            }
            *end = '\0';
            ++value_part;
            ai_store_characteristic_string(definition, macro->value, value_part);
        } else {
            ai_character_value_type_t type = macro->has_type ? macro->type : AI_CHARACTER_VALUE_NONE;
            ai_store_characteristic(definition, macro->value, type, value_part);
        }
    }

    fclose(file);
    return true;
}

static bool ai_locate_asset_root(char *buffer, size_t size)
{
    return BotLib_LocateAssetRoot(buffer, size);
}

static ai_character_definition_t *ai_parse_definition(const char *filename)
{
    if (!filename) {
        return NULL;
    }

    char base_dir[512];
    if (!ai_locate_asset_root(base_dir, sizeof(base_dir))) {
        BotLib_Print(PRT_ERROR,
                     "[ai_character] unable to locate asset directory for %s.\n",
                     filename);
        return NULL;
    }

    char full_path[512];
    if (!BotLib_ResolveAssetPath(filename, "bots", full_path, sizeof(full_path))) {
        BotLib_Print(PRT_ERROR,
                     "[ai_character] unable to resolve character %s.\n",
                     filename != NULL ? filename : "<null>");
        return NULL;
    }

    ai_character_definition_t *definition = (ai_character_definition_t *)GetClearedMemory(sizeof(*definition));
    if (!definition) {
        return NULL;
    }

    macro_table_t macros = {0};
    ai_parse_include(base_dir, "chars.h", &macros);
    ai_parse_include(base_dir, "game.h", &macros);

    if (!ai_parse_character_file(full_path, base_dir, &macros, definition)) {
        ai_free_definition(definition);
        return NULL;
    }

    return definition;
}

static const char *ai_profile_display_name(const ai_character_profile_t *profile,
                                          const char *fallback)
{
    if (!profile) {
        return fallback ? fallback : "<unknown>";
    }

    if (profile->character_filename[0] != '\0') {
        return profile->character_filename;
    }

    if (profile->definition_blob && profile->definition_blob->identifier[0] != '\0') {
        return profile->definition_blob->identifier;
    }

    return fallback ? fallback : "<unknown>";
}

static bool ai_profile_load_item_weights(ai_character_profile_t *profile,
                                         const char *character_name)
{
    const char *item_weights_file = AI_CharacteristicAsString(profile, AI_CHARACTER_INDEX_ITEM_WEIGHTS);
    if (!item_weights_file || item_weights_file[0] == '\0') {
        BotLib_Print(PRT_ERROR,
                     "[ai_character] %s is missing an item weight file.\n",
                     character_name);
        return false;
    }

    char resolved_path[512];
    if (!BotLib_ResolveAssetPath(item_weights_file, "bots", resolved_path, sizeof(resolved_path))) {
        BotLib_Print(PRT_ERROR,
                     "[ai_character] failed to locate item weights %s for %s.\n",
                     item_weights_file, character_name);
        return false;
    }

    profile->item_weights = ReadWeightConfig(resolved_path);
    if (!profile->item_weights) {
        BotLib_Print(PRT_ERROR,
                     "[ai_character] failed to load item weights %s for %s.\n",
                     item_weights_file, character_name);
        return false;
    }

    size_t bytes = MemoryByteSize(profile->item_weights);
    BotLib_Print(PRT_DEVELOPER, "%6d bytes item weights\n", (int)bytes);
    return true;
}

static bool ai_profile_load_weapon_weights(ai_character_profile_t *profile,
                                           const char *character_name)
{
    const char *weapon_weights_file = AI_CharacteristicAsString(profile, AI_CHARACTER_INDEX_WEAPON_WEIGHTS);
    if (!weapon_weights_file || weapon_weights_file[0] == '\0') {
        BotLib_Print(PRT_ERROR,
                     "[ai_character] %s is missing a weapon weight file.\n",
                     character_name);
        return false;
    }

    char resolved_path[512];
    if (!BotLib_ResolveAssetPath(weapon_weights_file, "bots", resolved_path, sizeof(resolved_path))) {
        BotLib_Print(PRT_ERROR,
                     "[ai_character] failed to locate weapon weights %s for %s.\n",
                     weapon_weights_file, character_name);
        return false;
    }

    profile->weapon_weights = AI_LoadWeaponWeights(resolved_path);
    if (!profile->weapon_weights) {
        BotLib_Print(PRT_ERROR,
                     "[ai_character] failed to load weapon weights %s for %s.\n",
                     weapon_weights_file, character_name);
        return false;
    }

    size_t bytes = MemoryByteSize(profile->weapon_weights);
    BotLib_Print(PRT_DEVELOPER, "%6d bytes weapon weights\n", (int)bytes);
    return true;
}

static bool ai_profile_load_chat(ai_character_profile_t *profile,
                                 const char *character_name)
{
    const char *chat_file = AI_CharacteristicAsString(profile, AI_CHARACTER_INDEX_CHAT_FILE);
    const char *chat_name = AI_CharacteristicAsString(profile, AI_CHARACTER_INDEX_CHAT_NAME);
    if (!chat_file || chat_file[0] == '\0' || !chat_name || chat_name[0] == '\0') {
        BotLib_Print(PRT_ERROR,
                     "[ai_character] %s is missing chat configuration.\n",
                     character_name);
        return false;
    }

    profile->chat_state = BotAllocChatState();
    if (!profile->chat_state) {
        BotLib_Print(PRT_ERROR,
                     "[ai_character] failed to allocate chat state for %s.\n",
                     character_name);
        return false;
    }

    char resolved_path[512];
    if (!BotLib_ResolveAssetPath(chat_file, "bots", resolved_path, sizeof(resolved_path))) {
        BotLib_Print(PRT_ERROR,
                     "[ai_character] failed to locate chat file %s for %s.\n",
                     chat_file, character_name);
        BotFreeChatState(profile->chat_state);
        profile->chat_state = NULL;
        return false;
    }

    if (!BotLoadChatFile(profile->chat_state, resolved_path, chat_name)) {
        BotLib_Print(PRT_ERROR,
                     "[ai_character] failed to load chat file %s (%s) for %s.\n",
                     resolved_path, chat_name, character_name);
        BotFreeChatState(profile->chat_state);
        profile->chat_state = NULL;
        return false;
    }

    size_t bytes = MemoryByteSize(profile->chat_state);
    BotLib_Print(PRT_DEVELOPER, "%6d bytes chat file\n", (int)bytes);
    return true;
}

ai_character_profile_t *AI_LoadCharacter(const char *filename, float skill)
{
    ai_character_definition_t *definition = ai_parse_definition(filename);
    if (!definition) {
        BotLib_Print(PRT_ERROR,
                     "[ai_character] failed to parse character %s.\n",
                     filename ? filename : "<null>");
        return NULL;
    }

    ai_character_profile_t *profile = (ai_character_profile_t *)GetClearedMemory(sizeof(*profile));
    if (!profile) {
        ai_free_definition(definition);
        return NULL;
    }

    profile->requested_skill = skill;
    profile->definition_blob = definition;

    if (filename) {
        strncpy(profile->character_filename, filename,
                sizeof(profile->character_filename) - 1);
        profile->character_filename[sizeof(profile->character_filename) - 1] = '\0';
    }

    size_t character_bytes = MemoryByteSize(profile->definition_blob);
    BotLib_Print(PRT_DEVELOPER, "%6d bytes character\n", (int)character_bytes);

    const char *character_name = ai_profile_display_name(profile, filename);

    if (!ai_profile_load_item_weights(profile, character_name)) {
        goto load_failure;
    }

    if (!ai_profile_load_weapon_weights(profile, character_name)) {
        goto load_failure;
    }

    if (!ai_profile_load_chat(profile, character_name)) {
        goto load_failure;
    }

    return profile;

load_failure:
    if (profile->chat_state) {
        BotFreeChatState(profile->chat_state);
        profile->chat_state = NULL;
    }
    if (profile->weapon_weights) {
        AI_FreeWeaponWeights(profile->weapon_weights);
        profile->weapon_weights = NULL;
    }
    if (profile->item_weights) {
        FreeWeightConfig(profile->item_weights);
        profile->item_weights = NULL;
    }

    ai_free_definition(profile->definition_blob);
    profile->definition_blob = NULL;
    FreeMemory(profile);
    return NULL;
}

void AI_FreeCharacter(ai_character_profile_t *profile)
{
    if (!profile) {
        return;
    }

    if (profile->chat_state != NULL) {
        BotFreeChatState(profile->chat_state);
        profile->chat_state = NULL;
    }

    if (profile->weapon_weights != NULL) {
        AI_FreeWeaponWeights(profile->weapon_weights);
        profile->weapon_weights = NULL;
    }

    if (profile->item_weights != NULL) {
        FreeWeightConfig(profile->item_weights);
        profile->item_weights = NULL;
    }

    if (profile->definition_blob) {
        ai_free_definition(profile->definition_blob);
        profile->definition_blob = NULL;
    }

    FreeMemory(profile);
}

bot_weight_config_t *AI_ItemWeightsForCharacter(const ai_character_profile_t *profile)
{
    if (!profile) {
        return NULL;
    }

    return profile->item_weights;
}

ai_weapon_weights_t *AI_WeaponWeightsForCharacter(const ai_character_profile_t *profile)
{
    if (!profile) {
        return NULL;
    }

    return profile->weapon_weights;
}

static const ai_character_definition_t *ai_definition(const ai_character_profile_t *profile)
{
    return profile ? profile->definition_blob : NULL;
}

float AI_CharacteristicAsFloat(const ai_character_profile_t *profile, int index)
{
    const ai_character_definition_t *definition = ai_definition(profile);
    if (!definition || index < 0 || index >= AI_MAX_CHARACTERISTICS) {
        return 0.0f;
    }

    const ai_characteristic_t *slot = &definition->characteristics[index];
    switch (slot->type) {
    case AI_CHARACTER_VALUE_FLOAT:
        return slot->data.float_value;
    case AI_CHARACTER_VALUE_INTEGER:
        return (float)slot->data.integer_value;
    default:
        return 0.0f;
    }
}

int AI_CharacteristicAsInteger(const ai_character_profile_t *profile, int index)
{
    const ai_character_definition_t *definition = ai_definition(profile);
    if (!definition || index < 0 || index >= AI_MAX_CHARACTERISTICS) {
        return 0;
    }

    const ai_characteristic_t *slot = &definition->characteristics[index];
    switch (slot->type) {
    case AI_CHARACTER_VALUE_INTEGER:
        return slot->data.integer_value;
    case AI_CHARACTER_VALUE_FLOAT:
        return (int)(slot->data.float_value + (slot->data.float_value >= 0.0f ? 0.5f : -0.5f));
    default:
        return 0;
    }
}

const char *AI_CharacteristicAsString(const ai_character_profile_t *profile, int index)
{
    const ai_character_definition_t *definition = ai_definition(profile);
    if (!definition || index < 0 || index >= AI_MAX_CHARACTERISTICS) {
        return NULL;
    }

    const ai_characteristic_t *slot = &definition->characteristics[index];
    if (slot->type == AI_CHARACTER_VALUE_STRING) {
        return slot->data.string_value;
    }

    return NULL;
}
