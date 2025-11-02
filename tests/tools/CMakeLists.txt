if(NOT BUILD_TESTING)
    return()
endif()

add_executable(bspc_filesystem_tests
    test_bspc_filesystem.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/filesystem_helper.cpp
)

target_include_directories(bspc_filesystem_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/tools/bspc/src
)

target_compile_features(bspc_filesystem_tests PRIVATE cxx_std_17)

target_compile_definitions(bspc_filesystem_tests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

add_test(NAME bspc_filesystem COMMAND bspc_filesystem_tests)
