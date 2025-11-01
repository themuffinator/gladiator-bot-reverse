if(NOT BUILD_TESTING)
    return()
endif()

set(EA_TEST_SOURCES
    test_ea_inputs.c
)

add_executable(ea_tests ${EA_TEST_SOURCES})
target_link_libraries(ea_tests PRIVATE botlib_ea ${BOTLIB_PARITY_TEST_LIBRARIES})
target_include_directories(ea_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(ea_tests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

if(UNIX AND NOT APPLE)
    target_link_libraries(ea_tests PRIVATE m)
endif()

add_test(NAME ea_tests COMMAND ea_tests)
