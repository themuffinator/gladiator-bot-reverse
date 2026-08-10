#include "mover_catalogue.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "botlib/common/l_memory.h"

static bot_mover_catalogue_entry_t *s_entries = NULL;
static size_t s_entry_count = 0U;
static size_t s_entry_capacity = 0U;
static bool s_catalogue_initialised = false;

/*
=============
BotMove_MoverCatalogueEnsureInit
=============
*/
static void BotMove_MoverCatalogueEnsureInit(void)
{
	if (!s_catalogue_initialised)
	{
		BotMove_MoverCatalogueInit();
	}
}

/*
=============
BotMove_MoverCatalogueFindMutable
=============
*/
static bot_mover_catalogue_entry_t *BotMove_MoverCatalogueFindMutable(int modelnum)
{
	if (s_entries == NULL)
	{
		return NULL;
	}

	for (size_t i = 0U; i < s_entry_count; ++i)
	{
		if (s_entries[i].modelnum == modelnum)
		{
			return &s_entries[i];
		}
	}

	return NULL;
}

/*
=============
BotMove_MoverCatalogueGrow
=============
*/
static bool BotMove_MoverCatalogueGrow(size_t required_capacity)
{
	if (required_capacity <= s_entry_capacity)
	{
		return true;
	}

	size_t new_capacity = (s_entry_capacity == 0U) ? 8U : s_entry_capacity;
	while (new_capacity < required_capacity)
	{
		new_capacity *= 2U;
	}

	bot_mover_catalogue_entry_t *new_entries =
		(bot_mover_catalogue_entry_t *)GetMemory(new_capacity * sizeof(*new_entries));
	if (new_entries == NULL)
	{
		return false;
	}

	if (s_entries != NULL && s_entry_count > 0U)
	{
		memcpy(new_entries, s_entries, s_entry_count * sizeof(*new_entries));
		FreeMemory(s_entries);
	}

	s_entries = new_entries;
	s_entry_capacity = new_capacity;
	return true;
}

/*
=============
BotMove_MoverCatalogueInit
=============
*/
void BotMove_MoverCatalogueInit(void)
{
	if (s_catalogue_initialised)
	{
		return;
	}

	s_entries = NULL;
	s_entry_count = 0U;
	s_entry_capacity = 0U;
	s_catalogue_initialised = true;
}

/*
=============
BotMove_MoverCatalogueReset
=============
*/
void BotMove_MoverCatalogueReset(void)
{
	BotMove_MoverCatalogueEnsureInit();

	if (s_entries != NULL)
	{
		FreeMemory(s_entries);
		s_entries = NULL;
	}

	s_entry_count = 0U;
	s_entry_capacity = 0U;
}

/*
=============
BotMove_MoverCatalogueInsert
=============
*/
bool BotMove_MoverCatalogueInsert(const bot_mover_catalogue_entry_t *entry)
{
	BotMove_MoverCatalogueEnsureInit();

	if (entry == NULL)
	{
		return false;
	}

	bot_mover_catalogue_entry_t *existing =
		BotMove_MoverCatalogueFindMutable(entry->modelnum);
	if (existing != NULL)
	{
		*existing = *entry;
		existing->modelindex = -1;
		existing->ready = true;
		return true;
	}

	if (!BotMove_MoverCatalogueGrow(s_entry_count + 1U))
	{
		return false;
	}

	s_entries[s_entry_count] = *entry;
	s_entries[s_entry_count].modelindex = -1;
	s_entries[s_entry_count].ready = true;
	++s_entry_count;
	return true;
}

/*
=============
BotMove_MoverCatalogueFinalize
=============
*/
bool BotMove_MoverCatalogueFinalize(char *const *models, size_t model_count)
{
	BotMove_MoverCatalogueEnsureInit();

	if (s_entry_count == 0U)
	{
		return true;
	}

	if (models == NULL)
	{
		for (size_t i = 0U; i < s_entry_count; ++i)
		{
			s_entries[i].modelindex = -1;
			s_entries[i].ready = false;
		}
		return false;
	}

	bool success = true;

	for (size_t entry_index = 0U; entry_index < s_entry_count; ++entry_index)
	{
		bot_mover_catalogue_entry_t *entry = &s_entries[entry_index];
		entry->modelindex = -1;
		entry->ready = false;

		char target[16] = {0};
		int written = snprintf(target, sizeof(target), "*%d", entry->modelnum);
		if (written < 0 || (size_t)written >= sizeof(target))
		{
			success = false;
			continue;
		}

		if (model_count == 0U)
		{
			success = false;
			continue;
		}

		for (size_t model_index = 0U; model_index < model_count; ++model_index)
		{
			const char *model_name = models[model_index];
			if (model_name == NULL)
			{
				continue;
			}

			if (strcmp(model_name, target) == 0)
			{
				entry->modelindex = (int)model_index;
				entry->ready = true;
				break;
			}
		}

		if (!entry->ready)
		{
			success = false;
		}
	}

	return success;
}

/*
=============
BotMove_MoverCatalogueFindByModel
=============
*/
const bot_mover_catalogue_entry_t *BotMove_MoverCatalogueFindByModel(int modelnum)
{
	BotMove_MoverCatalogueEnsureInit();
	const bot_mover_catalogue_entry_t *entry =
		BotMove_MoverCatalogueFindMutable(modelnum);
	if (entry == NULL || !entry->ready)
	{
		return NULL;
	}

	return entry;
}

/*
=============
BotMove_MoverCatalogueIsModelMover
=============
*/
bool BotMove_MoverCatalogueIsModelMover(int modelnum)
{
	return BotMove_MoverCatalogueFindByModel(modelnum) != NULL;
}

