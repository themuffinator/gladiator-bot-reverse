#include <stdarg.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <setjmp.h>
#include <cmocka.h>

#ifdef _WIN32
#include <direct.h>
#define rmdir _rmdir
#define unlink _unlink
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "botlib/aas/aas_local.h"
#include "botlib/aas/aas_map.h"
#include "botlib/aas/aas_sound.h"
#include "botlib/common/l_crc.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "botlib/interface/botlib_interface.h"
#include "botlib/precomp/l_precomp.h"

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined for the point-light sound fixture."
#endif

typedef struct pointlight_world_fixture_s
{
	aas_bspmodel_t models[1];
	aas_bspnode_t nodes[2];
	aas_plane_t planes[2];
	aas_bsptexinfo_t texinfo[1];
	aas_bspface_t faces[2];
	aas_bspsurfaceextent_t extents[2];
	unsigned char lightdata[81];
} pointlight_world_fixture_t;

static int g_print_count;
static int g_print_priority;
static char g_print_message[1024];
static qboolean g_saw_max_lights_range_error;

/*
=============
test_capture_print

Capture retail diagnostics emitted by the light heap and loader.
=============
*/
static void test_capture_print(int priority, const char *format, ...)
{
	g_print_count += 1;
	g_print_priority = priority;
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(g_print_message, sizeof(g_print_message), format, arguments);
	va_end(arguments);
	if (strstr(g_print_message,
		"max_aaslights out of range [0, 65536]") != NULL)
	{
		g_saw_max_lights_range_error = qtrue;
	}
}

/*
=============
test_capture_dprint

Accept botlib debug output without forwarding it to the test runner.
=============
*/
static void test_capture_dprint(const char *format, ...)
{
	(void)format;
}

/*
=============
test_libvar_get

Keep the engine libvar bridge empty so local l_libvar values are authoritative.
=============
*/
static int test_libvar_get(const char *name, char *value, size_t size)
{
	(void)name;
	if (value == NULL || size == 0U)
	{
		return -1;
	}
	value[0] = '\0';
	return -1;
}

/*
=============
test_libvar_set

Accept mirrored engine libvar writes used by the local registry.
=============
*/
static int test_libvar_set(const char *name, const char *value)
{
	(void)name;
	(void)value;
	return 0;
}

static const botlib_import_table_t g_imports = {
	.Print = test_capture_print,
	.DPrint = test_capture_dprint,
	.BotLibVarGet = test_libvar_get,
	.BotLibVarSet = test_libvar_set,
};

/*
=============
test_reset_print_capture

Reset the diagnostic discriminator between point-light operations.
=============
*/
static void test_reset_print_capture(void)
{
	g_print_count = 0;
	g_print_priority = 0;
	g_print_message[0] = '\0';
	g_saw_max_lights_range_error = qfalse;
}

/*
=============
test_setup_pointlight_world

Build two BSP nodes whose first face misses and whose far child supplies a
multistyle floor sample, exercising retail near-face-far recursion.
=============
*/
static void test_setup_pointlight_world(pointlight_world_fixture_t *fixture)
{
	assert_non_null(fixture);
	memset(fixture, 0, sizeof(*fixture));
	memset(&aasworld, 0, sizeof(aasworld));

	fixture->models[0].headnode = 0;
	fixture->models[0].firstface = 0;
	fixture->models[0].numfaces = 2;

	VectorSet(fixture->planes[0].normal, 0.0f, 0.0f, 1.0f);
	fixture->planes[0].dist = 32.0f;
	fixture->planes[0].type = 2;
	VectorSet(fixture->planes[1].normal, 0.0f, 0.0f, 1.0f);
	fixture->planes[1].dist = 0.0f;
	fixture->planes[1].type = 2;

	fixture->nodes[0].planenum = 0;
	fixture->nodes[0].children[0] = -1;
	fixture->nodes[0].children[1] = 1;
	fixture->nodes[0].firstface = 0;
	fixture->nodes[0].numfaces = 1;
	fixture->nodes[1].planenum = 1;
	fixture->nodes[1].children[0] = -1;
	fixture->nodes[1].children[1] = -1;
	fixture->nodes[1].firstface = 1;
	fixture->nodes[1].numfaces = 1;

	fixture->texinfo[0].vecs[0][0] = 1.0f;
	fixture->texinfo[0].vecs[1][1] = 1.0f;
	for (int index = 0; index < 2; ++index)
	{
		fixture->faces[index].texinfo = 0;
		memset(fixture->faces[index].styles, 0xff,
			sizeof(fixture->faces[index].styles));
		fixture->extents[index].extents[0] = 32;
		fixture->extents[index].extents[1] = 32;
	}

	fixture->faces[0].styles[0] = 0;
	fixture->faces[0].lightofs = 0;
	fixture->extents[0].texturemins[0] = 96;
	fixture->extents[0].texturemins[1] = 0;
	fixture->lightdata[9] = 20;
	fixture->lightdata[10] = 40;
	fixture->lightdata[11] = 60;

	fixture->faces[1].styles[0] = 0;
	fixture->faces[1].styles[1] = 7;
	fixture->faces[1].lightofs = 27;
	fixture->lightdata[36] = 100;
	fixture->lightdata[37] = 50;
	fixture->lightdata[38] = 25;
	fixture->lightdata[63] = 10;
	fixture->lightdata[64] = 20;
	fixture->lightdata[65] = 30;

	aasworld.loaded = qtrue;
	aasworld.numBspModels = 1;
	aasworld.bspModels = fixture->models;
	aasworld.numBspNodes = 2;
	aasworld.bspNodes = fixture->nodes;
	aasworld.numBspPlanes = 2;
	aasworld.bspPlanes = fixture->planes;
	aasworld.numBspTexInfo = 1;
	aasworld.bspTexInfo = fixture->texinfo;
	aasworld.numBspFaces = 2;
	aasworld.bspFaces = fixture->faces;
	aasworld.bspSurfaceExtents = fixture->extents;
	aasworld.bspLightDataSize = (int)sizeof(fixture->lightdata);
	aasworld.bspLightData = fixture->lightdata;
}

