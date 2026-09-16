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
# slice's Asset extension), never a format importer. uve_ui: Renderer3DUVE's "UIOverlay" pass
# consumes UI::UIRuntimeUVE's draw batch/font atlas exactly like it already consumes
# Scene::ParticleRuntimeUVE above - same optional-render-input pattern, different module.

add_library(uve_render_systems STATIC
    Internal/render_system_uve.cpp
    Internal/render_graph_uve.cpp
    Internal/camera_system_uve.cpp
    Internal/render_queue_uve.cpp
    Internal/particle_render_bridge_uve.cpp
    Internal/particle_draw_command_uve.cpp
    Internal/mesh_renderer_uve.cpp
    Internal/mesh_render_eligibility_uve.cpp
    Internal/primitive_geometry_uve.cpp
    Internal/light_system_uve.cpp
    Internal/renderer_3d_uve.cpp
    Internal/viewport_manager_uve.cpp
)
target_include_directories(uve_render_systems PUBLIC Expose)
target_link_libraries(uve_render_systems PUBLIC uve_platform uve_debug uve_math uve_scene uve_asset uve_rhi uve_rhi_shader uve_ui)
target_compile_features(uve_render_systems PUBLIC cxx_std_20)
uve_set_warnings(uve_render_systems)
