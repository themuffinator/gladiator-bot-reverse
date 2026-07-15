# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "E:/Repositories/Gladiator-Bot-reverse/build-fable/_deps/cmocka-src")
  file(MAKE_DIRECTORY "E:/Repositories/Gladiator-Bot-reverse/build-fable/_deps/cmocka-src")
endif()
file(MAKE_DIRECTORY
  "E:/Repositories/Gladiator-Bot-reverse/build-fable/_deps/cmocka-build"
  "E:/Repositories/Gladiator-Bot-reverse/build-fable/_deps/cmocka-subbuild/cmocka-populate-prefix"
  "E:/Repositories/Gladiator-Bot-reverse/build-fable/_deps/cmocka-subbuild/cmocka-populate-prefix/tmp"
  "E:/Repositories/Gladiator-Bot-reverse/build-fable/_deps/cmocka-subbuild/cmocka-populate-prefix/src/cmocka-populate-stamp"
  "E:/Repositories/Gladiator-Bot-reverse/build-fable/_deps/cmocka-subbuild/cmocka-populate-prefix/src"
  "E:/Repositories/Gladiator-Bot-reverse/build-fable/_deps/cmocka-subbuild/cmocka-populate-prefix/src/cmocka-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "E:/Repositories/Gladiator-Bot-reverse/build-fable/_deps/cmocka-subbuild/cmocka-populate-prefix/src/cmocka-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "E:/Repositories/Gladiator-Bot-reverse/build-fable/_deps/cmocka-subbuild/cmocka-populate-prefix/src/cmocka-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
