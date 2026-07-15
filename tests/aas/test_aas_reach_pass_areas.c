#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "botlib/aas/aas_local.h"
#include "botlib/interface/botlib_interface.h"

#define PASS_AREA_TEST_MAX_AREAS 40
#define PASS_AREA_TEST_MAX_REACHABILITY 11
#define PASS_AREA_TEST_MAX_LEAVES 34

typedef struct pass_area_test_context_s
{
	botlib_import_table_t imports;
	int print_count;
	int last_print_type;
	aas_reachability_t reachability[PASS_AREA_TEST_MAX_REACHABILITY];
	aas_areasettings_t areasettings[PASS_AREA_TEST_MAX_AREAS];
	aas_node_t nodes[PASS_AREA_TEST_MAX_LEAVES];
	aas_plane_t planes[PASS_AREA_TEST_MAX_LEAVES - 1];
} pass_area_test_context_t;

static pass_area_test_context_t *g_passAreaContext;

/*
=============
Mock_Print

Capture trace diagnostics without writing expected invalid-tree output.
=============
*/
static void Mock_Print(int type, const char *format, ...)
{
	(void)format;

	if (g_passAreaContext != NULL)
	{
		g_passAreaContext->print_count += 1;
		g_passAreaContext->last_print_type = type;
	}
}

/*
=============
ConfigurePassAreaWorld

Reset the synthetic AAS world and install one reachability array.
=============
*/
static void ConfigurePassAreaWorld(pass_area_test_context_t *context,
	int numreachability)
{
	assert_non_null(context);
	assert_true(numreachability > 0);
	assert_true(numreachability <= PASS_AREA_TEST_MAX_REACHABILITY);

	AAS_ClearReachabilityData();
	memset(&aasworld, 0, sizeof(aasworld));
	memset(context->reachability, 0, sizeof(context->reachability));
	memset(context->areasettings, 0, sizeof(context->areasettings));
	memset(context->nodes, 0, sizeof(context->nodes));
	memset(context->planes, 0, sizeof(context->planes));
	context->print_count = 0;
	context->last_print_type = 0;

	aasworld.loaded = qtrue;
	aasworld.numAreas = PASS_AREA_TEST_MAX_AREAS;
	aasworld.numAreaSettings = PASS_AREA_TEST_MAX_AREAS;
	aasworld.areasettings = context->areasettings;
	aasworld.numReachability = numreachability;
	aasworld.reachability = context->reachability;
}

/*
=============
ConfigureTraceChain

Build an ordered BSP slab chain whose leaves follow the supplied area sequence.
=============
*/
static void ConfigureTraceChain(pass_area_test_context_t *context,
	int axis,
	const int *leafareas,
	int numleafareas)
{
	assert_non_null(context);
	assert_non_null(leafareas);
	assert_true(axis >= 0 && axis < 3);
	assert_true(numleafareas >= 2);
	assert_true(numleafareas <= PASS_AREA_TEST_MAX_LEAVES);

	memset(context->nodes, 0, sizeof(context->nodes));
	memset(context->planes, 0, sizeof(context->planes));
	for (int split = 0; split < numleafareas - 1; ++split)
	{
		int nodenum = split + 1;
		context->planes[split].normal[axis] = 1.0f;
		context->planes[split].dist = (float)(split + 1);
		context->planes[split].type = axis;
		context->nodes[nodenum].planenum = split;
		context->nodes[nodenum].children[1] = -leafareas[split];
		context->nodes[nodenum].children[0] =
			split + 1 < numleafareas - 1
				? nodenum + 1
				: -leafareas[numleafareas - 1];
	}

	aasworld.numNodes = numleafareas;
	aasworld.nodes = context->nodes;
	aasworld.numPlanes = numleafareas - 1;
	aasworld.planes = context->planes;
}

