# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# The render systems built on top of RHI+Shader: RenderSystemUVE (frame submission),
# RenderGraphUVE/RenderQueueUVE (draw ordering), CameraSystemUVE, MeshRendererUVE/
# MeshRenderEligibilityUVE, PrimitiveGeometryUVE, LightSystemUVE, Renderer3DUVE (the composed
# per-frame 3D renderer), ComputeSystemUVE (the engine-level compute consumer layer over the
# M5a/M5b RHI compute slices), ViewportManagerUVE, and the particle rendering bridge
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
    Internal/render_system.cpp
    Internal/render_graph.cpp
    Internal/camera_2d_system.cpp
    Internal/camera_system.cpp
    Internal/compute_system.cpp
    Internal/particle_compute_simulation.cpp
    Internal/frustum_cull_compute.cpp
    Internal/frustum_cull_indirect.cpp
    Internal/mesh_skin_compute.cpp
    Internal/render_batch.cpp
    Internal/render_queue.cpp
    Internal/particle_render_bridge.cpp
    Internal/particle_draw_command.cpp
    Internal/mesh_visibility_set.cpp
    Internal/decal_draw_data.cpp
    Internal/decal_draw_command.cpp
    Internal/decal_renderer.cpp
    Internal/debug_renderer.cpp
    Internal/mesh_renderer.cpp
    Internal/mesh_render_eligibility.cpp
    Internal/primitive_geometry.cpp
    Internal/light_system.cpp
    Internal/renderer_2d.cpp
    Internal/renderer_3d.cpp
    Internal/viewport_manager.cpp
    Internal/dynamic_mesh_bvh.cpp
    Internal/shadow_atlas_manager.cpp
    Internal/light_shadow_planner.cpp
    Internal/contact_shadows.cpp
)
target_include_directories(uve_render_systems PUBLIC Expose)
target_link_libraries(uve_render_systems PUBLIC uve_platform uve_logging uve_containers uve_math uve_scene uve_objects_2d uve_asset uve_rhi uve_rhi_shader uve_ui)
target_link_libraries(uve_render_systems PUBLIC uve_object PRIVATE uve_utilities)
target_compile_features(uve_render_systems PUBLIC cxx_std_23)
uve_set_warnings(uve_render_systems)
