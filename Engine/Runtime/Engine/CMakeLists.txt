# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# EngineCoreUVE - the whole-engine bootstrap class - and everything it owns directly:
# EngineServicesUVE (the seam every subsystem accessor goes through), EngineConfigUVE,
# EngineStateUVE, FrameStatsUVE, IEditorViewportHostUVE/ISimulationControlUVE (the narrow
# editor-facing control seams), and VersionUVE.
# EngineCoreUVE creates the Android EGL window backend under __ANDROID__ and the
# GLFW backend on desktop. Both use the shared GLES/OpenGL render device interfaces.
# A platform-specific native host owns event dispatch and application lifecycle.
#
# uve_render in the reference module maps to this repo's RHI/RenderSystems split: EngineCoreUVE
# directly constructs both GlRenderDeviceUVE (real backend) and NullRenderDeviceUVE/
# NullWindowManagerUVE (headlessUVE), plus the render systems built on top (RenderSystemUVE,
# CameraSystemUVE, MeshRendererUVE, LightSystemUVE, Renderer3DUVE, ViewportManagerUVE) and
# ShaderManagerUVE - hence uve_rhi, uve_rhi_shader, uve_rhi_opengl, uve_rhi_null, and
# uve_render_systems all replace the reference's single uve_render dependency.

# Settings registration, platform path selection, CLI conversion, layer attachment, and
# EngineConfig application have no desktop-rendering dependency. Keep this seam buildable in
# CPU-only configurations. EngineCore is also built there, using its Null window/render backends
# so the real headless startup path and settings integration can be exercised without a display.
add_library(uve_engine_settings STATIC
    Internal/engine_project_settings_uve.cpp
)
target_include_directories(uve_engine_settings PUBLIC Expose)
target_link_libraries(uve_engine_settings PUBLIC uve_config uve_math uve_commandline uve_platform)
target_compile_features(uve_engine_settings PUBLIC cxx_std_23)
uve_set_warnings(uve_engine_settings)

add_library(uve_core STATIC
    Internal/version_uve.cpp
    Internal/engine_services_uve.cpp
    Internal/engine_core_uve.cpp
    Internal/raycast_audio_occlusion_uve.cpp
    Internal/input_map_document_uve.cpp
    Internal/uvscript_object_host_uve.cpp
)
target_include_directories(uve_core PUBLIC Expose)
target_link_libraries(uve_core
    PUBLIC uve_engine_settings uve_platform uve_logging uve_utilities uve_events uve_memory uve_threading uve_commandline
           uve_config uve_asset uve_scene uve_physics uve_navigation uve_gameplay uve_ai uve_input uve_audio uve_save uve_window
           uve_rhi uve_rhi_shader uve_rhi_opengl uve_rhi_vulkan uve_rhi_null uve_render_systems uve_uvscript uve_ui
    PRIVATE nlohmann_json::nlohmann_json uve_network
)
target_compile_features(uve_core PUBLIC cxx_std_23)
if(UVE_HAS_DESKTOP_GL OR ANDROID)
    target_compile_definitions(uve_core PRIVATE UVE_HAS_DESKTOP_GL=1)
else()
    target_compile_definitions(uve_core PRIVATE UVE_HAS_DESKTOP_GL=0)
endif()
uve_set_warnings(uve_core)
