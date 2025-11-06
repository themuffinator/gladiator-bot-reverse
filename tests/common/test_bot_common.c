#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "botlib_common/l_assets.h"
#include "botlib_common/l_crc.h"
#include "botlib_common/l_libvar.h"
#include "botlib_common/l_struct.h"
#include "botlib_common/l_utils.h"
#include "botlib_precomp/l_precomp.h"
#include "shared/q_platform.h"

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#include <direct.h>
#include <io.h>
#endif

#ifdef _WIN32
#define test_mkdir(path) _mkdir(path)
#define test_rmdir(path) _rmdir(path)
#define test_unlink(path) _unlink(path)
#define test_unsetenv(name) _putenv_s(name, "")
#else
#define test_mkdir(path) mkdir(path, 0700)
#define test_rmdir(path) rmdir(path)
#define test_unlink(path) unlink(path)
#define test_unsetenv(name) unsetenv(name)
#endif

static void test_utils_initialisation_flags(void) {
    L_Utils_Shutdown();
    assert(!L_Utils_IsInitialised());
    assert(L_Utils_Init());
    assert(L_Utils_IsInitialised());
    L_Utils_Shutdown();
    assert(!L_Utils_IsInitialised());
}

static void test_struct_initialisation_flags(void) {
    L_Struct_Shutdown();
    assert(!L_Struct_IsInitialised());
    assert(L_Struct_Init());
    assert(L_Struct_IsInitialised());
    L_Struct_Shutdown();
    assert(!L_Struct_IsInitialised());
}

static void test_case_insensitive_compare_helpers(void) {
    assert(Q_stricmp("Alpha", "alpha") == 0);
    assert(Q_stricmp("Beta", "gamma") != 0);
    assert(Q_strnicmp("PrefixValue", "prefix-other", 6) == 0);
    assert(Q_strnicmp("PrefixValue", "prefix-other", 12) != 0);
}

static void test_crc_matches_reference(void) {
    const char payload[] = "gladiator";
    const uint16_t expected_crc = 0x40FEu;
    const uint16_t crc = CRC_ProcessString((const uint8_t *)payload, strlen(payload));
    assert(crc == expected_crc);

    uint16_t running = CRC_INIT_VALUE;
    for (size_t i = 0; i < strlen(payload); ++i) {
        CRC_ProcessByte(&running, (uint8_t)payload[i]);
    }
    assert(CRC_Value(running) == expected_crc);
}

typedef struct test_sample_s {
    int integer;
    float real;
    char name[MAX_STRINGFIELD];
    unsigned char flags[3];
} test_sample_t;

static const fielddef_t g_test_sample_fields[] = {
    {"integer", (int)offsetof(test_sample_t, integer), FT_INT, 0, 0.0f, 0.0f, NULL},
    {"real", (int)offsetof(test_sample_t, real), FT_FLOAT | FT_BOUNDED, 0, -10.0f, 10.0f, NULL},
    {"name", (int)offsetof(test_sample_t, name), FT_STRING, 0, 0.0f, 0.0f, NULL},
    {"flags", (int)offsetof(test_sample_t, flags), FT_CHAR | FT_UNSIGNED | FT_ARRAY, 3, 0.0f, 0.0f, NULL},
    {NULL, 0, 0, 0, 0.0f, 0.0f, NULL},
};

static const structdef_t g_test_sample_struct = {
    (int)sizeof(test_sample_t),
    g_test_sample_fields,
};

static void test_read_structure_parses_basic_types(void) {
    const char script[] = "{ integer 1337 real 3.75 name \"Unit\" flags { 1, 2, 4 } }";

    PC_InitLexer();
    pc_source_t *source = PC_LoadSourceMemory("test_struct", script, strlen(script));
    assert(source != NULL);

    test_sample_t sample;
    memset(&sample, 0, sizeof(sample));

    assert(ReadStructure(source, &g_test_sample_struct, &sample));
    assert(sample.integer == 1337);
    assert(fabsf(sample.real - 3.75f) < 0.0001f);
    assert(strcmp(sample.name, "Unit") == 0);
    assert(sample.flags[0] == 1);
    assert(sample.flags[1] == 2);
    assert(sample.flags[2] == 4);

    PC_FreeSource(source);
    PC_ShutdownLexer();
}

