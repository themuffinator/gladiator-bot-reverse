#include "l_assets.h"

#include "l_libvar.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#endif

#define BOTLIB_PAK_NAME_FIELD 56
#define BOTLIB_MAX_PAK_FILES 32

#ifdef _WIN32
#define BotLib_PlatformMkdir(path) _mkdir(path)
#else
#define BotLib_PlatformMkdir(path) mkdir(path, 0775)
#endif

typedef struct BotLib_PakPath_s {
    char path[BOTLIB_ASSET_MAX_PATH];
} BotLib_PakPath;

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
                                    bool is_override,
                                    const char **roots,
                                    bool *override_flags,
                                    size_t *root_count,
                                    size_t max_roots)
{
    if (BotLib_IsStringEmpty(candidate) || roots == NULL || root_count == NULL
        || override_flags == NULL) {
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
    override_flags[*root_count] = is_override;
    *root_count += 1;
    return true;
}

static bool BotLib_AddCompoundRoot(const char *lhs,
                                   const char *rhs,
                                   bool is_override,
                                   char legacy_storage[][BOTLIB_ASSET_MAX_PATH],
                                   size_t legacy_capacity,
                                   size_t *legacy_count,
                                   const char **roots,
                                   bool *override_flags,
                                   size_t *root_count,
                                   size_t max_roots)
{
    if (BotLib_IsStringEmpty(lhs) || BotLib_IsStringEmpty(rhs) || legacy_storage == NULL ||
        legacy_count == NULL || override_flags == NULL) {
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
    override_flags[*root_count] = is_override;
    *root_count += 1;
    *legacy_count += 1;
    return true;
}

static size_t BotLib_BuildSearchRoots(const char **roots,
                                      bool *override_flags,
                                      size_t max_roots,
                                      char legacy_storage[][BOTLIB_ASSET_MAX_PATH],
                                      size_t legacy_capacity)
{
    size_t root_count = 0;
    size_t legacy_count = 0;

    const char *basedir = LibVarString("basedir", "");
    const char *gamedir = LibVarString("gamedir", "");
    const char *cddir = LibVarString("cddir", "");

    BotLib_AddCompoundRoot(basedir,
                           gamedir,
                           false,
                           legacy_storage,
                           legacy_capacity,
                           &legacy_count,
                           roots,
                           override_flags,
                           &root_count,
                           max_roots);
    BotLib_AddRootCandidate(basedir, false, roots, override_flags, &root_count, max_roots);

    BotLib_AddCompoundRoot(cddir,
                           gamedir,
                           false,
                           legacy_storage,
                           legacy_capacity,
                           &legacy_count,
                           roots,
                           override_flags,
                           &root_count,
                           max_roots);
    BotLib_AddRootCandidate(cddir, false, roots, override_flags, &root_count, max_roots);

    BotLib_AddRootCandidate(gamedir, false, roots, override_flags, &root_count, max_roots);

    const char *libvar_root = LibVarString("gladiator_asset_dir", "");
    BotLib_AddRootCandidate(libvar_root, true, roots, override_flags, &root_count, max_roots);

    const char *env_root = getenv("GLADIATOR_ASSET_DIR");
    BotLib_AddRootCandidate(env_root, true, roots, override_flags, &root_count, max_roots);

    const char *fallbacks[] = {
        "dev_tools/assets",
        "../dev_tools/assets",
        "../../dev_tools/assets",
    };

    for (size_t i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); ++i) {
        BotLib_AddRootCandidate(fallbacks[i], true, roots, override_flags, &root_count, max_roots);
    }

    return root_count;
}

static bool BotLib_StringEndsWith(const char *text, const char *suffix)
{
    if (text == NULL || suffix == NULL) {
        return false;
    }

    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);
    if (suffix_length > text_length) {
        return false;
    }

    return strncmp(text + text_length - suffix_length, suffix, suffix_length) == 0;
}

static const char *BotLib_FindLastSeparator(const char *path)
{
    if (path == NULL) {
        return NULL;
    }

    const char *forward = strrchr(path, '/');
    const char *backward = strrchr(path, '\\');
    if (forward == NULL) {
        return backward;
    }
    if (backward == NULL) {
        return forward;
    }
    return (forward > backward) ? forward : backward;
}

