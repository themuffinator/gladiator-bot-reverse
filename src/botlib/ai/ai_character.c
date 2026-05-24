#include "botlib/ai_character/bot_character.h"

#include "botlib/ai_chat/ai_chat.h"
#include "botlib/ai_weapon/bot_weapon.h"
#include "botlib/ai_weight/bot_weight.h"
#include "botlib/common/l_assets.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/precomp/l_precomp.h"
#include "botlib/precomp/l_script.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * HLIL map for Gladiator character setup:
 *
 * sub_10029480 calls the character loader with settings.characterfile and
 * settings.charactername, then asks the characteristic table for item weights
 * (28), weapon weights (5), chat file (12), chat name (13), and gender (3).
 * sub_10029eb0 performs a two-pass parse over preprocessed character files:
 * the first pass finds the named `character "name"` block and counts the
 * largest characteristic index plus string storage, the second pass allocates
 * one packed block and fills typed entries. Accessors at sub_1002a5b0 through
 * sub_1002a810 validate indices, convert integer/float values, and report
 * exact type diagnostics for invalid lookups.
 * HLIL refs: dev_tools/gladiator.dll.bndb_hlil.txt lines 32483-32599
 * and 32844-33312.
 */

#define AI_CHARACTER_NAME_MAX 128
#define AI_CHARACTER_PATH_MAX 512
#define AI_CHARACTER_INDEX_LIMIT 1024

enum {
	AI_CHARACTER_INDEX_GENDER = 3,
	AI_CHARACTER_INDEX_WEAPON_WEIGHTS = 5,
	AI_CHARACTER_INDEX_CHAT_FILE = 12,
	AI_CHARACTER_INDEX_CHAT_NAME = 13,
	AI_CHARACTER_INDEX_ITEM_WEIGHTS = 28,
};

typedef struct ai_characteristic_s {
	ai_character_value_type_t type;
	union {
		int integer_value;
		float float_value;
		char *string_value;
	} value;
} ai_characteristic_t;

typedef struct ai_character_scan_s {
	bool found;
	int count;
	size_t string_bytes;
	char identifier[AI_CHARACTER_NAME_MAX];
} ai_character_scan_t;

typedef struct ai_character_definition_s {
	int num_characteristics;
	size_t string_bytes;
	char identifier[AI_CHARACTER_NAME_MAX];
	ai_characteristic_t characteristics[1];
} ai_character_definition_t;

int PC_ExpectAnyToken(pc_source_t *source, pc_token_t *token);
int PC_ExpectTokenString(pc_source_t *source, char *string);
void StripDoubleQuotes(char *string);

/*
=============
ai_character_requested_name

Returns the requested character name used in diagnostics.
=============
*/
static const char *ai_character_requested_name(const char *character_name)
{
	if (character_name == NULL || character_name[0] == '\0')
	{
		return "<first>";
	}

	return character_name;
}

/*
=============
ai_character_name_matches

Compares a parsed character identifier with the requested identifier.
=============
*/
static bool ai_character_name_matches(const char *identifier, const char *character_name)
{
	if (character_name == NULL || character_name[0] == '\0')
	{
		return true;
	}

	return identifier != NULL && strcmp(identifier, character_name) == 0;
}

/*
=============
ai_character_copy_string

Copies text into a bounded destination and always terminates it.
=============
*/
static void ai_character_copy_string(char *destination, size_t destination_size, const char *source)
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
ai_character_token_is

Checks the token text against a punctuation or name literal.
=============
*/
static bool ai_character_token_is(const pc_token_t *token, const char *text)
{
	return token != NULL && text != NULL && strcmp(token->string, text) == 0;
}

/*
=============
ai_character_parse_integer_token

Converts a numeric precompiler token to an integer index or value.
=============
*/
static bool ai_character_parse_integer_token(const pc_token_t *token, int *value)
{
	if (token == NULL || token->type != TT_NUMBER)
	{
		return false;
	}

	errno = 0;
	char *end = NULL;
	long parsed = strtol(token->string, &end, 0);
	if (end == token->string || (end != NULL && *end != '\0') || errno == ERANGE)
	{
		parsed = (long)token->intvalue;
	}

	if (parsed < INT_MIN || parsed > INT_MAX)
	{
		return false;
	}

	if (value != NULL)
	{
		*value = (int)parsed;
	}
	return true;
}

