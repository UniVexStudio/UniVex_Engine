# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Backend-agnostic shader compilation/caching subsystem (ShaderManagerUVE, ShaderProgramUVE,
# ShaderPreprocessorUVE, ShaderBinaryCacheUVE, ShaderDiagnosticsParserUVE, BuiltInShadersUVE)
# ported from the reference engine's render/shader/ subfolder. Placed as a sibling of RHI/OpenGL
# and RHI/Null - it depends only on RHI's device/command-buffer/resource-handle interfaces
# (IRenderDeviceUVE, ICommandBufferUVE, PipelineHandleUVE, ShaderHandleUVE, etc.), never a
# concrete GL/Null backend, mirroring Unreal's own ShaderCore-alongside-RHI precedent verified
# earlier in this restructuring effort. A prerequisite for the render systems (RenderSystemUVE,
# Renderer3DUVE) that sit above it, still unported.
#
# uve_asset: ShaderManagerUVE resolves shader source through Asset::IFileSystemUVE (interface
# only - never a concrete AssetBundleUVE-backed pipeline). uve_threading/uve_events:
# ShaderManagerUVE's constructor takes Threading::IThreadPoolUVE&/Events::IEventSystemUVE&
# directly.

add_library(uve_rhi_shader STATIC
    Internal/built_in_shaders_uve.cpp
    Internal/shader_binary_cache_uve.cpp
    Internal/shader_diagnostics_parser_uve.cpp
    Internal/shader_manager_uve.cpp
    Internal/shader_preprocessor_uve.cpp
    Internal/shader_program_uve.cpp
)
target_include_directories(uve_rhi_shader PUBLIC Expose)
target_link_libraries(uve_rhi_shader PUBLIC uve_rhi uve_debug uve_math uve_asset uve_threading uve_events)
target_compile_features(uve_rhi_shader PUBLIC cxx_std_20)
uve_set_warnings(uve_rhi_shader)
