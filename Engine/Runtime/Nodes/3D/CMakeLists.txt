# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# One real .h + .cpp pair per 3D scene-node data type (RayCast3D, Skeleton3D, Hitbox3D, etc.) —
# each type's struct definition and validator live in their own discoverable file here, instead of
# being crammed together in one shared header. Depends only on Core Math.

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
)
target_include_directories(uve_nodes_3d PUBLIC Expose)
target_link_libraries(uve_nodes_3d PUBLIC uve_math)
target_compile_features(uve_nodes_3d PUBLIC cxx_std_20)
uve_set_warnings(uve_nodes_3d)
