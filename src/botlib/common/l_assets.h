#ifndef BOTLIB_COMMON_L_ASSETS_H
#define BOTLIB_COMMON_L_ASSETS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOTLIB_ASSET_MAX_PATH 1024

bool BotLib_LocateAssetRoot(char *buffer, size_t size);

bool BotLib_ResolveAssetPath(const char *requested,
                             const char *preferred_subdir,
                             char *buffer,
                             size_t size);

#ifdef __cplusplus
}
#endif

#endif /* BOTLIB_COMMON_L_ASSETS_H */