/*
=============
ai_character_parse_float_token

Converts a numeric precompiler token to a floating point value.
=============
*/
static bool ai_character_parse_float_token(const pc_token_t *token, float *value)
{
	if (token == NULL || token->type != TT_NUMBER)
	{
		return false;
	}

	errno = 0;
	char *end = NULL;
	double parsed = strtod(token->string, &end);
	if (end == token->string || (end != NULL && *end != '\0') || errno == ERANGE)
	{
		parsed = (double)token->floatvalue;
	}

	if (value != NULL)
	{
		*value = (float)parsed;
	}
	return true;
}

/*
=============
ai_character_expect_identifier

Reads and strips the string token following a `character` definition.
=============
*/
static bool ai_character_expect_identifier(pc_source_t *source, char *identifier, size_t identifier_size)
{
	pc_token_t token;
	if (!PC_ExpectTokenType(source, TT_STRING, 0, &token))
	{
		return false;
	}

	StripDoubleQuotes(token.string);
	ai_character_copy_string(identifier, identifier_size, token.string);
	return PC_ExpectTokenString(source, "{") != 0;
}

/*
=============
ai_character_skip_block

Skips a non-matching character block after its opening brace is consumed.
=============
*/
static bool ai_character_skip_block(pc_source_t *source)
{
	int depth = 1;
	pc_token_t token;

	while (PC_ExpectAnyToken(source, &token))
	{
		if (ai_character_token_is(&token, "{"))
		{
			depth++;
		}
		else if (ai_character_token_is(&token, "}"))
		{
			depth--;
			if (depth == 0)
			{
				return true;
			}
		}
	}

	return false;
}

/*
=============
ai_character_read_index

Reads and validates a characteristic index token.
=============
*/
static bool ai_character_read_index(const pc_token_t *token, int *index)
{
	if (token == NULL || token->type != TT_NUMBER || (token->subtype & TT_INTEGER) == 0)
	{
		BotLib_Print(PRT_ERROR,
			"expected integer index, found %s\n",
			token != NULL ? token->string : "<eof>");
		return false;
	}

	int parsed_index = 0;
	if (!ai_character_parse_integer_token(token, &parsed_index))
	{
		BotLib_Print(PRT_ERROR,
			"expected integer index, found %s\n",
			token->string);
		return false;
	}

	if (parsed_index < 0 || parsed_index >= AI_CHARACTER_INDEX_LIMIT)
	{
		BotLib_Print(PRT_ERROR,
			"characteristic index out of range [0, %d]\n",
			AI_CHARACTER_INDEX_LIMIT - 1);
		return false;
	}

	if (index != NULL)
	{
		*index = parsed_index;
	}
	return true;
}

/*
=============
ai_character_scan_block

First pass over the matching block: count entries and packed string bytes.
=============
*/
static bool ai_character_scan_block(pc_source_t *source, ai_character_scan_t *scan)
{
	pc_token_t token;

	while (PC_ExpectAnyToken(source, &token))
	{
		if (ai_character_token_is(&token, "}"))
		{
			return true;
		}

		int index = 0;
		if (!ai_character_read_index(&token, &index))
		{
			return false;
		}

		if (index + 1 > scan->count)
		{
			scan->count = index + 1;
		}

		pc_token_t value;
		if (!PC_ExpectAnyToken(source, &value))
		{
			return false;
		}

		if (value.type == TT_STRING)
		{
			StripDoubleQuotes(value.string);
			scan->string_bytes += strlen(value.string) + 1;
		}
		else if (value.type != TT_NUMBER)
		{
			BotLib_Print(PRT_ERROR,
				"expected integer, float or string, found %s\n",
				value.string);
			return false;
		}
	}

	return false;
}

/*
=============
ai_character_definition_string_storage

Returns the packed string storage immediately following the flexible table.
=============
*/
static char *ai_character_definition_string_storage(ai_character_definition_t *definition)
{
	int slot_count = definition->num_characteristics > 0 ? definition->num_characteristics : 1;
	return (char *)&definition->characteristics[slot_count];
}

