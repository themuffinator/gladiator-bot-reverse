#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "botlib/ai/ai_dm.h"
#include "botlib/ai/move/bot_move.h"
#include "botlib/ea/ea_local.h"
#include "botlib/common/l_libvar.h"
#include "botlib/ai/goal_move_orchestrator.h"
#include "q2bridge/botlib.h"

#include "shared/q_shared.h"

typedef struct ai_character_profile_s ai_character_profile_t;
typedef struct bot_chatstate_s bot_chatstate_t;
typedef struct ai_goal_state_s ai_goal_state_t;
typedef struct ai_move_state_s ai_move_state_t;
typedef struct ai_weapon_weights_s ai_weapon_weights_t;
typedef struct bot_weight_config_s bot_weight_config_t;

typedef struct bot_client_state_s
{
    int client_number;
    bool active;
    bot_settings_t settings;
    bot_clientsettings_t client_settings;
    int character_handle;
    ai_character_profile_t *character;
    bot_weight_config_t *item_weights;
    ai_weapon_weights_t *weapon_weights;
    bot_chatstate_t *chat_state;
    ai_goal_state_t *goal_state;
    ai_move_state_t *move_state;
    int move_handle;
    ai_dm_state_t *dm_state;
    int weapon_state;
    int current_weapon;
    int goal_handle;
    bot_goal_t goal_snapshot[2];
    int goal_snapshot_count;
    bot_updateclient_t last_client_update;
    bool client_update_valid;
    float last_update_time;
    bot_moveresult_t last_move_result;
    bool has_move_result;
    float goal_avoid_duration;
    int active_goal_number;
} bot_client_state_t;

