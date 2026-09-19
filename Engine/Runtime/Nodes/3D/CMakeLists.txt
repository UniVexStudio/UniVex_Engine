# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# One real .h + .cpp pair per 3D scene-node kind. The 21 data-carrying node components
# (RayCast3D, Skeleton3D, Hitbox3D, etc.) each define their authored struct + validator here;
# the 17 kinds whose data already lives in a shared component (Camera3D, Light3D, the primitive
# meshes, the physics bodies, etc.) each define a NodeDefinition here instead — the kind's
# creation recipe (components to attach, authored defaults, default entity name) — so every
# node kind has its own discoverable, customizable home rather than one shared header or one
# editor-side switch. uve_component supplies the component structs + EntityUVE, uve_entity
# supplies IEntityManagerUVE for the definition Apply functions, and uve_animation supplies
# AnimationTreeUVE for the (not-yet-creatable) AnimationTree definition.

add_library(uve_nodes_3d STATIC
    Internal/node_3d_common_uve.cpp
    Internal/ray_cast_3d_uve.cpp
    Internal/animatable_body_3d_uve.cpp
    Internal/navigation_region_3d_uve.cpp
    Internal/navigation_agent_3d_uve.cpp
    Internal/skeleton_3d_uve.cpp
    Internal/bone_attachment_3d_uve.cpp
    Internal/spring_arm_3d_uve.cpp
    Internal/marker_3d_uve.cpp
    Internal/hitbox_3d_uve.cpp
    Internal/hurtbox_3d_uve.cpp
    Internal/projectile_3d_uve.cpp
    Internal/interaction_area_3d_uve.cpp
    Internal/world_environment_3d_uve.cpp
    Internal/reflection_probe_3d_uve.cpp
    Internal/decal_3d_uve.cpp
    Internal/lod_group_3d_uve.cpp
    Internal/occluder_3d_uve.cpp
    Internal/visibility_region_3d_uve.cpp
    Internal/spawn_point_3d_uve.cpp
    Internal/level_streamer_3d_uve.cpp
    Internal/world_partition_3d_uve.cpp
    Internal/empty_uve.cpp
    Internal/camera_3d_uve.cpp
    Internal/light_3d_uve.cpp
    Internal/mesh_instance_3d_uve.cpp
    Internal/box_mesh_3d_uve.cpp
    Internal/sphere_mesh_3d_uve.cpp
    Internal/plane_mesh_3d_uve.cpp
    Internal/collider_3d_uve.cpp
    Internal/static_body_3d_uve.cpp
    Internal/rigid_body_3d_uve.cpp
    Internal/character_body_3d_uve.cpp
    Internal/area_3d_uve.cpp
    Internal/audio_source_3d_uve.cpp
    Internal/particle_emitter_3d_uve.cpp
    Internal/script_uve.cpp
    Internal/animation_player_uve.cpp
    Internal/animation_tree_uve.cpp
)
target_include_directories(uve_nodes_3d PUBLIC Expose)
target_link_libraries(uve_nodes_3d PUBLIC uve_math uve_component uve_entity uve_animation)
target_compile_features(uve_nodes_3d PUBLIC cxx_std_20)
uve_set_warnings(uve_nodes_3d)
