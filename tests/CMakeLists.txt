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
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    set(WITH_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
    set(UNIT_TESTING OFF CACHE BOOL "Build with unit testing" FORCE)
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
    add_compile_definitions(_USE_MATH_DEFINES)
    set(BOTLIB_PARITY_TEST_LIBRARIES cmocka::cmocka)
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

if(WIN32)
    set(_parity_asset_script "${PROJECT_BINARY_DIR}/parity_asset_verification.cmake")
    set(_parity_asset_script_contents [=[
set(project_root "@PROJECT_SOURCE_DIR@")

if(DEFINED ENV{GLADIATOR_ASSET_DIR} AND NOT "$ENV{GLADIATOR_ASSET_DIR}" STREQUAL "")
    set(asset_root "$ENV{GLADIATOR_ASSET_DIR}")
else()
    set(asset_root "${project_root}/dev_tools/assets")
endif()

set(missing "")
macro(require_file description path)
    if(NOT EXISTS "${path}")
        list(APPEND missing "${description} (${path})")
    endif()
endmacro()

# Optional assets gate individual test cases that skip themselves when the file
# is absent; they must not fail the fixture, because this test is FIXTURES_SETUP
# for the whole parity suite and a hard failure here disables every parity
# executable over a fixture only one test case consults.
set(optional_missing "")
macro(optional_file description path)
    if(NOT EXISTS "${path}")
        list(APPEND optional_missing "${description} (${path})")
    endif()
endmacro()

optional_file("mover BSP" "${asset_root}/maps/test_mover.bsp")
optional_file("mover AAS" "${asset_root}/maps/test_mover.aas")
require_file("precompiler lexer source (fw_items.c)" "${asset_root}/fw_items.c")
require_file("precompiler lexer source (syn.c)" "${asset_root}/syn.c")
require_file("weapon configuration stub (weapons.c)" "${asset_root}/weapons.c")
require_file("default weapon weights (defaul_w.c)" "${asset_root}/default/defaul_w.c")
require_file("default item weights (defaul_i.c)" "${asset_root}/default/defaul_i.c")

if(missing)
    message(STATUS "Parity asset verification failed:")
    foreach(entry IN LISTS missing)
        message(STATUS " - ${entry}")
    endforeach()
    message(FATAL_ERROR
        "Stage the listed assets under ${asset_root} (or set GLADIATOR_ASSET_DIR to the correct asset root) "
        "before rerunning the parity suite. See tests/README.md for download locations and layout requirements.")
endif()

if(optional_missing)
    message(STATUS "Parity optional assets absent (dependent cases will skip):")
    foreach(entry IN LISTS optional_missing)
        message(STATUS " - ${entry}")
    endforeach()
endif()

message(STATUS "Parity assets verified: ${asset_root}")

if(NOT "$ENV{GLADIATOR_Q2_BASEDIR}" STREQUAL "" OR NOT "$ENV{GLADIATOR_Q2_DEDICATED_SERVER}" STREQUAL "")
    if("$ENV{GLADIATOR_Q2_BASEDIR}" STREQUAL "")
        message(STATUS "GLADIATOR_Q2_BASEDIR is unset; headless Quake II parity will skip.")
    else()
        set(require_q2 "")
        if(NOT IS_DIRECTORY "$ENV{GLADIATOR_Q2_BASEDIR}/baseq2")
            list(APPEND require_q2 "baseq2 asset directory ($ENV{GLADIATOR_Q2_BASEDIR}/baseq2)")
        endif()
        if(NOT EXISTS "$ENV{GLADIATOR_Q2_BASEDIR}/baseq2/pak0.pak")
            list(APPEND require_q2 "pak0.pak inside baseq2")
        endif()
        if(require_q2)
            message(STATUS "Quake II assets incomplete:")
            foreach(entry IN LISTS require_q2)
                message(STATUS " - ${entry}")
            endforeach()
        else()
            message(STATUS "Quake II assets detected at $ENV{GLADIATOR_Q2_BASEDIR}")
        endif()
    endif()
    if("$ENV{GLADIATOR_Q2_DEDICATED_SERVER}" STREQUAL "")
        message(STATUS "GLADIATOR_Q2_DEDICATED_SERVER is unset; headless Quake II parity will skip.")
    elseif(NOT EXISTS "$ENV{GLADIATOR_Q2_DEDICATED_SERVER}")
        message(STATUS "Quake II dedicated server missing or not executable: $ENV{GLADIATOR_Q2_DEDICATED_SERVER}")
    else()
        message(STATUS "Quake II dedicated server executable: $ENV{GLADIATOR_Q2_DEDICATED_SERVER}")
    endif()
else()
    message(STATUS
        "Headless Quake II parity assets not checked (set GLADIATOR_Q2_BASEDIR and GLADIATOR_Q2_DEDICATED_SERVER to validate).")
endif()
]=])
    string(CONFIGURE "${_parity_asset_script_contents}" _parity_asset_script_contents @ONLY)
    file(WRITE "${_parity_asset_script}" "${_parity_asset_script_contents}")

    add_test(
        NAME parity_asset_verification
        COMMAND ${CMAKE_COMMAND} -P "${_parity_asset_script}"
    )
else()
    add_test(
        NAME parity_asset_verification
        COMMAND ${PROJECT_SOURCE_DIR}/dev_tools/scripts/verify_parity_assets.sh
    )
endif()
set_tests_properties(
    parity_asset_verification
    PROPERTIES
        FIXTURES_SETUP parity_assets
        LABELS "Parity"
)

if(WIN32 AND EXISTS "${CMAKE_OBJDUMP}")
    set(_retail_export_script "${CMAKE_CURRENT_BINARY_DIR}/verify_retail_exports.cmake")
    file(WRITE "${_retail_export_script}" [=[
if(NOT DEFINED OBJDUMP OR NOT DEFINED GLADIATOR_DLL)
    message(FATAL_ERROR "OBJDUMP and GLADIATOR_DLL are required")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -p "${GLADIATOR_DLL}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Could not inspect ${GLADIATOR_DLL}: ${_error}")
endif()

string(REGEX MATCH
    "Ordinal +RVA +Name[^\r\n]*[\r\n]+[ \t]*1[ \t]+0x[0-9A-Fa-f]+[ \t]+GetBotAPI[\r\n]"
    _retail_export
    "${_output}"
)
if(NOT _retail_export)
    message(FATAL_ERROR "gladiator.dll must export GetBotAPI as ordinal 1")
endif()

string(REGEX MATCH
    "GetBotAPI[\r\n]+[ \t]*[0-9]+[ \t]+0x[0-9A-Fa-f]+[ \t]+[^\r\n]+"
    _additional_export
    "${_output}"
)
if(_additional_export)
    message(FATAL_ERROR "gladiator.dll has an export after GetBotAPI: ${_additional_export}")
endif()
]=])
    add_test(
        NAME gladiator_retail_export_surface
        COMMAND ${CMAKE_COMMAND}
            "-DOBJDUMP=${CMAKE_OBJDUMP}"
            "-DGLADIATOR_DLL=$<TARGET_FILE:gladiator>"
            -P "${_retail_export_script}"
    )
endif()

if(Python3_Interpreter_FOUND)
    add_test(
        NAME be_ai_parity_matrix
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/dev_tools/scripts/check_be_ai_matrix.py
    )
    set_tests_properties(
        be_ai_parity_matrix
        PROPERTIES
            LABELS "Docs"
    )

    add_test(
        NAME headless_quake2_parity
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/headless/run_headless_parity.py
    )
    set_tests_properties(
        headless_quake2_parity
        PROPERTIES
            ENVIRONMENT "GLADIATOR_Q2_MODULE_PATH_BUILD=$<TARGET_FILE:gladiator>"
            LABELS "LongRunning"
            SKIP_RETURN_CODE 125
    )
else()
    message(STATUS "Python 3 interpreter not found; skipping documentation and headless Python-driven checks")
endif()
