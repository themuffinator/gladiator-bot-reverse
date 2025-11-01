#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bot_mover_catalogue_entry_s
{
    int modelnum;
    float lip;
    float height;
    float speed;
    int spawnflags;
    int doortype;
} bot_mover_catalogue_entry_t;

void BotMove_MoverCatalogueInit(void);
void BotMove_MoverCatalogueReset(void);
bool BotMove_MoverCatalogueInsert(const bot_mover_catalogue_entry_t *entry);
const bot_mover_catalogue_entry_t *BotMove_MoverCatalogueFindByModel(int modelnum);
bool BotMove_MoverCatalogueIsModelMover(int modelnum);

#ifdef __cplusplus
} /* extern "C" */
#endif

