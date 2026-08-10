#include "botlib/ai_character/bot_character.h"

#include "botlib/common/l_assets.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/precomp/l_precomp.h"
#include "botlib/precomp/l_script.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
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
 * the first pass finds the named `character "name"` block and tracks the
 * highest characteristic index plus string storage. The allocation includes
 * that highest slot, while the public count retains retail's hidden-sentinel
 * maximum. The second pass fills typed entries. Accessors at sub_1002a5b0 through
 * sub_1002a810 validate indices, convert integer/float values, and report
 * exact type diagnostics for invalid lookups.
 * HLIL refs: dev_tools/gladiator.dll.bndb_hlil.txt lines 32483-32599
 * and 32844-33312.
 */

#define AI_CHARACTER_NAME_MAX MAX_TOKEN
#define AI_CHARACTER_RETAIL_FILENAME_SIZE 0x104
#define AI_CHARACTER_Q3_MAX_CHARACTERISTICS 80
#define AI_CHARACTER_Q3_DEFAULT_FILE "bots/default_c.c"

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
	float skill;
	char identifier[AI_CHARACTER_NAME_MAX];
} ai_character_scan_t;

typedef struct ai_character_definition_s {
	int32_t highest_characteristic;
	ai_characteristic_t characteristics[1];
} ai_character_definition_t;

typedef struct ai_character_definition_metadata_s {
	ai_character_definition_t *definition;
	int num_characteristics;
	size_t string_bytes;
	float skill;
	char identifier[AI_CHARACTER_NAME_MAX];
	struct ai_character_definition_metadata_s *next;
} ai_character_definition_metadata_t;

/*
 * Retail x86 layout is exactly:
 *     int32_t highest_characteristic;
 *     ai_characteristic_t characteristics[highest + 1]; // 8 bytes each
 *     char strings[];
 *
 * Native x64 builds retain the same field order but naturally widen and align
 * the pointer-bearing characteristic slots.
 */
_Static_assert(offsetof(ai_character_definition_t, highest_characteristic) == 0,
	"retail character allocation must begin with the public header");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(offsetof(ai_character_definition_t, characteristics) == sizeof(int32_t),
	"retail character slots must immediately follow the x86 header");
_Static_assert(sizeof(ai_characteristic_t) == 8,
	"retail x86 character slots must be eight bytes");
#endif

static ai_character_definition_metadata_t *g_ai_character_metadata;
static char g_ai_character_empty_string;

void StripDoubleQuotes(char *string);

