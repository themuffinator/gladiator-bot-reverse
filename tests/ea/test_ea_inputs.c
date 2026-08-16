#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <cmocka.h>

#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/ea/ea_local.h"
#include "botlib/interface/botlib_interface.h"
#include "q2bridge/bridge.h"
#include "q2bridge/bridge_config.h"

#define TEST_MAX_COMMANDS 8
#define TEST_MAX_COMMAND_ARGUMENTS 9
#define TEST_MAX_COMMAND_LENGTH 256

typedef char ea_actionflags_prefix_offset_must_be_32[
	(offsetof(bot_input_t, actionflags) == 32U) ? 1 : -1];
typedef char ea_weapon_extension_offset_must_be_36[
	(offsetof(bot_input_t, weapon) == 36U) ? 1 : -1];

typedef enum capture_mode_e
{
	CAPTURE_NONE = 0,
	CAPTURE_LEGACY,
	CAPTURE_EA,
	CAPTURE_RETAIL_END
} capture_mode_t;

typedef struct capture_state_s
{
	bot_input_t legacy_input;
	bot_input_t ea_input;
	bot_input_t end_input_before_mutation;
	bot_input_t *end_input_pointer;
	int legacy_client;
	int ea_client;
	int end_client;
	int command_clients[TEST_MAX_COMMANDS];
	char command_names[TEST_MAX_COMMANDS][TEST_MAX_COMMAND_LENGTH];
	char command_arguments[TEST_MAX_COMMANDS]
		[TEST_MAX_COMMAND_ARGUMENTS][TEST_MAX_COMMAND_LENGTH];
	size_t command_argument_count[TEST_MAX_COMMANDS];
	bool command_argument_terminated[TEST_MAX_COMMANDS];
    size_t command_count;
	int print_type;
	char print_message[TEST_MAX_COMMAND_LENGTH];
	size_t print_count;
} capture_state_t;

typedef struct ea_test_context_s
{
    bot_import_extended_t imports;
} ea_test_context_t;

static capture_mode_t g_capture_mode = CAPTURE_NONE;
static capture_state_t g_capture_state;
static botlib_import_table_t g_log_imports;

/*
=============
BotInterface_GetImportTable

Isolates the EA fixture from the interface library's optional libvar bridge.
=============
*/
const botlib_import_table_t *BotInterface_GetImportTable(void)
{
	return &g_log_imports;
}

/*
=============
capture_reset

Clears input and exact command-import capture state between assertions.
=============
*/
static void capture_reset(void)
{
    memset(&g_capture_state, 0, sizeof(g_capture_state));
	g_capture_state.legacy_client = -1;
	g_capture_state.ea_client = -1;
	g_capture_state.end_client = -1;
	g_capture_state.end_input_pointer = NULL;
    g_capture_state.command_count = 0;
    for (size_t index = 0; index < TEST_MAX_COMMANDS; ++index)
    {
		g_capture_state.command_clients[index] = -1;
    }
    g_capture_mode = CAPTURE_NONE;
}

/*
=============
test_mock_bot_input

Captures snapshot inputs or mutates the live retail end-frame record.
=============
*/
static void test_mock_bot_input(int client, bot_input_t *input)
{
    if (input == NULL)
    {
        return;
    }

    switch (g_capture_mode)
    {
        case CAPTURE_LEGACY:
            g_capture_state.legacy_input = *input;
            g_capture_state.legacy_client = client;
            break;
		case CAPTURE_EA:
			g_capture_state.ea_input = *input;
			g_capture_state.ea_client = client;
			break;
		case CAPTURE_RETAIL_END:
			g_capture_state.end_input_before_mutation = *input;
			g_capture_state.end_input_pointer = input;
			g_capture_state.end_client = client;
			input->actionflags |= ACTION_JUMP;
			input->viewangles[YAW] = 222.0f;
			input->weapon = 17;
			input->speed = 999.0f;
			input->dir[0] = 99.0f;
			break;
        case CAPTURE_NONE:
        default:
            break;
    }
}