static void test_vector2angles_and_angle_helpers(void) {
    vec3_t forward = {0.0f, 1.0f, 0.0f};
    vec3_t angles;
    Vector2Angles(forward, angles);
    assert(fabsf(angles[PITCH] - 0.0f) < 0.001f);
    assert(fabsf(angles[YAW] - 90.0f) < 0.001f);

    vec3_t up = {0.0f, 0.0f, 1.0f};
    Vector2Angles(up, angles);
    assert(fabsf(angles[PITCH] + 90.0f) < 0.001f);
    assert(fabsf(angles[YAW] - 0.0f) < 0.001f);

    assert(fabsf(AngleNormalize360(370.0f) - 10.0f) < 0.001f);
    assert(fabsf(AngleNormalize180(200.0f) + 160.0f) < 0.001f);
    assert(fabsf(AngleDelta(10.0f, 350.0f) - 20.0f) < 0.001f);
}

static void test_path_helpers(void) {
    char buffer[64];
    strcpy(buffer, "base");
    AppendPathSeperator(buffer, sizeof(buffer));
    size_t len = strlen(buffer);
    assert(len > 0);
    assert(buffer[len - 1] == BOTLIB_PATH_SEPARATOR_CHAR);

    strcpy(buffer, "base\\game");
    ConvertPath(buffer);
    for (size_t i = 0; i < strlen(buffer); ++i) {
        if (buffer[i] == BOTLIB_PATH_SEPARATOR_CHAR) {
            continue;
        }
        assert(buffer[i] != '/' && buffer[i] != '\\');
    }

    char stripped[64];
    BotUtils_StripExtension("scripts/test.bot", stripped, sizeof(stripped));
    assert(strcmp(stripped, "scripts/test") == 0);

    assert(BotUtils_FileExists(__FILE__));
}

static bool test_create_temp_directory(char *buffer, size_t size, const char *prefix)
{
#ifdef _WIN32
    char temp_path[MAX_PATH];
    DWORD path_length = GetTempPathA((DWORD)sizeof(temp_path), temp_path);
    if (path_length == 0 || path_length >= sizeof(temp_path)) {
        return false;
    }

    char temp_file[MAX_PATH];
    if (GetTempFileNameA(temp_path, prefix != NULL ? prefix : "gla", 0, temp_file) == 0) {
        return false;
    }

    DeleteFileA(temp_file);
    if (_mkdir(temp_file) != 0) {
        return false;
    }

    int written = snprintf(buffer, size, "%s", temp_file);
    if (written < 0 || (size_t)written >= size) {
        _rmdir(temp_file);
        return false;
    }

    return true;
#else
    char template_path[PATH_MAX];
    int written = snprintf(template_path,
                           sizeof(template_path),
                           "/tmp/%sXXXXXX",
                           prefix != NULL ? prefix : "gla");
    if (written < 0 || (size_t)written >= sizeof(template_path)) {
        return false;
    }

    char *result = mkdtemp(template_path);
    if (result == NULL) {
        return false;
    }

    written = snprintf(buffer, size, "%s", result);
    if (written < 0 || (size_t)written >= size) {
        return false;
    }

    return true;
#endif
}

static bool test_create_asset_files(const char *root, const char *extra_file)
{
    if (root == NULL) {
        return false;
    }

    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/chars.h", root);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return false;
    }

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    fputs("// test asset\n", file);
    fclose(file);

    if (extra_file != NULL && extra_file[0] != '\0') {
        written = snprintf(path, sizeof(path), "%s/%s", root, extra_file);
        if (written < 0 || (size_t)written >= sizeof(path)) {
            return false;
        }

        FILE *extra = fopen(path, "wb");
        if (extra == NULL) {
            return false;
        }
        fputs("legacy asset\n", extra);
        fclose(extra);
    }

    return true;
}

static void test_cleanup_asset_files(const char *root, const char *extra_file)
{
    if (root == NULL) {
        return;
    }

    char path[PATH_MAX];
    if (extra_file != NULL && extra_file[0] != '\0') {
        int written = snprintf(path, sizeof(path), "%s/%s", root, extra_file);
        if (written >= 0 && (size_t)written < sizeof(path)) {
            test_unlink(path);
        }
    }

    int written = snprintf(path, sizeof(path), "%s/chars.h", root);
    if (written >= 0 && (size_t)written < sizeof(path)) {
        test_unlink(path);
    }
}

