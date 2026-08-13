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
#define BOTLIB_MAX_PAK_FILES 10

/* Retail stores this literal in the second game-directory slot of
   sub_10041ba0 at 0x10041c3f; aas_map.c uses the same spelling. */
#define BOTLIB_DEFAULT_GAMEDIR "baseq2"

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

/*
=============
BotLib_AddRootCandidate

Adds a search root while preserving override semantics when a later source
aliases an already-registered directory.
=============
*/
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

	for (size_t i = 0; i < *root_count; ++i) {
		if (BotLib_StringEquals(roots[i], candidate)) {
			if (is_override) {
				override_flags[i] = true;
			}
			return false;
		}
	}

	if (*root_count >= max_roots) {
		return false;
	}

	roots[*root_count] = candidate;
	override_flags[*root_count] = is_override;
	*root_count += 1;
	return true;
}

/*
=============
BotLib_AddCompoundRoot

Builds combined legacy roots and upgrades duplicate entries when an override
path resolves to the same directory.
=============
*/
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

	if (roots == NULL || root_count == NULL) {
		return false;
	}

	for (size_t i = 0; i < *root_count; ++i) {
		if (BotLib_StringEquals(roots[i], scratch)) {
			if (is_override) {
				override_flags[i] = true;
			}
			return false;
		}
	}

	if (*root_count >= max_roots || *legacy_count >= legacy_capacity) {
		return false;
	}

	snprintf(legacy_storage[*legacy_count], BOTLIB_ASSET_MAX_PATH, "%s", scratch);
	roots[*root_count] = legacy_storage[*legacy_count];
	override_flags[*root_count] = is_override;
	*root_count += 1;
	*legacy_count += 1;
	return true;
}

/*
=============
BotLib_BuildSearchRoots

Reproduces the retail probe order: basedir and cddir each crossed with the
gamedir libvar and the "baseq2" default, followed by the host-side overrides.
=============
*/
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

	/* Retail sub_10041ba0 (0x10041ba0) fills exactly two 0x90 byte game
	   directory slots - the gamedir libvar at 0x10041c2b and the "baseq2"
	   literal at 0x10041c3f - then iterates both of them (0x10041e24
	   "var_244 + 1 s< 2") once for basedir and once for cddir, the two calls
	   sub_10041f60 makes at 0x10041f92 and 0x10041fba. The slot append at
	   0x10041ca8 is skipped for a zero-length slot, so an empty gamedir
	   collapses slot 0 onto the bare directory; otherwise a bare basedir,
	   cddir or gamedir is never probed. */
	const char *legacy_dirs[2];
	legacy_dirs[0] = basedir;
	legacy_dirs[1] = cddir;

	for (size_t i = 0; i < sizeof(legacy_dirs) / sizeof(legacy_dirs[0]); ++i) {
		if (BotLib_IsStringEmpty(gamedir)) {
			BotLib_AddRootCandidate(legacy_dirs[i],
									false,
									roots,
									override_flags,
									&root_count,
									max_roots);
		} else {
			BotLib_AddCompoundRoot(legacy_dirs[i],
								   gamedir,
								   false,
								   legacy_storage,
								   legacy_capacity,
								   &legacy_count,
								   roots,
								   override_flags,
								   &root_count,
								   max_roots);
		}

		BotLib_AddCompoundRoot(legacy_dirs[i],
							   BOTLIB_DEFAULT_GAMEDIR,
							   false,
							   legacy_storage,
							   legacy_capacity,
							   &legacy_count,
							   roots,
							   override_flags,
							   &root_count,
							   max_roots);
	}

	/* Retail has no environment variable at all - only the basedir, gamedir
	   and cddir libvars exist - so an explicitly set gladiator_asset_dir must
	   win and GLADIATOR_ASSET_DIR is consulted only when it is empty. */
	const char *libvar_root = LibVarString("gladiator_asset_dir", "");
	if (!BotLib_IsStringEmpty(libvar_root)) {
		BotLib_AddRootCandidate(libvar_root, true, roots, override_flags, &root_count, max_roots);
	} else {
		const char *env_root = getenv("GLADIATOR_ASSET_DIR");
		BotLib_AddRootCandidate(env_root, true, roots, override_flags, &root_count, max_roots);
	}

	const char *fallbacks[] = {
		"dev_tools/assets",
		"../dev_tools/assets",
		"../../dev_tools/assets",
	};

	for (size_t i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); ++i) {
		BotLib_AddRootCandidate(fallbacks[i], false, roots, override_flags, &root_count, max_roots);
	}

	return root_count;
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