/*
=============
test_clear_pointlight_world

Detach stack-backed BSP fixtures without invoking the owning-world free path.
=============
*/
static void test_clear_pointlight_world(void)
{
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_set_sound_libvars

Configure the three raw sound setup variables before entering the no-argument
retail initialiser.
=============
*/
static void test_set_sound_libvars(int max_soundinfo,
	int max_aassounds,
	const char *soundconfig)
{
	char value[32];
	snprintf(value, sizeof(value), "%d", max_soundinfo);
	LibVarSet("max_soundinfo", value);
	snprintf(value, sizeof(value), "%d", max_aassounds);
	LibVarSet("max_aassounds", value);
	LibVarSet("soundconfig", soundconfig);
}

/*
=============
test_init_sound_heap

Initialise the sensory subsystem with a selected retail max_aaslights heap.
=============
*/
static void test_init_sound_heap(int capacity)
{
	BotInterface_SetImportTable(&g_imports);
	LibVar_Init();
	char capacity_text[32];
	snprintf(capacity_text, sizeof(capacity_text), "%d", capacity);
	LibVarSet("max_aaslights", capacity_text);

	test_set_sound_libvars(64,
		4,
		PROJECT_SOURCE_DIR "/dev_tools/assets/sounds.c");
	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);
	AAS_SoundSubsystem_InitPointLightHeap();
	AAS_SoundSubsystem_SetFrameTime(0.0f);
}

/*
=============
test_shutdown_sound_heap

Mirror the process-lifetime ownership used by retail shutdown after an
isolated heap test.
=============
*/
static void test_shutdown_sound_heap(void)
{
	AAS_SoundSubsystem_ResetState();
	PC_ShutdownLexer();
	LibVar_Shutdown();
	CRC_ResetSourceChecksums();
	BotMemory_Shutdown();
	BotInterface_SetImportTable(NULL);
}

/*
=============
test_static_pointlight_sampling_contract

Pin recursive traversal, 4096-unit depth, inclusive bounds, no-data/no-face
fallbacks, lightofs -1, retail s-major addressing and multistyle scaling.
=============
*/
static void test_static_pointlight_sampling_contract(void **state)
{
	(void)state;
	pointlight_world_fixture_t fixture;
	test_setup_pointlight_world(&fixture);

	vec3_t origin = {16.0f, 0.0f, 64.0f};
	vec3_t end = {16.0f, 0.0f, -4032.0f};
	vec3_t hit;
	int red = -1;
	int green = -1;
	int blue = -1;
	assert_true(AAS_BSPTracePointLight(origin,
		end,
		hit,
		&red,
		&green,
		&blue));
	assert_float_equal(hit[2], 0.0f, 0.0001f);
	assert_int_equal(red, 113);
	assert_int_equal(green, 72);
	assert_int_equal(blue, 56);
	assert_int_equal(AAS_PointLight(origin, &red, &green, &blue), 80);
	assert_int_equal(red, 113);
	assert_int_equal(green, 72);
	assert_int_equal(blue, 56);

	vec3_t near_face = {112.0f, 0.0f, 64.0f};
	assert_true(AAS_BSPTracePointLight(near_face,
		(vec3_t){112.0f, 0.0f, -4032.0f},
		hit,
		&red,
		&green,
		&blue));
	assert_float_equal(hit[2], 32.0f, 0.0001f);
	assert_int_equal(red, 20);
	assert_int_equal(green, 41);
	assert_int_equal(blue, 61);

	fixture.extents[1].extents[1] = 16;
	fixture.faces[1].styles[1] = 0xffU;
	fixture.lightdata[48] = 30;
	fixture.lightdata[49] = 40;
	fixture.lightdata[50] = 50;
	vec3_t nonsquare = {32.0f, 16.0f, 64.0f};
	assert_int_equal(AAS_PointLight(nonsquare, &red, &green, &blue), 40);
	assert_int_equal(red, 30);
	assert_int_equal(green, 41);
	assert_int_equal(blue, 51);
	fixture.extents[1].extents[1] = 32;
	fixture.faces[1].styles[1] = 7;

	vec3_t inclusive_edge = {32.0f, 0.0f, 64.0f};
	assert_int_equal(AAS_PointLight(inclusive_edge, &red, &green, &blue), 0);
	vec3_t outside_edge = {33.0f, 0.0f, 64.0f};
	assert_int_equal(AAS_PointLight(outside_edge, &red, &green, &blue), 255);
	assert_int_equal(red, 0);
	assert_int_equal(green, 0);
	assert_int_equal(blue, 0);

	fixture.faces[1].lightofs = -1;
	assert_true(AAS_BSPTracePointLight(origin,
		end,
		hit,
		&red,
		&green,
		&blue));
	assert_int_equal(red, 0);
	assert_int_equal(green, 0);
	assert_int_equal(blue, 0);
	assert_int_equal(AAS_PointLight(origin, &red, &green, &blue), 0);
	fixture.faces[1].lightofs = 27;

	unsigned char *saved_lightdata = aasworld.bspLightData;
	aasworld.bspLightData = NULL;
	assert_int_equal(AAS_PointLight(origin, &red, &green, &blue), 255);
	assert_int_equal(red, 0);
	assert_int_equal(green, 0);
	assert_int_equal(blue, 0);
	aasworld.bspLightData = saved_lightdata;

	fixture.planes[1].dist = -4032.5f;
	assert_false(AAS_BSPTracePointLight(origin,
		end,
		hit,
		&red,
		&green,
		&blue));
	fixture.planes[1].dist = -4031.5f;
	assert_true(AAS_BSPTracePointLight(origin,
		end,
		hit,
		&red,
		&green,
		&blue));
	assert_float_equal(hit[2], -4031.5f, 0.001f);

	test_clear_pointlight_world();
}

