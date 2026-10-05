# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# One real .h + .cpp pair per 3D scene-node kind. The 21 data-carrying node components
# (RayCast3D, Skeleton3D, Hitbox3D, etc.) each define their authored struct + validator here;
# the 17 kinds whose data already lives in a shared component (Camera3D, Light3D, the primitive
# meshes, the physics bodies, etc.) each define a NodeDefinition here instead — the kind's
# creation recipe (components to attach, authored defaults, default entity name) — so every
# node kind has its own discoverable, customizable home rather than one shared header or one
# editor-side switch. Physics-specific Object3D kinds live under Physics/{Expose,Internal}, AI
# navigation nodes under ../AI/3D, and animation nodes under ../Animation. They remain part of
# this one uve_objects_3d library. uve_component supplies the component structs + EntityUVE,
# uve_entity supplies IEntityManagerUVE for the definition Apply functions, and uve_animation
# supplies the runtime animation types.

add_library(uve_objects_3d STATIC
    Internal/object_3d_common_uve.cpp
    Internal/abstract_objects_3d_uve.cpp
    ../Animation/Internal/abstract_animation_objects_3d_uve.cpp
    ../Animation/Internal/animation_sequencer_uve.cpp
    ../Animation/Internal/animation_graph_uve.cpp
    ../Animation/Internal/skeleton_3d_uve.cpp
    ../Animation/Internal/bone_attachment_3d_uve.cpp
    ../Animation/Internal/two_bone_ik_3d_uve.cpp
    Physics/Internal/abstract_physics_objects_3d_uve.cpp
    Physics/Internal/kinematic_3d_uve.cpp
    Physics/Internal/area_3d_uve.cpp
    Physics/Internal/character_3d_uve.cpp
    Physics/Internal/collider_3d_uve.cpp
    Physics/Internal/hitbox_3d_uve.cpp
    Physics/Internal/hurtbox_3d_uve.cpp
    Physics/Internal/interaction_area_3d_uve.cpp
    Physics/Internal/projectile_3d_uve.cpp
    Physics/Internal/ray_cast_3d_uve.cpp
    Physics/Internal/rigid_3d_uve.cpp
    Physics/Internal/spring_arm_3d_uve.cpp
    Physics/Internal/static_3d_uve.cpp
    Internal/fog_volume_3d_uve.cpp
    ../AI/3D/Internal/nav_mesh_volume_3d_uve.cpp
    ../AI/3D/Internal/nav_seeker_3d_uve.cpp
    Internal/marker_3d_uve.cpp
    Internal/world_environment_3d_uve.cpp
    Internal/reflection_probe_3d_uve.cpp
    Internal/decal_3d_uve.cpp
    Internal/directional_light_3d_uve.cpp
    Internal/lod_group_3d_uve.cpp
    Internal/occluder_3d_uve.cpp
    Internal/visibility_region_3d_uve.cpp
    Internal/spawn_point_3d_uve.cpp
    Internal/player_3d_uve.cpp
    Internal/level_streamer_3d_uve.cpp
    Internal/world_partition_3d_uve.cpp
    Internal/object_3d_uve.cpp
    Internal/camera_3d_uve.cpp
    Internal/light_3d_uve.cpp
    Internal/mesh_instance_3d_uve.cpp
    Internal/box_mesh_3d_uve.cpp
    Internal/sphere_mesh_3d_uve.cpp
    Internal/plane_mesh_3d_uve.cpp
    Internal/audio_source_3d_uve.cpp
    Internal/particle_emitter_3d_uve.cpp
    Internal/script_uve.cpp
)
target_include_directories(uve_objects_3d PUBLIC Expose Physics/Expose ../AI/3D/Expose ../Animation/Expose)
target_link_libraries(uve_objects_3d PUBLIC uve_math uve_component uve_entity uve_animation PRIVATE uve_retarget)
target_include_directories(uve_objects_3d PRIVATE Internal Physics/Internal ../AI/3D/Internal ../Animation/Internal)
target_compile_features(uve_objects_3d PUBLIC cxx_std_23)
uve_set_warnings(uve_objects_3d)