/*
=============
test_mock_bot_client_command

Captures the retail command token and NULL-terminated argument vector.
=============
*/
static void test_mock_bot_client_command(int client, char *fmt, ...)
{
    if (fmt == NULL || g_capture_state.command_count >= TEST_MAX_COMMANDS)
    {
        return;
    }

	size_t index = g_capture_state.command_count;
	g_capture_state.command_clients[index] = client;
	strncpy(g_capture_state.command_names[index], fmt, TEST_MAX_COMMAND_LENGTH);
	g_capture_state.command_names[index][TEST_MAX_COMMAND_LENGTH - 1U] = '\0';

	va_list command_args;
	va_start(command_args, fmt);
	for (size_t argument_index = 0U;
		argument_index <= TEST_MAX_COMMAND_ARGUMENTS;
		++argument_index)
	{
		char *argument = va_arg(command_args, char *);
		if (argument == NULL)
		{
			g_capture_state.command_argument_terminated[index] = true;
			break;
		}
		if (argument_index >= TEST_MAX_COMMAND_ARGUMENTS)
		{
			continue;
		}

		strncpy(g_capture_state.command_arguments[index][argument_index],
			argument,
			TEST_MAX_COMMAND_LENGTH);
		g_capture_state.command_arguments[index][argument_index]
			[TEST_MAX_COMMAND_LENGTH - 1U] = '\0';
		g_capture_state.command_argument_count[index] += 1U;
	}
	va_end(command_args);
    g_capture_state.command_count += 1U;
}

/*
=============
test_mock_log_print

Captures exact EA diagnostics routed through the internal print shim.
=============
*/
static void test_mock_log_print(int type, const char *fmt, ...)
{
	g_capture_state.print_type = type;
	g_capture_state.print_count += 1U;

	va_list args;
	va_start(args, fmt);
	int written = vsnprintf(g_capture_state.print_message,
		sizeof(g_capture_state.print_message),
		fmt,
		args);
	va_end(args);
	if (written < 0)
	{
		g_capture_state.print_message[0] = '\0';
	}
	g_capture_state.print_message[sizeof(g_capture_state.print_message) - 1U] = '\0';
}

static int setup_ea(void **state)
{
    (void)state;

    ea_test_context_t *context = (ea_test_context_t *)calloc(1, sizeof(*context));
    assert_non_null(context);

    context->imports.BotInput = test_mock_bot_input;
    context->imports.BotClientCommand = test_mock_bot_client_command;

    Q2Bridge_SetImportTable(&context->imports);
	memset(&g_log_imports, 0, sizeof(g_log_imports));
	g_log_imports.Print = test_mock_log_print;
    capture_reset();

    int status = EA_Init(8);
    assert_int_equal(status, BLERR_NOERROR);

    *state = context;
    return 0;
}

static int teardown_ea(void **state)
{
    EA_Shutdown();
    Q2Bridge_ClearImportTable();
	memset(&g_log_imports, 0, sizeof(g_log_imports));

    ea_test_context_t *context = (ea_test_context_t *)(state != NULL ? *state : NULL);
    free(context);
    return 0;
}

static void test_ea_matches_legacy_input(void **state)
{
    (void)state;

    bot_input_t expected = {0};
    expected.thinktime = 0.05f;
    expected.dir[0] = 0.12f;
    expected.dir[1] = -0.31f;
    expected.dir[2] = 0.22f;
    expected.speed = 420.0f;
    expected.viewangles[PITCH] = -12.5f;
    expected.viewangles[YAW] = 87.3f;
    expected.viewangles[ROLL] = 4.0f;
    expected.actionflags = ACTION_ATTACK | ACTION_JUMP;

    capture_reset();
    g_capture_mode = CAPTURE_LEGACY;
    Q2_BotInput(2, &expected);
    g_capture_mode = CAPTURE_NONE;

    bot_input_t legacy = g_capture_state.legacy_input;
    int legacy_client = g_capture_state.legacy_client;
    assert_int_equal(legacy_client, 2);

    capture_reset();

    assert_int_equal(EA_ResetClient(2), BLERR_NOERROR);
    assert_int_equal(EA_SubmitInput(2, &expected), BLERR_NOERROR);
    EA_Command(2, "say %s", "hello world");

    bot_input_t actual = {0};
    assert_int_equal(EA_GetInput(2, expected.thinktime, &actual), BLERR_NOERROR);

    assert_int_equal(g_capture_state.command_count, 1);
	assert_string_equal(g_capture_state.command_names[0], "say");
	assert_int_equal(g_capture_state.command_argument_count[0], 1U);
	assert_string_equal(g_capture_state.command_arguments[0][0], "hello world");
	assert_true(g_capture_state.command_argument_terminated[0]);

    assert_int_equal(actual.actionflags, expected.actionflags);
    assert_float_equal(actual.thinktime, expected.thinktime, 0.0001f);
    assert_float_equal(actual.speed, expected.speed, 0.0001f);
    assert_memory_equal(actual.dir, expected.dir, sizeof(expected.dir));
    assert_memory_equal(actual.viewangles, expected.viewangles, sizeof(expected.viewangles));

    g_capture_mode = CAPTURE_EA;
    Q2_BotInput(2, &actual);
    g_capture_mode = CAPTURE_NONE;

    assert_int_equal(g_capture_state.ea_client, 2);
    assert_memory_equal(&legacy, &g_capture_state.ea_input, sizeof(bot_input_t));
}

