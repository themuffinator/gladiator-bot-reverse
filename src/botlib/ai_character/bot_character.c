#include "bot_character.h"

#include "q2bridge/botlib.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define BOT_CHARACTER_INVALID_HANDLE 0
#define BOT_CHARACTER_FILENAME_MAX 256
#define BOT_CHARACTER_NAME_MAX 128
#define BOT_CHARACTER_MAX_HANDLES (MAX_CLIENTS + 16)
#define BOT_CHARACTER_SKILL_EPSILON 0.01f
#define BOT_CHARACTER_Q3_DEFAULT_FILE "bots/default_c.c"

typedef struct bot_character_cache_entry_s {
	ai_character_profile_t *profile;
	char filename[BOT_CHARACTER_FILENAME_MAX];
	char character_name[BOT_CHARACTER_NAME_MAX];
	float skill;
	unsigned int refcount;
} bot_character_cache_entry_t;

static bot_character_cache_entry_t g_bot_character_cache[BOT_CHARACTER_MAX_HANDLES];

/*
=============
bot_character_compare_filename

Compares cache filenames case-insensitively while normalising slashes.
=============
*/
static int bot_character_compare_filename(const char *lhs, const char *rhs)
{
	if (lhs == NULL || rhs == NULL)
	{
		return (lhs == rhs) ? 0 : (lhs != NULL ? 1 : -1);
	}

	while (*lhs != '\0' && *rhs != '\0')
	{
		unsigned char left = (unsigned char)*lhs;
		unsigned char right = (unsigned char)*rhs;

		left = (left == '\\') ? '/' : (unsigned char)tolower(left);
		right = (right == '\\') ? '/' : (unsigned char)tolower(right);

		if (left != right)
		{
			return (int)left - (int)right;
		}

		lhs++;
		rhs++;
	}

	unsigned char left = (*lhs == '\\') ? '/' : (unsigned char)tolower((unsigned char)*lhs);
	unsigned char right = (*rhs == '\\') ? '/' : (unsigned char)tolower((unsigned char)*rhs);
	return (int)left - (int)right;
}

/*
=============
bot_character_is_q3_default_file

Checks whether a filename refers to the Q3 default character.
=============
*/
static bool bot_character_is_q3_default_file(const char *character_file)
{
	return bot_character_compare_filename(character_file,
		BOT_CHARACTER_Q3_DEFAULT_FILE) == 0;
}

/*
=============
bot_character_requested_name

Normalises NULL character names to the direct-load cache key.
=============
*/
static const char *bot_character_requested_name(const char *character_name)
{
	return character_name != NULL ? character_name : "";
}

/*
=============
bot_character_copy_string

Copies text into a bounded cache buffer.
=============
*/
static void bot_character_copy_string(char *destination, size_t destination_size, const char *source)
{
	if (destination == NULL || destination_size == 0)
	{
		return;
	}

	if (source == NULL)
	{
		destination[0] = '\0';
		return;
	}

	strncpy(destination, source, destination_size - 1);
	destination[destination_size - 1] = '\0';
}

/*
=============
bot_character_handles_equal

Checks whether a cache entry matches filename, character name, and optional skill.
=============
*/
static bool bot_character_handles_equal(const bot_character_cache_entry_t *entry,
	const char *filename,
	const char *character_name,
	float skill)
{
	if (entry == NULL || entry->profile == NULL)
	{
		return false;
	}

	if (skill >= 0.0f && fabsf(entry->skill - skill) > BOT_CHARACTER_SKILL_EPSILON)
	{
		return false;
	}

	if (bot_character_compare_filename(entry->filename, filename) != 0)
	{
		return false;
	}

	return strcmp(entry->character_name, bot_character_requested_name(character_name)) == 0;
}

/*
=============
bot_character_normalise_skill

Clamps the Q3-compatible public skill parameter to the retail range.
=============
*/
static float bot_character_normalise_skill(float skill)
{
	if (skill < 0.0f)
	{
		return -1.0f;
	}
	if (skill < 1.0f)
	{
		return 1.0f;
	}
	if (skill > 5.0f)
	{
		return 5.0f;
	}
	return skill;
}

/*
=============
bot_character_round_skill

Rounds public skill values the same way Q3 chooses a `skill N` block.
=============
*/
static int bot_character_round_skill(float skill)
{
	return (int)(skill + 0.5f);
}

