# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Ported from a standalone C++20/OpenGL viewport module (infinite grid, orbit camera, 4 transform
# gizmos + orientation gizmo) - unchanged logic except: ReferenceScene::Draw() gained a `model`
# matrix parameter (was implicitly always at the origin) and ViewportRenderPass gained an optional
# univex::integration::IEntityTransformSourceUVE* so scene geometry can be "one proxy cube per
# real engine entity" instead of always the single demo cube - see EntityTransformSource.h and
# WorldUveEntitySource.h/.cpp for why and how.
#
# The library splits in three deliberately: univex_viewport_core is pure CPU (math, camera, LOD)
# and links nothing; univex_viewport_gl adds the GL renderer; univex_viewport_engine_bridge (new)
# is the only target in this module that depends on the host UniVex engine (uve_world) - core/gl
# stay engine-agnostic so this module keeps working if dropped into a different engine.

option(UNIVEX_VIEWPORT_BUILD_APP "Build the interactive GLFW demo" ON)
option(UNIVEX_VIEWPORT_BUILD_TOOLS "Build the headless capture tool" ON)
option(UNIVEX_VIEWPORT_BUILD_TESTS "Build the CPU-side test suite" ON)

# ---------------------------------------------------------------------------
# Embed the shaders into a generated header at configure time.
# ---------------------------------------------------------------------------
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/infinite_grid.vert UNIVEX_GRID_VERT_SOURCE)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/infinite_grid.frag UNIVEX_GRID_FRAG_SOURCE)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GridShaderSources.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/univex/render/GridShaderSources.h
    @ONLY)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo_line.vert  UNIVEX_GIZMO_LINE_VERT)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo_line.frag  UNIVEX_GIZMO_LINE_FRAG)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo_solid.vert UNIVEX_GIZMO_SOLID_VERT)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo_solid.frag UNIVEX_GIZMO_SOLID_FRAG)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GizmoShaderSources.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/univex/render/GizmoShaderSources.h
    @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/infinite_grid.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/infinite_grid.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo_line.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo_line.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo_solid.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo_solid.frag)

# ---------------------------------------------------------------------------
# Pure-CPU core: math + camera + grid settings. No GL, no windowing, no
# dependency on the host UniVex engine.
# ---------------------------------------------------------------------------
add_library(univex_viewport_core STATIC
    src/Mat4.cpp
    src/OrbitCamera.cpp
    src/GizmoGeometry.cpp
    src/NavGizmo.cpp)
target_include_directories(univex_viewport_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(univex_viewport_core PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# GL renderer.
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

add_library(univex_viewport_gl STATIC
    src/ShaderProgram.cpp
    src/InfiniteGridRenderer.cpp
    src/GizmoRenderer.cpp)
target_include_directories(univex_viewport_gl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(univex_viewport_gl PUBLIC univex_viewport_core OpenGL::GL GLEW::GLEW)
target_compile_options(univex_viewport_gl PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Demo app support (ReferenceScene, ViewportRenderPass) - engine-agnostic;
# depends only on univex_viewport_gl.
# ---------------------------------------------------------------------------
add_library(univex_viewport_app_support STATIC
    app/ReferenceScene.cpp
    app/ViewportRenderPass.cpp)
target_include_directories(univex_viewport_app_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app)
target_link_libraries(univex_viewport_app_support PUBLIC univex_viewport_gl)
target_compile_options(univex_viewport_app_support PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Engine bridge: the only target here that depends on the host UniVex engine.
# ---------------------------------------------------------------------------
add_library(univex_viewport_engine_bridge STATIC
    src/integration/WorldUveEntitySource.cpp)
target_include_directories(univex_viewport_engine_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(univex_viewport_engine_bridge PUBLIC univex_viewport_app_support uve_world)
target_compile_options(univex_viewport_engine_bridge PRIVATE -Wall -Wextra -Wpedantic)
uve_set_warnings(univex_viewport_engine_bridge)

# ---------------------------------------------------------------------------
# Demo app + headless capture tool.
# ---------------------------------------------------------------------------
if(UNIVEX_VIEWPORT_BUILD_APP OR UNIVEX_VIEWPORT_BUILD_TOOLS)
    find_package(glfw3 REQUIRED)
endif()

if(UNIVEX_VIEWPORT_BUILD_APP)
    add_executable(univex_viewport_demo app/main.cpp)
    target_link_libraries(univex_viewport_demo PRIVATE univex_viewport_app_support glfw)
    target_compile_options(univex_viewport_demo PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(UNIVEX_VIEWPORT_BUILD_TOOLS)
    add_executable(univex_headless_capture tools/headless_capture.cpp)
    target_link_libraries(univex_headless_capture PRIVATE univex_viewport_engine_bridge glfw)
    target_compile_options(univex_headless_capture PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(UNIVEX_VIEWPORT_BUILD_TESTS)
    add_executable(univex_viewport_tests tests/ViewportTests.cpp)
    target_link_libraries(univex_viewport_tests PRIVATE univex_viewport_core)
    target_include_directories(univex_viewport_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(univex_viewport_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME viewport_tests COMMAND univex_viewport_tests)
endif()
