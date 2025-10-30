if(NOT BUILD_TESTING)
    return()
endif()

set(AI_TEST_SOURCES
    test_ai_character.c
)

add_executable(ai_character_tests ${AI_TEST_SOURCES})
target_link_libraries(ai_character_tests PRIVATE ${BOTLIB_PARITY_TEST_LIBRARIES})
target_include_directories(ai_character_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/dev_tools/assets
)
if(UNIX AND NOT APPLE)
    target_link_libraries(ai_character_tests PRIVATE m)
endif()
add_test(NAME ai_character COMMAND ai_character_tests)