/*
=============
bot_character_uses_q3_skill_loader

Routes successor skill requests, including default-character fallback cases.
=============
*/
static bool bot_character_uses_q3_skill_loader(const char *character_file)
{
	if (AI_CharacterFileUsesSkillBlocks(character_file))
	{
		return true;
	}
	if (AI_CharacterFileUsesNamedBlocks(character_file))
	{
		return false;
	}

	return AI_CharacterDefaultFileUsesSkillBlocks();
}

/*
=============
bot_character_find_cached_handle

Returns a handle for an already loaded tuple; negative skill matches any skill.
=============
*/
static int bot_character_find_cached_handle(const char *filename,
	const char *character_name,
	float skill)
{
	for (int handle = 1; handle < BOT_CHARACTER_MAX_HANDLES; ++handle)
	{
		bot_character_cache_entry_t *entry = &g_bot_character_cache[handle];
		if (bot_character_handles_equal(entry, filename, character_name, skill))
		{
			return handle;
		}
	}

	return BOT_CHARACTER_INVALID_HANDLE;
}

/*
=============
bot_character_allocate_handle

Finds a free cache slot for a newly loaded character profile.
=============
*/
static int bot_character_allocate_handle(void)
{
	for (int handle = 1; handle < BOT_CHARACTER_MAX_HANDLES; ++handle)
	{
		if (g_bot_character_cache[handle].profile == NULL)
		{
			return handle;
		}
	}

	return BOT_CHARACTER_INVALID_HANDLE;
}

/*
=============
bot_character_entry

Returns a mutable cache entry for a valid handle.
=============
*/
static bot_character_cache_entry_t *bot_character_entry(int handle)
{
	if (handle <= BOT_CHARACTER_INVALID_HANDLE || handle >= BOT_CHARACTER_MAX_HANDLES)
	{
		return NULL;
	}

	return &g_bot_character_cache[handle];
}

/*
=============
bot_character_should_reload

Returns whether character loads should bypass retained cache entries.
=============
*/
static bool bot_character_should_reload(void)
{
	return LibVarGetValue("bot_reloadcharacters") != 0.0f;
}

/*
=============
bot_character_cached_handle

Reports a matching cached character handle.
=============
*/
static int bot_character_cached_handle(const char *character_file,
	const char *character_name,
	float skill)
{
	int cached_handle = bot_character_find_cached_handle(character_file,
		character_name,
		skill);
	if (cached_handle == BOT_CHARACTER_INVALID_HANDLE)
	{
		return BOT_CHARACTER_INVALID_HANDLE;
	}

	bot_character_cache_entry_t *entry = bot_character_entry(cached_handle);
	BotLib_Print(PRT_MESSAGE,
		"[bot_character] reusing cached character %s:%s (skill %.2f, refs %u)\n",
		entry->filename,
		entry->character_name[0] != '\0' ? entry->character_name : "<first>",
		entry->skill,
		entry->refcount);
	return cached_handle;
}

/*
=============
bot_character_log_invalid_handle

Emits the non-fatal diagnostic used by reconstructed handle guards.
=============
*/
static void bot_character_log_invalid_handle(const char *context, int handle)
{
	BotLib_Print(PRT_WARNING,
		"[bot_character] %s: invalid handle %d\n",
		context != NULL ? context : "operation",
		handle);
}

/*
=============
bot_character_store_profile

Stores a loaded profile in the character cache.
=============
*/
static int bot_character_store_profile(const char *character_file,
	const char *character_name,
	float skill,
	ai_character_profile_t *profile)
{
	if (profile == NULL)
	{
		return BOT_CHARACTER_INVALID_HANDLE;
	}

	int handle = bot_character_allocate_handle();
	if (handle == BOT_CHARACTER_INVALID_HANDLE)
	{
		BotLib_Print(PRT_ERROR,
			"[bot_character] no free character slots for %s (skill %.2f)\n",
			character_file,
			skill);
		AI_FreeCharacter(profile);
		return BOT_CHARACTER_INVALID_HANDLE;
	}

	bot_character_cache_entry_t *entry = bot_character_entry(handle);
	entry->profile = profile;
	entry->skill = skill;
	entry->refcount = 1;
	bot_character_copy_string(entry->filename, sizeof(entry->filename), character_file);
	bot_character_copy_string(entry->character_name,
		sizeof(entry->character_name),
		bot_character_requested_name(character_name));

	BotLib_Print(PRT_MESSAGE,
		"[bot_character] loaded bot character %s:%s (skill %.2f)\n",
		entry->filename,
		entry->character_name[0] != '\0' ? entry->character_name : "<first>",
		entry->skill);

	return handle;
}

