#ifndef GLADIATOR_BOTLIB_AAS_AAS_MAP_H
#define GLADIATOR_BOTLIB_AAS_AAS_MAP_H

#include "../../../dev_tools/game_source/botlib.h"

#ifdef __cplusplus
extern "C" {
#endif

int AAS_LoadMap(const char *mapname,
                int modelindexes, char *modelindex[],
                int soundindexes, char *soundindex[],
                int imageindexes, char *imageindex[]);

void AAS_Shutdown(void);

int AAS_UpdateEntity(int ent, bot_updateentity_t *state);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GLADIATOR_BOTLIB_AAS_AAS_MAP_H */
