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
#include "botlib/common/l_libvar.h"
#include "botlib/interface/botlib_interface.h"

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

	botlib_library_variables_t variables;
	memset(&variables, 0, sizeof(variables));
	variables.max_soundinfo = 64;
	variables.max_aassounds = 4;
	snprintf(variables.soundconfig,
		sizeof(variables.soundconfig),
		"%s/dev_tools/assets/sounds.c",
		PROJECT_SOURCE_DIR);
	assert_int_equal(AAS_SoundSubsystem_Init(&variables), BLERR_NOERROR);
	AAS_SoundSubsystem_SetFrameTime(0.0f);
}

/*
=============
test_shutdown_sound_heap

Release sensory and libvar state after an isolated heap test.
=============
*/
static void test_shutdown_sound_heap(void)
{
	AAS_SoundSubsystem_Shutdown();
	LibVar_Shutdown();
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
	assert_float_equal(first_origin[0], 0.0f, 0.0f);
	assert_float_equal(first_origin[1], 0.0f, 0.0f);
	assert_float_equal(first_origin[2], 0.0f, 0.0f);
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
test_dynamic_pointlight_fractional_capacity_and_sound_independence

Pin __ftol-before-range-check capacity handling and prove point-light setup is
independent when max_soundinfo disables the sound metadata subsystem.
=============
*/
static void test_dynamic_pointlight_fractional_capacity_and_sound_independence(
	void **state)
{
	(void)state;
	BotInterface_SetImportTable(&g_imports);
	LibVar_Init();
	botlib_library_variables_t variables;
	memset(&variables, 0, sizeof(variables));

	LibVarSet("max_aaslights", "-0.9");
	test_reset_print_capture();
	assert_int_equal(AAS_SoundSubsystem_Init(&variables), BLERR_NOERROR);
	assert_false(g_saw_max_lights_range_error);
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

	AAS_SoundSubsystem_Shutdown();
	LibVarSet("max_aaslights", "65536.9");
	test_reset_print_capture();
	assert_int_equal(AAS_SoundSubsystem_Init(&variables), BLERR_NOERROR);
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

	botlib_library_variables_t variables;
	memset(&variables, 0, sizeof(variables));
	variables.max_soundinfo = 1024;
	variables.max_aassounds = 4;
	snprintf(variables.soundconfig,
		sizeof(variables.soundconfig),
		"%s/dev_tools/assets/sounds.c",
		PROJECT_SOURCE_DIR);
	test_reset_print_capture();
	assert_int_equal(AAS_SoundSubsystem_Init(&variables), BLERR_NOERROR);
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
		assert_false(aasworld.entities[entnum].inuse);
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
main

Run focused retail point-light reconstruction regressions.
=============
*/
int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_static_pointlight_sampling_contract),
		cmocka_unit_test(test_dynamic_pointlight_heap_lifecycle),
		cmocka_unit_test(
			test_dynamic_pointlight_fractional_capacity_and_sound_independence),
		cmocka_unit_test(test_dynamic_pointlight_capacity_range_fallback),
		cmocka_unit_test(test_dynamic_pointlight_uses_static_hit_point),
		cmocka_unit_test(test_bsp_pointlight_lumps_load_and_validate),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
