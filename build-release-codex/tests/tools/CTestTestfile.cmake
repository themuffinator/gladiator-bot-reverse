# CMake generated Testfile for 
# Source directory: E:/Repositories/Gladiator-Bot-reverse/tests/tools
# Build directory: E:/Repositories/Gladiator-Bot-reverse/build-release-codex/tests/tools
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(bspc_filesystem "E:/Repositories/Gladiator-Bot-reverse/build-release-codex/tests/tools/bspc_filesystem_tests.exe")
set_tests_properties(bspc_filesystem PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/tools/CMakeLists.txt;19;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/tools/CMakeLists.txt;0;")
add_test(bspc_cli_modes "C:/Python311/python.exe" "E:/Repositories/Gladiator-Bot-reverse/tools/bspc/run_cli_modes.py" "--bspc" "E:/Repositories/Gladiator-Bot-reverse/build-release-codex/tools/bspc/bspc.exe" "--workspace" "E:/Repositories/Gladiator-Bot-reverse/build-release-codex/test-output/bspc_cli")
set_tests_properties(bspc_cli_modes PROPERTIES  WORKING_DIRECTORY "E:/Repositories/Gladiator-Bot-reverse" _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/tools/CMakeLists.txt;22;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/tools/CMakeLists.txt;0;")
