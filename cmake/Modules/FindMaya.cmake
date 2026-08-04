# - Maya finder module
#
# Variables that will be defined:
# MAYA_FOUND          Defined if a Maya devkit installation has been detected
# MAYA_INCLUDE_DIR    Path to the devkit's include directory
# MAYA_LIBRARY_DIR    Path to the devkit's library directory
# MAYA_LIBRARIES      All the Maya libraries
#
# Maya 2023+ no longer bundles the devkit inside the application install
# (see e.g. /Applications/Autodesk/maya2027/devkit/README_DEVKIT_MOVED.txt) -
# it ships as a separate download extracted to its own root ("devkitBase"),
# containing include/ and lib/ directly. This module follows the same
# DEVKIT_LOCATION environment-variable convention Autodesk's own
# cmake/devkit.cmake (shipped inside that devkit package) uses, rather than
# guessing a per-version path under the Maya application bundle.

if(NOT DEFINED MAYA_VERSION)
    set(MAYA_VERSION 2027 CACHE STRING "Maya version (2026 or 2027)")
endif()

set(MAYA_COMPILE_DEFINITIONS "REQUIRE_IOSTREAM;_BOOL")
set(MAYA_TARGET_TYPE LIBRARY)

if(WIN32)
    set(MAYA_COMPILE_DEFINITIONS "${MAYA_COMPILE_DEFINITIONS};NT_PLUGIN")
    set(MAYA_OPENMAYA_LIB OpenMaya.lib)
    set(MAYA_FOUNDATION_LIB Foundation.lib)
    set(MAYA_PLUGIN_EXTENSION ".mll")
    set(MAYA_TARGET_TYPE RUNTIME)
    set(MAYA_DEVKIT_ROOT_DEFAULT "C:/Program Files/Autodesk/MayaDevkit${MAYA_VERSION}")
elseif(APPLE)
    set(MAYA_COMPILE_DEFINITIONS "${MAYA_COMPILE_DEFINITIONS};OSMac_")
    set(MAYA_OPENMAYA_LIB libOpenMaya.dylib)
    set(MAYA_FOUNDATION_LIB libFoundation.dylib)
    set(MAYA_PLUGIN_EXTENSION ".bundle")
    set(MAYA_DEVKIT_ROOT_DEFAULT "$ENV{HOME}/dev/devkitBase")
else()
    # Linux
    set(MAYA_COMPILE_DEFINITIONS "${MAYA_COMPILE_DEFINITIONS};LINUX")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    set(MAYA_OPENMAYA_LIB libOpenMaya.so)
    set(MAYA_FOUNDATION_LIB libFoundation.so)
    set(MAYA_PLUGIN_EXTENSION ".so")
    set(MAYA_DEVKIT_ROOT_DEFAULT "/opt/devkitBase")
endif()

# DEVKIT_LOCATION (env var) wins if set, matching Autodesk's own devkit.cmake
# convention; otherwise fall back to the per-platform default guess above.
if(DEFINED ENV{DEVKIT_LOCATION})
    set(MAYA_DEVKIT_ROOT_DEFAULT "$ENV{DEVKIT_LOCATION}")
endif()
set(MAYA_DEVKIT_ROOT "${MAYA_DEVKIT_ROOT_DEFAULT}" CACHE PATH
    "Root of the extracted Maya devkit package (contains include/ and lib/ directly). \
Set the DEVKIT_LOCATION environment variable or this cache variable.")

find_path(MAYA_LIBRARY_DIR ${MAYA_FOUNDATION_LIB}
    PATHS ${MAYA_DEVKIT_ROOT}
    PATH_SUFFIXES lib
    DOC "Maya devkit library path"
)

find_path(MAYA_INCLUDE_DIR maya/MFn.h
    PATHS ${MAYA_DEVKIT_ROOT}
    PATH_SUFFIXES include
    DOC "Maya devkit include path"
)

# Maya libraries
set(_MAYA_LIBRARIES OpenMaya OpenMayaAnim OpenMayaFX OpenMayaRender OpenMayaUI Foundation clew)
foreach(MAYA_LIB ${_MAYA_LIBRARIES})
    find_library(MAYA_${MAYA_LIB}_LIBRARY NAMES ${MAYA_LIB} PATHS ${MAYA_LIBRARY_DIR}
        NO_DEFAULT_PATH)
    if (MAYA_${MAYA_LIB}_LIBRARY)
        set(MAYA_LIBRARIES ${MAYA_LIBRARIES} ${MAYA_${MAYA_LIB}_LIBRARY})
    endif()
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Maya DEFAULT_MSG MAYA_INCLUDE_DIR MAYA_LIBRARIES)

if(NOT MAYA_FOUND)
    message(STATUS "Maya devkit not found - set the DEVKIT_LOCATION environment variable "
                    "(or -DMAYA_DEVKIT_ROOT=...) to the root of an extracted Maya ${MAYA_VERSION} "
                    "devkit package (containing include/ and lib/ directly), matching Autodesk's "
                    "own cmake/devkit.cmake convention for Maya 2023+.")
endif()

function(MAYA_PLUGIN _target)
    if (WIN32)
        set_target_properties(${_target} PROPERTIES
            LINK_FLAGS "/export:initializePlugin /export:uninitializePlugin"
        )
    endif()
    set_target_properties(${_target} PROPERTIES
        COMPILE_DEFINITIONS "${MAYA_COMPILE_DEFINITIONS}"
        PREFIX ""
        SUFFIX ${MAYA_PLUGIN_EXTENSION})
endfunction()
