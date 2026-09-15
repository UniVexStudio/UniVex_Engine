# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# ArchetypeUVE/ChunkUVE/ArchetypeSignatureUVE are private implementation details colocated with
# their .cpp files under Internal/ - target_include_directories below exposes only Expose/, so no
# other target ever sees them. IEntityManagerUVE/EntityManagerUVE own archetype/chunk-based ECS
# storage; EntityUVE (the handle) and the ComponentUVE concept/type-info descriptor it stores
# generically live in uve_component, which this module depends on (never the reverse - this is
# what keeps uve_component free of any Entity dependency).

add_library(uve_entity STATIC
    Internal/entity_manager_uve.cpp
    Internal/archetype_signature_uve.cpp
    Internal/chunk_uve.cpp
    Internal/archetype_uve.cpp
)
target_include_directories(uve_entity PUBLIC Expose)
target_link_libraries(uve_entity PUBLIC uve_platform uve_debug uve_memory uve_events uve_component)
target_compile_features(uve_entity PUBLIC cxx_std_20)
uve_set_warnings(uve_entity)
