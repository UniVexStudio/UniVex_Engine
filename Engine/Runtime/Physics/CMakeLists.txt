# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Backend-agnostic collision/narrow-phase over the ECS (AABB, box/sphere TOI, character
# controller, constraints, raycasts) ported from the reference engine's Physics module.
# Closes the CharacterBody3D gap left open when Engine/Runtime/Scene was ported - see
# nodes/scene_node_uve.h's restore note, which this module unblocks.

add_library(uve_physics STATIC
    Internal/area_overlap_lifecycle_tracker_uve.cpp
    Internal/character_controller_uve.cpp
    Internal/area_overlap_system_uve.cpp
    Internal/angular_dynamics_uve.cpp
    Internal/collider_world_aabb_cache_uve.cpp
    Internal/collider_world_aabb_dynamic_bvh_uve.cpp
    Internal/collision_lifecycle_tracker_uve.cpp
    Internal/collision_system_uve.cpp
    Internal/hinge_motor_limit_uve.cpp
    Internal/physics_constraint_system_uve.cpp
    Internal/physics_query_system_uve.cpp
    Internal/physics_system_uve.cpp
    Internal/raycast_system_uve.cpp
    Internal/shape_cast_system_uve.cpp
    Internal/shape_narrow_phase_uve.cpp
)
target_include_directories(uve_physics PUBLIC Expose)
target_link_libraries(uve_physics PUBLIC uve_platform uve_debug uve_math uve_scene)
target_compile_features(uve_physics PUBLIC cxx_std_20)
uve_set_warnings(uve_physics)