static bool test_write_pak(const char *pak_path, const char *entry_name, const char *contents)
{
    if (pak_path == NULL || entry_name == NULL || contents == NULL) {
        return false;
    }

    size_t name_length = strlen(entry_name);
    if (name_length == 0 || name_length >= 56) {
        return false;
    }

    size_t content_length = strlen(contents);
    if (content_length > (size_t)INT32_MAX) {
        return false;
    }

    FILE *stream = fopen(pak_path, "wb");
    if (stream == NULL) {
        return false;
    }

    bool success = false;
    const char magic[] = {'P', 'A', 'C', 'K'};
    int32_t zero = 0;

    if (fwrite(magic, 1, sizeof(magic), stream) != sizeof(magic)) {
        goto cleanup;
    }
    if (fwrite(&zero, sizeof(zero), 1, stream) != 1 || fwrite(&zero, sizeof(zero), 1, stream) != 1) {
        goto cleanup;
    }

    if (fwrite(contents, 1, content_length, stream) != content_length) {
        goto cleanup;
    }

    int32_t directory_offset = (int32_t)ftell(stream);
    int32_t directory_length = 64;
    int32_t entry_offset = 12;
    int32_t entry_length = (int32_t)content_length;

    unsigned char name_field[56] = {0};
    memcpy(name_field, entry_name, name_length);

    if (fwrite(name_field, 1, sizeof(name_field), stream) != sizeof(name_field)) {
        goto cleanup;
    }

    if (fwrite(&entry_offset, sizeof(entry_offset), 1, stream) != 1
        || fwrite(&entry_length, sizeof(entry_length), 1, stream) != 1) {
        goto cleanup;
    }

    if (fseek(stream, 4, SEEK_SET) != 0) {
        goto cleanup;
    }
    if (fwrite(&directory_offset, sizeof(directory_offset), 1, stream) != 1
        || fwrite(&directory_length, sizeof(directory_length), 1, stream) != 1) {
        goto cleanup;
    }

    success = true;

cleanup:
    fclose(stream);
    if (!success) {
        test_unlink(pak_path);
    }
    return success;
}

static void test_locate_asset_root_prefers_basedir(void)
{
    char legacy_root[PATH_MAX];
    char override_root[PATH_MAX];

    assert(test_create_temp_directory(legacy_root, sizeof(legacy_root), "gla"));
    assert(test_create_asset_files(legacy_root, NULL));

    assert(test_create_temp_directory(override_root, sizeof(override_root), "gla"));
    assert(test_create_asset_files(override_root, NULL));

    LibVar_Init();
    LibVarSet("basedir", legacy_root);
    LibVarSet("gamedir", "");
    LibVarSet("cddir", "");
    LibVarSet("gladiator_asset_dir", override_root);
    test_unsetenv("GLADIATOR_ASSET_DIR");

    char buffer[BOTLIB_ASSET_MAX_PATH];
    assert(BotLib_LocateAssetRoot(buffer, sizeof(buffer)));
    assert(strcmp(buffer, legacy_root) == 0);

    LibVar_Shutdown();

    test_cleanup_asset_files(legacy_root, NULL);
    test_cleanup_asset_files(override_root, NULL);
    test_rmdir(legacy_root);
    test_rmdir(override_root);
}

static void test_resolve_asset_path_prefers_cddir_over_new_knob(void)
{
    char basedir_override[PATH_MAX];
    char cddir_base[PATH_MAX];
    char cddir_game[PATH_MAX];
    char alternate_root[PATH_MAX];

    assert(test_create_temp_directory(basedir_override, sizeof(basedir_override), "gla"));
    assert(test_create_asset_files(basedir_override, NULL));

    assert(test_create_temp_directory(cddir_base, sizeof(cddir_base), "gla"));

    int written = snprintf(cddir_game, sizeof(cddir_game), "%s/game", cddir_base);
    assert(written >= 0 && (size_t)written < sizeof(cddir_game));
    assert(test_mkdir(cddir_game) == 0);
    assert(test_create_asset_files(cddir_game, "legacy_asset.dat"));

    assert(test_create_temp_directory(alternate_root, sizeof(alternate_root), "gla"));
    assert(test_create_asset_files(alternate_root, NULL));

    LibVar_Init();
    LibVarSet("basedir", basedir_override);
    LibVarSet("cddir", cddir_base);
    LibVarSet("gamedir", "game");
    LibVarSet("gladiator_asset_dir", alternate_root);
    test_unsetenv("GLADIATOR_ASSET_DIR");

    char buffer[BOTLIB_ASSET_MAX_PATH];
    bool resolved = BotLib_ResolveAssetPath("legacy_asset.dat", NULL, buffer, sizeof(buffer));
    assert(resolved);

    char expected[PATH_MAX];
    written = snprintf(expected, sizeof(expected), "%s/legacy_asset.dat", cddir_game);
    assert(written >= 0 && (size_t)written < sizeof(expected));
    assert(strcmp(buffer, expected) == 0);

    LibVar_Shutdown();

    test_cleanup_asset_files(basedir_override, NULL);
    test_cleanup_asset_files(cddir_game, "legacy_asset.dat");
    test_cleanup_asset_files(alternate_root, NULL);
    test_rmdir(cddir_game);
    test_rmdir(cddir_base);
    test_rmdir(basedir_override);
    test_rmdir(alternate_root);
}

