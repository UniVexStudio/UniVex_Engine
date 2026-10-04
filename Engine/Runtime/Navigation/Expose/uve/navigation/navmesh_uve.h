// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "uve/math/aabb_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Navigation {

/// One convex, walkable polygon of a baked navmesh, in world space.
///
/// Rectangles, not arbitrary convex shapes: the bake rasterizes the region's volume on a grid and
/// merges cells that agree on height, so every vertex sits on a cell corner and every edge is
/// axis-aligned in the grid plane. That is what makes the merge cheap, the adjacency exact rather
/// than tolerance-based, and the portal arithmetic a matter of shared cell edges.
///
/// Heights are per VERTEX, not per polygon, so a polygon that spans gentle ground follows it
/// instead of pretending to be a level plate: a cell corner's height is the average of the walkable
/// cells touching it, which is what makes a path across a slope walk the slope.
struct NavmeshPolygonUVE final {
    /// A merged rectangle has four corners. The last two slots exist so a future bake that trades a
    /// corner for a diagonal (a non-grid refinement) fits without a layout change; the bake below
    /// only ever writes four.
    static constexpr std::size_t kMaximumVerticesUVE = 6U;

    std::array<Math::Vector3UVE, kMaximumVerticesUVE> vertices{};
    std::uint32_t vertexCount = 0U;

    /// Centroid in the XZ plane, at the polygon's average surface height. The A* heuristic reads
    /// it, so it is stored rather than recomputed per search node.
    Math::Vector3UVE center{};
    float areaSquareMetres = 0.0F;

    /// The navigation layers of the surface this polygon was rasterized from. Cells only merge when
    /// their layers match exactly, so this is one layer set per polygon and a request's layer mask
    /// filters whole polygons rather than parts of them.
    std::uint32_t navigationLayers = 1U;

    /// This polygon's window into NavmeshUVE::portals. Contiguous and built once per bake, because
    /// A* walks it on every search.
    std::uint32_t firstPortal = 0U;
    std::uint32_t portalCount = 0U;
};

/// One traversable connection between two polygons: the edge they share, in world space, oriented
/// relative to travel from `from` to `to`.
struct NavmeshPortalUVE final {
    std::uint32_t from = 0U;
    std::uint32_t to = 0U;
    /// The shared edge's endpoints, ordered so that walking from `from` toward `to` passes `left`
    /// on the left of travel and `right` on the right. The string-pulling step reads exactly this
    /// order, so the bake decides the sides once rather than every traversal deciding them again.
    Math::Vector3UVE left{};
    Math::Vector3UVE right{};
};

/// A baked, walkable representation of one NavigationRegion3D's volume.
///
/// It is world-space and static: polygons and portals are answers to "where can an agent of this
/// size stand and how do I walk between those places", baked once from the collision world that
/// existed at bake time, exactly like the reference engine's navmesh is baked from the level rather
/// than re-derived per frame. Nothing here holds an entity or an asset reference, so a navmesh can
/// be copied, stored and reasoned about on its own.
struct NavmeshUVE final {
    std::vector<NavmeshPolygonUVE> polygons;
    std::vector<NavmeshPortalUVE> portals;

    /// The region volume that was rasterized, in world space.
    Math::AabbUVE bounds{};

    /// The settings the bake ran with, recorded because every query's tolerance and every path's
    /// clearance has to agree with what was baked: an agent wider than `agentRadius` is not merely
    /// "a bit tight" on this mesh, it is an agent whose radius the erosion never accounted for.
    std::uint32_t navigationLayers = 1U;
    float cellSize = 0.0F;
    float agentRadius = 0.0F;
    float agentHeight = 0.0F;
    float maximumSlopeDegrees = 0.0F;
    float maximumStepHeight = 0.0F;

    void ClearUVE() noexcept;

    [[nodiscard]] bool IsEmptyUVE() const noexcept;
    [[nodiscard]] std::size_t GetPolygonCountUVE() const noexcept;
    [[nodiscard]] std::size_t GetPortalCountUVE() const noexcept;
    [[nodiscard]] float GetWalkableAreaSquareMetresUVE() const noexcept;

    /// The polygon containing `worldPoint`, if any: the point must be inside the polygon in the XZ
    /// plane and within `maximumStepHeight` of the polygon's own surface, which is the same band a
    /// traversable portal uses - so "here" and "across that edge" mean the same thing to a caller.
    [[nodiscard]] std::optional<std::size_t> FindPolygonUVE(const Math::Vector3UVE& worldPoint) const noexcept;

    /// The nearest polygon within `maximumDistanceMetres` of `worldPoint`, restricted to polygons
    /// whose layers intersect `layerMask`. Searched over polygon centres first and then refined by
    /// projecting onto the polygon, so an off-mesh request (a spawn point beside the mesh, a target
    /// a script wrote by hand) lands on the mesh instead of being refused outright.
    [[nodiscard]] std::optional<std::size_t> FindNearestPolygonUVE(const Math::Vector3UVE& worldPoint,
                                                                   float maximumDistanceMetres,
                                                                   std::uint32_t layerMask) const noexcept;

    /// `worldPoint` moved onto `polygon`'s surface: the XZ position clamped into the polygon and the
    /// height read from its vertices, which is what a steering step uses to keep an agent's
    /// next-waypoint on walkable ground rather than in the air above it.
    [[nodiscard]] std::optional<Math::Vector3UVE> ProjectPointUVE(const Math::Vector3UVE& worldPoint,
                                                                  std::size_t polygon) const noexcept;

    /// The polygon centre, or `worldPoint` itself when `polygon` does not exist - a caller with a
    /// stale index gets a usable point rather than an out-of-range read.
    [[nodiscard]] Math::Vector3UVE PolygonCenterUVE(std::size_t polygon) const noexcept;
};

/// Signed area in the XZ plane, positive for counter-clockwise winding as seen from +Y looking down.
/// Public because it is the one definition of "which side is this" that the bake, the portal
/// orientation and the string-pulling all have to share; a second implementation anywhere would be a
/// second convention.
[[nodiscard]] float SignedAreaXZUVE(const Math::Vector3UVE* vertices, std::size_t vertexCount) noexcept;

} // namespace UVE::Navigation
