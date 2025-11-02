if(NOT BUILD_TESTING)
    return()
endif()

add_executable(bspc_map_parser_tests
    test_map_parser.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/map_parser.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/logging.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/filesystem_helper.cpp
)

target_include_directories(bspc_map_parser_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tools/bspc/src
)

target_link_libraries(bspc_map_parser_tests PRIVATE ${BOTLIB_PARITY_TEST_LIBRARIES})

target_compile_features(bspc_map_parser_tests PRIVATE cxx_std_17)

target_compile_definitions(bspc_map_parser_tests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

if(UNIX AND NOT APPLE)
    target_link_libraries(bspc_map_parser_tests PRIVATE pthread)
endif()

add_test(NAME bspc_map_parser COMMAND bspc_map_parser_tests)

add_executable(bspc_world_loader_tests
    test_load_world_state.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/bsp_builder.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/world_map_parser.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/world_bsp_parser.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/map_parser.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/logging.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/filesystem_helper.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/threads.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/memory.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/bsp_serializers.cpp
)

target_include_directories(bspc_world_loader_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tools/bspc/src
)

target_link_libraries(bspc_world_loader_tests PRIVATE ${BOTLIB_PARITY_TEST_LIBRARIES})

target_compile_features(bspc_world_loader_tests PRIVATE cxx_std_17)

target_compile_definitions(bspc_world_loader_tests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

if(UNIX AND NOT APPLE)
    target_link_libraries(bspc_world_loader_tests PRIVATE pthread)
endif()

add_test(NAME bspc_world_loader COMMAND bspc_world_loader_tests)
