// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include "object.h"
#include "entity_manager.h"
#include "scene_graph.h"
#include "event_system.h"

namespace UVE::World {
class World;
using SceneHandle = Core::ObjectHandle;

/// One owned runtime scene, not a new file format or another ECS. World owns it; its entity
/// storage, graph and local events reuse the existing implementations. Entity handles are
/// meaningful only in this scene. Secondary scenes have separate event buses so their equal
/// numeric Entity ids cannot accidentally notify primary-scene listeners.
/// GetEntityManager supports existing SceneSerializer/PrefabSystem loaders unchanged.
/// Borrowed references expire at unload. Owner-thread only; no concurrent ticking/destruction.
class Scene final : public Core::Object {
  public:
    [[nodiscard]] std::string_view GetName() const noexcept { return m_name; }
    [[nodiscard]] UVE::Scene::IEntityManager &GetEntityManager() noexcept { return m_entities; }
    [[nodiscard]] const UVE::Scene::IEntityManager &GetEntityManager() const noexcept { return m_entities; }
    [[nodiscard]] UVE::Scene::SceneGraph &GetSceneGraph() noexcept { return m_graph; }
    [[nodiscard]] Events::IEventSystem &GetEventSystem() noexcept { return m_events; }
    [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }
    [[nodiscard]] std::uint64_t GetFrameCount() const noexcept { return m_frameCount; }

  private:
    friend class World;
    Scene(std::string name, Memory::IAllocator &allocator, Events::IEventSystem *primaryEvents);
    void ClearEntities();
    std::string m_name;
    Events::EventSystem m_localEvents;
    Events::IEventSystem &m_events;
    UVE::Scene::EntityManager m_entities;
    UVE::Scene::SceneGraph m_graph;
    bool m_enabled = true;
    std::uint64_t m_frameCount = 0U;
    std::size_t m_startedSystems = 0U;
};
} // namespace UVE::World