/*
 * Mirrors retail vectoangles (0x10041790), which EA_LookAtPoint reaches like
 * every other caller. Both atan2 results pass through __ftol (0x1004180b,
 * 0x10041858) before being converted back, so the stored view angles are
 * always whole degrees, and the negative pitch is wrapped by +360 before it is
 * negated (0x10041867 / 0x10041876).
 */
static void compute_expected_angles(const vec3_t origin, const vec3_t target, vec3_t out)
{
    vec3_t direction;
    VectorSubtract(target, origin, direction);

    float yaw;
    float pitch;
    if (direction[0] == 0.0f && direction[1] == 0.0f)
    {
        yaw = 0.0f;
        pitch = (direction[2] > 0.0f) ? 90.0f : 270.0f;
    }
    else
    {
        yaw = (float)(int)(atan2(direction[1], direction[0]) * (180.0 / M_PI));
        if (yaw < 0.0f)
        {
            yaw += 360.0f;
        }

        float forward = sqrtf(direction[0] * direction[0] + direction[1] * direction[1]);
        pitch = (float)(int)(atan2(direction[2], forward) * (180.0 / M_PI));
        if (pitch < 0.0f)
        {
            pitch += 360.0f;
        }
    }

    out[PITCH] = -pitch;
    out[YAW] = yaw;
    out[ROLL] = 0.0f;
}

static void test_ea_view_helpers_override_input(void **state)
{
    (void)state;

    vec3_t eye = {10.0f, -5.0f, 2.5f};
    vec3_t target = {42.0f, 19.0f, 7.5f};

    bot_input_t base = {0};
    base.speed = 250.0f;
    base.dir[0] = 1.0f;

    capture_reset();
    assert_int_equal(EA_ResetClient(3), BLERR_NOERROR);
    assert_int_equal(EA_SubmitInput(3, &base), BLERR_NOERROR);

    EA_Move(3, base.dir, base.speed);
    EA_LookAtPoint(3, eye, target);
    EA_Attack(3);

    bot_input_t generated = {0};
    assert_int_equal(EA_GetInput(3, 0.1f, &generated), BLERR_NOERROR);

    vec3_t expected_angles;
    compute_expected_angles(eye, target, expected_angles);

    assert_true((generated.actionflags & ACTION_ATTACK) != 0);
    assert_float_equal(generated.speed, base.speed, 0.0001f);
    assert_float_equal(generated.dir[0], 1.0f, 0.0001f);
    assert_float_equal(generated.dir[1], 0.0f, 0.0001f);
    assert_float_equal(generated.dir[2], 0.0f, 0.0001f);
    assert_float_equal(generated.viewangles[PITCH], expected_angles[PITCH], 0.01f);
    assert_float_equal(generated.viewangles[YAW], expected_angles[YAW], 0.01f);
    assert_float_equal(generated.viewangles[ROLL], expected_angles[ROLL], 0.0001f);
}

static void test_ea_select_weapon_sets_pending(void **state)
{
    (void)state;

    bot_input_t base = {0};
    base.speed = 100.0f;

    capture_reset();
    assert_int_equal(EA_ResetClient(4), BLERR_NOERROR);
    assert_int_equal(EA_SubmitInput(4, &base), BLERR_NOERROR);

    EA_SelectWeapon(4, 7);

    bot_input_t generated = {0};
    assert_int_equal(EA_GetInput(4, 0.05f, &generated), BLERR_NOERROR);
    assert_int_equal(generated.weapon, 7);
}

