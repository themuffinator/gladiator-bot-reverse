#include <stdarg.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <setjmp.h>
#include <cmocka.h>

#include <stdbool.h>

#ifndef cmocka_skip
#define cmocka_skip(...) skip()
#endif

#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#define getcwd _getcwd
#define unlink _unlink
#define rmdir _rmdir
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "botlib/aas/aas_map.h"
#include "botlib/aas/aas_local.h"
#include "botlib/aas/aas_sound.h"
#include "botlib/ai_move/mover_catalogue.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/interface/botlib_interface.h"
#include "q2bridge/aas_translation.h"
#include "q2bridge/botlib.h"
#include "q2bridge/bridge.h"
#include "q2bridge/bridge_config.h"

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined so regression tests can resolve asset paths."
#endif

#define TEST_BOTLIB_HEAP_SIZE (1u << 18)
#define TEST_PRINT_HISTORY_SIZE 8
#define TEST_PAK_HEADER_SIZE 12U
#define TEST_PAK_ENTRY_NAME_SIZE 56U
#define TEST_PAK_ENTRY_SIZE 64U
#define TEST_ZIP_HISTORY_SIZE 8U

typedef qboolean (*test_zip_extract_callback_t)(const char *archive_path,
	const char *member_name);

void AAS_SetZipExtractorForTests(test_zip_extract_callback_t extractor);

typedef struct test_pak_fixture_entry_s
{
	const char *name;
	const void *data;
	size_t size;
} test_pak_fixture_entry_t;

typedef struct aas_test_environment_s {
    char asset_root[PATH_MAX];
    char previous_cwd[PATH_MAX];
    bool have_previous_cwd;
    bool libvar_initialised;
    bool memory_initialised;
    bool import_table_set;
    bool bridge_config_initialised;
} aas_test_environment_t;

static int g_test_print_priority;
static int g_test_print_count;
static char g_test_print_message[1024];
static int g_test_print_priority_history[TEST_PRINT_HISTORY_SIZE];
static char g_test_print_message_history[TEST_PRINT_HISTORY_SIZE][1024];
static int g_test_zip_extract_mode;
static size_t g_test_zip_extract_count;
static char g_test_zip_archive_history[TEST_ZIP_HISTORY_SIZE][PATH_MAX];
static char g_test_zip_member_history[TEST_ZIP_HISTORY_SIZE][PATH_MAX];
static char g_test_zip_game_success_path[PATH_MAX];
static char g_test_zip_base_success_path[PATH_MAX];
static char g_test_zip_expected_member[PATH_MAX];
static const void *g_test_zip_member_data;
static size_t g_test_zip_member_size;

/*
=============
test_make_directory

Create a fixture directory with the platform's native directory API.
=============
*/
static int test_make_directory(const char *path)
{
#ifdef _WIN32
	return _mkdir(path);
#else
	return mkdir(path, 0700);
#endif
}

/*
=============
test_write_fixture

Write one transient loader fixture and report whether every byte was stored.
=============
*/
static qboolean test_write_fixture(const char *path,
	const void *data,
	size_t size)
{
	FILE *file = fopen(path, "wb");
	if (file == NULL)
	{
		return qfalse;
	}

	size_t written = 0;
	if (size > 0)
	{
		written = fwrite(data, 1, size, file);
	}
	int close_status = fclose(file);
	return (size == 0 || written == size) && close_status == 0;
}

/*
=============
test_write_little_uint32

Encode one PAK header or directory field without relying on host endianness.
=============
*/
static void test_write_little_uint32(unsigned char *destination,
	uint32_t value)
{
	destination[0] = (unsigned char)(value & 0xFFU);
	destination[1] = (unsigned char)((value >> 8) & 0xFFU);
	destination[2] = (unsigned char)((value >> 16) & 0xFFU);
	destination[3] = (unsigned char)((value >> 24) & 0xFFU);
}

/*
=============
test_write_pak_fixture

Write a transient Quake II PAK with standard offset/length directory entries.
=============
*/
static qboolean test_write_pak_fixture(const char *path,
	const test_pak_fixture_entry_t *entries,
	size_t entry_count)
{
	if (path == NULL || (entry_count > 0U && entries == NULL) ||
		entry_count > (size_t)INT32_MAX / TEST_PAK_ENTRY_SIZE)
	{
		return qfalse;
	}

	size_t data_size = 0U;
	for (size_t index = 0U; index < entry_count; ++index)
	{
		if (entries[index].name == NULL ||
			strlen(entries[index].name) > TEST_PAK_ENTRY_NAME_SIZE ||
			(entries[index].size > 0U && entries[index].data == NULL) ||
			entries[index].size > (size_t)INT32_MAX ||
			data_size > (size_t)INT32_MAX - entries[index].size)
		{
			return qfalse;
		}
		data_size += entries[index].size;
	}

	if (data_size > (size_t)INT32_MAX - TEST_PAK_HEADER_SIZE)
	{
		return qfalse;
	}
	size_t directory_offset = TEST_PAK_HEADER_SIZE + data_size;
	size_t directory_length = entry_count * TEST_PAK_ENTRY_SIZE;
	if (directory_offset > SIZE_MAX - directory_length)
	{
		return qfalse;
	}

	size_t pak_size = directory_offset + directory_length;
	unsigned char *pak = (unsigned char *)calloc(pak_size, 1U);
	if (pak == NULL)
	{
		return qfalse;
	}

	memcpy(pak, "PACK", 4U);
	test_write_little_uint32(pak + 4U, (uint32_t)directory_offset);
	test_write_little_uint32(pak + 8U, (uint32_t)directory_length);

	size_t data_offset = TEST_PAK_HEADER_SIZE;
	for (size_t index = 0U; index < entry_count; ++index)
	{
		if (entries[index].size > 0U)
		{
			memcpy(pak + data_offset,
				entries[index].data,
				entries[index].size);
		}

		unsigned char *directory_entry = pak + directory_offset +
			index * TEST_PAK_ENTRY_SIZE;
		memcpy(directory_entry,
			entries[index].name,
			strlen(entries[index].name));
		test_write_little_uint32(directory_entry + TEST_PAK_ENTRY_NAME_SIZE,
			(uint32_t)data_offset);
		test_write_little_uint32(directory_entry + TEST_PAK_ENTRY_NAME_SIZE + 4U,
			(uint32_t)entries[index].size);
		data_offset += entries[index].size;
	}

	qboolean written = test_write_fixture(path, pak, pak_size);
	free(pak);
	return written;
}

/*
=============
test_crc32

Compute an independent CRC-32 for archive-entry checksum assertions.
=============
*/
static uint32_t test_crc32(const void *data, size_t size)
{
	const unsigned char *bytes = (const unsigned char *)data;
	uint32_t crc = UINT32_MAX;
	for (size_t index = 0U; index < size; ++index)
	{
		crc ^= bytes[index];
		for (int bit = 0; bit < 8; ++bit)
		{
			uint32_t mask = 0U - (crc & 1U);
			crc = (crc >> 1) ^ (0xEDB88320U & mask);
		}
	}
	return ~crc;
}

/*
=============
test_extract_zip_member

Record retail ZIP probes and emulate UNZIP32 extraction into the current path.
=============
*/
static qboolean test_extract_zip_member(const char *archive_path,
	const char *member_name)
{
	if (archive_path == NULL || member_name == NULL)
	{
		return qfalse;
	}

	if (g_test_zip_extract_count < TEST_ZIP_HISTORY_SIZE)
	{
		snprintf(g_test_zip_archive_history[g_test_zip_extract_count],
			sizeof(g_test_zip_archive_history[g_test_zip_extract_count]),
			"%s",
			archive_path);
		snprintf(g_test_zip_member_history[g_test_zip_extract_count],
			sizeof(g_test_zip_member_history[g_test_zip_extract_count]),
			"%s",
			member_name);
	}
	g_test_zip_extract_count += 1U;

	if (g_test_zip_extract_mode == 0 ||
		strcmp(member_name, g_test_zip_expected_member) != 0 ||
		(strcmp(archive_path, g_test_zip_game_success_path) != 0 &&
		strcmp(archive_path, g_test_zip_base_success_path) != 0))
	{
		return qfalse;
	}

	return test_write_fixture(member_name,
		g_test_zip_member_data,
		g_test_zip_member_size);
}

/*
=============
test_reset_print_capture

Reset the exact diagnostic capture between malformed loader cases.
=============
*/
static void test_reset_print_capture(void)
{
	g_test_print_priority = 0;
	g_test_print_count = 0;
	g_test_print_message[0] = '\0';
	memset(g_test_print_priority_history,
		0,
		sizeof(g_test_print_priority_history));
	memset(g_test_print_message_history,
		0,
		sizeof(g_test_print_message_history));
}

/*
=============
test_capture_print

Capture the most recent botlib diagnostic for exact AAS contract assertions.
=============
*/
static void test_capture_print(int priority, const char *fmt, ...)
{
	g_test_print_priority = priority;

	va_list args;
	va_start(args, fmt);
	vsnprintf(g_test_print_message, sizeof(g_test_print_message), fmt, args);
	va_end(args);

	if (g_test_print_count < TEST_PRINT_HISTORY_SIZE)
	{
		g_test_print_priority_history[g_test_print_count] = priority;
		snprintf(g_test_print_message_history[g_test_print_count],
			sizeof(g_test_print_message_history[g_test_print_count]),
			"%s",
			g_test_print_message);
	}
	g_test_print_count += 1;
}

/*
 * Retail sub_10007d30 emits one PRT_MESSAGE notice per empty visibility
 * (10007fbb) and lighting (1000813c) lump, ahead of every other BSP
 * diagnostic, so every header-only fixture picks up two extra prints.
 */
#define TEST_BSP_EMPTY_LUMP_NOTICES 2

/*
=============
test_assert_empty_bsp_lump_notices

Assert retail's two empty-lump notices lead the capture for a header-only BSP.
=============
*/
static void test_assert_empty_bsp_lump_notices(void)
{
	assert_int_equal(g_test_print_priority_history[0], PRT_MESSAGE);
	assert_string_equal(g_test_print_message_history[0],
		"WARNGING: bsp has no visibility data\n");
	assert_int_equal(g_test_print_priority_history[1], PRT_MESSAGE);
	assert_string_equal(g_test_print_message_history[1],
		"WARNING: bsp has no light data\n");
}

static void test_capture_dprint(const char *fmt, ...)
{
    (void)fmt;
}

static int test_libvar_get(const char *var_name, char *value, size_t size)
{
    (void)var_name;
    if (value == NULL || size == 0) {
        return -1;
    }
    value[0] = '\0';
    return -1;
}

static int test_libvar_set(const char *var_name, const char *value)
{
    (void)var_name;
    (void)value;
    return 0;
}

static int g_test_point_contents;
static unsigned int g_test_point_contents_calls;
static qboolean g_test_gap_trace_enabled;
static qboolean g_test_grapple_trace_enabled;
static float g_test_source_ground_max_x;
static float g_test_destination_ground_min_x;
static float g_test_ground_height;
static float g_test_destination_ground_height;

/*
=============
test_point_contents

Return the deterministic BSP contents mask selected by an in-memory AAS test.
=============
*/
static int test_point_contents(vec3_t point)
{
	(void)point;
	g_test_point_contents_calls += 1U;
	return g_test_point_contents;
}

/*
=============
test_gap_trace

Provide two separated horizontal support regions for deterministic jump
prediction while leaving the intervening span empty.
=============
*/
static bsp_trace_t test_gap_trace(vec3_t start,
	vec3_t mins,
	vec3_t maxs,
	vec3_t end,
	int passent,
	int contentmask)
{
	(void)mins;
	(void)maxs;
	(void)passent;
	(void)contentmask;
	bsp_trace_t trace;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	trace.plane.normal[2] = 1.0f;
	if (g_test_grapple_trace_enabled && contentmask == MASK_SHOT &&
		end[0] > start[0])
	{
		trace.fraction = 0.02f;
		for (int axis = 0; axis < 3; ++axis)
		{
			trace.endpos[axis] = start[axis] +
				trace.fraction * (end[axis] - start[axis]);
		}
		return trace;
	}
	if (!g_test_gap_trace_enabled)
	{
		return trace;
	}

	float delta_z = end[2] - start[2];
	if (delta_z < 0.0f)
	{
		float bestfraction = 2.0f;
		float bestheight = 0.0f;
		if (start[2] >= g_test_ground_height &&
			end[2] <= g_test_ground_height)
		{
			float fraction = (g_test_ground_height - start[2]) / delta_z;
			float hit_x = start[0] + fraction * (end[0] - start[0]);
			if (hit_x <= g_test_source_ground_max_x)
			{
				bestfraction = fraction;
				bestheight = g_test_ground_height;
			}
		}
		if (start[2] >= g_test_destination_ground_height &&
			end[2] <= g_test_destination_ground_height)
		{
			float fraction = (g_test_destination_ground_height - start[2]) /
				delta_z;
			float hit_x = start[0] + fraction * (end[0] - start[0]);
			if (hit_x >= g_test_destination_ground_min_x &&
				fraction < bestfraction)
			{
				bestfraction = fraction;
				bestheight = g_test_destination_ground_height;
			}
		}
		if (bestfraction <= 1.0f)
		{
			trace.fraction = bestfraction;
			for (int axis = 0; axis < 3; ++axis)
			{
				trace.endpos[axis] = start[axis] +
					bestfraction * (end[axis] - start[axis]);
			}
			trace.endpos[2] = bestheight;
			return trace;
		}
	}

	if (end[0] >= g_test_destination_ground_min_x &&
		end[2] <= g_test_destination_ground_height)
	{
		float fraction = 0.0f;
		float delta_x = end[0] - start[0];
		if (start[0] < g_test_destination_ground_min_x && delta_x > 0.0f)
		{
			fraction = (g_test_destination_ground_min_x - start[0]) / delta_x;
		}
		trace.fraction = fraction;
		VectorCopy(end, trace.endpos);
		trace.endpos[0] = g_test_destination_ground_min_x;
		trace.endpos[2] = g_test_destination_ground_height;
	}
	return trace;
}

static const botlib_import_table_t g_test_imports = {
    .Print = test_capture_print,
    .DPrint = test_capture_dprint,
    .BotLibVarGet = test_libvar_get,
    .BotLibVarSet = test_libvar_set,
};

static bot_import_extended_t g_test_q2_imports = {
	.Trace = test_gap_trace,
	.PointContents = test_point_contents,
};

#ifdef _WIN32
static int test_setenv(const char *name, const char *value)
{
    return _putenv_s(name, value);
}

static void test_unsetenv(const char *name)
{
    _putenv_s(name, "");
}
#else
static int test_setenv(const char *name, const char *value)
{
    return setenv(name, value, 1);
}

static void test_unsetenv(const char *name)
{
    unsetenv(name);
}
#endif

static bool aas_environment_initialise(aas_test_environment_t *env)
{
    if (env == NULL) {
        return false;
    }

    const char *override_root = getenv("GLADIATOR_AAS_TEST_ASSET_DIR");
    if (override_root != NULL && override_root[0] != '\0') {
        int written = snprintf(env->asset_root, sizeof(env->asset_root), "%s", override_root);
        if (written <= 0 || (size_t)written >= sizeof(env->asset_root)) {
            return false;
        }
    } else {
        int written = snprintf(env->asset_root,
                               sizeof(env->asset_root),
                               "%s/dev_tools/assets",
                               PROJECT_SOURCE_DIR);
        if (written <= 0 || (size_t)written >= sizeof(env->asset_root)) {
            return false;
        }
    }

    char map_probe[PATH_MAX];
    snprintf(map_probe, sizeof(map_probe), "%s/maps/test_nav.bsp", env->asset_root);
    FILE *probe = fopen(map_probe, "rb");
    if (probe == NULL) {
        print_message("AAS regression harness skipped: missing test_nav.bsp in %s\n",
                      env->asset_root);
        return false;
    }
    fclose(probe);

    snprintf(map_probe, sizeof(map_probe), "%s/maps/test_nav.aas", env->asset_root);
    probe = fopen(map_probe, "rb");
    if (probe == NULL) {
        print_message("AAS regression harness skipped: missing test_nav.aas in %s\n",
                      env->asset_root);
        return false;
    }
    fclose(probe);

    if (getcwd(env->previous_cwd, sizeof(env->previous_cwd)) != NULL) {
        env->have_previous_cwd = true;
    }

    if (chdir(env->asset_root) != 0) {
        return false;
    }

    if (test_setenv("GLADIATOR_ASSET_DIR", env->asset_root) != 0) {
        return false;
    }

    return true;
}

static void aas_environment_cleanup(aas_test_environment_t *env)
{
    if (env == NULL) {
        return;
    }

    test_unsetenv("GLADIATOR_ASSET_DIR");

    if (env->have_previous_cwd) {
        chdir(env->previous_cwd);
        env->have_previous_cwd = false;
    }
}

/*
 * The seven map-fixture cases below need the externally supplied
 * test_nav.bsp/test_nav.aas pair, which the repository does not ship; the
 * suite's CMake entry turns the resulting exit code 7 into a CTest skip.
 *
 * This function reports the missing fixture with a plain non-zero return
 * rather than cmocka_skip(). cmocka's skip() sets its global skip flag and
 * longjmps; when that happens inside a setup the harness reports the case as
 * an error but leaves the flag raised, and the next test that longjmps - i.e.
 * the next real assertion failure anywhere in the suite - is then reported as
 * SKIPPED instead of FAILED. That silently hid live failures in
 * test_retail_entity_link_heaps_are_fixed_and_reused and
 * test_reachability_jump_generation_and_rejections. The exit code is the same
 * either way, so nothing about the CMake skip gate changes.
 */
static int aas_environment_setup(void **state)
{
    aas_test_environment_t *env = (aas_test_environment_t *)calloc(1, sizeof(aas_test_environment_t));
    if (env == NULL) {
        return -1;
    }

    if (!aas_environment_initialise(env)) {
        aas_environment_cleanup(env);
        free(env);
        return -1;
    }

    BotInterface_SetImportTable(&g_test_imports);
    env->import_table_set = true;

    LibVar_Init();
    env->libvar_initialised = true;

    if (!BridgeConfig_Init()) {
        aas_environment_cleanup(env);
        free(env);
        return -1;
    }
    env->bridge_config_initialised = true;

    if (!BotMemory_Init(TEST_BOTLIB_HEAP_SIZE)) {
        aas_environment_cleanup(env);
        free(env);
        return -1;
    }
    env->memory_initialised = true;

    *state = env;
    return 0;
}

static int aas_environment_teardown(void **state)
{
    aas_test_environment_t *env = (aas_test_environment_t *)(*state);
    if (env == NULL) {
        return 0;
    }

    AAS_Shutdown();

    if (env->bridge_config_initialised) {
        BridgeConfig_Shutdown();
        env->bridge_config_initialised = false;
    }

    if (env->libvar_initialised) {
        LibVar_Shutdown();
        env->libvar_initialised = false;
    }

    if (env->memory_initialised) {
        BotMemory_Shutdown();
        env->memory_initialised = false;
    }

    if (env->import_table_set) {
        BotInterface_SetImportTable(NULL);
        env->import_table_set = false;
    }

    aas_environment_cleanup(env);
    free(env);
    *state = NULL;
    return 0;
}

static void assert_entity_area_membership(int ent, const int *expected, size_t expected_count)
{
    assert_true(ent >= 0);
    assert_non_null(aasworld.entities);
    aas_entity_t *entity = &aasworld.entities[ent];

    int observed[16];
    size_t observed_count = 0;
    memset(observed, 0, sizeof(observed));

    for (aas_link_t *link = entity->areas; link != NULL; link = link->next_area) {
        if (observed_count < sizeof(observed) / sizeof(observed[0])) {
            observed[observed_count] = link->areanum;
        }
        observed_count++;
    }

    assert_int_equal(observed_count, expected_count);

    for (size_t i = 0; i < expected_count; ++i) {
        assert_non_null(expected);
        bool found = false;
        for (size_t j = 0; j < observed_count; ++j) {
            if (observed[j] == expected[i]) {
                found = true;
                break;
            }
        }
        assert_true(found);
    }
}

static void assert_area_entity_list_contains(int areanum, int ent)
{
    assert_true(areanum >= 0);
    assert_true(aasworld.areaEntityLists != NULL);
    assert_true((size_t)areanum < aasworld.areaEntityListCount);

    aas_link_t *head = aasworld.areaEntityLists[areanum];
    bool found = false;
    for (aas_link_t *link = head; link != NULL; link = link->next_ent) {
        if (link->entnum == ent) {
            found = true;
            break;
        }
    }
    assert_true(found);
}

static void assert_travel_time_for_area(int areanum, unsigned short expected_time)
{
    bool matched = false;
    for (int index = 0; index < aasworld.numReachability; ++index) {
        const aas_reachability_t *reach = &aasworld.reachability[index];
        if (reach->areanum == areanum) {
            assert_int_equal(reach->traveltime, expected_time);
            matched = true;
        }
    }
    assert_true(matched);
}

/*
=============
test_aas_in_pvs_decodes_retail_visibility_rows

Pins direct dvis bit lookup, compressed zero runs, malformed zero repeats,
and the retail all-visible fallback when no visibility lump is available.
=============
*/
static void test_aas_in_pvs_decodes_retail_visibility_rows(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	test_reset_print_capture();
	BotInterface_SetImportTable(&g_test_imports);

	aas_plane_t planes[1] = {0};
	aas_bspnode_t nodes[1] = {0};
	aas_bspleaf_t leaves[2] = {0};
	unsigned char visibility[22] = {0};
	int32_t num_clusters = 2;
	int32_t data_offset = 20;

	planes[0].normal[0] = 1.0f;
	nodes[0].planenum = 0;
	nodes[0].children[0] = -1;
	nodes[0].children[1] = -2;
	leaves[0].cluster = 0;
	leaves[1].cluster = 1;
	memcpy(visibility, &num_clusters, sizeof(num_clusters));
	memcpy(visibility + 4, &data_offset, sizeof(data_offset));
	memcpy(visibility + 8, &data_offset, sizeof(data_offset));
	memcpy(visibility + 12, &data_offset, sizeof(data_offset));
	memcpy(visibility + 16, &data_offset, sizeof(data_offset));

	aasworld.numBspPlanes = 1;
	aasworld.bspPlanes = planes;
	aasworld.numBspNodes = 1;
	aasworld.bspNodes = nodes;
	aasworld.numBspLeaves = 2;
	aasworld.bspLeaves = leaves;
	aasworld.numBspVisibilityClusters = num_clusters;
	aasworld.bspVisibilitySize = sizeof(visibility);
	aasworld.bspVisibility = visibility;

	vec3_t cluster_zero = {1.0f, 0.0f, 0.0f};
	vec3_t cluster_one = {-1.0f, 0.0f, 0.0f};
	visibility[20] = 1U << 1;
	assert_true(AAS_InPVS(cluster_zero, cluster_one));

	visibility[20] = 0;
	visibility[21] = 1;
	assert_false(AAS_InPVS(cluster_zero, cluster_one));
	assert_int_equal(g_test_print_count, 0);

	visibility[21] = 0;
	assert_false(AAS_InPVS(cluster_zero, cluster_one));
	assert_int_equal(g_test_print_count, 1);
	assert_int_equal(g_test_print_priority, PRT_ERROR);
	assert_string_equal(g_test_print_message,
		"AAS_DecompressVis: 0 repeat\n");

	aasworld.bspVisibility = NULL;
	aasworld.bspVisibilitySize = 0U;
	assert_true(AAS_InPVS(cluster_zero, cluster_one));

	memset(&aasworld, 0, sizeof(aasworld));
	BotInterface_SetImportTable(NULL);
}

/*
=============
test_aas_null_map_refreshes_assets_without_world_reset

Proves NULL map loads refresh string indexes without reading or clearing a world.
=============
*/
static void test_aas_null_map_refreshes_assets_without_world_reset(void **state)
{
	(void)state;
	AAS_SoundSubsystem_ClearMapAssets();
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t retained_areas[2] = {0};
	aasworld.loaded = qtrue;
	aasworld.initialized = qtrue;
	aasworld.time = 37.5f;
	aasworld.numAreas = 2;
	aasworld.areas = retained_areas;
	snprintf(aasworld.mapName, sizeof(aasworld.mapName), "retained_world");

	char *models[] = {"maps/retained.bsp", "*1"};
	char *sounds[] = {"sound/old.wav", "sound/refreshed.wav"};
	char *images[] = {"pics/retained.pcx", "pics/retained_second.pcx"};

	int status = AAS_LoadMap(NULL, 2, models, 2, sounds, 2, images);
	assert_int_equal(status, BLERR_NOERROR);
	assert_true(aasworld.loaded);
	assert_true(aasworld.initialized);
	assert_float_equal(aasworld.time, 37.5f, 0.0001f);
	assert_int_equal(aasworld.numAreas, 2);
	assert_ptr_equal(aasworld.areas, retained_areas);
	assert_string_equal(aasworld.mapName, "retained_world");
	assert_string_equal(AAS_SoundSubsystem_AssetName(0), "sound/old.wav");
	assert_string_equal(AAS_SoundSubsystem_AssetName(1), "sound/refreshed.wav");
	assert_string_equal(AAS_ModelFromIndex(1), "*1");
	assert_int_equal(IndexFromModel("*1"), 1);
	assert_string_equal(AAS_SoundFromIndex(1), "sound/refreshed.wav");
	assert_int_equal(AAS_IndexFromSound("SOUND/REFRESHED.WAV"), 1);
	assert_string_equal(AAS_ImageFromIndex(0), "pics/retained.pcx");
	assert_int_equal(AAS_IndexFromImage("PICS/RETAINED_SECOND.PCX"), 1);

	char *replacement_sounds[] = {"sound/replacement.wav"};
	status = AAS_LoadMap(NULL, 0, NULL, 1, replacement_sounds, 0, NULL);
	assert_int_equal(status, BLERR_NOERROR);
	assert_true(aasworld.loaded);
	assert_ptr_equal(aasworld.areas, retained_areas);
	assert_string_equal(aasworld.mapName, "retained_world");
	assert_string_equal(AAS_SoundSubsystem_AssetName(0), "sound/replacement.wav");
	assert_null(AAS_SoundSubsystem_AssetName(1));
	assert_string_equal(AAS_ModelFromIndex(1), "*1");
	assert_string_equal(AAS_SoundFromIndex(0), "sound/old.wav");
	assert_string_equal(AAS_ImageFromIndex(0), "pics/retained.pcx");

	AAS_SoundSubsystem_ClearMapAssets();
	aasworld.areas = NULL;
	memset(&aasworld, 0, sizeof(aasworld));
	AAS_Shutdown();
}

