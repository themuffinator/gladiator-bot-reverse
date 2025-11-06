if(NOT BUILD_TESTING)
    return()
endif()

set(PARITY_TEST_SOURCES
    botlib_contract_loader.c
    jsmn.c
    ${PROJECT_SOURCE_DIR}/src/q2bridge/bridge.c
    test_bot_interface.c
    test_bot_move.c
    test_precompiler_lexer.c
    test_aas_debug.c
    test_bridge.c
    test_update_translator.c
    ../support/asset_env.c
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