#define DM_ASSERT(expr)                                                                                 \
    do                                                                                                  \
    {                                                                                                   \
        if (!(expr))                                                                                     \
        {                                                                                               \
            fprintf(stderr, "[dm_test] Assertion failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);    \
            exit(EXIT_FAILURE);                                                                          \
        }                                                                                               \
    } while (0)

#define DM_ASSERT_FLOAT_CLOSE(actual, expected, tolerance)                                                   \
    do                                                                                                       \
    {                                                                                                        \
        float dm_diff = fabsf((float)(actual) - (float)(expected));                                          \
        if (dm_diff > (float)(tolerance))                                                                    \
        {                                                                                                    \
            fprintf(stderr,                                                                                  \
                    "[dm_test] Float assertion failed: %s ≈ %s (diff=%f tol=%f) (%s:%d)\n",             \
                    #actual,                                                                                \
                    #expected,                                                                              \
                    dm_diff,                                                                                \
                    (float)(tolerance),                                                                     \
                    __FILE__,                                                                               \
                    __LINE__);                                                                              \
            exit(EXIT_FAILURE);                                                                             \
        }                                                                                                   \
    } while (0)

typedef struct dm_test_capture_s {
    bool pending;
    int client;
    bot_input_t command;
} dm_test_capture_t;

typedef struct dm_ea_state_s {
    bool initialised;
    bot_input_t base;
    vec3_t move_dir;
    float move_speed;
    bool has_move;
    vec3_t view_origin;
    vec3_t view_target;
    bool has_view;
    int actionflags;
    int weapon;
} dm_ea_state_t;

#define DM_MAX_TEST_CLIENTS 8

static dm_test_capture_t g_dm_capture;
static dm_ea_state_t g_dm_ea_state[DM_MAX_TEST_CLIENTS];

static dm_ea_state_t *dm_ea_get_state(int client)
{
    if (client < 0 || client >= DM_MAX_TEST_CLIENTS)
    {
        return NULL;
    }

    return &g_dm_ea_state[client];
}

static void dm_ea_reset_state(dm_ea_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    memset(state, 0, sizeof(*state));
}

static void dm_compute_viewangles(const vec3_t eye, const vec3_t target, vec3_t out)
{
    vec3_t direction;
    VectorSubtract(target, eye, direction);

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

int EA_ResetClient(int client)
{
    dm_ea_state_t *state = dm_ea_get_state(client);
    if (state == NULL)
    {
        return BLERR_INVALIDCLIENTNUMBER;
    }

    dm_ea_reset_state(state);
    state->initialised = true;
    return BLERR_NOERROR;
}

int EA_SubmitInput(int client, const bot_input_t *input)
{
    dm_ea_state_t *state = dm_ea_get_state(client);
    if (state == NULL || input == NULL)
    {
        return BLERR_INVALIDCLIENTNUMBER;
    }

    state->initialised = true;
    state->base = *input;
    state->weapon = input->weapon;
    return BLERR_NOERROR;
}

void EA_Move(int client, const vec3_t direction, float speed)
{
    dm_ea_state_t *state = dm_ea_get_state(client);
    if (state == NULL || !state->initialised)
    {
        return;
    }

    VectorCopy(direction, state->move_dir);
    state->move_speed = speed;
    state->has_move = true;
}

void EA_LookAtPoint(int client, const vec3_t eye_position, const vec3_t target_position)
{
    dm_ea_state_t *state = dm_ea_get_state(client);
    if (state == NULL || !state->initialised)
    {
        return;
    }

    VectorCopy(eye_position, state->view_origin);
    VectorCopy(target_position, state->view_target);
    state->has_view = true;
}

void EA_SelectWeapon(int client, int weapon)
{
    dm_ea_state_t *state = dm_ea_get_state(client);
    if (state == NULL || !state->initialised)
    {
        return;
    }

    state->weapon = weapon;
}

void EA_Attack(int client)
{
    dm_ea_state_t *state = dm_ea_get_state(client);
    if (state == NULL || !state->initialised)
    {
        return;
    }

    state->actionflags |= ACTION_ATTACK;
}

void EA_Jump(int client)
{
    dm_ea_state_t *state = dm_ea_get_state(client);
    if (state == NULL || !state->initialised)
    {
        return;
    }

    state->actionflags |= ACTION_JUMP;
}

int EA_GetInput(int client, float thinktime, bot_input_t *out_input)
{
    dm_ea_state_t *state = dm_ea_get_state(client);
    if (state == NULL || !state->initialised || out_input == NULL)
    {
        return BLERR_INVALIDCLIENTNUMBER;
    }

    bot_input_t command = state->base;
    command.thinktime = thinktime;

    if (state->has_move)
    {
        VectorCopy(state->move_dir, command.dir);
        command.speed = state->move_speed;
    }

    if (state->has_view)
    {
        dm_compute_viewangles(state->view_origin, state->view_target, command.viewangles);
    }

    command.weapon = state->weapon;
    command.actionflags |= state->actionflags;

    *out_input = command;

    g_dm_capture.pending = true;
    g_dm_capture.client = client;
    g_dm_capture.command = command;

    state->has_move = false;
    state->has_view = false;
    state->actionflags = 0;
    VectorClear(state->move_dir);
    state->move_speed = 0.0f;

    return BLERR_NOERROR;
}

libvar_t *Bridge_DMFlags(void)
{
    return NULL;
}

libvar_t *Bridge_RocketJump(void)
{
    return NULL;
}

libvar_t *Bridge_UseHook(void)
{
    return NULL;
}

void LibVarSetNotModified(const char *var_name)
{
    (void)var_name;
}

ai_avoid_list_t *AI_GoalState_GetAvoidList(ai_goal_state_t *state)
{
    (void)state;
    return NULL;
}

bool AI_AvoidList_Add(ai_avoid_list_t *list, int id, float expiry)
{
    (void)list;
    (void)id;
    (void)expiry;
    return false;
}

static void dm_capture_reset(void)
{
    g_dm_capture.pending = false;
    g_dm_capture.client = -1;
    memset(&g_dm_capture.command, 0, sizeof(g_dm_capture.command));
}

static void dm_mock_bot_input(int client, bot_input_t *input)
{
    (void)client;
    (void)input;
}

static void dm_mock_bot_client_command(int client, char *fmt, ...)
{
    (void)client;
    (void)fmt;
}

static int dm_test_setup(void)
{
    dm_capture_reset();
    for (int i = 0; i < DM_MAX_TEST_CLIENTS; ++i)
    {
        memset(&g_dm_ea_state[i], 0, sizeof(g_dm_ea_state[i]));
    }
    return 0;
}

static void dm_test_teardown(void)
{
    dm_capture_reset();
}

static void dm_prepare_client_state(bot_client_state_t *client_state, int client)
{
    memset(client_state, 0, sizeof(*client_state));
    client_state->client_number = client;
    client_state->current_weapon = 3;
    client_state->last_client_update.pm_type = PM_NORMAL;
    VectorClear(client_state->last_client_update.origin);
}

static void dm_prepare_enemy(ai_dm_enemy_info_t *enemy,
                             int entity,
                             const vec3_t origin,
                             float distance,
                             float last_seen)
{
    memset(enemy, 0, sizeof(*enemy));
    enemy->valid = true;
    enemy->visible = true;
    enemy->entity = entity;
    VectorCopy(origin, enemy->origin);
    enemy->distance = distance;
    enemy->last_seen_time = last_seen;
}

static bot_input_t dm_consume_input(int client, float thinktime)
{
    bot_input_t command;
    memset(&command, 0, sizeof(command));

    dm_capture_reset();
    DM_ASSERT(EA_GetInput(client, thinktime, &command) == BLERR_NOERROR);
    DM_ASSERT(g_dm_capture.pending);
    DM_ASSERT(g_dm_capture.client == client);
    return g_dm_capture.command;
}

static void test_dm_reaction_delay_gates_attack(void)
{
    const int client = 0;
    ai_dm_state_t *dm_state = AI_DMState_Create(client);
    DM_ASSERT(dm_state != NULL);

    bot_client_state_t client_state;
    dm_prepare_client_state(&client_state, client);

    ai_dm_enemy_info_t enemy;
    vec3_t enemy_origin = {128.0f, 0.0f, 0.0f};
    dm_prepare_enemy(&enemy, 2, enemy_origin, 128.0f, 1.0f);

    DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
    bot_input_t base = {0};
    DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

    AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 1.0f);
    bot_input_t command = dm_consume_input(client, 0.1f);
    DM_ASSERT((command.actionflags & ACTION_ATTACK) == 0);

    ai_dm_metrics_t metrics;
    AI_DMState_GetMetrics(dm_state, &metrics);
    DM_ASSERT(metrics.enemy_entity == 2);
    DM_ASSERT(metrics.enemy_visible);
    DM_ASSERT_FLOAT_CLOSE(metrics.enemysight_time, 1.0f, 0.0001f);

    enemy.last_seen_time = 1.2f;
    AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 1.2f);
    command = dm_consume_input(client, 0.1f);
    DM_ASSERT((command.actionflags & ACTION_ATTACK) == 0);

    enemy.last_seen_time = 1.4f;
    AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 1.4f);
    command = dm_consume_input(client, 0.1f);
    DM_ASSERT((command.actionflags & ACTION_ATTACK) != 0);

    AI_DMState_GetMetrics(dm_state, &metrics);
    DM_ASSERT_FLOAT_CLOSE(metrics.last_attack_time, 1.4f, 0.0001f);
    DM_ASSERT_FLOAT_CLOSE(metrics.enemyvisible_time, 1.4f, 0.0001f);

    AI_DMState_Destroy(dm_state);
}

static void test_dm_chase_timer_decays(void)
{
    const int client = 1;
    ai_dm_state_t *dm_state = AI_DMState_Create(client);
    DM_ASSERT(dm_state != NULL);

    bot_client_state_t client_state;
    dm_prepare_client_state(&client_state, client);

    ai_dm_enemy_info_t enemy;
    vec3_t origin = {64.0f, 64.0f, 0.0f};
    dm_prepare_enemy(&enemy, 3, origin, 90.5f, 2.0f);

    DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
    bot_input_t base = {0};
    DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

    AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 2.0f);
    dm_consume_input(client, 0.05f);

    ai_dm_metrics_t metrics;
    AI_DMState_GetMetrics(dm_state, &metrics);
    DM_ASSERT(metrics.enemy_visible);
    DM_ASSERT(metrics.chase_time > 2.0f);

    AI_DMState_Update(dm_state, &client_state, NULL, NULL, NULL, 2.1f);
    dm_consume_input(client, 0.05f);
    AI_DMState_GetMetrics(dm_state, &metrics);
    DM_ASSERT(!metrics.enemy_visible);
    DM_ASSERT(metrics.enemy_entity == 3);

    float expiry_time = metrics.chase_time + 0.2f;
    AI_DMState_Update(dm_state, &client_state, NULL, NULL, NULL, expiry_time);
    dm_consume_input(client, 0.05f);
    AI_DMState_GetMetrics(dm_state, &metrics);
    DM_ASSERT(metrics.enemy_entity == -1);

    float reacquire_time = expiry_time + 0.3f;
    dm_prepare_enemy(&enemy, 3, origin, 90.5f, reacquire_time);
    AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, reacquire_time);
    dm_consume_input(client, 0.05f);
    AI_DMState_GetMetrics(dm_state, &metrics);
    DM_ASSERT(metrics.enemy_visible);
    DM_ASSERT_FLOAT_CLOSE(metrics.enemysight_time, reacquire_time, 0.0001f);

    AI_DMState_Destroy(dm_state);
}