/*
=============
ai_character_alloc_definition

Allocates the packed characteristic table discovered by the first pass.
=============
*/
static ai_character_definition_t *ai_character_alloc_definition(const ai_character_scan_t *scan)
{
	int slot_count = scan->count > 0 ? scan->count : 1;
	size_t extra_slots = (size_t)(slot_count - 1) * sizeof(ai_characteristic_t);
	size_t allocation_size = sizeof(ai_character_definition_t) + extra_slots + scan->string_bytes;

	ai_character_definition_t *definition =
		(ai_character_definition_t *)GetClearedMemory(allocation_size);
	if (definition == NULL)
	{
		return NULL;
	}

	definition->num_characteristics = scan->count;
	definition->string_bytes = scan->string_bytes;
	ai_character_copy_string(definition->identifier,
		sizeof(definition->identifier),
		scan->identifier);
	return definition;
}

/*
=============
ai_character_store_string

Copies a string value into the packed definition storage.
=============
*/
static bool ai_character_store_string(ai_character_definition_t *definition,
	ai_characteristic_t *slot,
	const char *value,
	char **cursor,
	char *end)
{
	size_t length = strlen(value) + 1;
	if (*cursor + length > end)
	{
		BotLib_Print(PRT_ERROR, "character string storage overflow\n");
		return false;
	}

	memcpy(*cursor, value, length);
	slot->type = AI_CHARACTER_VALUE_STRING;
	slot->value.string_value = *cursor;
	*cursor += length;

	(void)definition;
	return true;
}

/*
=============
ai_character_fill_block

Second pass over the matching block: fill typed characteristic entries.
=============
*/
static bool ai_character_fill_block(pc_source_t *source, ai_character_definition_t *definition)
{
	char *string_cursor = ai_character_definition_string_storage(definition);
	char *string_end = string_cursor + definition->string_bytes;
	pc_token_t token;

	while (PC_ExpectAnyToken(source, &token))
	{
		if (ai_character_token_is(&token, "}"))
		{
			return true;
		}

		int index = 0;
		if (!ai_character_read_index(&token, &index))
		{
			return false;
		}

		if (index < 0 || index >= definition->num_characteristics)
		{
			BotLib_Print(PRT_ERROR,
				"characteristic index out of range [0, %d]\n",
				definition->num_characteristics - 1);
			return false;
		}

		ai_characteristic_t *slot = &definition->characteristics[index];
		if (slot->type != AI_CHARACTER_VALUE_NONE)
		{
			BotLib_Print(PRT_ERROR,
				"characteristic %d already initialized\n",
				index);
			return false;
		}

		pc_token_t value;
		if (!PC_ExpectAnyToken(source, &value))
		{
			return false;
		}

		if (value.type == TT_STRING)
		{
			StripDoubleQuotes(value.string);
			if (!ai_character_store_string(definition, slot, value.string, &string_cursor, string_end))
			{
				return false;
			}
		}
		else if (value.type == TT_NUMBER)
		{
			if ((value.subtype & TT_FLOAT) != 0)
			{
				float parsed_value = 0.0f;
				if (!ai_character_parse_float_token(&value, &parsed_value))
				{
					return false;
				}
				slot->type = AI_CHARACTER_VALUE_FLOAT;
				slot->value.float_value = parsed_value;
			}
			else
			{
				int parsed_value = 0;
				if (!ai_character_parse_integer_token(&value, &parsed_value))
				{
					return false;
				}
				slot->type = AI_CHARACTER_VALUE_INTEGER;
				slot->value.integer_value = parsed_value;
			}
		}
		else
		{
			BotLib_Print(PRT_ERROR,
				"expected integer, float or string, found %s\n",
				value.string);
			return false;
		}
	}

	return false;
}