/*
=============
test_ea_lifecycle_uses_tracked_storage

Pins setup and shutdown to the shared tracked botlib allocator.
=============
*/
static void test_ea_lifecycle_uses_tracked_storage(void **state)
{
	(void)state;

	assert_true(EA_IsInitialised());
	assert_true(BotMemory_TotalAllocated() > 0U);

	EA_Shutdown();
	assert_false(EA_IsInitialised());
	assert_int_equal(BotMemory_TotalAllocated(), 0U);

	EA_Shutdown();
	assert_false(EA_IsInitialised());
	assert_int_equal(EA_Init(0), BLERR_NOERROR);
	assert_true(EA_IsInitialised());
	assert_int_equal(EA_ResetClient(0), BLERR_NOERROR);
	EA_Shutdown();
	assert_int_equal(BotMemory_TotalAllocated(), 0U);

	assert_int_equal(EA_Init(8), BLERR_NOERROR);
	assert_true(EA_IsInitialised());
}

/*
=============
test_ea_command_dispatches_immediately

Pins Gladiator's immediate BotClientCommand import dispatch.
=============
*/
static void test_ea_command_dispatches_immediately(void **state)
{
	(void)state;

	capture_reset();
	EA_Command(2, "say %s", "right now");

	assert_int_equal(g_capture_state.command_count, 1U);
	assert_string_equal(g_capture_state.command_names[0], "say");
	assert_int_equal(g_capture_state.command_argument_count[0], 1U);
	assert_string_equal(g_capture_state.command_arguments[0][0], "right now");
	assert_true(g_capture_state.command_argument_terminated[0]);

	capture_reset();
	EA_Command(2, "%s", "hookoff");
	assert_int_equal(g_capture_state.command_count, 1U);
	assert_string_equal(g_capture_state.command_names[0], "hookoff");
	assert_int_equal(g_capture_state.command_argument_count[0], 0U);
	assert_true(g_capture_state.command_argument_terminated[0]);
}

/*
=============
assert_retail_command_shape

Checks one BotClientCommand call's client, command, argument, and terminator.
=============
*/
static void assert_retail_command_shape(size_t index,
	int client,
	const char *command,
	const char *argument)
{
	assert_true(index < g_capture_state.command_count);
	assert_int_equal(g_capture_state.command_clients[index], client);
	assert_string_equal(g_capture_state.command_names[index], command);
	assert_int_equal(g_capture_state.command_argument_count[index], 1U);
	assert_string_equal(g_capture_state.command_arguments[index][0], argument);
	assert_true(g_capture_state.command_argument_terminated[index]);
}

/*
=============
test_ea_specialized_commands_preserve_retail_import_shapes

Pins the seven specialized wrappers to command, argument, NULL import calls.
=============
*/
static void test_ea_specialized_commands_preserve_retail_import_shapes(void **state)
{
	(void)state;

	capture_reset();
	EA_Say(2, "hello world");
	EA_SayTeam(2, "hold position");
	EA_UseItem(2, "Rocket Launcher");
	EA_DropItem(2, "Power Shield");
	EA_UseInv(2, "Quad Damage");
	EA_DropInv(2, "Rebreather");
	EA_Gesture(2, -17);

	assert_int_equal(g_capture_state.command_count, 7U);
	assert_retail_command_shape(0U, 2, "say", "hello world");
	assert_retail_command_shape(1U, 2, "say_team", "hold position");
	assert_retail_command_shape(2U, 2, "use", "Rocket Launcher");
	assert_retail_command_shape(3U, 2, "drop", "Power Shield");
	assert_retail_command_shape(4U, 2, "invuse", "Quad Damage");
	assert_retail_command_shape(5U, 2, "invdrop", "Rebreather");
	assert_retail_command_shape(6U, 2, "wave", "-17");
}

