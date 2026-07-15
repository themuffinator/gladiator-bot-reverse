#----------------------------------------------------------------
# Generated CMake target import file for configuration "MinSizeRel".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "gladiator::gladiator" for configuration "MinSizeRel"
set_property(TARGET gladiator::gladiator APPEND PROPERTY IMPORTED_CONFIGURATIONS MINSIZEREL)
set_target_properties(gladiator::gladiator PROPERTIES
  IMPORTED_IMPLIB_MINSIZEREL "${_IMPORT_PREFIX}/lib/gladiator.lib"
  IMPORTED_LOCATION_MINSIZEREL "${_IMPORT_PREFIX}/bin/gladiator.dll"
  )

list(APPEND _cmake_import_check_targets gladiator::gladiator )
list(APPEND _cmake_import_check_files_for_gladiator::gladiator "${_IMPORT_PREFIX}/lib/gladiator.lib" "${_IMPORT_PREFIX}/bin/gladiator.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
