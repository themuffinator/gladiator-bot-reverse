set(BOTLIB_AI_SOURCES
    ai_dm.c
    ai_character.c
    ai_weapon.c
    goal_move_orchestrator.c
    goal/ai_goal.c
    goal/bot_goal.c
    weight/ai_weight.c
    weight/bot_weight.c
    move/bot_move.c
    move/mover_catalogue.c
    weapon/bot_weapon.c
    character/bot_character.c
    chat/ai_chat.c
)

add_library(botlib_ai OBJECT ${BOTLIB_AI_SOURCES})

target_include_directories(botlib_ai
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
)