/*
=============
test_ea_generic_command_preserves_retail_variadic_shape

Pins token forwarding, eight-argument capacity, and the ninth-argument warning.
=============
*/
static void test_ea_generic_command_preserves_retail_variadic_shape(void **state)
{
	(void)state;

	capture_reset();
	EA_Command(3, "custom", "alpha", "two words", "omega", NULL);
	assert_int_equal(g_capture_state.command_count, 1U);
	assert_int_equal(g_capture_state.command_clients[0], 3);
	assert_string_equal(g_capture_state.command_names[0], "custom");
	assert_int_equal(g_capture_state.command_argument_count[0], 3U);
	assert_string_equal(g_capture_state.command_arguments[0][0], "alpha");
	assert_string_equal(g_capture_state.command_arguments[0][1], "two words");
	assert_string_equal(g_capture_state.command_arguments[0][2], "omega");
	assert_true(g_capture_state.command_argument_terminated[0]);
	assert_int_equal(g_capture_state.print_count, 0U);

	capture_reset();
	EA_Command(3,
		"eight",
		"1", "2", "3", "4", "5", "6", "7", "8", NULL);
	assert_int_equal(g_capture_state.command_argument_count[0], 8U);
	assert_true(g_capture_state.command_argument_terminated[0]);
	assert_int_equal(g_capture_state.print_count, 0U);

	capture_reset();
	EA_Command(3,
		"nine",
		"1", "2", "3", "4", "5", "6", "7", "8", "9", NULL);
	assert_int_equal(g_capture_state.command_count, 1U);
	assert_string_equal(g_capture_state.command_names[0], "nine");
	assert_int_equal(g_capture_state.command_argument_count[0], 9U);
	assert_string_equal(g_capture_state.command_arguments[0][8], "9");
	assert_true(g_capture_state.command_argument_terminated[0]);
	assert_int_equal(g_capture_state.print_count, 1U);
	assert_int_equal(g_capture_state.print_type, PRT_ERROR);
	assert_string_equal(g_capture_state.print_message,
		"EA_Command: too many arguments");
}

/*
=============
test_ea_move_copies_direction_and_clamps_signed_speed

Pins the retail raw direction copy and symmetric 565-unit speed clamp.
=============
*/
static void test_ea_move_copies_direction_and_clamps_signed_speed(void **state)
{
	(void)state;

	vec3_t direction = {3.0f, -4.0f, 12.0f};
	assert_int_equal(EA_ResetClient(1), BLERR_NOERROR);
	EA_Move(1, direction, 900.0f);

	bot_input_t input = {0};
	assert_int_equal(EA_GetInput(1, 0.05f, &input), BLERR_NOERROR);
	assert_memory_equal(input.dir, direction, sizeof(direction));
	assert_float_equal(input.speed, 565.0f, 0.0001f);

	EA_Move(1, direction, -900.0f);
	assert_int_equal(EA_GetInput(1, 0.05f, &input), BLERR_NOERROR);
	assert_memory_equal(input.dir, direction, sizeof(direction));
	assert_float_equal(input.speed, -565.0f, 0.0001f);
}

/*
=============
test_ea_output_reset_preserves_view_and_weapon

Pins retail frame reset fields while retaining persistent view state.
=============
*/
static void test_ea_output_reset_preserves_view_and_weapon(void **state)
{
	(void)state;

	vec3_t direction = {0.5f, 0.25f, -0.75f};
	vec3_t viewangles = {-11.0f, 127.0f, 3.0f};
	vec3_t zero = {0.0f, 0.0f, 0.0f};
	assert_int_equal(EA_ResetClient(5), BLERR_NOERROR);
	EA_Move(5, direction, 300.0f);
	EA_View(5, viewangles);
	EA_SelectWeapon(5, 9);
	EA_Attack(5);

	bot_input_t first = {0};
	assert_int_equal(EA_GetInput(5, 0.1f, &first), BLERR_NOERROR);
	assert_memory_equal(first.dir, direction, sizeof(direction));
	assert_memory_equal(first.viewangles, viewangles, sizeof(viewangles));
	assert_float_equal(first.speed, 300.0f, 0.0001f);
	assert_int_equal(first.actionflags, ACTION_ATTACK);
	assert_int_equal(first.weapon, 9);

	bot_input_t second = {0};
	assert_int_equal(EA_GetInput(5, 0.2f, &second), BLERR_NOERROR);
	assert_float_equal(second.thinktime, 0.2f, 0.0001f);
	assert_memory_equal(second.viewangles, viewangles, sizeof(viewangles));
	assert_memory_equal(second.dir, zero, sizeof(zero));
	assert_float_equal(second.speed, 0.0f, 0.0001f);
	assert_int_equal(second.actionflags, 0);
	assert_int_equal(second.weapon, 9);
}

