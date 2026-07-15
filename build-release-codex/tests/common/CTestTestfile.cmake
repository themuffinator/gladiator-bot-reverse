# CMake generated Testfile for 
# Source directory: E:/Repositories/Gladiator-Bot-reverse/tests/common
# Build directory: E:/Repositories/Gladiator-Bot-reverse/build-release-codex/tests/common
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(bot_common "E:/Repositories/Gladiator-Bot-reverse/build-release-codex/tests/common/bot_common_tests.exe")
set_tests_properties(bot_common PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/common/CMakeLists.txt;39;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/common/CMakeLists.txt;0;")
add_test(bot_common_memory "E:/Repositories/Gladiator-Bot-reverse/build-release-codex/tests/common/bot_common_tests.exe" "--memory-only")
set_tests_properties(bot_common_memory PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/common/CMakeLists.txt;40;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/common/CMakeLists.txt;0;")
add_test(bot_common_crc "E:/Repositories/Gladiator-Bot-reverse/build-release-codex/tests/common/bot_common_tests.exe" "--crc-only")
set_tests_properties(bot_common_crc PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/common/CMakeLists.txt;41;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/common/CMakeLists.txt;0;")
add_test(bot_common_log "E:/Repositories/Gladiator-Bot-reverse/build-release-codex/tests/common/bot_common_tests.exe" "--log-only")
set_tests_properties(bot_common_log PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/common/CMakeLists.txt;42;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/common/CMakeLists.txt;0;")
add_test(bot_common_utils "E:/Repositories/Gladiator-Bot-reverse/build-release-codex/tests/common/bot_common_tests.exe" "--utils-only")
set_tests_properties(bot_common_utils PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/common/CMakeLists.txt;43;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/common/CMakeLists.txt;0;")
add_test(bot_common_struct "E:/Repositories/Gladiator-Bot-reverse/build-release-codex/tests/common/bot_common_tests.exe" "--struct-only")
set_tests_properties(bot_common_struct PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/common/CMakeLists.txt;44;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/common/CMakeLists.txt;0;")
add_test(bot_common_libvar "E:/Repositories/Gladiator-Bot-reverse/build-release-codex/tests/common/bot_common_libvar_tests.exe")
set_tests_properties(bot_common_libvar PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/common/CMakeLists.txt;65;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/common/CMakeLists.txt;0;")
