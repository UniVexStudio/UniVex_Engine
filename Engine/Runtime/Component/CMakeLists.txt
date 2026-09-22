# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# One real .h + .cpp pair per component — matching the per-file discipline of
# Engine/Runtime/Nodes/3D. Each component's struct + function declarations live in its own
# header under Expose/uve/component; its validators/behavior are defined in a matching .cpp
# under Internal (pure-data components — Hierarchy, WorldTransform, EditorInternalEntity — get
# a deliberately-empty .cpp placeholder so every component still has the same pair, ready for
# its behavior the moment one exists).
#
# The three machinery headers intentionally keep NO .cpp because they cannot have one:
#   - component_uve.h is a concept (template),
#   - component_type_info_uve.h's MakeComponentTypeInfoUVE() is a template,
#   - entity_uve.h's EntityUVE is the data-only entity handle (value semantics + std::hash),
#     not a component.
#
# Still depends only on Core-level modules (math, asset for PrefabInstanceComponentUVE's asset
# GUID reference) and never on Entity, so EntityUVE's manager (Engine/Runtime/Entity) can
# depend on this module without a cycle.

add_library(uve_component STATIC
    Internal/animation_player_component_uve.cpp
    Internal/area_component_uve.cpp
    Internal/audio_source_component_uve.cpp
    Internal/camera_component_uve.cpp
    Internal/canvas_component_uve.cpp
    Internal/character_controller_component_uve.cpp
    Internal/collider_component_uve.cpp
    Internal/editor_internal_entity_component_uve.cpp
    Internal/hierarchy_component_uve.cpp
    Internal/light_component_uve.cpp
    Internal/mesh_component_uve.cpp
    Internal/editor_description_component_uve.cpp
    Internal/name_component_uve.cpp
    Internal/particle_emitter_component_uve.cpp
    Internal/physics_interpolation_component_uve.cpp
    Internal/prefab_instance_component_uve.cpp
    Internal/primitive_mesh_component_uve.cpp
    Internal/rigid_body_component_uve.cpp
    Internal/script_component_uve.cpp
    Internal/auto_translate_component_uve.cpp
    Internal/node_metadata_component_uve.cpp
    Internal/process_component_uve.cpp
    Internal/thread_group_component_uve.cpp
    Internal/transform_component_uve.cpp
    Internal/visibility_component_uve.cpp
    Internal/ui_button_component_uve.cpp
    Internal/ui_image_component_uve.cpp
    Internal/ui_text_component_uve.cpp
    Internal/world_transform_component_uve.cpp
)
target_include_directories(uve_component PUBLIC Expose)
target_link_libraries(uve_component PUBLIC uve_math uve_asset)
target_compile_features(uve_component PUBLIC cxx_std_20)
