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

if(NOT MSVC)
    target_link_libraries(bot_common_tests PRIVATE m)
endif()

add_test(NAME bot_common COMMAND bot_common_tests)
