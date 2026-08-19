if(NOT BUILD_TESTING)
    return()
endif()

set(PARITY_SUPPORT_SOURCES
    botlib_contract_loader.c
    jsmn.c
    ../support/asset_env.c
)

if(BOTLIB_PARITY_ENABLE_SOURCES)
    function(add_botlib_parity_test target source)
        set(options)
        set(one_value_args)
        set(multi_value_args LIBRARIES EXTRA_SOURCES)
        cmake_parse_arguments(PARITY "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

        add_executable(${target} ${source} ${PARITY_SUPPORT_SOURCES} ${PARITY_EXTRA_SOURCES})
        target_link_libraries(${target} PRIVATE ${PARITY_LIBRARIES} ${BOTLIB_PARITY_TEST_LIBRARIES})
        target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
        target_compile_definitions(${target} PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
        # These suites reference fixture assets by project-relative path (e.g.
        # dev_tools/assets/fw_items.c), so they must run from the source root.
        # Without this they resolve nothing and fail in the build directory.
        add_test(NAME ${target} COMMAND ${target}
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(
            ${target}
            PROPERTIES
                FIXTURES_REQUIRED parity_assets
                LABELS "Parity"
        )
    endfunction()

    add_botlib_parity_test(botlib_parity_bot_interface test_bot_interface.c
        LIBRARIES botlib_interface
    )
    add_botlib_parity_test(botlib_parity_bot_move test_bot_move.c
        LIBRARIES botlib_interface
    )
    set_tests_properties(botlib_parity_bot_move PROPERTIES FIXTURES_REQUIRED "")
    add_botlib_parity_test(botlib_parity_precompiler_lexer test_precompiler_lexer.c
        LIBRARIES botlib_interface
    )
    add_botlib_parity_test(botlib_parity_aas_debug test_aas_debug.c
        LIBRARIES botlib_interface
    )
    add_botlib_parity_test(botlib_parity_bridge test_bridge.c
        LIBRARIES gladiator_shared_headers
        EXTRA_SOURCES
            ${PROJECT_SOURCE_DIR}/src/q2bridge/bridge.c
    )
    add_botlib_parity_test(botlib_parity_update_translator test_update_translator.c
        LIBRARIES botlib_common botlib_ai
        EXTRA_SOURCES
            ${PROJECT_SOURCE_DIR}/src/q2bridge/bridge.c
            ${PROJECT_SOURCE_DIR}/src/q2bridge/aas_translation.c
            ${PROJECT_SOURCE_DIR}/src/q2bridge/update_translator.c
    )
    set_tests_properties(botlib_parity_update_translator PROPERTIES FIXTURES_REQUIRED "")

    add_custom_target(botlib_parity_tests
        DEPENDS
            botlib_parity_bot_interface
            botlib_parity_bot_move
            botlib_parity_precompiler_lexer
            botlib_parity_aas_debug
            botlib_parity_bridge
            botlib_parity_update_translator
    )
else()
    add_custom_target(botlib_parity_tests
        COMMENT "Botlib parity tests placeholder until BOTLIB_PARITY_ENABLE_SOURCES is enabled."
    )
endif()