/*
=============
test_dynamic_pointlight_heap_lifecycle

Pin the x86 record ABI, reversed origin copy, newest-first active list,
capacity warning, persistent reset, strict expiry and stale-slot reuse order.
=============
*/
static void test_dynamic_pointlight_heap_lifecycle(void **state)
{
	(void)state;
	assert_int_equal(sizeof(aas_pointlight_event_t), 0x34);
	assert_int_equal(offsetof(aas_pointlight_event_t, origin), 0x00);
	assert_int_equal(offsetof(aas_pointlight_event_t, ent), 0x0c);
	assert_int_equal(offsetof(aas_pointlight_event_t, color), 0x10);
	assert_int_equal(offsetof(aas_pointlight_event_t, radius), 0x1c);
	assert_int_equal(offsetof(aas_pointlight_event_t, time), 0x20);
	assert_int_equal(offsetof(aas_pointlight_event_t, timestamp), 0x24);
	assert_int_equal(offsetof(aas_pointlight_event_t, decay), 0x28);
	assert_int_equal(offsetof(aas_pointlight_event_t, next_index), 0x2c);
	assert_int_equal(offsetof(aas_pointlight_event_t, prev_index), 0x30);

	test_init_sound_heap(2);
	vec3_t first_origin = {10.0f, 20.0f, 30.0f};
	assert_true(AAS_SoundSubsystem_RecordPointLight(first_origin,
		11,
		64.0f,
		1.0f,
		2.0f,
		3.0f,
		2.0f,
		999.0f));
	const aas_pointlight_event_t *first = AAS_SoundSubsystem_PointLight(0);
	assert_non_null(first);
	assert_int_equal(first->ent, 11);
	assert_float_equal(first->time, 2.0f, 0.0f);
	assert_float_equal(first->timestamp, 0.0f, 0.0f);
	VectorSet(((aas_pointlight_event_t *)first)->origin, 7.0f, 8.0f, 9.0f);

	vec3_t second_origin = {40.0f, 50.0f, 60.0f};
	assert_true(AAS_SoundSubsystem_RecordPointLight(second_origin,
		22,
		32.0f,
		4.0f,
		5.0f,
		6.0f,
		2.0f,
		0.0f));
	assert_int_equal(AAS_SoundSubsystem_PointLight(0)->ent, 22);
	assert_int_equal(AAS_SoundSubsystem_PointLight(1)->ent, 11);
	assert_int_equal(AAS_SoundSubsystem_PointLightCount(), 2);

	test_reset_print_capture();
	vec3_t third_origin = {70.0f, 80.0f, 90.0f};
	assert_true(AAS_SoundSubsystem_RecordPointLight(third_origin,
		33,
		16.0f,
		7.0f,
		8.0f,
		9.0f,
		2.0f,
		0.0f));
	assert_int_equal(AAS_SoundSubsystem_PointLightCount(), 2);
	assert_int_equal(g_print_count, 1);
	assert_int_equal(g_print_priority, PRT_MESSAGE);
	assert_string_equal(g_print_message, "WARNING: empty light heap\n");

	AAS_SoundSubsystem_ResetFrameEvents();
	assert_int_equal(AAS_SoundSubsystem_PointLightCount(), 2);
	AAS_SoundSubsystem_SetFrameTime(2.0f);
	assert_int_equal(AAS_SoundSubsystem_PointLightCount(), 2);
	AAS_SoundSubsystem_SetFrameTime(2.001f);
	assert_int_equal(AAS_SoundSubsystem_PointLightCount(), 0);

	vec3_t reused_origin = {100.0f, 101.0f, 102.0f};
	assert_true(AAS_SoundSubsystem_RecordPointLight(reused_origin,
		44,
		8.0f,
		1.0f,
		1.0f,
		1.0f,
		10.0f,
		5000.0f));
	assert_float_equal(reused_origin[0], 7.0f, 0.0f);
	assert_float_equal(reused_origin[1], 8.0f, 0.0f);
	assert_float_equal(reused_origin[2], 9.0f, 0.0f);
	assert_ptr_equal(AAS_SoundSubsystem_PointLight(0), first);

	test_shutdown_sound_heap();
}