/*
=============
bot_character_load

Loads or reuses a Gladiator named character cache entry.
=============
*/
static int bot_character_load(const char *character_file,
	const char *character_name,
	float skill)
{
	if (character_file == NULL || character_file[0] == '\0')
	{
		BotLib_Print(PRT_WARNING,
			"[bot_character] BotLoadCharacter: missing filename.\n");
		return BOT_CHARACTER_INVALID_HANDLE;
	}

	float normalised_skill = bot_character_normalise_skill(skill);
	if (!bot_character_should_reload())
	{
		int cached_handle = bot_character_cached_handle(character_file,
			character_name,
			normalised_skill);
		if (cached_handle != BOT_CHARACTER_INVALID_HANDLE)
		{
			return cached_handle;
		}
	}

	ai_character_profile_t *profile =
		AI_LoadCharacterNamed(character_file, character_name, normalised_skill);
	if (profile == NULL)
	{
		BotLib_Print(PRT_ERROR,
			"[bot_character] couldn't load bot character %s from %s\n",
			character_name != NULL && character_name[0] != '\0' ? character_name : character_file,
			character_file);
		return BOT_CHARACTER_INVALID_HANDLE;
	}

	return bot_character_store_profile(character_file,
		character_name,
		normalised_skill,
		profile);
}

/*
=============
bot_character_apply_cached_defaults

Applies the Q3 default-character handle selected before a requested skill load.
=============
*/
static bool bot_character_apply_cached_defaults(ai_character_profile_t *profile,
	int default_handle)
{
	if (default_handle == BOT_CHARACTER_INVALID_HANDLE)
	{
		return true;
	}

	return AI_ApplyCharacterDefaults(profile, BotCharacterFromHandle(default_handle));
}

/*
=============
bot_character_load_q3_skill_internal

Loads or reuses a Q3 `skill N` character cache entry with Q3 default preload control.
=============
*/
static int bot_character_load_q3_skill_internal(const char *character_file,
	float skill,
	bool preload_default,
	bool force_cache,
	bool clamp_skill)
{
	if (character_file == NULL || character_file[0] == '\0')
	{
		BotLib_Print(PRT_WARNING,
			"[bot_character] BotLoadCharacterSkill: missing filename.\n");
		return BOT_CHARACTER_INVALID_HANDLE;
	}

	float requested_skill = clamp_skill ? bot_character_normalise_skill(skill) : skill;
	int rounded_skill_block = bot_character_round_skill(requested_skill);
	bool is_default_file = bot_character_is_q3_default_file(character_file);
	bool use_cache = force_cache || !bot_character_should_reload();
	int default_handle = BOT_CHARACTER_INVALID_HANDLE;
	if (preload_default &&
		!is_default_file &&
		AI_CharacterDefaultFileUsesSkillBlocks())
	{
		default_handle = bot_character_load_q3_skill_internal(BOT_CHARACTER_Q3_DEFAULT_FILE,
			requested_skill,
			false,
			true,
			false);
	}

	if (use_cache)
	{
		int cached_handle = bot_character_cached_handle(character_file,
			NULL,
			requested_skill);
		if (cached_handle != BOT_CHARACTER_INVALID_HANDLE)
		{
			return cached_handle;
		}
	}

	ai_character_profile_t *profile =
		AI_LoadCharacterSkillProfileBlock(character_file,
			requested_skill,
			rounded_skill_block,
			false);
	if (profile != NULL)
	{
		if (!bot_character_apply_cached_defaults(profile, default_handle))
		{
			AI_FreeCharacter(profile);
			return BOT_CHARACTER_INVALID_HANDLE;
		}

		float loaded_skill = AI_CharacterProfileSkill(profile);
		if (loaded_skill < 0.0f)
		{
			loaded_skill = requested_skill;
		}

		return bot_character_store_profile(character_file,
			NULL,
			loaded_skill,
			profile);
	}

	BotLib_Print(PRT_WARNING,
		"couldn't find skill %d in %s\n",
		rounded_skill_block,
		character_file);

	if (!is_default_file)
	{
		if (use_cache)
		{
			int cached_default = bot_character_cached_handle(BOT_CHARACTER_Q3_DEFAULT_FILE,
				NULL,
				requested_skill);
			if (cached_default != BOT_CHARACTER_INVALID_HANDLE)
			{
				return cached_default;
			}
		}

		profile = AI_LoadCharacterSkillProfileBlock(BOT_CHARACTER_Q3_DEFAULT_FILE,
			requested_skill,
			rounded_skill_block,
			false);
		if (profile != NULL)
		{
			float loaded_skill = AI_CharacterProfileSkill(profile);
			if (loaded_skill < 0.0f)
			{
				loaded_skill = requested_skill;
			}

			return bot_character_store_profile(BOT_CHARACTER_Q3_DEFAULT_FILE,
				NULL,
				loaded_skill,
				profile);
		}
	}

	if (use_cache)
	{
		int cached_any = bot_character_cached_handle(character_file, NULL, -1.0f);
		if (cached_any != BOT_CHARACTER_INVALID_HANDLE)
		{
			return cached_any;
		}
	}

	profile = AI_LoadCharacterSkillProfileBlock(character_file,
		requested_skill,
		-1,
		false);
	if (profile != NULL)
	{
		if (!bot_character_apply_cached_defaults(profile, default_handle))
		{
			AI_FreeCharacter(profile);
			return BOT_CHARACTER_INVALID_HANDLE;
		}

		float loaded_skill = AI_CharacterProfileSkill(profile);
		if (loaded_skill < 0.0f)
		{
			loaded_skill = requested_skill;
		}

		return bot_character_store_profile(character_file,
			NULL,
			loaded_skill,
			profile);
	}

	if (!is_default_file)
	{
		if (use_cache)
		{
			int cached_default_any = bot_character_cached_handle(BOT_CHARACTER_Q3_DEFAULT_FILE,
				NULL,
				-1.0f);
			if (cached_default_any != BOT_CHARACTER_INVALID_HANDLE)
			{
				return cached_default_any;
			}
		}

		profile = AI_LoadCharacterSkillProfileBlock(BOT_CHARACTER_Q3_DEFAULT_FILE,
			requested_skill,
			-1,
			false);
		if (profile != NULL)
		{
			float loaded_skill = AI_CharacterProfileSkill(profile);
			if (loaded_skill < 0.0f)
			{
				loaded_skill = requested_skill;
			}

			return bot_character_store_profile(BOT_CHARACTER_Q3_DEFAULT_FILE,
				NULL,
				loaded_skill,
				profile);
		}
	}

	BotLib_Print(PRT_WARNING,
		"couldn't load any skill from %s\n",
		character_file);
	BotLib_Print(PRT_ERROR,
		"[bot_character] couldn't load bot character skill %.2f from %s\n",
		requested_skill,
		character_file);
	return BOT_CHARACTER_INVALID_HANDLE;
}

