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
    translators/InstancerTranslator.cpp
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