/*
=============
test_sound_heap_schedules_activates_and_expires_retail_records

Pin sub_1001cbe0 through sub_1001cfa0: the 0x34-byte pool, strict delayed
activation, end-time order, inclusive expiry, fixed-capacity diagnostic, and
zero-offset replacement of an active entity/sound pair.
=============
*/
static void test_sound_heap_schedules_activates_and_expires_retail_records(
	void **state)
{
	(void)state;
	assert_int_equal(sizeof(aas_soundinfo_t), 0xb0);
	assert_int_equal(offsetof(aas_soundinfo_t, name), 0x00);
	assert_int_equal(offsetof(aas_soundinfo_t, volume), 0x50);
	assert_int_equal(offsetof(aas_soundinfo_t, duration), 0x54);
	assert_int_equal(offsetof(aas_soundinfo_t, type), 0x58);
	assert_int_equal(offsetof(aas_soundinfo_t, recognition), 0x5c);
	assert_int_equal(offsetof(aas_soundinfo_t, string), 0x60);
	assert_int_equal(sizeof(aas_sound_event_t), 0x34);
	assert_int_equal(offsetof(aas_sound_event_t, start), 0x00);
	assert_int_equal(offsetof(aas_sound_event_t, end), 0x04);
	assert_int_equal(offsetof(aas_sound_event_t, origin), 0x08);
	assert_int_equal(offsetof(aas_sound_event_t, zero), 0x14);
	assert_int_equal(offsetof(aas_sound_event_t, ent), 0x18);
	assert_int_equal(offsetof(aas_sound_event_t, channel), 0x1c);
	assert_int_equal(offsetof(aas_sound_event_t, soundindex), 0x20);
	assert_int_equal(offsetof(aas_sound_event_t, volume), 0x24);
	assert_int_equal(offsetof(aas_sound_event_t, attenuation), 0x28);
	assert_int_equal(offsetof(aas_sound_event_t, next_index), 0x2c);
	assert_int_equal(offsetof(aas_sound_event_t, prev_index), 0x30);

	test_init_sound_heap(2);
	char *assets[] = {"player/step1.wav", "weapons/blastf1a.wav"};
	assert_true(AAS_SoundSubsystem_RegisterMapAssets(2, assets));
	int blaster_info_index =
		AAS_SoundSubsystem_FindInfoIndex("weapons/blastf1a.wav");
	assert_true(blaster_info_index >= 0);
	const aas_soundinfo_t *blaster_info =
		AAS_SoundSubsystem_Info((size_t)blaster_info_index);
	assert_non_null(blaster_info);
	assert_string_equal(blaster_info->string, "Blaster");

	vec3_t origin = {1.0f, 2.0f, 3.0f};
	test_reset_print_capture();
	assert_int_equal(AAS_SoundSubsystem_UpdateSound(origin,
		10,
		1,
		2,
		0.25f,
		0.5f,
		0.0f), BLERR_INVALIDSOUNDINDEX);
	assert_int_equal(g_print_count, 1);
	assert_int_equal(g_print_priority, PRT_FATAL);
	assert_string_equal(g_print_message, "sound index 2 out of range [0, 2]\n");
	assert_true(AAS_SoundSubsystem_RecordSound(origin,
		10,
		1,
		0,
		0.25f,
		0.5f,
		0.0f));
	assert_true(AAS_SoundSubsystem_RecordSound(origin,
		11,
		2,
		1,
		0.75f,
		1.0f,
		0.0f));
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 0);

	AAS_SoundSubsystem_SetFrameTime(0.0f);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 0);
	AAS_SoundSubsystem_SetFrameTime(0.001f);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 2);
	const aas_sound_event_t *first = AAS_SoundSubsystem_SoundEvent(0);
	const aas_sound_event_t *second = AAS_SoundSubsystem_SoundEvent(1);
	assert_non_null(first);
	assert_non_null(second);
	assert_int_equal(first->soundindex, 0);
	assert_int_equal(second->soundindex, 1);
	assert_float_equal(first->start, 0.0f, 0.0001f);
	assert_float_equal(first->end, 0.2f, 0.0001f);
	assert_float_equal(second->end, 0.47f, 0.0001f);
	assert_int_equal(first->zero, 0);

	AAS_SoundSubsystem_SetFrameTime(0.2f);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 1);
	AAS_SoundSubsystem_SetFrameTime(0.47f);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 0);

	AAS_SoundSubsystem_SetFrameTime(1.0f);
	for (int index = 0; index < 4; ++index)
	{
		assert_true(AAS_SoundSubsystem_RecordSound(origin,
			44,
			0,
			0,
			1.0f,
			1.0f,
			1.0f));
	}
	test_reset_print_capture();
	assert_true(AAS_SoundSubsystem_RecordSound(origin,
		45,
		0,
		0,
		1.0f,
		1.0f,
		1.0f));
	assert_int_equal(g_print_count, 1);
	assert_int_equal(g_print_priority, PRT_ERROR);
	assert_string_equal(g_print_message, "empty sound heap\n");
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 0);

	AAS_SoundSubsystem_SetFrameTime(3.0f);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 1);
	assert_int_equal(AAS_SoundSubsystem_SoundEvent(0)->ent, 44);

	/*
	 * Retail compares timeofs against 0.0f at 0x1001ce95 and branches on the
	 * x87 C3 (equal) bit with `test ah, 0x40` at 0x1001ce9a - not on C0
	 * (`test ah, 1`, '<'). Only an immediate emit drops the record already
	 * active for this (entity, sound index) pair; a negative offset is just
	 * another delayed emit, so the active record survives and the new one
	 * waits in the scheduled list.
	 */
	assert_true(AAS_SoundSubsystem_RecordSound(origin,
		44,
		0,
		0,
		1.0f,
		1.0f,
		-0.1f));
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 1);
	assert_int_equal(AAS_SoundSubsystem_SoundEvent(0)->ent, 44);
	assert_float_equal(AAS_SoundSubsystem_SoundEvent(0)->start, 2.0f, 0.0001f);

	AAS_SoundSubsystem_SetFrameTime(3.0f);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 1);
	assert_float_equal(AAS_SoundSubsystem_SoundEvent(0)->start, 2.9f, 0.0001f);
	/*
	 * End time is (AAS_Time() + duration) + timeofs at 0x1001cee4, which
	 * re-reads the clock instead of reusing the start value: 3.0 + 0.2 - 0.1.
	 */
	assert_float_equal(AAS_SoundSubsystem_SoundEvent(0)->end, 3.1f, 0.0001f);

	/*
	 * The same pair emitted with timeofs 0 does take the replacement path at
	 * 0x1001ce9e (sub_1001cdd0), so the active record is unlinked before the
	 * heap allocation. The replacement starts exactly on this frame and
	 * sub_1001cfa0 activates on a strict `start < time`, so it stays queued
	 * until a later frame.
	 */
	assert_true(AAS_SoundSubsystem_RecordSound(origin,
		44,
		0,
		0,
		1.0f,
		1.0f,
		0.0f));
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 0);
	AAS_SoundSubsystem_SetFrameTime(3.001f);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 1);
	assert_float_equal(AAS_SoundSubsystem_SoundEvent(0)->start, 3.0f, 0.0001f);
	float final_end = AAS_SoundSubsystem_SoundEvent(0)->end;
	assert_float_equal(final_end, 3.2f, 0.0001f);
	AAS_SoundSubsystem_SetFrameTime(final_end);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 0);

	test_shutdown_sound_heap();
}

