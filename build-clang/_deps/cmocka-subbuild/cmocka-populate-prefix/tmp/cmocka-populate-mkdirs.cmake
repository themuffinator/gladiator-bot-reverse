# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/_deps/cmocka-src")
  file(MAKE_DIRECTORY "C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/_deps/cmocka-src")
endif()
file(MAKE_DIRECTORY
  "C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/_deps/cmocka-build"
  "C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/_deps/cmocka-subbuild/cmocka-populate-prefix"
  "C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/_deps/cmocka-subbuild/cmocka-populate-prefix/tmp"
  "C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/_deps/cmocka-subbuild/cmocka-populate-prefix/src/cmocka-populate-stamp"
  "C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/_deps/cmocka-subbuild/cmocka-populate-prefix/src"
  "C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/_deps/cmocka-subbuild/cmocka-populate-prefix/src/cmocka-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/_deps/cmocka-subbuild/cmocka-populate-prefix/src/cmocka-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/djdac/source/repos/gladiator-bot-reverse/build-clang/_deps/cmocka-subbuild/cmocka-populate-prefix/src/cmocka-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
