cmake_minimum_required(VERSION 3.16)
project(gladiator-bot-reverse LANGUAGES C CXX)

# TODO: Define targets for shared utilities under src/shared.
# TODO: Implement the Q2 bridge components located in src/q2bridge.
# TODO: Add botlib interface sources from src/botlib/interface.

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
