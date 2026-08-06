# Mirage's `Mirage` target is a STATIC library that links OpenMP::OpenMP_CXX
# PRIVATE (see mirage/CMakeLists.txt) when OpenMP is found at Mirage's own
# build time - true for the Linux release tarball (GCC's OpenMP is found for
# free), not necessarily the macOS one (Apple Clang needs extra hints
# find_package(OpenMP) doesn't supply automatically). CMake's export logic
# for STATIC libraries re-exposes PRIVATE link dependencies to consumers
# (wrapped in $<LINK_ONLY:...>) since a .a doesn't pull its own dependencies
# in at link time - so MirageConfig.cmake's set_target_properties() call
# needs OpenMP::OpenMP_CXX to already exist. Find it first, quietly: this is
# a consumer-side requirement of linking a static library, not a Mirage bug,
# and it's a no-op wherever Mirage itself didn't link OpenMP.
find_package(OpenMP)
find_package(Mirage CONFIG REQUIRED)
find_package(Maya REQUIRED)
include_directories(${MAYA_INCLUDE_DIR})
link_directories(${MAYA_LIBRARY_DIR})

add_library(${PROJECT_NAME}
    SHARED
    mainPlugin.cpp
    RenderProcedure.cpp
    RenderSession.cpp
    RenderWorker.cpp
    SceneProcessing.cpp
    ImageWriter.cpp
    nodes/RenderGlobals.cpp
    translators/MayaTransformUtils.cpp
    translators/MaterialTranslator.cpp
    translators/MeshTranslator.cpp
    translators/LightTranslator.cpp
    translators/SceneTranslator.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE ${MAYA_LIBRARIES} Mirage::Mirage)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# libslang.dylib (linked in transitively via Mirage::Mirage ->
# Mirage::slang-rhi-bundled) has an @rpath-relative install name, and
# Mirage's own exported CMake package (MirageConfig.cmake) does not
# propagate an rpath to consumers - every consumer of Mirage::Mirage has to
# add one itself, or the plugin will build cleanly and then fail to load at
# runtime with an unresolved-library error. Bake an absolute rpath to the
# Mirage install's lib/ directory (Mirage_DIR, set by find_package() above,
# points at <prefix>/lib/cmake/Mirage). CMAKE_BUILD_WITH_INSTALL_RPATH (set
# in the top-level CMakeLists.txt) makes this effective immediately, even
# when running the plugin straight out of the build directory rather than
# after `cmake --install` - the normal Maya plugin dev loop.
get_filename_component(MIRAGE_INSTALL_LIB_DIR "${Mirage_DIR}/../.." ABSOLUTE)
set_target_properties(${PROJECT_NAME} PROPERTIES
    INSTALL_RPATH "${MIRAGE_INSTALL_LIB_DIR}"
)

MAYA_PLUGIN(${PROJECT_NAME})