/*
=============
ConfigureInvalidPlanTree

Install a root node that reports an error if a reachability tries to trace it.
=============
*/
static void ConfigureInvalidPlanTree(pass_area_test_context_t *context)
{
	assert_non_null(context);

	memset(context->nodes, 0, sizeof(context->nodes));
	memset(context->planes, 0, sizeof(context->planes));
	context->nodes[1].planenum = 1;
	aasworld.numNodes = 2;
	aasworld.nodes = context->nodes;
	aasworld.numPlanes = 1;
	aasworld.planes = context->planes;
}

/*
=============
ConfigurePartialFailureTree

Build one valid near leaf followed by an out-of-range far node.
=============
*/
static void ConfigurePartialFailureTree(pass_area_test_context_t *context)
{
	assert_non_null(context);

	memset(context->nodes, 0, sizeof(context->nodes));
	memset(context->planes, 0, sizeof(context->planes));
	context->planes[0].normal[2] = 1.0f;
	context->planes[0].dist = 1.0f;
	context->planes[0].type = 2;
	context->nodes[1].planenum = 0;
	context->nodes[1].children[1] = -7;
	context->nodes[1].children[0] = 2;
	aasworld.numNodes = 2;
	aasworld.nodes = context->nodes;
	aasworld.numPlanes = 1;
	aasworld.planes = context->planes;
}

/*
=============
ConfigureReachability

Populate one synthetic reachability with endpoints crossing the slab tree.
=============
*/
static void ConfigureReachability(pass_area_test_context_t *context,
	int reachnum,
	int traveltype,
	const vec3_t start,
	const vec3_t end,
	int destination)
{
	assert_non_null(context);
	assert_true(reachnum > 0);
	assert_true(reachnum < aasworld.numReachability);

	aas_reachability_t *reach = &context->reachability[reachnum];
	memset(reach, 0, sizeof(*reach));
	reach->areanum = destination;
	reach->traveltype = traveltype;
	VectorCopy(start, reach->start);
	VectorCopy(end, reach->end);
}

/*
=============
ConfigureVerticalPassReachabilities

Create the four travel types whose direct or vertical traces have pass areas.
=============
*/
static void ConfigureVerticalPassReachabilities(pass_area_test_context_t *context,
	float endheight,
	int destination)
{
	vec3_t lowleft = {4.0f, 8.0f, 0.0f};
	vec3_t highleft = {4.0f, 8.0f, endheight};
	vec3_t lowright = {64.0f, 96.0f, 0.0f};
	vec3_t highright = {64.0f, 96.0f, endheight};

	ConfigureReachability(context,
		1,
		TRAVEL_BARRIERJUMP,
		lowleft,
		highright,
		destination);
	ConfigureReachability(context,
		2,
		TRAVEL_WATERJUMP,
		lowleft,
		highright,
		destination);
	ConfigureReachability(context,
		3,
		TRAVEL_WALKOFFLEDGE,
		lowright,
		highleft,
		destination);
	ConfigureReachability(context,
		4,
		TRAVEL_GRAPPLEHOOK,
		lowleft,
		highleft,
		destination);
}

/*
=============
ConfigureEmptyArcMoverReachabilities

Create the six trajectory/model travel types that intentionally stay empty.
=============
*/
static void ConfigureEmptyArcMoverReachabilities(pass_area_test_context_t *context,
	int firstreachnum,
	float endheight,
	int destination)
{
	assert_non_null(context);
	assert_true(firstreachnum > 0);
	assert_true(firstreachnum + 5 < aasworld.numReachability);

	vec3_t start = {0.0f, 0.0f, 0.0f};
	vec3_t end = {0.0f, 0.0f, endheight};
	const int traveltypes[6] = {
		TRAVEL_JUMP,
		TRAVEL_ROCKETJUMP,
		TRAVEL_BFGJUMP,
		TRAVEL_JUMPPAD,
		TRAVEL_ELEVATOR,
		TRAVEL_FUNCBOB
	};
	for (int index = 0; index < 6; ++index)
	{
		int reachnum = firstreachnum + index;
		ConfigureReachability(context,
			reachnum,
			traveltypes[index],
			start,
			end,
			destination);
		context->reachability[reachnum].facenum = 100 + index;
	}
}

