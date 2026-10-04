// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <span>
#include <unordered_map>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/navigation/navmesh_agent_uve.h"
#include "uve/navigation/navmesh_bake_uve.h"
#include "uve/navigation/navmesh_uve.h"

namespace UVE::Scene {
class IEntityManagerUVE;
}

namespace UVE::Physics {
class IRaycastSystemUVE;
}

namespace UVE::Navigation {

/// What one navigation step did, as counters. A caller that only wants the world driven ignores it;
/// a test pins it, and the engine logs a bake when one happens so a level that re-rasterizes every
/// frame is visible instead of merely expensive.
struct NavigationSyncReportUVE final {
    /// Live, enabled regions this step read.
    std::size_t regions = 0U;
    /// Regions rasterized this step: the first time each was seen, after it moved or was resized,
    /// or because `rebuildRequested` asked for it.
    std::size_t bakes = 0U;
    /// Regions whose volume or settings could not describe a bake at all. They keep an empty mesh -
    /// a fact about the region, not an error - and are not retried until something changes.
    std::size_t bakeRefusals = 0U;
    /// Agents the caller listed that this step stepped.
    std::size_t agents = 0U;
    /// Agents with no region under them: their steering publishes nothing and reports Failed.
    std::size_t agentsWithoutMesh = 0U;
};

/// The navigation half of the ECS: it bakes each region into a mesh once and keeps it, then steps
/// every agent the caller lists against the mesh under it.
///
/// The runtime is deliberately not a monolith - it holds no entity types of its own and no frame
/// decisions of its own. The engine core decides WHEN the step runs (the fixed step, in the same
/// physicsPriority order every other mover is stepped in) and which entities participate; this class
/// decides what a step means: which region an agent is standing on, when a region is baked and
/// re-baked, and what the agent publishes back into its component. That is the same split the
/// hitbox, interaction-area and level-streamer seams use, and it is what makes all of this testable
/// without standing up an EngineCoreUVE.
///
/// Region meshes are cached by entity rather than rebuilt per step or per query: a navmesh is a
/// property of the level, and rasterizing one is hundreds of rays. A region is re-baked only when
/// its volume, its bake settings or its layers change, or when it asks. Entities that no longer
/// exist - or no longer hold the component - drop their cache on the next step.
class NavigationRuntimeUVE final {
public:
    /// Bakes whatever needs baking, then steps `agentsInStepOrder` in the order given.
    ///
    /// `agentsInStepOrder` is handed in rather than collected here because the order is a frame
    /// decision, not a navigation one: the engine core already orders fixed-step work by
    /// ProcessComponentUVE::physicsPriority and already knows which entities tick.
    [[nodiscard]] NavigationSyncReportUVE SyncUVE(Scene::IEntityManagerUVE& entityManager,
                                                  const Physics::IRaycastSystemUVE& raycasts,
                                                  std::span<const Scene::EntityUVE> agentsInStepOrder,
                                                  float deltaTimeSeconds);

    /// The mesh a region baked, or nullptr while it has none: not seen yet, switched off, or pruned.
    [[nodiscard]] const NavmeshUVE* FindRegionMeshUVE(Scene::EntityUVE region) const noexcept;
    [[nodiscard]] const NavmeshBakeReportUVE* FindRegionBakeReportUVE(Scene::EntityUVE region) const noexcept;

    /// The region a point should be navigated on: the first region whose volume contains it, or the
    /// nearest one within `maximumDistanceMetres`. Regions are considered in the order they were
    /// seen, which is the entity manager's own deterministic iteration order, so the answer does not
    /// depend on hash order.
    [[nodiscard]] Scene::EntityUVE FindRegionForPositionUVE(const Math::Vector3UVE& position,
                                                            float maximumDistanceMetres) const noexcept;

    [[nodiscard]] std::size_t GetRegionCountUVE() const noexcept { return m_regionOrder.size(); }
    [[nodiscard]] std::size_t GetAgentCountUVE() const noexcept { return m_agents.size(); }

    /// Drops every cached mesh and every agent, as a scene teardown needs.
    void ClearUVE() noexcept;

private:
    /// One region's bake, and the inputs it was baked from - kept so the next step can tell "nothing
    /// changed" from "bake me again" without re-deriving the mesh.
    struct RegionEntryUVE final {
        NavmeshUVE mesh{};
        NavmeshBakeReportUVE report{};
        NavmeshBakeSettingsUVE bakedSettings{};
        Math::AabbUVE bakedBounds{};
        bool baked = false;
    };

    std::unordered_map<Scene::EntityUVE, RegionEntryUVE> m_regions;
    /// Region entities in the order they were first seen. Deterministic iteration for the queries
    /// above, which an unordered_map cannot promise.
    std::vector<Scene::EntityUVE> m_regionOrder;
    std::unordered_map<Scene::EntityUVE, NavAgentUVE> m_agents;
};

} // namespace UVE::Navigation
