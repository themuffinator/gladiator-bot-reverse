if(NOT BUILD_TESTING)
    return()
endif()

add_executable(bot_common_tests
    test_bot_common.c
    test_bot_common_stubs.c
    test_bot_common_precomp_stub.c
    ${PROJECT_SOURCE_DIR}/src/botlib/common/l_assets.c
    ${PROJECT_SOURCE_DIR}/src/botlib/common/l_crc.c
    ${PROJECT_SOURCE_DIR}/src/botlib/common/l_struct.c
    ${PROJECT_SOURCE_DIR}/src/botlib/common/l_utils.c
    ${PROJECT_SOURCE_DIR}/src/botlib/common/l_libvar.c
    ${PROJECT_SOURCE_DIR}/src/botlib/common/l_log.c
    ${PROJECT_SOURCE_DIR}/src/botlib/common/l_memory.c
)

target_include_directories(bot_common_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(bot_common_tests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

target_compile_features(bot_common_tests PRIVATE c_std_11)

# This standalone harness uses standard assert() for both checks and fixture
# calls, so Release builds must not compile those expressions away.
if(MSVC)
    target_compile_options(bot_common_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(bot_common_tests PRIVATE -UNDEBUG)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(bot_common_tests PRIVATE m)
endif()

add_test(NAME bot_common COMMAND bot_common_tests)
add_test(NAME bot_common_memory COMMAND bot_common_tests --memory-only)
add_test(NAME bot_common_crc COMMAND bot_common_tests --crc-only)
add_test(NAME bot_common_log COMMAND bot_common_tests --log-only)
add_test(NAME bot_common_utils COMMAND bot_common_tests --utils-only)
add_test(NAME bot_common_struct COMMAND bot_common_tests --struct-only)

add_executable(bot_common_libvar_tests
    test_libvar.c
    ${PROJECT_SOURCE_DIR}/src/botlib/common/l_libvar.c
    ${PROJECT_SOURCE_DIR}/src/botlib/common/l_memory.c
)

target_include_directories(bot_common_libvar_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_features(bot_common_libvar_tests PRIVATE c_std_11)

if(MSVC)
    target_compile_options(bot_common_libvar_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(bot_common_libvar_tests PRIVATE -UNDEBUG)
endif()

add_test(NAME bot_common_libvar COMMAND bot_common_libvar_tests)
