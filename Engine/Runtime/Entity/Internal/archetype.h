// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "archetype_signature.h"
#include "archetype_chunk.h"
#include "i_allocator.h"
#include "component_type_info.h"
#include "entity.h"

namespace UVE::Scene::Detail {

/// Archetype owns every ArchetypeChunk storing entities with exactly its ArchetypeSignature. It
/// orchestrates row reservation (creating a new chunk once the last one is full) and the
/// destroy/vacate contract ArchetypeChunk documents. Private implementation detail — never appears in
/// any public header under engine/scene/include/.
/// Thread-safety: not thread-safe — owned by IEntityManager, which documents the same
/// main/scene-thread-only contract.
class Archetype final {
public:
    /// A row's location within this archetype's chunk storage.
    struct Location {
        std::size_t chunkIndex = 0;
        std::size_t row = 0;
    };

    Archetype(ArchetypeSignature signature, Memory::IAllocator& allocator,
                 const std::unordered_map<std::type_index, ComponentTypeInfo>& typeInfos);

    [[nodiscard]] const ArchetypeSignature& GetSignature() const noexcept;
    [[nodiscard]] std::size_t GetEntityCount() const noexcept;

    /// Reserves a row for `entity` in the last non-full chunk, creating a new chunk if none
    /// exists or the last one is full. Every column's memory at the returned location is raw
    /// and uninitialized — the caller must construct every column (see ArchetypeChunk's contract)
    /// before the row is valid.
    [[nodiscard]] Location ReserveEntity(Entity entity);

    /// Destroys every column's data at `location`, then compacts (swap-remove). Returns the
    /// entity that moved into `location` (kInvalidEntity if none) — the caller must patch
    /// that entity's record to point at `location`. UVE_ASSERTs `location.chunkIndex` is in
    /// range; also returns kInvalidEntity and logs an error in Release on an out-of-range
    /// index, rather than indexing past m_chunks.
    [[nodiscard]] Entity DestroyEntityAt(Location location);

    /// Compacts (swap-remove) at `location` WITHOUT destroying anything first — used by
    /// archetype-migration callers that have already moved/destroyed every column themselves.
    /// Same return-value contract as DestroyEntityAt(), including the out-of-range behavior.
    [[nodiscard]] Entity VacateWithoutDestroy(Location location);

    /// UVE_ASSERTs `chunkIndex` is in range; also throws std::out_of_range in Release on an
    /// out-of-range index (via std::vector::at()), rather than indexing past m_chunks.
    [[nodiscard]] ArchetypeChunk& GetChunk(std::size_t chunkIndex);
    [[nodiscard]] const ArchetypeChunk& GetChunk(std::size_t chunkIndex) const;

    /// Invokes `visitor(Entity, ArchetypeChunk&, std::size_t row)` once per occupied row across
    /// every chunk.
    template <typename TVisitor>
    void ForEachEntity(TVisitor&& visitor) {
        ForEachChunk([&visitor](ArchetypeChunk& chunk, std::size_t count) {
            for (std::size_t row = 0; row < count; ++row) {
                visitor(chunk.GetEntityAtRow(row), chunk, row);
            }
        });
    }

    /// Invokes `visitor(ArchetypeChunk&, std::size_t occupiedRowCount)` once per chunk.
    ///
    /// The chunk-granular counterpart to ForEachEntity, for callers that need to resolve
    /// something once per chunk rather than once per row - a chunk's column layout is fixed for
    /// its entire lifetime, so per-row column lookups repeat work that cannot have changed.
    /// ForEachEntity is defined in terms of this, so the two cannot disagree about which rows
    /// exist or in what order they are visited.
    template <typename TVisitor>
    void ForEachChunk(TVisitor&& visitor) {
        for (const std::unique_ptr<ArchetypeChunk>& chunk : m_chunks) {
            const std::size_t count = chunk->GetCount();
            if (count == 0U) {
                continue;
            }
            visitor(*chunk, count);
        }
    }

private:
    ArchetypeSignature m_signature;
    Memory::IAllocator& m_allocator;
    const std::unordered_map<std::type_index, ComponentTypeInfo>& m_typeInfos;
    std::vector<std::unique_ptr<ArchetypeChunk>> m_chunks;
};

} // namespace UVE::Scene::Detail
