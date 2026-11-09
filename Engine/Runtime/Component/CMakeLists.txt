# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# One real .h + .cpp pair per component — matching the per-file discipline of
# Engine/Runtime/Objects/3D. Each component's struct + function declarations live in its own
# header under Expose/uve/component; its validators/behavior are defined in a matching .cpp
# under Internal (pure-data components — Hierarchy, WorldTransform, EditorInternalEntity — get
# a deliberately-empty .cpp placeholder so every component still has the same pair, ready for
# its behavior the moment one exists).
#
# The three machinery headers intentionally keep NO .cpp because they cannot have one:
#   - component.h is a concept (template),
#   - component_type_info.h's MakeComponentTypeInfoUVE() is a template,
#   - entity.h's EntityUVE is the data-only entity handle (value semantics + std::hash),
#     not a component.
#
# Still depends only on Core-level modules (math, asset for PrefabInstanceComponentUVE's asset
# GUID reference) and never on Entity, so EntityUVE's manager (Engine/Runtime/Entity) can
# depend on this module without a cycle.

add_library(uve_component STATIC
    Internal/animation_driver_component.cpp
    Internal/animation_sequencer_component.cpp
    Internal/animation_graph_component.cpp
    Internal/area_component.cpp
    Internal/audio_source_component.cpp
    Internal/camera_2d_component.cpp
    Internal/camera_component.cpp
    Internal/camera_follow_component.cpp
    Internal/ui_layout_container_component.cpp
    Internal/ui_anchor_component.cpp
    Internal/ui_scrollbar_component.cpp
    Internal/ui_slider_component.cpp
    Internal/ui_progress_bar_component.cpp
    Internal/ui_dropdown_component.cpp
    Internal/ui_text_input_component.cpp
    Internal/ui_scroll_container_component.cpp
    Internal/ui_checkbox_component.cpp
    Internal/ui_tooltip_component.cpp
    Internal/ui_tween_component.cpp
    Internal/canvas_component.cpp
    Internal/character_controller_component.cpp
    Internal/collider_component.cpp
    Internal/disabled_component.cpp
    Internal/editor_internal_entity_component.cpp
    Internal/hierarchy_component.cpp
    Internal/light_component.cpp
    Internal/mesh_component.cpp
    Internal/editor_description_component.cpp
    Internal/name_component.cpp
    Internal/particle_emitter_component.cpp
    Internal/physics_interpolation_component.cpp
    Internal/prefab_instance_component.cpp
    Internal/primitive_mesh_component.cpp
    Internal/rigid_3d_component.cpp
    Internal/script_component.cpp
    Internal/auto_translate_component.cpp
    Internal/bone_modifier_component.cpp
    Internal/physics_object_component.cpp
    Internal/render_instance_component.cpp
    Internal/surface_instance_component.cpp
    Internal/light_emitter_component.cpp
    Internal/object_metadata_component.cpp
    Internal/process_component.cpp
    Internal/thread_group_component.cpp
    Internal/transform_component.cpp
    Internal/visibility_component.cpp
    Internal/sprite_2d_component.cpp
    Internal/ui_button_component.cpp
    Internal/ui_image_component.cpp
    Internal/ui_text_component.cpp
    Internal/skin_component.cpp
    Internal/morph_target_component.cpp
    Internal/world_transform_component.cpp
)
target_include_directories(uve_component PUBLIC Expose)
target_link_libraries(uve_component PUBLIC uve_math uve_asset uve_object)
target_compile_features(uve_component PUBLIC cxx_std_23)
