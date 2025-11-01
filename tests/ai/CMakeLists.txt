if(NOT BUILD_TESTING)
    return()
endif()

set(AI_TEST_SOURCES
    test_ai_character.c
    test_ai_goal_move.c
    test_ai_weight.c
    test_ai_weapon.c
    ../support/asset_env.c
)

add_executable(ai_character_tests ${AI_TEST_SOURCES})
target_link_libraries(ai_character_tests PRIVATE ${BOTLIB_PARITY_TEST_LIBRARIES})
target_include_directories(ai_character_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/botlib/ai
    ${PROJECT_SOURCE_DIR}/dev_tools/assets
)
target_compile_definitions(ai_character_tests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
if(UNIX AND NOT APPLE)
    target_link_libraries(ai_character_tests PRIVATE m)
endif()
add_test(NAME ai_character COMMAND ai_character_tests)

add_executable(ai_dm_tests
    test_ai_dm.c
    ${PROJECT_SOURCE_DIR}/src/botlib/ai/ai_dm.c
)
target_link_libraries(ai_dm_tests PRIVATE ${BOTLIB_PARITY_TEST_LIBRARIES})
target_include_directories(ai_dm_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/botlib/ai
    ${PROJECT_SOURCE_DIR}/src/botlib/precomp
)
if(UNIX AND NOT APPLE)
    target_link_libraries(ai_dm_tests PRIVATE m)
endif()
add_test(NAME ai_dm COMMAND ai_dm_tests)