/*
=============
ai_character_scan_source

Finds the requested character block and gathers allocation sizes.
=============
*/
static bool ai_character_scan_source(pc_source_t *source,
	const char *filename,
	const char *character_name,
	ai_character_scan_t *scan)
{
	pc_token_t token;

	while (PC_ReadToken(source, &token))
	{
		if (token.type != TT_NAME || strcmp(token.string, "character") != 0)
		{
			BotLib_Print(PRT_ERROR, "unknown definition %s\n", token.string);
			return false;
		}

		char identifier[AI_CHARACTER_NAME_MAX];
		if (!ai_character_expect_identifier(source, identifier, sizeof(identifier)))
		{
			return false;
		}

		if (!ai_character_name_matches(identifier, character_name))
		{
			if (!ai_character_skip_block(source))
			{
				return false;
			}
			continue;
		}

		ai_character_copy_string(scan->identifier, sizeof(scan->identifier), identifier);
		if (!ai_character_scan_block(source, scan))
		{
			return false;
		}
		scan->found = true;
		return true;
	}

	BotLib_Print(PRT_ERROR,
		"couldn't find character %s in %s\n",
		ai_character_requested_name(character_name),
		filename);
	return false;
}

/*
=============
ai_character_fill_source

Replays the source and fills the packed definition for the matching block.
=============
*/
static bool ai_character_fill_source(pc_source_t *source,
	const char *filename,
	const char *character_name,
	ai_character_definition_t *definition)
{
	pc_token_t token;

	while (PC_ReadToken(source, &token))
	{
		if (token.type != TT_NAME || strcmp(token.string, "character") != 0)
		{
			BotLib_Print(PRT_ERROR, "unknown definition %s\n", token.string);
			return false;
		}

		char identifier[AI_CHARACTER_NAME_MAX];
		if (!ai_character_expect_identifier(source, identifier, sizeof(identifier)))
		{
			return false;
		}

		if (!ai_character_name_matches(identifier, character_name))
		{
			if (!ai_character_skip_block(source))
			{
				return false;
			}
			continue;
		}

		return ai_character_fill_block(source, definition);
	}

	BotLib_Print(PRT_ERROR,
		"couldn't find character %s in %s\n",
		ai_character_requested_name(character_name),
		filename);
	return false;
}

/*
=============
ai_character_open_source

Loads a character file through the reconstructed precompiler.
=============
*/
static pc_source_t *ai_character_open_source(const char *filename)
{
	pc_source_t *source = PC_LoadSourceFile(filename);
	if (source == NULL)
	{
		BotLib_Print(PRT_ERROR, "counldn't load %s\n", filename);
	}

	return source;
}

/*
=============
ai_parse_definition

Builds the packed character definition with Gladiator's two-pass parser.
=============
*/
static ai_character_definition_t *ai_parse_definition(const char *filename,
	const char *character_name)
{
	if (filename == NULL || filename[0] == '\0')
	{
		return NULL;
	}

	ai_character_scan_t scan;
	memset(&scan, 0, sizeof(scan));

	pc_source_t *source = ai_character_open_source(filename);
	if (source == NULL)
	{
		return NULL;
	}

	bool scanned = ai_character_scan_source(source, filename, character_name, &scan);
	PC_FreeSource(source);
	if (!scanned || !scan.found)
	{
		return NULL;
	}

	ai_character_definition_t *definition = ai_character_alloc_definition(&scan);
	if (definition == NULL)
	{
		return NULL;
	}

	source = ai_character_open_source(filename);
	if (source == NULL)
	{
		FreeMemory(definition);
		return NULL;
	}

	bool filled = ai_character_fill_source(source, filename, character_name, definition);
	PC_FreeSource(source);
	if (!filled)
	{
		FreeMemory(definition);
		return NULL;
	}

	BotLib_Print(PRT_MESSAGE,
		"loaded %s from %s\n",
		definition->identifier[0] != '\0' ? definition->identifier : ai_character_requested_name(character_name),
		filename);
	return definition;
}

/*
=============
ai_profile_display_name

Returns a useful profile label for setup diagnostics.
=============
*/
static const char *ai_profile_display_name(const ai_character_profile_t *profile,
	const char *fallback)
{
	if (profile == NULL)
	{
		return fallback != NULL ? fallback : "<unknown>";
	}

	if (profile->character_name[0] != '\0')
	{
		return profile->character_name;
	}

	if (profile->definition_blob != NULL && profile->definition_blob->identifier[0] != '\0')
	{
		return profile->definition_blob->identifier;
	}

	if (profile->character_filename[0] != '\0')
	{
		return profile->character_filename;
	}

	return fallback != NULL ? fallback : "<unknown>";
}

