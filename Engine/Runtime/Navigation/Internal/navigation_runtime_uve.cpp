// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/navigation/navigation_runtime_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/nav_mesh_volume_3d_uve.h"
#include "uve/objects/3d/nav_seeker_3d_uve.h"
#include "uve/physics/i_raycast_system_uve.h"

namespace UVE::Navigation {

namespace {

/// The mesh an agent gets when there is no region under it: empty, so the search answers NoMesh and
/// the steering reports Failed instead of the agent keeping a route from wherever it used to be.
const NavmeshUVE kNoMeshUVE{};

[[nodiscard]] NavmeshBakeSettingsUVE SettingsFromRegionUVE(const Scene::NavMeshVolume3DComponentUVE& region) noexcept {
    NavmeshBakeSettingsUVE settings{};
    settings.cellSize = region.cellSize;
    settings.agentRadius = region.agentRadius;
    settings.agentHeight = region.agentHeight;
    settings.maximumSlopeDegrees = region.maximumSlopeDegrees;
    settings.maximumStepHeight = region.maximumStepHeight;
    settings.navigationLayers = region.navigationLayers;
    // The other fields keep the bake's own defaults: what the rasterization samples, how loosely
    // cells merge and the cell budget are engine policy, not per-region authoring, and none of them
    // changes what "walkable for this agent" means.
    return settings;
}

[[nodiscard]] bool SameBakeSettingsUVE(const NavmeshBakeSettingsUVE& left,
                                       const NavmeshBakeSettingsUVE& right) noexcept {
    return left.cellSize == right.cellSize && left.agentRadius == right.agentRadius &&
           left.agentHeight == right.agentHeight && left.maximumSlopeDegrees == right.maximumSlopeDegrees &&
           left.maximumStepHeight == right.maximumStepHeight &&
           left.mergeHeightToleranceMetres == right.mergeHeightToleranceMetres &&
           left.navigationLayers == right.navigationLayers && left.queryLayerMask == right.queryLayerMask &&
           left.maximumCells == right.maximumCells;
}

[[nodiscard]] bool SameBoundsUVE(const Math::AabbUVE& left, const Math::AabbUVE& right) noexcept {
    return left.min.x == right.min.x && left.min.y == right.min.y && left.min.z == right.min.z &&
           left.max.x == right.max.x && left.max.y == right.max.y && left.max.z == right.max.z;
}

[[nodiscard]] NavAgentSettingsUVE SettingsFromSeekerUVE(const Scene::NavSeeker3DComponentUVE& seeker) noexcept {
    NavAgentSettingsUVE settings{};
    settings.radius = seeker.radius;
    settings.height = seeker.height;
    settings.maxSpeed = seeker.maxSpeed;
    settings.accelerationMetresPerSecondSquared = seeker.acceleration;
    settings.pathUpdateInterval = seeker.pathUpdateInterval;
    settings.waypointRadiusMetres = seeker.waypointRadius;
    settings.targetToleranceMetres = seeker.targetTolerance;
    settings.slowDownRadiusMetres = seeker.slowDownRadius;
    settings.navigationLayers = seeker.navigationLayers;
    // Avoidance off is a zero radius, which is the steering's own way of saying "no push": the flag
    // and the number never disagree about whether this agent steps aside.
    settings.avoidanceRadiusMetres = seeker.avoidanceEnabled ? seeker.avoidanceRadius : 0.0F;
    return settings;
}

[[nodiscard]] Scene::NavigationAgentPathStatusUVE StatusFromAgentUVE(const NavAgentStatusUVE status) noexcept {
    switch (status) {
    case NavAgentStatusUVE::Following:
        return Scene::NavigationAgentPathStatusUVE::Following;
    case NavAgentStatusUVE::Finished:
        return Scene::NavigationAgentPathStatusUVE::Finished;
    case NavAgentStatusUVE::Failed:
        return Scene::NavigationAgentPathStatusUVE::Failed;
    case NavAgentStatusUVE::Idle:
        break;
    }
    return Scene::NavigationAgentPathStatusUVE::Idle;
}

/// Distance from `point` to a box, zero when inside it. The diagonal is deliberate: an agent above a
/// region whose floor is lower than the box's top is off the mesh by that much of its own height.
[[nodiscard]] float DistanceToBoundsUVE(const Math::AabbUVE& bounds, const Math::Vector3UVE& point) noexcept {
    const float deltaX = std::max(std::max(bounds.min.x - point.x, point.x - bounds.max.x), 0.0F);
    const float deltaY = std::max(std::max(bounds.min.y - point.y, point.y - bounds.max.y), 0.0F);
    const float deltaZ = std::max(std::max(bounds.min.z - point.z, point.z - bounds.max.z), 0.0F);
    return std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
}

/// What a disabled agent publishes: nothing, said explicitly. A consumer that reads the component's
/// velocity without checking `enabled` sees a stopped agent, never last step's motion.
void PublishIdleUVE(Scene::NavSeeker3DComponentUVE& seeker, const Math::Vector3UVE& position) noexcept {
    seeker.desiredVelocity = Math::Vector3UVE{};
    seeker.nextPathPosition = position;
    seeker.pathStatus = Scene::NavigationAgentPathStatusUVE::Idle;
    seeker.pathChanged = false;
    seeker.targetReached = false;
}

} // namespace

const NavmeshUVE* NavigationRuntimeUVE::FindRegionMeshUVE(const Scene::EntityUVE region) const noexcept {
    const auto found = m_regions.find(region);
    if (found == m_regions.end() || !found->second.baked) {
        return nullptr;
    }
    return &found->second.mesh;
}

const NavmeshBakeReportUVE* NavigationRuntimeUVE::FindRegionBakeReportUVE(const Scene::EntityUVE region) const noexcept {
    const auto found = m_regions.find(region);
    if (found == m_regions.end() || !found->second.baked) {
        return nullptr;
    }
    return &found->second.report;
}

Scene::EntityUVE NavigationRuntimeUVE::FindRegionForPositionUVE(const Math::Vector3UVE& position,
                                                               const float maximumDistanceMetres) const noexcept {
    Scene::EntityUVE nearest = Scene::kInvalidEntityUVE;
    float nearestDistance = maximumDistanceMetres;
    for (const Scene::EntityUVE region : m_regionOrder) {
        const auto found = m_regions.find(region);
        if (found == m_regions.end() || found->second.mesh.polygons.empty()) {
            // A region that baked to nothing, or one whose mesh is gone, is not somewhere an agent
            // can walk - however close it is.
            continue;
        }
        const float distance = DistanceToBoundsUVE(found->second.mesh.bounds, position);
        if (distance <= 0.0F) {
            // Inside a region's own volume. First one wins, in the order regions were seen, so
            // overlapping volumes resolve the same way every run.
            return region;
        }
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = region;
        }
    }
    return nearest;
}

