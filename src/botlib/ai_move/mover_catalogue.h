#pragma once

#include <stdbool.h>

#include "botlib/interface/bot_interface_assets.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bot_mover_kind_e
{
    BOT_MOVER_KIND_UNKNOWN = 0,
    BOT_MOVER_KIND_FUNC_PLAT,
    BOT_MOVER_KIND_FUNC_BOB,
    BOT_MOVER_KIND_FUNC_DOOR,
    BOT_MOVER_KIND_FUNC_DOOR_ROTATING,
    BOT_MOVER_KIND_FUNC_DOOR_SECRET,
} bot_mover_kind_t;

typedef struct bot_mover_catalogue_entry_s
{
    int modelnum;
    int modelindex;
    float lip;
    float height;
    float speed;
    int spawnflags;
    int doortype;
    bot_mover_kind_t kind;
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