/*
=============
test_dynamic_pointlight_fractional_capacity_and_sound_independence

Pin __ftol-before-range-check capacity handling and prove point-light setup is
independent when a zero sound-metadata capacity rejects the first record.
=============
*/
static void test_dynamic_pointlight_fractional_capacity_and_sound_independence(
	void **state)
{
	(void)state;
	BotInterface_SetImportTable(&g_imports);
	LibVar_Init();
	LibVarSet("max_aaslights", "-0.9");
	test_set_sound_libvars(0,
		0,
		PROJECT_SOURCE_DIR "/dev_tools/assets/sounds.c");
	test_reset_print_capture();
	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);
	AAS_SoundSubsystem_InitPointLightHeap();
	assert_false(g_saw_max_lights_range_error);
	assert_int_equal(AAS_SoundSubsystem_InfoCount(), 0);
	assert_non_null(strstr(g_print_message,
		"more than 0 sound infos defined"));
	vec3_t origin = {1.0f, 2.0f, 3.0f};
	test_reset_print_capture();
	assert_true(AAS_SoundSubsystem_RecordPointLight(origin,
		1,
		1.0f,
		1.0f,
		1.0f,
		1.0f,
		1.0f,
		0.0f));
	assert_int_equal(AAS_SoundSubsystem_PointLightCount(), 0);
	assert_string_equal(g_print_message, "WARNING: empty light heap\n");

	LibVarSet("max_aaslights", "65536.9");
	test_reset_print_capture();
	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);
	AAS_SoundSubsystem_InitPointLightHeap();
	assert_false(g_saw_max_lights_range_error);
	assert_true(AAS_SoundSubsystem_RecordPointLight(origin,
		2,
		1.0f,
		1.0f,
		1.0f,
		1.0f,
		1.0f,
		0.0f));
	assert_int_equal(AAS_SoundSubsystem_PointLightCount(), 1);

	test_shutdown_sound_heap();
}

/*
=============
test_dynamic_pointlight_capacity_range_fallback

Reject an out-of-range max_aaslights value with the exact diagnostic and use
the retail 128-slot fallback heap.
=============
*/
static void test_dynamic_pointlight_capacity_range_fallback(void **state)
{
	(void)state;
	BotInterface_SetImportTable(&g_imports);
	LibVar_Init();
	LibVarSet("max_aaslights", "70000");

	test_set_sound_libvars(1024,
		4,
		PROJECT_SOURCE_DIR "/dev_tools/assets/sounds.c");
	test_reset_print_capture();
	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);
	AAS_SoundSubsystem_InitPointLightHeap();
	assert_true(g_saw_max_lights_range_error);

	vec3_t origin = {0.0f, 0.0f, 0.0f};
	for (int index = 0; index < 128; ++index)
	{
		assert_true(AAS_SoundSubsystem_RecordPointLight(origin,
			index,
			1.0f,
			0.0f,
			0.0f,
			0.0f,
			100.0f,
			0.0f));
	}
	assert_int_equal(AAS_SoundSubsystem_PointLightCount(), 128);
	test_reset_print_capture();
	assert_true(AAS_SoundSubsystem_RecordPointLight(origin,
		128,
		1.0f,
		0.0f,
		0.0f,
		0.0f,
		100.0f,
		0.0f));
	assert_int_equal(AAS_SoundSubsystem_PointLightCount(), 128);
	assert_string_equal(g_print_message, "WARNING: empty light heap\n");

	test_shutdown_sound_heap();
}

/*
=============
test_dynamic_pointlight_uses_static_hit_point

Prove dynamic attenuation is measured from the BSP sample hit, not the request
point, and pin the deterministic request-point guard when static sampling fails.
=============
*/
static void test_dynamic_pointlight_uses_static_hit_point(void **state)
{
	(void)state;
	pointlight_world_fixture_t fixture;
	test_setup_pointlight_world(&fixture);
	test_init_sound_heap(1);

	vec3_t caller_origin = {500.0f, 500.0f, 500.0f};
	assert_true(AAS_SoundSubsystem_RecordPointLight(caller_origin,
		1,
		10.0f,
		1.9f,
		2.9f,
		3.9f,
		100.0f,
		0.0f));
	aas_pointlight_event_t *event =
		(aas_pointlight_event_t *)AAS_SoundSubsystem_PointLight(0);
	assert_non_null(event);
	VectorSet(event->origin, 16.0f, 0.0f, 0.0f);

	vec3_t query = {16.0f, 0.0f, 64.0f};
	int red;
	int green;
	int blue;
	assert_int_equal(AAS_PointLight(query, &red, &green, &blue), 90);
	assert_int_equal(red, 114);
	assert_int_equal(green, 74);
	assert_int_equal(blue, 59);

	aasworld.bspLightData = NULL;
	VectorCopy(query, event->origin);
	event->radius = 5.0f;
	assert_int_equal(AAS_PointLight(query, &red, &green, &blue), 260);
	assert_int_equal(red, 1);
	assert_int_equal(green, 2);
	assert_int_equal(blue, 3);

	test_shutdown_sound_heap();
	test_clear_pointlight_world();
}

/*
=============
test_make_directory

Create the transient maps directory used by the real BSP loader fixture.
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
test_write_file

Write one transient map fixture without retaining binary repository assets.
=============
*/
static qboolean test_write_file(const char *path, const void *data, size_t size)
{
	FILE *file = fopen(path, "wb");
	if (file == NULL)
	{
		return qfalse;
	}
	size_t written = size > 0U ? fwrite(data, 1U, size, file) : 0U;
	int closed = fclose(file);
	return (size == 0U || written == size) && closed == 0;
}

/*
=============
test_append_bsp_lump

Append a host-little-endian synthetic lump and update its BSP header span.
=============
*/
static void test_append_bsp_lump(unsigned char *buffer,
	size_t buffer_size,
	size_t *offset,
	q2_bsp_header_t *header,
	q2_bsp_lump_id_t lump,
	const void *data,
	size_t size)
{
	assert_non_null(buffer);
	assert_non_null(offset);
	assert_non_null(header);
	assert_true(*offset <= buffer_size);
	assert_true(size <= buffer_size - *offset);
	header->lumps[lump].offset = (int32_t)*offset;
	header->lumps[lump].length = (int32_t)size;
	if (size > 0U)
	{
		assert_non_null(data);
		memcpy(buffer + *offset, data, size);
	}
	*offset += size;
}