/*
=============
AssertReachAreaSequence

Verify one compacted pass-area record and its exact stored sequence.
=============
*/
static void AssertReachAreaSequence(int reachnum,
	int expectedfirst,
	const int *expectedareas,
	int expectedcount)
{
	assert_non_null(aasworld.reachabilityAreas);
	assert_int_equal(aasworld.reachabilityAreas[reachnum].firstarea, expectedfirst);
	assert_int_equal(aasworld.reachabilityAreas[reachnum].numareas, expectedcount);
	for (int index = 0; index < expectedcount; ++index)
	{
		assert_int_equal(aasworld.reachabilityAreaIndex[expectedfirst + index],
			expectedareas[index]);
	}
}

/*
=============
SetupPassAreaTest

Allocate one isolated world and capture diagnostics from the bridge import.
=============
*/
static int SetupPassAreaTest(void **state)
{
	pass_area_test_context_t *context =
		(pass_area_test_context_t *)calloc(1, sizeof(*context));
	assert_non_null(context);

	context->imports.Print = Mock_Print;
	g_passAreaContext = context;
	BotInterface_SetImportTable(&context->imports);
	memset(&aasworld, 0, sizeof(aasworld));
	*state = context;
	return 0;
}

/*
=============
TeardownPassAreaTest

Release generated indexes before discarding the synthetic base arrays.
=============
*/
static int TeardownPassAreaTest(void **state)
{
	pass_area_test_context_t *context =
		(pass_area_test_context_t *)(state != NULL ? *state : NULL);

	AAS_ClearReachabilityData();
	memset(&aasworld, 0, sizeof(aasworld));
	BotInterface_SetImportTable(NULL);
	g_passAreaContext = NULL;
	free(context);
	return 0;
}

/*
=============
test_arc_and_mover_reachabilities_keep_zero_pass_areas

Pin the explicit no-op for jump, rocket/BFG jump, jumppad, elevator, and funcbob.
=============
*/
static void test_arc_and_mover_reachabilities_keep_zero_pass_areas(void **state)
{
	pass_area_test_context_t *context =
		(pass_area_test_context_t *)(*state);
	ConfigurePassAreaWorld(context, 7);
	ConfigureInvalidPlanTree(context);
	ConfigureEmptyArcMoverReachabilities(context, 1, 32.0f, 3);

	assert_int_equal(AAS_PrepareReachability(), BLERR_NOERROR);
	assert_non_null(aasworld.reachabilityAreas);
	assert_non_null(aasworld.reachabilityAreaIndex);
	assert_int_equal(aasworld.reachabilityAreaIndexSize, 0);
	assert_int_equal(context->print_count, 0);
	for (int reachnum = 0; reachnum < 7; ++reachnum)
	{
		assert_int_equal(aasworld.reachabilityAreas[reachnum].firstarea, 0);
		assert_int_equal(aasworld.reachabilityAreas[reachnum].numareas, 0);
	}
}

/*
=============
test_direct_pass_areas_keep_order_duplicates_and_destination

Pin verbatim start-to-end storage, duplicate leaves, and destination inclusion.
=============
*/
static void test_direct_pass_areas_keep_order_duplicates_and_destination(void **state)
{
	pass_area_test_context_t *context =
		(pass_area_test_context_t *)(*state);
	ConfigurePassAreaWorld(context, 11);
	const int expectedareas[4] = {1, 2, 1, 3};
	ConfigureTraceChain(context, 2, expectedareas, 4);
	ConfigureVerticalPassReachabilities(context, 3.5f, 3);
	ConfigureEmptyArcMoverReachabilities(context, 5, 3.5f, 3);

	assert_int_equal(AAS_PrepareReachability(), BLERR_NOERROR);
	assert_int_equal(context->print_count, 0);
	assert_int_equal(aasworld.reachabilityAreaIndexSize, 16);
	for (int reachnum = 1; reachnum <= 4; ++reachnum)
	{
		AssertReachAreaSequence(reachnum,
			(reachnum - 1) * 4,
			expectedareas,
			4);
	}
	for (int reachnum = 5; reachnum <= 10; ++reachnum)
	{
		assert_int_equal(aasworld.reachabilityAreas[reachnum].firstarea, 16);
		assert_int_equal(aasworld.reachabilityAreas[reachnum].numareas, 0);
	}
}