/*
=============
test_ea_end_regular_dispatches_live_record_before_reset

Pins retail import ordering, post-callback jump sampling, and reset timing.
=============
*/
static void test_ea_end_regular_dispatches_live_record_before_reset(void **state)
{
	(void)state;

	bot_input_t pending = {0};
	pending.dir[0] = 1.0f;
	pending.dir[1] = 2.0f;
	pending.dir[2] = 3.0f;
	pending.speed = 321.0f;
	pending.viewangles[PITCH] = 10.0f;
	pending.viewangles[YAW] = 20.0f;
	pending.viewangles[ROLL] = 30.0f;
	pending.actionflags = ACTION_ATTACK | ACTION_MOVELEFT;
	pending.weapon = 7;

	assert_int_equal(EA_ResetClient(4), BLERR_NOERROR);
	assert_int_equal(EA_SubmitInput(4, &pending), BLERR_NOERROR);
	capture_reset();
	g_capture_mode = CAPTURE_RETAIL_END;
	assert_int_equal(EA_EndRegular(4, 0.25f), BLERR_NOERROR);
	g_capture_mode = CAPTURE_NONE;

	assert_int_equal(g_capture_state.end_client, 4);
	assert_non_null(g_capture_state.end_input_pointer);
	assert_float_equal(g_capture_state.end_input_before_mutation.thinktime,
		0.25f, 0.0001f);
	assert_memory_equal(g_capture_state.end_input_before_mutation.dir,
		pending.dir, sizeof(pending.dir));
	assert_float_equal(g_capture_state.end_input_before_mutation.speed,
		321.0f, 0.0001f);
	assert_memory_equal(g_capture_state.end_input_before_mutation.viewangles,
		pending.viewangles, sizeof(pending.viewangles));
	assert_int_equal(g_capture_state.end_input_before_mutation.actionflags,
		ACTION_ATTACK);
	assert_int_equal(g_capture_state.end_input_before_mutation.weapon, 7);

	bot_input_t *reset_input = g_capture_state.end_input_pointer;
	vec3_t zero = {0.0f, 0.0f, 0.0f};
	assert_float_equal(reset_input->thinktime, 0.0f, 0.0001f);
	assert_memory_equal(reset_input->dir, zero, sizeof(zero));
	assert_float_equal(reset_input->speed, 0.0f, 0.0001f);
	assert_int_equal(reset_input->actionflags, ACTION_MOVELEFT);
	assert_float_equal(reset_input->viewangles[PITCH], 10.0f, 0.0001f);
	assert_float_equal(reset_input->viewangles[YAW], 222.0f, 0.0001f);
	assert_float_equal(reset_input->viewangles[ROLL], 30.0f, 0.0001f);
	assert_int_equal(reset_input->weapon, 17);

	EA_Jump(4);
	bot_input_t next = {0};
	assert_int_equal(EA_GetInput(4, 0.5f, &next), BLERR_NOERROR);
	assert_float_equal(next.thinktime, 0.5f, 0.0001f);
	assert_memory_equal(next.dir, zero, sizeof(zero));
	assert_float_equal(next.speed, 0.0f, 0.0001f);
	assert_int_equal(next.actionflags, 0);
	assert_float_equal(next.viewangles[YAW], 222.0f, 0.0001f);
	assert_int_equal(next.weapon, 17);
}

/*
=============
test_ea_jump_uses_retail_last_frame_latch

Pins immediate jump suppression on the frame following a submitted jump.
=============
*/
static void test_ea_jump_uses_retail_last_frame_latch(void **state)
{
	(void)state;

	assert_int_equal(EA_ResetClient(6), BLERR_NOERROR);
	bot_input_t input = {0};

	EA_Jump(6);
	assert_int_equal(EA_GetInput(6, 0.1f, &input), BLERR_NOERROR);
	assert_true((input.actionflags & ACTION_JUMP) != 0);

	EA_Jump(6);
	assert_int_equal(EA_GetInput(6, 0.1f, &input), BLERR_NOERROR);
	assert_true((input.actionflags & ACTION_JUMP) == 0);

	EA_Jump(6);
	assert_int_equal(EA_GetInput(6, 0.1f, &input), BLERR_NOERROR);
	assert_true((input.actionflags & ACTION_JUMP) != 0);
}