/*
=============
test_bsp_pointlight_lumps_load_and_validate

Load real synthetic BSP/AAS files, asserting endian-fixed geometry, calculated
extents and lightdata ownership, then reject an out-of-range lightmap span.
=============
*/
static void test_bsp_pointlight_lumps_load_and_validate(void **state)
{
	(void)state;
	const char *map_name = "__gladiator_pointlight";
	const char *bsp_path = "maps/__gladiator_pointlight.bsp";
	const char *aas_path = "maps/__gladiator_pointlight.aas";
	unlink(bsp_path);
	unlink(aas_path);
	errno = 0;
	int directory_status = test_make_directory("maps");
	qboolean remove_directory = directory_status == 0;
	assert_true(remove_directory || errno == EEXIST);

	aas_bspmodel_t model;
	memset(&model, 0, sizeof(model));
	model.headnode = 0;
	model.numfaces = 1;
	aas_bspnode_t node;
	memset(&node, 0, sizeof(node));
	node.planenum = 0;
	node.children[0] = -1;
	node.children[1] = -1;
	node.numfaces = 1;
	aas_plane_t plane;
	memset(&plane, 0, sizeof(plane));
	VectorSet(plane.normal, 0.0f, 0.0f, 1.0f);
	plane.type = 2;
	aas_bsptexinfo_t texinfo;
	memset(&texinfo, 0, sizeof(texinfo));
	texinfo.vecs[0][0] = 1.0f;
	texinfo.vecs[1][1] = 1.0f;
	vec3_t vertexes[4] = {
		{0.0f, 0.0f, 0.0f},
		{32.0f, 0.0f, 0.0f},
		{32.0f, 32.0f, 0.0f},
		{0.0f, 32.0f, 0.0f},
	};
	aas_bspedge_t edges[4] = {
		{{0, 1}},
		{{1, 2}},
		{{2, 3}},
		{{3, 0}},
	};
	int surfedges[4] = {0, 1, 2, 3};
	aas_bspface_t face;
	memset(&face, 0, sizeof(face));
	face.numedges = 4;
	face.styles[0] = 0;
	face.styles[1] = 0xffU;
	face.styles[2] = 0xffU;
	face.styles[3] = 0xffU;
	unsigned char lightdata[27] = {0};
	lightdata[9] = 80;
	lightdata[10] = 40;
	lightdata[11] = 20;

	size_t bsp_size = sizeof(q2_bsp_header_t) + sizeof(model) +
		sizeof(node) + sizeof(plane) + sizeof(texinfo) + sizeof(vertexes) +
		sizeof(edges) + sizeof(surfedges) + sizeof(face) + sizeof(lightdata);
	unsigned char *bsp_data = (unsigned char *)calloc(bsp_size, 1U);
	assert_non_null(bsp_data);
	q2_bsp_header_t header;
	memset(&header, 0, sizeof(header));
	header.ident = Q2_BSP_IDENT;
	header.version = Q2_BSP_VERSION;
	size_t offset = sizeof(header);
	test_append_bsp_lump(bsp_data, bsp_size, &offset, &header,
		Q2_BSP_LUMP_MODELS, &model, sizeof(model));
	test_append_bsp_lump(bsp_data, bsp_size, &offset, &header,
		Q2_BSP_LUMP_NODES, &node, sizeof(node));
	test_append_bsp_lump(bsp_data, bsp_size, &offset, &header,
		Q2_BSP_LUMP_PLANES, &plane, sizeof(plane));
	test_append_bsp_lump(bsp_data, bsp_size, &offset, &header,
		Q2_BSP_LUMP_TEXINFO, &texinfo, sizeof(texinfo));
	test_append_bsp_lump(bsp_data, bsp_size, &offset, &header,
		Q2_BSP_LUMP_VERTICES, vertexes, sizeof(vertexes));
	test_append_bsp_lump(bsp_data, bsp_size, &offset, &header,
		Q2_BSP_LUMP_EDGES, edges, sizeof(edges));
	test_append_bsp_lump(bsp_data, bsp_size, &offset, &header,
		Q2_BSP_LUMP_SURFEDGES, surfedges, sizeof(surfedges));
	test_append_bsp_lump(bsp_data, bsp_size, &offset, &header,
		Q2_BSP_LUMP_FACES, &face, sizeof(face));
	test_append_bsp_lump(bsp_data, bsp_size, &offset, &header,
		Q2_BSP_LUMP_LIGHTING, lightdata, sizeof(lightdata));
	assert_int_equal(offset, bsp_size);
	memcpy(bsp_data, &header, sizeof(header));
	assert_true(test_write_file(bsp_path, bsp_data, bsp_size));

	q2_aas_header_t aas_header;
	memset(&aas_header, 0, sizeof(aas_header));
	aas_header.ident = Q2_AAS_IDENT;
	aas_header.version = Q2_AAS_VERSION;
	assert_true(test_write_file(aas_path, &aas_header, sizeof(aas_header)));

	BotInterface_SetImportTable(&g_imports);
	LibVar_Init();
	assert_int_equal(AAS_Init(), BLERR_NOERROR);
	assert_int_equal(AAS_ConfigureEntityLimits(6, 2), BLERR_NOERROR);
	aasworld.entities[1].inuse = qtrue;
	assert_int_equal(AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL),
		BLERR_NOERROR);
	assert_int_equal(aasworld.maxEntities, 6);
	assert_int_equal(aasworld.maxClients, 2);
	for (int entnum = 0; entnum < aasworld.maxEntities; ++entnum)
	{
		assert_int_equal(aasworld.entities[entnum].number, entnum);
		assert_int_equal(aasworld.entities[entnum].inuse, entnum == 1);
	}
	assert_int_equal(aasworld.numBspVertexes, 4);
	assert_int_equal(aasworld.numBspEdges, 4);
	assert_int_equal(aasworld.bspSurfEdgeIndexSize, 4);
	assert_int_equal(aasworld.numBspFaces, 1);
	assert_int_equal(aasworld.bspLightDataSize, 27);
	assert_non_null(aasworld.bspSurfaceExtents);
	assert_int_equal(aasworld.bspSurfaceExtents[0].texturemins[0], 0);
	assert_int_equal(aasworld.bspSurfaceExtents[0].texturemins[1], 0);
	assert_int_equal(aasworld.bspSurfaceExtents[0].extents[0], 32);
	assert_int_equal(aasworld.bspSurfaceExtents[0].extents[1], 32);
	vec3_t query = {16.0f, 0.0f, 64.0f};
	int red;
	int green;
	int blue;
	assert_int_equal(AAS_PointLight(query, &red, &green, &blue), 47);
	assert_int_equal(red, 82);
	assert_int_equal(green, 41);
	assert_int_equal(blue, 20);
	AAS_Shutdown();

	face.lightofs = 1024;
	offset = (size_t)header.lumps[Q2_BSP_LUMP_FACES].offset;
	memcpy(bsp_data + offset, &face, sizeof(face));
	assert_true(test_write_file(bsp_path, bsp_data, bsp_size));
	test_reset_print_capture();
	assert_int_equal(AAS_LoadMap(map_name, 0, NULL, 0, NULL, 0, NULL),
		BLERR_CANNOTREADBSPLUMP);
	assert_non_null(strstr(g_print_message, "invalid BSP point-light lightmap span"));

	AAS_Shutdown();
	LibVar_Shutdown();
	PC_ShutdownLexer();
	CRC_ResetSourceChecksums();
	BotMemory_Shutdown();
	BotInterface_SetImportTable(NULL);
	free(bsp_data);
	unlink(bsp_path);
	unlink(aas_path);
	if (remove_directory)
	{
		rmdir("maps");
	}
}

