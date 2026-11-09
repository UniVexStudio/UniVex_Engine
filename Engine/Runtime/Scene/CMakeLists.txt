# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# EntityUVE/ComponentUVE (uve_component) and IEntityManagerUVE/EntityManagerUVE plus their private
# ArchetypeUVE/ArchetypeChunkUVE/ArchetypeSignatureUVE storage internals (uve_entity) live in
# Engine/Runtime/Component and Engine/Runtime/Entity - uve_scene depends on both rather than
# compiling them directly. What lives here is the node/scene facade layer: SceneGraphUVE, the
# Object3D catalog, PrefabSystemUVE, and SceneSerializerUVE.
#
# nlohmann_json is linked PRIVATE (confined to scene_serializer.cpp).

add_library(uve_scene STATIC
    Internal/prefab_revision_policy.cpp
    Internal/children_index.cpp
    Internal/scene_graph.cpp
    Internal/bone_attachment_pass.cpp
    Internal/scene_world_frame.cpp
    Internal/two_bone_ik_pass.cpp
    Internal/full_body_ik_pass.cpp
    Internal/spawn_point_query.cpp
    Internal/marker_3d_query.cpp
    Internal/scene_component_metadata.cpp
    Internal/scene_serializer.cpp
    Internal/prefab_system.cpp
    Internal/objects/scene_object_registry.cpp
    Internal/objects/scene_object_type.cpp
    Internal/objects/scene_root.cpp
    Internal/objects/scene_folder.cpp
    Internal/particle_runtime.cpp
)
target_include_directories(uve_scene PUBLIC Expose)
target_link_libraries(uve_scene
    PUBLIC uve_platform uve_logging uve_memory uve_events uve_math uve_asset uve_component uve_entity uve_object uve_threading uve_animation uve_objects_3d uve_objects_canvas uve_objects_2d uve_strings
    PRIVATE nlohmann_json::nlohmann_json uve_gameplay uve_ai
)
target_compile_features(uve_scene PUBLIC cxx_std_23)
uve_set_warnings(uve_scene)
