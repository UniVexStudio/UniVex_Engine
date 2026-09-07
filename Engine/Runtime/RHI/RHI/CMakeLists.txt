# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# The generic, backend-neutral half of the reference engine's Render module - IRenderDeviceUVE,
# ICommandBufferUVE, and the resource/handle vocabulary (BufferHandleUVE, TextureHandleUVE,
# ShaderHandleUVE, PipelineHandleUVE, RenderResourceDescsUVE, UniformReflectionUVE,
# ShaderDataTypeUVE, RecordedCommandUVE) both interfaces are expressed in terms of. Mirrors
# Unreal's own RHI/OpenGLDrv/NullDrv split (a generic RHI module, backends as separate sibling
# modules) verified against the real Unreal Engine source earlier in this restructuring effort.
#
# Deliberately excludes the rest of the reference engine's ~10,000-line Render module -
# RenderSystemUVE, RenderGraphUVE, CameraSystemUVE, MeshRendererUVE, LightSystemUVE,
# Renderer3DUVE, ViewportManagerUVE, and the whole Shader/ subsystem (preprocessor, binary
# cache, ShaderManagerUVE, built-in shaders) - none of that is RHI, it's the renderer built on
# top of RHI, and stays future work.

add_library(uve_rhi STATIC
    src/shader_data_type_uve.cpp
)
target_include_directories(uve_rhi PUBLIC include)
target_link_libraries(uve_rhi PUBLIC uve_math uve_debug)
target_compile_features(uve_rhi PUBLIC cxx_std_20)
uve_set_warnings(uve_rhi)