void NavigationRuntimeUVE::ClearUVE() noexcept {
    m_regions.clear();
    m_regionOrder.clear();
    m_agents.clear();
}

NavigationSyncReportUVE NavigationRuntimeUVE::SyncUVE(Scene::IEntityManagerUVE& entityManager,
                                                      const Physics::IRaycastSystemUVE& raycasts,
                                                      const std::span<const Scene::EntityUVE> agentsInStepOrder,
                                                      const float deltaTimeSeconds) {
    NavigationSyncReportUVE report{};

    // ------------------------------------------------------------------ regions
    std::vector<Scene::EntityUVE> liveRegions;
    entityManager.ForEachUVE<Scene::NavMeshVolume3DComponentUVE>(
        [this, &entityManager, &raycasts, &report, &liveRegions](
            const Scene::EntityUVE entity, Scene::NavMeshVolume3DComponentUVE& region) {
            if (!region.enabled) {
                // A region switched off has no mesh: an agent standing on it must fail rather than
                // walk the ground the author just took away. Its cache is dropped, not kept warm, so
                // no later step can reach a mesh the author believes is gone.
                m_regions.erase(entity);
                return;
            }
            if (!entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
                // No pose, no volume: a bake needs somewhere in the world to rasterize. The region
                // keeps whatever mesh it had, because nothing about it changed.
                return;
            }
            ++report.regions;
            liveRegions.push_back(entity);

            const Scene::WorldTransformComponentUVE& world =
                entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
            const Math::AabbUVE bounds = Math::AabbUVE::FromCenterExtentsUVE(world.worldPosition, region.boundsHalfExtents);
            const NavmeshBakeSettingsUVE settings = SettingsFromRegionUVE(region);

            RegionEntryUVE& entry = m_regions[entity];
            const bool inputsChanged = !entry.baked || !SameBakeSettingsUVE(entry.bakedSettings, settings) ||
                                       !SameBoundsUVE(entry.bakedBounds, bounds);
            // An explicit request is an author saying the collision world moved under a region whose
            // own volume did not, so it is honoured even when every input this class can see is
            // identical - that is the whole reason the field exists.
            if (!inputsChanged && !region.rebuildRequested) {
                return;
            }
            entry.report = BakeNavmeshUVE(entityManager, raycasts, settings, bounds, entry.mesh);
            entry.bakedSettings = settings;
            entry.bakedBounds = bounds;
            entry.baked = true;
            region.rebuildRequested = false;
            ++report.bakes;
            if (!entry.report.bakePossible) {
                ++report.bakeRefusals;
            }
            // The volume is axis-aligned in world space: the bake's grid is built on the world axes,
            // so a rotated region is rasterized as its unrotated volume about its own world position
            // rather than silently baking a skewed grid.
        });

    // A region entity that is gone, or that lost the component, must not serve meshes to the agents
    // that used it. Pruning after the walk keeps the region order the walk produced.
    m_regionOrder = std::move(liveRegions);
    for (auto it = m_regions.begin(); it != m_regions.end();) {
        const bool live = std::find(m_regionOrder.begin(), m_regionOrder.end(), it->first) != m_regionOrder.end();
        it = live ? std::next(it) : m_regions.erase(it);
    }

    // ------------------------------------------------------------------ agents
    for (auto it = m_agents.begin(); it != m_agents.end();) {
        const bool live = entityManager.IsAliveUVE(it->first) &&
                          entityManager.HasComponentUVE<Scene::NavSeeker3DComponentUVE>(it->first);
        it = live ? std::next(it) : m_agents.erase(it);
    }

    // The neighbour list is built once for the whole step: every listed agent that has somewhere to
    // stand contributes its position and width, and each agent's own entry is recognised and ignored
    // by the steering (two agents cannot be at the same point unless they already overlap).
    std::vector<NavAgentObstacleUVE> neighbours;
    neighbours.reserve(agentsInStepOrder.size());
    for (const Scene::EntityUVE entity : agentsInStepOrder) {
        if (!entityManager.HasComponentUVE<Scene::NavSeeker3DComponentUVE>(entity) ||
            !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
            continue;
        }
        const Scene::NavSeeker3DComponentUVE& seeker = entityManager.GetComponentUVE<Scene::NavSeeker3DComponentUVE>(entity);
        const Scene::WorldTransformComponentUVE& world =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
        neighbours.push_back(NavAgentObstacleUVE{world.worldPosition, seeker.radius});
    }

    for (const Scene::EntityUVE entity : agentsInStepOrder) {
        if (!entityManager.IsAliveUVE(entity) ||
            !entityManager.HasComponentUVE<Scene::NavSeeker3DComponentUVE>(entity)) {
            continue;
        }
        Scene::NavSeeker3DComponentUVE& seeker = entityManager.GetComponentUVE<Scene::NavSeeker3DComponentUVE>(entity);
        if (!seeker.enabled) {
            // Switched off: the cached steering is dropped so a later enable starts from a clean
            // search, and the component is told plainly that nothing is being published.
            m_agents.erase(entity);
            const Math::Vector3UVE position =
                entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)
                    ? entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity).worldPosition
                    : Math::Vector3UVE{};
            PublishIdleUVE(seeker, position);
            continue;
        }
        if (!entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
            // A body with nowhere to be cannot be steered, and this pass never adds the component it
            // would need: leave the entity exactly as authored.
            continue;
        }

        const Math::Vector3UVE position =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity).worldPosition;
        const NavAgentSettingsUVE settings = SettingsFromSeekerUVE(seeker);
        const Scene::EntityUVE region = FindRegionForPositionUVE(position, settings.offMeshToleranceMetres);
        const NavmeshUVE* mesh = region == Scene::kInvalidEntityUVE ? &kNoMeshUVE : FindRegionMeshUVE(region);
        if (mesh == nullptr) {
            mesh = &kNoMeshUVE;
        }
        if (region == Scene::kInvalidEntityUVE) {
            ++report.agentsWithoutMesh;
        }

        NavAgentUVE& agent = m_agents[entity];
        agent.ConfigureUVE(settings);
        agent.SetPositionUVE(position);
        agent.SetTargetUVE(seeker.targetPosition);
        agent.StepUVE(*mesh, neighbours, deltaTimeSeconds);

        seeker.nextPathPosition = agent.GetNextPathPositionUVE();
        seeker.desiredVelocity = agent.GetDesiredVelocityUVE();
        seeker.pathStatus = StatusFromAgentUVE(agent.GetStatusUVE());
        seeker.pathChanged = agent.GetPathChangedUVE();
        seeker.targetReached = agent.HasReachedTargetUVE();
        ++report.agents;
    }

    return report;
}

} // namespace UVE::Navigation
