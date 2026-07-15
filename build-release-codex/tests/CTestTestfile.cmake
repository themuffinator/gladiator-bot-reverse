# CMake generated Testfile for 
# Source directory: E:/Repositories/Gladiator-Bot-reverse/tests
# Build directory: E:/Repositories/Gladiator-Bot-reverse/build-release-codex/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(parity_asset_verification "C:/Program Files/CMake/bin/cmake.exe" "-P" "E:/Repositories/Gladiator-Bot-reverse/build-release-codex/parity_asset_verification.cmake")
set_tests_properties(parity_asset_verification PROPERTIES  FIXTURES_SETUP "parity_assets" LABELS "Parity" _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/CMakeLists.txt;160;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/CMakeLists.txt;0;")
add_test(be_ai_parity_matrix "C:/Python311/python.exe" "E:/Repositories/Gladiator-Bot-reverse/dev_tools/scripts/check_be_ai_matrix.py")
set_tests_properties(be_ai_parity_matrix PROPERTIES  LABELS "Docs" _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/CMakeLists.txt;178;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/CMakeLists.txt;0;")
add_test(headless_quake2_parity "C:/Python311/python.exe" "E:/Repositories/Gladiator-Bot-reverse/tests/headless/run_headless_parity.py")
set_tests_properties(headless_quake2_parity PROPERTIES  ENVIRONMENT "GLADIATOR_Q2_MODULE_PATH=E:/Repositories/Gladiator-Bot-reverse/build-release-codex/gladiator.dll" LABELS "LongRunning" SKIP_RETURN_CODE "125" _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/CMakeLists.txt;188;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/CMakeLists.txt;0;")
subdirs("../_deps/cmocka-build")
subdirs("parity")
subdirs("chat")
subdirs("ai")
subdirs("ea")
subdirs("aas")
subdirs("common")
subdirs("tools")
subdirs("bspc")