/*
=============
bot_character_load_q3_skill

Loads or reuses a Q3 `skill N` character cache entry.
=============
*/
static int bot_character_load_q3_skill(const char *character_file, float skill)
{
	return bot_character_load_q3_skill_internal(character_file,
		skill,
		true,
		false,
		false);
}

/*
=============
BotLoadCharacter

Loads the first character block from a file for Q3-style callers.
=============
*/
int BotLoadCharacter(const char *character_file, float skill)
{
	if (bot_character_uses_q3_skill_loader(character_file))
	{
		float normalised_skill = bot_character_normalise_skill(skill);
		if (normalised_skill < 1.0f)
		{
			normalised_skill = 1.0f;
		}

		if (normalised_skill == 1.0f ||
			normalised_skill == 4.0f ||
			normalised_skill == 5.0f)
		{
			return bot_character_load_q3_skill(character_file, normalised_skill);
		}

		int cached_handle = bot_character_cached_handle(character_file,
			NULL,
			normalised_skill);
		if (cached_handle != BOT_CHARACTER_INVALID_HANDLE)
		{
			return cached_handle;
		}

		int first_skill = 0;
		int second_skill = 0;
		if (normalised_skill < 4.0f)
		{
			first_skill = bot_character_load_q3_skill(character_file, 1.0f);
			if (first_skill == BOT_CHARACTER_INVALID_HANDLE)
			{
				return BOT_CHARACTER_INVALID_HANDLE;
			}
			second_skill = bot_character_load_q3_skill(character_file, 4.0f);
			if (second_skill == BOT_CHARACTER_INVALID_HANDLE)
			{
				return first_skill;
			}
		}
		else
		{
			first_skill = bot_character_load_q3_skill(character_file, 4.0f);
			if (first_skill == BOT_CHARACTER_INVALID_HANDLE)
			{
				return BOT_CHARACTER_INVALID_HANDLE;
			}
			second_skill = bot_character_load_q3_skill(character_file, 5.0f);
			if (second_skill == BOT_CHARACTER_INVALID_HANDLE)
			{
				return first_skill;
			}
		}

		ai_character_profile_t *profile =
			AI_InterpolateCharacterProfiles(BotCharacterFromHandle(first_skill),
				BotCharacterFromHandle(second_skill),
				normalised_skill);
		if (profile == NULL)
		{
			return BOT_CHARACTER_INVALID_HANDLE;
		}

		bot_character_cache_entry_t *first_entry = bot_character_entry(first_skill);
		const char *interpolated_file = character_file;
		if (first_entry != NULL && first_entry->filename[0] != '\0')
		{
			interpolated_file = first_entry->filename;
		}

		return bot_character_store_profile(interpolated_file,
			NULL,
			normalised_skill,
			profile);
	}

	return bot_character_load(character_file, NULL, skill);
}

