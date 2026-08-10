#ifndef BOTLIB_COMMON_L_ASSETS_H
#define BOTLIB_COMMON_L_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOTLIB_ASSET_MAX_PATH 1024

typedef struct botlib_asset_resolution_s {
	char resolved_path[BOTLIB_ASSET_MAX_PATH];
	char source_path[BOTLIB_ASSET_MAX_PATH];
	int32_t pak_entry_length;
} botlib_asset_resolution_t;

bool BotLib_LocateAssetRoot(char *buffer, size_t size);

bool BotLib_ResolveAssetPathDetailed(const char *requested,
	const char *preferred_subdir,
	botlib_asset_resolution_t *resolution);

bool BotLib_ResolveAssetPath(const char *requested,
	const char *preferred_subdir,
	char *buffer,
	size_t size);

#ifdef __cplusplus
}
#endif

#endif /* BOTLIB_COMMON_L_ASSETS_H */
