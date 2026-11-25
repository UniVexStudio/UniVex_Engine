// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "world_partition_3d.h"

#include <algorithm>
#include <cmath>

#include "editor_internal_entity_component.h"
#include "mesh_component.h"
#include "particle_emitter_component.h"
#include "primitive_mesh_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "abstract_objects_3d.h"
#include "decal_3d.h"
#include "fog_volume_3d.h"

namespace UVE::Scene {

bool IsWorldPartition3DObjectComponentValid(const WorldPartition3DComponent& value) noexcept {
    if (!std::isfinite(value.cellSize) || value.cellSize <= 0.0F || value.maximumLoadedCells == 0U ||
        value.maximumLoadedCells > kMaximumStreamedCells || value.loadedCellCount > value.maximumLoadedCells) {
        return false;
    }
    for (const std::uint32_t count : value.cellCounts) {
        if (count == 0U || count > kMaximumStreamedCells) {
            return false;
        }
    }
    return true;
}

void ApplyWorldPartition3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                              const WorldPartition3DObjectDefinition& value) {
    ApplyObject3DRecipe(entityManager, entity, WorldPartition3DObjectDefinition::defaultName);
    if (entityManager.IsAlive(entity) &&
        !entityManager.HasComponent<WorldPartition3DComponent>(entity)) {
        entityManager.AddComponent<WorldPartition3DComponent>(entity, value.partition);
    }
}

std::optional<WorldPartition3DCellId> ResolveWorldPartition3DCellIdForPosition(
    const WorldPartition3DComponent& config,
    const Math::Vector3& gridOriginWorld,
    const Math::Vector3& pointWorld) noexcept {
    if (!IsWorldPartition3DObjectComponentValid(config)) {
        return std::nullopt;
    }
    const float localX = pointWorld.x - gridOriginWorld.x;
    const float localY = pointWorld.y - gridOriginWorld.y;
    const float localZ = pointWorld.z - gridOriginWorld.z;
    if (!std::isfinite(localX) || !std::isfinite(localY) || !std::isfinite(localZ)) {
        return std::nullopt;
    }
    // Each axis maps independently: depth-limited worlds (cellCounts[1] == 1, the common flat
    // case) still behave EXACTLY like the 3D rule, not a special case.
    const auto cellCoordinate = [&config](const float local, const std::uint32_t count,
                                         std::int32_t& outCoordinate) noexcept {
        if (local < 0.0F || local >= config.cellSize * static_cast<float>(count)) {
            return false; // strictly outside this axis' volume
        }
        // The floor rule: a point exactly on a cell start belongs to the cell it starts. The
        // < count*cellSize guard above already parked every edge coordinate inside [0, count)
        // exactly once, so floor() here can never produce an out-of-range id.
        const float scaled = local / config.cellSize;
        outCoordinate = static_cast<std::int32_t>(std::floor(scaled));
        return true;
    };
    WorldPartition3DCellId cell;
    if (!cellCoordinate(localX, config.cellCounts[0U], cell.x) ||
        !cellCoordinate(localY, config.cellCounts[1U], cell.y) ||
        !cellCoordinate(localZ, config.cellCounts[2U], cell.z)) {
        return std::nullopt;
    }
    return cell;
}

std::size_t ResolveWorldPartition3DCellLinearIndex(
    const WorldPartition3DCellId& cell,
    const std::array<std::uint32_t, 3U>& cellCounts) noexcept {
    const std::uint32_t countX = cellCounts[0U];
    const std::uint32_t countY = cellCounts[1U];
    return static_cast<std::size_t>(cell.x) +
           static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(countX) +
           static_cast<std::size_t>(cell.z) * static_cast<std::size_t>(countX) *
               static_cast<std::size_t>(countY);
}

