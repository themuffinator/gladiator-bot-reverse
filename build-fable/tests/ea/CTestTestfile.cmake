# CMake generated Testfile for 
# Source directory: E:/Repositories/Gladiator-Bot-reverse/tests/ea
# Build directory: E:/Repositories/Gladiator-Bot-reverse/build-fable/tests/ea
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test(ea_tests "E:/Repositories/Gladiator-Bot-reverse/build-fable/tests/ea/Debug/ea_tests.exe")
  set_tests_properties(ea_tests PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/ea/CMakeLists.txt;21;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/ea/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test(ea_tests "E:/Repositories/Gladiator-Bot-reverse/build-fable/tests/ea/Release/ea_tests.exe")
  set_tests_properties(ea_tests PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/ea/CMakeLists.txt;21;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/ea/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test(ea_tests "E:/Repositories/Gladiator-Bot-reverse/build-fable/tests/ea/MinSizeRel/ea_tests.exe")
  set_tests_properties(ea_tests PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/ea/CMakeLists.txt;21;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/ea/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test(ea_tests "E:/Repositories/Gladiator-Bot-reverse/build-fable/tests/ea/RelWithDebInfo/ea_tests.exe")
  set_tests_properties(ea_tests PROPERTIES  _BACKTRACE_TRIPLES "E:/Repositories/Gladiator-Bot-reverse/tests/ea/CMakeLists.txt;21;add_test;E:/Repositories/Gladiator-Bot-reverse/tests/ea/CMakeLists.txt;0;")
else()
  add_test(ea_tests NOT_AVAILABLE)
endif()