/*
=============
ai_profile_load_item_weights

Loads the item weights named by characteristic 28.
=============
*/
static bool ai_profile_load_item_weights(ai_character_profile_t *profile,
	const char *character_name)
{
	const char *item_weights_file = AI_CharacteristicAsString(profile, AI_CHARACTER_INDEX_ITEM_WEIGHTS);
	if (item_weights_file == NULL || item_weights_file[0] == '\0')
	{
		BotLib_Print(PRT_ERROR,
			"[ai_character] %s is missing an item weight file.\n",
			character_name);
		return false;
	}

	char resolved_path[AI_CHARACTER_PATH_MAX];
	if (!BotLib_ResolveAssetPath(item_weights_file, "bots", resolved_path, sizeof(resolved_path)))
	{
		BotLib_Print(PRT_ERROR,
			"[ai_character] failed to locate item weights %s for %s.\n",
			item_weights_file,
			character_name);
		return false;
	}

	profile->item_weights = ReadWeightConfig(resolved_path);
	if (profile->item_weights == NULL)
	{
		BotLib_Print(PRT_ERROR,
			"[ai_character] failed to load item weights %s for %s.\n",
			item_weights_file,
			character_name);
		return false;
	}

	BotLib_Print(PRT_DEVELOPER,
		"%6d bytes item weights\n",
		(int)MemoryByteSize(profile->item_weights));
	return true;
}

/*
=============
ai_profile_load_weapon_weights

Loads the weapon weights named by characteristic 5.
=============
*/
static bool ai_profile_load_weapon_weights(ai_character_profile_t *profile,
	const char *character_name)
{
	const char *weapon_weights_file = AI_CharacteristicAsString(profile, AI_CHARACTER_INDEX_WEAPON_WEIGHTS);
	if (weapon_weights_file == NULL || weapon_weights_file[0] == '\0')
	{
		BotLib_Print(PRT_ERROR,
			"[ai_character] %s is missing a weapon weight file.\n",
			character_name);
		return false;
	}

	char resolved_path[AI_CHARACTER_PATH_MAX];
	if (!BotLib_ResolveAssetPath(weapon_weights_file, "bots", resolved_path, sizeof(resolved_path)))
	{
		BotLib_Print(PRT_ERROR,
			"[ai_character] failed to locate weapon weights %s for %s.\n",
			weapon_weights_file,
			character_name);
		return false;
	}

	profile->weapon_weights = AI_LoadWeaponWeights(resolved_path);
	if (profile->weapon_weights == NULL)
	{
		BotLib_Print(PRT_ERROR,
			"[ai_character] failed to load weapon weights %s for %s.\n",
			weapon_weights_file,
			character_name);
		return false;
	}

	BotLib_Print(PRT_DEVELOPER,
		"%6d bytes weapon weights\n",
		(int)MemoryByteSize(profile->weapon_weights));
	return true;
}

