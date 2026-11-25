// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "i_allocator.h"
#include "component_type_info.h"
#include "entity.h"

namespace UVE::Scene::Detail {

/// Fixed capacity of every ArchetypeChunk — a simple, documented, testable policy (not the
/// byte-budget-driven variable capacity real DOTS chunks use; that's a documented future
/// optimization, not needed for correctness now).
inline constexpr std::size_t kChunkCapacity = 512;

/// Sentinel row returned by ReserveRow() if called on a full chunk (Release-build backstop —
/// no valid row is ever this value, since row indices are always < kChunkCapacity).
inline constexpr std::size_t kInvalidRow = static_cast<std::size_t>(-1);

/// ArchetypeChunk is one fixed-capacity (kChunkCapacity-entity) block of an Archetype's row
/// storage. Each component type in the owning archetype's signature gets its own separately-
/// allocated array of `size * kChunkCapacity` bytes (simpler than interleaving multiple
/// component arrays into one single allocation with computed offsets/padding — a documented
/// simplification). All buffers are allocated via IAllocator::Allocate()/
/// Deallocate() — never raw `new`/`delete` — but per-row construction/destruction inside
/// those buffers uses the type-erased ComponentTypeInfo function pointers (the type-erased
/// analog of Construct/Destroy — see docs/CODING_STANDARDS.md's allocator-boundary note).
///
/// Row lifecycle contract (enforced by Archetype, the only caller): a row reserved via
/// ReserveRow() has RAW, UNINITIALIZED memory for every column until the caller has
/// constructed (via ConstructDefault(), MoveComponentInto(), or the public template layer
/// placement-constructing directly into GetComponentPointer()'s result) every column at that
/// row — only then is the row considered valid. DestroyAllColumns()+VacateRow() is the
/// pair used to fully destroy an entity; VacateRow() alone (no preceding destroy) is used
/// during archetype migration, where every column was already individually moved-out or
/// destroyed by the caller.
/// Thread-safety: not thread-safe — owned entirely by its Archetype, which is owned by
/// IEntityManager, which documents the same main/scene-thread-only contract.
class ArchetypeChunk final {
public:
    ArchetypeChunk(Memory::IAllocator& allocator, const std::vector<std::type_index>& componentTypes,
             const std::unordered_map<std::type_index, ComponentTypeInfo>& typeInfos);
    ~ArchetypeChunk();

    ArchetypeChunk(const ArchetypeChunk&) = delete;
    ArchetypeChunk& operator=(const ArchetypeChunk&) = delete;
    ArchetypeChunk(ArchetypeChunk&&) = delete;
    ArchetypeChunk& operator=(ArchetypeChunk&&) = delete;

    [[nodiscard]] bool IsFull() const noexcept;
    [[nodiscard]] std::size_t GetCount() const noexcept;
    [[nodiscard]] bool HasColumn(std::type_index componentType) const;

    /// Reserves the next free row for `entity`. Every column's memory at that row is raw and
    /// uninitialized until the caller constructs it (see class doc comment). UVE_ASSERTs the
    /// chunk is not already full; also returns kInvalidRow and logs an error in Release on a
    /// full chunk, rather than writing past m_entitiesInChunk's capacity.
    [[nodiscard]] std::size_t ReserveRow(Entity entity);

    /// Default-constructs `componentType`'s data at `row`.
    void ConstructDefault(std::type_index componentType, std::size_t row);

    /// Destroys `componentType`'s already-constructed data at `row`.
    void DestroyComponent(std::type_index componentType, std::size_t row);

    /// Destroys every column's already-constructed data at `row`.
    void DestroyAllColumns(std::size_t row);

    /// Compacts the chunk after `row`'s data has already been fully destroyed/moved-out by the
    /// caller: if `row` wasn't the last occupied row, move-constructs the last row's data into
    /// `row` and returns the entity that moved (kInvalidEntity if no move was needed).
    /// UVE_ASSERTs `row` is in range; also returns kInvalidEntity and logs an error in
    /// Release on an out-of-range row, rather than reading/writing past m_count.
    [[nodiscard]] Entity VacateRow(std::size_t row);

    [[nodiscard]] void* GetComponentPointer(std::type_index componentType, std::size_t row) const;

    /// A column's base address and element stride, resolved once so a caller iterating rows does
    /// not pay a hash lookup per row.
    ///
    /// GetComponentPointer(type, row) is exactly `base + stride * row` for the same type, so
    /// this returns the two halves of that expression and nothing else - it cannot drift from the
    /// per-row accessor because the per-row accessor is defined in terms of it.
    ///
    /// Exists because ForEachErased resolves the same columns once PER ROW while iterating a
    /// chunk, even though a chunk's column layout is fixed for its whole lifetime. Measured at
    /// roughly 9.7x overhead against hoisting the resolution out of the row loop.
    struct ColumnView final {
        void* base = nullptr;
        std::size_t stride = 0U;

        [[nodiscard]] void* At(std::size_t row) const noexcept {
            return static_cast<std::byte*>(base) + (stride * row);
        }
    };

    [[nodiscard]] ColumnView GetColumnView(std::type_index componentType) const;
    /// UVE_ASSERTs `row` is in range; also returns kInvalidEntity and logs an error in
    /// Release on an out-of-range row.
    [[nodiscard]] Entity GetEntityAtRow(std::size_t row) const;

    /// Moves `componentType`'s data from this chunk's `sourceRow` into `destination`'s
    /// `destinationRow` (which must be raw/unconstructed for that column) — used by Archetype
    /// during add/remove-component migrations.
    void MoveComponentInto(std::type_index componentType, std::size_t sourceRow, ArchetypeChunk& destination,
                               std::size_t destinationRow) const;

private:
    struct Column {
        ComponentTypeInfo typeInfo;
        void* buffer = nullptr;
    };

    [[nodiscard]] const Column& GetColumn(std::type_index componentType) const;

    Memory::IAllocator& m_allocator;
    std::unordered_map<std::type_index, Column> m_columns;
    std::vector<Entity> m_entitiesInChunk;
    std::size_t m_count = 0;
};

} // namespace UVE::Scene::Detail
