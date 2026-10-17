# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# One real .h + .cpp pair per 2D scene-node kind, matching the convention
# Engine/Runtime/Objects/3D established. Sprite2D is the first node: its NodeDefinition here
# is the kind's creation recipe (components to attach, authored defaults, default entity
# name), while the component struct stays in Engine/Runtime/Component (one truth per
# concept). uve_component supplies the component structs + EntityUVE, uve_entity supplies
# IEntityManagerUVE for the Apply functions.
#

add_library(uve_objects_2d STATIC
    Internal/audio_anim_nav_input_2d_uve.cpp
    Internal/camera_2d_uve.cpp
    Internal/line_2d_uve.cpp
    Internal/marker_2d_uve.cpp
    Internal/path_2d_uve.cpp
    Internal/physics_2d_uve.cpp
    Internal/remote_transform_2d_uve.cpp
    Internal/rendering_lighting_2d_uve.cpp
    Internal/sprite_2d_uve.cpp
    Internal/tile_parallax_sprite_2d_uve.cpp
    Internal/visible_on_screen_notifier_2d_uve.cpp
)
target_include_directories(uve_objects_2d PUBLIC Expose)
target_link_libraries(uve_objects_2d PUBLIC uve_math uve_component uve_entity)
target_compile_features(uve_objects_2d PUBLIC cxx_std_23)
