# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# The render systems built on top of RHI+Shader: RenderSystemUVE (frame submission),
# RenderGraphUVE/RenderQueueUVE (draw ordering), CameraSystemUVE, MeshRendererUVE/
# MeshRenderEligibilityUVE, PrimitiveGeometryUVE, LightSystemUVE, Renderer3DUVE (the composed
# per-frame 3D renderer), ViewportManagerUVE, and the particle rendering bridge
# (ParticleRenderBridgeUVE/ParticleDrawCommandUVE) - the layer EngineCoreUVE itself sits on.
#
# uve_scene: these systems walk Scene::IEntityManagerUVE for Camera/Light/Mesh/PrimitiveMesh/
# WorldTransform components and Scene::ParticleRuntimeUVE for particle state (mirrors
# uve_physics's/uve_audio's own uve_scene dependency). uve_asset: mesh/texture/material/shader
# asset loading through AssetManagerUVE/AssetHandleUVE<T> (the runtime layer added alongside this
# slice's Asset extension), never a format importer.

add_library(uve_render_systems STATIC
    src/render_system_uve.cpp
    src/render_graph_uve.cpp
    src/camera_system_uve.cpp
    src/render_queue_uve.cpp
    src/particle_render_bridge_uve.cpp
    src/particle_draw_command_uve.cpp
    src/mesh_renderer_uve.cpp
    src/mesh_render_eligibility_uve.cpp
    src/primitive_geometry_uve.cpp
    src/light_system_uve.cpp
    src/renderer_3d_uve.cpp
    src/viewport_manager_uve.cpp
)
target_include_directories(uve_render_systems PUBLIC include)
target_link_libraries(uve_render_systems PUBLIC uve_platform uve_debug uve_math uve_scene uve_asset uve_rhi uve_rhi_shader)
target_compile_features(uve_render_systems PUBLIC cxx_std_20)
uve_set_warnings(uve_render_systems)