/*
=============
test_sensory_heaps_use_tracked_allocator_paths

Pin the raw cleared metadata pool and both non-clearing 0x34-byte sensory
heaps to the shared tracked allocator, including retail's no-op sound teardown
and process-lifetime release.
=============
*/
static void test_sensory_heaps_use_tracked_allocator_paths(void **state)
{
	(void)state;
	BotInterface_SetImportTable(&g_imports);
	LibVar_Init();
	LibVarSet("max_aaslights", "2");
	test_set_sound_libvars(2,
		2,
		PROJECT_SOURCE_DIR "/missing_sensory_config.c");

	const size_t baseline_bytes = BotMemory_TotalAllocated();
	const size_t baseline_blocks = BotMemory_BlockCount();

	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);
	const size_t sound_blocks = BotMemory_BlockCount();
	assert_true(sound_blocks >= baseline_blocks + 2U);
	AAS_SoundSubsystem_InitPointLightHeap();
	assert_true(BotMemory_TotalAllocated() > baseline_bytes);
	assert_int_equal(BotMemory_BlockCount(), sound_blocks + 1U);

	const size_t live_bytes = BotMemory_TotalAllocated();
	const size_t live_blocks = BotMemory_BlockCount();
	AAS_SoundSubsystem_Shutdown();
	assert_int_equal(BotMemory_TotalAllocated(), live_bytes);
	assert_int_equal(BotMemory_BlockCount(), live_blocks);

	AAS_SoundSubsystem_ResetState();
	PC_ShutdownLexer();
	LibVar_Shutdown();
	CRC_ResetSourceChecksums();
	BotMemory_Shutdown();
	assert_int_equal(BotMemory_TotalAllocated(), 0U);
	assert_int_equal(BotMemory_BlockCount(), 0U);
	BotInterface_SetImportTable(NULL);
}

/*
=============
test_load_soundconfig_text

Write one throwaway soundconfig, run the retail no-argument initialiser over
it, and remove the file again before any assertion can leave it behind.
=============
*/
static void test_load_soundconfig_text(const char *config_path, const char *text)
{
	FILE *config = fopen(config_path, "wb");
	assert_non_null(config);
	assert_true(fputs(text, config) >= 0);
	assert_int_equal(fclose(config), 0);

	test_set_sound_libvars(4, 2, config_path);
	test_reset_print_capture();
	int result = AAS_SoundSubsystem_Init();
	assert_int_equal(unlink(config_path), 0);
	assert_int_equal(result, BLERR_NOERROR);
}

/*
=============
test_soundinfo_defaults_and_bounds_follow_retail_field_table

Pin the raw 0xb0-byte field table at .data 0x1005c070. Every record is zero
filled before it is read - 0x1001c904 memfills 0x2c dwords of 0 over the slot
and there is no default-seeding pass - so an omitted field stays 0. The 80.0f
at 0x1005c0a0 and the 10.0f at 0x1005c0bc are the FT_BOUNDED maxima of volume
and duration, whose type words at 0x1005c094/0x1005c0b0 are 0x0203
(FT_FLOAT | FT_BOUNDED), not initial values. recognition stores 1.0f at
0x1005c0f4 but its type word at 0x1005c0e8 is a plain 0x0003, so that ceiling
is dead weight and out-of-range values pass.
=============
*/
static void test_soundinfo_defaults_and_bounds_follow_retail_field_table(
	void **state)
{
	(void)state;
	BotInterface_SetImportTable(&g_imports);
	LibVar_Init();

	test_load_soundconfig_text(
		PROJECT_SOURCE_DIR "/aas_soundinfo_defaults_tmp.c",
		"#define TEST_DURATION 3.25\n"
		"#define TEST_TYPE 17\n"
		"soundinfo\n"
		"{\n"
		"\tname \"defaults.wav\"\n"
		"\tduration TEST_DURATION\n"
		"\ttype TEST_TYPE\n"
		"}\n"
		"soundinfo\n"
		"{\n"
		"\tname \"ceilings.wav\"\n"
		"\tvolume 80.0\n"
		"\tduration 10.0\n"
		"\trecognition 5.0\n"
		"}\n");
	assert_int_equal(AAS_SoundSubsystem_InfoCount(), 2U);

	int info_index = AAS_SoundSubsystem_FindInfoIndex("defaults.wav");
	assert_true(info_index >= 0);
	const aas_soundinfo_t *info =
		AAS_SoundSubsystem_Info((size_t)info_index);
	assert_non_null(info);
	assert_float_equal(info->volume, 0.0f, 0.0001f);
	assert_float_equal(info->duration, 3.25f, 0.0001f);
	assert_int_equal(info->type, 17);
	assert_float_equal(info->recognition, 0.0f, 0.0001f);
	assert_string_equal(info->string, "");

	info_index = AAS_SoundSubsystem_FindInfoIndex("ceilings.wav");
	assert_true(info_index >= 0);
	info = AAS_SoundSubsystem_Info((size_t)info_index);
	assert_non_null(info);
	assert_float_equal(info->volume, 80.0f, 0.0001f);
	assert_float_equal(info->duration, 10.0f, 0.0001f);
	assert_float_equal(info->recognition, 5.0f, 0.0001f);

	/*
	 * A bounded field one step past its ceiling aborts the whole config read
	 * through SourceError, and retail keeps the records already committed:
	 * ReadStructure failure returns at 0x1001c99a without rewinding
	 * numsoundinfo (0x100669b4).
	 */
	test_load_soundconfig_text(
		PROJECT_SOURCE_DIR "/aas_soundinfo_volume_bound_tmp.c",
		"soundinfo\n"
		"{\n"
		"\tname \"quiet.wav\"\n"
		"\tvolume 80.0\n"
		"}\n"
		"soundinfo\n"
		"{\n"
		"\tname \"loud.wav\"\n"
		"\tvolume 80.5\n"
		"}\n");
	assert_int_equal(AAS_SoundSubsystem_InfoCount(), 1U);
	assert_int_equal(AAS_SoundSubsystem_FindInfoIndex("loud.wav"), -1);
	assert_non_null(strstr(g_print_message,
		"float out of range [0.000000, 80.000000]"));

	test_load_soundconfig_text(
		PROJECT_SOURCE_DIR "/aas_soundinfo_duration_bound_tmp.c",
		"soundinfo\n"
		"{\n"
		"\tname \"long.wav\"\n"
		"\tduration 10.5\n"
		"}\n");
	assert_int_equal(AAS_SoundSubsystem_InfoCount(), 0U);
	assert_non_null(strstr(g_print_message,
		"float out of range [0.000000, 10.000000]"));

	test_shutdown_sound_heap();
}