static void test_dm_revenge_counters_update_on_death(void)
{
    const int client = 2;
    ai_dm_state_t *dm_state = AI_DMState_Create(client);
    DM_ASSERT(dm_state != NULL);

    bot_client_state_t client_state;
    dm_prepare_client_state(&client_state, client);

    ai_dm_enemy_info_t enemy;
    vec3_t origin = {32.0f, -32.0f, 0.0f};
    dm_prepare_enemy(&enemy, 4, origin, 45.3f, 5.0f);

    DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
    bot_input_t base = {0};
    DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

    AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 5.0f);
    dm_consume_input(client, 0.05f);

    client_state.last_client_update.pm_type = PM_DEAD;
    AI_DMState_Update(dm_state, &client_state, NULL, NULL, NULL, 5.5f);
    dm_consume_input(client, 0.05f);

    ai_dm_metrics_t metrics;
    AI_DMState_GetMetrics(dm_state, &metrics);
    DM_ASSERT(metrics.revenge_enemy == 4);
    DM_ASSERT(metrics.revenge_kills == 1);
    DM_ASSERT(metrics.enemy_entity == -1);
    DM_ASSERT(!metrics.enemy_visible);

    AI_DMState_Destroy(dm_state);
}

static void test_dm_velocity_tracks_enemy_motion(void)
{
    const int client = 3;
    ai_dm_state_t *dm_state = AI_DMState_Create(client);
    DM_ASSERT(dm_state != NULL);

    bot_client_state_t client_state;
    dm_prepare_client_state(&client_state, client);

    ai_dm_enemy_info_t enemy;
    vec3_t origin = {0.0f, 0.0f, 0.0f};
    dm_prepare_enemy(&enemy, 5, origin, 0.0f, 10.0f);

    DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
    bot_input_t base = {0};
    DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

    AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 10.0f);
    dm_consume_input(client, 0.05f);

    vec3_t moved_origin = {5.0f, 0.0f, 0.0f};
    dm_prepare_enemy(&enemy, 5, moved_origin, 5.0f, 10.5f);
    AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 10.5f);
    dm_consume_input(client, 0.05f);

    ai_dm_metrics_t metrics;
    AI_DMState_GetMetrics(dm_state, &metrics);
    DM_ASSERT_FLOAT_CLOSE(metrics.last_enemy_origin[0], 5.0f, 0.0001f);
    DM_ASSERT_FLOAT_CLOSE(metrics.last_enemy_velocity[0], 10.0f, 0.0001f);
    DM_ASSERT_FLOAT_CLOSE(metrics.enemyposition_time, 10.5f, 0.0001f);

    AI_DMState_Destroy(dm_state);
}

int main(void)
{
    struct dm_test_case
    {
        const char *name;
        int (*setup)(void);
        void (*test)(void);
        void (*teardown)(void);
    } tests[] = {
        {"reaction_delay", dm_test_setup, test_dm_reaction_delay_gates_attack, dm_test_teardown},
        {"chase_timer", dm_test_setup, test_dm_chase_timer_decays, dm_test_teardown},
        {"revenge_counters", dm_test_setup, test_dm_revenge_counters_update_on_death, dm_test_teardown},
        {"velocity_tracking", dm_test_setup, test_dm_velocity_tracks_enemy_motion, dm_test_teardown},
    };

    size_t count = sizeof(tests) / sizeof(tests[0]);
    for (size_t index = 0; index < count; ++index)
    {
        if (tests[index].setup != NULL)
        {
            DM_ASSERT(tests[index].setup() == 0);
        }

        if (tests[index].test != NULL)
        {
            tests[index].test();
        }

        if (tests[index].teardown != NULL)
        {
            tests[index].teardown();
        }
    }

    return EXIT_SUCCESS;
}
