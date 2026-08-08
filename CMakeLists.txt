cmake_minimum_required(VERSION 3.20)

set(LIB_MAJOR_VERSION "1")
set(LIB_MINOR_VERSION "3")
set(LIB_PATCH_VERSION "0")
set(LIB_VERSION_STRING "${LIB_MAJOR_VERSION}.${LIB_MINOR_VERSION}.${LIB_PATCH_VERSION}")

if (CMAKE_VERSION VERSION_LESS 3.0)
    PROJECT(MirageMaya CXX)
else()
    cmake_policy(SET CMP0048 NEW)
    PROJECT(MirageMaya VERSION "${LIB_VERSION_STRING}" LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build with the install RPATH baked in from the start, so a plugin run
# straight out of the build directory (the normal Maya plugin dev loop -
# nothing here gets "installed" to run it under Maya) resolves its runtime
# deps identically to an installed one. The actual RPATH entries (notably
# Mirage's install lib/ dir, needed to resolve libslang.dylib's @rpath -
# relative install name - see src/CMakeLists.txt) are set per-target once
# find_package(Mirage) is available.
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
if(APPLE)
    set(CMAKE_MACOSX_RPATH TRUE)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules)

# Maya devkit location (Maya 2023+ ships the devkit as a standalone download,
# not bundled under the Maya app itself) - see cmake/Modules/FindMaya.cmake
# for the full resolution logic (DEVKIT_LOCATION env var / MAYA_DEVKIT_ROOT
# cache variable). Nothing needs to be pre-set here; FindMaya.cmake resolves
# MAYA_INCLUDE_DIR/MAYA_LIBRARY_DIR itself when find_package(Maya) runs in
# src/CMakeLists.txt.

# Maya home directory
if(WIN32 AND NOT CYGWIN)
    get_filename_component(Maya_HOME_DIR "$ENV{USERPROFILE}/Documents/maya" ABSOLUTE)
elseif(APPLE)
    get_filename_component(Maya_HOME_DIR "$ENV{HOME}/Library/Preferences/Autodesk/maya" ABSOLUTE)
else()
    get_filename_component(Maya_HOME_DIR "$ENV{HOME}/maya" ABSOLUTE)
endif()

# Set default install prefix
# if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
#   set(CMAKE_INSTALL_PREFIX "${Maya_HOME_DIR}/plug-ins" CACHE PATH "Install path prefix" FORCE)
# endif()

# add plugin source code
add_subdirectory(src)

install(
    TARGETS 
        ${PROJECT_NAME} 
    ${MAYA_TARGET_TYPE} DESTINATION 
        ${Maya_HOME_DIR}/plug-ins
    # RUNTIME DESTINATION
    #     ${CMAKE_INSTALL_PREFIX}
    # LIBRARY DESTINATION
    #     ${CMAKE_INSTALL_PREFIX}
    # COMPONENT
    #     runtime
)

# install scripts
install(DIRECTORY ${CMAKE_SOURCE_DIR}/scripts/MirageMaya DESTINATION ${Maya_HOME_DIR}/scripts)