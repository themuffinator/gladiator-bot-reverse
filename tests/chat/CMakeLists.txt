if(NOT BUILD_TESTING)
    return()
endif()

add_executable(bot_chat_tests
    test_bot_chat.c
    ${PROJECT_SOURCE_DIR}/src/botlib/ai_chat/ai_chat.c
    ${PROJECT_SOURCE_DIR}/src/botlib/common/l_crc.c
    ${PROJECT_SOURCE_DIR}/src/botlib/precomp/l_precomp.c
    ${PROJECT_SOURCE_DIR}/src/botlib/precomp/l_script.c
    ${PROJECT_SOURCE_DIR}/src/q2bridge/bridge.c
    test_bot_chat_stubs.c
)

target_include_directories(bot_chat_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_features(bot_chat_tests PRIVATE c_std_11)
target_compile_definitions(bot_chat_tests PRIVATE
    BOT_ASSET_ROOT="${PROJECT_SOURCE_DIR}/dev_tools/assets"
    BOTLIB_TEST_INJECT_SCRIPT_CREATE_FAILURE=1)

# The standalone harness intentionally uses standard assert() for calls whose
# side effects build its fixtures. Keep those checks active in Release builds.
if(MSVC)
    target_compile_options(bot_chat_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(bot_chat_tests PRIVATE -UNDEBUG)
endif()

add_test(NAME bot_chat COMMAND bot_chat_tests)