/*
=============
BotLoadNamedCharacter

Loads a specific Gladiator character block from a character file.
=============
*/
int BotLoadNamedCharacter(const char *character_file, const char *character_name, float skill)
{
	return bot_character_load(character_file, character_name, skill);
}

/*
=============
BotLoadCharacterSkill

Loads a character for direct skill-specific callers.
=============
*/
int BotLoadCharacterSkill(const char *character_file, float skill)
{
	if (character_file == NULL ||
		character_file[0] == '\0' ||
		bot_character_uses_q3_skill_loader(character_file))
	{
		return bot_character_load_q3_skill(character_file, skill);
	}

	return bot_character_load(character_file, NULL, bot_character_normalise_skill(skill));
}

/*
=============
BotFreeCharacter

Releases a character handle, respecting cache reference counts.
=============
*/
void BotFreeCharacter(int handle)
{
	bot_character_cache_entry_t *entry = bot_character_entry(handle);
	if (entry == NULL || entry->profile == NULL)
	{
		bot_character_log_invalid_handle("BotFreeCharacter", handle);
		return;
	}

	if (!bot_character_should_reload())
	{
		BotLib_Print(PRT_MESSAGE,
			"[bot_character] retained cached character %s:%s (skill %.2f)\n",
			entry->filename,
			entry->character_name[0] != '\0' ? entry->character_name : "<first>",
			entry->skill);
		return;
	}

	if (entry->refcount > 0)
	{
		entry->refcount--;
	}

	if (entry->refcount == 0)
	{
		BotLib_Print(PRT_MESSAGE,
			"[bot_character] freed bot character %s:%s (skill %.2f)\n",
			entry->filename,
			entry->character_name[0] != '\0' ? entry->character_name : "<first>",
			entry->skill);
		AI_FreeCharacter(entry->profile);
		memset(entry, 0, sizeof(*entry));
		return;
	}

	BotLib_Print(PRT_MESSAGE,
		"[bot_character] decremented %s:%s (skill %.2f) to %u references\n",
		entry->filename,
		entry->character_name[0] != '\0' ? entry->character_name : "<first>",
		entry->skill,
		entry->refcount);
}

/*
=============
BotFreeCharacterStrings

Frees a transient reconstructed profile.
=============
*/
void BotFreeCharacterStrings(ai_character_profile_t *profile)
{
	if (profile == NULL)
	{
		return;
	}

	AI_FreeCharacter(profile);
}

/*
=============
BotCharacterFromHandle

Returns the profile pointer for a cache handle.
=============
*/
ai_character_profile_t *BotCharacterFromHandle(int handle)
{
	bot_character_cache_entry_t *entry = bot_character_entry(handle);
	if (entry == NULL)
	{
		bot_character_log_invalid_handle("BotCharacterFromHandle", handle);
		return NULL;
	}

	return entry->profile;
}

/*
=============
bot_character_profile_for_queries

Resolves a character handle for characteristic accessors.
=============
*/
static ai_character_profile_t *bot_character_profile_for_queries(int handle)
{
	bot_character_cache_entry_t *entry = bot_character_entry(handle);
	if (entry == NULL || entry->profile == NULL)
	{
		bot_character_log_invalid_handle("Characteristic query", handle);
		return NULL;
	}

	return entry->profile;
}

