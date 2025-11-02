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

add_executable(bspc_legacy_tests
    test_legacy_primitives.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/legacy_common.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/brush_bsp.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/csg.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/portals.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/tree.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/leakfile.cpp
)

target_include_directories(bspc_legacy_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tools/bspc/src
)

target_compile_features(bspc_legacy_tests PRIVATE cxx_std_17)

add_test(NAME bspc_legacy COMMAND bspc_legacy_tests)

add_executable(bspc_world_loader_tests
    test_load_world_state.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/world_loader.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/world_parser_utils.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/world_map_parser.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/world_bsp_parser.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/map_parser.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/bsp_serializers.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/filesystem_helper.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/logging.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/memory.cpp
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

add_executable(bspc_bsp_builder_tests
    test_bsp_builder.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/bsp_builder.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/world_loader.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/world_parser_utils.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/world_map_parser.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/world_bsp_parser.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/map_parser.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/bsp_serializers.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/filesystem_helper.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/logging.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/memory.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/legacy_common.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/brush_bsp.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/csg.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/portals.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/tree.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/leakfile.cpp
    ${PROJECT_SOURCE_DIR}/tools/bspc/src/threads.cpp
)

target_include_directories(bspc_bsp_builder_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tools/bspc/src
)

target_compile_features(bspc_bsp_builder_tests PRIVATE cxx_std_17)

target_compile_definitions(bspc_bsp_builder_tests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

if(UNIX AND NOT APPLE)
    target_link_libraries(bspc_bsp_builder_tests PRIVATE pthread)
endif()

add_test(NAME bspc_bsp_builder COMMAND bspc_bsp_builder_tests)
