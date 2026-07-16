#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "botlib/aas/aas_debug.h"
#include "botlib/aas/aas_local.h"
#include "botlib/common/l_libvar.h"
#include "q2bridge/bridge.h"
#include "q2bridge/bridge_config.h"

#define AAS_DEBUG_LINE_TEST_EVENTS 280
#define AAS_DEBUG_LINE_TEST_CREATE_RESULTS 3

typedef struct aas_debug_line_test_context_s
{
	bot_import_extended_t imports;
	int print_count;
	int point_contents_count;
	int debug_line_create_count;
	int debug_line_delete_count;
	int debug_line_show_count;
	int create_results[AAS_DEBUG_LINE_TEST_CREATE_RESULTS];
	int create_result_count;
	int create_result_index;
	int next_debug_line;
	int shown_lines[AAS_DEBUG_LINE_TEST_EVENTS];
	int shown_colors[AAS_DEBUG_LINE_TEST_EVENTS];
	bool shown_start_null[AAS_DEBUG_LINE_TEST_EVENTS];
	bool shown_end_null[AAS_DEBUG_LINE_TEST_EVENTS];
	aas_area_t areas[2];
	aas_areasettings_t areasettings[2];
	aas_node_t nodes[2];
	aas_plane_t planes[2];
} aas_debug_line_test_context_t;

static aas_debug_line_test_context_t *g_debugLineContext;

/*
=============
Mock_Print

Capture bridge diagnostics without writing expected failed-create output.
=============
*/
static void Mock_Print(int type, char *fmt, ...)
{
	(void)type;
	(void)fmt;

	if (g_debugLineContext != NULL)
	{
		g_debugLineContext->print_count += 1;
	}
}

/*
=============
Mock_PointContents

Keep the movement-test wrapper out of all retail liquid contents.
=============
*/
static int Mock_PointContents(vec3_t point)
{
	(void)point;

	if (g_debugLineContext != NULL)
	{
		g_debugLineContext->point_contents_count += 1;
	}
	return 0;
}

/*
=============
Mock_DebugLineCreate

Return the queued zero and failed handles before issuing positive identifiers.
=============
*/
static int Mock_DebugLineCreate(void)
{
	if (g_debugLineContext == NULL)
	{
		return 0;
	}

	aas_debug_line_test_context_t *context = g_debugLineContext;
	context->debug_line_create_count += 1;
	if (context->create_result_index < context->create_result_count)
	{
		return context->create_results[context->create_result_index++];
	}

	return context->next_debug_line++;
}

/*
=============
Mock_DebugLineDelete

Count any deletion, which the retail shared pool must never request.
=============
*/
static void Mock_DebugLineDelete(int line)
{
	(void)line;

	if (g_debugLineContext != NULL)
	{
		g_debugLineContext->debug_line_delete_count += 1;
	}
}

/*
=============
Mock_DebugLineShow

Capture line handles and the NULL/-1 hide protocol at the import edge.
=============
*/
static void Mock_DebugLineShow(int line, vec3_t start, vec3_t end, int color)
{
	if (g_debugLineContext == NULL)
	{
		return;
	}

	aas_debug_line_test_context_t *context = g_debugLineContext;
	int index = context->debug_line_show_count;
	if (index >= 0 && index < AAS_DEBUG_LINE_TEST_EVENTS)
	{
		context->shown_lines[index] = line;
		context->shown_colors[index] = color;
		context->shown_start_null[index] = start == NULL;
		context->shown_end_null[index] = end == NULL;
	}
	context->debug_line_show_count += 1;
}

/*
=============
ResetDebugLineEvents

Clear captured import calls without resetting the retail pool or create queue.
=============
*/
static void ResetDebugLineEvents(aas_debug_line_test_context_t *context)
{
	assert_non_null(context);

	context->debug_line_create_count = 0;
	context->debug_line_delete_count = 0;
	context->debug_line_show_count = 0;
	memset(context->shown_lines, 0, sizeof(context->shown_lines));
	memset(context->shown_colors, 0, sizeof(context->shown_colors));
	memset(context->shown_start_null, 0, sizeof(context->shown_start_null));
	memset(context->shown_end_null, 0, sizeof(context->shown_end_null));
}

/*
=============
ConfigureOpenAASWorld

Build one open AAS area for the visual movement-test wrapper.
=============
*/
static void ConfigureOpenAASWorld(aas_debug_line_test_context_t *context)
{
	assert_non_null(context);

	memset(&aasworld, 0, sizeof(aasworld));
	aasworld.loaded = qtrue;
	aasworld.numAreas = 2;
	aasworld.areas = context->areas;
	aasworld.numAreaSettings = 2;
	aasworld.areasettings = context->areasettings;
	aasworld.numNodes = 2;
	aasworld.nodes = context->nodes;
	aasworld.numPlanes = 2;
	aasworld.planes = context->planes;

	context->areas[1].areanum = 1;
	VectorSet(context->areas[1].mins, -4096.0f, -4096.0f, -4096.0f);
	VectorSet(context->areas[1].maxs, 4096.0f, 4096.0f, 4096.0f);
	context->areasettings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	context->nodes[1].planenum = 0;
	context->nodes[1].children[0] = -1;
	context->nodes[1].children[1] = -1;
	VectorSet(context->planes[0].normal, 1.0f, 0.0f, 0.0f);
	VectorSet(context->planes[1].normal, -1.0f, 0.0f, 0.0f);
}

