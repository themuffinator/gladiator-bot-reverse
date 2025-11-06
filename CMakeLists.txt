cmake_minimum_required(VERSION 3.16)
project(gladiator-bot-reverse LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(BotlibSources)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)

# TODO: Define targets for shared utilities under src/shared.
# TODO: Flesh out the remaining Q2 bridge entry points in src/q2bridge.
# TODO: Expand the botlib interface implementation in src/botlib/interface.

# Temporary target that keeps the reconstructed botlib utility sources building
# while their functionality is filled in.
add_subdirectory(src/botlib/common)
add_subdirectory(src/botlib/precomp)
add_subdirectory(src/botlib/aas)
add_subdirectory(src/botlib/ea)
add_subdirectory(src/botlib/ai)
add_subdirectory(src/q2bridge)
add_subdirectory(src/botlib/interface)

set(_botlib_source_directories
    ${PROJECT_SOURCE_DIR}/src/botlib/common
    ${PROJECT_SOURCE_DIR}/src/botlib/precomp
    ${PROJECT_SOURCE_DIR}/src/botlib/aas
    ${PROJECT_SOURCE_DIR}/src/botlib/ea
    ${PROJECT_SOURCE_DIR}/src/botlib/ai
    ${PROJECT_SOURCE_DIR}/src/botlib/ai_chat
    ${PROJECT_SOURCE_DIR}/src/botlib/ai_character
    ${PROJECT_SOURCE_DIR}/src/botlib/ai_goal
    ${PROJECT_SOURCE_DIR}/src/botlib/ai_move
    ${PROJECT_SOURCE_DIR}/src/botlib/ai_weapon
    ${PROJECT_SOURCE_DIR}/src/botlib/ai_weight
    ${PROJECT_SOURCE_DIR}/src/botlib/interface
)

verify_all_botlib_sources(${_botlib_source_directories})

add_library(gladiator SHARED)

get_property(_gladiator_botlib_sources GLOBAL PROPERTY BOTLIB_REGISTERED_SOURCES)
if(_gladiator_botlib_sources)
    target_sources(gladiator PRIVATE ${_gladiator_botlib_sources})
endif()
target_link_libraries(gladiator
    PRIVATE
        botlib_interface
        botlib_common
        botlib_precomp
        botlib_aas
        botlib_ai
        botlib_ea
        q2bridge
)
set_target_properties(gladiator
    PROPERTIES
        OUTPUT_NAME gladiator
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
)

install(
    TARGETS gladiator
    EXPORT GladiatorTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
    EXPORT GladiatorTargets
    NAMESPACE gladiator::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gladiator
)

add_subdirectory(tools/bspc)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Test integration plan:
# * Once the production targets above exist, expose them to the tests via
#   add_subdirectory(src/shared ...), add_subdirectory(src/q2bridge ...), and
#   add_subdirectory(src/botlib/interface ...) as appropriate so the test harness
#   can link against their exported libraries.
# * Enable CTest (enable_testing() and include(CTest)) and add a tests directory
#   (add_subdirectory(tests)) that will host upcoming integration and unit checks.
# * Inside tests/, compile lightweight mock bot libraries that mimic the
#   loadable modules expected by the runtime so we can exercise dynamic loading
#   behaviour during the test suite.
