// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/world_partition_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/decal_3d_uve.h"
#include "uve/objects/3d/fog_volume_3d_uve.h"

namespace UVE::Scene {

bool IsWorldPartition3DObjectComponentValidUVE(const WorldPartition3DComponentUVE& value) noexcept {
    if (!std::isfinite(value.cellSize) || value.cellSize <= 0.0F || value.maximumLoadedCells == 0U ||
        value.maximumLoadedCells > kMaximumStreamedCellsUVE || value.loadedCellCount > value.maximumLoadedCells) {
        return false;
    }
    for (const std::uint32_t count : value.cellCounts) {
        if (count == 0U || count > kMaximumStreamedCellsUVE) {
            return false;
        }
    }
    return true;
}

std::optional<WorldPartition3DCellIdUVE> ResolveWorldPartition3DCellIdForPositionUVE(
    const WorldPartition3DComponentUVE& config,
    const Math::Vector3UVE& gridOriginWorld,
    const Math::Vector3UVE& pointWorld) noexcept {
    if (!IsWorldPartition3DObjectComponentValidUVE(config)) {
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
    WorldPartition3DCellIdUVE cell;
    if (!cellCoordinate(localX, config.cellCounts[0U], cell.x) ||
        !cellCoordinate(localY, config.cellCounts[1U], cell.y) ||
        !cellCoordinate(localZ, config.cellCounts[2U], cell.z)) {
        return std::nullopt;
    }
    return cell;
}

std::size_t ResolveWorldPartition3DCellLinearIndexUVE(
    const WorldPartition3DCellIdUVE& cell,
    const std::array<std::uint32_t, 3U>& cellCounts) noexcept {
    const std::uint32_t countX = cellCounts[0U];
    const std::uint32_t countY = cellCounts[1U];
    return static_cast<std::size_t>(cell.x) +
           static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(countX) +
           static_cast<std::size_t>(cell.z) * static_cast<std::size_t>(countX) *
               static_cast<std::size_t>(countY);
}

void SortWorldPartition3DOccupiedCellsUVE(std::span<WorldPartition3DOccupiedCellUVE> occupied,
                                          const std::array<std::uint32_t, 3U>& cellCounts) {
    std::sort(occupied.begin(), occupied.end(),
              [&cellCounts](const WorldPartition3DOccupiedCellUVE& lhs,
                            const WorldPartition3DOccupiedCellUVE& rhs) {
                  if (lhs.nearestDistanceSquared != rhs.nearestDistanceSquared) {
                      return lhs.nearestDistanceSquared < rhs.nearestDistanceSquared;
                  }
                  return ResolveWorldPartition3DCellLinearIndexUVE(lhs.id, cellCounts) <
                         ResolveWorldPartition3DCellLinearIndexUVE(rhs.id, cellCounts);
              });
}

std::size_t CountWorldPartition3DAdmittedCellsUVE(const std::size_t occupiedCount,
                                                  const std::uint32_t maximumLoadedCells) noexcept {
    return std::min(occupiedCount, static_cast<std::size_t>(maximumLoadedCells));
}

bool IsWorldPartition3DCellAdmittedUVE(const WorldPartition3DCellIdUVE& cell,
                                       const std::span<const WorldPartition3DOccupiedCellUVE> rankedNearestFirst,
                                       const std::uint32_t maximumLoadedCells) noexcept {
    const std::size_t live =
        CountWorldPartition3DAdmittedCellsUVE(rankedNearestFirst.size(), maximumLoadedCells);
    for (std::size_t i = 0U; i < live; ++i) {
        if (rankedNearestFirst[i].id == cell) {
            return true;
        }
    }
    return false;
}

Math::Vector3UVE ResolveWorldPartition3DVolumeSizeUVE(const WorldPartition3DComponentUVE& config) noexcept {
    return Math::Vector3UVE{config.cellSize * static_cast<float>(config.cellCounts[0U]),
                            config.cellSize * static_cast<float>(config.cellCounts[1U]),
                            config.cellSize * static_cast<float>(config.cellCounts[2U])};
}

bool CarriesWorldPartition3DDrawableUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    return entityManager.HasComponentUVE<MeshComponentUVE>(entity) ||
           entityManager.HasComponentUVE<PrimitiveMeshComponentUVE>(entity) ||
           entityManager.HasComponentUVE<Decal3DComponentUVE>(entity) ||
           entityManager.HasComponentUVE<ParticleEmitterComponentUVE>(entity) ||
           entityManager.HasComponentUVE<FogVolume3DComponentUVE>(entity);
}

bool IsWorldPartition3DDrawHiddenUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.HasComponentUVE<WorldPartition3DMembershipComponentUVE>(entity)) {
        return false;
    }
    const WorldPartition3DMembershipComponentUVE& membership =
        entityManager.GetComponentUVE<WorldPartition3DMembershipComponentUVE>(entity);
    const bool ownerAlive = membership.partition != kInvalidEntityUVE &&
                            entityManager.IsAliveUVE(membership.partition) &&
                            entityManager.HasComponentUVE<WorldPartition3DComponentUVE>(membership.partition);
    return !ResolveWorldPartition3DMembershipLiveUVE(ownerAlive, membership.live);
}

void CollectWorldPartition3DGizmosUVE(IEntityManagerUVE& entityManager,
                                      std::vector<WorldPartition3DGizmoUVE>& out) {
    entityManager.ForEachUVE<WorldTransformComponentUVE, WorldPartition3DComponentUVE>(
        [&entityManager, &out](const EntityUVE entity, const WorldTransformComponentUVE& world,
                               const WorldPartition3DComponentUVE& config) {
            if (entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(entity) || world.dirty ||
                !IsWorldPartition3DObjectComponentValidUVE(config)) {
                return;
            }
            WorldPartition3DGizmoUVE gizmo;
            gizmo.origin = world.worldPosition;
            gizmo.size = ResolveWorldPartition3DVolumeSizeUVE(config);
            gizmo.cellSize = config.cellSize;
            gizmo.cellCounts = config.cellCounts;
            gizmo.enabled = config.enabled;
            gizmo.color = config.enabled ? Math::Vector3UVE{0.28F, 0.72F, 1.0F}
                                         : Math::Vector3UVE{0.45F, 0.48F, 0.52F};
            out.push_back(gizmo);
        });
}

} // namespace UVE::Scene
