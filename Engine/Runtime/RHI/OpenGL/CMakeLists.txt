# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# The real OpenGL 3.3+ Core Profile backend for uve_rhi's IRenderDeviceUVE/ICommandBufferUVE -
# GlRenderDeviceUVE, GlCommandBufferUVE (module-private, matching ChunkUVE/ArchetypeUVE's
# private-header precedent), and the raw-GL-call helpers (GlFunctionsUVE, GlErrorCheckUVE) both
# rely on. GlRenderDeviceUVE's constructor takes a Window::IWindowManagerUVE& (it assumes a
# current GL context already exists - see i_window_manager_uve.h's own contract) and calls
# glfwGetProcAddress() to resolve entry points, so this links uve_window and glfw directly; no
# public header here ever includes a GLFW or GL header (confined to the .cpp files), matching the
# reference module's own boundary.
#
# GlRenderDeviceUVE's dedicated reference test suite (gl_render_device_uve_tests.cpp, 2,154
# lines) is NOT ported here - it exercises real texture/mesh loading through
# Asset::AssetBundleUVE/MeshAssetUVE and Render::Shader::ShaderManagerUVE, neither of which is
# part of this RHI-only slice (the full Asset pipeline and the Shader subsystem are both future
# work). Porting that suite would mean porting those systems too, well beyond "RHI taxonomy
# rename." GlRenderDeviceUVE compiles clean and is exercised indirectly by
# Engine/Editor/Viewport's own (different) GL renderer, but has no automated test of its own in
# this repo yet - a real, acknowledged gap, not a hidden one.

find_package(OpenGL REQUIRED)

add_library(uve_rhi_opengl STATIC
    src/gl_functions_uve.cpp
    src/gl_error_check_uve.cpp
    src/gl_command_buffer_uve.cpp
    src/gl_render_device_uve.cpp
)
target_include_directories(uve_rhi_opengl PUBLIC include)
target_link_libraries(uve_rhi_opengl
    PUBLIC uve_rhi uve_window uve_platform uve_debug
    PRIVATE glfw OpenGL::GL
)
target_compile_features(uve_rhi_opengl PUBLIC cxx_std_20)
uve_set_warnings(uve_rhi_opengl)