/*
=============
test_ea_reset_input_preserves_retail_persistent_fields

Pins the explicit reset contract independently of frame finalisation.
=============
*/
static void test_ea_reset_input_preserves_retail_persistent_fields(void **state)
{
	(void)state;

	vec3_t direction = {1.0f, 2.0f, 3.0f};
	vec3_t viewangles = {4.0f, 5.0f, 6.0f};
	vec3_t zero = {0.0f, 0.0f, 0.0f};
	assert_int_equal(EA_ResetClient(7), BLERR_NOERROR);
	EA_Move(7, direction, 400.0f);
	EA_View(7, viewangles);
	EA_SelectWeapon(7, 11);
	EA_Attack(7);
	EA_ResetInput(7);

	bot_input_t input = {0};
	assert_int_equal(EA_GetInput(7, 0.25f, &input), BLERR_NOERROR);
	assert_memory_equal(input.dir, zero, sizeof(zero));
	assert_memory_equal(input.viewangles, viewangles, sizeof(viewangles));
	assert_float_equal(input.speed, 0.0f, 0.0001f);
	assert_int_equal(input.actionflags, 0);
	assert_int_equal(input.weapon, 11);
}

/*
=============
test_ea_action_exports_match_retail_bits

Pins every simple Gladiator elementary-action export to its retail bit.
=============
*/
static void test_ea_action_exports_match_retail_bits(void **state)
{
	(void)state;

	assert_int_equal(EA_ResetClient(0), BLERR_NOERROR);
	EA_Attack(0);
	EA_Use(0);
	EA_Respawn(0);
	EA_Crouch(0);
	EA_MoveDown(0);
	EA_MoveUp(0);
	EA_MoveForward(0);
	EA_MoveBack(0);
	EA_MoveRight(0);
	EA_DelayedJump(0);

	bot_input_t input = {0};
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	/* 1|2|4|16|8|32|64|256|512 - move-right and delayed-jump are byte-1 bits. */
	assert_int_equal(input.actionflags, 895);
	assert_int_equal(input.actionflags,
		ACTION_ATTACK | ACTION_USE | ACTION_RESPAWN | ACTION_CROUCH |
		ACTION_MOVEUP | ACTION_MOVEFORWARD | ACTION_MOVEBACK |
		ACTION_MOVERIGHT | ACTION_DELAYEDJUMP);
}

/*
=============
test_ea_left_action_matches_retail_latch_alias

Pins Gladiator's observable 0x80 move-left versus jump-latch alias quirk.
=============
*/
static void test_ea_left_action_matches_retail_latch_alias(void **state)
{
	(void)state;

	assert_int_equal(EA_ResetClient(0), BLERR_NOERROR);
	EA_MoveLeft(0);

	bot_input_t input = {0};
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_int_equal(input.actionflags & ACTION_MOVELEFT, 0);
}

/*
=============
test_ea_right_and_delayed_jump_match_retail_bit_aliases

Pins the byte-1 0x100/0x200 bits written by the right-move and delayed-jump
elementary-action writers.
=============
*/
static void test_ea_right_and_delayed_jump_match_retail_bit_aliases(void **state)
{
	(void)state;

	assert_int_equal(EA_ResetClient(0), BLERR_NOERROR);
	EA_MoveRight(0);

	bot_input_t input = {0};
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	/* sub_100374f0: `flags:1.b |= 1` at 0x10037503 == bit 8. */
	assert_int_equal(input.actionflags, 0x100);

	assert_int_equal(EA_ResetClient(0), BLERR_NOERROR);
	EA_DelayedJump(0);
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	/* sub_10037390: `flags:1.b |= 2` at 0x100373ae == bit 9. */
	assert_int_equal(input.actionflags, 0x200);

	/* A preceding immediate jump latches bit 0x80 and suppresses this alias. */
	assert_int_equal(EA_ResetClient(0), BLERR_NOERROR);
	EA_Jump(0);
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_int_equal(input.actionflags, 0x08);
	EA_DelayedJump(0);
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_int_equal(input.actionflags, 0);
}

