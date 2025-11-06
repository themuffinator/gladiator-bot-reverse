cmake_minimum_required(VERSION 3.16)

if(NOT BUILD_TESTING)
    return()
endif()

option(BOTLIB_PARITY_ENABLE_SOURCES "Build botlib parity test sources" ON)

set(_botlib_parity_framework_default "none")
if(BOTLIB_PARITY_ENABLE_SOURCES)
    set(_botlib_parity_framework_default "cmocka")
endif()

if(NOT DEFINED CACHE{BOTLIB_PARITY_FRAMEWORK})
    set(BOTLIB_PARITY_FRAMEWORK "${_botlib_parity_framework_default}" CACHE STRING
        "Unit test framework for botlib parity checks")
elseif(BOTLIB_PARITY_ENABLE_SOURCES AND BOTLIB_PARITY_FRAMEWORK STREQUAL "none")
    # Promote legacy caches that were configured before the framework default was adjusted.
    set(BOTLIB_PARITY_FRAMEWORK "${_botlib_parity_framework_default}" CACHE STRING
        "Unit test framework for botlib parity checks" FORCE)
endif()
mark_as_advanced(BOTLIB_PARITY_FRAMEWORK)

if(BOTLIB_PARITY_ENABLE_SOURCES AND BOTLIB_PARITY_FRAMEWORK STREQUAL "none")
    message(FATAL_ERROR
        "BOTLIB_PARITY_ENABLE_SOURCES=ON requires BOTLIB_PARITY_FRAMEWORK to select a supported framework, e.g. cmocka")
endif()

find_package(Python3 COMPONENTS Interpreter)

if(BOTLIB_PARITY_FRAMEWORK STREQUAL "cmocka")
    include(FetchContent)
    # The original cmocka repository lives on git.cryptomilk.org, but that host
    # frequently blocks automated fetches that originate from CI environments.
    # Mirror the release tag from GitLab so contributors behind corporate
    # proxies (and the hosted CI runners) can clone the dependency reliably.
    FetchContent_Declare(
        cmocka
        GIT_REPOSITORY https://gitlab.com/cmocka/cmocka.git
        GIT_TAG cmocka-1.1.7
        FIND_PACKAGE_ARGS 1.1.7
    )
    FetchContent_GetProperties(cmocka)
    if(NOT cmocka_POPULATED)
        FetchContent_Populate(cmocka)

        set(_cmocka_cmakelists "${cmocka_SOURCE_DIR}/CMakeLists.txt")
        if(EXISTS "${_cmocka_cmakelists}")
            file(READ "${_cmocka_cmakelists}" _cmocka_root)
            set(_cmocka_symlink_block [=[# Link combile database for clangd
execute_process(COMMAND cmake -E create_symlink
                "${cmocka_BINARY_DIR}/compile_commands.json"
                "${cmocka_SOURCE_DIR}/compile_commands.json")
]=])
            string(REPLACE "${_cmocka_symlink_block}" "" _cmocka_patched "${_cmocka_root}")
            if(NOT _cmocka_root STREQUAL _cmocka_patched)
                file(WRITE "${_cmocka_cmakelists}" "${_cmocka_patched}")
            endif()
        endif()

        add_subdirectory("${cmocka_SOURCE_DIR}" "${cmocka_BINARY_DIR}")
    endif()
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