static void BotLib_PathSplit(const char *path, char *directory, size_t directory_size, char *filename, size_t filename_size)
{
    if (directory != NULL && directory_size > 0) {
        directory[0] = '\0';
    }
    if (filename != NULL && filename_size > 0) {
        filename[0] = '\0';
    }

    if (path == NULL) {
        return;
    }

    const char *separator = BotLib_FindLastSeparator(path);
    if (separator == NULL) {
        if (filename != NULL && filename_size > 0) {
            snprintf(filename, filename_size, "%s", path);
        }
        return;
    }

    if (directory != NULL && directory_size > 0) {
        size_t length = (size_t)(separator - path);
        if (length >= directory_size) {
            length = directory_size - 1;
        }
        memcpy(directory, path, length);
        directory[length] = '\0';
    }

    if (filename != NULL && filename_size > 0) {
        snprintf(filename, filename_size, "%s", separator + 1);
    }
}

static bool BotLib_PathHasParentTraversal(const char *path)
{
    if (path == NULL) {
        return true;
    }

    const char *cursor = path;
    while ((cursor = strstr(cursor, "..")) != NULL) {
        bool at_start = (cursor == path) || (*(cursor - 1) == '/');
        bool at_end = (cursor[2] == '\0') || (cursor[2] == '/');
        if (at_start && at_end) {
            return true;
        }
        cursor += 2;
    }

    return false;
}

static void BotLib_NormalizePakKey(char *buffer, size_t size, const char *path)
{
    if (buffer == NULL || size == 0) {
        return;
    }

    buffer[0] = '\0';
    if (path == NULL) {
        return;
    }

    size_t index = 0;
    for (size_t i = 0; path[i] != '\0' && index + 1 < size; ++i) {
        char c = path[i];
        if (c == '\\') {
            c = '/';
        }
        c = (char)tolower((unsigned char)c);
        if (index == 0 && c == '/') {
            continue;
        }
        buffer[index++] = c;
    }
    buffer[index] = '\0';
}

static bool BotLib_CreateDirectoryTree(const char *path)
{
    if (BotLib_IsStringEmpty(path)) {
        return true;
    }

    char scratch[BOTLIB_ASSET_MAX_PATH];
    BotLib_CopyPath(scratch, sizeof(scratch), path);
    size_t length = strlen(scratch);
    for (size_t i = 0; i < length; ++i) {
        if (scratch[i] == '\\') {
            scratch[i] = '/';
        }
    }

    for (size_t i = 1; i <= length; ++i) {
        if (scratch[i] != '/' && scratch[i] != '\0') {
            continue;
        }

        char old = scratch[i];
        scratch[i] = '\0';

        if (scratch[0] != '\0') {
            if (BotLib_PlatformMkdir(scratch) != 0 && errno != EEXIST) {
                scratch[i] = old;
                return false;
            }
        }

        scratch[i] = old;
    }

    return true;
}

static bool BotLib_EnsureDirectory(const char *path)
{
    if (BotLib_IsStringEmpty(path)) {
        return false;
    }

    char directory[BOTLIB_ASSET_MAX_PATH];
    BotLib_PathSplit(path, directory, sizeof(directory), NULL, 0);
    if (BotLib_IsStringEmpty(directory)) {
        return true;
    }

    return BotLib_CreateDirectoryTree(directory);
}

static int BotLib_ComparePakPaths(const void *lhs, const void *rhs)
{
    const BotLib_PakPath *a = (const BotLib_PakPath *)lhs;
    const BotLib_PakPath *b = (const BotLib_PakPath *)rhs;

    const char *a_base = BotLib_FindLastSeparator(a->path);
    const char *b_base = BotLib_FindLastSeparator(b->path);
    a_base = (a_base != NULL) ? a_base + 1 : a->path;
    b_base = (b_base != NULL) ? b_base + 1 : b->path;

    return strcmp(b_base, a_base);
}

