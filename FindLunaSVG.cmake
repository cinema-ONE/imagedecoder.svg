#.rst:
# FindLunaSVG
# --------
# Finds the lunasvg library
#
# This will define the following variables::
#
# LUNASVG_FOUND - system has lunasvg
# LUNASVG_INCLUDE_DIRS - the lunasvg include directory
# LUNASVG_LIBRARIES - the lunasvg libraries
#
# and the following imported targets::
#
#   LunaSVG::LunaSVG - The lunasvg library

if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_LUNASVG lunasvg QUIET)
endif()

find_path(LUNASVG_INCLUDE_DIR NAMES lunasvg.h
                              PATHS ${PC_LUNASVG_INCLUDEDIR})
find_library(LUNASVG_LIBRARY NAMES lunasvg
                             PATHS ${PC_LUNASVG_LIBDIR})

set(LUNASVG_VERSION ${PC_LUNASVG_VERSION})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LunaSVG
                                  REQUIRED_VARS LUNASVG_LIBRARY LUNASVG_INCLUDE_DIR
                                  VERSION_VAR LUNASVG_VERSION)

if(LUNASVG_FOUND)
  set(LUNASVG_INCLUDE_DIRS ${LUNASVG_INCLUDE_DIR})
  set(LUNASVG_LIBRARIES ${LUNASVG_LIBRARY})

  if(NOT TARGET LunaSVG::LunaSVG)
    add_library(LunaSVG::LunaSVG UNKNOWN IMPORTED)
    set_target_properties(LunaSVG::LunaSVG PROPERTIES
                                           IMPORTED_LOCATION "${LUNASVG_LIBRARY}")
  endif()
endif()

mark_as_advanced(LUNASVG_INCLUDE_DIR LUNASVG_LIBRARY)
