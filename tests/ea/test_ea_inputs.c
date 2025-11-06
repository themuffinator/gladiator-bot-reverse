#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <cmocka.h>

#include "botlib_ea/ea_local.h"
#include "botlib_common/l_log.h"
#include "q2bridge/bridge.h"

#define TEST_MAX_COMMANDS 8
#define TEST_MAX_COMMAND_LENGTH 256

typedef enum capture_mode_e
{
    CAPTURE_NONE = 0,
    CAPTURE_LEGACY,
    CAPTURE_EA
} capture_mode_t;

typedef struct capture_state_s
{
    bot_input_t legacy_input;
    bot_input_t ea_input;
    int legacy_client;
    int ea_client;
    char commands[TEST_MAX_COMMANDS][TEST_MAX_COMMAND_LENGTH];
    size_t command_count;
} capture_state_t;

typedef struct ea_test_context_s
{
    bot_import_t imports;
} ea_test_context_t;

static capture_mode_t g_capture_mode = CAPTURE_NONE;
static capture_state_t g_capture_state;

static void capture_reset(void)
{
    memset(&g_capture_state, 0, sizeof(g_capture_state));
    g_capture_state.legacy_client = -1;
    g_capture_state.ea_client = -1;
    g_capture_state.command_count = 0;
    for (size_t index = 0; index < TEST_MAX_COMMANDS; ++index)
    {
        g_capture_state.commands[index][0] = '\0';
    }
    g_capture_mode = CAPTURE_NONE;
}

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
        case CAPTURE_NONE:
        default:
            break;
    }
}

static void test_mock_bot_client_command(int client, char *fmt, ...)
{
    (void)client;

    if (fmt == NULL || g_capture_state.command_count >= TEST_MAX_COMMANDS)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char buffer[TEST_MAX_COMMAND_LENGTH];
    int written = vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    if (written < 0)
    {
        return;
    }

    buffer[TEST_MAX_COMMAND_LENGTH - 1] = '\0';
    strncpy(g_capture_state.commands[g_capture_state.command_count],
            buffer,
            TEST_MAX_COMMAND_LENGTH);
    g_capture_state.commands[g_capture_state.command_count][TEST_MAX_COMMAND_LENGTH - 1] = '\0';
    g_capture_state.command_count += 1U;
}

static int setup_ea(void **state)
{
    (void)state;

    ea_test_context_t *context = (ea_test_context_t *)calloc(1, sizeof(*context));
    assert_non_null(context);

    context->imports.BotInput = test_mock_bot_input;
    context->imports.BotClientCommand = test_mock_bot_client_command;

    Q2Bridge_SetImportTable(&context->imports);
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
    assert_string_equal(g_capture_state.commands[0], "say hello world");

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

static void compute_expected_angles(const vec3_t origin, const vec3_t target, vec3_t out)
{
    vec3_t direction;
    VectorSubtract(target, origin, direction);

    float yaw = atan2f(direction[1], direction[0]) * (180.0f / (float)M_PI);
    if (yaw < 0.0f)
    {
        yaw += 360.0f;
    }

    float forward = sqrtf(direction[0] * direction[0] + direction[1] * direction[1]);
    float pitch = 0.0f;
    if (forward > 0.0001f || fabsf(direction[2]) > 0.0001f)
    {
        pitch = atan2f(-direction[2], forward) * (180.0f / (float)M_PI);
    }

    out[PITCH] = pitch;
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

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_ea_matches_legacy_input, setup_ea, teardown_ea),
        cmocka_unit_test_setup_teardown(test_ea_view_helpers_override_input, setup_ea, teardown_ea),
        cmocka_unit_test_setup_teardown(test_ea_select_weapon_sets_pending, setup_ea, teardown_ea),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