/*
=============
test_direct_pass_areas_stop_at_thirty_two

Pin the fixed 32-area cap independently for every traced travel branch.
=============
*/
static void test_direct_pass_areas_stop_at_thirty_two(void **state)
{
	pass_area_test_context_t *context =
		(pass_area_test_context_t *)(*state);
	ConfigurePassAreaWorld(context, 11);
	int leafareas[PASS_AREA_TEST_MAX_LEAVES];
	for (int index = 0; index < PASS_AREA_TEST_MAX_LEAVES; ++index)
	{
		leafareas[index] = index + 1;
	}
	ConfigureTraceChain(context,
		2,
		leafareas,
		PASS_AREA_TEST_MAX_LEAVES);
	ConfigureVerticalPassReachabilities(context,
		(float)PASS_AREA_TEST_MAX_LEAVES,
		PASS_AREA_TEST_MAX_LEAVES);
	ConfigureEmptyArcMoverReachabilities(context,
		5,
		(float)PASS_AREA_TEST_MAX_LEAVES,
		PASS_AREA_TEST_MAX_LEAVES);

	assert_int_equal(AAS_PrepareReachability(), BLERR_NOERROR);
	assert_int_equal(context->print_count, 0);
	assert_int_equal(aasworld.reachabilityAreaIndexSize, 4 * 32);
	for (int reachnum = 1; reachnum <= 4; ++reachnum)
	{
		AssertReachAreaSequence(reachnum,
			(reachnum - 1) * 32,
			leafareas,
			32);
	}
	for (int reachnum = 5; reachnum <= 10; ++reachnum)
	{
		assert_int_equal(aasworld.reachabilityAreas[reachnum].firstarea, 4 * 32);
		assert_int_equal(aasworld.reachabilityAreas[reachnum].numareas, 0);
	}
}

/*
=============
test_partial_trace_failure_keeps_collected_pass_areas

Pin partial-result retention when traversal fails after visiting a valid leaf.
=============
*/
static void test_partial_trace_failure_keeps_collected_pass_areas(void **state)
{
	pass_area_test_context_t *context =
		(pass_area_test_context_t *)(*state);
	ConfigurePassAreaWorld(context, 2);
	ConfigurePartialFailureTree(context);
	vec3_t start = {0.0f, 0.0f, 0.0f};
	vec3_t end = {0.0f, 0.0f, 2.0f};
	ConfigureReachability(context,
		1,
		TRAVEL_GRAPPLEHOOK,
		start,
		end,
		7);

	assert_int_equal(AAS_PrepareReachability(), BLERR_NOERROR);
	assert_int_equal(context->print_count, 1);
	assert_int_equal(context->last_print_type, PRT_ERROR);
	assert_int_equal(aasworld.reachabilityAreaIndexSize, 1);
	const int expectedarea[1] = {7};
	AssertReachAreaSequence(1, 0, expectedarea, 1);
}

/*
=============
main

Run the synthetic reachability pass-area contract tests.
=============
*/
int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup_teardown(
			test_arc_and_mover_reachabilities_keep_zero_pass_areas,
			SetupPassAreaTest,
			TeardownPassAreaTest),
		cmocka_unit_test_setup_teardown(
			test_direct_pass_areas_keep_order_duplicates_and_destination,
			SetupPassAreaTest,
			TeardownPassAreaTest),
		cmocka_unit_test_setup_teardown(
			test_direct_pass_areas_stop_at_thirty_two,
			SetupPassAreaTest,
			TeardownPassAreaTest),
		cmocka_unit_test_setup_teardown(
			test_partial_trace_failure_keeps_collected_pass_areas,
			SetupPassAreaTest,
			TeardownPassAreaTest)
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