/*
=============
bot_character_check_index

Mirrors the retail characteristic index and initialization diagnostics.
=============
*/
static ai_character_value_type_t bot_character_check_index(const ai_character_profile_t *profile,
	int index)
{
	int count = AI_CharacteristicCount(profile);
	if (index < 0 || index >= count)
	{
		BotLib_Print(PRT_ERROR,
			"characteristic %d does not exist\n",
			index);
		return AI_CHARACTER_VALUE_NONE;
	}

	ai_character_value_type_t type = AI_CharacteristicType(profile, index);
	if (type == AI_CHARACTER_VALUE_NONE)
	{
		BotLib_Print(PRT_ERROR,
			"characteristic %d is not initialized\n",
			index);
	}

	return type;
}

/*
=============
Characteristic_Float

Returns a float characteristic with integer conversion.
=============
*/
float Characteristic_Float(int handle, int index)
{
	ai_character_profile_t *profile = bot_character_profile_for_queries(handle);
	if (profile == NULL)
	{
		return 0.0f;
	}

	ai_character_value_type_t type = bot_character_check_index(profile, index);
	if (type == AI_CHARACTER_VALUE_INTEGER || type == AI_CHARACTER_VALUE_FLOAT)
	{
		return AI_CharacteristicAsFloat(profile, index);
	}

	if (type != AI_CHARACTER_VALUE_NONE)
	{
		BotLib_Print(PRT_ERROR,
			"characteristic %d is not a float\n",
			index);
	}
	return 0.0f;
}

/*
=============
Characteristic_BFloat

Returns a bounded float characteristic.
=============
*/
float Characteristic_BFloat(int handle, int index, float minimum, float maximum)
{
	if (minimum > maximum)
	{
		BotLib_Print(PRT_ERROR,
			"cannot bound characteristic %d between %f and %f\n",
			index,
			minimum,
			maximum);
		return 0.0f;
	}

	float value = Characteristic_Float(handle, index);
	if (value < minimum)
	{
		return minimum;
	}
	if (value > maximum)
	{
		return maximum;
	}
	return value;
}

/*
=============
Characteristic_Integer

Returns an integer characteristic with float truncation.
=============
*/
int Characteristic_Integer(int handle, int index)
{
	ai_character_profile_t *profile = bot_character_profile_for_queries(handle);
	if (profile == NULL)
	{
		return 0;
	}

	ai_character_value_type_t type = bot_character_check_index(profile, index);
	if (type == AI_CHARACTER_VALUE_INTEGER || type == AI_CHARACTER_VALUE_FLOAT)
	{
		return AI_CharacteristicAsInteger(profile, index);
	}

	if (type != AI_CHARACTER_VALUE_NONE)
	{
		BotLib_Print(PRT_ERROR,
			"characteristic %d is not a integer\n",
			index);
	}
	return 0;
}

/*
=============
Characteristic_BInteger

Returns a bounded integer characteristic.
=============
*/
int Characteristic_BInteger(int handle, int index, int minimum, int maximum)
{
	if (minimum > maximum)
	{
		BotLib_Print(PRT_ERROR,
			"cannot bound characteristic %d between %d and %d\n",
			index,
			minimum,
			maximum);
		return 0;
	}

	int value = Characteristic_Integer(handle, index);
	if (value < minimum)
	{
		return minimum;
	}
	if (value > maximum)
	{
		return maximum;
	}
	return value;
}

/*
=============
Characteristic_String

Copies a string characteristic into the caller's buffer.
=============
*/
void Characteristic_String(int handle, int index, char *buffer, int buffer_size)
{
	if (buffer == NULL || buffer_size <= 0)
	{
		return;
	}

	buffer[0] = '\0';

	ai_character_profile_t *profile = bot_character_profile_for_queries(handle);
	if (profile == NULL)
	{
		return;
	}

	ai_character_value_type_t type = bot_character_check_index(profile, index);
	if (type == AI_CHARACTER_VALUE_STRING)
	{
		const char *value = AI_CharacteristicAsString(profile, index);
		if (value != NULL)
		{
			snprintf(buffer, (size_t)buffer_size, "%s", value);
		}
		return;
	}

	if (type != AI_CHARACTER_VALUE_NONE)
	{
		BotLib_Print(PRT_ERROR,
			"characteristic %d is not a string\n",
			index);
	}
}

/*
=============
BotShutdownCharacters

Frees all cached character handles.
=============
*/
void BotShutdownCharacters(void)
{
	for (int handle = 1; handle < BOT_CHARACTER_MAX_HANDLES; ++handle)
	{
		bot_character_cache_entry_t *entry = &g_bot_character_cache[handle];
		if (entry->profile == NULL)
		{
			continue;
		}

		AI_FreeCharacter(entry->profile);
		memset(entry, 0, sizeof(*entry));
	}
}
