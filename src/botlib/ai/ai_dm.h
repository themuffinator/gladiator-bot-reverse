#pragma once

#include <stdbool.h>

#include "shared/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ai_dm_state_s ai_dm_state_t;

typedef struct ai_dm_enemy_info_s {
    bool valid;
    bool visible;
    int entity;
    vec3_t origin;
    vec3_t velocity;
    /*
     * Retail BotAimAtEnemy leads the shot from the AAS entity record rather
     * than from a per-frame displacement: it normalises `origin -
     * lastvisorigin`, divides the recovered length by `update_time`, and
     * multiplies the result by the projectile time of flight.  Both inputs
     * travel with the enemy hand-off so the aim helper can apply that formula
     * without reaching back into AAS.
     */
    vec3_t lastvisorigin;
    float update_time;
    float distance;
    float last_seen_time;
    float field_of_view;
    bool is_invisible;
    bool is_chatting;
    bool is_shooting;
    bool triggered_by_damage;
    bool in_field_of_view;
    bool has_line_of_sight;
} ai_dm_enemy_info_t;

struct bot_client_state_s;
typedef struct bot_client_state_s bot_client_state_t;

typedef struct ai_goal_selection_s ai_goal_selection_t;

typedef struct bot_input_s bot_input_t;

struct bot_moveresult_s;
typedef void (*ai_dm_move_result_handler_t)(void *context,
	const struct bot_moveresult_s *result,
	bool attack_chase_active);

typedef struct ai_dm_metrics_s {
    int client_number;
    int enemy_entity;
    int revenge_enemy;
    int revenge_kills;
    bool enemy_visible;
    float last_attack_time;
    float last_jump_time;
    float enemyvisible_time;
    float enemysight_time;
    float enemydeath_time;
    float enemyposition_time;
    float chase_time;
    float attackstrafe_timer;
    float attackcrouch_time;
    float attackchase_time;
    float attackjump_time;
    float firethrottlewait_time;
    float firethrottleshoot_time;
    float reaction_delay;
    float chase_duration;
    float attack_strafe_interval;
    vec3_t last_enemy_origin;
    vec3_t last_enemy_velocity;
	vec3_t viewangles;
	vec3_t ideal_viewangles;
	vec3_t viewanglespeed;
	vec3_t aim_target;
	bool attack_latched;
} ai_dm_metrics_t;

ai_dm_state_t *AI_DMState_Create(int client_number);
ai_dm_state_t *AI_DMState_AcquireRetail(int client_number);
void AI_DMState_Destroy(ai_dm_state_t *state);
void AI_DMState_ForgetRetail(void);
void AI_DMState_Reset(ai_dm_state_t *state);
void AI_DMState_SetClient(ai_dm_state_t *state, int client_number);
void AI_DMState_SetEnemyContext(ai_dm_state_t *state,
	int enemy_entity,
	float enemy_sight_time,
	int last_enemy_area,
	const vec3_t last_enemy_origin);
void AI_DMState_SetChaseDeadline(ai_dm_state_t *state, float chase_time);
void AI_DMState_ApplyDeltaAngles(ai_dm_state_t *state,
	const vec3_t delta_angles);
int AI_DMState_GetViewAngles(const ai_dm_state_t *state,
	vec3_t viewangles);
void AI_DMState_SetIdealViewAngles(ai_dm_state_t *state,
	const vec3_t ideal_viewangles);
float AI_DMState_GetAttackCrouchTime(const ai_dm_state_t *state);
void AI_DMState_SetAttackCrouchTime(ai_dm_state_t *state, float crouch_time);
void AI_DMState_ChangeViewAngles(ai_dm_state_t *state,
	const bot_client_state_t *client_state,
	float think_time);
void AI_DMState_AimAtEnemy(ai_dm_state_t *state,
	const bot_client_state_t *client_state,
	const ai_dm_enemy_info_t *enemy,
	float think_time);
int AI_DMState_CheckAttack(ai_dm_state_t *state,
	const bot_client_state_t *client_state,
	const ai_dm_enemy_info_t *enemy,
	float now);
void AI_DMState_GetMetrics(const ai_dm_state_t *state, ai_dm_metrics_t *out_metrics);
void AI_DMState_SetAttackChaseTestState(ai_dm_state_t *state,
	float attackchase_time,
	int last_enemy_area);
void AI_DMState_Update(ai_dm_state_t *state,
                       const bot_client_state_t *client_state,
                       const ai_goal_selection_t *selection,
                       const ai_dm_enemy_info_t *enemy,
                       const bot_input_t *last_move_command,
                       float now);
void AI_DMState_UpdateWithMoveResult(ai_dm_state_t *state,
	const bot_client_state_t *client_state,
	const ai_goal_selection_t *selection,
	const ai_dm_enemy_info_t *enemy,
	const bot_input_t *last_move_command,
	float now,
	ai_dm_move_result_handler_t move_result_handler,
	void *move_result_context);

#ifdef __cplusplus
}
#endif

