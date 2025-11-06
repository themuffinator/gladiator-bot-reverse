#ifndef Q2BRIDGE_BRIDGE_CONFIG_H
#define Q2BRIDGE_BRIDGE_CONFIG_H

#include <stdbool.h>

#include "botlib/common/l_libvar.h"

#ifdef __cplusplus
extern "C" {
#endif

bool BridgeConfig_Init(void);
void BridgeConfig_Shutdown(void);

libvar_t *Bridge_MaxClients(void);
libvar_t *Bridge_MaxEntities(void);
libvar_t *Bridge_Gravity(void);
libvar_t *Bridge_MaxVelocity(void);
libvar_t *Bridge_AirAccelerate(void);
libvar_t *Bridge_MaxWalkVelocity(void);
libvar_t *Bridge_MaxCrouchVelocity(void);
libvar_t *Bridge_MaxSwimVelocity(void);
libvar_t *Bridge_JumpVelocity(void);
libvar_t *Bridge_MaxAcceleration(void);
libvar_t *Bridge_Friction(void);
libvar_t *Bridge_StopSpeed(void);
libvar_t *Bridge_MaxStep(void);
libvar_t *Bridge_MaxBarrier(void);
libvar_t *Bridge_MaxSteepness(void);
libvar_t *Bridge_MaxWaterJump(void);
libvar_t *Bridge_WaterGravity(void);
libvar_t *Bridge_WaterFriction(void);
libvar_t *Bridge_WeaponConfig(void);
libvar_t *Bridge_MaxWeaponInfo(void);
libvar_t *Bridge_MaxProjectileInfo(void);
libvar_t *Bridge_SoundConfig(void);
libvar_t *Bridge_MaxSoundInfo(void);
libvar_t *Bridge_MaxAASSounds(void);
libvar_t *Bridge_DMFlags(void);
libvar_t *Bridge_UseHook(void);
libvar_t *Bridge_LaserHook(void);
const char *Bridge_GrappleModelPath(void);
libvar_t *Bridge_RocketJump(void);
libvar_t *Bridge_ForceClustering(void);
libvar_t *Bridge_ForceReachability(void);
libvar_t *Bridge_ForceWrite(void);
libvar_t *Bridge_FrameReachability(void);

#ifdef __cplusplus
}
#endif

#endif /* Q2BRIDGE_BRIDGE_CONFIG_H */