static void test_resolve_asset_path_reads_from_pak_when_available(void)
{
    char basedir[PATH_MAX];
    char game_root[PATH_MAX];
    char pak_path[PATH_MAX];
    char cache_file[PATH_MAX];
    char cache_bots[PATH_MAX];
    char cache_pak[PATH_MAX];
    char cache_root[PATH_MAX];

    if (!test_create_temp_directory(basedir, sizeof(basedir), "gla")) {
        fprintf(stderr, "failed to create base directory\n");
        exit(EXIT_FAILURE);
    }

    int written = snprintf(game_root, sizeof(game_root), "%s/gladiator", basedir);
    if (written < 0 || (size_t)written >= sizeof(game_root)) {
        fprintf(stderr, "failed to compose game root path\n");
        exit(EXIT_FAILURE);
    }
    if (test_mkdir(game_root) != 0) {
        fprintf(stderr, "failed to create %s\n", game_root);
        exit(EXIT_FAILURE);
    }
    if (!test_create_asset_files(game_root, NULL)) {
        fprintf(stderr, "failed to create seed assets\n");
        exit(EXIT_FAILURE);
    }

    written = snprintf(pak_path, sizeof(pak_path), "%s/pak7.pak", game_root);
    if (written < 0 || (size_t)written >= sizeof(pak_path)) {
        fprintf(stderr, "failed to compose pak path\n");
        exit(EXIT_FAILURE);
    }
    if (!test_write_pak(pak_path, "bots/test.asset", "pak contents")) {
        fprintf(stderr, "failed to write pak %s\n", pak_path);
        exit(EXIT_FAILURE);
    }

    LibVar_Init();
    LibVarSet("basedir", basedir);
    LibVarSet("gamedir", "gladiator");
    LibVarSet("cddir", "");
    LibVarSet("gladiator_asset_dir", "");
    test_unsetenv("GLADIATOR_ASSET_DIR");

    char resolved[BOTLIB_ASSET_MAX_PATH];
    bool ok = BotLib_ResolveAssetPath("test.asset", "bots", resolved, sizeof(resolved));
    if (!ok) {
        fprintf(stderr, "expected packaged asset to resolve\n");
        LibVar_Shutdown();
        exit(EXIT_FAILURE);
    }

    FILE *reader = fopen(resolved, "rb");
    if (reader == NULL) {
        fprintf(stderr, "unable to open packaged asset %s\n", resolved);
        LibVar_Shutdown();
        exit(EXIT_FAILURE);
    }
    char buffer[32];
    size_t read = fread(buffer, 1, sizeof(buffer) - 1, reader);
    fclose(reader);
    buffer[read] = '\0';
    assert(strstr(buffer, "pak contents") != NULL);

    LibVar_Shutdown();

    written = snprintf(cache_file, sizeof(cache_file), "%s/.pak_cache/pak7/bots/test.asset", game_root);
    assert(written >= 0 && (size_t)written < sizeof(cache_file));
    test_unlink(cache_file);

    written = snprintf(cache_bots, sizeof(cache_bots), "%s/.pak_cache/pak7/bots", game_root);
    assert(written >= 0 && (size_t)written < sizeof(cache_bots));
    test_rmdir(cache_bots);

    written = snprintf(cache_pak, sizeof(cache_pak), "%s/.pak_cache/pak7", game_root);
    assert(written >= 0 && (size_t)written < sizeof(cache_pak));
    test_rmdir(cache_pak);

    written = snprintf(cache_root, sizeof(cache_root), "%s/.pak_cache", game_root);
    assert(written >= 0 && (size_t)written < sizeof(cache_root));
    test_rmdir(cache_root);

    test_cleanup_asset_files(game_root, NULL);
    test_unlink(pak_path);
    test_rmdir(game_root);
    test_rmdir(basedir);
}

