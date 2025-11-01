if(NOT BUILD_TESTING)
    return()
endif()

add_executable(aas_map_tests
    test_aas_map.c
)

target_link_libraries(aas_map_tests PRIVATE ${BOTLIB_PARITY_TEST_LIBRARIES})

target_include_directories(aas_map_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/dev_tools/assets
)

target_compile_definitions(aas_map_tests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

if(UNIX AND NOT APPLE)
    target_link_libraries(aas_map_tests PRIVATE m)
endif()

add_test(NAME aas_map COMMAND aas_map_tests)
