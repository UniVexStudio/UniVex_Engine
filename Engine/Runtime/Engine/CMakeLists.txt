# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# EngineCoreUVE - the whole-engine bootstrap class - and everything it owns directly:
# EngineServicesUVE (the seam every subsystem accessor goes through), EngineConfigUVE,
# EngineStateUVE, FrameStatsUVE, IEditorViewportHostUVE/ISimulationControlUVE (the narrow
# editor-facing control seams), and VersionUVE.
#
# Named Engine/Runtime/Engine (not nested under Core) to mirror real Unreal Engine's own module
# layout, verified against Engine/Source/Runtime/Engine in the real Unreal source tree: UEngine
# lives in a top-level Engine module, a sibling of Core, not inside it. This repo's
# Core/{Logging,Memory,Threading,Math,Utilities,Scheduling,Diagnostics} split stays reserved for
# the low-level HAL-like concerns Unreal's own Core module holds.
#
# EngineCoreUVE's dependency closure was surveyed three times this session as it grew: first
# found to need the full Asset importer pipeline and mobile Input (previously excluded), then
# the Android window backend was confirmed still safely excludable
# (#if defined(__ANDROID__)-guarded at the exact construction site, non-Android path uses the
# already-ported WindowManagerUVE). This final survey (after Asset and mobile Input both landed)
# diffed every uve/* #include across engine_core_uve.h/.cpp and engine_services_uve.h/.cpp
# against this repo - only the files in this module itself and the still-Android-guarded window
# backend were missing, confirming the closure is genuinely complete.
#
# uve_render in the reference module maps to this repo's RHI/RenderSystems split: EngineCoreUVE
# directly constructs both GlRenderDeviceUVE (real backend) and NullRenderDeviceUVE/
# NullWindowManagerUVE (headlessUVE), plus the render systems built on top (RenderSystemUVE,
# CameraSystemUVE, MeshRendererUVE, LightSystemUVE, Renderer3DUVE, ViewportManagerUVE) and
# ShaderManagerUVE - hence uve_rhi, uve_rhi_shader, uve_rhi_opengl, uve_rhi_null, and
# uve_render_systems all replace the reference's single uve_render dependency.

add_library(uve_core STATIC
    src/version_uve.cpp
    src/engine_services_uve.cpp
    src/engine_core_uve.cpp
)
target_include_directories(uve_core PUBLIC include)
target_link_libraries(uve_core
    PUBLIC uve_platform uve_debug uve_utilities uve_events uve_memory uve_threading uve_commandline
           uve_config uve_asset uve_scene uve_physics uve_input uve_audio uve_save uve_window
           uve_rhi uve_rhi_shader uve_rhi_opengl uve_rhi_null uve_render_systems
    PRIVATE nlohmann_json::nlohmann_json
)
target_compile_features(uve_core PUBLIC cxx_std_20)
uve_set_warnings(uve_core)
