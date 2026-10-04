// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/navigation/nav_path_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <vector>

namespace UVE::Navigation {

namespace {

[[nodiscard]] float PlanarDistanceUVE(const Math::Vector3UVE& a, const Math::Vector3UVE& b) noexcept {
    const float deltaX = b.x - a.x;
    const float deltaZ = b.z - a.z;
    return std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
}

[[nodiscard]] Math::Vector3UVE PortalMidpointUVE(const NavmeshPortalUVE& portal) noexcept {
    return Math::Vector3UVE{(portal.left.x + portal.right.x) * 0.5F, (portal.left.y + portal.right.y) * 0.5F,
                            (portal.left.z + portal.right.z) * 0.5F};
}

/// One node of the search, as the priority queue orders it.
struct OpenNodeUVE final {
    std::size_t polygon = 0U;
    float estimatedTotalCost = 0.0F;
};

[[nodiscard]] bool operator<(const OpenNodeUVE& left, const OpenNodeUVE& right) noexcept {
    // `std::priority_queue` is a max-heap, so the ordering is inverted: the cheapest node is the
    // first one out.
    return left.estimatedTotalCost > right.estimatedTotalCost;
}

/// The portal a node was entered through, kept per node so the chain can be walked back to the start
/// without a second search.
struct EntryUVE final {
    std::uint32_t portalIndex = 0U;
    bool entered = false;
};

/// Whether `polygon` may be used by a request with this layer mask.
[[nodiscard]] bool IsPolygonUsableUVE(const NavmeshUVE& mesh, const std::size_t polygon,
                                      const std::uint32_t layerMask) noexcept {
    return polygon < mesh.polygons.size() && (mesh.polygons[polygon].navigationLayers & layerMask) != 0U;
}

/// The closest point on the nearest polygon the request's layers allow, within the request's
/// off-mesh tolerance - the answer for an end point that is not sitting on a usable polygon itself.
[[nodiscard]] std::optional<std::pair<std::size_t, Math::Vector3UVE>> ProjectOntoUsableUVE(
    const NavmeshUVE& mesh, const Math::Vector3UVE& point, const NavPathRequestUVE& request) {
    const std::optional<std::size_t> nearest =
        mesh.FindNearestPolygonUVE(point, request.offMeshToleranceMetres, request.navigationLayers);
    if (!nearest.has_value()) {
        return std::nullopt;
    }
    const std::optional<Math::Vector3UVE> projected = mesh.ProjectPointUVE(point, *nearest);
    if (!projected.has_value()) {
        return std::nullopt;
    }
    return std::make_pair(*nearest, *projected);
}

/// Where a search may begin: on the polygon the point stands on when that polygon is one the
/// request's layers allow, else on the nearest allowed polygon within the tolerance. A start that
/// is on a polygon the agent may not walk cannot be left from, so it is snapped like any other
/// off-mesh start instead of being trusted.
[[nodiscard]] std::optional<std::pair<std::size_t, Math::Vector3UVE>> ResolveStartUVE(
    const NavmeshUVE& mesh, const Math::Vector3UVE& point, const NavPathRequestUVE& request) {
    const std::optional<std::size_t> containing = mesh.FindPolygonUVE(point);
    if (containing.has_value() && IsPolygonUsableUVE(mesh, *containing, request.navigationLayers)) {
        return std::make_pair(*containing, point);
    }
    return ProjectOntoUsableUVE(mesh, point, request);
}

/// Where a search may aim. A target standing on a polygon this request's layers exclude is still a
/// target on the mesh: the search simply may not finish on that polygon, and the partial-path
/// handling walks as close to it as the allowed polygons reach. Refusing it as off-mesh would throw
/// away an answer the caller can use - an agent told to walk to a room it may not enter should walk
/// to the doorway, not stand still.
[[nodiscard]] std::optional<std::pair<std::size_t, Math::Vector3UVE>> ResolveTargetUVE(
    const NavmeshUVE& mesh, const Math::Vector3UVE& point, const NavPathRequestUVE& request) {
    const std::optional<std::size_t> containing = mesh.FindPolygonUVE(point);
    if (containing.has_value()) {
        return std::make_pair(*containing, point);
    }
    return ProjectOntoUsableUVE(mesh, point, request);
}

/// One portal window with its ends told apart the way the funnel needs them: `left` is on the
/// walker's left as the portal is crossed, `right` on the right.
struct OrientedWindowUVE final {
    Math::Vector3UVE left{};
    Math::Vector3UVE right{};
};

/// Orders a portal's two ends by the walker's own left and right, from the direction the portal is
/// crossed.
///
/// The mesh stores the same thing (the bake writes each direction's ends in this order), and the
/// funnel below only works when the two agree: feeding it a window with its ends swapped makes it
/// turn at the wrong corner - the path still ends in the right place, but it hugs the outside of a
/// bend instead of its inside. Deriving the order here means a mesh that was built by hand, or
/// edited, cannot quietly become a worse path; what the mesh says is checked against what the
/// geometry says in the bake's own tests.
[[nodiscard]] OrientedWindowUVE OrientWindowUVE(const NavmeshUVE& mesh, const NavmeshPortalUVE& portal) {
    OrientedWindowUVE window{};
    const Math::Vector3UVE direction = mesh.PolygonCenterUVE(portal.to) - mesh.PolygonCenterUVE(portal.from);
    // The walker's left in a right-handed, y-up frame: a point is on it when the cross product of
    // the travel direction with the offset to the point has a positive y - which in the XZ plane is
    // this product.
    const auto leftSideUVE = [&direction](const Math::Vector3UVE& endpoint, const Math::Vector3UVE& other) {
        const Math::Vector3UVE offset = endpoint - other;
        return direction.z * offset.x - direction.x * offset.z;
    };
    if (leftSideUVE(portal.left, portal.right) >= 0.0F) {
        window.left = portal.left;
        window.right = portal.right;
    } else {
        window.left = portal.right;
        window.right = portal.left;
    }
    return window;
}

/// The 2D triangle area Detour's funnel is written in terms of: positive when `point` falls on one
/// side of the line `from` -> `to`, negative on the other, zero on it.
[[nodiscard]] float TriangleAreaUVE(const Math::Vector3UVE& from, const Math::Vector3UVE& to,
                                    const Math::Vector3UVE& point) noexcept {
    return (to.x - from.x) * (point.z - from.z) - (to.z - from.z) * (point.x - from.x);
}

[[nodiscard]] bool SamePointUVE(const Math::Vector3UVE& left, const Math::Vector3UVE& right) noexcept {
    constexpr float kSamePointToleranceUVE = 1.0e-4F;
    return std::fabs(left.x - right.x) <= kSamePointToleranceUVE && std::fabs(left.z - right.z) <= kSamePointToleranceUVE;
}

/// Adds a waypoint unless it is where the walk already stands.
///
/// The funnel reaches its verification of the last window through the same corner branch that
/// handles every other turn, so the goal can be named as a corner and then again as the end. A
/// follower given that path would stop, wait a frame and step again; a duplicate waypoint is a
/// stopped agent, not a cosmetic detail.
void AppendWaypointUVE(std::vector<Math::Vector3UVE>& outWaypoints, const Math::Vector3UVE& from,
                       const Math::Vector3UVE& waypoint) {
    if (!outWaypoints.empty() && SamePointUVE(outWaypoints.back(), waypoint)) {
        return;
    }
    if (outWaypoints.empty() && SamePointUVE(from, waypoint)) {
        return;
    }
    outWaypoints.push_back(waypoint);
}

/// The funnel, in the form every navigation library converges on: walk the windows from the apex,
/// keeping the tightest pair of lines that still reach through all of them, and turn at a corner
/// when the next window falls outside the wedge.
///
/// `windows` are the portals of the polygon chain in walking order plus one last window whose two
/// ends are the goal itself - the trick that makes "the goal is round a corner" the same calculation
/// as "the corridor is round a corner". The wedge starts collapsed on the apex, so the first window
/// sets both of its sides; from then on a window end inside the wedge tightens the side it is on,
/// and a window end that has crossed to the far side of the *other* side means no straight line from
/// the apex reaches both windows any more, which is what makes the walk turn there.
void FunnelUVE(const std::vector<OrientedWindowUVE>& windows, const Math::Vector3UVE& start,
               const Math::Vector3UVE& end, std::vector<Math::Vector3UVE>& outWaypoints) {
    Math::Vector3UVE apex = start;
    Math::Vector3UVE wedgeLeft = start;
    Math::Vector3UVE wedgeRight = start;
    std::size_t apexIndex = 0U;
    std::size_t leftIndex = 0U;
    std::size_t rightIndex = 0U;

    std::size_t index = 0U;
    while (index <= windows.size()) {
        Math::Vector3UVE windowLeft = end;
        Math::Vector3UVE windowRight = end;
        if (index < windows.size()) {
            windowLeft = windows[index].left;
            windowRight = windows[index].right;
            // A window the walk is already standing on constrains nothing - and its wedge would be
            // degenerate, which is how a start position placed exactly in a doorway used to come
            // back with the doorway as a waypoint.
            if (index == 0U) {
                const Math::Vector3UVE span = windowRight - windowLeft;
                const Math::Vector3UVE offset = apex - windowLeft;
                const float spanLengthSquared = LengthSquaredUVE(span);
                if (spanLengthSquared > 0.0F) {
                    const float projection = (offset.x * span.x + offset.z * span.z) / spanLengthSquared;
                    const float clamped = std::min(1.0F, std::max(0.0F, projection));
                    const Math::Vector3UVE closest{windowLeft.x + span.x * clamped, apex.y,
                                                   windowLeft.z + span.z * clamped};
                    if (LengthSquaredUVE(closest - apex) < 1.0e-6F) {
                        ++index;
                        continue;
                    }
                }
            }
        }

        if (TriangleAreaUVE(apex, wedgeRight, windowRight) <= 0.0F) {
            if (SamePointUVE(apex, wedgeRight) || TriangleAreaUVE(apex, wedgeLeft, windowRight) > 0.0F) {
                wedgeRight = windowRight;
                rightIndex = index;
            } else {
                // The window's right end is past the wedge's left side: the walk turns at the left
                // corner, and the window that corner came from is walked again to seed the new wedge.
                AppendWaypointUVE(outWaypoints, start, wedgeLeft);
                apex = wedgeLeft;
                apexIndex = leftIndex;
                wedgeLeft = apex;
                wedgeRight = apex;
                leftIndex = apexIndex;
                rightIndex = apexIndex;
                index = apexIndex + 1U;
                continue;
            }
        }

        if (TriangleAreaUVE(apex, wedgeLeft, windowLeft) >= 0.0F) {
            if (SamePointUVE(apex, wedgeLeft) || TriangleAreaUVE(apex, wedgeRight, windowLeft) < 0.0F) {
                wedgeLeft = windowLeft;
                leftIndex = index;
            } else {
                AppendWaypointUVE(outWaypoints, start, wedgeRight);
                apex = wedgeRight;
                apexIndex = rightIndex;
                wedgeLeft = apex;
                wedgeRight = apex;
                leftIndex = apexIndex;
                rightIndex = apexIndex;
                index = apexIndex + 1U;
                continue;
            }
        }
        ++index;
    }

    AppendWaypointUVE(outWaypoints, start, end);
}

void StringPullUVE(const NavmeshUVE& mesh, const std::vector<NavmeshPortalUVE>& portals,
                   const Math::Vector3UVE& start, const Math::Vector3UVE& end,
                   std::vector<Math::Vector3UVE>& outWaypoints) {
    std::vector<OrientedWindowUVE> windows;
    windows.reserve(portals.size());
    for (const NavmeshPortalUVE& portal : portals) {
        windows.push_back(OrientWindowUVE(mesh, portal));
    }
    FunnelUVE(windows, start, end, outWaypoints);
}

} // namespace

void NavPathUVE::ClearUVE() noexcept {
    waypoints.clear();
    lengthMetres = 0.0F;
    expandedPolygons = 0U;
    status = NavPathStatusUVE::NoMesh;
}

NavPathUVE FindNavPathUVE(const NavmeshUVE& mesh, const NavPathRequestUVE& request) {
    NavPathUVE path{};
    path.ClearUVE();
    if (mesh.IsEmptyUVE()) {
        return path;
    }

    const std::optional<std::pair<std::size_t, Math::Vector3UVE>> start =
        ResolveStartUVE(mesh, request.start, request);
    if (!start.has_value()) {
        path.status = NavPathStatusUVE::StartOffMesh;
        return path;
    }
    const std::optional<std::pair<std::size_t, Math::Vector3UVE>> goal =
        ResolveTargetUVE(mesh, request.target, request);
    if (!goal.has_value()) {
        path.status = NavPathStatusUVE::TargetOffMesh;
        return path;
    }

    const std::size_t startPolygon = start->first;
    const std::size_t goalPolygon = goal->first;
    if (startPolygon == goalPolygon) {
        // One polygon is convex, so the straight line between two of its points is walkable by
        // construction and the path has no corners at all.
        path.waypoints.push_back(goal->second);
        path.lengthMetres = PlanarDistanceUVE(start->second, goal->second);
        path.status = NavPathStatusUVE::Found;
        return path;
    }

    const std::size_t polygonCount = mesh.polygons.size();
    const float infinity = std::numeric_limits<float>::max();
    std::vector<float> cost(polygonCount, infinity);
    std::vector<EntryUVE> enteredThrough(polygonCount);
    std::vector<bool> closed(polygonCount, false);
    std::priority_queue<OpenNodeUVE> open;

    cost[startPolygon] = 0.0F;
    open.push(OpenNodeUVE{startPolygon, PlanarDistanceUVE(mesh.polygons[startPolygon].center, goal->second)});

    std::size_t bestNode = startPolygon;
    float bestRemaining = PlanarDistanceUVE(mesh.polygons[startPolygon].center, goal->second);
    bool reachedGoal = false;

    while (!open.empty()) {
        const std::size_t current = open.top().polygon;
        open.pop();
        if (closed[current]) {
            continue;
        }
        closed[current] = true;
        ++path.expandedPolygons;

        if (current == goalPolygon) {
            reachedGoal = true;
            break;
        }
        if (path.expandedPolygons >= request.maximumExpandedPolygons) {
            break;
        }

        const NavmeshPolygonUVE& polygon = mesh.polygons[current];
        for (std::uint32_t offset = 0U; offset < polygon.portalCount; ++offset) {
            const NavmeshPortalUVE& portal = mesh.portals[polygon.firstPortal + offset];
            const std::size_t neighbour = portal.to;
            if (neighbour >= polygonCount || closed[neighbour] ||
                !IsPolygonUsableUVE(mesh, neighbour, request.navigationLayers)) {
                continue;
            }
            // The cost of stepping through a portal is the distance from where the search already
            // is (the portal it came in by, or the start point itself) to this portal's midpoint -
            // not polygon centre to polygon centre. Measuring the centres makes A* prefer detours
            // through big polygons over the shorter walk through small ones; measuring the portals
            // measures the path a body will actually take.
            const Math::Vector3UVE from = enteredThrough[current].entered
                                              ? PortalMidpointUVE(mesh.portals[enteredThrough[current].portalIndex])
                                              : start->second;
            const Math::Vector3UVE to = PortalMidpointUVE(portal);
            const float tentative = cost[current] + PlanarDistanceUVE(from, to);
            if (tentative >= cost[neighbour]) {
                continue;
            }
            cost[neighbour] = tentative;
            enteredThrough[neighbour].portalIndex = polygon.firstPortal + offset;
            enteredThrough[neighbour].entered = true;
            // The heuristic is the straight line from the portal just crossed to the goal, which is
            // a lower bound on the rest of the walk and so keeps the search both directed and
            // optimal - the sum of the two is never an overestimate of the true cost.
            const float remaining = PlanarDistanceUVE(to, goal->second);
            if (remaining < bestRemaining) {
                bestRemaining = remaining;
                bestNode = neighbour;
            }
            open.push(OpenNodeUVE{neighbour, tentative + remaining});
        }
    }

    // The chain of portals from the start's polygon to the one the search stopped at, in walking
    // order - which is what the funnel expects and what the reverse walk cannot change.
    std::vector<std::uint32_t> portalChain;
    const std::size_t lastPolygon = reachedGoal ? goalPolygon : bestNode;
    for (std::size_t node = lastPolygon; node != startPolygon;) {
        if (!enteredThrough[node].entered) {
            // Unreachable only when the chain was never linked, which cannot happen for a node that
            // was expanded; the guard keeps the walk finite rather than trusting that.
            portalChain.clear();
            break;
        }
        portalChain.push_back(enteredThrough[node].portalIndex);
        node = mesh.portals[enteredThrough[node].portalIndex].from;
    }
    std::reverse(portalChain.begin(), portalChain.end());

    std::vector<NavmeshPortalUVE> portals;
    portals.reserve(portalChain.size());
    for (const std::uint32_t portalIndex : portalChain) {
        portals.push_back(mesh.portals[portalIndex]);
    }

    // Where the path ends: the requested goal when the search reached its polygon, else the closest
    // point to the goal on the polygon it stopped at - a partial path that stopped in mid-air would
    // be worse than one that stops at the nearest walkable ground to what was asked for.
    Math::Vector3UVE endPoint = goal->second;
    if (!reachedGoal) {
        const std::optional<Math::Vector3UVE> clamped = mesh.ProjectPointUVE(goal->second, bestNode);
        endPoint = clamped.has_value() ? *clamped : mesh.PolygonCenterUVE(bestNode);
    }

    StringPullUVE(mesh, portals, start->second, endPoint, path.waypoints);

    float length = PlanarDistanceUVE(start->second, path.waypoints.empty() ? endPoint : path.waypoints.front());
    for (std::size_t waypointIndex = 1U; waypointIndex < path.waypoints.size(); ++waypointIndex) {
        length += PlanarDistanceUVE(path.waypoints[waypointIndex - 1U], path.waypoints[waypointIndex]);
    }
    path.lengthMetres = length;
    path.status = reachedGoal ? NavPathStatusUVE::Found : NavPathStatusUVE::Partial;
    return path;
}

} // namespace UVE::Navigation
