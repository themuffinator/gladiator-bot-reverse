#include "l_assets.h"

#include "l_libvar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool BotLib_FileExists(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    fclose(file);
    return true;
}

static void BotLib_CopyPath(char *buffer, size_t size, const char *text)
{
    if (buffer == NULL || size == 0) {
        return;
    }

    if (text == NULL) {
        buffer[0] = '\0';
        return;
    }

    buffer[0] = '\0';
    snprintf(buffer, size, "%s", text);
}

static bool BotLib_ValidateAssetRoot(const char *root)
{
    if (root == NULL || root[0] == '\0') {
        return false;
    }

    char probe[BOTLIB_ASSET_MAX_PATH];
    int written = snprintf(probe, sizeof(probe), "%s/chars.h", root);
    if (written < 0 || (size_t)written >= sizeof(probe)) {
        return false;
    }

    return BotLib_FileExists(probe);
}

static bool BotLib_StringEquals(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return false;
    }
    return strcmp(lhs, rhs) == 0;
}

bool BotLib_LocateAssetRoot(char *buffer, size_t size)
{
    if (buffer != NULL && size > 0) {
        buffer[0] = '\0';
    }

    if (buffer == NULL || size == 0) {
        return false;
    }

    const char *libvar_root = LibVarString("gladiator_asset_dir", "");
    if (BotLib_ValidateAssetRoot(libvar_root)) {
        BotLib_CopyPath(buffer, size, libvar_root);
        return true;
    }

    const char *env_root = getenv("GLADIATOR_ASSET_DIR");
    if (BotLib_ValidateAssetRoot(env_root)) {
        BotLib_CopyPath(buffer, size, env_root);
        return true;
    }

    const char *fallbacks[] = {
        "dev_tools/assets",
        "../dev_tools/assets",
        "../../dev_tools/assets",
    };

    for (size_t i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); ++i) {
        if (BotLib_ValidateAssetRoot(fallbacks[i])) {
            BotLib_CopyPath(buffer, size, fallbacks[i]);
            return true;
        }
    }

    return false;
}

bool BotLib_ResolveAssetPath(const char *requested,
                             const char *preferred_subdir,
                             char *buffer,
                             size_t size)
{
    if (buffer != NULL && size > 0) {
        buffer[0] = '\0';
    }

    if (requested == NULL || requested[0] == '\0') {
        return false;
    }

    if (BotLib_FileExists(requested)) {
        BotLib_CopyPath(buffer, size, requested);
        return true;
    }

    char last_candidate[BOTLIB_ASSET_MAX_PATH];
    last_candidate[0] = '\0';

    const bool bare_name = (strchr(requested, '/') == NULL && strchr(requested, '\\') == NULL);

    const char *roots[8];
    size_t root_count = 0;

    const char *libvar_root = LibVarString("gladiator_asset_dir", "");
    if (libvar_root != NULL && libvar_root[0] != '\0') {
        roots[root_count++] = libvar_root;
    }

    const char *env_root = getenv("GLADIATOR_ASSET_DIR");
    if (env_root != NULL && env_root[0] != '\0') {
        bool duplicate = false;
        for (size_t i = 0; i < root_count; ++i) {
            if (BotLib_StringEquals(roots[i], env_root)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            roots[root_count++] = env_root;
        }
    }

    const char *fallback_roots[] = {
        "dev_tools/assets",
        "../dev_tools/assets",
        "../../dev_tools/assets",
    };

    for (size_t i = 0; i < sizeof(fallback_roots) / sizeof(fallback_roots[0]); ++i) {
        bool duplicate = false;
        for (size_t j = 0; j < root_count; ++j) {
            if (BotLib_StringEquals(roots[j], fallback_roots[i])) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            roots[root_count++] = fallback_roots[i];
        }
    }

    char candidate[BOTLIB_ASSET_MAX_PATH];
    for (size_t i = 0; i < root_count; ++i) {
        const char *root = roots[i];
        if (root == NULL || root[0] == '\0') {
            continue;
        }

        if (bare_name && preferred_subdir != NULL && preferred_subdir[0] != '\0') {
            int written = snprintf(candidate, sizeof(candidate), "%s/%s/%s", root, preferred_subdir, requested);
            if (written >= 0 && (size_t)written < sizeof(candidate)) {
                BotLib_CopyPath(last_candidate, sizeof(last_candidate), candidate);
                if (BotLib_FileExists(candidate)) {
                    BotLib_CopyPath(buffer, size, candidate);
                    return true;
                }
            }
        }

        int written = snprintf(candidate, sizeof(candidate), "%s/%s", root, requested);
        if (written >= 0 && (size_t)written < sizeof(candidate)) {
            BotLib_CopyPath(last_candidate, sizeof(last_candidate), candidate);
            if (BotLib_FileExists(candidate)) {
                BotLib_CopyPath(buffer, size, candidate);
                return true;
            }
        }
    }

    if (buffer != NULL && size > 0) {
        if (last_candidate[0] != '\0') {
            BotLib_CopyPath(buffer, size, last_candidate);
        } else {
            BotLib_CopyPath(buffer, size, requested);
        }
    }

    return false;
}
