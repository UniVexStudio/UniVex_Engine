# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Backend-agnostic collision/narrow-phase over the ECS (AABB, box/sphere TOI, character
# controller, constraints, raycasts) ported from the reference engine's Physics module.
# Closes the CharacterBody3D gap left open when Engine/Runtime/Scene was ported - see
# nodes/scene_node_uve.h's restore note, which this module unblocks.
#
# TrajectoryCollisionPredictionUVE needs FutureTrajectoryContractUVE, a small (194-line),
# genuinely self-contained value-type contract that lives at uve/core/... in the reference
# engine (physically under engine/core/, alongside EngineCoreUVE's own bootstrap code) despite
# having no dependency on it - only uve/math. Ported here, at its one real consumer, keeping its
# original include path (uve/core/future_trajectory_contract_uve.h) unchanged rather than
# inventing a new home for a single header.

add_library(uve_physics STATIC
    src/area_overlap_lifecycle_tracker_uve.cpp
    src/character_controller_uve.cpp
    src/area_overlap_system_uve.cpp
    src/angular_dynamics_uve.cpp
    src/collider_world_aabb_cache_uve.cpp
    src/collider_world_aabb_dynamic_bvh_uve.cpp
    src/collision_system_uve.cpp
    src/hinge_motor_limit_uve.cpp
    src/physics_constraint_system_uve.cpp
    src/physics_query_system_uve.cpp
    src/physics_system_uve.cpp
    src/raycast_system_uve.cpp
    src/shape_cast_system_uve.cpp
    src/trajectory_collision_prediction_uve.cpp
    src/shape_narrow_phase_uve.cpp
)
target_include_directories(uve_physics PUBLIC include)
target_link_libraries(uve_physics PUBLIC uve_platform uve_debug uve_math uve_scene)
target_compile_features(uve_physics PUBLIC cxx_std_20)
uve_set_warnings(uve_physics)
