// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/world/scene_uve.h"
#include <algorithm>
#include <utility>
#include <vector>
namespace UVE::World {
SceneUVE::SceneUVE(std::string name, Memory::IAllocatorUVE &allocator, Events::IEventSystemUVE *primaryEvents)
    : m_name(std::move(name)), m_events(primaryEvents == nullptr ? m_localEvents : *primaryEvents),
      m_entities(allocator, m_events) {}
void SceneUVE::ClearEntitiesUVE() {
    // Snapshot before structural edits, including component-less entities. EntityManager stores
    // those in its empty archetype. Sort to avoid hash/archetype order leaking into destruction.
    std::vector<Scene::EntityUVE> entities;
    entities.reserve(m_entities.GetEntityCountUVE());
    m_entities.ForEachUVE<>([&](const Scene::EntityUVE entity) { entities.push_back(entity); });
    std::sort(entities.begin(), entities.end(), [](const auto a, const auto b) { return a.index < b.index; });
    for (const auto entity : entities)
        if (m_entities.IsAliveUVE(entity))
            m_entities.DestroyEntityUVE(entity);
    m_graph.UpdateUVE(m_entities);
    m_graph.RebuildChildrenIndexUVE(m_entities);
}
} // namespace UVE::World
