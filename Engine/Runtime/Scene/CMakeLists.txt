# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# EntityUVE/ComponentUVE (uve_component) and IEntityManagerUVE/EntityManagerUVE plus their private
# ArchetypeUVE/ChunkUVE/ArchetypeSignatureUVE storage internals (uve_entity) live in
# Engine/Runtime/Component and Engine/Runtime/Entity - uve_scene depends on both rather than
# compiling them directly. What lives here is the node/scene facade layer: SceneGraphUVE, the
# Node3D catalog, PrefabSystemUVE, and SceneSerializerUVE.
#
# nlohmann_json is linked PRIVATE (confined to scene_serializer_uve.cpp).

add_library(uve_scene STATIC
    src/prefab_revision_policy_uve.cpp
    src/scene_graph_uve.cpp
    src/scene_serializer_uve.cpp
    src/prefab_system_uve.cpp
    src/nodes/scene_node_registry_uve.cpp
    src/particle_runtime_uve.cpp
)
target_include_directories(uve_scene PUBLIC include)
target_link_libraries(uve_scene
    PUBLIC uve_platform uve_debug uve_memory uve_events uve_math uve_asset uve_component uve_entity uve_animation
    PRIVATE nlohmann_json::nlohmann_json
)
target_compile_features(uve_scene PUBLIC cxx_std_20)
uve_set_warnings(uve_scene)
