if(NOT BUILD_TESTING)
    return()
endif()

option(BOTLIB_PARITY_ENABLE_SOURCES "Build botlib parity test sources" OFF)

set(PARITY_TEST_SOURCES
    test_bot_interface.c
    test_precompiler_lexer.c
)

if(BOTLIB_PARITY_ENABLE_SOURCES)
    add_executable(botlib_parity_tests ${PARITY_TEST_SOURCES})
    target_link_libraries(botlib_parity_tests PRIVATE ${BOTLIB_PARITY_TEST_LIBRARIES})
    target_include_directories(botlib_parity_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(botlib_parity_tests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME botlib_parity COMMAND botlib_parity_tests)
else()
    add_custom_target(botlib_parity_tests
        COMMENT "Botlib parity tests placeholder until BOTLIB_PARITY_ENABLE_SOURCES is enabled."
    )
endif()
