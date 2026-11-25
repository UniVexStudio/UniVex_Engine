// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "scene.h"
#include <algorithm>
#include <utility>
#include <vector>
namespace UVE::World {
Scene::Scene(std::string name, Memory::IAllocator &allocator, Events::IEventSystem *primaryEvents)
    : m_name(std::move(name)), m_events(primaryEvents == nullptr ? m_localEvents : *primaryEvents),
      m_entities(allocator, m_events) {}
void Scene::ClearEntities() {
    // Snapshot before structural edits, including component-less entities. EntityManager stores
    // those in its empty archetype. Sort to avoid hash/archetype order leaking into destruction.
    std::vector<UVE::Scene::Entity> entities;
    entities.reserve(m_entities.GetEntityCount());
    m_entities.ForEach<>([&](const UVE::Scene::Entity entity) { entities.push_back(entity); });
    std::sort(entities.begin(), entities.end(), [](const auto a, const auto b) { return a.index < b.index; });
    for (const auto entity : entities)
        if (m_entities.IsAlive(entity))
            m_entities.DestroyEntity(entity);
    m_graph.Update(m_entities);
    m_graph.RebuildChildrenIndex(m_entities);
}
} // namespace UVE::World
