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

static bool BotLib_IsStringEmpty(const char *text)
{
    return text == NULL || text[0] == '\0';
}

static bool BotLib_AddRootCandidate(const char *candidate,
                                    const char **roots,
                                    size_t *root_count,
                                    size_t max_roots)
{
    if (BotLib_IsStringEmpty(candidate) || roots == NULL || root_count == NULL) {
        return false;
    }

    if (*root_count >= max_roots) {
        return false;
    }

    for (size_t i = 0; i < *root_count; ++i) {
        if (BotLib_StringEquals(roots[i], candidate)) {
            return false;
        }
    }

    roots[*root_count] = candidate;
    *root_count += 1;
    return true;
}

static bool BotLib_AddCompoundRoot(const char *lhs,
                                   const char *rhs,
                                   char legacy_storage[][BOTLIB_ASSET_MAX_PATH],
                                   size_t legacy_capacity,
                                   size_t *legacy_count,
                                   const char **roots,
                                   size_t *root_count,
                                   size_t max_roots)
{
    if (BotLib_IsStringEmpty(lhs) || BotLib_IsStringEmpty(rhs) || legacy_storage == NULL ||
        legacy_count == NULL) {
        return false;
    }

    char scratch[BOTLIB_ASSET_MAX_PATH];
    int written = snprintf(scratch, sizeof(scratch), "%s/%s", lhs, rhs);
    if (written < 0 || (size_t)written >= sizeof(scratch)) {
        return false;
    }

    if (roots == NULL || root_count == NULL || *root_count >= max_roots) {
        return false;
    }

    for (size_t i = 0; i < *root_count; ++i) {
        if (BotLib_StringEquals(roots[i], scratch)) {
            return false;
        }
    }

    if (*legacy_count >= legacy_capacity) {
        return false;
    }

    snprintf(legacy_storage[*legacy_count], BOTLIB_ASSET_MAX_PATH, "%s", scratch);
    roots[*root_count] = legacy_storage[*legacy_count];
    *root_count += 1;
    *legacy_count += 1;
    return true;
}

static size_t BotLib_BuildSearchRoots(const char **roots,
                                      size_t max_roots,
                                      char legacy_storage[][BOTLIB_ASSET_MAX_PATH],
                                      size_t legacy_capacity)
{
    size_t root_count = 0;
    size_t legacy_count = 0;

    const char *basedir = LibVarString("basedir", "");
    const char *gamedir = LibVarString("gamedir", "");
    const char *cddir = LibVarString("cddir", "");

    BotLib_AddCompoundRoot(basedir, gamedir, legacy_storage, legacy_capacity, &legacy_count, roots, &root_count, max_roots);
    BotLib_AddRootCandidate(basedir, roots, &root_count, max_roots);

    BotLib_AddCompoundRoot(cddir, gamedir, legacy_storage, legacy_capacity, &legacy_count, roots, &root_count, max_roots);
    BotLib_AddRootCandidate(cddir, roots, &root_count, max_roots);

    BotLib_AddRootCandidate(gamedir, roots, &root_count, max_roots);

    const char *libvar_root = LibVarString("gladiator_asset_dir", "");
    BotLib_AddRootCandidate(libvar_root, roots, &root_count, max_roots);

    const char *env_root = getenv("GLADIATOR_ASSET_DIR");
    BotLib_AddRootCandidate(env_root, roots, &root_count, max_roots);

    const char *fallbacks[] = {
        "dev_tools/assets",
        "../dev_tools/assets",
        "../../dev_tools/assets",
    };

    for (size_t i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); ++i) {
        BotLib_AddRootCandidate(fallbacks[i], roots, &root_count, max_roots);
    }

    return root_count;
}

bool BotLib_LocateAssetRoot(char *buffer, size_t size)
{
    if (buffer != NULL && size > 0) {
        buffer[0] = '\0';
    }

    if (buffer == NULL || size == 0) {
        return false;
    }

    const char *roots[16];
    char legacy_storage[4][BOTLIB_ASSET_MAX_PATH];
    size_t root_count = BotLib_BuildSearchRoots(roots, sizeof(roots) / sizeof(roots[0]), legacy_storage, sizeof(legacy_storage) / sizeof(legacy_storage[0]));

    for (size_t i = 0; i < root_count; ++i) {
        if (BotLib_ValidateAssetRoot(roots[i])) {
            BotLib_CopyPath(buffer, size, roots[i]);
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

    const char *roots[16];
    char legacy_storage[4][BOTLIB_ASSET_MAX_PATH];
    size_t root_count = BotLib_BuildSearchRoots(roots, sizeof(roots) / sizeof(roots[0]), legacy_storage, sizeof(legacy_storage) / sizeof(legacy_storage[0]));

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