static size_t BotLib_CollectPakFiles(const char *root, BotLib_PakPath *paths, size_t max_paths)
{
    if (BotLib_IsStringEmpty(root) || paths == NULL || max_paths == 0) {
        return 0;
    }

    size_t count = 0;

#ifdef _WIN32
    char pattern[BOTLIB_ASSET_MAX_PATH];
    int written = snprintf(pattern, sizeof(pattern), "%s\\*.pak", root);
    if (written < 0 || (size_t)written >= sizeof(pattern)) {
        return 0;
    }

    struct _finddata_t data;
    intptr_t handle = _findfirst(pattern, &data);
    if (handle == -1) {
        return 0;
    }

    do {
        if (count >= max_paths) {
            break;
        }
        if ((data.attrib & _A_SUBDIR) != 0) {
            continue;
        }

        int full_written = snprintf(paths[count].path, sizeof(paths[count].path), "%s/%s", root, data.name);
        if (full_written < 0 || (size_t)full_written >= sizeof(paths[count].path)) {
            continue;
        }
        ++count;
    } while (_findnext(handle, &data) == 0);

    _findclose(handle);
#else
    DIR *dir = opendir(root);
    if (dir == NULL) {
        return 0;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (count >= max_paths) {
            break;
        }

        if (entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN) {
            continue;
        }

        if (!BotLib_StringEndsWith(entry->d_name, ".pak")) {
            continue;
        }

        int written = snprintf(paths[count].path, sizeof(paths[count].path), "%s/%s", root, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(paths[count].path)) {
            continue;
        }
        ++count;
    }

    closedir(dir);
#endif

    qsort(paths, count, sizeof(paths[0]), BotLib_ComparePakPaths);
    return count;
}

static bool BotLib_BuildPakCachePath(const char *pak_file, const char *entry_name, char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return false;
    }

    buffer[0] = '\0';
    if (BotLib_IsStringEmpty(pak_file) || BotLib_IsStringEmpty(entry_name)) {
        return false;
    }

    char normalised_entry[BOTLIB_ASSET_MAX_PATH];
    BotLib_NormalizePakKey(normalised_entry, sizeof(normalised_entry), entry_name);
    if (BotLib_IsStringEmpty(normalised_entry) || BotLib_PathHasParentTraversal(normalised_entry)) {
        return false;
    }

    char directory[BOTLIB_ASSET_MAX_PATH];
    char filename[64];
    BotLib_PathSplit(pak_file, directory, sizeof(directory), filename, sizeof(filename));

    char *extension = strrchr(filename, '.');
    if (extension != NULL) {
        *extension = '\0';
    }

    if (filename[0] == '\0') {
        snprintf(filename, sizeof(filename), "pak");
    }

    if (directory[0] != '\0') {
        int written = snprintf(buffer,
                               size,
                               "%s/.pak_cache/%s/%s",
                               directory,
                               filename,
                               normalised_entry);
        return written >= 0 && (size_t)written < size;
    }

    int written = snprintf(buffer, size, ".pak_cache/%s/%s", filename, normalised_entry);
    return written >= 0 && (size_t)written < size;
}

static bool BotLib_WritePakEntryToCache(FILE *pak,
                                        const char *pak_file,
                                        const char *entry_name,
                                        int32_t entry_offset,
                                        int32_t entry_length,
                                        char *resolved,
                                        size_t resolved_size)
{
    if (pak == NULL || pak_file == NULL || entry_name == NULL || entry_offset < 0 || entry_length < 0) {
        return false;
    }

    char cache_path[BOTLIB_ASSET_MAX_PATH];
    if (!BotLib_BuildPakCachePath(pak_file, entry_name, cache_path, sizeof(cache_path))) {
        return false;
    }

    if (!BotLib_EnsureDirectory(cache_path)) {
        return false;
    }

    if (BotLib_FileExists(cache_path)) {
        FILE *existing = fopen(cache_path, "rb");
        if (existing != NULL) {
            if (fseek(existing, 0, SEEK_END) == 0) {
                long size = ftell(existing);
                if (size >= 0 && (int32_t)size == entry_length) {
                    fclose(existing);
                    BotLib_CopyPath(resolved, resolved_size, cache_path);
                    return true;
                }
            }
            fclose(existing);
        }
    }

    if (fseek(pak, entry_offset, SEEK_SET) != 0) {
        return false;
    }

    FILE *output = fopen(cache_path, "wb");
    if (output == NULL) {
        return false;
    }

    bool success = true;
    unsigned char buffer[4096];
    int32_t remaining = entry_length;
    while (remaining > 0) {
        size_t chunk = (size_t)((remaining > (int32_t)sizeof(buffer)) ? sizeof(buffer) : remaining);
        size_t read_bytes = fread(buffer, 1, chunk, pak);
        if (read_bytes != chunk) {
            success = false;
            break;
        }

        size_t written = fwrite(buffer, 1, read_bytes, output);
        if (written != read_bytes) {
            success = false;
            break;
        }

        remaining -= (int32_t)read_bytes;
    }

    fclose(output);

    if (!success) {
        remove(cache_path);
        return false;
    }

    BotLib_CopyPath(resolved, resolved_size, cache_path);
    return true;
}