/*
=============
test_aas_empty_map_reports_retail_missing_bsp

Pins the retail filesystem-discovery failure used even for an empty map name.
=============
*/
static void test_aas_empty_map_reports_retail_missing_bsp(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	test_reset_print_capture();
	BotInterface_SetImportTable(&g_test_imports);

	int status = AAS_LoadMap("", 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_NOBSPFILE);
	assert_int_equal(g_test_print_count, 1);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
#ifdef _WIN32
	assert_string_equal(g_test_print_message,
		"couldn't find the bsp file maps\\.bsp\n");
#else
	assert_string_equal(g_test_print_message,
		"couldn't find the bsp file maps/.bsp\n");
#endif
	assert_false(aasworld.loaded);

	AAS_Shutdown();
	BotInterface_SetImportTable(NULL);
}

/*
=============
test_aas_loader_preserves_retail_header_error_contracts

Exercises exact fatal diagnostics and status codes for malformed BSP/AAS files.
=============
*/
static void test_aas_loader_preserves_retail_header_error_contracts(void **state)
{
	(void)state;
	const char *map_name = "__gladiator_loader_contract";
	const char *bsp_path = "maps/__gladiator_loader_contract.bsp";
	const char *root_aas_path = "__gladiator_loader_contract.aas";
	const char *aas_path = "maps/__gladiator_loader_contract.aas";
#ifdef _WIN32
	const char *reported_bsp_path =
		"maps\\__gladiator_loader_contract.bsp";
	const char *reported_aas_path =
		"maps\\__gladiator_loader_contract.aas";
#else
	const char *reported_bsp_path = bsp_path;
	const char *reported_aas_path = aas_path;
#endif

	unlink(bsp_path);
	unlink(root_aas_path);
	unlink(aas_path);
	errno = 0;
	int directory_status = test_make_directory("maps");
	qboolean remove_directory = directory_status == 0;
	assert_true(remove_directory || errno == EEXIST);
	BotInterface_SetImportTable(&g_test_imports);
	memset(&aasworld, 0, sizeof(aasworld));

	const unsigned char truncated_bsp = 0;
	assert_true(test_write_fixture(bsp_path, &truncated_bsp, 1));
	test_reset_print_capture();
	int status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_CANNOTREADBSPHEADER);
	assert_int_equal(g_test_print_count, 1);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	char expected[PATH_MAX];
	snprintf(expected,
		sizeof(expected),
		"can't read header of bsp file %s\n",
		reported_bsp_path);
	assert_string_equal(g_test_print_message, expected);

	q2_bsp_header_t bsp_header;
	memset(&bsp_header, 0, sizeof(bsp_header));
	assert_true(test_write_fixture(bsp_path, &bsp_header, sizeof(bsp_header)));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_WRONGBSPFILEID);
	assert_int_equal(g_test_print_count, 1);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	snprintf(expected,
		sizeof(expected),
		"%s is not an BSP file\n",
		reported_bsp_path);
	assert_string_equal(g_test_print_message, expected);

	bsp_header.ident = Q2_BSP_IDENT;
	bsp_header.version = Q2_BSP_VERSION - 1;
	assert_true(test_write_fixture(bsp_path, &bsp_header, sizeof(bsp_header)));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_WRONGBSPFILEVERSION);
	assert_int_equal(g_test_print_count, 1);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	snprintf(expected,
		sizeof(expected),
		"bsp file %s is version %i, not %i\n",
		reported_bsp_path,
		Q2_BSP_VERSION - 1,
		Q2_BSP_VERSION);
	assert_string_equal(g_test_print_message, expected);

	bsp_header.version = Q2_BSP_VERSION;
	bsp_header.lumps[Q2_BSP_LUMP_ENTITIES].offset = -1;
	bsp_header.lumps[Q2_BSP_LUMP_ENTITIES].length = 1;
	assert_true(test_write_fixture(bsp_path, &bsp_header, sizeof(bsp_header)));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_CANNOTREADBSPLUMP);
	assert_int_equal(g_test_print_count, 3);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	assert_string_equal(g_test_print_message,
		"can't seek to bsp lump entity\n");
	/*
	 * Retail sub_10007d30 reports the two lumps that may legitimately be empty
	 * before any other BSP diagnostic: 10007fbb prints the visibility notice
	 * (with the shipped "WARNGING" typo at 0x1005ac70) and 1000813c the light
	 * notice, both at PRT_MESSAGE.
	 */
	test_assert_empty_bsp_lump_notices();

	/* Both notices are keyed off a zero lump length only, so a populated
	   visibility or lighting lump suppresses them. */
	unsigned char bsp_with_vis_light[sizeof(bsp_header) + 8U];
	bsp_header.lumps[Q2_BSP_LUMP_VISIBILITY].offset =
		(int32_t)sizeof(bsp_header);
	bsp_header.lumps[Q2_BSP_LUMP_VISIBILITY].length = 4;
	bsp_header.lumps[Q2_BSP_LUMP_LIGHTING].offset =
		(int32_t)sizeof(bsp_header) + 4;
	bsp_header.lumps[Q2_BSP_LUMP_LIGHTING].length = 4;
	memcpy(bsp_with_vis_light, &bsp_header, sizeof(bsp_header));
	memset(bsp_with_vis_light + sizeof(bsp_header), 0, 8U);
	assert_true(test_write_fixture(bsp_path,
		bsp_with_vis_light,
		sizeof(bsp_with_vis_light)));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_CANNOTREADBSPLUMP);
	assert_int_equal(g_test_print_count, 1);
	assert_string_equal(g_test_print_message,
		"can't seek to bsp lump entity\n");
	memset(&bsp_header.lumps[Q2_BSP_LUMP_VISIBILITY],
		0,
		sizeof(bsp_header.lumps[Q2_BSP_LUMP_VISIBILITY]));
	memset(&bsp_header.lumps[Q2_BSP_LUMP_LIGHTING],
		0,
		sizeof(bsp_header.lumps[Q2_BSP_LUMP_LIGHTING]));

	bsp_header.lumps[Q2_BSP_LUMP_ENTITIES].offset =
		(int32_t)sizeof(bsp_header);
	assert_true(test_write_fixture(bsp_path, &bsp_header, sizeof(bsp_header)));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_CANNOTREADBSPLUMP);
	assert_int_equal(g_test_print_count, 3);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	assert_string_equal(g_test_print_message,
		"can't read bsp lump entity\n");

	memset(&bsp_header.lumps[Q2_BSP_LUMP_ENTITIES],
		0,
		sizeof(bsp_header.lumps[Q2_BSP_LUMP_ENTITIES]));
	assert_true(test_write_fixture(bsp_path, &bsp_header, sizeof(bsp_header)));
	unlink(aas_path);
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_NOAASFILE);
	assert_int_equal(g_test_print_count, 4);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	assert_string_equal(g_test_print_message, "no AAS file available\n");

	assert_true(test_write_fixture(aas_path, NULL, 0));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_CANNOTREADAASHEADER);
	assert_int_equal(g_test_print_count, 4);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	snprintf(expected,
		sizeof(expected),
		"can't read header of file %s\n",
		reported_aas_path);
	assert_string_equal(g_test_print_message, expected);

	q2_aas_header_t aas_header;
	memset(&aas_header, 0, sizeof(aas_header));
	assert_true(test_write_fixture(aas_path, &aas_header, sizeof(aas_header)));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_WRONGAASFILEID);
	assert_int_equal(g_test_print_count, 4);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	snprintf(expected,
		sizeof(expected),
		"%s is not an AAS file\n",
		reported_aas_path);
	assert_string_equal(g_test_print_message, expected);

	aas_header.ident = Q2_AAS_IDENT;
	aas_header.version = Q2_AAS_VERSION + 1;
	assert_true(test_write_fixture(aas_path, &aas_header, sizeof(aas_header)));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_WRONGAASFILEVERSION);
	assert_int_equal(g_test_print_count, 4);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	snprintf(expected,
		sizeof(expected),
		"aas file %s is version %i, not %i\n",
		reported_aas_path,
		Q2_AAS_VERSION + 1,
		Q2_AAS_VERSION);
	assert_string_equal(g_test_print_message, expected);

	aas_header.version = Q2_AAS_VERSION;
	aas_header.lumps[Q2_AAS_LUMP_BBOXES].offset =
		(int32_t)sizeof(aas_header);
	aas_header.lumps[Q2_AAS_LUMP_BBOXES].length = 1;
	assert_true(test_write_fixture(aas_path, &aas_header, sizeof(aas_header)));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_CANNOTREADAASLUMP);
	assert_int_equal(g_test_print_count, 4);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	assert_string_equal(g_test_print_message, "can't read aas lump\n");

	aas_header.lumps[Q2_AAS_LUMP_BBOXES].offset = -1;
	assert_true(test_write_fixture(aas_path, &aas_header, sizeof(aas_header)));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_CANNOTREADAASLUMP);
	assert_int_equal(g_test_print_count, 4);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	assert_string_equal(g_test_print_message, "can't seek to aas lump\n");

	unsigned char aas_with_odd_bbox[sizeof(aas_header) + 1U];
	aas_header.lumps[Q2_AAS_LUMP_BBOXES].offset =
		(int32_t)sizeof(aas_header);
	aas_header.lumps[Q2_AAS_LUMP_BBOXES].length = 1;
	memcpy(aas_with_odd_bbox, &aas_header, sizeof(aas_header));
	aas_with_odd_bbox[sizeof(aas_header)] = 0;
	assert_true(test_write_fixture(aas_path,
		aas_with_odd_bbox,
		sizeof(aas_with_odd_bbox)));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(g_test_print_count, 4);
	assert_int_equal(g_test_print_priority, PRT_MESSAGE);
	snprintf(expected, sizeof(expected), "loaded %s\n", reported_aas_path);
	assert_string_equal(g_test_print_message, expected);
	assert_true(aasworld.loaded);
	AAS_Shutdown();

	memset(&aas_header.lumps, 0, sizeof(aas_header.lumps));
	assert_true(test_write_fixture(aas_path, &aas_header, sizeof(aas_header)));
	bsp_header.lumps[Q2_BSP_LUMP_PLANES].offset = -1;
	bsp_header.lumps[Q2_BSP_LUMP_PLANES].length =
		(int32_t)sizeof(aas_plane_t);
	assert_true(test_write_fixture(bsp_path, &bsp_header, sizeof(bsp_header)));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_CANNOTREADBSPLUMP);
	assert_int_equal(g_test_print_count, 4);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	assert_string_equal(g_test_print_message,
		"can't seek to bsp lump planes\n");

	bsp_header.lumps[Q2_BSP_LUMP_PLANES].offset =
		(int32_t)sizeof(bsp_header);
	bsp_header.lumps[Q2_BSP_LUMP_PLANES].length =
		(int32_t)sizeof(aas_plane_t);
	assert_true(test_write_fixture(bsp_path, &bsp_header, sizeof(bsp_header)));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_CANNOTREADBSPLUMP);
	assert_int_equal(g_test_print_count, 4);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	assert_string_equal(g_test_print_message,
		"can't read bsp lump planes\n");

	unsigned char bsp_with_odd_plane[sizeof(bsp_header) + 1U];
	bsp_header.lumps[Q2_BSP_LUMP_PLANES].length = 1;
	memcpy(bsp_with_odd_plane, &bsp_header, sizeof(bsp_header));
	bsp_with_odd_plane[sizeof(bsp_header)] = 0;
	assert_true(test_write_fixture(bsp_path,
		bsp_with_odd_plane,
		sizeof(bsp_with_odd_plane)));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_CANNOTREADBSPLUMP);
	assert_int_equal(g_test_print_count, 4);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	assert_string_equal(g_test_print_message,
		"odd planes bsp lump size\n");

	unlink(aas_path);
	unlink(root_aas_path);
	unlink(bsp_path);
	AAS_Shutdown();
	BotInterface_SetImportTable(NULL);
	if (remove_directory)
	{
		assert_int_equal(rmdir("maps"), 0);
	}
}

/*
=============
test_bsp_texinfo_payload_load_and_retail_trace_boundary

Loads a synthetic BSP texinfo/brush graph and pins the retail internal-trace
boundary: the hit retains its brush-side index and expanded distance, while
the surface payload remains zero rather than being synthesized from texinfo.
=============
*/
static void test_bsp_texinfo_payload_load_and_retail_trace_boundary(void **state)
{
	(void)state;
	const char *map_name = "__gladiator_bsp_texinfo";
	const char *bsp_path = "maps/__gladiator_bsp_texinfo.bsp";
	const char *root_aas_path = "__gladiator_bsp_texinfo.aas";
	const char *aas_path = "maps/__gladiator_bsp_texinfo.aas";

	unlink(bsp_path);
	unlink(root_aas_path);
	unlink(aas_path);
	errno = 0;
	int directory_status = test_make_directory("maps");
	qboolean remove_directory = directory_status == 0;
	assert_true(remove_directory || errno == EEXIST);

	assert_int_equal(sizeof(aas_bsptexinfo_t), 0x4c);
	assert_int_equal(offsetof(aas_bsptexinfo_t, flags), 0x20);
	assert_int_equal(offsetof(aas_bsptexinfo_t, value), 0x24);
	assert_int_equal(offsetof(aas_bsptexinfo_t, texture), 0x28);
	assert_int_equal(offsetof(aas_bsptexinfo_t, nexttexinfo), 0x48);

	aas_plane_t bsp_planes[6];
	memset(bsp_planes, 0, sizeof(bsp_planes));
	VectorSet(bsp_planes[0].normal, -1.0f, 0.0f, 0.0f);
	bsp_planes[0].dist = 0.0f;
	bsp_planes[0].type = 0;
	VectorSet(bsp_planes[1].normal, 1.0f, 0.0f, 0.0f);
	bsp_planes[1].dist = 10.0f;
	bsp_planes[1].type = 0;
	VectorSet(bsp_planes[2].normal, 0.0f, -1.0f, 0.0f);
	bsp_planes[2].dist = 16.0f;
	bsp_planes[2].type = 1;
	VectorSet(bsp_planes[3].normal, 0.0f, 1.0f, 0.0f);
	bsp_planes[3].dist = 16.0f;
	bsp_planes[3].type = 1;
	VectorSet(bsp_planes[4].normal, 0.0f, 0.0f, -1.0f);
	bsp_planes[4].dist = 16.0f;
	bsp_planes[4].type = 2;
	VectorSet(bsp_planes[5].normal, 0.0f, 0.0f, 1.0f);
	bsp_planes[5].dist = 16.0f;
	bsp_planes[5].type = 2;

	aas_bsptexinfo_t bsp_texinfo[2];
	memset(bsp_texinfo, 0, sizeof(bsp_texinfo));
	for (int axis = 0; axis < 2; ++axis)
	{
		for (int component = 0; component < 4; ++component)
		{
			bsp_texinfo[1].vecs[axis][component] =
				(float)(axis * 10 + component) + 0.25f;
		}
	}
	bsp_texinfo[0].flags = 1;
	bsp_texinfo[0].value = 11;
	strncpy(bsp_texinfo[0].texture,
		"textures/default",
		sizeof(bsp_texinfo[0].texture) - 1U);
	bsp_texinfo[0].nexttexinfo = 1;
	bsp_texinfo[1].flags = 4;
	bsp_texinfo[1].value = 321;
	strncpy(bsp_texinfo[1].texture,
		"textures/sky_payload",
		sizeof(bsp_texinfo[1].texture) - 1U);
	bsp_texinfo[1].nexttexinfo = -1;

	aas_bspleaf_t bsp_leaf;
	memset(&bsp_leaf, 0, sizeof(bsp_leaf));
	bsp_leaf.contents = CONTENTS_SOLID;
	bsp_leaf.cluster = -1;
	bsp_leaf.firstleafbrush = 0;
	bsp_leaf.numleafbrushes = 1;
	unsigned short bsp_leafbrush = 0;

	aas_bspmodel_t bsp_model;
	memset(&bsp_model, 0, sizeof(bsp_model));
	VectorSet(bsp_model.mins, 0.0f, -16.0f, -16.0f);
	VectorSet(bsp_model.maxs, 10.0f, 16.0f, 16.0f);
	bsp_model.headnode = -1;

	aas_bspbrushside_t bsp_brushsides[6];
	memset(bsp_brushsides, 0, sizeof(bsp_brushsides));
	for (int index = 0; index < 6; ++index)
	{
		bsp_brushsides[index].planenum = (unsigned short)index;
		bsp_brushsides[index].texinfo = 0;
	}
	bsp_brushsides[0].texinfo = 1;

	aas_bspbrush_t bsp_brush;
	memset(&bsp_brush, 0, sizeof(bsp_brush));
	bsp_brush.firstside = 0;
	bsp_brush.numsides = 6;
	bsp_brush.contents = CONTENTS_SOLID;

	q2_bsp_header_t bsp_header;
	memset(&bsp_header, 0, sizeof(bsp_header));
	bsp_header.ident = Q2_BSP_IDENT;
	bsp_header.version = Q2_BSP_VERSION;
	static const char entity_data[] =
		"{\n\"classname\" \"worldspawn\"\n}\n";
	unsigned char bsp_data[sizeof(bsp_header) + sizeof(entity_data) - 1U +
		sizeof(bsp_planes) +
		sizeof(bsp_texinfo) + sizeof(bsp_leaf) + sizeof(bsp_leafbrush) +
		sizeof(bsp_model) + sizeof(bsp_brushsides) + sizeof(bsp_brush)];
	size_t bsp_offset = sizeof(bsp_header);

	bsp_header.lumps[Q2_BSP_LUMP_ENTITIES].offset = (int32_t)bsp_offset;
	bsp_header.lumps[Q2_BSP_LUMP_ENTITIES].length =
		(int32_t)(sizeof(entity_data) - 1U);
	memcpy(bsp_data + bsp_offset, entity_data, sizeof(entity_data) - 1U);
	bsp_offset += sizeof(entity_data) - 1U;
	bsp_header.lumps[Q2_BSP_LUMP_PLANES].offset = (int32_t)bsp_offset;
	bsp_header.lumps[Q2_BSP_LUMP_PLANES].length = (int32_t)sizeof(bsp_planes);
	memcpy(bsp_data + bsp_offset, bsp_planes, sizeof(bsp_planes));
	bsp_offset += sizeof(bsp_planes);
	bsp_header.lumps[Q2_BSP_LUMP_TEXINFO].offset = (int32_t)bsp_offset;
	bsp_header.lumps[Q2_BSP_LUMP_TEXINFO].length = (int32_t)sizeof(bsp_texinfo);
	memcpy(bsp_data + bsp_offset, bsp_texinfo, sizeof(bsp_texinfo));
	bsp_offset += sizeof(bsp_texinfo);
	bsp_header.lumps[Q2_BSP_LUMP_LEAFS].offset = (int32_t)bsp_offset;
	bsp_header.lumps[Q2_BSP_LUMP_LEAFS].length = (int32_t)sizeof(bsp_leaf);
	memcpy(bsp_data + bsp_offset, &bsp_leaf, sizeof(bsp_leaf));
	bsp_offset += sizeof(bsp_leaf);
	bsp_header.lumps[Q2_BSP_LUMP_LEAFBRUSHES].offset = (int32_t)bsp_offset;
	bsp_header.lumps[Q2_BSP_LUMP_LEAFBRUSHES].length =
		(int32_t)sizeof(bsp_leafbrush);
	memcpy(bsp_data + bsp_offset, &bsp_leafbrush, sizeof(bsp_leafbrush));
	bsp_offset += sizeof(bsp_leafbrush);
	bsp_header.lumps[Q2_BSP_LUMP_MODELS].offset = (int32_t)bsp_offset;
	bsp_header.lumps[Q2_BSP_LUMP_MODELS].length = (int32_t)sizeof(bsp_model);
	memcpy(bsp_data + bsp_offset, &bsp_model, sizeof(bsp_model));
	bsp_offset += sizeof(bsp_model);
	bsp_header.lumps[Q2_BSP_LUMP_BRUSHSIDES].offset = (int32_t)bsp_offset;
	bsp_header.lumps[Q2_BSP_LUMP_BRUSHSIDES].length =
		(int32_t)sizeof(bsp_brushsides);
	memcpy(bsp_data + bsp_offset, bsp_brushsides, sizeof(bsp_brushsides));
	bsp_offset += sizeof(bsp_brushsides);
	bsp_header.lumps[Q2_BSP_LUMP_BRUSHES].offset = (int32_t)bsp_offset;
	bsp_header.lumps[Q2_BSP_LUMP_BRUSHES].length = (int32_t)sizeof(bsp_brush);
	memcpy(bsp_data + bsp_offset, &bsp_brush, sizeof(bsp_brush));
	bsp_offset += sizeof(bsp_brush);
	assert_int_equal(bsp_offset, sizeof(bsp_data));
	memcpy(bsp_data, &bsp_header, sizeof(bsp_header));
	assert_true(test_write_fixture(bsp_path, bsp_data, sizeof(bsp_data)));

	q2_aas_header_t aas_header;
	memset(&aas_header, 0, sizeof(aas_header));
	aas_header.ident = Q2_AAS_IDENT;
	aas_header.version = Q2_AAS_VERSION;
	assert_true(test_write_fixture(aas_path, &aas_header, sizeof(aas_header)));

	BotInterface_SetImportTable(&g_test_imports);
	memset(&aasworld, 0, sizeof(aasworld));
	test_reset_print_capture();
	size_t tracked_before = BotMemory_TotalAllocated();
	int status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_NOERROR);
	assert_true(aasworld.loaded);
	assert_true(BotMemory_TotalAllocated() > tracked_before);
	assert_int_equal(aasworld.bspEntityChecksum, 0xd218);
	assert_int_equal(aasworld.numBspTexInfo, 2);
	assert_non_null(aasworld.bspTexInfo);
	assert_float_equal(aasworld.bspTexInfo[1].vecs[0][0], 0.25f, 0.00001f);
	assert_float_equal(aasworld.bspTexInfo[1].vecs[1][3], 13.25f, 0.00001f);
	assert_int_equal(aasworld.bspTexInfo[1].flags, 4);
	assert_int_equal(aasworld.bspTexInfo[1].value, 321);
	assert_string_equal(aasworld.bspTexInfo[1].texture,
		"textures/sky_payload");
	assert_int_equal(aasworld.bspTexInfo[1].nexttexinfo, -1);
	assert_int_equal(aasworld.numBspBrushSides, 6);
	assert_int_equal(aasworld.bspBrushSides[0].texinfo, 1);

	vec3_t trace_start = {-10.0f, 0.0f, 0.0f};
	vec3_t trace_mins = {-1.0f, -1.0f, -1.0f};
	vec3_t trace_maxs = {1.0f, 1.0f, 1.0f};
	vec3_t trace_end = {20.0f, 0.0f, 0.0f};
	bsp_trace_t trace = AAS_TraceBSPModel(0,
		NULL,
		NULL,
		trace_start,
		trace_mins,
		trace_maxs,
		trace_end,
		CONTENTS_SOLID);
	assert_false(trace.allsolid);
	assert_false(trace.startsolid);
	/*
	 * Retail expands the brush by the trace box: expandedDist =
	 * planedist - minSupport = 0 - (-maxs[0]) = 1, so the -x face at x = 0
	 * blocks the box once its +x face reaches it.  The centre travels 9 of
	 * the 30 units, less the 0.005 clip epsilon (0x10003ea3 / 0x10003f80).
	 */
	assert_float_equal(trace.fraction, (9.0f - 0.005f) / 30.0f, 0.00001f);
	assert_float_equal(trace.endpos[0], -1.005f, 0.0001f);
	assert_float_equal(trace.plane.normal[0], -1.0f, 0.00001f);
	assert_float_equal(trace.plane.dist, 0.0f, 0.00001f);
	assert_int_equal(trace.plane.signbits, 0);
	assert_float_equal(trace.exp_dist, 1.0f, 0.00001f);
	assert_int_equal(trace.sidenum, 0);
	assert_int_equal(trace.contents, CONTENTS_SOLID);
	bsp_surface_t empty_surface;
	memset(&empty_surface, 0, sizeof(empty_surface));
	assert_memory_equal(&trace.surface, &empty_surface, sizeof(empty_surface));

	aasworld.bspBrushSides[0].texinfo = 0x7fff;
	bsp_trace_t invalid_texinfo_trace = AAS_TraceBSPModel(0,
		NULL,
		NULL,
		trace_start,
		trace_mins,
		trace_maxs,
		trace_end,
		CONTENTS_SOLID);
	assert_float_equal(invalid_texinfo_trace.fraction,
		trace.fraction,
		0.00001f);
	assert_float_equal(invalid_texinfo_trace.exp_dist,
		trace.exp_dist,
		0.00001f);
	assert_int_equal(invalid_texinfo_trace.sidenum, trace.sidenum);
	assert_int_equal(invalid_texinfo_trace.contents, trace.contents);
	assert_memory_equal(&invalid_texinfo_trace.surface,
		&empty_surface,
		sizeof(empty_surface));

	AAS_Shutdown();
	assert_int_equal(BotMemory_TotalAllocated(), tracked_before);
	assert_int_equal(aasworld.numBspTexInfo, 0);
	assert_null(aasworld.bspTexInfo);
	BotInterface_SetImportTable(NULL);
	unlink(aas_path);
	unlink(root_aas_path);
	unlink(bsp_path);
	if (remove_directory)
	{
		assert_int_equal(rmdir("maps"), 0);
	}
}

