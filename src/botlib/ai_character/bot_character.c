#include "bot_character.h"

#include "q2bridge/botlib.h"
#include "botlib/common/l_log.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOT_CHARACTER_INVALID_HANDLE 0
#define BOT_CHARACTER_FILENAME_MAX 256
#define BOT_CHARACTER_MAX_HANDLES (MAX_CLIENTS + 16)
#define BOT_CHARACTER_SKILL_EPSILON 0.01f

typedef struct bot_character_cache_entry_s {
    ai_character_profile_t *profile;
    char filename[BOT_CHARACTER_FILENAME_MAX];
    float skill;
    unsigned int refcount;
} bot_character_cache_entry_t;

static bot_character_cache_entry_t g_bot_character_cache[BOT_CHARACTER_MAX_HANDLES];

static int bot_character_compare_name(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return (lhs == rhs) ? 0 : (lhs != NULL ? 1 : -1);
    }

    while (*lhs != '\0' && *rhs != '\0') {
        unsigned char left = (unsigned char)*lhs;
        unsigned char right = (unsigned char)*rhs;

        if (left == '\\') {
            left = '/';
        } else {
            left = (unsigned char)tolower(left);
        }

        if (right == '\\') {
            right = '/';
        } else {
            right = (unsigned char)tolower(right);
        }

        if (left != right) {
            return (int)left - (int)right;
        }

        ++lhs;
        ++rhs;
    }

    unsigned char left = (*lhs == '\\') ? '/' : (unsigned char)tolower((unsigned char)*lhs);
    unsigned char right = (*rhs == '\\') ? '/' : (unsigned char)tolower((unsigned char)*rhs);
    return (int)left - (int)right;
}

static bool bot_character_handles_equal(const bot_character_cache_entry_t *entry,
                                        const char *filename,
                                        float skill)
{
    if (!entry || !entry->profile) {
        return false;
    }

    if (fabsf(entry->skill - skill) > BOT_CHARACTER_SKILL_EPSILON) {
        return false;
    }

    return bot_character_compare_name(entry->filename, filename) == 0;
}

static float bot_character_normalise_skill(float skill)
{
    if (skill < 0.0f) {
        return -1.0f;
    }
    if (skill < 1.0f) {
        return 1.0f;
    }
    if (skill > 5.0f) {
        return 5.0f;
    }
    return skill;
}

static int bot_character_find_cached_handle(const char *filename, float skill)
{
    for (int handle = 1; handle < BOT_CHARACTER_MAX_HANDLES; ++handle) {
        bot_character_cache_entry_t *entry = &g_bot_character_cache[handle];
        if (!entry->profile) {
            continue;
        }

        if (bot_character_handles_equal(entry, filename, skill)) {
            return handle;
        }
    }

    return BOT_CHARACTER_INVALID_HANDLE;
}

static int bot_character_allocate_handle(void)
{
    for (int handle = 1; handle < BOT_CHARACTER_MAX_HANDLES; ++handle) {
        if (g_bot_character_cache[handle].profile == NULL) {
            return handle;
        }
    }

    return BOT_CHARACTER_INVALID_HANDLE;
}

static bot_character_cache_entry_t *bot_character_entry(int handle)
{
    if (handle <= BOT_CHARACTER_INVALID_HANDLE || handle >= BOT_CHARACTER_MAX_HANDLES) {
        return NULL;
    }

    return &g_bot_character_cache[handle];
}

static void bot_character_log_invalid_handle(const char *context, int handle)
{
    BotLib_Print(PRT_WARNING,
                 "[bot_character] %s: invalid handle %d\n",
                 context ? context : "operation",
                 handle);
}

