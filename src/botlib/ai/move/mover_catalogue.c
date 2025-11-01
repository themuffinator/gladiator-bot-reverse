#include "mover_catalogue.h"

#include <stdlib.h>
#include <string.h>

typedef struct bot_move_mover_catalogue_s
{
    void *entries;
    size_t entry_count;
    size_t entry_capacity;
} bot_move_mover_catalogue_t;

static bot_move_mover_catalogue_t g_botMoveMoverCatalogue = {0};

static void BotMove_MoverCatalogueFreeEntries(bot_move_mover_catalogue_t *catalogue)
{
    if (catalogue == NULL)
    {
        return;
    }

    if (catalogue->entries != NULL)
    {
        free(catalogue->entries);
        catalogue->entries = NULL;
    }

    catalogue->entry_count = 0U;
    catalogue->entry_capacity = 0U;
}

void BotMove_MoverCatalogueReset(void)
{
    BotMove_MoverCatalogueFreeEntries(&g_botMoveMoverCatalogue);
    memset(&g_botMoveMoverCatalogue, 0, sizeof(g_botMoveMoverCatalogue));
}

