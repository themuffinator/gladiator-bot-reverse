cmake_minimum_required(VERSION 3.16)
project(gladiator-bot-reverse LANGUAGES C CXX)

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
