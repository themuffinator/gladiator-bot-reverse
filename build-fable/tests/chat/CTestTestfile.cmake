# CMake generated Testfile for 
# Source directory: E:/Repositories/Gladiator-Bot-reverse/tests/chat
# Build directory: E:/Repositories/Gladiator-Bot-reverse/build-fable/tests/chat
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test(bot_chat "E:/Repositories/Gladiator-Bot-reverse/build-fable/tests/chat/Debug/bot_chat_tests.exe")
  set_tests_properties(bot_chat PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/chat/CMakeLists.txt;32;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/chat/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test(bot_chat "E:/Repositories/Gladiator-Bot-reverse/build-fable/tests/chat/Release/bot_chat_tests.exe")
  set_tests_properties(bot_chat PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/chat/CMakeLists.txt;32;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/chat/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test(bot_chat "E:/Repositories/Gladiator-Bot-reverse/build-fable/tests/chat/MinSizeRel/bot_chat_tests.exe")
  set_tests_properties(bot_chat PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/chat/CMakeLists.txt;32;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/chat/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test(bot_chat "E:/Repositories/Gladiator-Bot-reverse/build-fable/tests/chat/RelWithDebInfo/bot_chat_tests.exe")
  set_tests_properties(bot_chat PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/chat/CMakeLists.txt;32;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/chat/CMakeLists.txt;0;")
else()
  add_test(bot_chat NOT_AVAILABLE)
endif()
