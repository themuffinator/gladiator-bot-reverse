if(NOT BUILD_TESTING)
    return()
endif()

set(AI_TEST_COMMON_SOURCES
    ../support/asset_env.c
)

function(add_gladiator_ai_test target source)
    add_executable(${target}
        ${source}
        ${AI_TEST_COMMON_SOURCES}
    )
    target_link_libraries(${target} PRIVATE botlib_interface ${BOTLIB_PARITY_TEST_LIBRARIES})
    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/dev_tools/assets
    )
    target_compile_definitions(${target} PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    if(UNIX AND NOT APPLE)
        target_link_libraries(${target} PRIVATE m)
    endif()
endfunction()

add_gladiator_ai_test(ai_character_tests test_ai_character.c)
add_test(NAME ai_character COMMAND ai_character_tests)

add_gladiator_ai_test(ai_goal_move_tests test_ai_goal_move.c)
add_test(NAME ai_goal_move COMMAND ai_goal_move_tests)

add_gladiator_ai_test(ai_weight_tests test_ai_weight.c)
add_test(NAME ai_weight COMMAND ai_weight_tests)

add_gladiator_ai_test(ai_weapon_tests test_ai_weapon.c)
add_test(NAME ai_weapon COMMAND ai_weapon_tests)

add_executable(ai_dm_tests
    test_ai_dm.c
    ${PROJECT_SOURCE_DIR}/src/botlib/ai/ai_dm.c
)
target_link_libraries(ai_dm_tests PRIVATE ${BOTLIB_PARITY_TEST_LIBRARIES})
target_include_directories(ai_dm_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/botlib/precomp
)
if(UNIX AND NOT APPLE)
    target_link_libraries(ai_dm_tests PRIVATE m)
endif()
add_test(NAME ai_dm COMMAND ai_dm_tests)

add_executable(ai_move_tests
    test_bot_move.c
)
target_link_libraries(ai_move_tests PRIVATE botlib_interface ${BOTLIB_PARITY_TEST_LIBRARIES})
target_include_directories(ai_move_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
if(UNIX AND NOT APPLE)
    target_link_libraries(ai_move_tests PRIVATE m)
endif()
add_test(NAME ai_move COMMAND ai_move_tests)
