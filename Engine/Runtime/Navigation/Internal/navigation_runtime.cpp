// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "navigation_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "nav_mesh_volume_3d.h"
#include "nav_seeker_3d.h"
#include "i_raycast_system.h"

namespace UVE::Navigation {

namespace {

/// The mesh an agent gets when there is no region under it: empty, so the search answers NoMesh and
/// the steering reports Failed instead of the agent keeping a route from wherever it used to be.
const Navmesh kNoMesh{};

[[nodiscard]] NavmeshBakeSettings SettingsFromRegion(const Scene::NavMeshVolume3DComponent& region) noexcept {
    NavmeshBakeSettings settings{};
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

[[nodiscard]] bool SameBakeSettings(const NavmeshBakeSettings& left,
                                       const NavmeshBakeSettings& right) noexcept {
    return left.cellSize == right.cellSize && left.agentRadius == right.agentRadius &&
           left.agentHeight == right.agentHeight && left.maximumSlopeDegrees == right.maximumSlopeDegrees &&
           left.maximumStepHeight == right.maximumStepHeight &&
           left.mergeHeightToleranceMetres == right.mergeHeightToleranceMetres &&
           left.navigationLayers == right.navigationLayers && left.queryLayerMask == right.queryLayerMask &&
           left.maximumCells == right.maximumCells;
}

[[nodiscard]] bool SameBounds(const Math::Aabb& left, const Math::Aabb& right) noexcept {
    return left.min.x == right.min.x && left.min.y == right.min.y && left.min.z == right.min.z &&
           left.max.x == right.max.x && left.max.y == right.max.y && left.max.z == right.max.z;
}

[[nodiscard]] NavAgentSettings SettingsFromSeeker(
    const Scene::NavSeeker3DComponent& seeker,
    const NavigationRuntimeConfig& runtimeConfig) noexcept {
    NavAgentSettings settings{};
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
    settings.avoidanceRadiusMetres = runtimeConfig.globalAvoidanceEnabled
                                         ? Scene::NavSeeker3D::EffectiveAvoidanceRadius(seeker)
                                         : 0.0F;
    return settings;
}

[[nodiscard]] Scene::NavigationAgentPathStatus StatusFromAgent(const NavAgentStatus status) noexcept {
    switch (status) {
    case NavAgentStatus::Following:
        return Scene::NavigationAgentPathStatus::Following;
    case NavAgentStatus::Finished:
        return Scene::NavigationAgentPathStatus::Finished;
    case NavAgentStatus::Failed:
        return Scene::NavigationAgentPathStatus::Failed;
    case NavAgentStatus::Idle:
        break;
    }
    return Scene::NavigationAgentPathStatus::Idle;
}

} // namespace

const Navmesh* NavigationRuntime::FindRegionMesh(const Scene::Entity region) const noexcept {
    const auto found = m_regions.find(region);
    if (found == m_regions.end() || !found->second.baked) {
        return nullptr;
    }
    return &found->second.mesh;
}

const NavmeshBakeReport* NavigationRuntime::FindRegionBakeReport(const Scene::Entity region) const noexcept {
    const auto found = m_regions.find(region);
    if (found == m_regions.end() || !found->second.baked) {
        return nullptr;
    }
    return &found->second.report;
}

Scene::Entity NavigationRuntime::FindRegionForPosition(const Math::Vector3& position,
                                                               const float maximumDistanceMetres) const noexcept {
    Scene::Entity nearest = Scene::kInvalidEntity;
    float nearestDistance = maximumDistanceMetres;
    for (const Scene::Entity region : m_regionOrder) {
        const auto found = m_regions.find(region);
        if (found == m_regions.end() || found->second.mesh.polygons.empty()) {
            // A region that baked to nothing, or one whose mesh is gone, is not somewhere an agent
            // can walk - however close it is.
            continue;
        }
        const float distance =
            Scene::NavMeshVolume3D::DistanceToVolumeBounds(found->second.mesh.bounds, position);
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

void NavigationRuntime::Clear() noexcept {
    m_regions.clear();
    m_regionOrder.clear();
    m_agents.clear();
}

NavigationSyncReport NavigationRuntime::Sync(Scene::IEntityManager& entityManager,
                                                      const Physics::IRaycastSystem& raycasts,
                                                      const std::span<const Scene::Entity> agentsInStepOrder,
                                                      const float deltaTimeSeconds) {
    NavigationSyncReport report{};

    // ------------------------------------------------------------------ regions
    std::vector<Scene::Entity> liveRegions;
    entityManager.ForEach<Scene::NavMeshVolume3DComponent>(
        [this, &entityManager, &raycasts, &report, &liveRegions](
            const Scene::Entity entity, Scene::NavMeshVolume3DComponent& region) {
            if (!region.enabled) {
                // A region switched off has no mesh: an agent standing on it must fail rather than
                // walk the ground the author just took away. Its cache is dropped, not kept warm, so
                // no later step can reach a mesh the author believes is gone.
                m_regions.erase(entity);
                return;
            }
            if (!entityManager.HasComponent<Scene::WorldTransformComponent>(entity)) {
                // No pose, no volume: a bake needs somewhere in the world to rasterize. The region
                // keeps whatever mesh it had, because nothing about it changed.
                return;
            }
            ++report.regions;
            liveRegions.push_back(entity);

            const Scene::WorldTransformComponent& world =
                entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
            const Math::Aabb bounds =
                Scene::NavMeshVolume3D::ResolveWorldBounds(region, world.worldPosition);
            const NavmeshBakeSettings settings = SettingsFromRegion(region);

            RegionEntry& entry = m_regions[entity];
            const bool inputsChanged = !entry.baked || !SameBakeSettings(entry.bakedSettings, settings) ||
                                       !SameBounds(entry.bakedBounds, bounds);
            // An explicit request is an author saying the collision world moved under a region whose
            // own volume did not, so it is honoured even when every input this class can see is
            // identical - that is the whole reason the field exists.
            if (!inputsChanged && !Scene::NavMeshVolume3D::ConsumeRebuildRequest(region)) {
                return;
            }
            entry.report = BakeNavmesh(entityManager, raycasts, settings, bounds, entry.mesh);
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
        const bool live = entityManager.IsAlive(it->first) &&
                          entityManager.HasComponent<Scene::NavSeeker3DComponent>(it->first);
        it = live ? std::next(it) : m_agents.erase(it);
    }

    // The neighbour list is built once for the whole step: every listed agent that has somewhere to
    // stand contributes its position and width, and each agent's own entry is recognised and ignored
    // by the steering (two agents cannot be at the same point unless they already overlap).
    std::vector<NavAgentObstacle> neighbours;
    neighbours.reserve(agentsInStepOrder.size());
    for (const Scene::Entity entity : agentsInStepOrder) {
        if (!entityManager.HasComponent<Scene::NavSeeker3DComponent>(entity) ||
            !entityManager.HasComponent<Scene::WorldTransformComponent>(entity)) {
            continue;
        }
        const Scene::NavSeeker3DComponent& seeker = entityManager.GetComponent<Scene::NavSeeker3DComponent>(entity);
        const Scene::WorldTransformComponent& world =
            entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
        neighbours.push_back(NavAgentObstacle{world.worldPosition, seeker.radius});
    }

    for (const Scene::Entity entity : agentsInStepOrder) {
        if (!entityManager.IsAlive(entity) ||
            !entityManager.HasComponent<Scene::NavSeeker3DComponent>(entity)) {
            continue;
        }
        Scene::NavSeeker3DComponent& seeker = entityManager.GetComponent<Scene::NavSeeker3DComponent>(entity);
        if (!seeker.enabled) {
            // Switched off: the cached steering is dropped so a later enable starts from a clean
            // search, and the component is told plainly that nothing is being published.
            m_agents.erase(entity);
            const Math::Vector3 position =
                entityManager.HasComponent<Scene::WorldTransformComponent>(entity)
                    ? entityManager.GetComponent<Scene::WorldTransformComponent>(entity).worldPosition
                    : Math::Vector3{};
            Scene::NavSeeker3D::PublishIdleState(seeker, position);
            continue;
        }
        if (!entityManager.HasComponent<Scene::WorldTransformComponent>(entity)) {
            // A body with nowhere to be cannot be steered, and this pass never adds the component it
            // would need: leave the entity exactly as authored.
            continue;
        }

        const Math::Vector3 position =
            entityManager.GetComponent<Scene::WorldTransformComponent>(entity).worldPosition;
        const NavAgentSettings settings = SettingsFromSeeker(seeker, m_config);
        const Scene::Entity region = FindRegionForPosition(position, settings.offMeshToleranceMetres);
        const Navmesh* mesh = region == Scene::kInvalidEntity ? &kNoMesh : FindRegionMesh(region);
        if (mesh == nullptr) {
            mesh = &kNoMesh;
        }
        if (region == Scene::kInvalidEntity) {
            ++report.agentsWithoutMesh;
        }

        NavAgent& agent = m_agents[entity];
        agent.Configure(settings);
        agent.SetPosition(position);
        agent.SetTarget(seeker.targetPosition);
        if (m_config.maxAvoidanceNeighbors > 0U && neighbours.size() > m_config.maxAvoidanceNeighbors) {
            const float maxDist = std::isfinite(m_config.maxNeighborDistanceMetres) &&
                                          m_config.maxNeighborDistanceMetres > 0.0F
                                      ? m_config.maxNeighborDistanceMetres
                                      : 15.0F;
            const float maxDistSq = maxDist * maxDist;
            std::vector<std::pair<float, NavAgentObstacle>> candidateNeighbours;
            candidateNeighbours.reserve(neighbours.size());
            for (const NavAgentObstacle& obstacle : neighbours) {
                const float dx = obstacle.position.x - position.x;
                const float dy = obstacle.position.y - position.y;
                const float dz = obstacle.position.z - position.z;
                const float distSq = dx * dx + dy * dy + dz * dz;
                if (distSq <= maxDistSq) {
                    candidateNeighbours.emplace_back(distSq, obstacle);
                }
            }
            if (candidateNeighbours.size() > m_config.maxAvoidanceNeighbors) {
                std::partial_sort(
                    candidateNeighbours.begin(),
                    candidateNeighbours.begin() + static_cast<std::ptrdiff_t>(m_config.maxAvoidanceNeighbors),
                    candidateNeighbours.end(),
                    [](const auto& lhs, const auto& rhs) noexcept { return lhs.first < rhs.first; });
                candidateNeighbours.resize(m_config.maxAvoidanceNeighbors);
            }
            std::vector<NavAgentObstacle> localNeighbours;
            localNeighbours.reserve(candidateNeighbours.size());
            for (const auto& [distSq, obstacle] : candidateNeighbours) {
                static_cast<void>(distSq);
                localNeighbours.push_back(obstacle);
            }
            agent.Step(*mesh, localNeighbours, deltaTimeSeconds);
        } else {
            agent.Step(*mesh, neighbours, deltaTimeSeconds);
        }

        seeker.nextPathPosition = agent.GetNextPathPosition();
        seeker.desiredVelocity = agent.GetDesiredVelocity();
        seeker.pathStatus = StatusFromAgent(agent.GetStatus());
        seeker.pathChanged = agent.GetPathChanged();
        seeker.targetReached = agent.HasReachedTarget();
        ++report.agents;
    }

    return report;
}

} // namespace UVE::Navigation
