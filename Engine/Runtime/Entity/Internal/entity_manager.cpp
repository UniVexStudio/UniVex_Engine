// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "entity_manager.h"

#include <cstdint>
#include <unordered_map>
#include <utility>

#include "archetype_signature.h"
#include "archetype.h"
#include "archetype_chunk.h"
#include "engine_assert.h"
#include "entity_lifecycle_events.h"

namespace UVE::Scene {

namespace {

/// Where one alive entity currently lives: which archetype, which of its chunks, which row.
struct EntityRecord {
    std::uint32_t generation = 0;
    bool alive = false;
    bool destroying = false;
    Detail::Archetype* archetype = nullptr;
    std::size_t chunkIndex = 0;
    std::size_t row = 0;
};

} // namespace

struct EntityManager::Impl {
    Impl(Memory::IAllocator& allocatorIn, Events::IEventSystem& eventSystemIn)
        : allocator(allocatorIn), eventSystem(eventSystemIn) {}

    Memory::IAllocator& allocator;
    Events::IEventSystem& eventSystem;
    std::unordered_map<std::type_index, ComponentTypeInfo> typeInfos;
    std::vector<EntityRecord> records;
    std::vector<std::uint32_t> freeIndices;
    std::unordered_map<Detail::ArchetypeSignature, std::unique_ptr<Detail::Archetype>> archetypes;