/*
=============
test_ea_right_and_delayed_jump_do_not_touch_attack_or_use

Proves the byte-1 writers leave the byte-0 attack/use bits alone.
=============
*/
static void test_ea_right_and_delayed_jump_do_not_touch_attack_or_use(void **state)
{
	(void)state;

	bot_input_t input = {0};

	/* EA_MoveRight must not press fire (retail bit 8, not bit 0). */
	assert_int_equal(EA_ResetClient(0), BLERR_NOERROR);
	EA_MoveRight(0);
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_int_equal(input.actionflags & ACTION_ATTACK, 0);
	assert_int_equal(input.actionflags & ACTION_MOVERIGHT, ACTION_MOVERIGHT);

	/* EA_DelayedJump must not press +use (retail bit 9, not bit 1). */
	assert_int_equal(EA_ResetClient(0), BLERR_NOERROR);
	EA_DelayedJump(0);
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_int_equal(input.actionflags & ACTION_USE, 0);
	assert_int_equal(input.actionflags & ACTION_DELAYEDJUMP, ACTION_DELAYEDJUMP);

	/*
	 * The suppressed path clears bit 9 only, so a +use issued in the same
	 * frame survives - retail does `flags:1.b &= 0xfd` at 0x100373a7.
	 */
	assert_int_equal(EA_ResetClient(0), BLERR_NOERROR);
	EA_Jump(0);
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	EA_Use(0);
	EA_DelayedJump(0);
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_int_equal(input.actionflags, ACTION_USE);
}

/*
=============
test_bridge_config_framereachability_default_matches_retail

Pins the retail "20" default string registered by sub_10018920 at 0x10018957.
=============
*/
static void test_bridge_config_framereachability_default_matches_retail(void **state)
{
	(void)state;

	assert_true(BridgeConfig_Init());

	libvar_t *framereachability = Bridge_FrameReachability();
	assert_non_null(framereachability);
	assert_non_null(framereachability->string);
	/* data_1005bdf8 == "20"; the <= 0 clamp to 15.0f must not fire by default. */
	assert_string_equal(framereachability->string, "20");
	assert_float_equal(framereachability->value, 20.0f, 0.0001f);
	assert_float_equal(LibVarValue("framereachability", "0"), 20.0f, 0.0001f);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_ea_matches_legacy_input, setup_ea, teardown_ea),
        cmocka_unit_test_setup_teardown(test_ea_view_helpers_override_input, setup_ea, teardown_ea),
        cmocka_unit_test_setup_teardown(test_ea_select_weapon_sets_pending, setup_ea, teardown_ea),
		cmocka_unit_test_setup_teardown(test_ea_lifecycle_uses_tracked_storage, setup_ea, teardown_ea),
		cmocka_unit_test_setup_teardown(test_ea_command_dispatches_immediately, setup_ea, teardown_ea),
		cmocka_unit_test_setup_teardown(test_ea_specialized_commands_preserve_retail_import_shapes, setup_ea, teardown_ea),
		cmocka_unit_test_setup_teardown(test_ea_generic_command_preserves_retail_variadic_shape, setup_ea, teardown_ea),
		cmocka_unit_test_setup_teardown(test_ea_move_copies_direction_and_clamps_signed_speed, setup_ea, teardown_ea),
		cmocka_unit_test_setup_teardown(test_ea_output_reset_preserves_view_and_weapon, setup_ea, teardown_ea),
		cmocka_unit_test_setup_teardown(test_ea_end_regular_dispatches_live_record_before_reset, setup_ea, teardown_ea),
		cmocka_unit_test_setup_teardown(test_ea_jump_uses_retail_last_frame_latch, setup_ea, teardown_ea),
		cmocka_unit_test_setup_teardown(test_ea_reset_input_preserves_retail_persistent_fields, setup_ea, teardown_ea),
		cmocka_unit_test_setup_teardown(test_ea_action_exports_match_retail_bits, setup_ea, teardown_ea),
		cmocka_unit_test_setup_teardown(test_ea_left_action_matches_retail_latch_alias, setup_ea, teardown_ea),
		cmocka_unit_test_setup_teardown(test_ea_right_and_delayed_jump_match_retail_bit_aliases, setup_ea, teardown_ea),
		cmocka_unit_test_setup_teardown(test_ea_right_and_delayed_jump_do_not_touch_attack_or_use, setup_ea, teardown_ea),
		cmocka_unit_test_setup_teardown(test_bridge_config_framereachability_default_matches_retail, setup_ea, teardown_ea),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
