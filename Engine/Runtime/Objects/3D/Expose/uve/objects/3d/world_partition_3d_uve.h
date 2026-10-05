// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

inline constexpr std::size_t kMaximumStreamedCellsUVE = 4096U;

struct WorldPartition3DComponentUVE final {
    float cellSize = 128.0F;
    std::array<std::uint32_t, 3U> cellCounts{16U, 1U, 16U};
    std::uint32_t maximumLoadedCells = 64U;
    // Runtime-only: the number of cells the sync decided live this tick. Always <=
    // maximumLoadedCells, always 0 while !enabled (the serializer both protects and ignores it -
    // ToJson never writes it, FromJson never reads it, so it starts at 0 on every fresh load).
    std::uint32_t loadedCellCount = 0U;
    bool enabled = true;
};

[[nodiscard]] bool IsWorldPartition3DObjectComponentValidUVE(const WorldPartition3DComponentUVE& value) noexcept;

// Runtime-only cell membership, owned by EngineCoreUVE::SyncWorldPartition3DObjectsUVE() and
// ATTACHED BY THE ENGINE to drawable descendants of a world partition (mesh, primitive mesh,
// decal, particle emitter, fog volume - not lights). Never authored, never saved. `live` is this
// tick's vis-budget verdict: false only when the cell landed outside maximumLoadedCells. Draw
// paths consult this through IsWorldPartition3DDrawHiddenUVE(). This does not load or unload
// files; LevelStreamer3D is the file streamer.
struct WorldPartition3DMembershipComponentUVE final {
    Scene::EntityUVE partition = Scene::kInvalidEntityUVE;
    bool live = true;
};

// A cell's integer coordinate inside its partition grid. The partition's own world position is
// the grid's minimum corner (Unreal's convention), so cell (0,0,0) starts exactly AT the
// partition object and the volume covers [origin, origin + cellSize*counts).
struct WorldPartition3DCellIdUVE final {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};

[[nodiscard]] constexpr bool operator==(const WorldPartition3DCellIdUVE& lhs,
                                        const WorldPartition3DCellIdUVE& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] constexpr bool operator!=(const WorldPartition3DCellIdUVE& lhs,
                                        const WorldPartition3DCellIdUVE& rhs) noexcept {
    return !(lhs == rhs);
}

// Which cell `pointWorld` lives in, given an invalid-safe config. Rules, each measured:
//   * non-finite point or origin, or invalid config      -> no cell (fail-open: unmanaged)
//   * point strictly outside [origin, origin+cellSize*counts) per axis -> no cell (the
//     partition does not manage beyond its volume - anything else keeps rendering)
//   * a point exactly ON a cell boundary belongs to the HIGHER-numbered cell (the floor rule:
//     cell i spans [origin + i*cellSize, origin + (i+1)*cellSize)), so every point matches
//     exactly one cell among finite configurations, never two.
[[nodiscard]] std::optional<WorldPartition3DCellIdUVE>
ResolveWorldPartition3DCellIdForPositionUVE(const WorldPartition3DComponentUVE& config,
                                            const Math::Vector3UVE& gridOriginWorld,
                                            const Math::Vector3UVE& pointWorld) noexcept;

// x-major linear index inside the grid (x + y*cx + z*cx*cy) - the deterministic key the sync's
// map/sort work and counters use. The caller must already hold a cell id inside the grid; the
// input range is exactly what ResolveWorldPartition3DCellIdForPositionUVE() can produce.
[[nodiscard]] std::size_t
ResolveWorldPartition3DCellLinearIndexUVE(const WorldPartition3DCellIdUVE& cell,
                                          const std::array<std::uint32_t, 3U>& cellCounts) noexcept;

// The renderer-side verdict for one membership entry. Alive owner: trust the live flag. Dead
// owner (the partition was destroyed or its document unloaded): fail OPEN - the owner can no
// longer update anything, so its residual opinion must not hide content forever.
[[nodiscard]] inline constexpr bool ResolveWorldPartition3DMembershipLiveUVE(
    bool partitionOwnerAlive, bool live) noexcept {
    return live || !partitionOwnerAlive;
}

// One occupied cell waiting to be ranked for the vis-budget. `nearestDistanceSquared` is the
// closest drawable in that cell to any viewer.
struct WorldPartition3DOccupiedCellUVE final {
    WorldPartition3DCellIdUVE id{};
    float nearestDistanceSquared = 0.0F;
};

void SortWorldPartition3DOccupiedCellsUVE(std::span<WorldPartition3DOccupiedCellUVE> occupied,
                                          const std::array<std::uint32_t, 3U>& cellCounts);

[[nodiscard]] std::size_t CountWorldPartition3DAdmittedCellsUVE(std::size_t occupiedCount,
                                                                std::uint32_t maximumLoadedCells) noexcept;

[[nodiscard]] bool IsWorldPartition3DCellAdmittedUVE(
    const WorldPartition3DCellIdUVE& cell, std::span<const WorldPartition3DOccupiedCellUVE> rankedNearestFirst,
    std::uint32_t maximumLoadedCells) noexcept;

[[nodiscard]] Math::Vector3UVE
ResolveWorldPartition3DVolumeSizeUVE(const WorldPartition3DComponentUVE& config) noexcept;

[[nodiscard]] bool CarriesWorldPartition3DDrawableUVE(IEntityManagerUVE& entityManager, EntityUVE entity);

[[nodiscard]] bool IsWorldPartition3DDrawHiddenUVE(IEntityManagerUVE& entityManager, EntityUVE entity);

struct WorldPartition3DGizmoUVE final {
    Math::Vector3UVE origin{};
    Math::Vector3UVE size{};
    Math::Vector3UVE color{0.28F, 0.72F, 1.0F};
    float cellSize = 128.0F;
    std::array<std::uint32_t, 3U> cellCounts{16U, 1U, 16U};
    bool enabled = true;
};

void CollectWorldPartition3DGizmosUVE(IEntityManagerUVE& entityManager, std::vector<WorldPartition3DGizmoUVE>& out);

} // namespace UVE::Scene