    [[nodiscard]] Detail::Archetype& FindOrCreateArchetype(const Detail::ArchetypeSignature& signature) {
        const auto archetypeIt = archetypes.find(signature);
        if (archetypeIt != archetypes.end()) {
            return *archetypeIt->second;
        }
        auto archetype = std::make_unique<Detail::Archetype>(signature, allocator, typeInfos);
        Detail::Archetype& archetypeRef = *archetype;
        archetypes.emplace(signature, std::move(archetype));
        return archetypeRef;
    }
};

EntityManager::EntityManager(Memory::IAllocator& allocator, Events::IEventSystem& eventSystem)
    : m_impl(std::make_unique<Impl>(allocator, eventSystem)) {}

EntityManager::~EntityManager() = default;

Entity EntityManager::CreateEntity() {
    std::uint32_t index;
    if (!m_impl->freeIndices.empty()) {
        index = m_impl->freeIndices.back();
        m_impl->freeIndices.pop_back();
    } else {
        index = static_cast<std::uint32_t>(m_impl->records.size());
        m_impl->records.emplace_back();
    }

    EntityRecord& record = m_impl->records[index];
    record.alive = true;
    const Entity entity{index, record.generation};

    Detail::Archetype& emptyArchetype = m_impl->FindOrCreateArchetype(Detail::ArchetypeSignature{});
    const Detail::Archetype::Location location = emptyArchetype.ReserveEntity(entity);
    record.archetype = &emptyArchetype;
    record.chunkIndex = location.chunkIndex;
    record.row = location.row;

    m_impl->eventSystem.Publish(EntityCreatedEvent{entity});
    return entity;
}

void EntityManager::DestroyEntity(Entity entity) {
    if (!IsAlive(entity)) {
        UVE_ASSERT(IsAlive(entity));
        return;
    }
    if (m_impl->records[entity.index].destroying) {
        return;
    }
    m_impl->records[entity.index].destroying = true;

    m_impl->eventSystem.Publish(EntityDestroyedEvent{entity});

    if (!IsAlive(entity)) {
        return;
    }
    EntityRecord& record = m_impl->records[entity.index];
    const Detail::Archetype::Location location{record.chunkIndex, record.row};
    const Entity movedEntity = record.archetype->DestroyEntityAt(location);
    if (movedEntity != kInvalidEntity) {
        m_impl->records[movedEntity.index].row = record.row;
    }

    record.alive = false;
    record.destroying = false;
    record.archetype = nullptr;
    ++record.generation;
    m_impl->freeIndices.push_back(entity.index);
}

bool EntityManager::IsAlive(Entity entity) const noexcept {
    if (entity.index >= m_impl->records.size()) {
        return false;
    }
    const EntityRecord& record = m_impl->records[entity.index];
    return record.alive && record.generation == entity.generation;
}

std::size_t EntityManager::GetEntityCount() const noexcept {
    std::size_t count = 0;
    for (const EntityRecord& record : m_impl->records) {
        if (record.alive) {
            ++count;
        }
    }
    return count;
}

std::vector<std::type_index> EntityManager::GetComponentTypes(Entity entity) const {
    if (!IsAlive(entity)) {
        UVE_ASSERT(IsAlive(entity));
        return {};
    }
    return m_impl->records[entity.index].archetype->GetSignature().GetTypes();
}

void* EntityManager::AddComponentErased(Entity entity, std::type_index componentType,
                                            const ComponentTypeInfo& typeInfo) {
    UVE_ASSERT(IsAlive(entity));
    EntityRecord& record = m_impl->records[entity.index];
    Detail::Archetype& oldArchetype = *record.archetype;
    UVE_ASSERT(!oldArchetype.GetSignature().Contains(componentType));

    m_impl->typeInfos.emplace(componentType, typeInfo);

    const Detail::ArchetypeSignature newSignature = oldArchetype.GetSignature().With(componentType);
    Detail::Archetype& newArchetype = m_impl->FindOrCreateArchetype(newSignature);

    const Detail::Archetype::Location oldLocation{record.chunkIndex, record.row};
    const Detail::Archetype::Location newLocation = newArchetype.ReserveEntity(entity);
    Detail::ArchetypeChunk& oldChunk = oldArchetype.GetChunk(oldLocation.chunkIndex);
    Detail::ArchetypeChunk& newChunk = newArchetype.GetChunk(newLocation.chunkIndex);

    for (const std::type_index& type : oldArchetype.GetSignature().GetTypes()) {
        oldChunk.MoveComponentInto(type, oldLocation.row, newChunk, newLocation.row);
    }

    const Entity movedEntity = oldArchetype.VacateWithoutDestroy(oldLocation);
    if (movedEntity != kInvalidEntity) {
        m_impl->records[movedEntity.index].row = oldLocation.row;
    }

    record.archetype = &newArchetype;
    record.chunkIndex = newLocation.chunkIndex;
    record.row = newLocation.row;

    return newChunk.GetComponentPointer(componentType, newLocation.row);
}

void EntityManager::RemoveComponentErased(Entity entity, std::type_index componentType) {
    if (!IsAlive(entity)) {
        UVE_ASSERT(IsAlive(entity));
        return;
    }
    EntityRecord& record = m_impl->records[entity.index];
    Detail::Archetype& oldArchetype = *record.archetype;
    if (!oldArchetype.GetSignature().Contains(componentType)) {
        UVE_ASSERT(oldArchetype.GetSignature().Contains(componentType));
        return;
    }

    const Detail::ArchetypeSignature newSignature = oldArchetype.GetSignature().Without(componentType);
    Detail::Archetype& newArchetype = m_impl->FindOrCreateArchetype(newSignature);

    const Detail::Archetype::Location oldLocation{record.chunkIndex, record.row};
    const Detail::Archetype::Location newLocation = newArchetype.ReserveEntity(entity);
    Detail::ArchetypeChunk& oldChunk = oldArchetype.GetChunk(oldLocation.chunkIndex);
    Detail::ArchetypeChunk& newChunk = newArchetype.GetChunk(newLocation.chunkIndex);

    for (const std::type_index& type : newSignature.GetTypes()) {
        oldChunk.MoveComponentInto(type, oldLocation.row, newChunk, newLocation.row);
    }
    oldChunk.DestroyComponent(componentType, oldLocation.row);

    const Entity movedEntity = oldArchetype.VacateWithoutDestroy(oldLocation);
    if (movedEntity != kInvalidEntity) {
        m_impl->records[movedEntity.index].row = oldLocation.row;
    }

    record.archetype = &newArchetype;
    record.chunkIndex = newLocation.chunkIndex;
    record.row = newLocation.row;
}

bool EntityManager::HasComponentErased(Entity entity, std::type_index componentType) const {
    if (!IsAlive(entity)) {
        UVE_ASSERT(IsAlive(entity));
        return false;
    }
    return m_impl->records[entity.index].archetype->GetSignature().Contains(componentType);
}

void* EntityManager::GetComponentErased(Entity entity, std::type_index componentType) {
    UVE_ASSERT(IsAlive(entity));
    const EntityRecord& record = m_impl->records[entity.index];
    UVE_ASSERT(record.archetype->GetSignature().Contains(componentType));
    return record.archetype->GetChunk(record.chunkIndex).GetComponentPointer(componentType, record.row);
}

void EntityManager::ForEachErased(
    const std::vector<std::type_index>& componentTypes,
    const std::function<void(Entity, const std::vector<void*>&)>& callback) {
    const Detail::ArchetypeSignature requested(componentTypes);

    // Two buffers reused across every chunk of every archetype, rather than one heap allocation
    // per entity. The previous shape allocated a std::vector<void*> and did one hash lookup per
    // requested component type FOR EVERY ROW, even though a chunk's column layout is fixed for its
    // whole lifetime - measured at roughly 9.7x the cost of hoisting that work out of the row
    // loop. This is the walk 22 call sites across 13 systems pay every frame, so it is not only
    // the renderer that was paying it.
    //
    // Declared outside the archetype loop so their capacity survives from one chunk to the next;
    // the callback only ever sees `pointers`, whose size and order are unchanged.
    std::vector<Detail::ArchetypeChunk::ColumnView> columnViews;
    std::vector<void*> pointers;
    columnViews.reserve(componentTypes.size());
    pointers.resize(componentTypes.size());

    for (auto& [signature, archetype] : m_impl->archetypes) {
        if (!archetype->GetSignature().IsSupersetOf(requested)) {
            continue;
        }
        archetype->ForEachChunk([&componentTypes, &callback, &columnViews, &pointers](
                                       Detail::ArchetypeChunk& chunk, std::size_t count) {
            // Resolved ONCE per chunk. This is the whole optimization: the hash lookups move from
            // per-row to per-chunk, and with kChunkCapacity at 512 that is up to 512 rows
            // sharing one resolution.
            columnViews.clear();
            for (const std::type_index& type : componentTypes) {
                columnViews.push_back(chunk.GetColumnView(type));
            }

            for (std::size_t row = 0; row < count; ++row) {
                for (std::size_t index = 0U; index < columnViews.size(); ++index) {
                    pointers[index] = columnViews[index].At(row);
                }
                callback(chunk.GetEntityAtRow(row), pointers);
            }
        });
    }
}

} // namespace UVE::Scene