static bool BotLib_ExtractPakEntry(const char *pak_file,
                                   const char *normalized_target,
                                   char *resolved,
                                   size_t resolved_size)
{
    if (resolved != NULL && resolved_size > 0) {
        resolved[0] = '\0';
    }

    if (BotLib_IsStringEmpty(pak_file) || BotLib_IsStringEmpty(normalized_target)) {
        return false;
    }

    FILE *pak = fopen(pak_file, "rb");
    if (pak == NULL) {
        return false;
    }

    char magic[4];
    if (fread(magic, 1, sizeof(magic), pak) != sizeof(magic) || memcmp(magic, "PACK", sizeof(magic)) != 0) {
        fclose(pak);
        return false;
    }

    int32_t directory_offset = 0;
    int32_t directory_length = 0;
    if (fread(&directory_offset, sizeof(directory_offset), 1, pak) != 1
        || fread(&directory_length, sizeof(directory_length), 1, pak) != 1) {
        fclose(pak);
        return false;
    }

    if (directory_offset <= 0 || directory_length <= 0) {
        fclose(pak);
        return false;
    }

    if (fseek(pak, directory_offset, SEEK_SET) != 0) {
        fclose(pak);
        return false;
    }

    size_t entry_count = (size_t)(directory_length / 64);
    for (size_t i = 0; i < entry_count; ++i) {
        unsigned char raw_name[BOTLIB_PAK_NAME_FIELD];
        if (fread(raw_name, 1, sizeof(raw_name), pak) != sizeof(raw_name)) {
            fclose(pak);
            return false;
        }

        char entry_name[BOTLIB_PAK_NAME_FIELD + 1];
        memcpy(entry_name, raw_name, sizeof(raw_name));
        entry_name[sizeof(raw_name)] = '\0';

        int32_t entry_offset = 0;
        int32_t entry_length = 0;
        if (fread(&entry_offset, sizeof(entry_offset), 1, pak) != 1
            || fread(&entry_length, sizeof(entry_length), 1, pak) != 1) {
            fclose(pak);
            return false;
        }

        char normalised_entry[BOTLIB_ASSET_MAX_PATH];
        BotLib_NormalizePakKey(normalised_entry, sizeof(normalised_entry), entry_name);
        if (!BotLib_StringEquals(normalised_entry, normalized_target)) {
            continue;
        }

        bool result = BotLib_WritePakEntryToCache(pak,
                                                  pak_file,
                                                  entry_name,
                                                  entry_offset,
                                                  entry_length,
                                                  resolved,
                                                  resolved_size);
        fclose(pak);
        return result;
    }

    fclose(pak);
    return false;
}

static bool BotLib_SearchPakForAsset(const char *root,
                                     const char *relative,
                                     char *resolved,
                                     size_t resolved_size)
{
    if (resolved != NULL && resolved_size > 0) {
        resolved[0] = '\0';
    }

    if (BotLib_IsStringEmpty(root) || BotLib_IsStringEmpty(relative)) {
        return false;
    }

    char target[BOTLIB_ASSET_MAX_PATH];
    BotLib_NormalizePakKey(target, sizeof(target), relative);
    if (BotLib_IsStringEmpty(target) || BotLib_PathHasParentTraversal(target)) {
        return false;
    }

    BotLib_PakPath pak_files[BOTLIB_MAX_PAK_FILES];
    size_t pak_count = BotLib_CollectPakFiles(root, pak_files, BOTLIB_MAX_PAK_FILES);
    if (pak_count == 0) {
        return false;
    }

    for (size_t i = 0; i < pak_count; ++i) {
        if (BotLib_ExtractPakEntry(pak_files[i].path, target, resolved, resolved_size)) {
            return true;
        }
    }

    return false;
}

