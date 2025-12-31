#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "cmocka::cmocka" for configuration "Debug"
set_property(TARGET cmocka::cmocka APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(cmocka::cmocka PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/cmocka.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/cmocka.dll"
  )

list(APPEND _cmake_import_check_targets cmocka::cmocka )
list(APPEND _cmake_import_check_files_for_cmocka::cmocka "${_IMPORT_PREFIX}/lib/cmocka.lib" "${_IMPORT_PREFIX}/bin/cmocka.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
