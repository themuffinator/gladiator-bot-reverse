#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>

#include <cmocka.h>

#include "botlib/ai_move/bot_move.h"
#include "botlib/common/l_libvar.h"

typedef struct test_move_libvar_s
{
	const char *name;
	const char *value;
} test_move_libvar_t;

/*
=============
test_setup

Initialise the libvar registry used by the retail movement setup contract.
=============
*/
static int test_setup(void **state)
{
	(void)state;
	LibVar_Init();
	return 0;
}

/*
=============
test_teardown

Release movement handles and the isolated libvar registry after each test.
=============
*/
static int test_teardown(void **state)
{
	(void)state;
	BotShutdownMoveAI();
	LibVar_Shutdown();
	return 0;
}

/*
=============
test_retail_move_structure_layout

Pin the exact public structures consumed by the retail movement routines.
=============
*/
static void test_retail_move_structure_layout(void **state)
{
	(void)state;

	assert_int_equal(sizeof(bot_moveresult_t), 0x30);
	assert_int_equal(offsetof(bot_moveresult_t, movedir), 0x18);
	assert_int_equal(offsetof(bot_moveresult_t, ideal_viewangles), 0x24);

	assert_int_equal(sizeof(bot_movestate_t), 0x80);
	assert_int_equal(offsetof(bot_movestate_t, areanum), 0x40);
	assert_int_equal(offsetof(bot_movestate_t, lastreachnum), 0x4c);
	assert_int_equal(offsetof(bot_movestate_t, moveflags), 0x60);
	assert_int_equal(offsetof(bot_movestate_t, reachability_time), 0x70);
	assert_int_equal(offsetof(bot_movestate_t, avoidreach), 0x74);
	assert_int_equal(offsetof(bot_movestate_t, avoidreachtimes), 0x78);
	assert_int_equal(offsetof(bot_movestate_t, avoidreachtries), 0x7c);
}

/*
=============
test_clear_move_result_only_clears_retail_prefix

Verify the original helper clears six dwords and leaves both vector tails alone.
=============
*/
static void test_clear_move_result_only_clears_retail_prefix(void **state)
{
	(void)state;

	bot_moveresult_t result;
	memset(&result, 0xa5, sizeof(result));

	assert_ptr_equal(BotClearMoveResult(&result), &result);

	const unsigned char *bytes = (const unsigned char *)&result;
	for (size_t index = 0; index < offsetof(bot_moveresult_t, movedir); ++index)
	{
		assert_int_equal(bytes[index], 0);
	}
	for (size_t index = offsetof(bot_moveresult_t, movedir); index < sizeof(result); ++index)
	{
		assert_int_equal(bytes[index], 0xa5);
	}
}

/*
=============
test_reset_move_state_clears_exact_retail_state

Verify the pointer core resets all 128 retail movement-state bytes.
=============
*/
static void test_reset_move_state_clears_exact_retail_state(void **state)
{
	(void)state;

	bot_movestate_t movestate;
	memset(&movestate, 0xa5, sizeof(movestate));

	assert_int_equal(BotResetMoveState(&movestate), 0);

	const unsigned char *bytes = (const unsigned char *)&movestate;
	for (size_t index = 0; index < sizeof(movestate); ++index)
	{
		assert_int_equal(bytes[index], 0);
	}
}

/*
=============
test_reset_avoid_reach_only_clears_retail_slot

Pin the single retail avoid-reach slot and its unusual return address.
=============
*/
static void test_reset_avoid_reach_only_clears_retail_slot(void **state)
{
	(void)state;

	bot_movestate_t movestate;
	memset(&movestate, 0, sizeof(movestate));
	movestate.lastreachnum = 73;
	movestate.reachability_time = 19.0f;
	movestate.avoidreach[0] = 11;
	movestate.avoidreachtimes[0] = 12.0f;
	movestate.avoidreachtries[0] = 13;

	assert_ptr_equal(BotResetAvoidReach(&movestate), &movestate.avoidreachtries[0]);
	assert_int_equal(movestate.avoidreach[0], 0);
	assert_float_equal(movestate.avoidreachtimes[0], 0.0f, 0.0001f);
	assert_int_equal(movestate.avoidreachtries[0], 0);
	assert_int_equal(movestate.lastreachnum, 73);
	assert_float_equal(movestate.reachability_time, 19.0f, 0.0001f);
}

