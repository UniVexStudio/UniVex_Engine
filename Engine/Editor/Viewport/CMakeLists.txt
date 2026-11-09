# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Ported from a standalone C++20/OpenGL viewport module: infinite grid, orbit camera, the four
# transform gizmos and the orientation gizmo. It draws the editor's own furniture - backdrop,
# grid, gizmos - while scene entities are rendered by the host engine's Renderer3DUVE and
# composited against this pass by the editor (see Internal/integration/editor_mesh_layer.h).
#
# The library splits in four deliberately. univex_viewport_core is pure CPU (math, camera, LOD,
# gizmo geometry and picking) and links nothing; univex_viewport_gl adds the GL renderer;
# univex_viewport_app_support is the frame-level render pass; univex_viewport_engine_bridge is
# the only target here that depends on the host UniVex engine. Core, gl and app_support stay
# engine-agnostic so this module keeps working if dropped into a different engine.

option(UNIVEX_VIEWPORT_BUILD_APP "Build the interactive GLFW demo" ON)
option(UNIVEX_VIEWPORT_BUILD_TOOLS "Build the headless capture tool" ON)
option(UNIVEX_VIEWPORT_BUILD_TESTS "Build the CPU-side test suite" ON)

# ---------------------------------------------------------------------------
# Embed the shaders into a generated header at configure time.
# ---------------------------------------------------------------------------
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/infinite_grid.vert UNIVEX_GRID_VERT_SOURCE)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/infinite_grid.frag UNIVEX_GRID_FRAG_SOURCE)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/grid_shader_sources.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/univex/render/grid_shader_sources.h
    @ONLY)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo_line.vert  UNIVEX_GIZMO_LINE_VERT)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo_line.frag  UNIVEX_GIZMO_LINE_FRAG)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo_solid.vert UNIVEX_GIZMO_SOLID_VERT)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo_solid.frag UNIVEX_GIZMO_SOLID_FRAG)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gizmo_shader_sources.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/univex/render/gizmo_shader_sources.h
    @ONLY)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection_mask.vert     UNIVEX_SELECTION_MASK_VERT)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection_mask.frag     UNIVEX_SELECTION_MASK_FRAG)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection_outline.vert  UNIVEX_SELECTION_OUTLINE_VERT)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection_outline.frag  UNIVEX_SELECTION_OUTLINE_FRAG)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/selection_outline_shader_sources.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/univex/render/selection_outline_shader_sources.h
    @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection_mask.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection_mask.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection_outline.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection_outline.frag
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
    Internal/mat4.cpp
    Internal/orbit_camera.cpp
    Internal/gizmo_geometry.cpp
    Internal/nav_gizmo.cpp
    Internal/gizmo_picking.cpp
    Internal/viewport_metrics.cpp
    Internal/gizmo_drag.cpp
    Internal/bone_shape.cpp
    Internal/viewport_2d/camera_2d.cpp
    Internal/viewport_2d/grid_ruler.cpp
    Internal/viewport_2d/snapping.cpp
    Internal/viewport_2d/transform_gizmo.cpp
    Internal/viewport_2d/viewport_2d.cpp)
target_include_directories(univex_viewport_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Expose)
target_compile_options(univex_viewport_core PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# GL renderer.
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

add_library(univex_viewport_gl STATIC
    Internal/shader_program.cpp
    Internal/overlay_raster_state_gl.cpp
    Internal/infinite_grid_renderer.cpp
    Internal/gizmo_renderer.cpp
    Internal/selection_outline_renderer.cpp
    Internal/studio_backdrop_renderer.cpp)
target_include_directories(univex_viewport_gl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Expose
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(univex_viewport_gl PUBLIC univex_viewport_core OpenGL::GL GLEW::GLEW)
target_compile_options(univex_viewport_gl PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Viewport render pass (background, grid, gizmos) - engine-agnostic; depends
# only on univex_viewport_gl. Scene entities are the host renderer's job.
# ---------------------------------------------------------------------------
add_library(univex_viewport_app_support STATIC
    app/viewport_render_pass.cpp)
target_include_directories(univex_viewport_app_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app)
target_link_libraries(univex_viewport_app_support PUBLIC univex_viewport_gl)
target_compile_options(univex_viewport_app_support PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Engine bridge: the only target here that depends on the host UniVex engine.
# ---------------------------------------------------------------------------
add_library(univex_viewport_engine_bridge STATIC
    Internal/integration/editor_mesh_layer.cpp
    Internal/integration/scene_light_rules.cpp
    Internal/integration/overlay_raster_state_to_pipeline.cpp
    Internal/integration/entity_picker.cpp
    Internal/integration/selection_outline_geometry.cpp)
target_include_directories(univex_viewport_engine_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Internal)
# uve_core brings in EngineServicesUVE/Renderer3DUVE/GlRenderDeviceUVE/Scene for
# EditorMeshLayerUVE, which renders the live scene's real mesh/material entities into the
# Viewport panel.
target_link_libraries(univex_viewport_engine_bridge PUBLIC univex_viewport_app_support uve_world uve_core)
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
    target_link_libraries(univex_headless_capture PRIVATE univex_viewport_app_support glfw)
    target_compile_options(univex_headless_capture PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(UNIVEX_VIEWPORT_BUILD_TESTS)
    add_executable(univex_viewport_tests tests/viewport_tests.cpp)
    target_link_libraries(univex_viewport_tests PRIVATE univex_viewport_core)
    target_include_directories(univex_viewport_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Expose)
    target_compile_options(univex_viewport_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME viewport_tests COMMAND univex_viewport_tests)
endif()
