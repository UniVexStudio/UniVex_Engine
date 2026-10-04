// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "uve/math/vector3_uve.h"
#include "uve/navigation/navmesh_uve.h"

namespace UVE::Navigation {

/// What a path request produced, in the terms an agent cares about: can I follow this, and did I
/// reach what I asked for.
enum class NavPathStatusUVE : std::uint8_t {
    /// A complete path from the start's polygon to the target's polygon.
    Found = 0,
    /// A path toward the target that stops short of it: the goal is on an island this navmesh does
    /// not connect to the start, or the search ran out of its own budget. The path is still
    /// followable and still ends as close to the goal as the mesh allows, which is what an agent
    /// should walk before giving up.
    Partial,
    /// The start is not on this navmesh and not close enough to it to be put on it.
    StartOffMesh,
    /// The start was fine; the target is not on this navmesh and not close enough to it.
    TargetOffMesh,
    /// There is nothing to search - the navmesh has no polygons at all.
    NoMesh,
};

struct NavPathRequestUVE final {
    Math::Vector3UVE start{};
    Math::Vector3UVE target{};
    /// Which polygons the path may use: a polygon is traversable when
    /// `(polygon.navigationLayers & navigationLayers) != 0`. Defaults to every layer, so a caller
    /// that has no opinion still gets a path.
    std::uint32_t navigationLayers = 0xFFFFFFFFU;
    /// How far the start or the target may sit from the mesh and still be brought onto it. A target
    /// a script wrote a little above the floor, or beside the edge of a region, is the ordinary case
    /// this exists for; past it, the request is refused rather than answered with a path from
    /// somewhere the caller did not ask about.
    float offMeshToleranceMetres = 2.0F;
    /// The most search nodes one request may expand before it gives up and answers with a partial
    /// path. A node is one doorway a chain stepped through, so a room entered twice is two of them;
    /// A* over a bounded mesh terminates anyway, and this is the bound that keeps one pathological
    /// request from spending a frame's whole budget - reported back, so a caller can tell a complete
    /// path from a truncated search.
    std::size_t maximumExpandedNodes = 4096U;
};

struct NavPathUVE final {
    /// The corners the path turns at, ending with the goal. The START is deliberately not in here:
    /// the caller already stands there, and a follower that must first walk to its own position
    /// spends a step doing nothing.
    std::vector<Math::Vector3UVE> waypoints;
    /// Length of the waypoint polyline, measured from the start's own point on the mesh to the goal.
    float lengthMetres = 0.0F;
    /// Search nodes the request took off its open list, one per doorway the chain stepped through.
    /// Reported so a truncated search is visible as one.
    std::size_t expandedNodes = 0U;
    NavPathStatusUVE status = NavPathStatusUVE::NoMesh;

    [[nodiscard]] bool IsUsableUVE() const noexcept {
        return status == NavPathStatusUVE::Found || status == NavPathStatusUVE::Partial;
    }
    [[nodiscard]] bool IsCompleteUVE() const noexcept { return status == NavPathStatusUVE::Found; }

    void ClearUVE() noexcept;
};

/// Finds a path across `mesh` from `request.start` to `request.target`.
///
/// A* over the polygons (the cost being the distance through the portals actually used, the
/// heuristic the straight-line distance to the goal, so the search is both complete and directed),
/// then string-pulling: the polygon chain is reduced to the corners a body would actually walk,
/// which is what stops an agent from zig-zagging between polygon centres. The result is in world
/// space and on the mesh's surface, because the portal endpoints and the projected goal already are.
///
/// The search never leaves the polygons whose layers the request allows, so "this agent may not walk
/// there" is a property of the path rather than of the follower.
[[nodiscard]] NavPathUVE FindNavPathUVE(const NavmeshUVE& mesh, const NavPathRequestUVE& request);

} // namespace UVE::Navigation
