#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "convex::convex" for configuration "Debug"
set_property(TARGET convex::convex APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(convex::convex PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libconvex.a"
  )

list(APPEND _cmake_import_check_targets convex::convex )
list(APPEND _cmake_import_check_files_for_convex::convex "${_IMPORT_PREFIX}/lib/libconvex.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
