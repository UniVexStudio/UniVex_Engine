# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Gameplay glue the rest of the engine can call without owning a game: named input actions,
# the interact request event, and the helpers that read them. Scene objects that a game drops
# in (Player3D) live under Engine/Runtime/Objects/3D.

add_library(uve_gameplay STATIC
    Internal/gameplay_input_uve.cpp
    Internal/gameplay_attributes_uve.cpp
    Internal/gameplay_tags_uve.cpp
    Internal/status_effects_uve.cpp
    Internal/trigger_volume_uve.cpp
    Internal/pawn_uve.cpp
    Internal/cinematic_uve.cpp
    Internal/camera_follow_uve.cpp
)
target_include_directories(uve_gameplay PUBLIC Expose)
target_link_libraries(uve_gameplay PUBLIC uve_input uve_math uve_component uve_physics)
target_compile_features(uve_gameplay PUBLIC cxx_std_23)
uve_set_warnings(uve_gameplay)
