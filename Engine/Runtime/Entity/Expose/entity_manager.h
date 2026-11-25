// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <memory>
#include <typeindex>
#include <vector>

#include "i_event_system.h"
#include "i_allocator.h"
#include "i_entity_manager.h"

namespace UVE::Scene {

/// EntityManager is the concrete, engine-standard implementation of IEntityManager: an
/// archetype ECS with chunk-based component storage (see Archetype/ArchetypeChunk in
/// engine/scene/src/ — private implementation details hidden entirely behind this PIMPL, the
/// same "no internals in the public header" idiom ConfigManager established; here it's
/// motivated by avoiding incomplete-type risk in a public header rather than hiding a
/// third-party dependency, but the mechanism is identical).
/// Thread-safety: not thread-safe — every method (including ForEach, inherited from
/// IEntityManager) must be called only from the main/scene thread, matching EngineCore's
/// own single-threaded contract.
class EntityManager final : public IEntityManager {
public:
    /// `allocator` backs every chunk's component storage (see ArchetypeChunk); `eventSystem` receives
    /// EntityCreatedEvent/EntityDestroyedEvent publications. Both references must outlive
    /// this EntityManager.
    EntityManager(Memory::IAllocator& allocator, Events::IEventSystem& eventSystem);
    ~EntityManager() override;

    EntityManager(const EntityManager&) = delete;
    EntityManager& operator=(const EntityManager&) = delete;

    [[nodiscard]] Entity CreateEntity() override;
    void DestroyEntity(Entity entity) override;
    [[nodiscard]] bool IsAlive(Entity entity) const noexcept override;
    [[nodiscard]] std::size_t GetEntityCount() const noexcept override;
    [[nodiscard]] std::vector<std::type_index> GetComponentTypes(Entity entity) const override;

protected:
    [[nodiscard]] void* AddComponentErased(Entity entity, std::type_index componentType,
                                            const ComponentTypeInfo& typeInfo) override;
    void RemoveComponentErased(Entity entity, std::type_index componentType) override;
    [[nodiscard]] bool HasComponentErased(Entity entity, std::type_index componentType) const override;
    [[nodiscard]] void* GetComponentErased(Entity entity, std::type_index componentType) override;
    void ForEachErased(const std::vector<std::type_index>& componentTypes,
                        const std::function<void(Entity, const std::vector<void*>&)>& callback) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Scene
