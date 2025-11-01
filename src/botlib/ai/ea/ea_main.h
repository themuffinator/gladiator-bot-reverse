#ifndef GLADIATOR_BOTLIB_AI_EA_MAIN_H
#define GLADIATOR_BOTLIB_AI_EA_MAIN_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int EA_Init(int max_clients);
void EA_Shutdown(void);
bool EA_IsInitialised(void);

#ifdef __cplusplus
}
#endif

#endif /* GLADIATOR_BOTLIB_AI_EA_MAIN_H */
