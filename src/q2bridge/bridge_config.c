#include "q2bridge/bridge_config.h"

#include <stddef.h>
#include <string.h>

#include "q2bridge/botlib.h"
#include "q2bridge/bridge.h"

typedef struct bridge_config_cache_s {
    libvar_t *maxclients;
    libvar_t *maxentities;
    libvar_t *sv_gravity;
    libvar_t *sv_maxvelocity;
    libvar_t *sv_airaccelerate;
    libvar_t *sv_maxwalkvelocity;
    libvar_t *sv_maxcrouchvelocity;
    libvar_t *sv_maxswimvelocity;
    libvar_t *sv_jumpvel;
    libvar_t *sv_maxacceleration;
    libvar_t *sv_friction;
    libvar_t *sv_stopspeed;
    libvar_t *sv_maxstep;
    libvar_t *sv_maxbarrier;
    libvar_t *sv_maxsteepness;
    libvar_t *sv_maxwaterjump;
    libvar_t *sv_watergravity;
    libvar_t *sv_waterfriction;
    libvar_t *weaponconfig;
    libvar_t *max_weaponinfo;
    libvar_t *max_projectileinfo;
    libvar_t *dmflags;
    libvar_t *usehook;
    libvar_t *rocketjump;
} bridge_config_cache_t;

static bridge_config_cache_t g_bridge_config_cache;
static bool g_bridge_config_initialised = false;

#define BRIDGE_DEFAULT_WEAPONCONFIG "weapons.c"

static libvar_t *BridgeConfig_CacheLibVar(libvar_t **slot, const char *name, const char *default_value)
{
    if (slot == NULL || name == NULL)
    {
        return NULL;
    }

    if (*slot != NULL)
    {
        return *slot;
    }

    libvar_t *var = LibVarGet(name);
    if (var == NULL)
    {
        if (default_value != NULL)
        {
            Q2_Print(PRT_WARNING,
                     "[bridge_config] failed to query libvar '%s'; defaulting to '%s'\n",
                     name,
                     default_value);
            var = LibVar(name, default_value);
        }
        else
        {
            Q2_Print(PRT_ERROR,
                     "[bridge_config] failed to query libvar '%s' and no default is available\n",
                     name);
            return NULL;
        }
    }

    *slot = var;
    return var;
}

bool BridgeConfig_Init(void)
{
    if (g_bridge_config_initialised)
    {
        return true;
    }

    memset(&g_bridge_config_cache, 0, sizeof(g_bridge_config_cache));

    BridgeConfig_CacheLibVar(&g_bridge_config_cache.maxclients, "maxclients", "4");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.maxentities, "maxentities", "1024");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_gravity, "sv_gravity", "800");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_maxvelocity, "sv_maxvelocity", "300");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_airaccelerate, "sv_airaccelerate", "0");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_maxwalkvelocity, "sv_maxwalkvelocity", "300");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_maxcrouchvelocity, "sv_maxcrouchvelocity", "100");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_maxswimvelocity, "sv_maxswimvelocity", "150");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_jumpvel, "sv_jumpvel", "224");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_maxacceleration, "sv_maxacceleration", "2200");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_friction, "sv_friction", "6");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_stopspeed, "sv_stopspeed", "100");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_maxstep, "sv_maxstep", "18");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_maxbarrier, "sv_maxbarrier", "50");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_maxsteepness, "sv_maxsteepness", "0.7");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_maxwaterjump, "sv_maxwaterjump", "20");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_watergravity, "sv_watergravity", "400");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.sv_waterfriction, "sv_waterfriction", "1");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.weaponconfig, "weaponconfig", BRIDGE_DEFAULT_WEAPONCONFIG);
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.max_weaponinfo, "max_weaponinfo", "32");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.max_projectileinfo, "max_projectileinfo", "32");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.dmflags, "dmflags", "0");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.usehook, "usehook", "1");
    BridgeConfig_CacheLibVar(&g_bridge_config_cache.rocketjump, "rocketjump", "1");

    g_bridge_config_initialised = true;
    return true;
}

void BridgeConfig_Shutdown(void)
{
    memset(&g_bridge_config_cache, 0, sizeof(g_bridge_config_cache));
    g_bridge_config_initialised = false;
}

libvar_t *Bridge_MaxClients(void)
{
    return g_bridge_config_cache.maxclients;
}

libvar_t *Bridge_MaxEntities(void)
{
    return g_bridge_config_cache.maxentities;
}

libvar_t *Bridge_Gravity(void)
{
    return g_bridge_config_cache.sv_gravity;
}

libvar_t *Bridge_MaxVelocity(void)
{
    return g_bridge_config_cache.sv_maxvelocity;
}

libvar_t *Bridge_AirAccelerate(void)
{
    return g_bridge_config_cache.sv_airaccelerate;
}

libvar_t *Bridge_MaxWalkVelocity(void)
{
    return g_bridge_config_cache.sv_maxwalkvelocity;
}

libvar_t *Bridge_MaxCrouchVelocity(void)
{
    return g_bridge_config_cache.sv_maxcrouchvelocity;
}

libvar_t *Bridge_MaxSwimVelocity(void)
{
    return g_bridge_config_cache.sv_maxswimvelocity;
}

libvar_t *Bridge_JumpVelocity(void)
{
    return g_bridge_config_cache.sv_jumpvel;
}

libvar_t *Bridge_MaxAcceleration(void)
{
    return g_bridge_config_cache.sv_maxacceleration;
}

libvar_t *Bridge_Friction(void)
{
    return g_bridge_config_cache.sv_friction;
}

libvar_t *Bridge_StopSpeed(void)
{
    return g_bridge_config_cache.sv_stopspeed;
}

libvar_t *Bridge_MaxStep(void)
{
    return g_bridge_config_cache.sv_maxstep;
}

libvar_t *Bridge_MaxBarrier(void)
{
    return g_bridge_config_cache.sv_maxbarrier;
}

libvar_t *Bridge_MaxSteepness(void)
{
    return g_bridge_config_cache.sv_maxsteepness;
}

libvar_t *Bridge_MaxWaterJump(void)
{
    return g_bridge_config_cache.sv_maxwaterjump;
}

libvar_t *Bridge_WaterGravity(void)
{
    return g_bridge_config_cache.sv_watergravity;
}

libvar_t *Bridge_WaterFriction(void)
{
    return g_bridge_config_cache.sv_waterfriction;
}

libvar_t *Bridge_WeaponConfig(void)
{
    return g_bridge_config_cache.weaponconfig;
}

libvar_t *Bridge_MaxWeaponInfo(void)
{
    return g_bridge_config_cache.max_weaponinfo;
}

libvar_t *Bridge_MaxProjectileInfo(void)
{
    return g_bridge_config_cache.max_projectileinfo;
}

libvar_t *Bridge_DMFlags(void)
{
    return g_bridge_config_cache.dmflags;
}

libvar_t *Bridge_UseHook(void)
{
    return g_bridge_config_cache.usehook;
}

libvar_t *Bridge_RocketJump(void)
{
    return g_bridge_config_cache.rocketjump;
}