static bool BotLib_CheckFilesystemPaths(const char *root,
                                        const char *requested,
                                        bool bare_name,
                                        const char *preferred_subdir,
                                        char *buffer,
                                        size_t size,
                                        char *last_candidate,
                                        size_t last_size)
{
    if (BotLib_IsStringEmpty(root) || BotLib_IsStringEmpty(requested)) {
        return false;
    }

    char candidate[BOTLIB_ASSET_MAX_PATH];

    if (bare_name && !BotLib_IsStringEmpty(preferred_subdir)) {
        int written = snprintf(candidate, sizeof(candidate), "%s/%s/%s", root, preferred_subdir, requested);
        if (written >= 0 && (size_t)written < sizeof(candidate)) {
            BotLib_CopyPath(last_candidate, last_size, candidate);
            if (BotLib_FileExists(candidate)) {
                BotLib_CopyPath(buffer, size, candidate);
                return true;
            }
        }
    }

    int written = snprintf(candidate, sizeof(candidate), "%s/%s", root, requested);
    if (written >= 0 && (size_t)written < sizeof(candidate)) {
        BotLib_CopyPath(last_candidate, last_size, candidate);
        if (BotLib_FileExists(candidate)) {
            BotLib_CopyPath(buffer, size, candidate);
            return true;
        }
    }

    return false;
}

static bool BotLib_CheckPakPaths(const char *root,
                                 const char *requested,
                                 bool bare_name,
                                 const char *preferred_subdir,
                                 char *resolved,
                                 size_t resolved_size,
                                 char *last_candidate,
                                 size_t last_size)
{
    if (BotLib_IsStringEmpty(root) || BotLib_IsStringEmpty(requested)) {
        return false;
    }

    char relative[BOTLIB_ASSET_MAX_PATH];

    if (bare_name && !BotLib_IsStringEmpty(preferred_subdir)) {
        int written = snprintf(relative, sizeof(relative), "%s/%s", preferred_subdir, requested);
        if (written >= 0 && (size_t)written < sizeof(relative)) {
            if (BotLib_SearchPakForAsset(root, relative, resolved, resolved_size)) {
                BotLib_CopyPath(last_candidate, last_size, resolved);
                return true;
            }
        }
    }

    if (BotLib_SearchPakForAsset(root, requested, resolved, resolved_size)) {
        BotLib_CopyPath(last_candidate, last_size, resolved);
        return true;
    }

    return false;
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
    bool override_flags[sizeof(roots) / sizeof(roots[0])];
    char legacy_storage[4][BOTLIB_ASSET_MAX_PATH];
    size_t root_count = BotLib_BuildSearchRoots(roots,
                                               override_flags,
                                               sizeof(roots) / sizeof(roots[0]),
                                               legacy_storage,
                                               sizeof(legacy_storage) / sizeof(legacy_storage[0]));

    (void)override_flags;

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
    bool override_flags[sizeof(roots) / sizeof(roots[0])];
    char legacy_storage[4][BOTLIB_ASSET_MAX_PATH];
    size_t root_count = BotLib_BuildSearchRoots(roots,
                                               override_flags,
                                               sizeof(roots) / sizeof(roots[0]),
                                               legacy_storage,
                                               sizeof(legacy_storage) / sizeof(legacy_storage[0]));

    char resolved_from_pak[BOTLIB_ASSET_MAX_PATH];

    for (size_t i = 0; i < root_count; ++i) {
        if (!override_flags[i]) {
            continue;
        }

        const char *root = roots[i];
        if (root == NULL || root[0] == '\0') {
            continue;
        }

        if (BotLib_CheckFilesystemPaths(root,
                                        requested,
                                        bare_name,
                                        preferred_subdir,
                                        buffer,
                                        size,
                                        last_candidate,
                                        sizeof(last_candidate))) {
            return true;
        }
    }

    for (size_t i = 0; i < root_count; ++i) {
        const char *root = roots[i];
        if (root == NULL || root[0] == '\0') {
            continue;
        }

        if (!override_flags[i]) {
            if (BotLib_CheckPakPaths(root,
                                     requested,
                                     bare_name,
                                     preferred_subdir,
                                     resolved_from_pak,
                                     sizeof(resolved_from_pak),
                                     last_candidate,
                                     sizeof(last_candidate))) {
                BotLib_CopyPath(buffer, size, resolved_from_pak);
                return true;
            }

            if (BotLib_CheckFilesystemPaths(root,
                                            requested,
                                            bare_name,
                                            preferred_subdir,
                                            buffer,
                                            size,
                                            last_candidate,
                                            sizeof(last_candidate))) {
                return true;
            }
        } else {
            if (BotLib_CheckPakPaths(root,
                                     requested,
                                     bare_name,
                                     preferred_subdir,
                                     resolved_from_pak,
                                     sizeof(resolved_from_pak),
                                     last_candidate,
                                     sizeof(last_candidate))) {
                BotLib_CopyPath(buffer, size, resolved_from_pak);
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