/*
=============
test_sound_capacity_libvars_follow_retail_ranges

Pin the two raw sound-pool range checks and their fallback writebacks. Sound
setup must consult its own libvars, not a bridge-cached variables structure.
=============
*/
static void test_sound_capacity_libvars_follow_retail_ranges(void **state)
{
	(void)state;
	BotInterface_SetImportTable(&g_imports);
	LibVar_Init();
	LibVarSet("max_aaslights", "0");
	LibVarSet("max_soundinfo", "65536");
	LibVarSet("max_aassounds", "65537");
	LibVarSet("soundconfig", PROJECT_SOURCE_DIR "/dev_tools/assets/sounds.c");

	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);
	assert_string_equal(LibVarGetString("max_soundinfo"), "256");
	assert_string_equal(LibVarGetString("max_aassounds"), "256");

	test_shutdown_sound_heap();
}

/*
=============
test_soundindex_table_borrows_assets_and_folds_name_case

Pin the raw sound-index pointer table built by sub_1001d140: it is tracked,
borrows the engine-owned asset string slots rather than copying them, and
matches soundinfo names case-insensitively. The compare at 0x1001d1c9 goes
through j_sub_10043c10, which tail-calls the _stricmp body at sub_10045cb0
(it folds 'A'..'Z' by adding 0x20 at 0x10045cda/0x10045ce9). Case is the only
transform: no backslash conversion and no "sound/" prefix stripping. An asset
with no matching soundinfo keeps its NULL slot from the memset at 0x1001d18a.
=============
*/
static void test_soundindex_table_borrows_assets_and_folds_name_case(
	void **state)
{
	(void)state;
	test_init_sound_heap(2);
	const size_t baseline_blocks = BotMemory_BlockCount();
	char first_asset[] = "weapons/blastf1a.wav";
	char second_asset[] = "WEAPONS/BLASTF1A.WAV";
	char third_asset[] = "weapons\\blastf1a.wav";
	char fourth_asset[] = "no/such/sound.wav";
	char *assets[] = {first_asset, second_asset, third_asset, fourth_asset};
	assert_true(AAS_SoundSubsystem_RegisterMapAssets(4, assets));
	assert_int_equal(BotMemory_BlockCount(), baseline_blocks + 1U);
	assert_ptr_equal(AAS_SoundSubsystem_AssetName(0), first_asset);
	assert_ptr_equal(AAS_SoundSubsystem_AssetName(1), second_asset);
	assert_ptr_equal(AAS_SoundSubsystem_AssetName(2), third_asset);
	assert_ptr_equal(AAS_SoundSubsystem_AssetName(3), fourth_asset);
	const aas_soundinfo_t *blaster = AAS_SoundSubsystem_InfoForSoundIndex(0);
	assert_non_null(blaster);
	assert_ptr_equal(AAS_SoundSubsystem_InfoForSoundIndex(1), blaster);
	assert_null(AAS_SoundSubsystem_InfoForSoundIndex(2));
	assert_null(AAS_SoundSubsystem_InfoForSoundIndex(3));

	AAS_SoundSubsystem_ClearMapAssets();
	assert_int_equal(BotMemory_BlockCount(), baseline_blocks);
	test_shutdown_sound_heap();
}

/*
=============
main

Run focused retail point-light reconstruction regressions.
=============
*/
int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_static_pointlight_sampling_contract),
		cmocka_unit_test(test_dynamic_pointlight_heap_lifecycle),
		cmocka_unit_test(test_sound_heap_schedules_activates_and_expires_retail_records),
		cmocka_unit_test(
			test_dynamic_pointlight_fractional_capacity_and_sound_independence),
		cmocka_unit_test(test_dynamic_pointlight_capacity_range_fallback),
		cmocka_unit_test(test_dynamic_pointlight_uses_static_hit_point),
		cmocka_unit_test(test_bsp_pointlight_lumps_load_and_validate),
		cmocka_unit_test(test_sensory_heaps_use_tracked_allocator_paths),
		cmocka_unit_test(
			test_soundinfo_defaults_and_bounds_follow_retail_field_table),
		cmocka_unit_test(test_sound_capacity_libvars_follow_retail_ranges),
		cmocka_unit_test(
			test_soundindex_table_borrows_assets_and_folds_name_case),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
