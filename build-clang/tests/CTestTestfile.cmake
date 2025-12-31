# CMake generated Testfile for 
# Source directory: C:/Users/djdac/source/repos/gladiator-bot-reverse/tests
# Build directory: C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(parity_asset_verification "C:/Program Files/CMake/bin/cmake.exe" "-P" "C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/parity_asset_verification.cmake")
set_tests_properties(parity_asset_verification PROPERTIES  FIXTURES_SETUP "parity_assets" LABELS "Parity" _BACKTRACE_TRIPLES "C:/Users/djdac/source/repos/gladiator-bot-reverse/tests/CMakeLists.txt;156;add_test;C:/Users/djdac/source/repos/gladiator-bot-reverse/tests/CMakeLists.txt;0;")
add_test(be_ai_parity_matrix "C:/Python311/python.exe" "C:/Users/djdac/source/repos/gladiator-bot-reverse/dev_tools/scripts/check_be_ai_matrix.py")
set_tests_properties(be_ai_parity_matrix PROPERTIES  LABELS "Docs" _BACKTRACE_TRIPLES "C:/Users/djdac/source/repos/gladiator-bot-reverse/tests/CMakeLists.txt;174;add_test;C:/Users/djdac/source/repos/gladiator-bot-reverse/tests/CMakeLists.txt;0;")
add_test(headless_quake2_parity "C:/Python311/python.exe" "C:/Users/djdac/source/repos/gladiator-bot-reverse/tests/headless/run_headless_parity.py")
set_tests_properties(headless_quake2_parity PROPERTIES  ENVIRONMENT "GLADIATOR_Q2_MODULE_PATH=C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/gladiator.dll" LABELS "LongRunning" SKIP_RETURN_CODE "125" _BACKTRACE_TRIPLES "C:/Users/djdac/source/repos/gladiator-bot-reverse/tests/CMakeLists.txt;184;add_test;C:/Users/djdac/source/repos/gladiator-bot-reverse/tests/CMakeLists.txt;0;")
subdirs("../_deps/cmocka-build")
subdirs("parity")
subdirs("chat")
subdirs("ai")
subdirs("ea")
subdirs("aas")
subdirs("common")
subdirs("tools")
subdirs("bspc")
