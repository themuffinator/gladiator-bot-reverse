/*
 * Gladiator goal selection design skeleton.
 *
 * Frame loop responsibilities:
 *   1. Maintain the goal stack (`bot_goalstate_t`) exactly like Quake III does:
 *      update `goalstacktop`, copy `bot_goal_t` records, and persist
 *      `lastreachabilityarea` after each reachability-aware query. The HLIL
 *      confirms that movement helpers exit early when the active reachability
 *      matches the goal, so the stack discipline must remain intact. 【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_goal.c†L1241-L1444】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40791-L40806】
 *   2. Recompute long-term (`BotChooseLTGItem`) and nearby (`BotChooseNBGItem`)
 *      objectives using fuzzy weights plus AAS travel times. Gladiator’s binary
 *      feeds the same inventory/goal inputs into its movement helper, implying
 *      we need drop/respawn handling, roam goal weighting, and travel-time caps
 *      identical to the Quake reference. 【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_goal.c†L1284-L1519】【F:docs/ai_movement_state_machines.md†L34-L60】
 *   3. Keep the avoid-goal and avoid-reach lists in sync with movement. The
 *      HLIL writes offsets `0x1d`–`0x1f` immediately after repathing, so the
 *      goal module must expose the same bookkeeping APIs (`BotAddToAvoidGoals`,
 *      `BotResetAvoidGoals`) to prevent desynchronisation. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40810-L40820】
 *
 * Data requirements:
 *   - Item configuration (`itemconfig`, `levelitems`) identical to the Quake
 *     data structures so fuzzy weights can be evaluated without shims.
 *   - Map annotations (camp spots, locations) loaded through the same parser
 *     pipeline as `be_ai_goal.c`.
 *
 * External dependencies:
 *   - AAS helpers for area resolution and travel time (`BotReachabilityArea`,
 *     `AAS_AreaTravelTimeToGoalArea`).
 *   - Weight config loader reused from the `weight` module.
 */
