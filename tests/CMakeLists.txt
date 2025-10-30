cmake_minimum_required(VERSION 3.16)

if(NOT BUILD_TESTING)
    return()
endif()

set(BOTLIB_PARITY_FRAMEWORK "cmocka" CACHE STRING "Unit test framework for botlib parity checks")
mark_as_advanced(BOTLIB_PARITY_FRAMEWORK)

if(BOTLIB_PARITY_FRAMEWORK STREQUAL "cmocka")
    include(FetchContent)
    FetchContent_Declare(
        cmocka
        GIT_REPOSITORY https://git.cryptomilk.org/projects/cmocka.git
        GIT_TAG cmocka-1.1.7
        FIND_PACKAGE_ARGS 1.1.7
    )
    FetchContent_MakeAvailable(cmocka)
    if(NOT TARGET cmocka)
        add_library(cmocka INTERFACE)
        target_include_directories(cmocka INTERFACE ${cmocka_SOURCE_DIR}/include)
    endif()
    set(BOTLIB_PARITY_TEST_LIBRARIES cmocka::cmocka cmocka)
else()
    message(FATAL_ERROR "Unsupported test framework: ${BOTLIB_PARITY_FRAMEWORK}")
endif()

add_subdirectory(parity)
