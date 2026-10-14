// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include "uve/object/object_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/events/event_system_uve.h"

namespace UVE::World {
class WorldUVE;
using SceneHandleUVE = Core::ObjectHandleUVE;

/// One owned runtime scene, not a new file format or another ECS. WorldUVE owns it; its entity
/// storage, graph and local events reuse the existing implementations. Entity handles are
/// meaningful only in this scene. Secondary scenes have separate event buses so their equal
/// numeric EntityUVE ids cannot accidentally notify primary-scene listeners.
/// GetEntityManagerUVE supports existing SceneSerializerUVE/PrefabSystemUVE loaders unchanged.
/// Borrowed references expire at unload. Owner-thread only; no concurrent ticking/destruction.
class SceneUVE final : public Core::ObjectUVE {
  public:
    [[nodiscard]] std::string_view GetNameUVE() const noexcept { return m_name; }
    [[nodiscard]] Scene::IEntityManagerUVE &GetEntityManagerUVE() noexcept { return m_entities; }
    [[nodiscard]] const Scene::IEntityManagerUVE &GetEntityManagerUVE() const noexcept { return m_entities; }
    [[nodiscard]] Scene::SceneGraphUVE &GetSceneGraphUVE() noexcept { return m_graph; }
    [[nodiscard]] Events::IEventSystemUVE &GetEventSystemUVE() noexcept { return m_events; }
    [[nodiscard]] bool IsEnabledUVE() const noexcept { return m_enabled; }
    [[nodiscard]] std::uint64_t GetFrameCountUVE() const noexcept { return m_frameCount; }

  private:
    friend class WorldUVE;
    SceneUVE(std::string name, Memory::IAllocatorUVE &allocator, Events::IEventSystemUVE *primaryEvents);
    void ClearEntitiesUVE();
    std::string m_name;
    Events::EventSystemUVE m_localEvents;
    Events::IEventSystemUVE &m_events;
    Scene::EntityManagerUVE m_entities;
    Scene::SceneGraphUVE m_graph;
    bool m_enabled = true;
    std::uint64_t m_frameCount = 0U;
    std::size_t m_startedSystems = 0U;
};
} // namespace UVE::World
