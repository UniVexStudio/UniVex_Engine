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
    Internal/object_3d_common.cpp
    Internal/abstract_objects_3d.cpp
    ../Animation/Internal/abstract_animation_objects_3d.cpp
    ../Animation/Internal/animation_sequencer.cpp
    ../Animation/Internal/animation_graph.cpp
    ../Animation/Internal/skeleton_3d.cpp
    ../Animation/Internal/bone_attachment_3d.cpp
    ../Animation/Internal/spring_bone_simulator_3d.cpp
    ../Animation/Internal/two_bone_ik_3d.cpp
    ../Animation/Internal/full_body_ik_3d.cpp
    Physics/Internal/abstract_physics_objects_3d.cpp
    Physics/Internal/kinematic_3d.cpp
    Physics/Internal/area_3d.cpp
    Physics/Internal/character_3d.cpp
    Physics/Internal/character_prediction.cpp
    Physics/Internal/replicated_entity.cpp
    Physics/Internal/interest_relevancy.cpp
    Physics/Internal/hitbox_lag_wiring.cpp
    Physics/Internal/collider_3d.cpp
    Physics/Internal/hitbox_3d.cpp
    Physics/Internal/hurtbox_3d.cpp
    Physics/Internal/interaction_area_3d.cpp
    Physics/Internal/joint_3d.cpp
    Physics/Internal/physical_bone_3d.cpp
    Physics/Internal/ragdoll_3d.cpp
    Physics/Internal/collider_debug_scene.cpp
    Physics/Internal/projectile_3d.cpp
    Physics/Internal/ray_cast_3d.cpp
    Physics/Internal/shape_cast_3d.cpp
    Physics/Internal/soft_body_3d.cpp
    Physics/Internal/rigid_3d.cpp
    Physics/Internal/spring_arm_3d.cpp
    Physics/Internal/static_3d.cpp
    Physics/Internal/vehicle_3d.cpp
    Internal/csg_shape_3d.cpp
    Internal/fog_volume_3d.cpp
    Internal/gi_probes_3d.cpp
    Internal/grid_map_3d.cpp
    ../AI/3D/Internal/nav_mesh_volume_3d.cpp
    ../AI/3D/Internal/nav_obstacle_3d.cpp
    ../AI/3D/Internal/nav_seeker_3d.cpp
    ../AI/3D/Internal/smart_object_3d.cpp
    ../AI/3D/Internal/state_tree_3d.cpp
    Internal/marker_3d.cpp
    Internal/audio_listener_3d.cpp
    Internal/label_3d.cpp
    Internal/multi_mesh_instance_3d.cpp
    Internal/path_3d.cpp
    Internal/path_follow_3d.cpp
    Internal/remote_transform_3d.cpp
    Internal/sprite_3d.cpp
    Internal/visible_on_screen_notifier_3d.cpp
    Internal/preview_lighting.cpp
    Internal/world_environment_3d.cpp
    Internal/sky_3d.cpp
    Internal/reflection_probe_3d.cpp
    Internal/decal_3d.cpp
    Internal/directional_light_3d.cpp
    Internal/lod_group_3d.cpp
    Internal/occluder_3d.cpp
    Internal/visibility_region_3d.cpp
    Internal/spawn_point_3d.cpp
    Internal/health.cpp
    Internal/player_3d.cpp
    Internal/level_streamer_3d.cpp
    Internal/world_partition_3d.cpp
    Internal/xr_objects_3d.cpp
    Internal/object_3d.cpp
    Internal/camera_3d.cpp
    Internal/light_3d.cpp
    Internal/mesh_instance_3d.cpp
    Internal/box_mesh_3d.cpp
    Internal/sphere_mesh_3d.cpp
    Internal/plane_mesh_3d.cpp
    Internal/water_3d.cpp
    Internal/terrain_3d.cpp
    Internal/audio_source_3d.cpp
    Internal/particle_emitter_3d.cpp
    Internal/script.cpp
)
target_include_directories(uve_objects_3d PUBLIC Expose Physics/Expose ../AI/3D/Expose ../Animation/Expose)
target_link_libraries(uve_objects_3d PUBLIC uve_math uve_component uve_entity uve_animation uve_gameplay uve_physics uve_network PRIVATE uve_retarget)
target_include_directories(uve_objects_3d PRIVATE Internal Physics/Internal ../AI/3D/Internal ../Animation/Internal)
target_compile_features(uve_objects_3d PUBLIC cxx_std_23)
uve_set_warnings(uve_objects_3d)