/*
=============
test_aas_loader_uses_retail_candidate_order_and_reports_selected_paths

Pins the loose-file AAS probe order and the BSP/AAS paths printed after discovery.
=============
*/
static void test_aas_loader_uses_retail_candidate_order_and_reports_selected_paths(
	void **state)
{
	(void)state;
	const char *map_name = "__gladiator_loader_candidates";
	const char *bsp_path = "maps/__gladiator_loader_candidates.bsp";
	const char *root_aas_path = "__gladiator_loader_candidates.aas";
	const char *maps_aas_path = "maps/__gladiator_loader_candidates.aas";
#ifdef _WIN32
	const char *reported_bsp_path =
		"maps\\__gladiator_loader_candidates.bsp";
	const char *reported_maps_aas_path =
		"maps\\__gladiator_loader_candidates.aas";
#else
	const char *reported_bsp_path = bsp_path;
	const char *reported_maps_aas_path = maps_aas_path;
#endif

	unlink(bsp_path);
	unlink(root_aas_path);
	unlink(maps_aas_path);
	errno = 0;
	int directory_status = test_make_directory("maps");
	qboolean remove_directory = directory_status == 0;
	assert_true(remove_directory || errno == EEXIST);
	BotInterface_SetImportTable(&g_test_imports);
	memset(&aasworld, 0, sizeof(aasworld));

	q2_bsp_header_t bsp_header;
	memset(&bsp_header, 0, sizeof(bsp_header));
	bsp_header.ident = Q2_BSP_IDENT;
	bsp_header.version = Q2_BSP_VERSION;
	assert_true(test_write_fixture(bsp_path,
		&bsp_header,
		sizeof(bsp_header)));

	q2_aas_header_t aas_header;
	memset(&aas_header, 0, sizeof(aas_header));
	aas_header.ident = Q2_AAS_IDENT;
	aas_header.version = Q2_AAS_VERSION;
	assert_true(test_write_fixture(maps_aas_path,
		&aas_header,
		sizeof(aas_header)));

	test_reset_print_capture();
	int status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(g_test_print_count, TEST_BSP_EMPTY_LUMP_NOTICES + 2);
	test_assert_empty_bsp_lump_notices();
	assert_int_equal(
		g_test_print_priority_history[TEST_BSP_EMPTY_LUMP_NOTICES],
		PRT_MESSAGE);
	assert_int_equal(
		g_test_print_priority_history[TEST_BSP_EMPTY_LUMP_NOTICES + 1],
		PRT_MESSAGE);
	char expected[PATH_MAX];
	/* Retail 1000e9bb prints the logical "maps\<map>.bsp" name for a loose
	   BSP; only the AAS line at 1000eb0f uses the resolved physical path. */
	snprintf(expected, sizeof(expected), "loaded %s\n", reported_bsp_path);
	assert_string_equal(
		g_test_print_message_history[TEST_BSP_EMPTY_LUMP_NOTICES],
		expected);
	snprintf(expected,
		sizeof(expected),
		"loaded %s\n",
		reported_maps_aas_path);
	assert_string_equal(
		g_test_print_message_history[TEST_BSP_EMPTY_LUMP_NOTICES + 1],
		expected);
	assert_string_equal(aasworld.aasFilePath, reported_maps_aas_path);
	AAS_Shutdown();

	assert_true(test_write_fixture(root_aas_path,
		&aas_header,
		sizeof(aas_header)));
	const unsigned char truncated_aas = 0;
	assert_true(test_write_fixture(maps_aas_path, &truncated_aas, 1U));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(g_test_print_count, TEST_BSP_EMPTY_LUMP_NOTICES + 2);
	snprintf(expected, sizeof(expected), "loaded %s\n", root_aas_path);
	assert_string_equal(
		g_test_print_message_history[TEST_BSP_EMPTY_LUMP_NOTICES + 1],
		expected);
	assert_string_equal(aasworld.aasFilePath, root_aas_path);
	AAS_Shutdown();

	assert_true(test_write_fixture(root_aas_path, &truncated_aas, 1U));
	assert_true(test_write_fixture(maps_aas_path,
		&aas_header,
		sizeof(aas_header)));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_CANNOTREADAASHEADER);
	assert_int_equal(g_test_print_count, TEST_BSP_EMPTY_LUMP_NOTICES + 2);
	assert_int_equal(
		g_test_print_priority_history[TEST_BSP_EMPTY_LUMP_NOTICES],
		PRT_MESSAGE);
	assert_int_equal(
		g_test_print_priority_history[TEST_BSP_EMPTY_LUMP_NOTICES + 1],
		PRT_FATAL);
	snprintf(expected,
		sizeof(expected),
		"can't read header of file %s\n",
		root_aas_path);
	assert_string_equal(
		g_test_print_message_history[TEST_BSP_EMPTY_LUMP_NOTICES + 1],
		expected);
	assert_false(aasworld.loaded);

	unlink(root_aas_path);
	unlink(maps_aas_path);
	unlink(bsp_path);
	AAS_Shutdown();
	BotInterface_SetImportTable(NULL);
	if (remove_directory)
	{
		assert_int_equal(rmdir("maps"), 0);
	}
}

/*
=============
test_assert_resolved_map_load

Loads one transient map and verifies both loose-file "loaded" diagnostics.

Retail reports the two loose cases from different buffers: the BSP line at
1000e9bb prints the logical "maps\<map>.bsp" name assembled at 1000e8d6, while
the AAS line at 1000eb0f prints the resolved physical path. Directory
precedence is therefore pinned by the AAS line alone.
=============
*/
static void test_assert_resolved_map_load(const char *map_name,
	const char *expected_aas_path)
{
	char reported_bsp_path[PATH_MAX];
	char reported_aas_path[PATH_MAX];
	snprintf(reported_bsp_path,
		sizeof(reported_bsp_path),
		"maps/%s.bsp",
		map_name);
	snprintf(reported_aas_path,
		sizeof(reported_aas_path),
		"%s",
		expected_aas_path);
#ifdef _WIN32
	for (char *cursor = reported_bsp_path; *cursor != '\0'; ++cursor)
	{
		if (*cursor == '/')
		{
			*cursor = '\\';
		}
	}
	for (char *cursor = reported_aas_path; *cursor != '\0'; ++cursor)
	{
		if (*cursor == '/')
		{
			*cursor = '\\';
		}
	}
#endif

	test_reset_print_capture();
	int status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(g_test_print_count, TEST_BSP_EMPTY_LUMP_NOTICES + 2);
	test_assert_empty_bsp_lump_notices();
	assert_int_equal(
		g_test_print_priority_history[TEST_BSP_EMPTY_LUMP_NOTICES],
		PRT_MESSAGE);
	assert_int_equal(
		g_test_print_priority_history[TEST_BSP_EMPTY_LUMP_NOTICES + 1],
		PRT_MESSAGE);
	char expected[PATH_MAX + 16];
	snprintf(expected,
		sizeof(expected),
		"loaded %s\n",
		reported_bsp_path);
	assert_string_equal(
		g_test_print_message_history[TEST_BSP_EMPTY_LUMP_NOTICES],
		expected);
	snprintf(expected,
		sizeof(expected),
		"loaded %s\n",
		reported_aas_path);
	assert_string_equal(
		g_test_print_message_history[TEST_BSP_EMPTY_LUMP_NOTICES + 1],
		expected);
	assert_string_equal(aasworld.aasFilePath, reported_aas_path);
	AAS_Shutdown();
}

/*
=============
test_aas_loader_resolves_retail_loose_directory_precedence

Pins basedir/cddir and gamedir/baseq2 ordering without archive participation.
=============
*/
static void test_aas_loader_resolves_retail_loose_directory_precedence(
	void **state)
{
	(void)state;
	const char *map_name = "__gladiator_loose_roots";
	const char *basedir = "__gladiator_loose_basedir";
	const char *cddir = "__gladiator_loose_cddir";
	const char *gamedir = "retailgame";
	const char *directories[] = {
		"__gladiator_loose_basedir",
		"__gladiator_loose_basedir/retailgame",
		"__gladiator_loose_basedir/retailgame/maps",
		"__gladiator_loose_basedir/baseq2",
		"__gladiator_loose_basedir/baseq2/maps",
		"__gladiator_loose_cddir",
		"__gladiator_loose_cddir/retailgame",
		"__gladiator_loose_cddir/retailgame/maps",
		"__gladiator_loose_cddir/baseq2",
		"__gladiator_loose_cddir/baseq2/maps"
	};
	const char *bsp_paths[] = {
		"__gladiator_loose_basedir/retailgame/maps/__gladiator_loose_roots.bsp",
		"__gladiator_loose_basedir/baseq2/maps/__gladiator_loose_roots.bsp",
		"__gladiator_loose_cddir/retailgame/maps/__gladiator_loose_roots.bsp",
		"__gladiator_loose_cddir/baseq2/maps/__gladiator_loose_roots.bsp"
	};
	const char *aas_paths[] = {
		"__gladiator_loose_basedir/retailgame/__gladiator_loose_roots.aas",
		"__gladiator_loose_basedir/baseq2/__gladiator_loose_roots.aas",
		"__gladiator_loose_cddir/retailgame/__gladiator_loose_roots.aas",
		"__gladiator_loose_cddir/baseq2/__gladiator_loose_roots.aas"
	};
	const char *basedir_maps_aas =
		"__gladiator_loose_basedir/retailgame/maps/__gladiator_loose_roots.aas";

	for (size_t index = 0;
		index < sizeof(bsp_paths) / sizeof(bsp_paths[0]);
		++index)
	{
		unlink(bsp_paths[index]);
		unlink(aas_paths[index]);
	}
	unlink(basedir_maps_aas);
	for (size_t index = sizeof(directories) / sizeof(directories[0]);
		index > 0U;
		--index)
	{
		rmdir(directories[index - 1U]);
	}
	for (size_t index = 0;
		index < sizeof(directories) / sizeof(directories[0]);
		++index)
	{
		errno = 0;
		int status = test_make_directory(directories[index]);
		assert_true(status == 0 || errno == EEXIST);
	}

	BotInterface_SetImportTable(&g_test_imports);
	LibVar_Init();
	LibVarSet("basedir", basedir);
	LibVarSet("gamedir", gamedir);
	LibVarSet("cddir", cddir);
	memset(&aasworld, 0, sizeof(aasworld));

	q2_bsp_header_t bsp_header;
	memset(&bsp_header, 0, sizeof(bsp_header));
	bsp_header.ident = Q2_BSP_IDENT;
	bsp_header.version = Q2_BSP_VERSION;
	q2_aas_header_t aas_header;
	memset(&aas_header, 0, sizeof(aas_header));
	aas_header.ident = Q2_AAS_IDENT;
	aas_header.version = Q2_AAS_VERSION;
	for (size_t index = 0;
		index < sizeof(bsp_paths) / sizeof(bsp_paths[0]);
		++index)
	{
		assert_true(test_write_fixture(bsp_paths[index],
			&bsp_header,
			sizeof(bsp_header)));
		assert_true(test_write_fixture(aas_paths[index],
			&aas_header,
			sizeof(aas_header)));
	}

	test_assert_resolved_map_load(map_name, aas_paths[0]);
	unlink(bsp_paths[0]);
	unlink(aas_paths[0]);
	test_assert_resolved_map_load(map_name, aas_paths[1]);
	unlink(bsp_paths[1]);
	unlink(aas_paths[1]);
	test_assert_resolved_map_load(map_name, aas_paths[2]);
	unlink(bsp_paths[2]);
	unlink(aas_paths[2]);
	test_assert_resolved_map_load(map_name, aas_paths[3]);

	assert_true(test_write_fixture(bsp_paths[0],
		&bsp_header,
		sizeof(bsp_header)));
	assert_true(test_write_fixture(basedir_maps_aas,
		&aas_header,
		sizeof(aas_header)));
	test_assert_resolved_map_load(map_name, aas_paths[3]);

	LibVar_Shutdown();
	BotInterface_SetImportTable(NULL);
	for (size_t index = 0;
		index < sizeof(bsp_paths) / sizeof(bsp_paths[0]);
		++index)
	{
		unlink(bsp_paths[index]);
		unlink(aas_paths[index]);
	}
	unlink(basedir_maps_aas);
	for (size_t index = sizeof(directories) / sizeof(directories[0]);
		index > 0U;
		--index)
	{
		assert_int_equal(rmdir(directories[index - 1U]), 0);
	}
}

/*
=============
test_aas_loader_probes_retail_paks_and_reads_bounded_entries

Pins per-directory pak0..pak9 checkpoints, access/search log records, normalized
entry lookup, bounded checksums, archive diagnostics, and logical writeback.
=============
*/
static void test_aas_loader_probes_retail_paks_and_reads_bounded_entries(
	void **state)
{
	(void)state;
	const char *map_name = "__gladiator_pak_roots";
	const char *logical_bsp = "maps/__gladiator_pak_roots.bsp";
	const char *logical_aas = "__gladiator_pak_roots.aas";
	const char *basedir = "__gladiator_pak_basedir";
	const char *gamedir = "retailgame";
	const char *log_path = "__gladiator_pak_search.log";
	const char *directories[] = {
		"__gladiator_pak_basedir",
		"__gladiator_pak_basedir/retailgame",
		"__gladiator_pak_basedir/baseq2",
		"__gladiator_pak_basedir/baseq2/maps"
	};
	const char *pak0_path =
		"__gladiator_pak_basedir/retailgame/pak0.pak";
	const char *pak1_path =
		"__gladiator_pak_basedir/retailgame/pak1.pak";
	const char *missing_loose_bsp =
		"__gladiator_pak_basedir/retailgame/maps/__gladiator_pak_roots.bsp";
	const char *missing_loose_aas =
		"__gladiator_pak_basedir/retailgame/__gladiator_pak_roots.aas";
	const char *later_loose_bsp =
		"__gladiator_pak_basedir/baseq2/maps/__gladiator_pak_roots.bsp";
	const char *later_loose_aas =
		"__gladiator_pak_basedir/baseq2/__gladiator_pak_roots.aas";

	unlink(pak0_path);
	unlink(pak1_path);
	unlink(later_loose_bsp);
	unlink(later_loose_aas);
	unlink(logical_aas);
	unlink(log_path);
	for (size_t index = sizeof(directories) / sizeof(directories[0]);
		index > 0U;
		--index)
	{
		rmdir(directories[index - 1U]);
	}
	for (size_t index = 0U;
		index < sizeof(directories) / sizeof(directories[0]);
		++index)
	{
		errno = 0;
		int status = test_make_directory(directories[index]);
		assert_true(status == 0 || errno == EEXIST);
	}

	q2_bsp_header_t bsp_header;
	memset(&bsp_header, 0, sizeof(bsp_header));
	bsp_header.ident = Q2_BSP_IDENT;
	bsp_header.version = Q2_BSP_VERSION;
	const char bsp_entities[] = "{}";
	aas_plane_t bsp_plane;
	memset(&bsp_plane, 0, sizeof(bsp_plane));
	bsp_header.lumps[Q2_BSP_LUMP_ENTITIES].offset =
		(int32_t)sizeof(bsp_header);
	bsp_header.lumps[Q2_BSP_LUMP_ENTITIES].length =
		(int32_t)sizeof(bsp_entities);
	bsp_header.lumps[Q2_BSP_LUMP_PLANES].offset =
		(int32_t)(sizeof(bsp_header) + sizeof(bsp_entities));
	bsp_header.lumps[Q2_BSP_LUMP_PLANES].length =
		(int32_t)sizeof(bsp_plane);
	unsigned char bsp_entry[sizeof(bsp_header) + sizeof(bsp_entities) +
		sizeof(bsp_plane)];
	memcpy(bsp_entry, &bsp_header, sizeof(bsp_header));
	memcpy(bsp_entry + sizeof(bsp_header),
		bsp_entities,
		sizeof(bsp_entities));
	memcpy(bsp_entry + sizeof(bsp_header) + sizeof(bsp_entities),
		&bsp_plane,
		sizeof(bsp_plane));

	q2_aas_header_t aas_header;
	memset(&aas_header, 0, sizeof(aas_header));
	aas_header.ident = Q2_AAS_IDENT;
	aas_header.version = Q2_AAS_VERSION;
	aas_bbox_t aas_bbox;
	memset(&aas_bbox, 0, sizeof(aas_bbox));
	aas_header.lumps[Q2_AAS_LUMP_BBOXES].offset =
		(int32_t)sizeof(aas_header);
	aas_header.lumps[Q2_AAS_LUMP_BBOXES].length =
		(int32_t)sizeof(aas_bbox);
	unsigned char aas_entry[sizeof(aas_header) + sizeof(aas_bbox)];
	memcpy(aas_entry, &aas_header, sizeof(aas_header));
	memcpy(aas_entry + sizeof(aas_header), &aas_bbox, sizeof(aas_bbox));

	const unsigned char ignored_data[] = {0xA5U, 0x5AU, 0xC3U};
	const test_pak_fixture_entry_t pak0_entries[] = {
		{"scripts/ignored.bin", ignored_data, sizeof(ignored_data)},
		{"__GLADIATOR_PAK_ROOTS.AAS", aas_entry, sizeof(aas_entry)}
	};
	const test_pak_fixture_entry_t pak1_entries[] = {
		{"MAPS/__GLADIATOR_PAK_ROOTS.BSP", bsp_entry, sizeof(bsp_entry)}
	};
	assert_true(test_write_pak_fixture(pak0_path,
		pak0_entries,
		sizeof(pak0_entries) / sizeof(pak0_entries[0])));
	assert_true(test_write_pak_fixture(pak1_path,
		pak1_entries,
		sizeof(pak1_entries) / sizeof(pak1_entries[0])));

	q2_bsp_header_t loose_bsp_decoy;
	memset(&loose_bsp_decoy, 0, sizeof(loose_bsp_decoy));
	q2_aas_header_t loose_aas_decoy;
	memset(&loose_aas_decoy, 0, sizeof(loose_aas_decoy));
	assert_true(test_write_fixture(later_loose_bsp,
		&loose_bsp_decoy,
		sizeof(loose_bsp_decoy)));
	assert_true(test_write_fixture(later_loose_aas,
		&loose_aas_decoy,
		sizeof(loose_aas_decoy)));

	BotInterface_SetImportTable(&g_test_imports);
	LibVar_Init();
	LibVarSet("basedir", basedir);
	LibVarSet("gamedir", gamedir);
	LibVarSet("cddir", "__gladiator_pak_missing_cddir");
	LibVarSet("log", "1");
	memset(&aasworld, 0, sizeof(aasworld));
	BotLib_LogOpen(log_path);
	assert_non_null(BotLib_LogFile());
	test_reset_print_capture();

	int status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_NOERROR);
	assert_true(aasworld.loaded);
	assert_int_equal(g_test_print_count, TEST_BSP_EMPTY_LUMP_NOTICES + 2);
	test_assert_empty_bsp_lump_notices();
	assert_int_equal(
		g_test_print_priority_history[TEST_BSP_EMPTY_LUMP_NOTICES],
		PRT_MESSAGE);
	assert_int_equal(
		g_test_print_priority_history[TEST_BSP_EMPTY_LUMP_NOTICES + 1],
		PRT_MESSAGE);

	char reported_pak0[PATH_MAX];
	char reported_pak1[PATH_MAX];
	char reported_logical_bsp[PATH_MAX];
	char reported_loose_bsp[PATH_MAX];
	char reported_loose_aas[PATH_MAX];
	snprintf(reported_pak0, sizeof(reported_pak0), "%s", pak0_path);
	snprintf(reported_pak1, sizeof(reported_pak1), "%s", pak1_path);
	snprintf(reported_logical_bsp,
		sizeof(reported_logical_bsp),
		"%s",
		logical_bsp);
	snprintf(reported_loose_bsp,
		sizeof(reported_loose_bsp),
		"%s",
		missing_loose_bsp);
	snprintf(reported_loose_aas,
		sizeof(reported_loose_aas),
		"%s",
		missing_loose_aas);
#ifdef _WIN32
	char *reported_paths[] = {
		reported_pak0,
		reported_pak1,
		reported_logical_bsp,
		reported_loose_bsp,
		reported_loose_aas
	};
	for (size_t path_index = 0U;
		path_index < sizeof(reported_paths) / sizeof(reported_paths[0]);
		++path_index)
	{
		for (char *cursor = reported_paths[path_index];
			*cursor != '\0';
			++cursor)
		{
			if (*cursor == '/')
			{
				*cursor = '\\';
			}
		}
	}
	const char archive_separator = '\\';
#else
	const char archive_separator = '/';
#endif
	char expected[PATH_MAX * 2U];
	snprintf(expected,
		sizeof(expected),
		"loaded %s%c%s\n",
		reported_pak1,
		archive_separator,
		reported_logical_bsp);
	assert_string_equal(
		g_test_print_message_history[TEST_BSP_EMPTY_LUMP_NOTICES],
		expected);
	snprintf(expected,
		sizeof(expected),
		"loaded %s%c%s\n",
		reported_pak0,
		archive_separator,
		logical_aas);
	assert_string_equal(
		g_test_print_message_history[TEST_BSP_EMPTY_LUMP_NOTICES + 1],
		expected);
	assert_string_equal(aasworld.aasFilePath, logical_aas);
	assert_int_equal(aasworld.numBspPlanes, 1);
	assert_int_equal(aasworld.numBBoxes, 1);
	assert_int_equal(aasworld.bspChecksum,
		(int32_t)test_crc32(bsp_entry, sizeof(bsp_entry)));
	assert_int_equal(aasworld.aasChecksum,
		(int32_t)test_crc32(aas_entry, sizeof(aas_entry)));

	AAS_Shutdown();
	BotLib_LogClose();
	FILE *log_file = fopen(log_path, "rb");
	assert_non_null(log_file);
	char log_contents[2048];
	size_t log_length = fread(log_contents,
		1U,
		sizeof(log_contents) - 1U,
		log_file);
	assert_false(ferror(log_file));
	assert_int_equal(fclose(log_file), 0);
	log_contents[log_length] = '\0';
	char expected_log[2048];
	int expected_log_length = snprintf(expected_log,
		sizeof(expected_log),
		"accessing %s\r\n"
		"searching %s in %s\r\n"
		"searching %s in %s\r\n"
		"accessing %s\r\n"
		"searching %s in %s\r\n",
		reported_loose_bsp,
		reported_logical_bsp, reported_pak0,
		reported_logical_bsp, reported_pak1,
		reported_loose_aas,
		logical_aas, reported_pak0);
	assert_true(expected_log_length >= 0 &&
		(size_t)expected_log_length < sizeof(expected_log));
	assert_string_equal(log_contents, expected_log);
	LibVar_Shutdown();
	BotInterface_SetImportTable(NULL);
	unlink(logical_aas);
	unlink(pak0_path);
	unlink(pak1_path);
	unlink(later_loose_bsp);
	unlink(later_loose_aas);
	unlink(log_path);
	for (size_t index = sizeof(directories) / sizeof(directories[0]);
		index > 0U;
		--index)
	{
		assert_int_equal(rmdir(directories[index - 1U]), 0);
	}
}

