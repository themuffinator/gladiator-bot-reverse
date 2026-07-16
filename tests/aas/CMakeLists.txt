if(NOT BUILD_TESTING)
    return()
endif()

add_executable(aas_map_tests
    test_aas_map.c
)

target_link_libraries(aas_map_tests PRIVATE ${BOTLIB_PARITY_TEST_LIBRARIES})
target_link_libraries(aas_map_tests PRIVATE botlib_interface)

target_include_directories(aas_map_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/dev_tools/assets
)

target_compile_definitions(aas_map_tests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

if(UNIX AND NOT APPLE)
    target_link_libraries(aas_map_tests PRIVATE m)
endif()

add_test(NAME aas_map COMMAND aas_map_tests)
# cmocka reports the seven setup-skipped map-fixture cases with exit code 7
# when the externally supplied test_nav BSP/AAS pair is absent. Preserve
# normal assertion failures while presenting that known asset gate as a skip.
set_tests_properties(aas_map PROPERTIES SKIP_RETURN_CODE 7)

add_executable(aas_pointlight_tests
	test_aas_pointlight.c
)

target_link_libraries(aas_pointlight_tests PRIVATE ${BOTLIB_PARITY_TEST_LIBRARIES})
target_link_libraries(aas_pointlight_tests PRIVATE botlib_interface)

target_include_directories(aas_pointlight_tests PRIVATE
	${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(aas_pointlight_tests PRIVATE
	PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

if(UNIX AND NOT APPLE)
	target_link_libraries(aas_pointlight_tests PRIVATE m)
endif()

add_test(NAME aas_pointlight COMMAND aas_pointlight_tests)

add_executable(aas_debug_line_tests
	test_aas_debug_lines.c
)

target_link_libraries(aas_debug_line_tests PRIVATE ${BOTLIB_PARITY_TEST_LIBRARIES})
target_link_libraries(aas_debug_line_tests PRIVATE botlib_interface)

target_include_directories(aas_debug_line_tests PRIVATE
	${PROJECT_SOURCE_DIR}/src
)

if(UNIX AND NOT APPLE)
	target_link_libraries(aas_debug_line_tests PRIVATE m)
endif()

add_test(NAME aas_debug_lines COMMAND aas_debug_line_tests)

add_executable(aas_reach_pass_area_tests
	test_aas_reach_pass_areas.c
)

target_link_libraries(aas_reach_pass_area_tests PRIVATE ${BOTLIB_PARITY_TEST_LIBRARIES})
target_link_libraries(aas_reach_pass_area_tests PRIVATE botlib_interface)

target_include_directories(aas_reach_pass_area_tests PRIVATE
	${PROJECT_SOURCE_DIR}/src
)

if(UNIX AND NOT APPLE)
	target_link_libraries(aas_reach_pass_area_tests PRIVATE m)
endif()

add_test(NAME aas_reach_pass_areas COMMAND aas_reach_pass_area_tests)