void SortWorldPartition3DOccupiedCells(std::span<WorldPartition3DOccupiedCell> occupied,
                                          const std::array<std::uint32_t, 3U>& cellCounts) {
    std::sort(occupied.begin(), occupied.end(),
              [&cellCounts](const WorldPartition3DOccupiedCell& lhs,
                            const WorldPartition3DOccupiedCell& rhs) {
                  if (lhs.nearestDistanceSquared != rhs.nearestDistanceSquared) {
                      return lhs.nearestDistanceSquared < rhs.nearestDistanceSquared;
                  }
                  return ResolveWorldPartition3DCellLinearIndex(lhs.id, cellCounts) <
                         ResolveWorldPartition3DCellLinearIndex(rhs.id, cellCounts);
              });
}

std::size_t CountWorldPartition3DAdmittedCells(const std::size_t occupiedCount,
                                                  const std::uint32_t maximumLoadedCells) noexcept {
    return std::min(occupiedCount, static_cast<std::size_t>(maximumLoadedCells));
}

bool IsWorldPartition3DCellAdmitted(const WorldPartition3DCellId& cell,
                                       const std::span<const WorldPartition3DOccupiedCell> rankedNearestFirst,
                                       const std::uint32_t maximumLoadedCells) noexcept {
    const std::size_t live =
        CountWorldPartition3DAdmittedCells(rankedNearestFirst.size(), maximumLoadedCells);
    for (std::size_t i = 0U; i < live; ++i) {
        if (rankedNearestFirst[i].id == cell) {
            return true;
        }
    }
    return false;
}

Math::Vector3 ResolveWorldPartition3DVolumeSize(const WorldPartition3DComponent& config) noexcept {
    return Math::Vector3{config.cellSize * static_cast<float>(config.cellCounts[0U]),
                            config.cellSize * static_cast<float>(config.cellCounts[1U]),
                            config.cellSize * static_cast<float>(config.cellCounts[2U])};
}

bool CarriesWorldPartition3DDrawable(IEntityManager& entityManager, const Entity entity) {
    return entityManager.HasComponent<MeshComponent>(entity) ||
           entityManager.HasComponent<PrimitiveMeshComponent>(entity) ||
           entityManager.HasComponent<Decal3DComponent>(entity) ||
           entityManager.HasComponent<ParticleEmitterComponent>(entity) ||
           entityManager.HasComponent<FogVolume3DComponent>(entity);
}

bool IsWorldPartition3DDrawHidden(IEntityManager& entityManager, const Entity entity) {
    if (!entityManager.IsAlive(entity)) {
        return false;
    }
    if (!entityManager.HasComponent<WorldPartition3DMembershipComponent>(entity)) {
        return false;
    }
    const WorldPartition3DMembershipComponent& membership =
        entityManager.GetComponent<WorldPartition3DMembershipComponent>(entity);
    const bool ownerAlive = membership.partition != kInvalidEntity &&
                            entityManager.IsAlive(membership.partition) &&
                            entityManager.HasComponent<WorldPartition3DComponent>(membership.partition);
    return !ResolveWorldPartition3DMembershipLive(ownerAlive, membership.live);
}

void CollectWorldPartition3DGizmos(IEntityManager& entityManager,
                                      std::vector<WorldPartition3DGizmo>& out) {
    entityManager.ForEach<WorldTransformComponent, WorldPartition3DComponent>(
        [&entityManager, &out](const Entity entity, const WorldTransformComponent& world,
                               const WorldPartition3DComponent& config) {
            if (entityManager.HasComponent<EditorInternalEntityComponent>(entity) || world.dirty ||
                !IsWorldPartition3DObjectComponentValid(config)) {
                return;
            }
            WorldPartition3DGizmo gizmo;
            gizmo.origin = world.worldPosition;
            gizmo.size = ResolveWorldPartition3DVolumeSize(config);
            gizmo.cellSize = config.cellSize;
            gizmo.cellCounts = config.cellCounts;
            gizmo.enabled = config.enabled;
            gizmo.color = config.enabled ? Math::Vector3{0.28F, 0.72F, 1.0F}
                                         : Math::Vector3{0.45F, 0.48F, 0.52F};
            out.push_back(gizmo);
        });
}

} // namespace UVE::Scene
