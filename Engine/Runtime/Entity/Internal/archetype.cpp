// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "archetype.h"

#include <utility>

#include "engine_assert.h"

namespace UVE::Scene::Detail {

Archetype::Archetype(ArchetypeSignature signature, Memory::IAllocator& allocator,
                            const std::unordered_map<std::type_index, ComponentTypeInfo>& typeInfos)
    : m_signature(std::move(signature)), m_allocator(allocator), m_typeInfos(typeInfos) {}

const ArchetypeSignature& Archetype::GetSignature() const noexcept {
    return m_signature;
}

std::size_t Archetype::GetEntityCount() const noexcept {
    std::size_t total = 0;
    for (const std::unique_ptr<ArchetypeChunk>& chunk : m_chunks) {
        total += chunk->GetCount();
    }
    return total;
}

Archetype::Location Archetype::ReserveEntity(Entity entity) {
    if (m_chunks.empty() || m_chunks.back()->IsFull()) {
        m_chunks.push_back(
            std::make_unique<ArchetypeChunk>(m_allocator, m_signature.GetTypes(), m_typeInfos));
    }
    const std::size_t chunkIndex = m_chunks.size() - 1;
    const std::size_t row = m_chunks[chunkIndex]->ReserveRow(entity);
    return Location{chunkIndex, row};
}

Entity Archetype::DestroyEntityAt(Location location) {
    UVE_ASSERT(location.chunkIndex < m_chunks.size());
    if (location.chunkIndex >= m_chunks.size()) {
        UVE_ERROR("Archetype: DestroyEntityAt received an out-of-range chunkIndex ({} >= {})",
                   location.chunkIndex, m_chunks.size());
        return kInvalidEntity;
    }
    ArchetypeChunk& chunk = *m_chunks[location.chunkIndex];
    chunk.DestroyAllColumns(location.row);
    return chunk.VacateRow(location.row);
}

Entity Archetype::VacateWithoutDestroy(Location location) {
    UVE_ASSERT(location.chunkIndex < m_chunks.size());
    if (location.chunkIndex >= m_chunks.size()) {
        UVE_ERROR("Archetype: VacateWithoutDestroy received an out-of-range chunkIndex ({} >= {})",
                   location.chunkIndex, m_chunks.size());
        return kInvalidEntity;
    }
    return m_chunks[location.chunkIndex]->VacateRow(location.row);
}

ArchetypeChunk& Archetype::GetChunk(std::size_t chunkIndex) {
    UVE_ASSERT(chunkIndex < m_chunks.size());
    return *m_chunks.at(chunkIndex); // throws std::out_of_range in Release if chunkIndex is invalid
}

const ArchetypeChunk& Archetype::GetChunk(std::size_t chunkIndex) const {
    UVE_ASSERT(chunkIndex < m_chunks.size());
    return *m_chunks.at(chunkIndex); // throws std::out_of_range in Release if chunkIndex is invalid
}

} // namespace UVE::Scene::Detail