/*
=============
test_aas_loader_runs_retail_zip_fallback_as_a_separate_extraction_pass

Pins normal-search precedence, ZIP order/logging, retail failure residue,
successful extracted-member deletion/CWD restore, and empty ZIP writeback paths.
=============
*/
static void test_aas_loader_runs_retail_zip_fallback_as_a_separate_extraction_pass(
	void **state)
{
	(void)state;
	const char *map_name = "__gladiator_zip_roots";
	const char *basedir = "__gladiator_zip_basedir";
	const char *gamedir = "retailgame";
	const char *logical_member = "__gladiator_zip_roots.aas";
	const char *log_path = "__gladiator_zip_search.log";
	const char *directories[] = {
		"__gladiator_zip_basedir",
		"__gladiator_zip_basedir/retailgame",
		"__gladiator_zip_basedir/retailgame/maps",
		"__gladiator_zip_basedir/baseq2"
	};
	const char *bsp_path =
		"__gladiator_zip_basedir/retailgame/maps/__gladiator_zip_roots.bsp";
	const char *normal_aas_path =
		"__gladiator_zip_basedir/retailgame/maps/__gladiator_zip_roots.aas";
	const char *extracted_aas_path =
		"__gladiator_zip_basedir/retailgame/__gladiator_zip_roots.aas";
	const char *game_zip0_path =
		"__gladiator_zip_basedir/retailgame/aas0.zip";
	const char *game_zip1_path =
		"__gladiator_zip_basedir/retailgame/aas1.zip";
	const char *base_zip0_path =
		"__gladiator_zip_basedir/baseq2/aas0.zip";
#ifdef _WIN32
	const char *relative_game_zip0 = "..\\retailgame\\aas0.zip";
	const char *relative_game_zip1 = "..\\retailgame\\aas1.zip";
	const char *relative_base_zip0 = "..\\baseq2\\aas0.zip";
	const char archive_separator = '\\';
#else
	const char *relative_game_zip0 = "../retailgame/aas0.zip";
	const char *relative_game_zip1 = "../retailgame/aas1.zip";
	const char *relative_base_zip0 = "../baseq2/aas0.zip";
	const char archive_separator = '/';
#endif

	unlink(bsp_path);
	unlink(normal_aas_path);
	unlink(extracted_aas_path);
	unlink(game_zip0_path);
	unlink(game_zip1_path);
	unlink(base_zip0_path);
	unlink(log_path);
	for (size_t index = sizeof(directories) / sizeof(directories[0]);
		index > 0U;
		--index)
	{
		rmdir(directories[index - 1U]);
	}
	for (size_t index = 0U;
		index < sizeof(directories) / sizeof(directories[0]);
		++index)
	{
		errno = 0;
		int status = test_make_directory(directories[index]);
		assert_true(status == 0 || errno == EEXIST);
	}

	q2_bsp_header_t bsp_header;
	memset(&bsp_header, 0, sizeof(bsp_header));
	bsp_header.ident = Q2_BSP_IDENT;
	bsp_header.version = Q2_BSP_VERSION;
	assert_true(test_write_fixture(bsp_path,
		&bsp_header,
		sizeof(bsp_header)));

	q2_aas_header_t aas_header;
	memset(&aas_header, 0, sizeof(aas_header));
	aas_header.ident = Q2_AAS_IDENT;
	aas_header.version = Q2_AAS_VERSION;
	aas_bbox_t aas_bbox;
	memset(&aas_bbox, 0, sizeof(aas_bbox));
	aas_header.lumps[Q2_AAS_LUMP_BBOXES].offset =
		(int32_t)sizeof(aas_header);
	aas_header.lumps[Q2_AAS_LUMP_BBOXES].length =
		(int32_t)sizeof(aas_bbox);
	unsigned char aas_member[sizeof(aas_header) + sizeof(aas_bbox)];
	memcpy(aas_member, &aas_header, sizeof(aas_header));
	memcpy(aas_member + sizeof(aas_header), &aas_bbox, sizeof(aas_bbox));

	const unsigned char archive_marker[] = {0x50U, 0x4BU, 0x03U, 0x04U};
	assert_true(test_write_fixture(game_zip0_path,
		archive_marker,
		sizeof(archive_marker)));
	assert_true(test_write_fixture(game_zip1_path,
		archive_marker,
		sizeof(archive_marker)));
	assert_true(test_write_fixture(base_zip0_path,
		archive_marker,
		sizeof(archive_marker)));

	snprintf(g_test_zip_game_success_path,
		sizeof(g_test_zip_game_success_path),
		"%s",
		relative_game_zip1);
	snprintf(g_test_zip_base_success_path,
		sizeof(g_test_zip_base_success_path),
		"%s",
		relative_base_zip0);
	snprintf(g_test_zip_expected_member,
		sizeof(g_test_zip_expected_member),
		"%s",
		logical_member);
	g_test_zip_member_data = aas_member;
	g_test_zip_member_size = sizeof(aas_member);
	AAS_SetZipExtractorForTests(test_extract_zip_member);

	char original_directory[PATH_MAX];
	assert_non_null(getcwd(original_directory, sizeof(original_directory)));
	BotInterface_SetImportTable(&g_test_imports);
	LibVar_Init();
	LibVarSet("basedir", basedir);
	LibVarSet("gamedir", gamedir);
	LibVarSet("cddir", "__gladiator_zip_missing_cddir");
	LibVarSet("log", "1");
	memset(&aasworld, 0, sizeof(aasworld));
	BotLib_LogOpen(log_path);
	assert_non_null(BotLib_LogFile());

	assert_true(test_write_fixture(normal_aas_path,
		aas_member,
		sizeof(aas_member)));
	g_test_zip_extract_mode = 1;
	g_test_zip_extract_count = 0U;
	test_reset_print_capture();
	int status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(g_test_zip_extract_count, 0);
	char reported_normal_aas[PATH_MAX];
	snprintf(reported_normal_aas,
		sizeof(reported_normal_aas),
		"%s",
		normal_aas_path);
#ifdef _WIN32
	for (char *cursor = reported_normal_aas; *cursor != '\0'; ++cursor)
	{
		if (*cursor == '/')
		{
			*cursor = '\\';
		}
	}
#endif
	assert_string_equal(aasworld.aasFilePath, reported_normal_aas);
	AAS_Shutdown();
	unlink(normal_aas_path);

	g_test_zip_extract_mode = 0;
	g_test_zip_extract_count = 0U;
	memset(g_test_zip_archive_history, 0, sizeof(g_test_zip_archive_history));
	memset(g_test_zip_member_history, 0, sizeof(g_test_zip_member_history));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_NOAASFILE);
	assert_int_equal(g_test_zip_extract_count, 3);
	assert_string_equal(g_test_zip_archive_history[0], relative_game_zip0);
	assert_string_equal(g_test_zip_archive_history[1], relative_game_zip1);
	assert_string_equal(g_test_zip_archive_history[2], relative_base_zip0);
	for (size_t index = 0U; index < 3U; ++index)
	{
		assert_string_equal(g_test_zip_member_history[index], logical_member);
	}
	assert_int_equal(g_test_print_count, TEST_BSP_EMPTY_LUMP_NOTICES + 2);
	test_assert_empty_bsp_lump_notices();
	assert_int_equal(
		g_test_print_priority_history[TEST_BSP_EMPTY_LUMP_NOTICES],
		PRT_MESSAGE);
	assert_int_equal(
		g_test_print_priority_history[TEST_BSP_EMPTY_LUMP_NOTICES + 1],
		PRT_FATAL);
	assert_string_equal(g_test_print_message, "no AAS file available\n");
	char current_directory[PATH_MAX];
	assert_non_null(getcwd(current_directory, sizeof(current_directory)));
	assert_string_equal(current_directory, original_directory);
	AAS_Shutdown();

	g_test_zip_member_data = archive_marker;
	g_test_zip_member_size = sizeof(archive_marker);
	g_test_zip_extract_mode = 1;
	g_test_zip_extract_count = 0U;
	memset(g_test_zip_archive_history, 0, sizeof(g_test_zip_archive_history));
	memset(g_test_zip_member_history, 0, sizeof(g_test_zip_member_history));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_NOAASFILE);
	assert_int_equal(g_test_zip_extract_count, 2);
	assert_string_equal(g_test_zip_archive_history[0], relative_game_zip0);
	assert_string_equal(g_test_zip_archive_history[1], relative_game_zip1);
	assert_string_equal(g_test_print_message, "no AAS file available\n");
	assert_non_null(getcwd(current_directory, sizeof(current_directory)));
	char retained_directory[PATH_MAX];
	snprintf(retained_directory,
		sizeof(retained_directory),
		"%s%c%s%c%s",
		original_directory,
		archive_separator,
		basedir,
		archive_separator,
		gamedir);
	assert_string_equal(current_directory, retained_directory);
	FILE *retained_aas = fopen(logical_member, "rb");
	assert_non_null(retained_aas);
	unsigned char retained_bytes[sizeof(archive_marker)];
	assert_int_equal(fread(retained_bytes,
		1U,
		sizeof(retained_bytes),
		retained_aas),
		sizeof(retained_bytes));
	assert_int_equal(fclose(retained_aas), 0);
	assert_memory_equal(retained_bytes,
		archive_marker,
		sizeof(retained_bytes));
	assert_int_equal(chdir(original_directory), 0);
	assert_int_equal(unlink(extracted_aas_path), 0);
	AAS_Shutdown();

	g_test_zip_member_data = aas_member;
	g_test_zip_member_size = sizeof(aas_member);
	g_test_zip_extract_mode = 1;
	g_test_zip_extract_count = 0U;
	memset(g_test_zip_archive_history, 0, sizeof(g_test_zip_archive_history));
	memset(g_test_zip_member_history, 0, sizeof(g_test_zip_member_history));
	test_reset_print_capture();
	status = AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_NOERROR);
	assert_true(aasworld.loaded);
	assert_int_equal(g_test_zip_extract_count, 2);
	assert_string_equal(g_test_zip_archive_history[0], relative_game_zip0);
	assert_string_equal(g_test_zip_archive_history[1], relative_game_zip1);
	assert_string_equal(g_test_zip_member_history[0], logical_member);
	assert_string_equal(g_test_zip_member_history[1], logical_member);
	/*
	 * The header-only BSP reloaded by this pass still runs retail
	 * sub_10007d30, which reports its empty visibility (10007fbb) and
	 * lighting (1000813c) lumps ahead of the two "loaded" lines, exactly as
	 * the earlier passes in this test already assert.
	 */
	assert_int_equal(g_test_print_count, TEST_BSP_EMPTY_LUMP_NOTICES + 2);
	test_assert_empty_bsp_lump_notices();
	char expected[PATH_MAX * 2U];
	snprintf(expected,
		sizeof(expected),
		"loaded %s%c%s\n",
		relative_game_zip1,
		archive_separator,
		logical_member);
	assert_string_equal(
		g_test_print_message_history[TEST_BSP_EMPTY_LUMP_NOTICES + 1],
		expected);
	assert_string_equal(aasworld.aasFilePath, "");
	assert_int_equal(aasworld.numBBoxes, 1);
	assert_int_equal(aasworld.aasChecksum,
		(int32_t)test_crc32(aas_member, sizeof(aas_member)));
	FILE *extracted_aas = fopen(extracted_aas_path, "rb");
	assert_null(extracted_aas);
	assert_non_null(getcwd(current_directory, sizeof(current_directory)));
	assert_string_equal(current_directory, original_directory);

	AAS_Shutdown();
	BotLib_LogClose();
	FILE *log_file = fopen(log_path, "rb");
	assert_non_null(log_file);
	char log_contents[16384];
	size_t log_length = fread(log_contents,
		1U,
		sizeof(log_contents) - 1U,
		log_file);
	assert_int_equal(fgetc(log_file), EOF);
	assert_false(ferror(log_file));
	assert_int_equal(fclose(log_file), 0);
	log_contents[log_length] = '\0';
	char zip_log_contents[4096];
	size_t zip_log_length = 0U;
	const char *line_start = log_contents;
	while (*line_start != '\0')
	{
		const char *line_end = strstr(line_start, "\r\n");
		assert_non_null(line_end);
		const char *member_position = strstr(line_start, logical_member);
		bool zip_record = member_position != NULL &&
			member_position < line_end &&
			(strncmp(line_start,
				"searching ",
				sizeof("searching ") - 1U) == 0 ||
			strncmp(line_start,
				"could not find ",
				sizeof("could not find ") - 1U) == 0 ||
			strncmp(line_start,
				"found ",
				sizeof("found ") - 1U) == 0);
		if (zip_record)
		{
			size_t line_length = (size_t)(line_end - line_start);
			assert_true(zip_log_length + line_length + 2U <
				sizeof(zip_log_contents));
			memcpy(zip_log_contents + zip_log_length,
				line_start,
				line_length + 2U);
			zip_log_length += line_length + 2U;
		}
		line_start = line_end + 2;
	}
	zip_log_contents[zip_log_length] = '\0';
	char expected_log[4096];
	int expected_log_length = snprintf(expected_log,
		sizeof(expected_log),
		"searching %s in %s\r\n"
		"could not find %s in %s\r\n"
		"searching %s in %s\r\n"
		"could not find %s in %s\r\n"
		"searching %s in %s\r\n"
		"could not find %s in %s\r\n"
		"searching %s in %s\r\n"
		"could not find %s in %s\r\n"
		"searching %s in %s\r\n"
		"searching %s in %s\r\n"
		"could not find %s in %s\r\n"
		"searching %s in %s\r\n"
		"found %s in %s\r\n",
		logical_member, relative_game_zip0,
		logical_member, relative_game_zip0,
		logical_member, relative_game_zip1,
		logical_member, relative_game_zip1,
		logical_member, relative_base_zip0,
		logical_member, relative_base_zip0,
		logical_member, relative_game_zip0,
		logical_member, relative_game_zip0,
		logical_member, relative_game_zip1,
		logical_member, relative_game_zip0,
		logical_member, relative_game_zip0,
		logical_member, relative_game_zip1,
		logical_member, relative_game_zip1);
	assert_true(expected_log_length >= 0 &&
		(size_t)expected_log_length < sizeof(expected_log));
	assert_string_equal(zip_log_contents, expected_log);
	AAS_SetZipExtractorForTests(NULL);
	LibVar_Shutdown();
	BotInterface_SetImportTable(NULL);
	g_test_zip_member_data = NULL;
	g_test_zip_member_size = 0U;
	unlink(bsp_path);
	unlink(normal_aas_path);
	unlink(extracted_aas_path);
	unlink(game_zip0_path);
	unlink(game_zip1_path);
	unlink(base_zip0_path);
	unlink(log_path);
	for (size_t index = sizeof(directories) / sizeof(directories[0]);
		index > 0U;
		--index)
	{
		assert_int_equal(rmdir(directories[index - 1U]), 0);
	}
}

static void test_aas_loads_sample_map(void **state)
{
    (void)state;

    int status = AAS_LoadMap("test_nav", 0, NULL, 0, NULL, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);

    assert_true(aasworld.loaded);
    assert_true(aasworld.initialized);
    assert_int_equal(aasworld.numAreas, 3);
    assert_int_equal(aasworld.numReachability, 2);
    assert_int_equal(aasworld.numNodes, 1);
    assert_int_equal(aasworld.bspChecksum, -331439195);
    assert_int_equal(aasworld.aasChecksum, -42330527);

    AAS_Shutdown();
}

static void test_aas_entity_linking_and_reachability(void **state)
{
    (void)state;

    int status = AAS_LoadMap("test_nav", 0, NULL, 0, NULL, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateentity_t fixtures[3];
    memset(fixtures, 0, sizeof(fixtures));
	for (size_t index = 0; index < 3; ++index)
	{
		fixtures[index].solid = SOLID_BBOX;
	}

    fixtures[0].origin[0] = 16.0f;
    fixtures[0].origin[1] = 16.0f;
    fixtures[0].origin[2] = 16.0f;
    fixtures[0].old_origin[0] = 16.0f;
    fixtures[0].old_origin[1] = 16.0f;
    fixtures[0].old_origin[2] = 16.0f;
    fixtures[0].mins[0] = -8.0f;
    fixtures[0].mins[1] = -8.0f;
    fixtures[0].mins[2] = -8.0f;
    fixtures[0].maxs[0] = 8.0f;
    fixtures[0].maxs[1] = 8.0f;
    fixtures[0].maxs[2] = 8.0f;

    fixtures[1].origin[0] = 48.0f;
    fixtures[1].origin[1] = 16.0f;
    fixtures[1].origin[2] = 16.0f;
    fixtures[1].old_origin[0] = 48.0f;
    fixtures[1].old_origin[1] = 16.0f;
    fixtures[1].old_origin[2] = 16.0f;
    fixtures[1].mins[0] = -40.0f;
    fixtures[1].mins[1] = -8.0f;
    fixtures[1].mins[2] = -8.0f;
    fixtures[1].maxs[0] = 40.0f;
    fixtures[1].maxs[1] = 8.0f;
    fixtures[1].maxs[2] = 8.0f;

    fixtures[2].origin[0] = 160.0f;
    fixtures[2].origin[1] = 160.0f;
    fixtures[2].origin[2] = 160.0f;
    fixtures[2].old_origin[0] = 160.0f;
    fixtures[2].old_origin[1] = 160.0f;
    fixtures[2].old_origin[2] = 160.0f;
    fixtures[2].mins[0] = -8.0f;
    fixtures[2].mins[1] = -8.0f;
    fixtures[2].mins[2] = -8.0f;
    fixtures[2].maxs[0] = 8.0f;
    fixtures[2].maxs[1] = 8.0f;
    fixtures[2].maxs[2] = 8.0f;

    aasworld.time = 1.0f;
    AASEntityFrame translated = {0};
    status = TranslateEntityUpdate(1, &fixtures[0], &translated);
    assert_int_equal(status, BLERR_NOERROR);
    status = AAS_UpdateEntity(1, &translated);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(aasworld.entitiesValid);
    assert_non_null(aasworld.entities);
    assert_false(aasworld.entities[1].outsideAllAreas);
    assert_int_equal(aasworld.entities[1].areaOccupancyCount, 1);
    int single_area[] = {1};
    assert_entity_area_membership(1, single_area, 1);
    assert_area_entity_list_contains(1, 1);

    aasworld.time = 2.0f;
    status = TranslateEntityUpdate(1, &fixtures[1], &translated);
    assert_int_equal(status, BLERR_NOERROR);
    status = AAS_UpdateEntity(1, &translated);
    assert_int_equal(status, BLERR_NOERROR);
    assert_false(aasworld.entities[1].outsideAllAreas);
    assert_int_equal(aasworld.entities[1].areaOccupancyCount, 2);
    int dual_areas[] = {1, 2};
    assert_entity_area_membership(1, dual_areas, 2);
    assert_area_entity_list_contains(1, 1);
    assert_area_entity_list_contains(2, 1);

    aasworld.time = 3.0f;
    status = TranslateEntityUpdate(1, &fixtures[2], &translated);
    assert_int_equal(status, BLERR_NOERROR);
    status = AAS_UpdateEntity(1, &translated);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(aasworld.entities[1].outsideAllAreas);
    assert_int_equal(aasworld.entities[1].areaOccupancyCount, 0);
    assert_entity_area_membership(1, NULL, 0);

    /* Validate reachability metadata seeded by the sample file. */
    assert_travel_time_for_area(2, 40);
    assert_travel_time_for_area(1, 42);

    AAS_Shutdown();
}

/*
=============
test_aas_entity_relinks_on_bsp_angle_change

Retail relinks SOLID_BSP entities when their angles change even when their
origin and derived bounds are otherwise unchanged.
=============
*/
static void test_aas_entity_relinks_on_bsp_angle_change(void **state)
{
	(void)state;
	assert_int_equal(AAS_LoadMap("test_nav", 0, NULL, 0, NULL, 0, NULL),
		BLERR_NOERROR);

	AASEntityFrame entity = {0};
	entity.solid = SOLID_BSP;
	VectorSet(entity.origin, 16.0f, 16.0f, 16.0f);
	VectorSet(entity.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(entity.maxs, 8.0f, 8.0f, 8.0f);
	entity.angles_dirty = true;
	entity.angles[1] = 45.0f;

	/* sub_1000a920 gates both spatial relink calls on entnum > 0. */
	assert_int_equal(AAS_UpdateEntity(0, &entity), BLERR_NOERROR);
	assert_null(aasworld.entities[0].areas);
	assert_int_equal(aasworld.entities[0].areaOccupancyCount, 0);

	entity.number = 2;
	entity.angles_dirty = false;
	entity.angles[1] = 0.0f;
	assert_int_equal(AAS_UpdateEntity(2, &entity), BLERR_NOERROR);
	assert_null(aasworld.entities[2].areas);
	assert_int_equal(aasworld.entities[2].areaOccupancyCount, 0);

	entity.angles_dirty = true;
	entity.angles[1] = 45.0f;
	assert_int_equal(AAS_UpdateEntity(2, &entity), BLERR_NOERROR);
	assert_non_null(aasworld.entities[2].areas);
	assert_int_equal(aasworld.entities[2].areaOccupancyCount, 1);
	int expected_area[] = {1};
	assert_entity_area_membership(2, expected_area, 1);

	assert_int_equal(AAS_UpdateEntity(0, NULL), BLERR_NOERROR);
	assert_int_equal(AAS_UpdateEntity(2, NULL), BLERR_NOERROR);
	AAS_Shutdown();
}

/*
=============
aas_link_heap_setup

Prepare the minimal import, libvar, and memory state used by link-heap tests.
=============
*/
static int aas_link_heap_setup(void **state)
{
	(void)state;
	test_reset_print_capture();
	BotInterface_SetImportTable(&g_test_imports);
	LibVar_Init();
	if (!BotMemory_Init(TEST_BOTLIB_HEAP_SIZE))
	{
		LibVar_Shutdown();
		BotInterface_SetImportTable(NULL);
		return -1;
	}

	return 0;
}

/*
=============
aas_link_heap_teardown

Release the minimal link-heap test state without requiring map assets.
=============
*/
static int aas_link_heap_teardown(void **state)
{
	(void)state;
	AAS_Shutdown();
	LibVar_Shutdown();
	BotMemory_Shutdown();
	BotInterface_SetImportTable(NULL);
	return 0;
}

/*
=============
test_retail_map_load_keeps_configured_entity_storage

Retail map loading resets the configured entity records' link heads without
reallocating the fixed setup-time entity table, even if discovery fails.
=============
*/
static void test_retail_map_load_keeps_configured_entity_storage(void **state)
{
	(void)state;
	assert_int_equal(AAS_ConfigureEntityLimits(2, 1), BLERR_NOERROR);
	assert_non_null(aasworld.entities);

	aas_entity_t *entities = aasworld.entities;
	aas_link_t area_link = {0};
	bsp_link_t leaf_link = {0};
	aasworld.entities[1].inuse = qtrue;
	aasworld.entities[1].number = 91;
	aasworld.entities[1].areas = &area_link;
	aasworld.entities[1].leaves = &leaf_link;

	assert_int_equal(AAS_LoadMap("", 0, NULL, 0, NULL, 0, NULL),
		BLERR_NOBSPFILE);
	assert_ptr_equal(aasworld.entities, entities);
	assert_int_equal(aasworld.maxEntities, 2);
	assert_int_equal(aasworld.maxClients, 1);
	assert_true(aasworld.entities[1].inuse);
	assert_int_equal(aasworld.entities[1].number, 91);
	assert_null(aasworld.entities[1].areas);
	assert_null(aasworld.entities[1].leaves);
}

/*
=============
test_retail_entity_configuration_initialises_sound_state

Retail configuration rebuilds the ordinary sound heap and soundinfo table
before it invalidates entity records.
=============
*/
static void test_retail_entity_configuration_initialises_sound_state(void **state)
{
	(void)state;
	LibVarSet("max_soundinfo", "64");
	LibVarSet("max_aassounds", "4");
	LibVarSet("soundconfig", PROJECT_SOURCE_DIR "/dev_tools/assets/sounds.c");

	assert_int_equal(AAS_ConfigureEntityLimits(2, 1), BLERR_NOERROR);
	assert_true(AAS_SoundSubsystem_InfoCount() > 0U);

	char *sound_indexes[] = {"player/step1.wav"};
	assert_int_equal(AAS_LoadMap(NULL, 0, NULL, 1, sound_indexes, 0, NULL),
		BLERR_NOERROR);
	vec3_t origin = {0.0f, 0.0f, 0.0f};
	assert_int_equal(AAS_SoundSubsystem_UpdateSound(origin,
		1,
		0,
		0,
		1.0f,
		1.0f,
		0.0f),
		BLERR_NOERROR);
	AAS_SoundSubsystem_SetFrameTime(1.0f);
	assert_int_equal((int)AAS_SoundSubsystem_SoundEventCount(), 1);

	assert_int_equal(AAS_ConfigureEntityLimits(2, 1), BLERR_NOERROR);
	assert_true(AAS_SoundSubsystem_InfoCount() > 0U);
	assert_int_equal((int)AAS_SoundSubsystem_SoundEventCount(), 0);
}

/*
=============
test_retail_missing_bsp_preserves_navigation_and_bsp_storage

Retail does not enter either loader when BSP discovery fails: it only clears
the loaded flag and entity link heads.
=============
*/
static void test_retail_missing_bsp_preserves_navigation_and_bsp_storage(void **state)
{
	(void)state;
	aas_bspmodel_t *bsp_models =
		(aas_bspmodel_t *)GetClearedMemory(sizeof(*bsp_models));
	aas_area_t *areas = (aas_area_t *)GetClearedMemory(sizeof(*areas));
	assert_non_null(bsp_models);
	assert_non_null(areas);

	aasworld.loaded = qtrue;
	aasworld.numBspModels = 1;
	aasworld.bspModels = bsp_models;
	aasworld.numAreas = 1;
	aasworld.areas = areas;

	assert_int_equal(AAS_LoadMap("__gladiator_missing_bsp_lifetime",
		0,
		NULL,
		0,
		NULL,
		0,
		NULL),
		BLERR_NOBSPFILE);
	assert_false(aasworld.loaded);
	assert_ptr_equal(aasworld.bspModels, bsp_models);
	assert_int_equal(aasworld.numBspModels, 1);
	assert_ptr_equal(aasworld.areas, areas);
	assert_int_equal(aasworld.numAreas, 1);
}

/*
=============
test_retail_missing_aas_releases_only_bsp_storage

Once retail discovers a BSP it clears the prior BSP domain. It does not clear
the prior AAS domain until an AAS candidate itself is found.
=============
*/
static void test_retail_missing_aas_releases_only_bsp_storage(void **state)
{
	(void)state;
	const char *map_name = "__gladiator_missing_aas_lifetime";
	const char *bsp_path = "maps/__gladiator_missing_aas_lifetime.bsp";
	const char *root_aas_path = "__gladiator_missing_aas_lifetime.aas";
	const char *aas_path = "maps/__gladiator_missing_aas_lifetime.aas";
	unlink(root_aas_path);
	unlink(aas_path);
	unlink(bsp_path);
	errno = 0;
	int directory_status = test_make_directory("maps");
	qboolean remove_directory = directory_status == 0;
	assert_true(remove_directory || errno == EEXIST);

	q2_bsp_header_t bsp_header = {0};
	bsp_header.ident = Q2_BSP_IDENT;
	bsp_header.version = Q2_BSP_VERSION;
	assert_true(test_write_fixture(bsp_path, &bsp_header, sizeof(bsp_header)));

	aas_bspmodel_t *bsp_models =
		(aas_bspmodel_t *)GetClearedMemory(sizeof(*bsp_models));
	aas_area_t *areas = (aas_area_t *)GetClearedMemory(sizeof(*areas));
	assert_non_null(bsp_models);
	assert_non_null(areas);
	aasworld.loaded = qtrue;
	aasworld.numBspModels = 1;
	aasworld.bspModels = bsp_models;
	aasworld.numAreas = 1;
	aasworld.areas = areas;

	assert_int_equal(AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL),
		BLERR_NOAASFILE);
	assert_false(aasworld.loaded);
	assert_null(aasworld.bspModels);
	assert_int_equal(aasworld.numBspModels, 0);
	assert_ptr_equal(aasworld.areas, areas);
	assert_int_equal(aasworld.numAreas, 1);

	unlink(bsp_path);
	if (remove_directory)
	{
		rmdir("maps");
	}
}

/*
=============
test_successful_map_commit_retains_parsed_mover_catalogue

Pin mover-catalogue ownership across the AAS clear/commit boundary and discard
the parsed catalogue when a later load fails after reading the BSP entities.
=============
*/
static void test_successful_map_commit_retains_parsed_mover_catalogue(void **state)
{
	(void)state;
	const char *map_name = "__gladiator_mover_catalogue_commit";
	const char *bsp_path = "maps/__gladiator_mover_catalogue_commit.bsp";
	const char *root_aas_path = "__gladiator_mover_catalogue_commit.aas";
	const char *aas_path = "maps/__gladiator_mover_catalogue_commit.aas";
	unlink(bsp_path);
	unlink(root_aas_path);
	unlink(aas_path);
	errno = 0;
	int directory_status = test_make_directory("maps");
	qboolean remove_directory = directory_status == 0;
	assert_true(remove_directory || errno == EEXIST);

	static const char entity_data[] =
		"/* retained BSP lexer comment */\n"
		"{\n"
		"\"classname\" \"worldspawn\"\n"
		"}\n"
		"{\n"
		"\"classname\" /* key/value comment */ \"func_door\"\n"
		"\"model\" \"*1\"\n"
		"\"noise\" \"textures\\metal\\door\"\n"
		"}\n";
	q2_bsp_header_t bsp_header = {0};
	bsp_header.ident = Q2_BSP_IDENT;
	bsp_header.version = Q2_BSP_VERSION;
	bsp_header.lumps[Q2_BSP_LUMP_ENTITIES].offset =
		(int32_t)sizeof(bsp_header);
	bsp_header.lumps[Q2_BSP_LUMP_ENTITIES].length =
		(int32_t)(sizeof(entity_data) - 1U);
	unsigned char bsp_data[sizeof(bsp_header) + sizeof(entity_data) - 1U];
	memcpy(bsp_data, &bsp_header, sizeof(bsp_header));
	memcpy(bsp_data + sizeof(bsp_header),
		entity_data,
		sizeof(entity_data) - 1U);
	assert_true(test_write_fixture(bsp_path, bsp_data, sizeof(bsp_data)));

	q2_aas_header_t aas_header = {0};
	aas_header.ident = Q2_AAS_IDENT;
	aas_header.version = Q2_AAS_VERSION;
	assert_true(test_write_fixture(aas_path, &aas_header, sizeof(aas_header)));

	BotMove_MoverCatalogueReset();
	bot_mover_catalogue_entry_t stale_entry = {0};
	stale_entry.modelnum = 7;
	assert_true(BotMove_MoverCatalogueInsert(&stale_entry));
	assert_int_equal(AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL),
		BLERR_NOERROR);
	assert_true(aasworld.loaded);
	assert_non_null(aasworld.bspEntityData);
	assert_int_equal(aasworld.bspEntityDataSize,
		(int)(sizeof(entity_data) - 1U));
	assert_memory_equal(aasworld.bspEntityData,
		entity_data,
		sizeof(entity_data) - 1U);
	assert_null(BotMove_MoverCatalogueFindByModel(7));
	const bot_mover_catalogue_entry_t *mover =
		BotMove_MoverCatalogueFindByModel(1);
	assert_non_null(mover);
	assert_int_equal(mover->modelnum, 1);
	assert_int_equal(mover->kind, BOT_MOVER_KIND_FUNC_DOOR);

	assert_int_equal(unlink(aas_path), 0);
	assert_int_equal(AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL),
		BLERR_NOAASFILE);
	assert_null(BotMove_MoverCatalogueFindByModel(1));

	unlink(root_aas_path);
	unlink(bsp_path);
	if (remove_directory)
	{
		rmdir("maps");
	}
}

