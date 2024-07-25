#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "hiredis::hiredis" for configuration "Debug"
set_property(TARGET hiredis::hiredis APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(hiredis::hiredis PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/libhiredis.dll.a"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/cyghiredis.dll"
  )

list(APPEND _cmake_import_check_targets hiredis::hiredis )
list(APPEND _cmake_import_check_files_for_hiredis::hiredis "${_IMPORT_PREFIX}/lib/libhiredis.dll.a" "${_IMPORT_PREFIX}/bin/cyghiredis.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
