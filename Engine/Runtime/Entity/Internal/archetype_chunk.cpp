// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "archetype_chunk.h"

#include <cstddef>

#include "engine_assert.h"

namespace UVE::Scene::Detail {

namespace {
[[nodiscard]] void* Offset(void* buffer, std::size_t elementSize, std::size_t row) {
    return static_cast<std::byte*>(buffer) + (elementSize * row);
}
} // namespace

ArchetypeChunk::ArchetypeChunk(Memory::IAllocator& allocator, const std::vector<std::type_index>& componentTypes,
                    const std::unordered_map<std::type_index, ComponentTypeInfo>& typeInfos)
    : m_allocator(allocator) {
    m_entitiesInChunk.resize(kChunkCapacity, kInvalidEntity);
    for (const std::type_index& type : componentTypes) {
        const ComponentTypeInfo& info = typeInfos.at(type);
        void* const buffer =
            m_allocator.Allocate(info.size * kChunkCapacity, info.alignment, __FILE__, __LINE__);
        m_columns.emplace(type, Column{info, buffer});
    }
}

ArchetypeChunk::~ArchetypeChunk() {
    for (std::size_t row = 0; row < m_count; ++row) {
        DestroyAllColumns(row);
    }
    for (auto& [type, column] : m_columns) {
        m_allocator.Deallocate(column.buffer);
    }
}

bool ArchetypeChunk::IsFull() const noexcept {
    return m_count >= kChunkCapacity;
}

std::size_t ArchetypeChunk::GetCount() const noexcept {
    return m_count;
}

bool ArchetypeChunk::HasColumn(std::type_index componentType) const {
    return m_columns.find(componentType) != m_columns.end();
}

const ArchetypeChunk::Column& ArchetypeChunk::GetColumn(std::type_index componentType) const {
    UVE_ASSERT(m_columns.find(componentType) != m_columns.end());
    return m_columns.at(componentType); // throws std::out_of_range in Release if this chunk lacks componentType
}

std::size_t ArchetypeChunk::ReserveRow(Entity entity) {
    UVE_ASSERT(!IsFull());
    if (IsFull()) {
        UVE_ERROR("ArchetypeChunk: ReserveRow called on a full chunk (capacity {})", kChunkCapacity);
        return kInvalidRow; // callers (Archetype::ReserveEntity) already check IsFull()
                                // before calling this, so this is a defense-in-depth backstop
    }
    const std::size_t row = m_count;
    m_entitiesInChunk[row] = entity;
    ++m_count;
    return row;
}

void ArchetypeChunk::ConstructDefault(std::type_index componentType, std::size_t row) {
    const Column& column = GetColumn(componentType);
    column.typeInfo.constructDefault(Offset(column.buffer, column.typeInfo.size, row));
}

void ArchetypeChunk::DestroyComponent(std::type_index componentType, std::size_t row) {
    const Column& column = GetColumn(componentType);
    column.typeInfo.destroy(Offset(column.buffer, column.typeInfo.size, row));
}

void ArchetypeChunk::DestroyAllColumns(std::size_t row) {
    for (auto& [type, column] : m_columns) {
        column.typeInfo.destroy(Offset(column.buffer, column.typeInfo.size, row));
    }
}

Entity ArchetypeChunk::VacateRow(std::size_t row) {
    UVE_ASSERT(row < m_count);
    if (row >= m_count) {
        UVE_ERROR("ArchetypeChunk: VacateRow received an out-of-range row ({} >= {})", row, m_count);
        return kInvalidEntity;
    }
    const std::size_t lastRow = m_count - 1;

    Entity movedEntity = kInvalidEntity;
    if (row != lastRow) {
        for (auto& [type, column] : m_columns) {
            void* const destination = Offset(column.buffer, column.typeInfo.size, row);
            void* const source = Offset(column.buffer, column.typeInfo.size, lastRow);
            column.typeInfo.moveConstructAndDestroySource(destination, source);
        }
        movedEntity = m_entitiesInChunk[lastRow];
        m_entitiesInChunk[row] = movedEntity;
    }
    m_entitiesInChunk[lastRow] = kInvalidEntity;
    --m_count;
    return movedEntity;
}

void* ArchetypeChunk::GetComponentPointer(std::type_index componentType, std::size_t row) const {
    // Defined in terms of the column view, so the hoisted and per-row paths cannot disagree about
    // where a row lives.
    return GetColumnView(componentType).At(row);
}

ArchetypeChunk::ColumnView ArchetypeChunk::GetColumnView(std::type_index componentType) const {
    const Column& column = GetColumn(componentType);
    return ColumnView{column.buffer, column.typeInfo.size};
}

Entity ArchetypeChunk::GetEntityAtRow(std::size_t row) const {
    UVE_ASSERT(row < m_count);
    if (row >= m_count) {
        UVE_ERROR("ArchetypeChunk: GetEntityAtRow received an out-of-range row ({} >= {})", row, m_count);
        return kInvalidEntity;
    }
    return m_entitiesInChunk[row];
}

void ArchetypeChunk::MoveComponentInto(std::type_index componentType, std::size_t sourceRow, ArchetypeChunk& destination,
                                     std::size_t destinationRow) const {
    void* const sourceSlot = GetComponentPointer(componentType, sourceRow);
    void* const destinationSlot = destination.GetComponentPointer(componentType, destinationRow);
    const Column& column = GetColumn(componentType);
    column.typeInfo.moveConstructAndDestroySource(destinationSlot, sourceSlot);
}

} // namespace UVE::Scene::Detail
