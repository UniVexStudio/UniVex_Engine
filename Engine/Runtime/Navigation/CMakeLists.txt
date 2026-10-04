# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Navigation: the navmesh a NavigationRegion3D bakes, the pathfinder that answers a
# NavigationAgent3D, and the steering step that turns a path into the velocity the agent publishes.
#
# It sits above Physics on purpose. The bake samples the world through IRaycastSystemUVE rather than
# reading colliders itself, so what the navmesh believes is walkable is exactly what a body standing
# there would collide with, and the module never grows a second collision representation that could
# disagree with the first.

add_library(uve_navigation STATIC
    Internal/navmesh_uve.cpp
    Internal/navmesh_bake_uve.cpp
    Internal/nav_path_uve.cpp
)
target_include_directories(uve_navigation PUBLIC Expose)
target_link_libraries(uve_navigation PUBLIC uve_platform uve_logging uve_math uve_scene uve_physics)
target_compile_features(uve_navigation PUBLIC cxx_std_23)
uve_set_warnings(uve_navigation)