/*
=============
ai_character_requested_name

Returns the requested character name used in diagnostics.
=============
*/
static const char *ai_character_requested_name(const char *character_name)
{
	if (character_name == NULL)
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
	if (character_name == NULL)
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
ai_character_definition_metadata

Finds the private metadata sidecar for a packed definition.
=============
*/
static ai_character_definition_metadata_t *ai_character_definition_metadata(
	const ai_character_definition_t *definition)
{
	for (ai_character_definition_metadata_t *metadata = g_ai_character_metadata;
		metadata != NULL;
		metadata = metadata->next)
	{
		if (metadata->definition == definition)
		{
			return metadata;
		}
	}

	return NULL;
}

/*
=============
ai_character_register_definition

Attaches private parser metadata without changing the retail allocation.
=============
*/
static bool ai_character_register_definition(ai_character_definition_t *definition,
	const ai_character_scan_t *scan)
{
	if (definition == NULL || scan == NULL)
	{
		return false;
	}

	ai_character_definition_metadata_t *metadata =
		(ai_character_definition_metadata_t *)calloc(1, sizeof(*metadata));
	if (metadata == NULL)
	{
		return false;
	}

	metadata->definition = definition;
	metadata->num_characteristics = scan->count;
	metadata->string_bytes = scan->string_bytes;
	metadata->skill = scan->skill;
	ai_character_copy_string(metadata->identifier,
		sizeof(metadata->identifier),
		scan->identifier);
	metadata->next = g_ai_character_metadata;
	g_ai_character_metadata = metadata;
	return true;
}

/*
=============
ai_character_release_definition

Unlinks a definition sidecar and frees the packed retail allocation.
=============
*/
static void ai_character_release_definition(ai_character_definition_t *definition)
{
	if (definition == NULL)
	{
		return;
	}

	ai_character_definition_metadata_t **link = &g_ai_character_metadata;
	while (*link != NULL)
	{
		ai_character_definition_metadata_t *metadata = *link;
		if (metadata->definition == definition)
		{
			*link = metadata->next;
			free(metadata);
			break;
		}
		link = &metadata->next;
	}

	FreeMemory(definition);
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

	if (value != NULL)
	{
		uint32_t low = (uint32_t)token->intvalue;
		*value = low <= INT32_MAX
			? (int)low
			: (int)((int64_t)low - INT64_C(4294967296));
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

	if (value != NULL)
	{
		*value = (float)token->floatvalue;
	}
	return true;
}

/*
=============
ai_character_retail_ftol

Emulates the retail x87 __ftol helper: truncate to a signed 64-bit integer,
then return its low 32 bits. Masked invalid conversions yield integer
indefinite, whose low 32 bits are zero.
=============
*/
static int ai_character_retail_ftol(float value)
{
	double extended = (double)value;
	if (!(extended >= -9223372036854775808.0 &&
		extended < 9223372036854775808.0))
	{
		return 0;
	}

	int64_t wide = (int64_t)extended;
	uint32_t low = (uint32_t)(uint64_t)wide;
	if (low <= INT32_MAX)
	{
		return (int)low;
	}

	return (int)((int64_t)low - INT64_C(4294967296));
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

Reads a characteristic index token and applies only the requested safety bound.
=============
*/
static bool ai_character_read_index(pc_source_t *source,
	const pc_token_t *token,
	int max_index,
	int *index)
{
	if (token == NULL || token->type != TT_NUMBER || (token->subtype & TT_INTEGER) == 0)
	{
		SourceError(source,
			"expected integer index, found %s\n",
			token != NULL ? token->string : "<eof>");
		return false;
	}

	int parsed_index = 0;
	if (!ai_character_parse_integer_token(token, &parsed_index))
	{
		SourceError(source,
			"expected integer index, found %s\n",
			token->string);
		return false;
	}

	if (parsed_index < 0 ||
		(max_index >= 0 && parsed_index > max_index) ||
		(max_index < 0 && parsed_index == INT_MAX))
	{
		int diagnostic_max = max_index >= 0 ? max_index : INT_MAX - 1;
		SourceError(source,
			"characteristic index out of range [0, %d]\n",
			diagnostic_max);
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

First pass over the matching block: track the highest slot and packed strings.
=============
*/
static bool ai_character_scan_block(pc_source_t *source,
	ai_character_scan_t *scan,
	int max_index)
{
	pc_token_t token;

	while (PC_ExpectAnyToken(source, &token))
	{
		if (ai_character_token_is(&token, "}"))
		{
			return true;
		}

		int index = 0;
		if (!ai_character_read_index(source, &token, max_index, &index))
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
			size_t length = strlen(value.string) + 1;
			if (length > (size_t)-1 - scan->string_bytes)
			{
				return false;
			}
			scan->string_bytes += length;
		}
		else if (value.type != TT_NUMBER)
		{
			SourceError(source,
				"expected integer, float or string, found %s\n",
				value.string);
			return false;
		}
	}

	return true;
}

/*
=============
ai_character_definition_string_storage

Returns the packed string storage immediately following the flexible table.
=============
*/
static char *ai_character_definition_string_storage(ai_character_definition_t *definition)
{
	ai_character_definition_metadata_t *metadata =
		ai_character_definition_metadata(definition);
	if (metadata == NULL)
	{
		return NULL;
	}

	int slot_count =
		metadata->num_characteristics > 0 ? metadata->num_characteristics : 1;
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
	size_t slot_offset = offsetof(ai_character_definition_t, characteristics);
	if (scan->string_bytes > (size_t)-1 - slot_offset)
	{
		return NULL;
	}

	size_t available = (size_t)-1 - slot_offset - scan->string_bytes;
	if ((size_t)slot_count > available / sizeof(ai_characteristic_t))
	{
		return NULL;
	}

	size_t slot_bytes = (size_t)slot_count * sizeof(ai_characteristic_t);
	size_t allocation_size = slot_offset + slot_bytes + scan->string_bytes;

	ai_character_definition_t *definition =
		(ai_character_definition_t *)GetClearedMemory(allocation_size);
	if (definition == NULL)
	{
		return NULL;
	}

	definition->highest_characteristic = scan->count;
	if (!ai_character_register_definition(definition, scan))
	{
		FreeMemory(definition);
		return NULL;
	}

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
ai_character_slot_string_bytes

Returns packed storage needed by a string characteristic.
=============
*/
static size_t ai_character_slot_string_bytes(const ai_characteristic_t *slot)
{
	if (slot == NULL || slot->type != AI_CHARACTER_VALUE_STRING || slot->value.string_value == NULL)
	{
		return 0;
	}

	return strlen(slot->value.string_value) + 1;
}

/*
=============
ai_character_definition_slot

Returns a characteristic slot when it exists in a definition.
=============
*/
static const ai_characteristic_t *ai_character_definition_slot(const ai_character_definition_t *definition,
	int index)
{
	ai_character_definition_metadata_t *metadata =
		ai_character_definition_metadata(definition);
	if (metadata == NULL ||
		index < 0 ||
		index >= metadata->num_characteristics)
	{
		return NULL;
	}

	return &definition->characteristics[index];
}

/*
=============
ai_character_copy_slot

Copies one typed characteristic into a packed destination definition.
=============
*/
static bool ai_character_copy_slot(ai_character_definition_t *definition,
	ai_characteristic_t *destination,
	const ai_characteristic_t *source,
	char **cursor,
	char *end)
{
	if (definition == NULL || destination == NULL || source == NULL)
	{
		return false;
	}

	switch (source->type)
	{
	case AI_CHARACTER_VALUE_INTEGER:
		destination->type = AI_CHARACTER_VALUE_INTEGER;
		destination->value.integer_value = source->value.integer_value;
		return true;
	case AI_CHARACTER_VALUE_FLOAT:
		destination->type = AI_CHARACTER_VALUE_FLOAT;
		destination->value.float_value = source->value.float_value;
		return true;
	case AI_CHARACTER_VALUE_STRING:
		if (source->value.string_value == NULL)
		{
			return true;
		}
		return ai_character_store_string(definition,
			destination,
			source->value.string_value,
			cursor,
			end);
	default:
		return true;
	}
}

/*
=============
ai_character_fill_block

Second pass over the matching block: fill typed characteristic entries.
=============
*/
static bool ai_character_fill_block(pc_source_t *source,
	ai_character_definition_t *definition,
	char **string_cursor,
	char *string_end)
{
	ai_character_definition_metadata_t *metadata =
		ai_character_definition_metadata(definition);
	if (metadata == NULL)
	{
		return false;
	}

	int max_index = metadata->num_characteristics - 1;
	pc_token_t token;

	while (PC_ExpectAnyToken(source, &token))
	{
		if (ai_character_token_is(&token, "}"))
		{
			return true;
		}

		int index = 0;
		if (!ai_character_read_index(source, &token, max_index, &index))
		{
			return false;
		}

		if (index < 0 || index >= metadata->num_characteristics)
		{
			SourceError(source,
				"characteristic index out of range [0, %d]\n",
				metadata->num_characteristics - 1);
			return false;
		}

		ai_characteristic_t *slot = &definition->characteristics[index];
		if (slot->type != AI_CHARACTER_VALUE_NONE)
		{
			SourceError(source,
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
			if (!ai_character_store_string(definition, slot, value.string, string_cursor, string_end))
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
			SourceError(source,
				"expected integer, float or string, found %s\n",
				value.string);
			return false;
		}
	}

	return true;
}

/*
=============
ai_character_alloc_derived_definition

Allocates a packed definition for reconstructed default/interpolated tables.
=============
*/
static ai_character_definition_t *ai_character_alloc_derived_definition(int count,
	int public_count,
	size_t string_bytes,
	float skill,
	const char *identifier)
{
	ai_character_scan_t scan;
	memset(&scan, 0, sizeof(scan));
	scan.count = count;
	scan.string_bytes = string_bytes;
	scan.skill = skill;
	ai_character_copy_string(scan.identifier, sizeof(scan.identifier), identifier);
	ai_character_definition_t *definition = ai_character_alloc_definition(&scan);
	if (definition != NULL)
	{
		definition->highest_characteristic = public_count;
	}
	return definition;
}

/*
=============
ai_character_public_count

Returns the explicitly recorded public characteristic count.
=============
*/
static int ai_character_public_count(const ai_character_definition_t *definition)
{
	if (definition == NULL)
	{
		return 0;
	}

	return definition->highest_characteristic;
}

/*
=============
ai_character_merge_default_definition

Fills missing slots from a Q3 default character definition.
=============
*/
static ai_character_definition_t *ai_character_merge_default_definition(const ai_character_definition_t *definition,
	const ai_character_definition_t *defaults)
{
	if (definition == NULL)
	{
		return NULL;
	}
	if (defaults == NULL)
	{
		return NULL;
	}

	ai_character_definition_metadata_t *metadata =
		ai_character_definition_metadata(definition);
	ai_character_definition_metadata_t *default_metadata =
		ai_character_definition_metadata(defaults);
	if (metadata == NULL || default_metadata == NULL)
	{
		return NULL;
	}

	int count = metadata->num_characteristics;
	if (default_metadata->num_characteristics > count)
	{
		count = default_metadata->num_characteristics;
	}

	int public_count = ai_character_public_count(definition);
	int default_public_count = ai_character_public_count(defaults);
	if (default_public_count > public_count)
	{
		public_count = default_public_count;
	}
	if (count > public_count)
	{
		count = public_count;
	}

	size_t string_bytes = 0;
	for (int index = 0; index < count; ++index)
	{
		const ai_characteristic_t *source = ai_character_definition_slot(definition, index);
		if (source == NULL || source->type == AI_CHARACTER_VALUE_NONE)
		{
			source = ai_character_definition_slot(defaults, index);
		}
		string_bytes += ai_character_slot_string_bytes(source);
	}

	ai_character_definition_t *merged =
		ai_character_alloc_derived_definition(count,
			public_count,
			string_bytes,
			metadata->skill,
			metadata->identifier);
	if (merged == NULL)
	{
		return NULL;
	}

	ai_character_definition_metadata_t *merged_metadata =
		ai_character_definition_metadata(merged);
	char *cursor = ai_character_definition_string_storage(merged);
	if (merged_metadata == NULL || cursor == NULL)
	{
		ai_character_release_definition(merged);
		return NULL;
	}
	char *end = cursor + merged_metadata->string_bytes;
	for (int index = 0; index < count; ++index)
	{
		const ai_characteristic_t *source = ai_character_definition_slot(definition, index);
		if (source == NULL || source->type == AI_CHARACTER_VALUE_NONE)
		{
			source = ai_character_definition_slot(defaults, index);
		}
		if (source == NULL || source->type == AI_CHARACTER_VALUE_NONE)
		{
			continue;
		}
		if (!ai_character_copy_slot(merged, &merged->characteristics[index], source, &cursor, end))
		{
			ai_character_release_definition(merged);
			return NULL;
		}
	}

	return merged;
}

/*
=============
ai_character_interpolate_definitions

Builds a Q3-style interpolated characteristic definition.
=============
*/
static ai_character_definition_t *ai_character_interpolate_definitions(const ai_character_definition_t *first,
	const ai_character_definition_t *second,
	float skill)
{
	ai_character_definition_metadata_t *first_metadata =
		ai_character_definition_metadata(first);
	ai_character_definition_metadata_t *second_metadata =
		ai_character_definition_metadata(second);
	if (first_metadata == NULL ||
		second_metadata == NULL ||
		first_metadata->skill == second_metadata->skill)
	{
		return NULL;
	}

	int count = ai_character_public_count(first);
	int second_public_count = ai_character_public_count(second);
	if (second_public_count > count)
	{
		count = second_public_count;
	}

	size_t string_bytes = 0;
	for (int index = 0; index < count; ++index)
	{
		const ai_characteristic_t *first_slot = ai_character_definition_slot(first, index);
		if (first_slot != NULL && first_slot->type == AI_CHARACTER_VALUE_STRING)
		{
			string_bytes += ai_character_slot_string_bytes(first_slot);
		}
	}

	ai_character_definition_t *interpolated =
		ai_character_alloc_derived_definition(count,
			count,
			string_bytes,
			skill,
			first_metadata->identifier);
	if (interpolated == NULL)
	{
		return NULL;
	}

	ai_character_definition_metadata_t *interpolated_metadata =
		ai_character_definition_metadata(interpolated);
	char *cursor = ai_character_definition_string_storage(interpolated);
	if (interpolated_metadata == NULL || cursor == NULL)
	{
		ai_character_release_definition(interpolated);
		return NULL;
	}
	float scale = (skill - first_metadata->skill) /
		(second_metadata->skill - first_metadata->skill);
	char *end = cursor + interpolated_metadata->string_bytes;
	for (int index = 0; index < count; ++index)
	{
		const ai_characteristic_t *first_slot = ai_character_definition_slot(first, index);
		const ai_characteristic_t *second_slot = ai_character_definition_slot(second, index);
		if (first_slot == NULL || first_slot->type == AI_CHARACTER_VALUE_NONE)
		{
			continue;
		}

		ai_characteristic_t *out = &interpolated->characteristics[index];
		if (first_slot->type == AI_CHARACTER_VALUE_FLOAT &&
			second_slot != NULL &&
			second_slot->type == AI_CHARACTER_VALUE_FLOAT)
		{
			out->type = AI_CHARACTER_VALUE_FLOAT;
			out->value.float_value = first_slot->value.float_value +
				(second_slot->value.float_value - first_slot->value.float_value) * scale;
		}
		else if (first_slot->type == AI_CHARACTER_VALUE_INTEGER)
		{
			out->type = AI_CHARACTER_VALUE_INTEGER;
			out->value.integer_value = first_slot->value.integer_value;
		}
		else if (first_slot->type == AI_CHARACTER_VALUE_STRING)
		{
			if (!ai_character_copy_slot(interpolated, out, first_slot, &cursor, end))
			{
				ai_character_release_definition(interpolated);
				return NULL;
			}
		}
	}

	return interpolated;
}

/*
=============
ai_character_scan_source

Finds the requested character block and gathers allocation sizes.
=============
*/
static bool ai_character_scan_source(pc_source_t *source,
	const char *character_name,
	ai_character_scan_t *scan)
{
	pc_token_t token;

	while (PC_ReadToken(source, &token))
	{
		if (strcmp(token.string, "character") != 0)
		{
			SourceError(source, "unknown definition %s\n", token.string);
			return false;
		}

		char identifier[AI_CHARACTER_NAME_MAX];
		if (!ai_character_expect_identifier(source, identifier, sizeof(identifier)))
		{
			return false;
		}

		bool matches = ai_character_name_matches(identifier, character_name);
		if (character_name == NULL && scan->found)
		{
			matches = strcmp(identifier, scan->identifier) == 0;
		}

		if (!matches)
		{
			if (!ai_character_skip_block(source))
			{
				return false;
			}
			continue;
		}

		ai_character_copy_string(scan->identifier, sizeof(scan->identifier), identifier);
		if (!ai_character_scan_block(source, scan, -1))
		{
			return false;
		}
		scan->found = true;
	}

	return true;
}

/*
=============
ai_character_fill_source

Replays the source and fills the packed definition for the matching block.
=============
*/
static bool ai_character_fill_source(pc_source_t *source,
	const char *character_name,
	ai_character_definition_t *definition,
	bool *found_out)
{
	ai_character_definition_metadata_t *metadata =
		ai_character_definition_metadata(definition);
	if (metadata == NULL)
	{
		return false;
	}

	bool found = false;
	char *string_cursor = ai_character_definition_string_storage(definition);
	if (string_cursor == NULL)
	{
		return false;
	}
	char *string_end = string_cursor + metadata->string_bytes;
	pc_token_t token;

	while (PC_ReadToken(source, &token))
	{
		if (strcmp(token.string, "character") != 0)
		{
			SourceError(source, "unknown definition %s\n", token.string);
			return false;
		}

		char identifier[AI_CHARACTER_NAME_MAX];
		if (!ai_character_expect_identifier(source, identifier, sizeof(identifier)))
		{
			return false;
		}

		const char *selected_name = character_name;
		if (selected_name == NULL)
		{
			selected_name = metadata->identifier;
		}

		if (!ai_character_name_matches(identifier, selected_name))
		{
			if (!ai_character_skip_block(source))
			{
				return false;
			}
			continue;
		}

		if (!ai_character_fill_block(source, definition, &string_cursor, string_end))
		{
			return false;
		}
		found = true;
	}

	if (found_out != NULL)
	{
		*found_out = found;
	}
	return true;
}

/*
=============
ai_character_skill_matches

Checks a Q3 skill block against the requested rounded skill.
=============
*/
static bool ai_character_skill_matches(int parsed_skill, int requested_skill)
{
	return requested_skill < 0 || parsed_skill == requested_skill;
}

/*
=============
ai_character_expect_skill

Reads the skill number and opening brace for a Q3 skill definition.
=============
*/
static bool ai_character_expect_skill(pc_source_t *source, int *skill)
{
	pc_token_t token;
	if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &token))
	{
		return false;
	}

	int parsed_skill = 0;
	if (!ai_character_parse_integer_token(&token, &parsed_skill))
	{
		return false;
	}

	if (skill != NULL)
	{
		*skill = parsed_skill;
	}
	return PC_ExpectTokenString(source, "{") != 0;
}

/*
=============
ai_character_scan_skill_source

Finds a Q3 skill block and gathers allocation sizes.
=============
*/
static bool ai_character_scan_skill_source(pc_source_t *source,
	int requested_skill,
	ai_character_scan_t *scan)
{
	pc_token_t token;

	while (PC_ReadToken(source, &token))
	{
		if (token.type != TT_NAME || strcmp(token.string, "skill") != 0)
		{
			SourceError(source, "unknown definition %s\n", token.string);
			return false;
		}

		int parsed_skill = 0;
		if (!ai_character_expect_skill(source, &parsed_skill))
		{
			return false;
		}

		if (!ai_character_skill_matches(parsed_skill, requested_skill))
		{
			if (!ai_character_skip_block(source))
			{
				return false;
			}
			continue;
		}

		scan->skill = (float)parsed_skill;
		snprintf(scan->identifier, sizeof(scan->identifier), "skill %d", parsed_skill);
		if (!ai_character_scan_block(source, scan, AI_CHARACTER_Q3_MAX_CHARACTERISTICS))
		{
			return false;
		}
		if (scan->count < AI_CHARACTER_Q3_MAX_CHARACTERISTICS)
		{
			scan->count = AI_CHARACTER_Q3_MAX_CHARACTERISTICS;
		}
		scan->found = true;
		return true;
	}

	return true;
}

/*
=============
ai_character_fill_skill_source

Replays the source and fills the requested Q3 skill block.
=============
*/
static bool ai_character_fill_skill_source(pc_source_t *source,
	int requested_skill,
	ai_character_definition_t *definition)
{
	ai_character_definition_metadata_t *metadata =
		ai_character_definition_metadata(definition);
	if (metadata == NULL)
	{
		return false;
	}

	char *string_cursor = ai_character_definition_string_storage(definition);
	if (string_cursor == NULL)
	{
		return false;
	}
	char *string_end = string_cursor + metadata->string_bytes;
	pc_token_t token;

	while (PC_ReadToken(source, &token))
	{
		if (token.type != TT_NAME || strcmp(token.string, "skill") != 0)
		{
			SourceError(source, "unknown definition %s\n", token.string);
			return false;
		}

		int parsed_skill = 0;
		if (!ai_character_expect_skill(source, &parsed_skill))
		{
			return false;
		}

		if (!ai_character_skill_matches(parsed_skill, requested_skill))
		{
			if (!ai_character_skip_block(source))
			{
				return false;
			}
			continue;
		}

		return ai_character_fill_block(source, definition, &string_cursor, string_end);
	}

	return false;
}

/*
=============
ai_character_open_source

Loads a previously resolved character file through the reconstructed
precompiler while preserving its logical name for include resolution.
=============
*/
static pc_source_t *ai_character_open_source(const char *filename,
	const botlib_asset_resolution_t *resolution)
{
	if (resolution == NULL || resolution->resolved_path[0] == '\0')
	{
		BotLib_Print(PRT_ERROR, "couldn't find %s\n", filename);
		return NULL;
	}

	pc_source_t *source = PC_LoadSourceFile(filename);
	if (source == NULL)
	{
		BotLib_Print(PRT_ERROR,
			"counldn't load %s\n",
			resolution->source_path[0] != '\0' ?
				resolution->source_path : filename);
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
	if (filename == NULL)
	{
		return NULL;
	}
	char retail_filename[AI_CHARACTER_RETAIL_FILENAME_SIZE];
	ai_character_copy_string(retail_filename,
		sizeof(retail_filename),
		filename);

	ai_character_scan_t scan;
	memset(&scan, 0, sizeof(scan));

	botlib_asset_resolution_t resolution;
	if (!BotLib_ResolveAssetPathDetailed(retail_filename, NULL, &resolution))
	{
		BotLib_Print(PRT_ERROR, "couldn't find %s\n", retail_filename);
		return NULL;
	}

	pc_source_t *source = ai_character_open_source(retail_filename, &resolution);
	if (source == NULL)
	{
		return NULL;
	}

	const char *diagnostic_source = resolution.source_path[0] != '\0' ?
		resolution.source_path : retail_filename;
	bool scanned = ai_character_scan_source(source,
		character_name,
		&scan);
	PC_FreeSource(source);
	if (!scanned)
	{
		return NULL;
	}
	if (!scan.found)
	{
		BotLib_Print(PRT_ERROR,
			"couldn't find character %s in %s\n",
			ai_character_requested_name(character_name),
			diagnostic_source);
		return NULL;
	}

	ai_character_definition_t *definition = ai_character_alloc_definition(&scan);
	if (definition == NULL)
	{
		return NULL;
	}
	definition->highest_characteristic = scan.count > 0 ? scan.count - 1 : 0;

	source = ai_character_open_source(retail_filename, &resolution);
	if (source == NULL)
	{
		/* Retail abandons the pass-one allocation on a pass-two open failure. */
		return NULL;
	}

	bool fill_found = false;
	bool filled = ai_character_fill_source(source,
		character_name,
		definition,
		&fill_found);
	PC_FreeSource(source);
	if (!filled)
	{
		/* Retail likewise retains the allocation after any pass-two parse error. */
		return NULL;
	}
	if (!fill_found)
	{
		BotLib_Print(PRT_ERROR,
			"couldn't find character %s in %s\n",
			ai_character_requested_name(character_name),
			diagnostic_source);
		return NULL;
	}

	if (resolution.pak_entry_length != 0)
	{
		BotLib_Print(PRT_MESSAGE,
			"loaded %s from %s\\%s\n",
			ai_character_requested_name(character_name),
			resolution.source_path,
			retail_filename);
	}
	else
	{
		BotLib_Print(PRT_MESSAGE,
			"loaded %s from %s\n",
			ai_character_requested_name(character_name),
			retail_filename);
	}
	return definition;
}

/*
=============
AI_LoadCharacterDefinition

Loads the single packed definition allocation used by the retail pointer ABI.
=============
*/
bot_character_t *AI_LoadCharacterDefinition(const char *filename,
	const char *character_name)
{
	return ai_parse_definition(filename, character_name);
}

/*
=============
AI_FreeCharacterDefinition

Frees a packed retail character definition.
=============
*/
void AI_FreeCharacterDefinition(bot_character_t *character)
{
	ai_character_release_definition(character);
}

/*
=============
AI_ShutdownCharacterDefinitions

Reclaims definition/sidecar pairs abandoned by retail-compatible partial
setup retries before the bot memory arena is destroyed.
=============
*/
void AI_ShutdownCharacterDefinitions(void)
{
	while (g_ai_character_metadata != NULL)
	{
		ai_character_definition_metadata_t *metadata =
			g_ai_character_metadata;
		g_ai_character_metadata = metadata->next;
		FreeMemory(metadata->definition);
		free(metadata);
	}
}

/*
=============
ai_parse_skill_definition

Builds a packed Q3 skill definition from a `skill N` block.
=============
*/
static ai_character_definition_t *ai_parse_skill_definition(const char *filename,
	int skill)
{
	if (filename == NULL || filename[0] == '\0')
	{
		return NULL;
	}

	ai_character_scan_t scan;
	memset(&scan, 0, sizeof(scan));

	botlib_asset_resolution_t resolution;
	if (!BotLib_ResolveAssetPathDetailed(filename, NULL, &resolution))
	{
		BotLib_Print(PRT_ERROR, "couldn't find %s\n", filename);
		return NULL;
	}

	pc_source_t *source = ai_character_open_source(filename, &resolution);
	if (source == NULL)
	{
		return NULL;
	}

	bool scanned = ai_character_scan_skill_source(source, skill, &scan);
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
	definition->highest_characteristic = AI_CHARACTER_Q3_MAX_CHARACTERISTICS;

	source = ai_character_open_source(filename, &resolution);
	if (source == NULL)
	{
		ai_character_release_definition(definition);
		return NULL;
	}

	bool filled = ai_character_fill_skill_source(source, skill, definition);
	PC_FreeSource(source);
	if (!filled)
	{
		ai_character_release_definition(definition);
		return NULL;
	}

	return definition;
}

/*
=============
ai_profile_from_definition

Wraps a packed characteristic definition in a profile without setup resources.
=============
*/
static ai_character_profile_t *ai_profile_from_definition(const char *filename,
	const char *character_name,
	float skill,
	ai_character_definition_t *definition)
{
	if (definition == NULL)
	{
		return NULL;
	}

	ai_character_definition_metadata_t *metadata =
		ai_character_definition_metadata(definition);
	if (metadata == NULL)
	{
		ai_character_release_definition(definition);
		return NULL;
	}

	ai_character_profile_t *profile = (ai_character_profile_t *)GetClearedMemory(sizeof(*profile));
	if (profile == NULL)
	{
		ai_character_release_definition(definition);
		return NULL;
	}

	profile->requested_skill = skill;
	profile->definition_blob = definition;
	ai_character_copy_string(profile->character_filename,
		sizeof(profile->character_filename),
		filename);
	ai_character_copy_string(profile->character_name,
		sizeof(profile->character_name),
		character_name != NULL ? character_name : metadata->identifier);
	return profile;
}

/*
=============
ai_character_file_first_keyword

Reads the first preprocessed name token from a character source.
=============
*/
static bool ai_character_file_first_keyword(const char *filename,
	char *keyword,
	size_t keyword_size)
{
	if (keyword != NULL && keyword_size > 0)
	{
		keyword[0] = '\0';
	}
	if (filename == NULL || filename[0] == '\0' || keyword == NULL || keyword_size == 0)
	{
		return false;
	}

	pc_source_t *source = PC_LoadSourceFile(filename);
	if (source == NULL)
	{
		return false;
	}

	pc_token_t token;
	bool found = PC_ReadToken(source, &token) != 0 && token.type == TT_NAME;
	if (found)
	{
		ai_character_copy_string(keyword, keyword_size, token.string);
	}
	PC_FreeSource(source);
	return found;
}

/*
=============
AI_CharacterFileUsesSkillBlocks

Detects whether a file uses Q3 `skill N` character blocks.
=============
*/
bool AI_CharacterFileUsesSkillBlocks(const char *filename)
{
	char keyword[AI_CHARACTER_NAME_MAX];
	if (!ai_character_file_first_keyword(filename, keyword, sizeof(keyword)))
	{
		return false;
	}

	return strcmp(keyword, "skill") == 0;
}

/*
=============
AI_CharacterFileUsesNamedBlocks

Detects whether a file uses Gladiator `character "name"` blocks.
=============
*/
bool AI_CharacterFileUsesNamedBlocks(const char *filename)
{
	char keyword[AI_CHARACTER_NAME_MAX];
	if (!ai_character_file_first_keyword(filename, keyword, sizeof(keyword)))
	{
		return false;
	}

	return strcmp(keyword, "character") == 0;
}

/*
=============
AI_CharacterDefaultFileUsesSkillBlocks

Returns whether the Q3 default character file is available.
=============
*/
bool AI_CharacterDefaultFileUsesSkillBlocks(void)
{
	return AI_CharacterFileUsesSkillBlocks(AI_CHARACTER_Q3_DEFAULT_FILE);
}

/*
=============
AI_LoadCharacterSkillProfileBlock

Loads one Q3 skill block without applying the retail fallback ladder.
=============
*/
ai_character_profile_t *AI_LoadCharacterSkillProfileBlock(const char *filename,
	float requested_skill,
	int block_skill,
	bool merge_defaults)
{
	if (filename == NULL || filename[0] == '\0')
	{
		return NULL;
	}

	ai_character_definition_t *definition = ai_parse_skill_definition(filename, block_skill);
	if (definition == NULL)
	{
		return NULL;
	}

	if (merge_defaults &&
		strcmp(filename, AI_CHARACTER_Q3_DEFAULT_FILE) != 0 &&
		AI_CharacterFileUsesSkillBlocks(AI_CHARACTER_Q3_DEFAULT_FILE))
	{
		int default_skill = requested_skill < 0.0f ? -1 : (int)(requested_skill + 0.5f);
		ai_character_definition_t *defaults =
			ai_parse_skill_definition(AI_CHARACTER_Q3_DEFAULT_FILE, default_skill);
		if (defaults == NULL)
		{
			defaults = ai_parse_skill_definition(AI_CHARACTER_Q3_DEFAULT_FILE, -1);
		}
		if (defaults != NULL)
		{
			ai_character_definition_t *merged =
				ai_character_merge_default_definition(definition, defaults);
			ai_character_release_definition(defaults);
			if (merged != NULL)
			{
				ai_character_release_definition(definition);
				definition = merged;
			}
		}
	}

	return ai_profile_from_definition(filename, NULL, requested_skill, definition);
}

/*
=============
AI_LoadCharacterSkillProfile

Loads a Q3 skill-block character profile without Gladiator setup resources.
=============
*/
ai_character_profile_t *AI_LoadCharacterSkillProfile(const char *filename, float skill)
{
	if (filename == NULL || filename[0] == '\0')
	{
		return NULL;
	}

	int rounded_skill = skill < 0.0f ? -1 : (int)(skill + 0.5f);
	ai_character_definition_t *definition = ai_parse_skill_definition(filename, rounded_skill);
	const char *loaded_file = filename;
	if (definition != NULL && rounded_skill >= 0)
	{
		BotLib_Print(PRT_MESSAGE,
			"loaded skill %d from %s\n",
			rounded_skill,
			filename);
	}
	else if (definition != NULL)
	{
		ai_character_definition_metadata_t *metadata =
			ai_character_definition_metadata(definition);
		BotLib_Print(PRT_MESSAGE,
			"loaded skill %f from %s\n",
			metadata != NULL ? metadata->skill : skill,
			filename);
	}

	bool default_available = AI_CharacterFileUsesSkillBlocks(AI_CHARACTER_Q3_DEFAULT_FILE);
	if (definition == NULL)
	{
		BotLib_Print(PRT_WARNING,
			"couldn't find skill %d in %s\n",
			rounded_skill,
			filename);
		if (default_available)
		{
			definition = ai_parse_skill_definition(AI_CHARACTER_Q3_DEFAULT_FILE, rounded_skill);
			if (definition != NULL)
			{
				loaded_file = AI_CHARACTER_Q3_DEFAULT_FILE;
				BotLib_Print(PRT_MESSAGE,
					"loaded default skill %d from %s\n",
					rounded_skill,
					filename);
			}
		}
	}

	if (definition == NULL)
	{
		definition = ai_parse_skill_definition(filename, -1);
		if (definition != NULL)
		{
			ai_character_definition_metadata_t *metadata =
				ai_character_definition_metadata(definition);
			BotLib_Print(PRT_MESSAGE,
				"loaded skill %f from %s\n",
				metadata != NULL ? metadata->skill : skill,
				filename);
		}
	}

	if (definition == NULL && default_available)
	{
		definition = ai_parse_skill_definition(AI_CHARACTER_Q3_DEFAULT_FILE, -1);
		if (definition != NULL)
		{
			ai_character_definition_metadata_t *metadata =
				ai_character_definition_metadata(definition);
			loaded_file = AI_CHARACTER_Q3_DEFAULT_FILE;
			BotLib_Print(PRT_MESSAGE,
				"loaded default skill %f from %s\n",
				metadata != NULL ? metadata->skill : skill,
				filename);
		}
	}

	if (definition == NULL)
	{
		BotLib_Print(PRT_WARNING,
			"couldn't load any skill from %s\n",
			filename);
		return NULL;
	}

	if (loaded_file == filename && default_available)
	{
		ai_character_definition_t *defaults =
			ai_parse_skill_definition(AI_CHARACTER_Q3_DEFAULT_FILE, rounded_skill);
		if (defaults == NULL)
		{
			defaults = ai_parse_skill_definition(AI_CHARACTER_Q3_DEFAULT_FILE, -1);
		}
		if (defaults != NULL)
		{
			ai_character_definition_t *merged =
				ai_character_merge_default_definition(definition, defaults);
			ai_character_release_definition(defaults);
			if (merged != NULL)
			{
				ai_character_release_definition(definition);
				definition = merged;
			}
		}
	}

	return ai_profile_from_definition(loaded_file, NULL, skill, definition);
}

/*
=============
AI_ApplyCharacterDefaults

Fills a loaded Q3 profile from an already selected default profile.
=============
*/
bool AI_ApplyCharacterDefaults(ai_character_profile_t *profile,
	const ai_character_profile_t *defaults)
{
	if (profile == NULL || profile->definition_blob == NULL)
	{
		return false;
	}
	if (defaults == NULL || defaults->definition_blob == NULL)
	{
		return true;
	}

	ai_character_definition_t *merged =
		ai_character_merge_default_definition(profile->definition_blob,
			defaults->definition_blob);
	if (merged == NULL)
	{
		return false;
	}

	ai_character_release_definition(profile->definition_blob);
	profile->definition_blob = merged;
	return true;
}

/*
=============
AI_InterpolateCharacterProfiles

Interpolates two Q3 skill-block profiles into a transient profile.
=============
*/
ai_character_profile_t *AI_InterpolateCharacterProfiles(const ai_character_profile_t *first,
	const ai_character_profile_t *second,
	float skill)
{
	if (first == NULL || second == NULL)
	{
		return NULL;
	}

	ai_character_definition_t *definition =
		ai_character_interpolate_definitions(first->definition_blob,
			second->definition_blob,
			skill);
	if (definition == NULL)
	{
		return NULL;
	}

	return ai_profile_from_definition(first->character_filename, NULL, skill, definition);
}

/*
=============
AI_LoadCharacterNamed

Loads a named Gladiator character definition.
=============
*/
ai_character_profile_t *AI_LoadCharacterNamed(const char *filename,
	const char *character_name,
	float skill)
{
	ai_character_definition_t *definition =
		AI_LoadCharacterDefinition(filename, character_name);
	if (definition == NULL)
	{
		return NULL;
	}

	return ai_profile_from_definition(filename, character_name, skill, definition);
}

/*
=============
AI_LoadCharacter

Loads the first character block in a file for direct botlib callers.
=============
*/
ai_character_profile_t *AI_LoadCharacter(const char *filename, float skill)
{
	if (AI_CharacterFileUsesSkillBlocks(filename))
	{
		return AI_LoadCharacterSkillProfile(filename, skill);
	}

	return AI_LoadCharacterNamed(filename, NULL, skill);
}

/*
=============
AI_FreeCharacter

Frees a character profile and its packed definition.
=============
*/
void AI_FreeCharacter(ai_character_profile_t *profile)
{
	if (profile == NULL)
	{
		return;
	}

	if (profile->definition_blob != NULL)
	{
		AI_FreeCharacterDefinition(profile->definition_blob);
		profile->definition_blob = NULL;
	}

	FreeMemory(profile);
}

/*
=============
AI_FreeCharacterStrings

Invalidates public string slots while retaining the packed definition storage.
=============
*/
void AI_FreeCharacterStrings(ai_character_profile_t *profile)
{
	ai_character_definition_t *definition =
		profile != NULL ? profile->definition_blob : NULL;
	if (definition == NULL)
	{
		return;
	}

	ai_character_definition_metadata_t *metadata =
		ai_character_definition_metadata(definition);
	if (metadata == NULL)
	{
		return;
	}

	int count = ai_character_public_count(definition);
	if (count > metadata->num_characteristics)
	{
		count = metadata->num_characteristics;
	}

	for (int index = 0; index < count; ++index)
	{
		ai_characteristic_t *slot = &definition->characteristics[index];
		if (slot->type == AI_CHARACTER_VALUE_STRING)
		{
			slot->type = AI_CHARACTER_VALUE_NONE;
			slot->value.string_value = NULL;
		}
	}
}

/*
=============
AI_ItemWeightsForCharacter

Returns the item weight configuration attached to a character profile.
=============
*/
bot_weight_config_t *AI_ItemWeightsForCharacter(const ai_character_profile_t *profile)
{
	(void)profile;
	return NULL;
}

/*
=============
AI_WeaponWeightsForCharacter

Returns the weapon weight table attached to a character profile.
=============
*/
ai_weapon_weights_t *AI_WeaponWeightsForCharacter(const ai_character_profile_t *profile)
{
	(void)profile;
	return NULL;
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
AI_CharacterProfileSkill

Returns the skill value stored in a loaded character definition.
=============
*/
float AI_CharacterProfileSkill(const ai_character_profile_t *profile)
{
	const ai_character_definition_t *definition = ai_definition(profile);
	ai_character_definition_metadata_t *metadata =
		ai_character_definition_metadata(definition);
	if (metadata != NULL)
	{
		return metadata->skill;
	}

	return profile != NULL ? profile->requested_skill : 0.0f;
}

/*
=============
AI_CharacterProfileFilename

Returns the source filename recorded on a loaded character profile.
=============
*/
const char *AI_CharacterProfileFilename(const ai_character_profile_t *profile)
{
	return profile != NULL ? profile->character_filename : "";
}

/*
=============
ai_character_definition_checked_slot

Validates a packed-definition lookup and emits retail diagnostics.
=============
*/
static const ai_characteristic_t *ai_character_definition_checked_slot(
	const bot_character_t *character,
	int index)
{
	const ai_character_definition_t *definition = character;
	ai_character_definition_metadata_t *metadata =
		ai_character_definition_metadata(definition);
	if (metadata == NULL ||
		index < 0 ||
		index >= ai_character_public_count(definition) ||
		index >= metadata->num_characteristics)
	{
		BotLib_Print(PRT_ERROR,
			"characteristic %d does not exist\n",
			index);
		return NULL;
	}

	const ai_characteristic_t *slot = &definition->characteristics[index];
	if (slot->type == AI_CHARACTER_VALUE_NONE)
	{
		BotLib_Print(PRT_ERROR,
			"characteristic %d is not initialized\n",
			index);
		return NULL;
	}

	return slot;
}

/*
=============
AI_CharacteristicDefinitionCount

Returns the retail-visible count stored in a packed definition.
=============
*/
int AI_CharacteristicDefinitionCount(const bot_character_t *character)
{
	return ai_character_public_count(character);
}

/*
=============
AI_CharacteristicDefinitionType

Returns a packed definition's characteristic type without diagnostics.
=============
*/
ai_character_value_type_t AI_CharacteristicDefinitionType(const bot_character_t *character,
	int index)
{
	const ai_character_definition_t *definition = character;
	ai_character_definition_metadata_t *metadata =
		ai_character_definition_metadata(definition);
	if (metadata == NULL ||
		index < 0 ||
		index >= ai_character_public_count(definition) ||
		index >= metadata->num_characteristics)
	{
		return AI_CHARACTER_VALUE_NONE;
	}

	return definition->characteristics[index].type;
}

/*
=============
AI_CharacteristicDefinitionAsFloat

Returns a packed float characteristic, converting integers like retail.
=============
*/
float AI_CharacteristicDefinitionAsFloat(const bot_character_t *character, int index)
{
	const ai_characteristic_t *slot =
		ai_character_definition_checked_slot(character, index);
	if (slot == NULL)
	{
		return 0.0f;
	}

	if (slot->type == AI_CHARACTER_VALUE_FLOAT)
	{
		return slot->value.float_value;
	}
	if (slot->type == AI_CHARACTER_VALUE_INTEGER)
	{
		return (float)slot->value.integer_value;
	}

	BotLib_Print(PRT_ERROR,
		"characteristic %d is not a float\n",
		index);
	return 0.0f;
}

/*
=============
AI_CharacteristicDefinitionAsInteger

Returns a packed integer characteristic, truncating floats like retail.
=============
*/
int AI_CharacteristicDefinitionAsInteger(const bot_character_t *character, int index)
{
	const ai_characteristic_t *slot =
		ai_character_definition_checked_slot(character, index);
	if (slot == NULL)
	{
		return 0;
	}

	if (slot->type == AI_CHARACTER_VALUE_INTEGER)
	{
		return slot->value.integer_value;
	}
	if (slot->type == AI_CHARACTER_VALUE_FLOAT)
	{
		return ai_character_retail_ftol(slot->value.float_value);
	}

	BotLib_Print(PRT_ERROR,
		"characteristic %d is not a integer\n",
		index);
	return 0;
}

/*
=============
AI_CharacteristicDefinitionAsString

Returns a packed string, the retail writable empty sentinel, or a type-error NULL.
=============
*/
const char *AI_CharacteristicDefinitionAsString(const bot_character_t *character, int index)
{
	const ai_characteristic_t *slot =
		ai_character_definition_checked_slot(character, index);
	if (slot == NULL)
	{
		return &g_ai_character_empty_string;
	}

	if (slot->type == AI_CHARACTER_VALUE_STRING)
	{
		return slot->value.string_value;
	}

	BotLib_Print(PRT_ERROR,
		"characteristic %d is not a string\n",
		index);
	return NULL;
}

/*
=============
AI_CharacteristicCount

Returns the number of addressable characteristic slots in a profile.
=============
*/
int AI_CharacteristicCount(const ai_character_profile_t *profile)
{
	return AI_CharacteristicDefinitionCount(ai_definition(profile));
}

/*
=============
AI_CharacteristicType

Returns the stored type for an initialized characteristic slot.
=============
*/
ai_character_value_type_t AI_CharacteristicType(const ai_character_profile_t *profile, int index)
{
	return AI_CharacteristicDefinitionType(ai_definition(profile), index);
}

/*
=============
AI_CharacteristicAsFloat

Returns a float characteristic, converting integers like retail botlib.
=============
*/
float AI_CharacteristicAsFloat(const ai_character_profile_t *profile, int index)
{
	return AI_CharacteristicDefinitionAsFloat(ai_definition(profile), index);
}

/*
=============
AI_CharacteristicAsInteger

Returns an integer characteristic, truncating floats like the original code.
=============
*/
int AI_CharacteristicAsInteger(const ai_character_profile_t *profile, int index)
{
	return AI_CharacteristicDefinitionAsInteger(ai_definition(profile), index);
}

/*
=============
AI_CharacteristicAsString

Returns a string characteristic pointer or NULL for non-string slots.
=============
*/
const char *AI_CharacteristicAsString(const ai_character_profile_t *profile, int index)
{
	return AI_CharacteristicDefinitionAsString(ai_definition(profile), index);
}
