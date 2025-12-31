# CMake generated Testfile for 
# Source directory: C:/Users/djdac/source/repos/gladiator-bot-reverse/tests/tools
# Build directory: C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/tests/tools
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(bspc_filesystem "C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/tests/tools/bspc_filesystem_tests.exe")
set_tests_properties(bspc_filesystem PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/djdac/source/repos/gladiator-bot-reverse/tests/tools/CMakeLists.txt;19;add_test;C:/Users/djdac/source/repos/gladiator-bot-reverse/tests/tools/CMakeLists.txt;0;")
add_test(bspc_cli_modes "C:/Python311/python.exe" "C:/Users/djdac/source/repos/gladiator-bot-reverse/tools/bspc/run_cli_modes.py" "--bspc" "C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/tools/bspc/bspc.exe" "--workspace" "C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/test-output/bspc_cli")
set_tests_properties(bspc_cli_modes PROPERTIES  WORKING_DIRECTORY "C:/Users/djdac/source/repos/gladiator-bot-reverse" _BACKTRACE_TRIPLES "C:/Users/djdac/source/repos/gladiator-bot-reverse/tests/tools/CMakeLists.txt;22;add_test;C:/Users/djdac/source/repos/gladiator-bot-reverse/tests/tools/CMakeLists.txt;0;")
