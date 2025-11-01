#include "mover_catalogue.h"

#include <stddef.h>
#include <string.h>

#include "../../common/l_memory.h"

static bot_mover_catalogue_entry_t *s_entries = NULL;
static size_t s_entry_count = 0U;
static size_t s_entry_capacity = 0U;
static bool s_catalogue_initialised = false;

static void BotMove_MoverCatalogueEnsureInit(void)
{
    if (!s_catalogue_initialised)
    {
        BotMove_MoverCatalogueInit();
    }
}

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
        return true;
    }

    if (!BotMove_MoverCatalogueGrow(s_entry_count + 1U))
    {
        return false;
    }

    s_entries[s_entry_count] = *entry;
    ++s_entry_count;
    return true;
}

const bot_mover_catalogue_entry_t *BotMove_MoverCatalogueFindByModel(int modelnum)
{
    BotMove_MoverCatalogueEnsureInit();
    return BotMove_MoverCatalogueFindMutable(modelnum);
}

bool BotMove_MoverCatalogueIsModelMover(int modelnum)
{
    return BotMove_MoverCatalogueFindByModel(modelnum) != NULL;
}