/*
=============
ai_profile_load_chat

Loads the chat file and chat persona named by characteristics 12 and 13.
=============
*/
static bool ai_profile_load_chat(ai_character_profile_t *profile,
	const char *character_name)
{
	const char *chat_file = AI_CharacteristicAsString(profile, AI_CHARACTER_INDEX_CHAT_FILE);
	const char *chat_name = AI_CharacteristicAsString(profile, AI_CHARACTER_INDEX_CHAT_NAME);
	if (chat_file == NULL || chat_file[0] == '\0' || chat_name == NULL || chat_name[0] == '\0')
	{
		BotLib_Print(PRT_ERROR,
			"[ai_character] %s is missing chat configuration.\n",
			character_name);
		return false;
	}

	profile->chat_state = BotAllocChatState();
	if (profile->chat_state == NULL)
	{
		BotLib_Print(PRT_ERROR,
			"[ai_character] failed to allocate chat state for %s.\n",
			character_name);
		return false;
	}

	char resolved_path[AI_CHARACTER_PATH_MAX];
	if (!BotLib_ResolveAssetPath(chat_file, "bots", resolved_path, sizeof(resolved_path)))
	{
		BotLib_Print(PRT_ERROR,
			"[ai_character] failed to locate chat file %s for %s.\n",
			chat_file,
			character_name);
		return false;
	}

	if (!BotLoadChatFile(profile->chat_state, resolved_path, chat_name))
	{
		BotLib_Print(PRT_ERROR,
			"[ai_character] failed to load chat file %s (%s) for %s.\n",
			resolved_path,
			chat_name,
			character_name);
		return false;
	}

	BotSetChatName(profile->chat_state, chat_name, -1);
	const char *gender = AI_CharacteristicAsString(profile, AI_CHARACTER_INDEX_GENDER);
	if (gender != NULL && (gender[0] == 'f' || gender[0] == 'F'))
	{
		BotSetChatGender(profile->chat_state, CHAT_GENDERFEMALE);
	}
	else if (gender != NULL && (gender[0] == 'm' || gender[0] == 'M'))
	{
		BotSetChatGender(profile->chat_state, CHAT_GENDERMALE);
	}
	else
	{
		BotSetChatGender(profile->chat_state, CHAT_GENDERLESS);
	}

	BotLib_Print(PRT_DEVELOPER,
		"%6d bytes chat file\n",
		(int)MemoryByteSize(profile->chat_state));
	return true;
}

/*
=============
AI_LoadCharacterNamed

Loads a named Gladiator character and wires item weights, weapon weights, and chat.
=============
*/
ai_character_profile_t *AI_LoadCharacterNamed(const char *filename,
	const char *character_name,
	float skill)
{
	ai_character_definition_t *definition = ai_parse_definition(filename, character_name);
	if (definition == NULL)
	{
		BotLib_Print(PRT_ERROR,
			"[ai_character] failed to parse character %s from %s.\n",
			ai_character_requested_name(character_name),
			filename != NULL ? filename : "<null>");
		return NULL;
	}

	ai_character_profile_t *profile = (ai_character_profile_t *)GetClearedMemory(sizeof(*profile));
	if (profile == NULL)
	{
		FreeMemory(definition);
		return NULL;
	}

	profile->requested_skill = skill;
	profile->definition_blob = definition;
	ai_character_copy_string(profile->character_filename,
		sizeof(profile->character_filename),
		filename);
	ai_character_copy_string(profile->character_name,
		sizeof(profile->character_name),
		definition->identifier[0] != '\0' ? definition->identifier : character_name);

	BotLib_Print(PRT_DEVELOPER,
		"%6d bytes character\n",
		(int)MemoryByteSize(profile->definition_blob));

	const char *profile_name = ai_profile_display_name(profile, filename);

	if (!ai_profile_load_item_weights(profile, profile_name))
	{
		goto free_profile;
	}

	if (!ai_profile_load_weapon_weights(profile, profile_name))
	{
		goto free_item_weights;
	}

	if (!ai_profile_load_chat(profile, profile_name))
	{
		goto free_weapon_weights;
	}

	return profile;

free_weapon_weights:
	if (profile->chat_state != NULL)
	{
		BotFreeChatState(profile->chat_state);
		profile->chat_state = NULL;
	}
	if (profile->weapon_weights != NULL)
	{
		AI_FreeWeaponWeights(profile->weapon_weights);
		profile->weapon_weights = NULL;
	}

free_item_weights:
	if (profile->item_weights != NULL)
	{
		FreeWeightConfig(profile->item_weights);
		profile->item_weights = NULL;
	}

free_profile:
	FreeMemory(profile->definition_blob);
	profile->definition_blob = NULL;
	FreeMemory(profile);
	return NULL;
}

/*
=============
AI_LoadCharacter

Loads the first character block in a file for direct botlib callers.
=============
*/
ai_character_profile_t *AI_LoadCharacter(const char *filename, float skill)
{
	return AI_LoadCharacterNamed(filename, NULL, skill);
}

