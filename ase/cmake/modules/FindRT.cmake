# - Try to find librt
# Once done, this will define
#
#  librt_FOUND - system has librt
#  librt_LIBRARIES - link these to use librt

find_package(PkgConfig)
pkg_check_modules(PC_RT QUIET rt)

# The library itself
find_library(librt_LIBRARIES
  NAMES rt
  HINTS ${PC_RT_LIBDIR}
        ${PC_RT_LIBRARY_DIRS})

if(librt_LIBRARIES)
  set(librt_FOUND true)
endif(librt_LIBRARIES)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(librt REQUIRED_VARS librt_LIBRARIES NAME_MISMATCHED)