/*
=============
test_bsp_entity_loader_prefers_retained_lump

Pins retail dentdata ownership: generator parsing uses the entity lump retained
by the BSP load and does not require map discovery or a second file read.
=============
*/
static void test_bsp_entity_loader_prefers_retained_lump(void **state)
{
	(void)state;
	static const char entity_data[] =
		"{\n"
		"\"classname\" \"retained_worldspawn\"\n"
		"}\n";

	aasworld.bspEntityData = GetMemory(sizeof(entity_data) - 1U);
	assert_non_null(aasworld.bspEntityData);
	memcpy(aasworld.bspEntityData,
		entity_data,
		sizeof(entity_data) - 1U);
	aasworld.bspEntityDataSize = (int)(sizeof(entity_data) - 1U);
	strncpy(aasworld.mapName,
		"__gladiator_retained_entity_lump_without_bsp",
		sizeof(aasworld.mapName) - 1U);
	aasworld.mapName[sizeof(aasworld.mapName) - 1U] = '\0';

	aas_bspentity_t *entities = AAS_LoadBSPEntities();
	assert_non_null(entities);
	assert_string_equal(AAS_ValueForBSPEpairKey(entities, "classname"),
		"retained_worldspawn");
	AAS_FreeBSPEntities(entities);
}

/*
=============
test_retail_entity_link_heaps_are_fixed_and_reused

Pins the fixed AAS and BSP link heaps, their retail exhaustion diagnostics,
and head-first reuse after a link is returned.
=============
*/
static void test_retail_entity_link_heaps_are_fixed_and_reused(void **state)
{
	(void)state;
	AAS_FreeAASLinkHeap();
	AAS_FreeBSPLinkHeap();
	LibVarSet("max_aaslinks", "1");
	LibVarSet("max_bsplinks", "1");

	test_reset_print_capture();
	AAS_InitAASLinkHeap();
	aas_link_t *aas_link = AAS_AllocAASLink();
	assert_non_null(aas_link);
	assert_null(AAS_AllocAASLink());
	assert_int_equal(g_test_print_count, 1);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	assert_string_equal(g_test_print_message, "empty aas link heap\n");
	AAS_FreeAASLink(aas_link);
	assert_ptr_equal(AAS_AllocAASLink(), aas_link);
	AAS_FreeAASLink(aas_link);

	test_reset_print_capture();
	AAS_InitBSPLinkHeap();
	bsp_link_t *bsp_link = AAS_AllocBSPLink();
	assert_non_null(bsp_link);
	assert_null(AAS_AllocBSPLink());
	assert_int_equal(g_test_print_count, 1);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	assert_string_equal(g_test_print_message, "empty bsp link heap\n");
	AAS_FreeBSPLink(bsp_link);
	assert_ptr_equal(AAS_AllocBSPLink(), bsp_link);
	AAS_FreeBSPLink(bsp_link);

	/* Reinitialization rebuilds retail's free list without reallocating either heap. */
	AAS_FreeAASLinkHeap();
	AAS_FreeBSPLinkHeap();
	LibVarSet("max_aaslinks", "2");
	LibVarSet("max_bsplinks", "2");
	AAS_InitAASLinkHeap();
	AAS_InitBSPLinkHeap();
	aas_link_t *first_aas_link = AAS_AllocAASLink();
	aas_link_t *second_aas_link = AAS_AllocAASLink();
	bsp_link_t *first_bsp_link = AAS_AllocBSPLink();
	bsp_link_t *second_bsp_link = AAS_AllocBSPLink();
	assert_non_null(first_aas_link);
	assert_non_null(second_aas_link);
	assert_non_null(first_bsp_link);
	assert_non_null(second_bsp_link);
	first_aas_link->next_area = second_aas_link;
	first_aas_link->prev_area = second_aas_link;
	first_bsp_link->next_leaf = second_bsp_link;
	first_bsp_link->prev_leaf = second_bsp_link;
	AAS_InitAASLinkHeap();
	AAS_InitBSPLinkHeap();
	assert_ptr_equal(AAS_AllocAASLink(), first_aas_link);
	assert_ptr_equal(AAS_AllocAASLink(), second_aas_link);
	assert_ptr_equal(AAS_AllocBSPLink(), first_bsp_link);
	assert_ptr_equal(AAS_AllocBSPLink(), second_bsp_link);
	assert_ptr_equal(first_aas_link->next_area, second_aas_link);
	assert_ptr_equal(first_aas_link->prev_area, second_aas_link);
	assert_ptr_equal(first_bsp_link->next_leaf, second_bsp_link);
	assert_ptr_equal(first_bsp_link->prev_leaf, second_bsp_link);
	AAS_FreeAASLinkHeap();
	AAS_FreeBSPLinkHeap();
	LibVarSet("max_aaslinks", "1");
	LibVarSet("max_bsplinks", "1");
	AAS_InitAASLinkHeap();
	AAS_InitBSPLinkHeap();

	/* sub_1000a920 keeps an AAS entity update successful with a partial list. */
	aas_area_t areas[3] = {0};
	aas_node_t nodes[2] = {0};
	aas_plane_t planes[1] = {0};
	nodes[1].planenum = 0;
	nodes[1].children[0] = -1;
	nodes[1].children[1] = -2;
	planes[0].normal[0] = 1.0f;
	aasworld.loaded = qtrue;
	/*
	 * The split node hands out area 1 on the front side and area 2 on the back,
	 * so the world has to be three areas wide - retail's numareas counts the
	 * unused zero slot. With numAreas at 2 the second leaf is out of range and
	 * the entity only ever needs one link, which is not the exhaustion this
	 * case exists to pin.
	 */
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numNodes = 2;
	aasworld.nodes = nodes;
	aasworld.numPlanes = 1;
	aasworld.planes = planes;

	AASEntityFrame frame = {0};
	frame.bounds_dirty = true;
	VectorSet(frame.mins, -1.0f, -1.0f, -1.0f);
	VectorSet(frame.maxs, 1.0f, 1.0f, 1.0f);
	test_reset_print_capture();
	assert_int_equal(AAS_UpdateEntity(1, &frame), BLERR_NOERROR);
	assert_int_equal(aasworld.areaEntityListCount, 3U);
	assert_int_equal(g_test_print_count, 1);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	assert_string_equal(g_test_print_message, "empty aas link heap\n");
	assert_non_null(aasworld.entities[1].areas);
	assert_int_equal(aasworld.entities[1].areaOccupancyCount, 1);
	assert_int_equal(aasworld.entities[1].areas->areanum, 1);
	assert_int_equal(AAS_UpdateEntity(1, NULL), BLERR_NOERROR);
	free(aasworld.entities[1].areaOccupancyBits);
	FreeMemory(aasworld.entities);
	FreeMemory(aasworld.areaEntityLists);
	memset(&aasworld, 0, sizeof(aasworld));

	/* sub_10006210 also preserves its partial BSP leaf list on exhaustion. */
	aas_bspmodel_t bsp_models[1] = {0};
	aas_bspnode_t bsp_nodes[1] = {0};
	aas_bspleaf_t bsp_leaves[2] = {0};
	aas_plane_t bsp_planes[1] = {0};
	bsp_models[0].headnode = 0;
	bsp_nodes[0].planenum = 0;
	bsp_nodes[0].children[0] = -1;
	bsp_nodes[0].children[1] = -2;
	bsp_planes[0].normal[0] = 1.0f;
	aasworld.loaded = qtrue;
	aasworld.numBspModels = 1;
	aasworld.bspModels = bsp_models;
	aasworld.numBspNodes = 1;
	aasworld.bspNodes = bsp_nodes;
	aasworld.numBspLeaves = 2;
	aasworld.bspLeaves = bsp_leaves;
	aasworld.numBspPlanes = 1;
	aasworld.bspPlanes = bsp_planes;
	test_reset_print_capture();
	assert_int_equal(AAS_UpdateEntity(1, &frame), BLERR_NOERROR);
	assert_int_equal(g_test_print_count, 1);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	assert_string_equal(g_test_print_message, "empty bsp link heap\n");
	assert_non_null(aasworld.entities[1].leaves);
	assert_int_equal(aasworld.entities[1].leaves->leafnum, 0);
	assert_int_equal(AAS_UpdateEntity(1, NULL), BLERR_NOERROR);
	FreeMemory(aasworld.entities);
	free(aasworld.bspLeafEntityLists);
	memset(&aasworld, 0, sizeof(aasworld));

	AAS_FreeAASLinkHeap();
	AAS_FreeBSPLinkHeap();
	LibVarSet("max_aaslinks", "4096");
	LibVarSet("max_bsplinks", "4096");
}

static void test_routing_frame_respects_framereachability(void **state)
{
    (void)state;

    LibVarSet("forcewrite", "0");
    LibVarSet("framereachability", "0");

    AAS_RouteFrameResetDiagnostics();
    AAS_RouteFrameUpdate();

    assert_int_equal(AAS_RouteFrameWorkCounter(), 0);
    assert_int_equal(AAS_RouteFrameSkipCounter(), 1);
    assert_int_equal(AAS_RouteFrameLastBudget(), 0);
    assert_false(AAS_RouteFrameForceWriteActive());

    LibVarSet("framereachability", "8");

    AAS_RouteFrameResetDiagnostics();
    AAS_RouteFrameUpdate();

    assert_int_equal(AAS_RouteFrameWorkCounter(), 1);
    assert_int_equal(AAS_RouteFrameSkipCounter(), 0);
    assert_int_equal(AAS_RouteFrameLastBudget(), 8);

    LibVarSet("framereachability", "0");
}

static void test_routing_frame_forcewrite_toggle(void **state)
{
    (void)state;

    LibVarSet("framereachability", "4");

    LibVarSet("forcewrite", "0");
    AAS_RouteFrameResetDiagnostics();
    AAS_RouteFrameUpdate();
    assert_false(AAS_RouteFrameForceWriteActive());

    LibVarSet("forcewrite", "1");
    AAS_RouteFrameResetDiagnostics();
    AAS_RouteFrameUpdate();
    assert_true(AAS_RouteFrameForceWriteActive());

    LibVarSet("forcewrite", "0");
    LibVarSet("framereachability", "0");
}

static void test_reachability_force_reachability_toggle(void **state)
{
    (void)state;

    LibVarSet("forcereachability", "0");
    LibVarSet("forceclustering", "0");

    AAS_ReachabilityFrameResetDiagnostics();
    AAS_ReachabilityFrameUpdate();

    assert_int_equal(AAS_ReachabilityFrameWorkCounter(), 0);
    assert_int_equal(AAS_ReachabilityFrameSkipCounter(), 1);
    assert_false(AAS_ReachabilityForceReachabilityActive());
    assert_false(AAS_ReachabilityForceClusteringActive());

    LibVarSet("forcereachability", "1");

    AAS_ReachabilityFrameResetDiagnostics();
    AAS_ReachabilityFrameUpdate();

    assert_int_equal(AAS_ReachabilityFrameWorkCounter(), 1);
    assert_true(AAS_ReachabilityForceReachabilityActive());

    LibVarSet("forcereachability", "0");
}

static void test_reachability_force_clustering_toggle(void **state)
{
    (void)state;

    LibVarSet("forcereachability", "0");
    LibVarSet("forceclustering", "1");

    AAS_ReachabilityFrameResetDiagnostics();
    AAS_ReachabilityFrameUpdate();

    assert_int_equal(AAS_ReachabilityFrameWorkCounter(), 1);
    assert_true(AAS_ReachabilityForceClusteringActive());
    assert_false(AAS_ReachabilityForceReachabilityActive());

    LibVarSet("forceclustering", "0");
}