int BotLoadCharacter(const char *character_file, float skill)
{
    if (!character_file || character_file[0] == '\0') {
        BotLib_Print(PRT_WARNING, "[bot_character] BotLoadCharacter: missing filename.\n");
        return BOT_CHARACTER_INVALID_HANDLE;
    }

    float normalised_skill = bot_character_normalise_skill(skill);
    int cached_handle = bot_character_find_cached_handle(character_file, normalised_skill);
    if (cached_handle != BOT_CHARACTER_INVALID_HANDLE) {
        bot_character_cache_entry_t *entry = bot_character_entry(cached_handle);
        ++entry->refcount;
        BotLib_Print(PRT_MESSAGE,
                     "[bot_character] reusing cached character %s (skill %.2f, refs %u)\n",
                     entry->filename,
                     entry->skill,
                     entry->refcount);
        return cached_handle;
    }

    ai_character_profile_t *profile = AI_LoadCharacter(character_file, normalised_skill);
    if (!profile) {
        BotLib_Print(PRT_ERROR,
                     "[bot_character] couldn't load bot character %s from %s\n",
                     character_file,
                     character_file);
        return BOT_CHARACTER_INVALID_HANDLE;
    }

    int handle = bot_character_allocate_handle();
    if (handle == BOT_CHARACTER_INVALID_HANDLE) {
        BotLib_Print(PRT_ERROR,
                     "[bot_character] no free character slots for %s (skill %.2f)\n",
                     character_file,
                     normalised_skill);
        AI_FreeCharacter(profile);
        return BOT_CHARACTER_INVALID_HANDLE;
    }

    bot_character_cache_entry_t *entry = bot_character_entry(handle);
    entry->profile = profile;
    entry->skill = normalised_skill;
    entry->refcount = 1;
    strncpy(entry->filename, character_file, sizeof(entry->filename) - 1);
    entry->filename[sizeof(entry->filename) - 1] = '\0';

    BotLib_Print(PRT_MESSAGE,
                 "[bot_character] loaded bot character %s (skill %.2f)\n",
                 entry->filename,
                 entry->skill);

    return handle;
}

int BotLoadCharacterSkill(const char *character_file, float skill)
{
    return BotLoadCharacter(character_file, bot_character_normalise_skill(skill));
}

void BotFreeCharacter(int handle)
{
    bot_character_cache_entry_t *entry = bot_character_entry(handle);
    if (!entry || !entry->profile) {
        bot_character_log_invalid_handle("BotFreeCharacter", handle);
        return;
    }

    if (entry->refcount > 0) {
        --entry->refcount;
    }

    if (entry->refcount == 0) {
        BotLib_Print(PRT_MESSAGE,
                     "[bot_character] freed bot character %s (skill %.2f)\n",
                     entry->filename,
                     entry->skill);
        AI_FreeCharacter(entry->profile);
        memset(entry, 0, sizeof(*entry));
        return;
    }

    BotLib_Print(PRT_MESSAGE,
                 "[bot_character] decremented %s (skill %.2f) to %u references\n",
                 entry->filename,
                 entry->skill,
                 entry->refcount);
}

void BotFreeCharacterStrings(ai_character_profile_t *profile)
{
    if (!profile) {
        return;
    }

    AI_FreeCharacter(profile);
}

ai_character_profile_t *BotCharacterFromHandle(int handle)
{
    bot_character_cache_entry_t *entry = bot_character_entry(handle);
    if (!entry) {
        bot_character_log_invalid_handle("BotCharacterFromHandle", handle);
        return NULL;
    }

    return entry->profile;
}

static ai_character_profile_t *bot_character_profile_for_queries(int handle)
{
    bot_character_cache_entry_t *entry = bot_character_entry(handle);
    if (!entry || !entry->profile) {
        bot_character_log_invalid_handle("Characteristic query", handle);
        return NULL;
    }

    return entry->profile;
}

float Characteristic_Float(int handle, int index)
{
    ai_character_profile_t *profile = bot_character_profile_for_queries(handle);
    if (!profile) {
        return 0.0f;
    }

    return AI_CharacteristicAsFloat(profile, index);
}

float Characteristic_BFloat(int handle, int index, float minimum, float maximum)
{
    float value = Characteristic_Float(handle, index);
    if (value < minimum) {
        value = minimum;
    }
    if (value > maximum) {
        value = maximum;
    }
    return value;
}

int Characteristic_Integer(int handle, int index)
{
    ai_character_profile_t *profile = bot_character_profile_for_queries(handle);
    if (!profile) {
        return 0;
    }

    return AI_CharacteristicAsInteger(profile, index);
}

int Characteristic_BInteger(int handle, int index, int minimum, int maximum)
{
    int value = Characteristic_Integer(handle, index);
    if (value < minimum) {
        value = minimum;
    }
    if (value > maximum) {
        value = maximum;
    }
    return value;
}

void Characteristic_String(int handle, int index, char *buffer, int buffer_size)
{
    if (!buffer || buffer_size <= 0) {
        return;
    }

    buffer[0] = '\0';

    ai_character_profile_t *profile = bot_character_profile_for_queries(handle);
    if (!profile) {
        return;
    }

    const char *value = AI_CharacteristicAsString(profile, index);
    if (!value) {
        return;
    }

    snprintf(buffer, (size_t)buffer_size, "%s", value);
}

