# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Backend-agnostic collision/narrow-phase over the ECS (AABB, box/sphere TOI, character
# controller, constraints, raycasts) ported from the reference engine's Physics module.
# Closes the Character3D gap left open when Engine/Runtime/Scene was ported - see
# objects/scene_object.h's restore note, which this module unblocks.

add_library(uve_physics STATIC
    Internal/area_3d_runtime.cpp
    Internal/area_overlap_lifecycle_tracker.cpp
    Internal/character_controller.cpp
    Internal/character_body_motion.cpp
    Internal/character_world_query.cpp
    Internal/kinematic_body.cpp
    Internal/interaction_area.cpp
    Internal/hitbox_lag_compensation.cpp
    Internal/hitbox_strike.cpp
    Internal/hitbox_strike_lifecycle_tracker.cpp
    Internal/spring_arm.cpp
    Internal/projectile_3d_step.cpp
    Internal/ray_cast_3d_step.cpp
    Internal/area_overlap_system.cpp
    Internal/angular_dynamics.cpp
    Internal/collider_world_aabb_cache.cpp
    Internal/collider_world_aabb_dynamic_bvh.cpp
    Internal/collision_lifecycle_tracker.cpp
    Internal/collision_system.cpp
    Internal/continuous_collision.cpp
    Internal/deterministic_physics.cpp
    Internal/hinge_motor_limit.cpp
    Internal/physics_constraint_system.cpp
    Internal/physics_islands.cpp
    Internal/collider_debug_geometry.cpp
    Internal/ragdoll_solver.cpp
    Internal/ragdoll_world.cpp
    Internal/physics_query_system.cpp
    Internal/physics_system.cpp
    Internal/raycast_system.cpp
    Internal/shape_cast_system.cpp
    Internal/shape_narrow_phase.cpp
)
target_include_directories(uve_physics PUBLIC Expose)
target_link_libraries(uve_physics PUBLIC uve_platform uve_logging uve_math uve_scene uve_threading)
target_compile_features(uve_physics PUBLIC cxx_std_23)
uve_set_warnings(uve_physics)