/*
=============
test_handle_init_refreshes_frame_fields_only

Verify the handle adapter updates frame input while retaining route bookkeeping.
=============
*/
static void test_handle_init_refreshes_frame_fields_only(void **state)
{
	(void)state;

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *movestate = BotMoveStateFromHandle(handle);
	assert_non_null(movestate);

	movestate->lastreachnum = 17;
	movestate->lastgoalareanum = 23;
	movestate->reachability_time = 42.0f;
	movestate->moveflags = MFL_GRAPPLEATTACHED | MFL_ONGROUND | MFL_WATERJUMP;

	bot_initmove_t init;
	memset(&init, 0, sizeof(init));
	VectorSet(init.origin, 4.0f, 8.0f, 16.0f);
	VectorSet(init.velocity, 1.0f, 2.0f, 3.0f);
	VectorSet(init.viewoffset, 0.0f, 0.0f, 22.0f);
	VectorSet(init.viewangles, 10.0f, 20.0f, 30.0f);
	init.entitynum = 5;
	init.client = 2;
	init.thinktime = 0.1f;
	init.presencetype = PRESENCE_NORMAL;
	init.or_moveflags = MFL_TELEPORTED;

	BotInitMoveStateHandle(handle, &init);

	assert_float_equal(movestate->origin[0], 4.0f, 0.0001f);
	assert_float_equal(movestate->velocity[2], 3.0f, 0.0001f);
	assert_float_equal(movestate->viewoffset[2], 22.0f, 0.0001f);
	assert_float_equal(movestate->viewangles[1], 20.0f, 0.0001f);
	assert_int_equal(movestate->entitynum, 5);
	assert_int_equal(movestate->client, 2);
	assert_float_equal(movestate->thinktime, 0.1f, 0.0001f);
	assert_int_equal(movestate->presencetype, PRESENCE_NORMAL);
	assert_int_equal(movestate->lastreachnum, 17);
	assert_int_equal(movestate->lastgoalareanum, 23);
	assert_float_equal(movestate->reachability_time, 42.0f, 0.0001f);
	assert_true((movestate->moveflags & MFL_GRAPPLEATTACHED) != 0);
	assert_true((movestate->moveflags & MFL_TELEPORTED) != 0);
	assert_false((movestate->moveflags & MFL_ONGROUND) != 0);
	assert_false((movestate->moveflags & MFL_WATERJUMP) != 0);

	BotFreeMoveStateHandle(handle);
}

/*
=============
test_setup_move_ai_registers_retail_libvars

Pin all names and defaults registered by the original movement setup routine.
=============
*/
static void test_setup_move_ai_registers_retail_libvars(void **state)
{
	(void)state;

	static const test_move_libvar_t expected[] = {
		{"sv_friction", "6"},
		{"sv_stopspeed", "100"},
		{"sv_gravity", "800"},
		{"sv_waterfriction", "1"},
		{"sv_watergravity", "400"},
		{"sv_maxvelocity", "300"},
		{"sv_maxwalkvelocity", "300"},
		{"sv_maxcrouchvelocity", "100"},
		{"sv_maxswimvelocity", "150"},
		{"sv_maxacceleration", "2200"},
		{"sv_airaccelerate", "0"},
		{"sv_step", "18"},
		{"sv_maxbarrier", "50"},
		{"sv_maxsteepness", "0.7"},
		{"sv_jumpvel", "224"},
		{"sv_maxwaterjump", "21"},
	};

	assert_true(BotSetupMoveAI() != 0);
	for (size_t index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index)
	{
		libvar_t *libvar = LibVarGet(expected[index].name);
		assert_non_null(libvar);
		assert_string_equal(libvar->name, expected[index].name);
		assert_string_equal(libvar->string, expected[index].value);
	}
}

/*
=============
main

Run the focused retail movement ABI and setup-contract tests.
=============
*/
int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup_teardown(test_retail_move_structure_layout,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_clear_move_result_only_clears_retail_prefix,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_reset_move_state_clears_exact_retail_state,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_reset_avoid_reach_only_clears_retail_slot,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_handle_init_refreshes_frame_fields_only,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_setup_move_ai_registers_retail_libvars,
			test_setup,
			test_teardown),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
