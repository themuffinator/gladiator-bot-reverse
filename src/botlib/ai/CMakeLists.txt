set(BOTLIB_AI_SOURCES
    ai_character.c
    ai_dm.c
    ai_weapon.c
    goal_move_orchestrator.c
    ../ai_chat/ai_chat.c
    ../ai_character/bot_character.c
    ../ai_goal/ai_goal.c
    ../ai_goal/bot_goal.c
    ../ai_move/ai_move.c
    ../ai_move/bot_move.c
    ../ai_move/mover_catalogue.c
    ../ai_weight/ai_weight.c
    ../ai_weight/bot_weight.c
    ../ai_weapon/bot_weapon.c
)

add_library(botlib_ai STATIC ${BOTLIB_AI_SOURCES})

register_botlib_sources(
    TARGET botlib_ai
    SOURCES
        ${BOTLIB_AI_SOURCES}
)

target_include_directories(botlib_ai
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
)
