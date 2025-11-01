#pragma once

#include <stdbool.h>

#include "../interface/bot_interface_assets.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bot_mover_catalogue_entry_s
{
    int modelnum;
    int modelindex;
    float lip;
    float height;
    float speed;
    int spawnflags;
    int doortype;
    bool ready;
} bot_mover_catalogue_entry_t;

void BotMove_MoverCatalogueInit(void);
void BotMove_MoverCatalogueReset(void);
bool BotMove_MoverCatalogueInsert(const bot_mover_catalogue_entry_t *entry);
bool BotMove_MoverCatalogueFinalize(const botinterface_asset_list_t *models);
const bot_mover_catalogue_entry_t *BotMove_MoverCatalogueFindByModel(int modelnum);
bool BotMove_MoverCatalogueIsModelMover(int modelnum);

#ifdef __cplusplus
} /* extern "C" */
#endif