static void test_resolve_asset_path_prefers_override_to_pak(void)
{
    char basedir[PATH_MAX];
    char game_root[PATH_MAX];
    char pak_path[PATH_MAX];
    char override_root[PATH_MAX];
    char override_bots[PATH_MAX];
    char override_file[PATH_MAX];

    if (!test_create_temp_directory(basedir, sizeof(basedir), "gla")) {
        fprintf(stderr, "failed to create base directory\n");
        exit(EXIT_FAILURE);
    }
    if (!test_create_temp_directory(override_root, sizeof(override_root), "gla")) {
        fprintf(stderr, "failed to create override directory\n");
        exit(EXIT_FAILURE);
    }

    int written = snprintf(game_root, sizeof(game_root), "%s/gladiator", basedir);
    if (written < 0 || (size_t)written >= sizeof(game_root)) {
        fprintf(stderr, "failed to compose game root path\n");
        exit(EXIT_FAILURE);
    }
    if (test_mkdir(game_root) != 0) {
        fprintf(stderr, "failed to create %s\n", game_root);
        exit(EXIT_FAILURE);
    }
    if (!test_create_asset_files(game_root, NULL)) {
        fprintf(stderr, "failed to create seed assets\n");
        exit(EXIT_FAILURE);
    }

    written = snprintf(pak_path, sizeof(pak_path), "%s/pak7.pak", game_root);
    if (written < 0 || (size_t)written >= sizeof(pak_path)) {
        fprintf(stderr, "failed to compose pak path\n");
        exit(EXIT_FAILURE);
    }
    if (!test_write_pak(pak_path, "bots/test.asset", "pak contents")) {
        fprintf(stderr, "failed to write pak %s\n", pak_path);
        exit(EXIT_FAILURE);
    }

    if (!test_create_asset_files(override_root, NULL)) {
        fprintf(stderr, "failed to seed override assets\n");
        exit(EXIT_FAILURE);
    }
    written = snprintf(override_bots, sizeof(override_bots), "%s/bots", override_root);
    if (written < 0 || (size_t)written >= sizeof(override_bots)) {
        fprintf(stderr, "failed to compose override bots path\n");
        exit(EXIT_FAILURE);
    }
    if (test_mkdir(override_bots) != 0) {
        fprintf(stderr, "failed to create override bots directory\n");
        exit(EXIT_FAILURE);
    }

    written = snprintf(override_file, sizeof(override_file), "%s/test.asset", override_bots);
    if (written < 0 || (size_t)written >= sizeof(override_file)) {
        fprintf(stderr, "failed to compose override asset path\n");
        exit(EXIT_FAILURE);
    }
    FILE *override_stream = fopen(override_file, "wb");
    assert(override_stream != NULL);
    fputs("override data", override_stream);
    fclose(override_stream);

    LibVar_Init();
    LibVarSet("basedir", basedir);
    LibVarSet("gamedir", "gladiator");
    LibVarSet("cddir", "");
    LibVarSet("gladiator_asset_dir", override_root);
    test_unsetenv("GLADIATOR_ASSET_DIR");

    char resolved[BOTLIB_ASSET_MAX_PATH];
    bool ok = BotLib_ResolveAssetPath("test.asset", "bots", resolved, sizeof(resolved));
    if (!ok) {
        fprintf(stderr, "expected override asset to resolve\n");
        LibVar_Shutdown();
        exit(EXIT_FAILURE);
    }
    assert(strcmp(resolved, override_file) == 0);

    FILE *reader = fopen(resolved, "rb");
    if (reader == NULL) {
        fprintf(stderr, "unable to open override asset %s\n", resolved);
        LibVar_Shutdown();
        exit(EXIT_FAILURE);
    }
    char buffer[32];
    size_t read = fread(buffer, 1, sizeof(buffer) - 1, reader);
    fclose(reader);
    buffer[read] = '\0';
    assert(strstr(buffer, "override data") != NULL);

    LibVar_Shutdown();

    test_cleanup_asset_files(game_root, NULL);
    test_cleanup_asset_files(override_root, NULL);
    test_unlink(override_file);
    test_rmdir(override_bots);
    test_unlink(pak_path);
    test_rmdir(game_root);
    test_rmdir(override_root);
    test_rmdir(basedir);
}

int main(void) {
    test_utils_initialisation_flags();
    test_struct_initialisation_flags();
    test_case_insensitive_compare_helpers();
    test_crc_matches_reference();
    test_read_structure_parses_basic_types();
    test_vector2angles_and_angle_helpers();
    test_path_helpers();
    test_locate_asset_root_prefers_basedir();
    test_resolve_asset_path_prefers_cddir_over_new_knob();
    test_resolve_asset_path_reads_from_pak_when_available();
    test_resolve_asset_path_prefers_override_to_pak();

    printf("bot_common_tests: all checks passed\n");
    return 0;
}
