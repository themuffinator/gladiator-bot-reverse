cmake_minimum_required(VERSION 3.16)

if(NOT BUILD_TESTING)
    return()
endif()

set(BOTLIB_PARITY_FRAMEWORK "none" CACHE STRING "Unit test framework for botlib parity checks")
mark_as_advanced(BOTLIB_PARITY_FRAMEWORK)

find_package(Python3 COMPONENTS Interpreter)

if(BOTLIB_PARITY_FRAMEWORK STREQUAL "cmocka")
    include(FetchContent)
    # The original cmocka repository lives on git.cryptomilk.org, but that host
    # frequently blocks automated fetches that originate from CI environments.
    # Mirror the release tag from GitHub so contributors behind corporate
    # proxies (and the hosted CI runners) can clone the dependency reliably.
    FetchContent_Declare(
        cmocka
        GIT_REPOSITORY https://github.com/cmocka/cmocka.git
        GIT_TAG cmocka-1.1.7
        FIND_PACKAGE_ARGS 1.1.7
    )
    FetchContent_MakeAvailable(cmocka)
    if(NOT TARGET cmocka)
        add_library(cmocka INTERFACE)
        target_include_directories(cmocka INTERFACE ${cmocka_SOURCE_DIR}/include)
    endif()
    set(BOTLIB_PARITY_TEST_LIBRARIES cmocka::cmocka cmocka)
elseif(BOTLIB_PARITY_FRAMEWORK STREQUAL "none")
    set(BOTLIB_PARITY_TEST_LIBRARIES "")
else()
    message(FATAL_ERROR "Unsupported test framework: ${BOTLIB_PARITY_FRAMEWORK}")
endif()

add_subdirectory(parity)
add_subdirectory(chat)
add_subdirectory(ai)
add_subdirectory(ea)
add_subdirectory(aas)
add_subdirectory(common)
add_subdirectory(tools)
add_subdirectory(bspc)

if(Python3_Interpreter_FOUND)
    add_test(
        NAME headless_quake2_parity
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/headless/run_headless_parity.py
    )
    set_tests_properties(
        headless_quake2_parity
        PROPERTIES
            ENVIRONMENT "GLADIATOR_Q2_MODULE_PATH=$<TARGET_FILE:gladiator>"
            LABELS "LongRunning"
            SKIP_RETURN_CODE 125
    )
else()
    message(STATUS "Python 3 interpreter not found; skipping headless Quake II parity harness registration")
endif()