/*
=============
test_reachability_geometry_helpers

Validates the Q3-derived face center, face area, and convex area volume helpers
against an in-memory unit tetrahedron.
=============
*/
static void test_reachability_geometry_helpers(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[2] = {0};
	aas_vertex_t vertexes[4] = {
		{0.0f, 0.0f, 0.0f},
		{1.0f, 0.0f, 0.0f},
		{0.0f, 1.0f, 0.0f},
		{0.0f, 0.0f, 1.0f}
	};
	aas_edge_t edges[12] = {
		{{0, 1}}, {{1, 2}}, {{2, 0}},
		{{0, 3}}, {{3, 1}}, {{1, 0}},
		{{0, 2}}, {{2, 3}}, {{3, 0}},
		{{1, 3}}, {{3, 2}}, {{2, 1}}
	};
	int edge_index[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
	aas_face_t faces[4] = {0};
	int face_index[4] = {0, 1, 2, 3};
	aas_plane_t planes[4] = {0};

	areas[1].areanum = 1;
	areas[1].numfaces = 4;
	areas[1].firstface = 0;
	for (int index = 0; index < 4; ++index)
	{
		faces[index].planenum = index;
		faces[index].numedges = 3;
		faces[index].firstedge = index * 3;
		faces[index].backarea = 1;
	}
	planes[0].normal[2] = 1.0f;
	planes[1].normal[1] = 1.0f;
	planes[2].normal[0] = 1.0f;
	planes[3].normal[0] = 0.577350269f;
	planes[3].normal[1] = 0.577350269f;
	planes[3].normal[2] = 0.577350269f;
	planes[3].dist = 0.577350269f;

	aasworld.areas = areas;
	aasworld.numAreas = 1;
	aasworld.vertexes = vertexes;
	aasworld.numVertexes = 4;
	aasworld.edges = edges;
	aasworld.numEdges = 12;
	aasworld.edgeIndex = edge_index;
	aasworld.edgeIndexSize = 12;
	aasworld.faces = faces;
	aasworld.numFaces = 4;
	aasworld.faceIndex = face_index;
	aasworld.faceIndexSize = 4;
	aasworld.planes = planes;
	aasworld.numPlanes = 4;

	vec3_t center;
	AAS_FaceCenter(0, center);
	assert_float_equal(center[0], 1.0f / 3.0f, 0.0001f);
	assert_float_equal(center[1], 1.0f / 3.0f, 0.0001f);
	assert_float_equal(center[2], 0.0f, 0.0001f);
	assert_float_equal(AAS_FaceArea(&faces[3]), 0.8660254f, 0.0001f);
	assert_float_equal(AAS_AreaVolume(1), 1.0f / 6.0f, 0.0001f);

	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_area_and_link_helpers

Exercises generator predicates, linked-area selection, and loaded reachability
duplicate detection without external map assets.
=============
*/
static void test_reachability_area_and_link_helpers(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[4] = {0};
	aas_areasettings_t settings[4] = {0};
	aas_reachability_t reachability[3] = {0};
	settings[1].firstreachablearea = 1;
	settings[1].numreachableareas = 2;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[3].areaflags = AAS_AREA_LIQUID;
	settings[3].contents = AAS_AREACONTENTS_LAVA |
		AAS_AREACONTENTS_SLIME |
		AAS_AREACONTENTS_TELEPORTER;
	reachability[1].areanum = 2;
	reachability[2].areanum = 3;
	VectorSet(areas[2].mins, 50.0f, -8.0f, -32.0f);
	VectorSet(areas[2].maxs, 60.0f, 8.0f, 32.0f);
	VectorSet(areas[3].mins, 70.0f, -8.0f, -32.0f);
	VectorSet(areas[3].maxs, 80.0f, 8.0f, 32.0f);
	aasworld.loaded = qtrue;
	aasworld.areas = areas;
	aasworld.areasettings = settings;
	aasworld.numAreaSettings = 4;
	aasworld.reachability = reachability;
	aasworld.numReachability = 3;
	aasworld.numAreas = 4;

	assert_true(AAS_AreaGrounded(2));
	assert_true(AAS_AreaLiquid(3));
	assert_int_equal(AAS_AreaLava(3), AAS_AREACONTENTS_LAVA);
	assert_int_equal(AAS_AreaSlime(3), AAS_AREACONTENTS_SLIME);
	assert_int_equal(AAS_AreaTeleporter(3), AAS_AREACONTENTS_TELEPORTER);
	assert_true(AAS_ReachabilityExists(1, 2));
	assert_true(AAS_ReachabilityExists(1, 3));
	assert_false(AAS_ReachabilityExists(2, 1));

	aas_link_t second = {0};
	second.areanum = 2;
	aas_link_t first = {0};
	first.areanum = 1;
	first.next_area = &second;
	assert_int_equal(AAS_BestReachableLinkArea(&first), 2);
	first.next_area = NULL;
	assert_int_equal(AAS_BestReachableLinkArea(&first), 1);
	aas_entity_t entity_slots[1] = {0};
	entity_slots[0].areas = &first;
	aasworld.entities = entity_slots;
	aasworld.maxEntities = 1;
	assert_int_equal(AAS_BestReachableEntityArea(0), 1);
	assert_int_equal(AAS_BestReachableEntityArea(1), 0);
	aasworld.entities = NULL;
	aasworld.maxEntities = 0;

	/* Retail's reset helper only nulls each entity's link ownership fields. */
	AAS_InitAASLinkHeap();
	AAS_InitBSPLinkHeap();
	aas_link_t *area_link = AAS_AllocAASLink();
	bsp_link_t *leaf_link = AAS_AllocBSPLink();
	assert_non_null(area_link);
	assert_non_null(leaf_link);
	memset(area_link, 0, sizeof(*area_link));
	memset(leaf_link, 0, sizeof(*leaf_link));
	aas_link_t *area_heads[1] = {area_link};
	bsp_link_t *leaf_heads[1] = {leaf_link};
	entity_slots[0].areas = area_link;
	entity_slots[0].leaves = leaf_link;
	area_link->areanum = 0;
	leaf_link->leafnum = 0;
	aasworld.entities = entity_slots;
	aasworld.maxEntities = 1;
	aasworld.areaEntityLists = area_heads;
	aasworld.areaEntityListCount = 1U;
	aasworld.bspLeafEntityLists = leaf_heads;
	aasworld.bspLeafEntityListCount = 1U;
	AAS_ResetEntityLinks();
	assert_null(entity_slots[0].areas);
	assert_null(entity_slots[0].leaves);
	assert_ptr_equal(area_heads[0], area_link);
	assert_ptr_equal(leaf_heads[0], leaf_link);
	aasworld.entities = NULL;
	aasworld.maxEntities = 0;
	aasworld.areaEntityLists = NULL;
	aasworld.areaEntityListCount = 0U;
	aasworld.bspLeafEntityLists = NULL;
	aasworld.bspLeafEntityListCount = 0U;
	AAS_FreeAASLinkHeap();
	AAS_FreeBSPLinkHeap();

	/*
	 * sub_1000b300 uses a temporary AAS entity-link list for the bbox
	 * fallback.  sub_1001c460 prepends each visited leaf, so two grounded
	 * leaves choose the second traversal leaf first.
	 */
	aas_node_t nodes[3] = {0};
	aas_plane_t link_planes[2] = {0};
	link_planes[0].normal[0] = 1.0f;
	link_planes[1].normal[1] = 1.0f;
	nodes[1].planenum = 0;
	nodes[1].children[0] = 2;
	nodes[1].children[1] = 0;
	nodes[2].planenum = 1;
	nodes[2].children[0] = -1;
	nodes[2].children[1] = -2;
	aasworld.nodes = nodes;
	aasworld.numNodes = 3;
	aasworld.planes = link_planes;
	aasworld.numPlanes = 2;
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	vec3_t unlinked_origin = {-64.0f, 0.0f, 0.0f};
	vec3_t unlinked_mins = {-128.0f, -8.0f, -8.0f};
	vec3_t unlinked_maxs = {128.0f, 8.0f, 8.0f};
	vec3_t best_goal_origin;
	assert_int_equal(AAS_BestReachableArea(unlinked_origin,
		unlinked_mins,
		unlinked_maxs,
		best_goal_origin),
		2);
	assert_float_equal(best_goal_origin[0], -64.0f, 0.001f);
	aasworld.nodes = NULL;
	aasworld.numNodes = 0;
	aasworld.planes = NULL;
	aasworld.numPlanes = 0;

	vec3_t start = {0.0f, 0.0f, 0.0f};
	vec3_t end = {10.0f, 0.0f, 0.0f};
	settings[3].areaflags = 0;
	assert_true(AAS_NearbySolidOrGap(start, end));
	settings[3].areaflags = AAS_AREA_GROUNDED;
	assert_false(AAS_NearbySolidOrGap(start, end));
	areas[2].mins[0] = 90.0f;
	areas[2].maxs[0] = 100.0f;
	assert_true(AAS_NearbySolidOrGap(start, end));

	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_bridge_config_retail_maxwaterjump_default

Pins the retail sv_maxwaterjump fallback cached during bridge startup.
=============
*/
static void test_bridge_config_retail_maxwaterjump_default(void **state)
{
	(void)state;
	test_reset_print_capture();
	BotInterface_SetImportTable(&g_test_imports);
	assert_true(BotMemory_Init(TEST_BOTLIB_HEAP_SIZE));
	LibVar_Init();
	assert_true(BridgeConfig_Init());

	libvar_t *maxwaterjump = Bridge_MaxWaterJump();
	assert_non_null(maxwaterjump);
	assert_non_null(maxwaterjump->string);
	assert_string_equal(maxwaterjump->string, "21");
	assert_float_equal(maxwaterjump->value, 21.0f, 0.0001f);

	BridgeConfig_Shutdown();
	LibVar_Shutdown();
	BotInterface_SetImportTable(NULL);
}

/*
=============
test_reachability_physics_helpers

Pins the retail fallback gravity, velocity, fall-damage, and jump estimates used
before bridge tuning variables are available.
=============
*/
static void test_reachability_physics_helpers(void **state)
{
	(void)state;
	assert_int_equal(AAS_FallDamageDistance(), 187);
	assert_float_equal(AAS_FallDelta(100.0f), 16.0f, 0.0001f);
	assert_float_equal(AAS_MaxJumpHeight(224.0f), 31.36f, 0.0001f);
	assert_float_equal(AAS_MaxJumpDistance(224.0f), 402.198f, 0.001f);
	assert_int_equal(AAS_BarrierJumpTravelTime(), 2);

	vec3_t first_start = {0.0f, 0.0f, 0.0f};
	vec3_t first_end = {10.0f, 0.0f, 0.0f};
	vec3_t second_start = {0.0f, 5.0f, 0.0f};
	vec3_t second_end = {10.0f, 5.0f, 0.0f};
	aas_plane_t plane = {0};
	plane.normal[2] = 1.0f;
	vec3_t beststart1 = {0.0f, 0.0f, 0.0f};
	vec3_t bestend1 = {0.0f, 0.0f, 0.0f};
	vec3_t beststart2 = {0.0f, 0.0f, 0.0f};
	vec3_t bestend2 = {0.0f, 0.0f, 0.0f};
	float edgedistance = AAS_ClosestEdgePoints(first_start,
		first_end,
		second_start,
		second_end,
		&plane,
		&plane,
		beststart1,
		bestend1,
		beststart2,
		bestend2,
		99999.0f);
	assert_float_equal(edgedistance, 5.0f, 0.0001f);
	assert_float_equal(beststart1[0], 10.0f, 0.0001f);
	assert_float_equal(beststart1[1], 0.0f, 0.0001f);
	assert_float_equal(bestend1[0], 10.0f, 0.0001f);
	assert_float_equal(bestend1[1], 5.0f, 0.0001f);
}

/*
=============
test_reachability_swim_generation_and_storage

Builds two liquid areas around one shared face and verifies the retail swim
link, small-volume penalty, duplicate lookup, and one-based storage metadata.
=============
*/
static void test_reachability_swim_generation_and_storage(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_vertex_t vertexes[3] = {
		{0.0f, 0.0f, 0.0f},
		{0.0f, 8.0f, 0.0f},
		{0.0f, 0.0f, 8.0f}
	};
	aas_edge_t edges[3] = {{{0, 1}}, {{1, 2}}, {{2, 0}}};
	int edge_index[3] = {0, 1, 2};
	aas_face_t faces[2] = {0};
	int face_index[2] = {1, -1};
	aas_plane_t planes[2] = {0};
	aas_bspmodel_t bsp_models[1] = {0};
	aas_bspleaf_t bsp_leaves[1] = {0};
	unsigned short bsp_leaf_brushes[1] = {0};
	aas_plane_t bsp_planes[6] = {0};
	aas_bspbrushside_t bsp_brush_sides[6] = {0};
	aas_bspbrush_t bsp_brushes[1] = {0};

	areas[1].areanum = 1;
	areas[1].numfaces = 1;
	areas[1].firstface = 0;
	VectorSet(areas[1].mins, -10.0f, -1.0f, -1.0f);
	VectorSet(areas[1].maxs, 0.0f, 9.0f, 9.0f);
	areas[2].areanum = 2;
	areas[2].numfaces = 1;
	areas[2].firstface = 1;
	VectorSet(areas[2].mins, 0.0f, -1.0f, -1.0f);
	VectorSet(areas[2].maxs, 10.0f, 9.0f, 9.0f);
	settings[1].areaflags = AAS_AREA_LIQUID;
	settings[1].presencetype = PRESENCE_NORMAL;
	settings[2].areaflags = AAS_AREA_LIQUID;
	settings[2].presencetype = PRESENCE_NORMAL;
	faces[1].planenum = 0;
	faces[1].numedges = 3;
	faces[1].firstedge = 0;
	faces[1].frontarea = 2;
	faces[1].backarea = 1;
	planes[0].normal[0] = 1.0f;
	planes[1].normal[0] = -1.0f;
	bsp_models[0].headnode = -1;
	bsp_leaves[0].firstleafbrush = 0;
	bsp_leaves[0].numleafbrushes = 1;
	VectorSet(bsp_planes[0].normal, 1.0f, 0.0f, 0.0f);
	bsp_planes[0].dist = 100.0f;
	VectorSet(bsp_planes[1].normal, -1.0f, 0.0f, 0.0f);
	bsp_planes[1].dist = 100.0f;
	VectorSet(bsp_planes[2].normal, 0.0f, 1.0f, 0.0f);
	bsp_planes[2].dist = 100.0f;
	VectorSet(bsp_planes[3].normal, 0.0f, -1.0f, 0.0f);
	bsp_planes[3].dist = 100.0f;
	VectorSet(bsp_planes[4].normal, 0.0f, 0.0f, 1.0f);
	bsp_planes[4].dist = 100.0f;
	VectorSet(bsp_planes[5].normal, 0.0f, 0.0f, -1.0f);
	bsp_planes[5].dist = 100.0f;
	for (int side = 0; side < 6; ++side)
	{
		bsp_brush_sides[side].planenum = (unsigned short)side;
	}
	bsp_brushes[0].firstside = 0;
	bsp_brushes[0].numsides = 6;
	bsp_brushes[0].contents = CONTENTS_WATER;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numVertexes = 3;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = 3;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 3;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 2;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 2;
	aasworld.faceIndex = face_index;
	aasworld.numPlanes = 2;
	aasworld.planes = planes;
	aasworld.numBspModels = 1;
	aasworld.bspModels = bsp_models;
	aasworld.numBspLeaves = 1;
	aasworld.bspLeaves = bsp_leaves;
	aasworld.bspLeafBrushIndexSize = 1;
	aasworld.bspLeafBrushes = bsp_leaf_brushes;
	aasworld.numBspPlanes = 6;
	aasworld.bspPlanes = bsp_planes;
	aasworld.numBspBrushSides = 6;
	aasworld.bspBrushSides = bsp_brush_sides;
	aasworld.numBspBrushes = 1;
	aasworld.bspBrushes = bsp_brushes;

	/*
	 * Retail sub_10003080 is a bare tail call into the bot_import
	 * PointContents slot ("1000308e  return data_10063ff0(arg1)"), so the
	 * engine callback - not the DLL's own BSP walk - decides the answer.
	 */
	Q2Bridge_SetImportTable(&g_test_q2_imports);
	g_test_gap_trace_enabled = qfalse;
	g_test_point_contents = CONTENTS_WATER;

	AAS_InitReachability();
	assert_int_equal(AAS_PointContents(vertexes[0]), CONTENTS_WATER);
	assert_true(AAS_Reachability_Swim(1, 2));
	assert_true(AAS_ReachabilityExists(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(settings[1].firstreachablearea, 1);
	assert_int_equal(settings[1].numreachableareas, 1);
	assert_int_equal(aasworld.reachability[0].areanum, 0);
	assert_int_equal(aasworld.reachability[1].areanum, 2);
	assert_int_equal(aasworld.reachability[1].facenum, 1);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_SWIM);
	assert_int_equal(aasworld.reachability[1].traveltime, 201);
	assert_float_equal(aasworld.reachability[1].end[0], 2.0f, 0.0001f);

	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	FreeMemory(aasworld.reachability);
	g_test_point_contents = 0;
	Q2Bridge_SetImportTable(NULL);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_equal_floor_generation_and_storage

Pins lowest/longest shared-edge walk generation, inside offsets, destination
crouch startup cost, and flattened area reachability spans.
=============
*/
static void test_reachability_equal_floor_generation_and_storage(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_vertex_t vertexes[4] = {
		{0.0f, 0.0f, 0.0f},
		{0.0f, 10.0f, 0.0f},
		{-10.0f, 0.0f, 0.0f},
		{10.0f, 0.0f, 0.0f}
	};
	aas_edge_t edges[6] = {
		{{0, 0}}, {{0, 1}}, {{1, 2}},
		{{2, 0}}, {{0, 3}}, {{3, 1}}
	};
	int edge_index[6] = {-1, 2, 3, 1, 4, 5};
	aas_face_t faces[3] = {0};
	int face_index[2] = {1, 2};
	aas_plane_t planes[1] = {0};

	areas[1].areanum = 1;
	areas[1].numfaces = 1;
	areas[1].firstface = 0;
	VectorSet(areas[1].mins, -10.0f, 0.0f, 0.0f);
	VectorSet(areas[1].maxs, 0.0f, 10.0f, 0.0f);
	areas[2].areanum = 2;
	areas[2].numfaces = 1;
	areas[2].firstface = 1;
	VectorSet(areas[2].mins, 0.0f, 0.0f, 0.0f);
	VectorSet(areas[2].maxs, 10.0f, 10.0f, 0.0f);
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_NORMAL;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[2].presencetype = PRESENCE_CROUCH;
	faces[1].planenum = 0;
	faces[1].faceflags = AAS_FACE_GROUND;
	faces[1].numedges = 3;
	faces[1].firstedge = 0;
	faces[2].planenum = 0;
	faces[2].faceflags = AAS_FACE_GROUND;
	faces[2].numedges = 3;
	faces[2].firstedge = 3;
	planes[0].normal[2] = 1.0f;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numVertexes = 4;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = 6;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 6;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 3;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 2;
	aasworld.faceIndex = face_index;
	aasworld.numPlanes = 1;
	aasworld.planes = planes;

	AAS_InitReachability();
	assert_true(AAS_Reachability_EqualFloorHeight(1, 2));
	assert_true(AAS_ReachabilityExists(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(settings[1].firstreachablearea, 1);
	assert_int_equal(settings[1].numreachableareas, 1);
	assert_int_equal(aasworld.reachability[0].areanum, 0);
	assert_int_equal(aasworld.reachability[1].areanum, 2);
	assert_int_equal(aasworld.reachability[1].edgenum, -1);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_WALK);
	/*
	 * Retail seeds the equal-floor link with 1 and then applies three
	 * surcharges after prepending the record (sub_10011a20):
	 *   1001200a/10012020  +0x12c when the destination is crouch-only,
	 *   10012037/10012039  +0x64 when AAS_NearbySolidOrGap(start, end) is false,
	 *   10012040/10012055  +0x64 when AAS_AreaGroundFaceArea(areanum) < 500.
	 * Area 2's ground face here is a 50-unit triangle, so the third fires; the
	 * destination edge runs off the end of the fixture world, so sub_10011740
	 * reports the gap and the second does not: 1 + 300 + 100 = 401.
	 */
	assert_int_equal(aasworld.reachability[1].traveltime, 401);
	assert_float_equal(aasworld.reachability[1].start[0], 0.1f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[0], 5.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[2], 0.125f, 0.0001f);

	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	FreeMemory(aasworld.reachability);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_adjacent_edge_travel_branches

Builds paired coplanar boundary edges at several height differences and pins
the retail step, barrier, downhill-walk, water-jump, and walk-off-ledge branch
ordering, endpoints, costs, and one-based storage.
=============
*/
static void test_reachability_adjacent_edge_travel_branches(void **state)
{
	(void)state;
	typedef struct
	{
		float height;
		qboolean source_liquid;
		int traveltype;
		int traveltime;
		float start_x;
		float end_x;
	} adjacent_case_t;
	/*
	 * Retail sub_10012200 hard-codes every one of these costs; none of them
	 * comes from an rs_* libvar.
	 *
	 * - step-up walk (0x100130bd type 2): seeded with 1 at 0x100130c5, then
	 *   +400 at 0x10013118 when AAS_NearbySolidOrGap(start, end) is false and
	 *   +400 at 0x10013134 when AAS_AreaGroundFaceArea(areanum) < 500. This
	 *   fixture's destination edge runs off the end of the world, so
	 *   sub_10011740 finds the gap and returns true (no first penalty), while
	 *   the single-edge ground face has zero area, so only the second fires:
	 *   1 + 400 = 401.
	 * - barrier jump (0x100133a6 type 4): flat 0x190 at 0x100133ad.
	 * - downhill walk (0x10013473 type 2): flat 1 at 0x1001347a, with no
	 *   solid/gap or ground-area penalty on this branch.
	 * - water jump (0x10013279 type 9): flat 0x2bc at 0x10013280.
	 * - walk off ledge (0x10013631 type 7): flat 0x64 at 0x10013638, with no
	 *   gravity term and no fall-damage surcharge.
	 */
	const adjacent_case_t cases[] = {
		{8.0f, qfalse, TRAVEL_WALK, 401, 0.1f, 5.0f},
		{30.0f, qfalse, TRAVEL_BARRIERJUMP, 400, 0.1f, 5.0f},
		{-8.0f, qfalse, TRAVEL_WALK, 1, 0.1f, 5.0f},
		{30.0f, qtrue, TRAVEL_WATERJUMP, 700, 0.0f, 15.0f},
		{-40.0f, qfalse, TRAVEL_WALKOFFLEDGE, 100, 0.0f, 2.0f}
	};

	for (size_t case_index = 0;
		case_index < sizeof(cases) / sizeof(cases[0]);
		++case_index)
	{
		const adjacent_case_t *testcase = &cases[case_index];
		memset(&aasworld, 0, sizeof(aasworld));
		aas_area_t areas[3] = {0};
		aas_areasettings_t settings[3] = {0};
		aas_vertex_t vertexes[4] = {
			{0.0f, 0.0f, 0.0f},
			{0.0f, 10.0f, 0.0f},
			{0.0f, 0.0f, 0.0f},
			{0.0f, 10.0f, 0.0f}
		};
		vertexes[2][2] = testcase->height;
		vertexes[3][2] = testcase->height;
		aas_edge_t edges[3] = {{{0, 0}}, {{0, 1}}, {{2, 3}}};
		int edge_index[2] = {-1, 2};
		aas_face_t faces[3] = {0};
		int face_index[2] = {testcase->source_liquid ? -1 : 1, 2};
		aas_plane_t planes[2] = {0};

		areas[1].areanum = 1;
		areas[1].firstface = 0;
		areas[1].numfaces = 1;
		VectorSet(areas[1].mins, -10.0f, -1.0f, -1.0f);
		VectorSet(areas[1].maxs, 0.0f, 11.0f, 32.0f);
		areas[2].areanum = 2;
		areas[2].firstface = 1;
		areas[2].numfaces = 1;
		VectorSet(areas[2].mins, 0.0f, -1.0f, testcase->height - 1.0f);
		VectorSet(areas[2].maxs, 10.0f, 11.0f,
			testcase->height + 32.0f > 4.0f ?
				testcase->height + 32.0f : 4.0f);
		settings[1].areaflags = testcase->source_liquid ?
			AAS_AREA_LIQUID : AAS_AREA_GROUNDED;
		settings[1].presencetype = PRESENCE_NORMAL;
		settings[2].areaflags = AAS_AREA_GROUNDED;
		settings[2].presencetype = PRESENCE_NORMAL;
		faces[1].planenum = 0;
		faces[1].faceflags = testcase->source_liquid ? 0 : AAS_FACE_GROUND;
		faces[1].firstedge = 0;
		faces[1].numedges = 1;
		faces[2].planenum = 0;
		faces[2].faceflags = AAS_FACE_GROUND;
		faces[2].firstedge = 1;
		faces[2].numedges = 1;
		planes[0].normal[2] = 1.0f;
		planes[1].normal[2] = 1.0f;

		aasworld.loaded = qtrue;
		aasworld.numAreas = 3;
		aasworld.areas = areas;
		aasworld.numAreaSettings = 3;
		aasworld.areasettings = settings;
		aasworld.numVertexes = 4;
		aasworld.vertexes = vertexes;
		aasworld.numEdges = 3;
		aasworld.edges = edges;
		aasworld.edgeIndexSize = 2;
		aasworld.edgeIndex = edge_index;
		aasworld.numFaces = 3;
		aasworld.faces = faces;
		aasworld.faceIndexSize = 2;
		aasworld.faceIndex = face_index;
		aasworld.numPlanes = 2;
		aasworld.planes = planes;

		AAS_InitReachability();
		assert_true(AAS_Reachability_Step_Barrier_WaterJump_WalkOffLedge(1, 2));
		AAS_StoreReachability();
		assert_int_equal(aasworld.numReachability, 2);
		assert_int_equal(settings[1].firstreachablearea, 1);
		assert_int_equal(settings[1].numreachableareas, 1);
		assert_int_equal(aasworld.reachability[1].areanum, 2);
		assert_int_equal(aasworld.reachability[1].traveltype, testcase->traveltype);
		assert_int_equal(aasworld.reachability[1].traveltime, testcase->traveltime);
		assert_float_equal(aasworld.reachability[1].start[0],
			testcase->start_x, 0.0001f);
		assert_float_equal(aasworld.reachability[1].end[0],
			testcase->end_x, 0.0001f);

		AAS_ShutDownReachabilityHeap();
		AAS_ClearReachabilityData();
		FreeMemory(aasworld.reachability);
		memset(&aasworld, 0, sizeof(aasworld));
	}
}

/*
=============
test_reachability_jump_generation_and_rejections

Pins closest-edge jump prediction across a synthetic gap, the retail false
return after successful linking, stored jump cost, and crouch-area rejection.
=============
*/
static void test_reachability_jump_generation_and_rejections(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	Q2Bridge_SetImportTable(&g_test_q2_imports);
	g_test_gap_trace_enabled = qtrue;
	g_test_source_ground_max_x = 0.0f;
	g_test_destination_ground_min_x = 64.0f;
	g_test_ground_height = 0.0f;
	g_test_destination_ground_height = 0.0f;
	g_test_point_contents = 0;

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_vertex_t vertexes[4] = {
		{0.0f, 0.0f, 0.0f},
		{0.0f, 10.0f, 0.0f},
		{64.0f, 0.0f, 0.0f},
		{64.0f, 10.0f, 0.0f}
	};
	aas_edge_t edges[3] = {{{0, 0}}, {{0, 1}}, {{2, 3}}};
	int edge_index[2] = {1, 2};
	aas_face_t faces[3] = {0};
	int face_index[2] = {1, 2};
	aas_plane_t planes[3] = {0};
	aas_node_t nodes[5] = {0};

	areas[1].areanum = 1;
	areas[1].firstface = 0;
	areas[1].numfaces = 1;
	VectorSet(areas[1].mins, -20.0f, -2.0f, -2.0f);
	VectorSet(areas[1].maxs, 0.0f, 12.0f, 32.0f);
	areas[2].areanum = 2;
	areas[2].firstface = 1;
	areas[2].numfaces = 1;
	VectorSet(areas[2].mins, 64.0f, -2.0f, -2.0f);
	VectorSet(areas[2].maxs, 100.0f, 12.0f, 32.0f);
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_NORMAL;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[2].presencetype = PRESENCE_NORMAL;
	faces[1].planenum = 0;
	faces[1].faceflags = AAS_FACE_GROUND;
	faces[1].firstedge = 0;
	faces[1].numedges = 1;
	faces[2].planenum = 0;
	faces[2].faceflags = AAS_FACE_GROUND;
	faces[2].firstedge = 1;
	faces[2].numedges = 1;
	planes[0].normal[2] = 1.0f;

	/*
	 * Retail AAS_TraceClientBBox (sub_1001b260) walks the AAS node tree rather
	 * than the engine trace, so the probe jump only lands if this fixture
	 * supplies a tree.  Model the real shape of the case: solid under both
	 * ledges, open air over the 64 unit gap between them.
	 *
	 *   plane 0  z = 0     ground level (also the two ground faces above)
	 *   plane 1  x = 0     source ledge lip
	 *   plane 2  x = 64    destination ledge lip
	 *
	 *   node 1   z > 0 -> node 2 (air)        z < 0 -> node 3 (below ground)
	 *   node 2   x > 64 -> area 2             x < 64 -> area 1
	 *   node 3   x > 0  -> node 4             x < 0  -> solid (floor of area 1)
	 *   node 4   x > 64 -> solid (floor of area 2)   otherwise -> area 1
	 *
	 * Node 4's open leaf is what makes the gap a gap: a bot that drops into it
	 * never meets a solid leaf, so the prediction burns all 30 frames and the
	 * link is rejected, exactly as a bottomless pit would behave.
	 */
	planes[1].normal[0] = 1.0f;
	planes[1].dist = 0.0f;
	planes[2].normal[0] = 1.0f;
	planes[2].dist = 64.0f;
	nodes[1].planenum = 0;
	nodes[1].children[0] = 2;
	nodes[1].children[1] = 3;
	nodes[2].planenum = 2;
	nodes[2].children[0] = -2;
	nodes[2].children[1] = -1;
	nodes[3].planenum = 1;
	nodes[3].children[0] = 4;
	nodes[3].children[1] = 0;
	nodes[4].planenum = 2;
	nodes[4].children[0] = 0;
	nodes[4].children[1] = -1;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numVertexes = 4;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = 3;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 2;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 3;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 2;
	aasworld.faceIndex = face_index;
	aasworld.numPlanes = 3;
	aasworld.planes = planes;
	aasworld.numNodes = 5;
	aasworld.nodes = nodes;

	AAS_InitReachability();
	/*
	 * Retail returns false from AAS_Reachability_Jump even when it links the
	 * pair; the link itself is the observable effect, so check the heap rather
	 * than the return value.
	 */
	assert_false(AAS_Reachability_Jump(1, 2));
	assert_true(AAS_ReachabilityExists(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_JUMP);
	/*
	 * Retail costs jumps and walk-off-ledges with one unconditional expression
	 * at 0x10014a8f: (int)(VectorDistance(bestend, beststart) * 240 /
	 * sv_maxwalkvelocity + 600). sv_maxwalkvelocity is created with the default
	 * string at 0x10037a91, so a 64-unit gap costs 64 * 240 / 300 + 600 = 651.
	 * There is no rs_startjump term and no fall-damage surcharge.
	 */
	assert_int_equal(aasworld.reachability[1].traveltime, 651);
	assert_float_equal(aasworld.reachability[1].start[0], 0.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[0], 64.0f, 0.0001f);

	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	FreeMemory(aasworld.reachability);
	aasworld.reachability = NULL;
	aasworld.numReachability = 0;
	settings[2].presencetype = PRESENCE_CROUCH;
	AAS_InitReachability();
	assert_false(AAS_Reachability_Jump(1, 2));
	assert_false(AAS_ReachabilityExists(1, 2));

	AAS_ShutDownReachabilityHeap();
	g_test_gap_trace_enabled = qfalse;
	Q2Bridge_SetImportTable(NULL);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_aas_against_ladder_retail_geometry_contract

Pins the retail boundary-probe order, ladder area/presence guards, signed face
plane selection, strict three-unit distance, and 0.1 face epsilon.
=============
*/
static void test_aas_against_ladder_retail_geometry_contract(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	assert_false(AAS_AgainstLadder(NULL));

	aas_area_t areas[2] = {0};
	aas_areasettings_t settings[2] = {0};
	aas_vertex_t vertexes[4] = {
		{0.0f, 10.0f, -10.0f},
		{0.0f, -10.0f, -10.0f},
		{0.0f, -10.0f, 10.0f},
		{0.0f, 10.0f, 10.0f}
	};
	aas_edge_t edges[5] = {0};
	int edge_index[4] = {1, 2, 3, 4};
	aas_face_t faces[2] = {0};
	int face_index[1] = {1};
	aas_plane_t planes[2] = {0};

	edges[1].v[0] = 0;
	edges[1].v[1] = 1;
	edges[2].v[0] = 1;
	edges[2].v[1] = 2;
	edges[3].v[0] = 2;
	edges[3].v[1] = 3;
	edges[4].v[0] = 3;
	edges[4].v[1] = 0;
	areas[1].firstface = 0;
	areas[1].numfaces = 1;
	settings[1].areaflags = AAS_AREA_LADDER;
	settings[1].presencetype = PRESENCE_NORMAL;
	faces[1].planenum = 0;
	faces[1].faceflags = AAS_FACE_LADDER;
	faces[1].firstedge = 0;
	faces[1].numedges = 4;
	VectorSet(planes[0].normal, 1.0f, 0.0f, 0.0f);
	VectorSet(planes[1].normal, -1.0f, 0.0f, 0.0f);

	aasworld.loaded = qtrue;
	aasworld.numAreas = 2;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 2;
	aasworld.areasettings = settings;
	aasworld.numVertexes = 4;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = 5;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 4;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 2;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 1;
	aasworld.faceIndex = face_index;
	aasworld.numPlanes = 2;
	aasworld.planes = planes;

	const vec3_t fallback_bounds[4] = {
		{1.0f, 0.0f, 0.0f},
		{1.0f, 1.0f, 0.0f},
		{-1.0f, 1.0f, 0.0f},
		{-1.0f, -1.0f, 0.0f}
	};
	vec3_t origin = {0.0f, 0.0f, 0.0f};
	for (size_t index = 0;
		index < sizeof(fallback_bounds) / sizeof(fallback_bounds[0]);
		++index)
	{
		VectorSet(areas[1].mins,
			fallback_bounds[index][0] - 0.1f,
			fallback_bounds[index][1] - 0.1f,
			-1.0f);
		VectorSet(areas[1].maxs,
			fallback_bounds[index][0] + 0.1f,
			fallback_bounds[index][1] + 0.1f,
			1.0f);
		assert_true(AAS_AgainstLadder(origin));
	}

	VectorSet(areas[1].mins, -20.0f, -20.0f, -20.0f);
	VectorSet(areas[1].maxs, 20.0f, 20.0f, 20.0f);
	settings[1].areaflags = 0;
	assert_false(AAS_AgainstLadder(origin));
	settings[1].areaflags = AAS_AREA_LADDER;
	settings[1].presencetype = PRESENCE_CROUCH;
	assert_false(AAS_AgainstLadder(origin));
	settings[1].presencetype = PRESENCE_NORMAL;
	faces[1].faceflags = 0;
	assert_false(AAS_AgainstLadder(origin));
	faces[1].faceflags = AAS_FACE_LADDER;

	planes[0].dist = 2.999f;
	assert_true(AAS_AgainstLadder(origin));
	planes[0].dist = 3.0f;
	assert_false(AAS_AgainstLadder(origin));
	planes[0].dist = 0.0f;

	vec3_t epsilon_inside = {0.0f, 10.004f, 0.0f};
	vec3_t epsilon_outside = {0.0f, 10.01f, 0.0f};
	assert_true(AAS_AgainstLadder(epsilon_inside));
	assert_false(AAS_AgainstLadder(epsilon_outside));

	face_index[0] = -1;
	planes[1].dist = 100.0f;
	assert_false(AAS_AgainstLadder(origin));
	planes[1].dist = 0.0f;
	assert_true(AAS_AgainstLadder(origin));

	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_ladder_shared_edge_generation

Pins the symmetric retail ladder transition across a horizontal edge shared
by two vertical, same-facing ladder surfaces, including the 32-unit offsets.
=============
*/
static void test_reachability_ladder_shared_edge_generation(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_vertex_t vertexes[2] = {
		{0.0f, 0.0f, 0.0f},
		{0.0f, 10.0f, 0.0f}
	};
	aas_edge_t edges[2] = {{{0, 0}}, {{0, 1}}};
	int edge_index[2] = {1, 1};
	aas_face_t faces[3] = {0};
	int face_index[2] = {1, 2};
	aas_plane_t planes[1] = {0};

	areas[1].areanum = 1;
	areas[1].firstface = 0;
	areas[1].numfaces = 1;
	VectorSet(areas[1].mins, -16.0f, -1.0f, -32.0f);
	VectorSet(areas[1].maxs, 0.0f, 11.0f, 0.0f);
	areas[2].areanum = 2;
	areas[2].firstface = 1;
	areas[2].numfaces = 1;
	VectorSet(areas[2].mins, -16.0f, -1.0f, 0.0f);
	VectorSet(areas[2].maxs, 0.0f, 11.0f, 32.0f);
	settings[1].areaflags = AAS_AREA_LADDER;
	settings[1].presencetype = PRESENCE_NORMAL;
	settings[2].areaflags = AAS_AREA_LADDER;
	settings[2].presencetype = PRESENCE_NORMAL;
	faces[1].planenum = 0;
	faces[1].faceflags = AAS_FACE_LADDER;
	faces[1].firstedge = 0;
	faces[1].numedges = 1;
	faces[2].planenum = 0;
	faces[2].faceflags = AAS_FACE_LADDER;
	faces[2].firstedge = 1;
	faces[2].numedges = 1;
	planes[0].normal[0] = 1.0f;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numVertexes = 2;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = 2;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 2;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 3;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 2;
	aasworld.faceIndex = face_index;
	aasworld.numPlanes = 1;
	aasworld.planes = planes;

	AAS_InitReachability();
	assert_true(AAS_Reachability_Ladder(1, 2));
	assert_true(AAS_ReachabilityExists(1, 2));
	assert_true(AAS_ReachabilityExists(2, 1));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 3);
	assert_int_equal(settings[1].firstreachablearea, 1);
	assert_int_equal(settings[1].numreachableareas, 1);
	assert_int_equal(settings[2].firstreachablearea, 2);
	assert_int_equal(settings[2].numreachableareas, 1);
	assert_int_equal(aasworld.reachability[0].areanum, 0);
	assert_int_equal(aasworld.reachability[1].areanum, 2);
	assert_int_equal(aasworld.reachability[1].facenum, 1);
	assert_int_equal(aasworld.reachability[1].edgenum, 1);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_LADDER);
	assert_int_equal(aasworld.reachability[1].traveltime, 10);
	assert_float_equal(aasworld.reachability[1].start[2], -32.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[0], -3.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[2], 32.0f, 0.0001f);
	assert_int_equal(aasworld.reachability[2].areanum, 1);
	assert_int_equal(aasworld.reachability[2].facenum, 2);
	assert_int_equal(aasworld.reachability[2].edgenum, 1);
	assert_int_equal(aasworld.reachability[2].traveltype, TRAVEL_LADDER);
	assert_int_equal(aasworld.reachability[2].traveltime, 10);
	assert_float_equal(aasworld.reachability[2].start[2], 32.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[2].end[0], -3.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[2].end[2], -32.0f, 0.0001f);

	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	FreeMemory(aasworld.reachability);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_retail_bsp_entity_and_epair_prepend_contract

Pins reverse textual entity order, reverse epair insertion and duplicate-key
precedence, plus the retail vector accessor's missing and partial-key writes.
=============
*/
static void test_retail_bsp_entity_and_epair_prepend_contract(void **state)
{
	(void)state;
	const char entity_data[] =
		"{\n"
		"\"classname\" \"first\"\n"
		"\"duplicate\" \"early\"\n"
		"\"duplicate\" \"late\"\n"
		"\"partial\" \"4.5\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"second\"\n"
		"}\n";
	aas_bspentity_t *entities = AAS_ParseBSPEntities(entity_data,
		sizeof(entity_data) - 1U);
	assert_non_null(entities);
	assert_non_null(entities->next);
	assert_null(entities->next->next);
	assert_string_equal(AAS_ValueForBSPEpairKey(entities, "classname"),
		"second");
	assert_string_equal(AAS_ValueForBSPEpairKey(entities->next, "classname"),
		"first");
	assert_string_equal(AAS_ValueForBSPEpairKey(entities->next, "duplicate"),
		"late");

	vec3_t missing = {11.0f, 22.0f, 33.0f};
	assert_false(AAS_VectorForBSPEpairKey(entities->next, "missing", missing));
	assert_float_equal(missing[0], 11.0f, 0.0001f);
	assert_float_equal(missing[1], 22.0f, 0.0001f);
	assert_float_equal(missing[2], 33.0f, 0.0001f);

	vec3_t partial = {11.0f, 22.0f, 33.0f};
	assert_true(AAS_VectorForBSPEpairKey(entities->next, "partial", partial));
	assert_float_equal(partial[0], 4.5f, 0.0001f);
	assert_float_equal(partial[1], 0.0f, 0.0001f);
	assert_float_equal(partial[2], 0.0f, 0.0001f);

	AAS_FreeBSPEntities(entities);
}

/*
=============
test_retail_bsp_entity_lexer_flags

Pins SetScriptFlags(12): comments remain lexer whitespace, adjacent quoted
tokens stay separate, and backslashes inside strings are copied literally.
=============
*/
static void test_retail_bsp_entity_lexer_flags(void **state)
{
	(void)state;
	const char entity_data[] =
		"// leading entity comment\n"
		"{ /* comment after brace */\n"
		"\"classname\" /* between key and value */ \"worldspawn\"\n"
		"\"path\" \"textures\\metal\\door\" // trailing comment\n"
		"}\n"
		"/* comment between entities */\n"
		"{\n"
		"\"classname\" \"info_notnull\"\n"
		"}\n";

	aas_bspentity_t *entities = AAS_ParseBSPEntities(entity_data,
		sizeof(entity_data) - 1U);
	assert_non_null(entities);
	assert_non_null(entities->next);
	assert_null(entities->next->next);
	assert_string_equal(AAS_ValueForBSPEpairKey(entities, "classname"),
		"info_notnull");
	assert_string_equal(AAS_ValueForBSPEpairKey(entities->next, "classname"),
		"worldspawn");
	assert_string_equal(AAS_ValueForBSPEpairKey(entities->next, "path"),
		"textures\\metal\\door");

	AAS_FreeBSPEntities(entities);
}

/*
=============
test_reachability_retail_teleporter_generation

Parses retail BSP epairs and pins misc_teleporter destination matching, the
24-unit destination lift/drop trace, expanded source hull, and stored cost.
=============
*/
static void test_reachability_retail_teleporter_generation(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	const char entity_data[] =
		"{\n"
		"\"classname\" \"misc_teleporter\"\n"
		"\"origin\" \"0 0 0\"\n"
		"\"target\" \"teleport_destination\"\n"
		"\"speed\" \"3.5\"\n"
		"\"spawnflags\" \"7\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"misc_teleporter_dest\"\n"
		"\"targetname\" \"teleport_destination\"\n"
		"\"origin\" \"100 0 16\"\n"
		"}\n";
	aas_bspentity_t *entities = AAS_ParseBSPEntities(entity_data,
		sizeof(entity_data) - 1U);
	assert_non_null(entities);
	assert_non_null(entities->next);
	assert_string_equal(AAS_ValueForBSPEpairKey(entities, "classname"),
		"misc_teleporter_dest");
	assert_string_equal(AAS_ValueForBSPEpairKey(entities->next, "classname"),
		"misc_teleporter");
	assert_float_equal(AAS_FloatForBSPEpairKey(entities->next, "speed"),
		3.5f, 0.0001f);
	assert_int_equal(AAS_IntForBSPEpairKey(entities->next, "spawnflags"), 7);
	vec3_t parsedorigin;
	assert_true(AAS_VectorForBSPEpairKey(entities, "origin", parsedorigin));
	assert_float_equal(parsedorigin[0], 100.0f, 0.0001f);

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_node_t nodes[3] = {0};
	aas_plane_t planes[2] = {0};
	areas[1].areanum = 1;
	VectorSet(areas[1].mins, -64.0f, -64.0f, -64.0f);
	VectorSet(areas[1].maxs, 49.0f, 64.0f, 64.0f);
	areas[2].areanum = 2;
	VectorSet(areas[2].mins, 50.0f, -64.0f, 0.0f);
	VectorSet(areas[2].maxs, 160.0f, 64.0f, 128.0f);
	/*
	 * Retail admits a teleporter source area on AAS_AreaGrounded alone:
	 * 0x10015f21 calls sub_10011670, which is "areasettings[area].areaflags &
	 * 1" (0x1001168a). Quake II has no teleporter brush contents, so the
	 * AAS_AREACONTENTS_TELEPORTER bit is descriptive here and is not what
	 * qualifies the area.
	 */
	settings[1].contents = AAS_AREACONTENTS_TELEPORTER;
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_CROUCH;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[2].presencetype = PRESENCE_CROUCH;
	nodes[1].planenum = 0;
	nodes[1].children[0] = 2;
	nodes[1].children[1] = -1;
	nodes[2].planenum = 1;
	nodes[2].children[0] = -2;
	nodes[2].children[1] = 0;
	planes[0].normal[0] = 1.0f;
	planes[0].dist = 50.0f;
	planes[1].normal[2] = 1.0f;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numNodes = 3;
	aasworld.nodes = nodes;
	aasworld.numPlanes = 2;
	aasworld.planes = planes;

	AAS_InitReachability();
	assert_int_equal(AAS_Reachability_TeleportEntityList(entities), 1);
	assert_true(AAS_ReachabilityExists(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(settings[1].firstreachablearea, 1);
	assert_int_equal(settings[1].numreachableareas, 1);
	assert_int_equal(aasworld.reachability[1].areanum, 2);
	assert_int_equal(aasworld.reachability[1].facenum, 0);
	assert_int_equal(aasworld.reachability[1].edgenum, 0);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_TELEPORT);
	assert_int_equal(aasworld.reachability[1].traveltime, 50);
	assert_float_equal(aasworld.reachability[1].start[0], 0.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[0], 100.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[2], 0.25f, 0.0001f);

	AAS_FreeBSPEntities(entities);
	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	FreeMemory(aasworld.reachability);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_retail_elevator_generation

Pins retail func_plat edge sampling, model/height metadata, outside-hull start
placement, and the height-times-100-over-speed route cost.
=============
*/
static void test_reachability_retail_elevator_generation(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	const char entity_data[] =
		"{\n"
		"\"classname\" \"func_plat\"\n"
		"\"model\" \"*1\"\n"
		"\"lip\" \"8\"\n"
		"\"height\" \"32\"\n"
		"\"speed\" \"200\"\n"
		"}\n";
	aas_bspentity_t *entities = AAS_ParseBSPEntities(entity_data,
		sizeof(entity_data) - 1U);
	assert_non_null(entities);

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_bspmodel_t models[2] = {0};
	areas[1].areanum = 1;
	VectorSet(areas[1].mins, -40.0f, -4.0f, 0.0f);
	VectorSet(areas[1].maxs, -16.0f, 4.0f, 20.0f);
	areas[2].areanum = 2;
	VectorSet(areas[2].mins, 20.0f, -4.0f, 30.0f);
	VectorSet(areas[2].maxs, 22.0f, 4.0f, 40.0f);
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_CROUCH;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[2].presencetype = PRESENCE_CROUCH;
	VectorSet(models[1].mins, -16.0f, -16.0f, 0.0f);
	VectorSet(models[1].maxs, 16.0f, 16.0f, 16.0f);
	VectorClear(models[1].origin);

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numBspModels = 2;
	aasworld.bspModels = models;

	AAS_InitReachability();
	assert_int_equal(AAS_Reachability_ElevatorEntityList(entities), 1);
	assert_true(AAS_ReachabilityExists(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(settings[1].firstreachablearea, 1);
	assert_int_equal(settings[1].numreachableareas, 1);
	assert_int_equal(aasworld.reachability[1].areanum, 2);
	assert_int_equal(aasworld.reachability[1].facenum, 1);
	assert_int_equal(aasworld.reachability[1].edgenum, 32);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_ELEVATOR);
	assert_int_equal(aasworld.reachability[1].traveltime, 16);
	assert_float_equal(aasworld.reachability[1].start[0],
		-34.4765f, 0.001f);
	assert_float_equal(aasworld.reachability[1].start[1], 0.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].start[2], 2.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[0], 21.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[1], 0.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[2], 34.0f, 0.0001f);

	AAS_FreeBSPEntities(entities);
	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	FreeMemory(aasworld.reachability);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_retail_grapple_generation

Pins the retail grounded-source grapple scan, higher solid-face selection,
near-wall shot trace, safe landing, metadata, and distance-derived route cost.
=============
*/
static void test_reachability_retail_grapple_generation(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	Q2Bridge_SetImportTable(&g_test_q2_imports);
	g_test_grapple_trace_enabled = qtrue;
	g_test_gap_trace_enabled = qfalse;
	g_test_point_contents = 0;

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_node_t nodes[4] = {0};
	aas_plane_t planes[4] = {0};
	aas_vertex_t vertexes[4] = {
		{100.0f, -10.0f, 64.0f},
		{100.0f, 10.0f, 64.0f},
		{100.0f, 10.0f, 136.0f},
		{100.0f, -10.0f, 136.0f}
	};
	aas_edge_t edges[5] = {
		{{0, 0}},
		{{0, 1}},
		{{1, 2}},
		{{2, 3}},
		{{3, 0}}
	};
	int edge_index[4] = {1, 2, 3, 4};
	aas_face_t faces[2] = {0};
	int face_index[1] = {1};

	areas[1].areanum = 1;
	VectorSet(areas[1].mins, -32.0f, -32.0f, 0.0f);
	VectorSet(areas[1].maxs, 49.0f, 32.0f, 96.0f);
	VectorSet(areas[1].center, 0.0f, 0.0f, 32.0f);
	areas[2].areanum = 2;
	areas[2].firstface = 0;
	areas[2].numfaces = 1;
	VectorSet(areas[2].mins, 50.0f, -32.0f, 0.0f);
	VectorSet(areas[2].maxs, 160.0f, 32.0f, 160.0f);
	VectorSet(areas[2].center, 100.0f, 0.0f, 80.0f);
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[2].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;

	nodes[1].planenum = 0;
	nodes[1].children[0] = 2;
	nodes[1].children[1] = 3;
	nodes[2].planenum = 2;
	nodes[2].children[0] = -2;
	nodes[2].children[1] = 0;
	nodes[3].planenum = 1;
	nodes[3].children[0] = -1;
	nodes[3].children[1] = 0;
	planes[0].normal[0] = 1.0f;
	planes[0].dist = 50.0f;
	planes[1].normal[2] = 1.0f;
	planes[2].normal[2] = 1.0f;
	planes[3].normal[0] = -1.0f;
	planes[3].dist = -100.0f;
	faces[1].planenum = 3;
	faces[1].faceflags = AAS_FACE_SOLID;
	faces[1].firstedge = 0;
	faces[1].numedges = 4;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numNodes = 4;
	aasworld.nodes = nodes;
	aasworld.numPlanes = 4;
	aasworld.planes = planes;
	aasworld.numVertexes = 4;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = 5;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 4;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 2;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 1;
	aasworld.faceIndex = face_index;

	AAS_InitReachability();
	assert_false(AAS_Reachability_Grapple(1, 2));
	assert_true(AAS_ReachabilityExists(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(settings[1].firstreachablearea, 1);
	assert_int_equal(settings[1].numreachableareas, 1);
	assert_int_equal(aasworld.reachability[1].areanum, 2);
	assert_int_equal(aasworld.reachability[1].facenum, 1);
	assert_int_equal(aasworld.reachability[1].edgenum, 0);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_GRAPPLEHOOK);
	assert_int_equal(aasworld.reachability[1].traveltime, 537);
	assert_float_equal(aasworld.reachability[1].start[0], 0.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].start[2], 0.25f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[0], 110.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[2], 100.0f, 0.0001f);

	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	FreeMemory(aasworld.reachability);
	g_test_grapple_trace_enabled = qfalse;
	Q2Bridge_SetImportTable(NULL);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_retail_weapon_jump_generation

Pins retail item-area marking, downward rocket knockback, higher ground-face
selection, predicted landing, and the fixed rocket-jump route metadata.
=============
*/
static void test_reachability_retail_weapon_jump_generation(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	Q2Bridge_SetImportTable(&g_test_q2_imports);
	g_test_gap_trace_enabled = qtrue;
	g_test_grapple_trace_enabled = qfalse;
	g_test_source_ground_max_x = 0.0f;
	g_test_destination_ground_min_x = 50.0f;
	g_test_ground_height = 0.0f;
	g_test_destination_ground_height = 100.0f;
	g_test_point_contents = 0;

	const char entitydata[] =
		"{\n"
		"\"classname\" \"worldspawn\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"item_health\"\n"
		"\"origin\" \"0 0 48\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"item_health_mega\"\n"
		"\"origin\" \"80 0 140\"\n"
		"}\n";
	aas_bspentity_t *entities = AAS_ParseBSPEntities(entitydata,
		sizeof(entitydata) - 1);
	assert_non_null(entities);

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_node_t nodes[4] = {0};
	aas_plane_t planes[3] = {0};
	aas_vertex_t vertexes[4] = {
		{64.0f, -16.0f, 100.0f},
		{96.0f, -16.0f, 100.0f},
		{96.0f, 16.0f, 100.0f},
		{64.0f, 16.0f, 100.0f}
	};
	aas_edge_t edges[5] = {
		{{0, 0}},
		{{0, 1}},
		{{1, 2}},
		{{2, 3}},
		{{3, 0}}
	};
	int edge_index[4] = {1, 2, 3, 4};
	aas_face_t faces[2] = {0};
	int face_index[1] = {1};

	areas[1].areanum = 1;
	VectorSet(areas[1].mins, -64.0f, -32.0f, 24.0f);
	VectorSet(areas[1].maxs, 49.0f, 32.0f, 96.0f);
	VectorSet(areas[1].center, 0.0f, 0.0f, 48.0f);
	areas[2].areanum = 2;
	areas[2].firstface = 0;
	areas[2].numfaces = 1;
	VectorSet(areas[2].mins, 50.0f, -32.0f, 100.0f);
	VectorSet(areas[2].maxs, 160.0f, 32.0f, 180.0f);
	VectorSet(areas[2].center, 80.0f, 0.0f, 132.0f);
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[2].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;

	nodes[1].planenum = 0;
	nodes[1].children[0] = 2;
	nodes[1].children[1] = 3;
	nodes[2].planenum = 2;
	nodes[2].children[0] = -2;
	nodes[2].children[1] = 0;
	nodes[3].planenum = 1;
	nodes[3].children[0] = -1;
	nodes[3].children[1] = 0;
	planes[0].normal[0] = 1.0f;
	planes[0].dist = 50.0f;
	planes[1].normal[2] = 1.0f;
	planes[1].dist = 24.0f;
	planes[2].normal[2] = 1.0f;
	planes[2].dist = 100.0f;
	faces[1].planenum = 2;
	faces[1].faceflags = AAS_FACE_GROUND;
	faces[1].firstedge = 0;
	faces[1].numedges = 4;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numNodes = 4;
	aasworld.nodes = nodes;
	aasworld.numPlanes = 3;
	aasworld.planes = planes;
	aasworld.numVertexes = 4;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = 5;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 4;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 2;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 1;
	aasworld.faceIndex = face_index;

	assert_int_equal(AAS_SetWeaponJumpAreaFlagsEntityList(entities), 1);
	assert_int_equal(settings[1].areaflags & AAS_AREA_WEAPONJUMP, 0);
	assert_int_equal(settings[2].areaflags & AAS_AREA_WEAPONJUMP,
		AAS_AREA_WEAPONJUMP);
	vec3_t rocketorigin = {0.0f, 0.0f, 24.25f};
	assert_true(AAS_RocketJumpZVelocity(rocketorigin) > 600.0f);
	assert_float_equal(AAS_BFGJumpZVelocity(rocketorigin),
		AAS_RocketJumpZVelocity(rocketorigin),
		0.0001f);

	AAS_InitReachability();
	assert_true(AAS_Reachability_WeaponJump(1, 2));
	assert_true(AAS_ReachabilityExists(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(settings[1].firstreachablearea, 1);
	assert_int_equal(settings[1].numreachableareas, 1);
	assert_int_equal(aasworld.reachability[1].areanum, 2);
	assert_int_equal(aasworld.reachability[1].facenum, 0);
	assert_int_equal(aasworld.reachability[1].edgenum, 0);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_ROCKETJUMP);
	assert_int_equal(aasworld.reachability[1].traveltime, 500);
	assert_float_equal(aasworld.reachability[1].start[0], 0.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].start[2], 24.25f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[0], 80.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[2], 100.0f, 0.0001f);

	AAS_FreeBSPEntities(entities);
	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	FreeMemory(aasworld.reachability);
	g_test_gap_trace_enabled = qfalse;
	g_test_destination_ground_height = 0.0f;
	Q2Bridge_SetImportTable(NULL);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_retail_secondary_walkoff_generation

Pins the separate retail exposed-edge scan and its 100/3000 safe-versus-
damaging-fall route cost split.
=============
*/
static void test_reachability_retail_secondary_walkoff_generation(void **state)
{
	(void)state;
	const float landingheights[2] = {0.0f, -200.0f};
	const unsigned short traveltimes[2] = {100, 3000};

	for (int testcase = 0; testcase < 2; ++testcase)
	{
		memset(&aasworld, 0, sizeof(aasworld));
		aas_area_t areas[4] = {0};
		aas_areasettings_t settings[4] = {0};
		aas_node_t nodes[4] = {0};
		aas_plane_t planes[3] = {0};
		aas_vertex_t vertexes[2] = {
			{0.0f, -10.0f, 100.0f},
			{0.0f, 10.0f, 100.0f}
		};
		aas_edge_t edges[2] = {{{0, 0}}, {{0, 1}}};
		int edge_index[2] = {1, 1};
		aas_face_t faces[3] = {0};
		int face_index[2] = {1, 2};

		areas[1].areanum = 1;
		areas[1].firstface = 0;
		areas[1].numfaces = 2;
		VectorSet(areas[1].mins, 0.0f, -16.0f, 100.0f);
		VectorSet(areas[1].maxs, 64.0f, 16.0f, 180.0f);
		VectorSet(areas[1].center, 8.0f, 0.0f, 132.0f);
		areas[2].areanum = 2;
		VectorSet(areas[2].mins,
			-64.0f,
			-16.0f,
			landingheights[testcase]);
		VectorSet(areas[2].maxs,
			-0.1f,
			16.0f,
			100.0f);
		areas[3].areanum = 3;
		settings[1].areaflags = AAS_AREA_GROUNDED;
		settings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
		settings[2].areaflags = AAS_AREA_GROUNDED;
		settings[2].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
		settings[3].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;

		nodes[1].planenum = 0;
		nodes[1].children[0] = 2;
		nodes[1].children[1] = 3;
		nodes[2].planenum = 1;
		nodes[2].children[0] = -1;
		nodes[2].children[1] = 0;
		nodes[3].planenum = 2;
		nodes[3].children[0] = -2;
		nodes[3].children[1] = 0;
		planes[0].normal[0] = 1.0f;
		planes[1].normal[2] = 1.0f;
		planes[1].dist = 100.0f;
		planes[2].normal[2] = 1.0f;
		planes[2].dist = landingheights[testcase];
		faces[1].planenum = 1;
		faces[1].faceflags = AAS_FACE_GROUND;
		faces[1].firstedge = 0;
		faces[1].numedges = 1;
		faces[2].planenum = 0;
		faces[2].faceflags = AAS_FACE_SOLID;
		faces[2].firstedge = 1;
		faces[2].numedges = 1;
		faces[2].frontarea = 1;
		faces[2].backarea = 3;

		aasworld.loaded = qtrue;
		aasworld.numAreas = 4;
		aasworld.areas = areas;
		aasworld.numAreaSettings = 4;
		aasworld.areasettings = settings;
		aasworld.numNodes = 4;
		aasworld.nodes = nodes;
		aasworld.numPlanes = 3;
		aasworld.planes = planes;
		aasworld.numVertexes = 2;
		aasworld.vertexes = vertexes;
		aasworld.numEdges = 2;
		aasworld.edges = edges;
		aasworld.edgeIndexSize = 2;
		aasworld.edgeIndex = edge_index;
		aasworld.numFaces = 3;
		aasworld.faces = faces;
		aasworld.faceIndexSize = 2;
		aasworld.faceIndex = face_index;

		AAS_InitReachability();
		AAS_Reachability_WalkOffLedge(1);
		assert_true(AAS_ReachabilityExists(1, 2));
		AAS_StoreReachability();
		assert_int_equal(aasworld.numReachability, 2);
		assert_int_equal(aasworld.reachability[1].areanum, 2);
		assert_int_equal(aasworld.reachability[1].facenum, 0);
		assert_int_equal(aasworld.reachability[1].edgenum, 1);
		assert_int_equal(aasworld.reachability[1].traveltype,
			TRAVEL_WALKOFFLEDGE);
		assert_int_equal(aasworld.reachability[1].traveltime,
			traveltimes[testcase]);
		assert_float_equal(aasworld.reachability[1].start[0],
			-8.0f,
			0.0001f);
		assert_float_equal(aasworld.reachability[1].start[2],
			100.0f,
			0.0001f);
		assert_float_equal(aasworld.reachability[1].end[0],
			-8.0f,
			0.0001f);
		assert_float_equal(aasworld.reachability[1].end[2],
			landingheights[testcase] + 0.25f,
			0.0001f);

		AAS_ShutDownReachabilityHeap();
		AAS_ClearReachabilityData();
		FreeMemory(aasworld.reachability);
		memset(&aasworld, 0, sizeof(aasworld));
	}
}

/*
=============
test_reachability_retail_incremental_lifecycle

Pins retail one-based generation progress, final secondary/entity passes,
sentinel storage, heap cleanup, and delayed AAS initialization.
=============
*/
static void test_reachability_retail_incremental_lifecycle(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	aas_area_t areas[2] = {0};
	aas_areasettings_t settings[2] = {0};
	areas[1].areanum = 1;
	settings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	aasworld.loaded = qtrue;
	aasworld.numAreas = 2;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 2;
	aasworld.areasettings = settings;

	AAS_InitReachability();
	assert_true(aasworld.saveFile);
	assert_int_equal(aasworld.numReachabilityAreas, 1);
	assert_true(AAS_ContinueInitReachability());
	assert_int_equal(aasworld.numReachabilityAreas, 2);
	assert_int_equal(aasworld.numReachability, 1);
	assert_non_null(aasworld.reachability);
	assert_false(AAS_ContinueInitReachability());
	assert_false(aasworld.initialized);

	AAS_ContinueInit(0.0f);
	assert_true(aasworld.initialized);
	assert_int_equal(aasworld.numClusters, 2);
	assert_int_equal(aasworld.clusters[1].numareas, 1);
	AAS_ClearReachabilityData();
	FreeMemory(aasworld.reachability);
	FreeMemory(aasworld.portals);
	FreeMemory(aasworld.portalIndex);
	FreeMemory(aasworld.clusters);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_retail_cluster_portal_rebuild

Pins the retail 12-byte cluster ABI, geometric two-plane portal recognition,
face flooding of otherwise unreachable areas, and two-sided portal numbering.
=============
*/
static void test_retail_cluster_portal_rebuild(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[4] = {0};
	aas_areasettings_t settings[4] = {0};
	aas_face_t faces[3] = {0};
	int faceindex[4] = {1, 2, 1, 2};
	aas_edge_t edges[3] = {0};
	int edgeindex[2] = {1, 2};

	areas[1].firstface = 0;
	areas[1].numfaces = 2;
	areas[2].firstface = 2;
	areas[2].numfaces = 1;
	areas[3].firstface = 3;
	areas[3].numfaces = 1;
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	settings[2].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	settings[3].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;

	faces[1].planenum = 0;
	faces[1].firstedge = 0;
	faces[1].numedges = 1;
	faces[1].frontarea = 1;
	faces[1].backarea = 2;
	faces[2].planenum = 2;
	faces[2].firstedge = 1;
	faces[2].numedges = 1;
	faces[2].frontarea = 1;
	faces[2].backarea = 3;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 4;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 4;
	aasworld.areasettings = settings;
	aasworld.numFaces = 3;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 4;
	aasworld.faceIndex = faceindex;
	aasworld.numEdges = 3;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 2;
	aasworld.edgeIndex = edgeindex;

	assert_int_equal(sizeof(aas_cluster_t), 12);
	AAS_InitClustering();
	assert_true(aasworld.saveFile);
	assert_int_equal(settings[1].contents & AAS_AREACONTENTS_CLUSTERPORTAL,
		AAS_AREACONTENTS_CLUSTERPORTAL);
	assert_int_equal(settings[1].contents & AAS_AREACONTENTS_ROUTEPORTAL,
		AAS_AREACONTENTS_ROUTEPORTAL);
	assert_int_equal(settings[1].cluster, -1);
	assert_int_equal(settings[2].cluster, 1);
	assert_int_equal(settings[3].cluster, 2);
	assert_int_equal(aasworld.numPortals, 2);
	assert_int_equal(aasworld.portalIndexSize, 2);
	assert_int_equal(aasworld.numClusters, 3);
	assert_int_equal(aasworld.portals[1].areanum, 1);
	assert_int_equal(aasworld.portals[1].frontcluster, 1);
	assert_int_equal(aasworld.portals[1].backcluster, 2);
	assert_int_equal(aasworld.portals[1].clusterareanum[0], 1);
	assert_int_equal(aasworld.portals[1].clusterareanum[1], 1);
	assert_int_equal(aasworld.clusters[1].numareas, 2);
	assert_int_equal(aasworld.clusters[1].numportals, 1);
	assert_int_equal(aasworld.clusters[1].firstportal, 0);
	assert_int_equal(aasworld.clusters[2].numareas, 2);
	assert_int_equal(aasworld.clusters[2].numportals, 1);
	assert_int_equal(aasworld.clusters[2].firstportal, 1);
	assert_int_equal(aasworld.portalIndex[0], 1);
	assert_int_equal(aasworld.portalIndex[1], 1);

	FreeMemory(aasworld.portals);
	FreeMemory(aasworld.portalIndex);
	FreeMemory(aasworld.clusters);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_retail_cluster_flood_invalid_area_diagnostic

Pins the retail recursive-cluster-flood diagnostic when a reachability points
outside the one-based area table.
=============
*/
static void test_retail_cluster_flood_invalid_area_diagnostic(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[2] = {0};
	aas_areasettings_t settings[2] = {0};
	aas_reachability_t reachability[2] = {0};
	settings[1].firstreachablearea = 1;
	settings[1].numreachableareas = 1;
	reachability[1].areanum = 2;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 2;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 2;
	aasworld.areasettings = settings;
	aasworld.numReachability = 2;
	aasworld.reachability = reachability;

	test_reset_print_capture();
	BotInterface_SetImportTable(&g_test_imports);
	AAS_InitClustering();
	assert_int_equal(g_test_print_count, 5);
	/*
	 * Retail routes this diagnostic through AAS_Error: 0x10008959 calls
	 * sub_1000d7e0, which formats into a stack buffer and hands the result to
	 * the Print import with priority 4 (0x1000d810), i.e. PRT_FATAL. The
	 * .rdata literal at 0x1005adfc is 0x2e bytes - 45 characters plus the NUL -
	 * so it carries no trailing newline.
	 */
	for (int index = 1; index <= 3; ++index)
	{
		assert_int_equal(g_test_print_priority_history[index], PRT_FATAL);
		assert_string_equal(g_test_print_message_history[index],
			"AAS_FloodClusterAreas_r: areanum out of range");
	}
	assert_int_equal(g_test_print_priority_history[4], PRT_ERROR);
	assert_string_equal(g_test_print_message_history[4],
		"AAS_InitClustering: cluster rebuild did not converge\n");
	BotInterface_SetImportTable(NULL);

	FreeMemory(aasworld.portals);
	FreeMemory(aasworld.portalIndex);
	FreeMemory(aasworld.clusters);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_retail_aas_geometry_optimization

Pins ladder-only face retention, compact signed indexes, non-elevator
reachability remapping, and the retail elevator model-number exception.
=============
*/
static void test_retail_aas_geometry_optimization(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aasworld.numVertexes = 4;
	aasworld.vertexes = (aas_vertex_t *)GetClearedMemory(4U * sizeof(aas_vertex_t));
	aasworld.numEdges = 4;
	aasworld.edges = (aas_edge_t *)GetClearedMemory(4U * sizeof(aas_edge_t));
	aasworld.edgeIndexSize = 3;
	aasworld.edgeIndex = (int *)GetClearedMemory(3U * sizeof(int));
	aasworld.numFaces = 3;
	aasworld.faces = (aas_face_t *)GetClearedMemory(3U * sizeof(aas_face_t));
	aasworld.faceIndexSize = 3;
	aasworld.faceIndex = (int *)GetClearedMemory(3U * sizeof(int));
	aasworld.numAreas = 3;
	aasworld.areas = (aas_area_t *)GetClearedMemory(3U * sizeof(aas_area_t));
	aasworld.numReachability = 4;
	aasworld.reachability = (aas_reachability_t *)GetClearedMemory(
		4U * sizeof(aas_reachability_t));
	assert_non_null(aasworld.vertexes);
	assert_non_null(aasworld.edges);
	assert_non_null(aasworld.edgeIndex);
	assert_non_null(aasworld.faces);
	assert_non_null(aasworld.faceIndex);
	assert_non_null(aasworld.areas);
	assert_non_null(aasworld.reachability);

	VectorSet(aasworld.vertexes[0], 1.0f, 0.0f, 0.0f);
	VectorSet(aasworld.vertexes[1], 2.0f, 0.0f, 0.0f);
	VectorSet(aasworld.vertexes[2], 3.0f, 0.0f, 0.0f);
	VectorSet(aasworld.vertexes[3], 4.0f, 0.0f, 0.0f);
	aasworld.edges[1].v[0] = 0;
	aasworld.edges[1].v[1] = 1;
	aasworld.edges[2].v[0] = 1;
	aasworld.edges[2].v[1] = 2;
	aasworld.edges[3].v[0] = 2;
	aasworld.edges[3].v[1] = 3;
	aasworld.edgeIndex[0] = 1;
	aasworld.edgeIndex[1] = -2;
	aasworld.edgeIndex[2] = 3;

	aasworld.faces[1].faceflags = AAS_FACE_LADDER;
	aasworld.faces[1].firstedge = 0;
	aasworld.faces[1].numedges = 2;
	aasworld.faces[2].faceflags = AAS_FACE_GROUND;
	aasworld.faces[2].firstedge = 2;
	aasworld.faces[2].numedges = 1;
	aasworld.faceIndex[0] = 1;
	aasworld.faceIndex[1] = 2;
	aasworld.faceIndex[2] = -1;
	aasworld.areas[1].firstface = 0;
	aasworld.areas[1].numfaces = 2;
	aasworld.areas[2].firstface = 2;
	aasworld.areas[2].numfaces = 1;

	aasworld.reachability[1].facenum = 1;
	aasworld.reachability[1].edgenum = -2;
	aasworld.reachability[1].traveltype = TRAVEL_LADDER;
	aasworld.reachability[2].facenum = 2;
	aasworld.reachability[2].edgenum = 3;
	aasworld.reachability[2].traveltype = TRAVEL_WALK;
	aasworld.reachability[3].facenum = 77;
	aasworld.reachability[3].edgenum = 88;
	aasworld.reachability[3].traveltype = TRAVEL_ELEVATOR;
	aasworld.loaded = qtrue;

	AAS_Optimize();
	assert_int_equal(aasworld.numVertexes, 3);
	assert_int_equal(aasworld.numEdges, 3);
	assert_int_equal(aasworld.edgeIndexSize, 2);
	assert_int_equal(aasworld.numFaces, 2);
	assert_int_equal(aasworld.faceIndexSize, 2);
	assert_int_equal(aasworld.numAreas, 3);
	assert_int_equal(aasworld.faces[1].faceflags, AAS_FACE_LADDER);
	assert_int_equal(aasworld.faces[1].firstedge, 0);
	assert_int_equal(aasworld.faces[1].numedges, 2);
	assert_int_equal(aasworld.edgeIndex[0], 1);
	assert_int_equal(aasworld.edgeIndex[1], -2);
	assert_int_equal(aasworld.areas[1].firstface, 0);
	assert_int_equal(aasworld.areas[1].numfaces, 1);
	assert_int_equal(aasworld.areas[2].firstface, 1);
	assert_int_equal(aasworld.areas[2].numfaces, 1);
	assert_int_equal(aasworld.faceIndex[0], 1);
	assert_int_equal(aasworld.faceIndex[1], -1);
	/*
	 * Retail sub_10010e90 performs exactly one store per non-elevator
	 * reachability: 0x10010ef5 writes faceoptimizeindex[facenum] back into the
	 * facenum field at record offset +4 (the record stride is 0x2c and the
	 * travel type it compares against 0xb lives at +0x24). The edgenum field at
	 * +8 is never remapped, so it keeps its pre-optimization value even though
	 * the compacted edge arrays are swapped in immediately afterwards.
	 */
	assert_int_equal(aasworld.reachability[1].facenum, 1);
	assert_int_equal(aasworld.reachability[1].edgenum, -2);
	assert_int_equal(aasworld.reachability[2].facenum, 0);
	assert_int_equal(aasworld.reachability[2].edgenum, 3);
	assert_int_equal(aasworld.reachability[3].facenum, 77);
	assert_int_equal(aasworld.reachability[3].edgenum, 88);

	FreeMemory(aasworld.vertexes);
	FreeMemory(aasworld.edges);
	FreeMemory(aasworld.edgeIndex);
	FreeMemory(aasworld.faces);
	FreeMemory(aasworld.faceIndex);
	FreeMemory(aasworld.areas);
	FreeMemory(aasworld.reachability);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_retail_routing_intra_area_travel_cost

Pins reverse routing across an intermediate area, including the retail
walk-distance cost between the incoming and outgoing reachabilities.
=============
*/
static void test_retail_routing_intra_area_travel_cost(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[4] = {0};
	aas_areasettings_t settings[4] = {0};
	aas_reachability_t reachability[3] = {0};
	settings[1].firstreachablearea = 1;
	settings[1].numreachableareas = 1;
	settings[2].firstreachablearea = 2;
	settings[2].numreachableareas = 1;
	settings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	settings[2].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	settings[3].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	reachability[1].areanum = 2;
	VectorSet(reachability[1].start, 0.0f, 0.0f, 0.0f);
	VectorSet(reachability[1].end, 100.0f, 0.0f, 0.0f);
	reachability[1].traveltype = TRAVEL_WALK;
	reachability[1].traveltime = 10;
	reachability[2].areanum = 3;
	VectorSet(reachability[2].start, 200.0f, 0.0f, 0.0f);
	VectorSet(reachability[2].end, 300.0f, 0.0f, 0.0f);
	reachability[2].traveltype = TRAVEL_WALK;
	reachability[2].traveltime = 10;

	aasworld.loaded = qtrue;
	aasworld.initialized = qtrue;
	aasworld.numAreas = 4;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 4;
	aasworld.areasettings = settings;
	aasworld.numReachability = 3;
	aasworld.reachability = reachability;
	AAS_InitTravelFlagFromType();
	AAS_InitAreaContentsTravelFlags();
	assert_int_equal(AAS_PrepareReachability(), BLERR_NOERROR);

	vec3_t origin = {0.0f, 0.0f, 0.0f};
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		origin,
		3,
		TFL_WALK | TFL_AIR),
		54);
	assert_int_equal(AAS_AreaReachabilityToGoalArea(1,
		origin,
		3,
		TFL_WALK | TFL_AIR),
		1);

	AAS_FreeAllRoutingCaches();
	AAS_ClearReachabilityData();
	free(aasworld.areacontentstravelflags);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_retail_presence_type_bounding_box_table

Pins sub_1000dda0's +/-16 half extents and its literal 4/2 box selector, which
is the opposite of the Q3 presence-type mapping.
=============
*/
static void test_retail_presence_type_bounding_box_table(void **state)
{
	(void)state;
	BotInterface_SetImportTable(&g_test_imports);

	vec3_t mins;
	vec3_t maxs;

	/* 1000de3c: presence type 4 selects table row 1, the 32-unit-tall box. */
	test_reset_print_capture();
	AAS_PresenceTypeBoundingBox(4, mins, maxs);
	assert_int_equal(g_test_print_count, 0);
	assert_float_equal(mins[0], -16.0f, 0.0001f);
	assert_float_equal(mins[1], -16.0f, 0.0001f);
	assert_float_equal(mins[2], -24.0f, 0.0001f);
	assert_float_equal(maxs[0], 16.0f, 0.0001f);
	assert_float_equal(maxs[1], 16.0f, 0.0001f);
	assert_float_equal(maxs[2], 32.0f, 0.0001f);

	/* 1000de46/1000de58: presence type 2 selects row 2, the crouch box. */
	test_reset_print_capture();
	AAS_PresenceTypeBoundingBox(2, mins, maxs);
	assert_int_equal(g_test_print_count, 0);
	assert_float_equal(mins[0], -16.0f, 0.0001f);
	assert_float_equal(mins[1], -16.0f, 0.0001f);
	assert_float_equal(mins[2], -24.0f, 0.0001f);
	assert_float_equal(maxs[0], 16.0f, 0.0001f);
	assert_float_equal(maxs[1], 16.0f, 0.0001f);
	assert_float_equal(maxs[2], 8.0f, 0.0001f);

	/* 1000de4f: any other value warns and still falls through to row 2. */
	test_reset_print_capture();
	AAS_PresenceTypeBoundingBox(3, mins, maxs);
	assert_int_equal(g_test_print_count, 1);
	assert_int_equal(g_test_print_priority, PRT_FATAL);
	assert_string_equal(g_test_print_message,
		"AAS_PresenceTypeBoundingBox: unknown presence type\n");
	assert_float_equal(maxs[2], 8.0f, 0.0001f);

	BotInterface_SetImportTable(NULL);
}

/*
=============
test_retail_point_contents_uses_engine_import

Pins sub_10003080, whose whole body is a tail call into the PointContents
import slot rather than a walk of the DLL's own BSP copy.
=============
*/
static void test_retail_point_contents_uses_engine_import(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	BotInterface_SetImportTable(&g_test_imports);
	Q2Bridge_SetImportTable(&g_test_q2_imports);

	vec3_t point = {0.0f, 0.0f, 0.0f};
	g_test_point_contents_calls = 0U;

	/* The engine ORs entity contents into the world answer; retail returns
	   that combined value verbatim (1000308e). */
	g_test_point_contents = CONTENTS_WATER | CONTENTS_MONSTER;
	assert_int_equal(AAS_PointContents(point),
		CONTENTS_WATER | CONTENTS_MONSTER);
	g_test_point_contents = CONTENTS_SOLID;
	assert_int_equal(AAS_PointContents(point), CONTENTS_SOLID);
	g_test_point_contents = 0;
	assert_int_equal(AAS_PointContents(point), 0);
	assert_int_equal(g_test_point_contents_calls, 3U);

	Q2Bridge_SetImportTable(NULL);
	BotInterface_SetImportTable(NULL);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_retail_bsp_entity_parse_errors_use_script_diagnostics

Pins sub_100069a0's three failure paths, which all report through ScriptError
(sub_1003e2c0) on the "entdata" script rather than a bare Print.
=============
*/
static void test_retail_bsp_entity_parse_errors_use_script_diagnostics(
	void **state)
{
	(void)state;
	BotInterface_SetImportTable(&g_test_imports);

	/* 10006bda: a leading token that is not "{". */
	const char stray[] = "foo\n{\n\"classname\" \"worldspawn\"\n}\n";
	test_reset_print_capture();
	assert_null(AAS_ParseBSPEntities(stray, sizeof(stray) - 1U));
	assert_int_equal(g_test_print_priority, PRT_ERROR);
	assert_string_equal(g_test_print_message,
		"file entdata, line 1: invalid foo\n\n");

	/* 10006bed: a key token whose type is not TT_STRING. */
	const char badkey[] = "{\n1 \"worldspawn\"\n}\n";
	test_reset_print_capture();
	assert_null(AAS_ParseBSPEntities(badkey, sizeof(badkey) - 1U));
	assert_int_equal(g_test_print_priority, PRT_ERROR);
	assert_string_equal(g_test_print_message,
		"file entdata, line 2: invalid 1\n\n");

	/* 10006c34: an entity that is never closed. */
	const char unterminated[] = "{\n\"classname\" \"worldspawn\"\n";
	test_reset_print_capture();
	assert_null(AAS_ParseBSPEntities(unterminated,
		sizeof(unterminated) - 1U));
	assert_int_equal(g_test_print_priority, PRT_ERROR);
	assert_string_equal(g_test_print_message,
		"file entdata, line 3: missing }\n\n");

	BotInterface_SetImportTable(NULL);
}

/*
=============
test_retail_shutdown_reports_unconditionally

Pins sub_1000ee30, whose "AAS shutdown." message is the last statement and is
guarded by nothing, so it fires with no map loaded and on repeat shutdowns.
=============
*/
static void test_retail_shutdown_reports_unconditionally(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	BotInterface_SetImportTable(&g_test_imports);

	/* 1000ee6f zeroes both the loaded and initialized flags before the print
	   at 1000ee80, so no predicate could ever have referenced them. */
	test_reset_print_capture();
	AAS_Shutdown();
	assert_int_equal(g_test_print_count, 1);
	assert_int_equal(g_test_print_priority, PRT_MESSAGE);
	assert_string_equal(g_test_print_message, "AAS shutdown.\n");

	test_reset_print_capture();
	AAS_Shutdown();
	assert_int_equal(g_test_print_count, 1);
	assert_int_equal(g_test_print_priority, PRT_MESSAGE);
	assert_string_equal(g_test_print_message, "AAS shutdown.\n");

	BotInterface_SetImportTable(NULL);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_retail_update_entity_keeps_routing_caches

Pins sub_1000a920, whose mover path only relinks; the routing cache head
tables survive every runtime door, plat and elevator move.
=============
*/
static void test_retail_update_entity_keeps_routing_caches(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	BotInterface_SetImportTable(&g_test_imports);

	aas_area_t areas[2] = {0};
	aas_areasettings_t settings[2] = {0};
	aas_cluster_t clusters[2] = {0};
	clusters[1].numareas = 1;

	aasworld.numAreas = 2;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 2;
	aasworld.areasettings = settings;
	aasworld.numClusters = 2;
	aasworld.clusters = clusters;
	aasworld.loaded = qtrue;

	assert_true(AAS_InitRetailRoutingCaches());
	assert_non_null(aasworld.retailClusterAreaCache);
	assert_non_null(aasworld.retailPortalCache);

	AASEntityFrame mover = {0};
	mover.number = 1;
	mover.solid = SOLID_BSP;
	mover.origin_dirty = true;
	VectorSet(mover.previous_origin, 0.0f, 0.0f, 0.0f);
	VectorSet(mover.origin, 0.0f, 0.0f, 64.0f);
	VectorSet(mover.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(mover.maxs, 8.0f, 8.0f, 8.0f);

	/*
	 * Retail's whole mover path is 1000aafd/1000ab14/1000ab1e/1000ab38 -
	 * unlink and relink - followed by "1000ab47  return 0". The only routine
	 * that releases the cache head tables is sub_10019550, called just from
	 * AAS_LoadMap (1000ed30) and AAS_Shutdown (1000ee30).
	 */
	assert_int_equal(AAS_UpdateEntity(1, &mover), BLERR_NOERROR);
	assert_non_null(aasworld.retailClusterAreaCache);
	assert_non_null(aasworld.retailPortalCache);

	VectorCopy(mover.origin, mover.previous_origin);
	VectorSet(mover.origin, 0.0f, 0.0f, 128.0f);
	assert_int_equal(AAS_UpdateEntity(1, &mover), BLERR_NOERROR);
	assert_non_null(aasworld.retailClusterAreaCache);
	assert_non_null(aasworld.retailPortalCache);

	AAS_FreeAllRoutingCaches();
	aasworld.numAreas = 0;
	aasworld.areas = NULL;
	aasworld.numAreaSettings = 0;
	aasworld.areasettings = NULL;
	aasworld.numClusters = 0;
	aasworld.clusters = NULL;
	AAS_Shutdown();
	BotInterface_SetImportTable(NULL);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_retail_entity_visible_keeps_samples_inside_pvs_gate

Pins sub_1000b750: the bottom/top sample advance at 1000b98d-1000b9af sits
inside the PVS-gated block, so a failing PVS test re-probes the same midpoint
for all three iterations and never reaches the feet or head samples.
=============
*/
static void test_retail_entity_visible_keeps_samples_inside_pvs_gate(
	void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	BotInterface_SetImportTable(&g_test_imports);
	Q2Bridge_SetImportTable(&g_test_q2_imports);
	g_test_gap_trace_enabled = qfalse;

	aas_plane_t planes[1] = {0};
	aas_bspnode_t nodes[1] = {0};
	aas_bspleaf_t leaves[2] = {0};
	unsigned char visibility[22] = {0};
	int32_t num_clusters = 2;
	int32_t data_offset = 20;

	/* One z-splitting node: z > 0 lands in cluster 0, z < 0 in cluster 1. */
	planes[0].normal[2] = 1.0f;
	nodes[0].planenum = 0;
	nodes[0].children[0] = -1;
	nodes[0].children[1] = -2;
	leaves[0].cluster = 0;
	leaves[1].cluster = 1;
	memcpy(visibility, &num_clusters, sizeof(num_clusters));
	memcpy(visibility + 4, &data_offset, sizeof(data_offset));
	memcpy(visibility + 8, &data_offset, sizeof(data_offset));
	memcpy(visibility + 12, &data_offset, sizeof(data_offset));
	memcpy(visibility + 16, &data_offset, sizeof(data_offset));
	/* Cluster 1 sees only itself, never cluster 0. */
	visibility[20] = 1U << 1;

	aasworld.numBspPlanes = 1;
	aasworld.bspPlanes = planes;
	aasworld.numBspNodes = 1;
	aasworld.bspNodes = nodes;
	aasworld.numBspLeaves = 2;
	aasworld.bspLeaves = leaves;
	aasworld.numBspVisibilityClusters = num_clusters;
	aasworld.bspVisibilitySize = sizeof(visibility);
	aasworld.bspVisibility = visibility;

	aas_entity_t entities[2] = {0};
	entities[1].inuse = qtrue;
	entities[1].number = 1;
	VectorSet(entities[1].origin, 0.0f, 0.0f, 10.0f);
	VectorSet(entities[1].mins, -8.0f, -8.0f, -20.0f);
	VectorSet(entities[1].maxs, 8.0f, 8.0f, 4.0f);
	aasworld.entities = entities;
	aasworld.maxEntities = 2;

	/*
	 * The bbox midpoint sits at z = +2 (cluster 0, invisible from the eye),
	 * while the feet sample would land at z = -18 and the head sample at
	 * z = +6. Retail never advances past the midpoint here.
	 */
	vec3_t eye = {0.0f, 0.0f, -10.0f};
	vec3_t viewangles = {0.0f, 0.0f, 0.0f};
	g_test_point_contents_calls = 0U;
	g_test_point_contents = 0;
	assert_false(AAS_EntityVisible(0, eye, viewangles, 360.0f, 1));
	assert_int_equal(g_test_point_contents_calls, 0U);

	/* Once the midpoint's own cluster is visible the normal path runs. */
	visibility[20] = (1U << 0) | (1U << 1);
	g_test_point_contents_calls = 0U;
	assert_true(AAS_EntityVisible(0, eye, viewangles, 360.0f, 1));
	assert_true(g_test_point_contents_calls > 0U);

	aasworld.entities = NULL;
	aasworld.maxEntities = 0;
	Q2Bridge_SetImportTable(NULL);
	BotInterface_SetImportTable(NULL);
	memset(&aasworld, 0, sizeof(aasworld));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_aas_in_pvs_decodes_retail_visibility_rows),
		cmocka_unit_test(test_aas_null_map_refreshes_assets_without_world_reset),
		cmocka_unit_test(test_aas_empty_map_reports_retail_missing_bsp),
		cmocka_unit_test(test_aas_loader_preserves_retail_header_error_contracts),
		cmocka_unit_test(test_bsp_texinfo_payload_load_and_retail_trace_boundary),
		cmocka_unit_test(
			test_aas_loader_uses_retail_candidate_order_and_reports_selected_paths),
		cmocka_unit_test(
			test_aas_loader_resolves_retail_loose_directory_precedence),
		cmocka_unit_test(
			test_aas_loader_probes_retail_paks_and_reads_bounded_entries),
		cmocka_unit_test(
			test_aas_loader_runs_retail_zip_fallback_as_a_separate_extraction_pass),
        cmocka_unit_test_setup_teardown(test_aas_loads_sample_map,
                                        aas_environment_setup,
                                        aas_environment_teardown),
        cmocka_unit_test_setup_teardown(test_aas_entity_linking_and_reachability,
                                        aas_environment_setup,
                                        aas_environment_teardown),
        cmocka_unit_test_setup_teardown(test_aas_entity_relinks_on_bsp_angle_change,
                                        aas_environment_setup,
                                        aas_environment_teardown),
		cmocka_unit_test_setup_teardown(test_retail_entity_link_heaps_are_fixed_and_reused,
			aas_link_heap_setup,
			aas_link_heap_teardown),
		cmocka_unit_test_setup_teardown(test_retail_map_load_keeps_configured_entity_storage,
			aas_link_heap_setup,
			aas_link_heap_teardown),
		cmocka_unit_test_setup_teardown(test_retail_entity_configuration_initialises_sound_state,
			aas_link_heap_setup,
			aas_link_heap_teardown),
		cmocka_unit_test_setup_teardown(test_retail_missing_bsp_preserves_navigation_and_bsp_storage,
			aas_link_heap_setup,
			aas_link_heap_teardown),
		cmocka_unit_test_setup_teardown(test_retail_missing_aas_releases_only_bsp_storage,
			aas_link_heap_setup,
			aas_link_heap_teardown),
		cmocka_unit_test_setup_teardown(
			test_successful_map_commit_retains_parsed_mover_catalogue,
			aas_link_heap_setup,
			aas_link_heap_teardown),
		cmocka_unit_test_setup_teardown(
			test_bsp_entity_loader_prefers_retained_lump,
			aas_link_heap_setup,
			aas_link_heap_teardown),
        cmocka_unit_test_setup_teardown(test_routing_frame_respects_framereachability,
                                        aas_environment_setup,
                                        aas_environment_teardown),
        cmocka_unit_test_setup_teardown(test_routing_frame_forcewrite_toggle,
                                        aas_environment_setup,
                                        aas_environment_teardown),
        cmocka_unit_test_setup_teardown(test_reachability_force_reachability_toggle,
                                        aas_environment_setup,
                                        aas_environment_teardown),
        cmocka_unit_test_setup_teardown(test_reachability_force_clustering_toggle,
                                        aas_environment_setup,
                                        aas_environment_teardown),
		cmocka_unit_test(test_reachability_geometry_helpers),
		cmocka_unit_test(test_reachability_area_and_link_helpers),
		cmocka_unit_test(test_bridge_config_retail_maxwaterjump_default),
		cmocka_unit_test(test_reachability_physics_helpers),
		cmocka_unit_test(test_reachability_swim_generation_and_storage),
		cmocka_unit_test(test_reachability_equal_floor_generation_and_storage),
		cmocka_unit_test(test_reachability_adjacent_edge_travel_branches),
		cmocka_unit_test(test_reachability_jump_generation_and_rejections),
		cmocka_unit_test(test_aas_against_ladder_retail_geometry_contract),
		cmocka_unit_test(test_reachability_ladder_shared_edge_generation),
		cmocka_unit_test(test_retail_bsp_entity_and_epair_prepend_contract),
		cmocka_unit_test(test_retail_bsp_entity_lexer_flags),
		cmocka_unit_test(test_reachability_retail_teleporter_generation),
		cmocka_unit_test(test_reachability_retail_elevator_generation),
		cmocka_unit_test(test_reachability_retail_grapple_generation),
		cmocka_unit_test(test_reachability_retail_weapon_jump_generation),
		cmocka_unit_test(test_reachability_retail_secondary_walkoff_generation),
		cmocka_unit_test(test_reachability_retail_incremental_lifecycle),
		cmocka_unit_test(test_retail_cluster_portal_rebuild),
		cmocka_unit_test(test_retail_cluster_flood_invalid_area_diagnostic),
		cmocka_unit_test(test_retail_aas_geometry_optimization),
		cmocka_unit_test(test_retail_routing_intra_area_travel_cost),
		cmocka_unit_test(test_retail_presence_type_bounding_box_table),
		cmocka_unit_test(test_retail_point_contents_uses_engine_import),
		cmocka_unit_test(
			test_retail_bsp_entity_parse_errors_use_script_diagnostics),
		cmocka_unit_test(test_retail_shutdown_reports_unconditionally),
		cmocka_unit_test(test_retail_update_entity_keeps_routing_caches),
		cmocka_unit_test(
			test_retail_entity_visible_keeps_samples_inside_pvs_gate),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