/*
=============
SetupDebugLineTest

Install the exact retail debug imports and movement configuration.
=============
*/
static int SetupDebugLineTest(void **state)
{
	aas_debug_line_test_context_t *context =
		(aas_debug_line_test_context_t *)calloc(1, sizeof(*context));
	assert_non_null(context);

	context->imports.Print = Mock_Print;
	context->imports.PointContents = Mock_PointContents;
	context->imports.DebugLineCreate = Mock_DebugLineCreate;
	context->imports.DebugLineDelete = Mock_DebugLineDelete;
	context->imports.DebugLineShow = Mock_DebugLineShow;
	context->create_results[0] = 0;
	context->create_results[1] = 0;
	context->create_results[2] = -1;
	context->create_result_count = AAS_DEBUG_LINE_TEST_CREATE_RESULTS;
	context->next_debug_line = 100;

	g_debugLineContext = context;
	Q2Bridge_SetImportTable(&context->imports);
	Q2Bridge_SetDebugLinesEnabled(true);
	LibVar_Init();
	assert_true(BridgeConfig_Init());
	ConfigureOpenAASWorld(context);

	*state = context;
	return 0;
}

/*
=============
TeardownDebugLineTest

Release movement configuration after the single-process pool contract test.
=============
*/
static int TeardownDebugLineTest(void **state)
{
	aas_debug_line_test_context_t *context =
		(aas_debug_line_test_context_t *)(state != NULL ? *state : NULL);

	BridgeConfig_Shutdown();
	LibVar_Shutdown();
	Q2Bridge_SetDebugLinesEnabled(false);
	Q2Bridge_ClearImportTable();
	memset(&aasworld, 0, sizeof(aasworld));
	g_debugLineContext = NULL;
	free(context);
	return 0;
}

/*
=============
test_retail_shared_debug_line_pool_and_movement_wrapper

Pin the 256-slot lazy pool, zero/failed create handling, hide protocol,
exhaustion, reuse, and the wrapper-only clear boundary.
=============
*/
static void test_retail_shared_debug_line_pool_and_movement_wrapper(void **state)
{
	aas_debug_line_test_context_t *context =
		(aas_debug_line_test_context_t *)(*state);
	vec3_t start = {1.0f, 2.0f, 3.0f};
	vec3_t end = {4.0f, 5.0f, 6.0f};
	int initial_print_count = context->print_count;

	AAS_DebugLine(start, end, LINECOLOR_RED);
	AAS_DebugLine(start, end, LINECOLOR_GREEN);
	AAS_DebugLine(start, end, LINECOLOR_BLUE);
	assert_int_equal(context->debug_line_create_count, 3);
	assert_int_equal(context->debug_line_show_count, 3);
	assert_int_equal(context->shown_lines[0], 0);
	assert_int_equal(context->shown_lines[1], 0);
	assert_int_equal(context->shown_lines[2], -1);
	assert_int_equal(context->print_count, initial_print_count + 1);
	assert_int_equal(context->debug_line_delete_count, 0);

	AAS_ClearShownDebugLines();
	assert_int_equal(context->debug_line_show_count, 4);
	assert_int_equal(context->shown_lines[3], -1);
	assert_int_equal(context->shown_colors[3], LINECOLOR_NONE);
	assert_true(context->shown_start_null[3]);
	assert_true(context->shown_end_null[3]);

	ResetDebugLineEvents(context);
	for (int line = 0; line < 257; ++line)
	{
		AAS_DebugLine(start, end, LINECOLOR_YELLOW);
	}
	assert_int_equal(context->debug_line_create_count, 255);
	assert_int_equal(context->debug_line_show_count, 256);
	assert_int_equal(context->shown_lines[0], -1);
	assert_int_equal(context->shown_lines[1], 100);
	assert_int_equal(context->shown_lines[255], 354);
	assert_int_equal(context->debug_line_delete_count, 0);

	ResetDebugLineEvents(context);
	AAS_ClearShownDebugLines();
	assert_int_equal(context->debug_line_create_count, 0);
	assert_int_equal(context->debug_line_show_count, 256);
	for (int line = 0; line < 256; ++line)
	{
		assert_int_equal(context->shown_colors[line], LINECOLOR_NONE);
		assert_true(context->shown_start_null[line]);
		assert_true(context->shown_end_null[line]);
	}
	assert_int_equal(context->shown_lines[0], -1);
	assert_int_equal(context->shown_lines[255], 354);
	assert_int_equal(context->debug_line_delete_count, 0);

	ResetDebugLineEvents(context);
	vec3_t origin = {0.0f, 0.0f, 32.0f};
	vec3_t direction = {1.0f, 0.0f, 5.0f};
	AAS_TestMovementPrediction(7, origin, direction);
	assert_int_equal(context->debug_line_create_count, 0);
	assert_int_equal(context->debug_line_show_count, 269);
	assert_int_equal(context->shown_colors[0], LINECOLOR_NONE);
	assert_true(context->shown_start_null[0]);
	assert_true(context->shown_end_null[255]);
	assert_int_equal(context->shown_colors[256], (int)LINECOLOR_RED);
	assert_false(context->shown_start_null[256]);
	assert_false(context->shown_end_null[256]);
	assert_float_equal(direction[0], 1.0f, 0.0001f);
	assert_float_equal(direction[2], 0.0f, 0.0001f);
	assert_int_equal(context->debug_line_delete_count, 0);
}

/*
=============
main

Run the isolated retail AAS debug-line pool contract.
=============
*/
int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup_teardown(
			test_retail_shared_debug_line_pool_and_movement_wrapper,
			SetupDebugLineTest,
			TeardownDebugLineTest),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
