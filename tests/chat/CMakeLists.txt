if(NOT BUILD_TESTING)
    return()
endif()

add_executable(bot_chat_tests
    test_bot_chat.c
    ${PROJECT_SOURCE_DIR}/src/botlib_ai_chat/ai_chat.c
    ${PROJECT_SOURCE_DIR}/src/botlib_precomp/l_precomp.c
    ${PROJECT_SOURCE_DIR}/src/botlib_precomp/l_script.c
    test_bot_chat_stubs.c
)

target_include_directories(bot_chat_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_features(bot_chat_tests PRIVATE c_std_11)
target_compile_definitions(bot_chat_tests PRIVATE BOT_ASSET_ROOT="${PROJECT_SOURCE_DIR}/dev_tools/assets")

add_test(NAME bot_chat COMMAND bot_chat_tests)