/*
=============
BotLib_CreateDirectoryTree

Creates cache parent directories without attempting to create the Windows
drive designator itself.
=============
*/
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

    size_t first_separator = 1U;
#ifdef _WIN32
	if (length >= 3U && isalpha((unsigned char)scratch[0]) &&
		scratch[1] == ':' && scratch[2] == '/')
	{
		first_separator = 3U;
	}
#endif

    for (size_t i = first_separator; i <= length; ++i) {
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

/*
=============
BotLib_EnsureDirectory

Creates the parent directory tree required by a resolved cache path.
=============
*/
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

#ifndef _WIN32
/*
=============
BotLib_PakNameEquals

Matches package basenames with the case-insensitive filesystem semantics used
by the retail Windows build.
=============
*/
static bool BotLib_PakNameEquals(const char *lhs, const char *rhs)
{
	if (lhs == NULL || rhs == NULL)
	{
		return false;
	}

	while (*lhs != '\0' && *rhs != '\0')
	{
		if (tolower((unsigned char)*lhs) != tolower((unsigned char)*rhs))
		{
			return false;
		}
		++lhs;
		++rhs;
	}

	return *lhs == *rhs;
}
#endif

/*
=============
BotLib_CollectPakFiles

Probes only retail's pak0.pak through pak9.pak names, in ascending order.
=============
*/
static size_t BotLib_CollectPakFiles(const char *root, BotLib_PakPath *paths, size_t max_paths)
{
	if (BotLib_IsStringEmpty(root) || paths == NULL || max_paths == 0)
	{
		return 0;
	}

	size_t count = 0;
#ifndef _WIN32
	DIR *directory = opendir(root);
#endif

	for (unsigned int index = 0;
		index < BOTLIB_MAX_PAK_FILES && count < max_paths;
		++index)
	{
		char expected_name[16];
		int name_written = snprintf(expected_name,
			sizeof(expected_name),
			"pak%u.pak",
			index);
		if (name_written < 0 || (size_t)name_written >= sizeof(expected_name))
		{
			continue;
		}

		int path_written = snprintf(paths[count].path,
			sizeof(paths[count].path),
			"%s/%s",
			root,
			expected_name);
		if (path_written < 0 ||
			(size_t)path_written >= sizeof(paths[count].path))
		{
			continue;
		}
		if (BotLib_FileExists(paths[count].path))
		{
			++count;
			continue;
		}

#ifndef _WIN32
		if (directory == NULL)
		{
			continue;
		}

		rewinddir(directory);
		struct dirent *entry = NULL;
		while ((entry = readdir(directory)) != NULL)
		{
			if (!BotLib_PakNameEquals(entry->d_name, expected_name))
			{
				continue;
			}

			path_written = snprintf(paths[count].path,
				sizeof(paths[count].path),
				"%s/%s",
				root,
				entry->d_name);
			if (path_written >= 0 &&
				(size_t)path_written < sizeof(paths[count].path) &&
				BotLib_FileExists(paths[count].path))
			{
				++count;
			}
			break;
		}
#endif
	}

#ifndef _WIN32
	if (directory != NULL)
	{
		closedir(directory);
	}
#endif
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

/*
=============
BotLib_ExtractPakEntry

Extracts one package entry and reports both its readable cache file and the
container metadata retained by retail's file-open bookkeeping.
=============
*/
static bool BotLib_ExtractPakEntry(const char *pak_file,
	const char *normalized_target,
	char *resolved,
	size_t resolved_size,
	char *source_path,
	size_t source_path_size,
	int32_t *entry_length_out)
{
	if (resolved != NULL && resolved_size > 0)
	{
		resolved[0] = '\0';
	}
	if (source_path != NULL && source_path_size > 0)
	{
		source_path[0] = '\0';
	}
	if (entry_length_out != NULL)
	{
		*entry_length_out = 0;
	}

	if (BotLib_IsStringEmpty(pak_file) || BotLib_IsStringEmpty(normalized_target))
	{
		return false;
	}

	FILE *pak = fopen(pak_file, "rb");
	if (pak == NULL)
	{
		return false;
	}

	char magic[4];
	if (fread(magic, 1, sizeof(magic), pak) != sizeof(magic) ||
		memcmp(magic, "PACK", sizeof(magic)) != 0)
	{
		fclose(pak);
		return false;
	}

	int32_t directory_offset = 0;
	int32_t directory_length = 0;
	if (fread(&directory_offset, sizeof(directory_offset), 1, pak) != 1 ||
		fread(&directory_length, sizeof(directory_length), 1, pak) != 1)
	{
		fclose(pak);
		return false;
	}

	if (directory_offset <= 0 || directory_length <= 0)
	{
		fclose(pak);
		return false;
	}

	if (fseek(pak, directory_offset, SEEK_SET) != 0)
	{
		fclose(pak);
		return false;
	}

	size_t entry_count = (size_t)(directory_length / 64);
	for (size_t i = 0; i < entry_count; ++i)
	{
		unsigned char raw_name[BOTLIB_PAK_NAME_FIELD];
		if (fread(raw_name, 1, sizeof(raw_name), pak) != sizeof(raw_name))
		{
			fclose(pak);
			return false;
		}

		char entry_name[BOTLIB_PAK_NAME_FIELD + 1];
		memcpy(entry_name, raw_name, sizeof(raw_name));
		entry_name[sizeof(raw_name)] = '\0';

		int32_t entry_offset = 0;
		int32_t entry_length = 0;
		if (fread(&entry_offset, sizeof(entry_offset), 1, pak) != 1 ||
			fread(&entry_length, sizeof(entry_length), 1, pak) != 1)
		{
			fclose(pak);
			return false;
		}

		char normalised_entry[BOTLIB_ASSET_MAX_PATH];
		BotLib_NormalizePakKey(normalised_entry, sizeof(normalised_entry), entry_name);
		if (!BotLib_StringEquals(normalised_entry, normalized_target))
		{
			continue;
		}

		bool extracted = BotLib_WritePakEntryToCache(pak,
			pak_file,
			entry_name,
			entry_offset,
			entry_length,
			resolved,
			resolved_size);
		if (extracted)
		{
			BotLib_CopyPath(source_path, source_path_size, pak_file);
			if (entry_length_out != NULL)
			{
				*entry_length_out = entry_length;
			}
		}
		fclose(pak);
		return extracted;
	}

	fclose(pak);
	return false;
}

/*
=============
BotLib_SearchPakForAsset

Searches packages in retail precedence order while retaining the winning
container and directory-entry length.
=============
*/
static bool BotLib_SearchPakForAsset(const char *root,
	const char *relative,
	char *resolved,
	size_t resolved_size,
	char *source_path,
	size_t source_path_size,
	int32_t *entry_length_out)
{
	if (resolved != NULL && resolved_size > 0)
	{
		resolved[0] = '\0';
	}
	if (source_path != NULL && source_path_size > 0)
	{
		source_path[0] = '\0';
	}
	if (entry_length_out != NULL)
	{
		*entry_length_out = 0;
	}

	if (BotLib_IsStringEmpty(root) || BotLib_IsStringEmpty(relative))
	{
		return false;
	}

	char target[BOTLIB_ASSET_MAX_PATH];
	BotLib_NormalizePakKey(target, sizeof(target), relative);
	if (BotLib_IsStringEmpty(target) || BotLib_PathHasParentTraversal(target))
	{
		return false;
	}

	BotLib_PakPath pak_files[BOTLIB_MAX_PAK_FILES];
	size_t pak_count = BotLib_CollectPakFiles(root, pak_files, BOTLIB_MAX_PAK_FILES);
	if (pak_count == 0)
	{
		return false;
	}

	for (size_t i = 0; i < pak_count; ++i)
	{
		if (BotLib_ExtractPakEntry(pak_files[i].path,
			target,
			resolved,
			resolved_size,
			source_path,
			source_path_size,
			entry_length_out))
		{
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

/*
=============
BotLib_CheckPakPaths

Checks preferred and direct package-relative spellings and forwards the
winning package provenance.
=============
*/
static bool BotLib_CheckPakPaths(const char *root,
	const char *requested,
	bool bare_name,
	const char *preferred_subdir,
	char *resolved,
	size_t resolved_size,
	char *source_path,
	size_t source_path_size,
	int32_t *entry_length_out,
	char *last_candidate,
	size_t last_size)
{
	if (BotLib_IsStringEmpty(root) || BotLib_IsStringEmpty(requested))
	{
		return false;
	}

	char relative[BOTLIB_ASSET_MAX_PATH];

	if (bare_name && !BotLib_IsStringEmpty(preferred_subdir))
	{
		int written = snprintf(relative, sizeof(relative), "%s/%s", preferred_subdir, requested);
		if (written >= 0 && (size_t)written < sizeof(relative) &&
			BotLib_SearchPakForAsset(root,
				relative,
				resolved,
				resolved_size,
				source_path,
				source_path_size,
				entry_length_out))
		{
			BotLib_CopyPath(last_candidate, last_size, resolved);
			return true;
		}
	}

	if (BotLib_SearchPakForAsset(root,
		requested,
		resolved,
		resolved_size,
		source_path,
		source_path_size,
		entry_length_out))
	{
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

/*
=============
BotLib_ResolveAssetPathDetailed

Resolves an asset to a readable file while retaining its loose source or PAK
container provenance. Failed lookups retain the same last-candidate spelling
returned by the legacy resolver.
=============
*/
bool BotLib_ResolveAssetPathDetailed(const char *requested,
	const char *preferred_subdir,
	botlib_asset_resolution_t *resolution)
{
	if (resolution == NULL)
	{
		return false;
	}
	memset(resolution, 0, sizeof(*resolution));

	if (requested == NULL || requested[0] == '\0')
	{
		return false;
	}

	const bool bare_name = (strchr(requested, '/') == NULL && strchr(requested, '\\') == NULL);
	if (!bare_name && BotLib_FileExists(requested))
	{
		BotLib_CopyPath(resolution->resolved_path,
			sizeof(resolution->resolved_path),
			requested);
		BotLib_CopyPath(resolution->source_path,
			sizeof(resolution->source_path),
			requested);
		return true;
	}

	char last_candidate[BOTLIB_ASSET_MAX_PATH];
	char resolved_path[BOTLIB_ASSET_MAX_PATH];
	char source_path[BOTLIB_ASSET_MAX_PATH];
	int32_t pak_entry_length = 0;
	last_candidate[0] = '\0';
	resolved_path[0] = '\0';
	source_path[0] = '\0';

	const char *roots[16];
	bool override_flags[sizeof(roots) / sizeof(roots[0])];
	char legacy_storage[4][BOTLIB_ASSET_MAX_PATH];
	size_t root_count = BotLib_BuildSearchRoots(roots,
		override_flags,
		sizeof(roots) / sizeof(roots[0]),
		legacy_storage,
		sizeof(legacy_storage) / sizeof(legacy_storage[0]));

	for (size_t i = 0; i < root_count; ++i)
	{
		if (!override_flags[i])
		{
			continue;
		}

		const char *root = roots[i];
		if (BotLib_IsStringEmpty(root))
		{
			continue;
		}

		if (BotLib_CheckFilesystemPaths(root,
			requested,
			bare_name,
			preferred_subdir,
			resolved_path,
			sizeof(resolved_path),
			last_candidate,
			sizeof(last_candidate)))
		{
			BotLib_CopyPath(resolution->resolved_path,
				sizeof(resolution->resolved_path),
				resolved_path);
			BotLib_CopyPath(resolution->source_path,
				sizeof(resolution->source_path),
				resolved_path);
			return true;
		}
	}

	for (size_t i = 0; i < root_count; ++i)
	{
		const char *root = roots[i];
		if (BotLib_IsStringEmpty(root))
		{
			continue;
		}

		if (!override_flags[i] &&
			BotLib_CheckFilesystemPaths(root,
				requested,
				bare_name,
				preferred_subdir,
				resolved_path,
				sizeof(resolved_path),
				last_candidate,
				sizeof(last_candidate)))
		{
			BotLib_CopyPath(resolution->resolved_path,
				sizeof(resolution->resolved_path),
				resolved_path);
			BotLib_CopyPath(resolution->source_path,
				sizeof(resolution->source_path),
				resolved_path);
			return true;
		}

		if (BotLib_CheckPakPaths(root,
			requested,
			bare_name,
			preferred_subdir,
			resolved_path,
			sizeof(resolved_path),
			source_path,
			sizeof(source_path),
			&pak_entry_length,
			last_candidate,
			sizeof(last_candidate)))
		{
			BotLib_CopyPath(resolution->resolved_path,
				sizeof(resolution->resolved_path),
				resolved_path);
			BotLib_CopyPath(resolution->source_path,
				sizeof(resolution->source_path),
				source_path);
			resolution->pak_entry_length = pak_entry_length;
			return true;
		}
	}

	BotLib_CopyPath(resolution->resolved_path,
		sizeof(resolution->resolved_path),
		last_candidate[0] != '\0' ? last_candidate : requested);
	return false;
}

/*
=============
BotLib_ResolveAssetPath

Preserves the original path-only interface over detailed asset resolution.
=============
*/
bool BotLib_ResolveAssetPath(const char *requested,
	const char *preferred_subdir,
	char *buffer,
	size_t size)
{
	if (buffer != NULL && size > 0)
	{
		buffer[0] = '\0';
	}

	botlib_asset_resolution_t resolution;
	bool resolved = BotLib_ResolveAssetPathDetailed(requested,
		preferred_subdir,
		&resolution);
	BotLib_CopyPath(buffer, size, resolution.resolved_path);
	return resolved;
}