/*
=============
AI_FreeCharacter

Frees a character profile and all resources wired through setup.
=============
*/
void AI_FreeCharacter(ai_character_profile_t *profile)
{
	if (profile == NULL)
	{
		return;
	}

	if (profile->chat_state != NULL)
	{
		BotFreeChatState(profile->chat_state);
		profile->chat_state = NULL;
	}

	if (profile->weapon_weights != NULL)
	{
		AI_FreeWeaponWeights(profile->weapon_weights);
		profile->weapon_weights = NULL;
	}

	if (profile->item_weights != NULL)
	{
		FreeWeightConfig(profile->item_weights);
		profile->item_weights = NULL;
	}

	if (profile->definition_blob != NULL)
	{
		FreeMemory(profile->definition_blob);
		profile->definition_blob = NULL;
	}

	FreeMemory(profile);
}

/*
=============
AI_ItemWeightsForCharacter

Returns the item weight configuration attached to a character profile.
=============
*/
bot_weight_config_t *AI_ItemWeightsForCharacter(const ai_character_profile_t *profile)
{
	return profile != NULL ? profile->item_weights : NULL;
}

/*
=============
AI_WeaponWeightsForCharacter

Returns the weapon weight table attached to a character profile.
=============
*/
ai_weapon_weights_t *AI_WeaponWeightsForCharacter(const ai_character_profile_t *profile)
{
	return profile != NULL ? profile->weapon_weights : NULL;
}

/*
=============
ai_definition

Returns the packed definition behind a profile.
=============
*/
static const ai_character_definition_t *ai_definition(const ai_character_profile_t *profile)
{
	return profile != NULL ? profile->definition_blob : NULL;
}

/*
=============
AI_CharacteristicCount

Returns the number of addressable characteristic slots in a profile.
=============
*/
int AI_CharacteristicCount(const ai_character_profile_t *profile)
{
	const ai_character_definition_t *definition = ai_definition(profile);
	return definition != NULL ? definition->num_characteristics : 0;
}

/*
=============
AI_CharacteristicType

Returns the stored type for an initialized characteristic slot.
=============
*/
ai_character_value_type_t AI_CharacteristicType(const ai_character_profile_t *profile, int index)
{
	const ai_character_definition_t *definition = ai_definition(profile);
	if (definition == NULL || index < 0 || index >= definition->num_characteristics)
	{
		return AI_CHARACTER_VALUE_NONE;
	}

	return definition->characteristics[index].type;
}

/*
=============
AI_CharacteristicAsFloat

Returns a float characteristic, converting integers like retail botlib.
=============
*/
float AI_CharacteristicAsFloat(const ai_character_profile_t *profile, int index)
{
	const ai_character_definition_t *definition = ai_definition(profile);
	if (definition == NULL || index < 0 || index >= definition->num_characteristics)
	{
		return 0.0f;
	}

	const ai_characteristic_t *slot = &definition->characteristics[index];
	if (slot->type == AI_CHARACTER_VALUE_FLOAT)
	{
		return slot->value.float_value;
	}
	if (slot->type == AI_CHARACTER_VALUE_INTEGER)
	{
		return (float)slot->value.integer_value;
	}

	return 0.0f;
}

/*
=============
AI_CharacteristicAsInteger

Returns an integer characteristic, truncating floats like the original code.
=============
*/
int AI_CharacteristicAsInteger(const ai_character_profile_t *profile, int index)
{
	const ai_character_definition_t *definition = ai_definition(profile);
	if (definition == NULL || index < 0 || index >= definition->num_characteristics)
	{
		return 0;
	}

	const ai_characteristic_t *slot = &definition->characteristics[index];
	if (slot->type == AI_CHARACTER_VALUE_INTEGER)
	{
		return slot->value.integer_value;
	}
	if (slot->type == AI_CHARACTER_VALUE_FLOAT)
	{
		return (int)slot->value.float_value;
	}

	return 0;
}

/*
=============
AI_CharacteristicAsString

Returns a string characteristic pointer or NULL for non-string slots.
=============
*/
const char *AI_CharacteristicAsString(const ai_character_profile_t *profile, int index)
{
	const ai_character_definition_t *definition = ai_definition(profile);
	if (definition == NULL || index < 0 || index >= definition->num_characteristics)
	{
		return NULL;
	}

	const ai_characteristic_t *slot = &definition->characteristics[index];
	return slot->type == AI_CHARACTER_VALUE_STRING ? slot->value.string_value : NULL;
}
