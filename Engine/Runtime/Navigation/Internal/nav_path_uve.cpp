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

/// One state of the search on the frontier, as the priority queue orders it.
struct OpenNodeUVE final {
    std::uint32_t node = 0U;
    float estimatedTotalCost = 0.0F;
};

[[nodiscard]] bool operator<(const OpenNodeUVE& left, const OpenNodeUVE& right) noexcept {
    // `std::priority_queue` is a max-heap, so the ordering is inverted: the cheapest node is the
    // first one out.
    return left.estimatedTotalCost > right.estimatedTotalCost;
}

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

/// The "nothing here" index: a wedge side no window has set yet, or a node with no parent - the
/// start of a chain.
constexpr std::uint32_t kNoneUVE = 0xFFFFFFFFU;

/// Which corner of the wedge a window forced the walk to turn on. The step moves the apex onto that
/// corner; the caller restarts its walk from there.
enum class FunnelTurnUVE : std::uint8_t { None, Left, Right };

/// The funnel's wedge: the lines from the apex through the two corners that still reach every window
/// the walk has crossed, plus the caller's names for the windows that set them.
///
/// The same rules run at two granularities - once over the windows of a whole polygon chain to
/// produce a path, and once a window at a time inside the search to price a chain by the walk an
/// agent would actually take instead of by polygon or portal centres. A "site" is the caller's name
/// for a window: the search names windows by where they sit on the chain it is building, and the
/// straight pull by their place in the window list.
struct FunnelWedgeUVE final {
    Math::Vector3UVE apex{};
    Math::Vector3UVE left{};
    Math::Vector3UVE right{};
    std::uint32_t apexSite = kNoneUVE;
    std::uint32_t leftSite = kNoneUVE;
    std::uint32_t rightSite = kNoneUVE;
};

/// One funnel step - the pair of tests Detour's `findStraightPath` runs for every window, in its
/// order. A window end inside the wedge tightens the side it is on; a window end that has crossed to
/// the far side of the *other* side means no straight line from the apex reaches both windows any
/// more, so the walk turns there: the apex moves onto that corner and the wedge collapses onto it.
[[nodiscard]] FunnelTurnUVE StepFunnelUVE(FunnelWedgeUVE& wedge, const Math::Vector3UVE& windowLeft,
                                          const Math::Vector3UVE& windowRight, const std::uint32_t site) noexcept {
    if (TriangleAreaUVE(wedge.apex, wedge.right, windowRight) <= 0.0F) {
        if (SamePointUVE(wedge.apex, wedge.right) || TriangleAreaUVE(wedge.apex, wedge.left, windowRight) > 0.0F) {
            wedge.right = windowRight;
            wedge.rightSite = site;
        } else {
            wedge.apex = wedge.left;
            wedge.apexSite = wedge.leftSite;
            wedge.left = wedge.apex;
            wedge.right = wedge.apex;
            wedge.leftSite = wedge.apexSite;
            wedge.rightSite = wedge.apexSite;
            return FunnelTurnUVE::Left;
        }
    }
    if (TriangleAreaUVE(wedge.apex, wedge.left, windowLeft) >= 0.0F) {
        if (SamePointUVE(wedge.apex, wedge.left) || TriangleAreaUVE(wedge.apex, wedge.right, windowLeft) < 0.0F) {
            wedge.left = windowLeft;
            wedge.leftSite = site;
        } else {
            wedge.apex = wedge.right;
            wedge.apexSite = wedge.rightSite;
            wedge.left = wedge.apex;
            wedge.right = wedge.apex;
            wedge.leftSite = wedge.apexSite;
            wedge.rightSite = wedge.apexSite;
            return FunnelTurnUVE::Right;
        }
    }
    return FunnelTurnUVE::None;
}

/// Whether a walk whose apex is `apex` is already standing on the window. A window under the walk's
/// feet constrains nothing, and its degenerate wedge would name the doorway itself a waypoint.
[[nodiscard]] bool StandingOnWindowUVE(const Math::Vector3UVE& apex, const Math::Vector3UVE& windowLeft,
                                       const Math::Vector3UVE& windowRight) noexcept {
    const Math::Vector3UVE span = windowRight - windowLeft;
    const Math::Vector3UVE offset = apex - windowLeft;
    const float spanLengthSquared = LengthSquaredUVE(span);
    if (spanLengthSquared <= 0.0F) {
        return false;
    }
    const float projection = (offset.x * span.x + offset.z * span.z) / spanLengthSquared;
    const float clamped = std::min(1.0F, std::max(0.0F, projection));
    const Math::Vector3UVE closest{windowLeft.x + span.x * clamped, apex.y, windowLeft.z + span.z * clamped};
    return LengthSquaredUVE(closest - apex) < 1.0e-6F;
}

/// The planar distance from a point to a window: how close a walk that has to cross that window
/// could get to the point without leaving the window. The search prices a chain with the two
/// distances to the last window it crossed - the walk it cannot have skipped, and the goal it has
/// not reached - which bounds the whole path from below and keeps the frontier pointed at the goal.
[[nodiscard]] float PlanarDistanceToWindowUVE(const Math::Vector3UVE& point, const Math::Vector3UVE& windowLeft,
                                              const Math::Vector3UVE& windowRight) noexcept {
    const Math::Vector3UVE span = windowRight - windowLeft;
    const Math::Vector3UVE offset = point - windowLeft;
    const float spanLengthSquared = LengthSquaredUVE(span);
    if (spanLengthSquared <= 0.0F) {
        return PlanarDistanceUVE(point, windowLeft);
    }
    const float projection = (offset.x * span.x + offset.z * span.z) / spanLengthSquared;
    const float clamped = std::min(1.0F, std::max(0.0F, projection));
    const Math::Vector3UVE closest{windowLeft.x + span.x * clamped, point.y, windowLeft.z + span.z * clamped};
    return PlanarDistanceUVE(point, closest);
}

/// The length of the walk a waypoint list describes, from `start` through every waypoint to `end` -
/// `end` standing in for the whole walk when the list is empty, which is how a path whose corners
/// all collapsed into one straight line is measured.
[[nodiscard]] float PolylineLengthUVE(const Math::Vector3UVE& start, const std::vector<Math::Vector3UVE>& waypoints,
                                      const Math::Vector3UVE& end) noexcept {
    float length = 0.0F;
    Math::Vector3UVE previous = start;
    for (const Math::Vector3UVE& waypoint : waypoints) {
        length += PlanarDistanceUVE(previous, waypoint);
        previous = waypoint;
    }
    if (waypoints.empty()) {
        length += PlanarDistanceUVE(previous, end);
    }
    return length;
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
    FunnelWedgeUVE wedge{};
    wedge.apex = start;
    wedge.left = start;
    wedge.right = start;

    std::size_t index = 0U;
    while (index <= windows.size()) {
        Math::Vector3UVE windowLeft = end;
        Math::Vector3UVE windowRight = end;
        if (index < windows.size()) {
            windowLeft = windows[index].left;
            windowRight = windows[index].right;
            if (index == 0U && StandingOnWindowUVE(wedge.apex, windowLeft, windowRight)) {
                ++index;
                continue;
            }
        }

        if (StepFunnelUVE(wedge, windowLeft, windowRight, static_cast<std::uint32_t>(index)) == FunnelTurnUVE::None) {
            ++index;
            continue;
        }
        // The step moved the apex onto the corner the window forced, and the window that corner came
        // from is walked again to seed the new wedge - Detour's `i = apexIndex`.
        AppendWaypointUVE(outWaypoints, start, wedge.apex);
        index = static_cast<std::size_t>(wedge.apexSite) + 1U;
    }

    AppendWaypointUVE(outWaypoints, start, end);
}

void StringPullUVE(const NavmeshUVE& mesh, const std::vector<std::uint32_t>& portalChain,
                   const Math::Vector3UVE& start, const Math::Vector3UVE& end,
                   std::vector<Math::Vector3UVE>& outWaypoints) {
    std::vector<OrientedWindowUVE> windows;
    windows.reserve(portalChain.size());
    for (const std::uint32_t portalIndex : portalChain) {
        windows.push_back(OrientWindowUVE(mesh, mesh.portals[portalIndex]));
    }
    FunnelUVE(windows, start, end, outWaypoints);
}

/// One state of the search: a chain that crossed a door and pulled a wedge taut behind it. A state is
/// named by the door it crossed and by the corner its funnel turns on, because those two decide the
/// walk it can still take: two chains that crossed the same door but turned at different corners look
/// out at the rest of the level from different places, price it differently, and are dropped for each
/// other at the search's peril - the cheaper of the two is exactly the one a door-only merge loses.
struct SearchNodeUVE final {
    /// How long a walk of this chain is by the time it stands on the wedge's apex - the taut walk
    /// the chain has pulled the agent into, not polygon or portal centres.
    float apexDistance = 0.0F;
    /// How close the window the chain crossed to get here comes to the goal - the goal side of the
    /// lower bound the frontier is ordered by, and how close a partial path got when none arrives.
    float reachedGoalDistance = 0.0F;
    /// `apexDistance` plus the walk out to the window and the walk from it to the goal: no path that
    /// uses this chain can be shorter, which is what tells the search when the frontier cannot beat
    /// the path it already has.
    float estimate = std::numeric_limits<float>::max();
    FunnelWedgeUVE wedge{};
    /// The wedge a state is: the corner its apex stands on and the corner each side runs through,
    /// named the way the mesh names its portal ends - the window that set the corner, and which of
    /// that window's two ends it is, or `kNoneUVE` for the start point, which no window leads into.
    /// Two chains that crossed the same door and turned at the same corner can still hold wedges that
    /// look out at the rest of the level differently, because the windows between the turn and the
    /// door tightened them differently - and the wedge, not the corner, is what decides where the
    /// walk can go next.
    std::uint32_t apex = kNoneUVE;
    std::uint32_t left = kNoneUVE;
    std::uint32_t right = kNoneUVE;
    /// The state this one was reached from, and the portal crossed to get here. A chain is walked back
    /// through these, both to produce the path and to re-walk the windows a turn must see again.
    std::uint32_t parent = kNoneUVE;
    std::uint32_t portal = 0U;
    std::uint32_t depth = 0U;
    /// Set once the state has left the frontier: a state's estimate only ever falls, so the first time
    /// one is taken off the queue it carries the cheapest walk that reaches it.
    bool closed = false;
};

} // namespace

void NavPathUVE::ClearUVE() noexcept {
    waypoints.clear();
    lengthMetres = 0.0F;
    expandedNodes = 0U;
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
    const std::size_t portalCount = mesh.portals.size();
    const float infinity = std::numeric_limits<float>::max();
    // The states the search grows: the start of the chain, and then one per door-and-wedge a chain
    // has crossed and pulled taut. `statesOfDoor` lists the states a door has, so the chain that
    // prices best for one of a door's wedges is the chain that keeps it.
    std::vector<SearchNodeUVE> nodes;
    std::vector<std::vector<std::uint32_t>> statesOfDoor(portalCount);
    nodes.push_back(SearchNodeUVE{});
    const std::uint32_t startNode = 0U;

    const auto polygonOfUVE = [&mesh, &nodes, startPolygon, startNode](const std::uint32_t node) {
        return node == startNode ? startPolygon : static_cast<std::size_t>(mesh.portals[nodes[node].portal].to);
    };

    nodes[startNode].reachedGoalDistance = PlanarDistanceUVE(start->second, goal->second);
    nodes[startNode].estimate = nodes[startNode].reachedGoalDistance;
    nodes[startNode].wedge.apex = start->second;
    nodes[startNode].wedge.left = start->second;
    nodes[startNode].wedge.right = start->second;
    std::priority_queue<OpenNodeUVE> open;
    open.push(OpenNodeUVE{startNode, nodes[startNode].estimate});

    // The chain that reached a state, in walking order - the windows the funnel runs over to give
    // that state its path. Every state was created from a parent, so the walk always reaches the start.
    const auto chainToUVE = [&mesh, &nodes, startNode](const std::uint32_t lastNode,
                                                       std::vector<std::uint32_t>& outChain) {
        outChain.clear();
        for (std::uint32_t node = lastNode; node != startNode; node = nodes[node].parent) {
            outChain.push_back(nodes[node].portal);
        }
        std::reverse(outChain.begin(), outChain.end());
    };

    // The windows of the chain link being priced, plus the window into the neighbour. A turn walks
    // the windows after the apex's own again, so the list is rebuilt from the chain links - and kept
    // across links so a query does not allocate once per portal.
    std::vector<std::uint32_t> sequence;
    std::vector<std::uint32_t> chain;
    std::vector<Math::Vector3UVE> candidateWaypoints;
    std::vector<Math::Vector3UVE> bestWaypoints;
    float bestLength = infinity;
    bool foundGoal = false;
    std::uint32_t bestPartialNode = startNode;
    float bestPartialRemaining = nodes[startNode].reachedGoalDistance;

    while (!open.empty()) {
        const std::uint32_t current = open.top().node;
        open.pop();
        if (nodes[current].closed) {
            continue;
        }
        // The state's own price, not the queue entry's: a state re-priced after its entry was pushed
        // has a cheaper entry waiting in front of this one, so this is the cheapest walk to it.
        const float currentEstimate = nodes[current].estimate;
        if (currentEstimate >= bestLength) {
            // Everything left on the frontier would have to walk at least this far, so none of it
            // can beat the path already in hand.
            break;
        }
        ++path.expandedNodes;

        if (polygonOfUVE(current) == goalPolygon) {
            // A chain reached the goal's own polygon: pull it taut and keep it when it is the best
            // one yet. The search does not stop here - the first chain to arrive is not necessarily
            // the cheapest to walk, which is the whole reason a chain is priced by its funnel.
            chainToUVE(current, chain);
            candidateWaypoints.clear();
            StringPullUVE(mesh, chain, start->second, goal->second, candidateWaypoints);
            const float candidateLength = PolylineLengthUVE(start->second, candidateWaypoints, goal->second);
            foundGoal = true;
            if (candidateLength < bestLength) {
                bestLength = candidateLength;
                bestWaypoints = candidateWaypoints;
            }
            continue;
        }
        nodes[current].closed = true;
        if (path.expandedNodes >= request.maximumExpandedNodes) {
            break;
        }

        const NavmeshPolygonUVE& polygon = mesh.polygons[polygonOfUVE(current)];
        // A copy, not a reference: pricing a link can add a state, and growing the list would leave a
        // reference into it dangling. The mesh is only read, so its references stay valid.
        const SearchNodeUVE state = nodes[current];
        // The window the apex stands on is the last one the chain did not have to turn on; a turn
        // here walks again every window the chain crossed after it.
        const std::size_t firstSite =
            state.wedge.apexSite == kNoneUVE ? 0U : static_cast<std::size_t>(state.wedge.apexSite) + 1U;
        sequence.clear();
        for (std::uint32_t node = current; node != startNode && nodes[node].depth > firstSite;
             node = nodes[node].parent) {
            sequence.push_back(nodes[node].portal);
        }
        std::reverse(sequence.begin(), sequence.end());
        const std::size_t suffixCount = sequence.size();

        for (std::uint32_t offset = 0U; offset < polygon.portalCount; ++offset) {
            const std::uint32_t portalIndex = polygon.firstPortal + offset;
            const std::size_t neighbour = mesh.portals[portalIndex].to;
            if (neighbour >= polygonCount || !IsPolygonUsableUVE(mesh, neighbour, request.navigationLayers)) {
                continue;
            }
            const OrientedWindowUVE newWindow = OrientWindowUVE(mesh, mesh.portals[portalIndex]);
            sequence.push_back(portalIndex);

            // Price this link by the walk it forces: carry the wedge the chain has pulled taut so
            // far over the new window, turning wherever the funnel would turn.
            FunnelWedgeUVE wedge = state.wedge;
            float apexDistance = state.apexDistance;
            std::uint32_t apex = state.apex;
            std::uint32_t left = state.left;
            std::uint32_t right = state.right;
            bool turned = false;
            std::size_t cursor = sequence.size() - 1U;
            std::size_t turns = 0U;
            while (cursor < sequence.size()) {
                const OrientedWindowUVE window =
                    cursor + 1U == sequence.size() ? newWindow : OrientWindowUVE(mesh, mesh.portals[sequence[cursor]]);
                if (cursor == 0U && wedge.apexSite == kNoneUVE &&
                    StandingOnWindowUVE(wedge.apex, window.left, window.right)) {
                    ++cursor;
                    continue;
                }
                const Math::Vector3UVE previousApex = wedge.apex;
                const std::uint32_t site = static_cast<std::uint32_t>(firstSite + cursor);
                if (StepFunnelUVE(wedge, window.left, window.right, site) == FunnelTurnUVE::None) {
                    ++cursor;
                    continue;
                }
                turned = true;
                apexDistance += PlanarDistanceUVE(previousApex, wedge.apex);
                if (++turns > sequence.size()) {
                    // A turn moves the apex onto a later window, so a chain can turn at most once
                    // per window; more than that means the sites and the chain disagree, and leaving
                    // the wedge where it stands beats spinning on it. The path is pulled taut over
                    // the chain afterwards, so a mispriced link can only cost the search a detour.
                    break;
                }
                const std::size_t restart = static_cast<std::size_t>(wedge.apexSite) + 1U;
                cursor = restart > firstSite ? restart - firstSite : 0U;
            }
            // Name a corner of the wedge the way the mesh names its portal ends: the window whose end
            // the corner is, and which of that window's two ends. A site counts windows along the
            // chain, so it only names a corner while this pass still walks that window - the windows
            // before the apex are not walked again and the name their site would give is meaningless -
            // but every name that does come out means the same thing to every later pass, which is
            // what lets a wedge be a state's name and not just its scratch.
            const auto cornerOfUVE = [&mesh, &sequence, firstSite](const std::uint32_t site,
                                                                   const Math::Vector3UVE& corner) {
                if (site == kNoneUVE || site < firstSite) {
                    return kNoneUVE;
                }
                const std::size_t position = static_cast<std::size_t>(site) - firstSite;
                if (position >= sequence.size()) {
                    return kNoneUVE;
                }
                const std::uint32_t window = sequence[position];
                const OrientedWindowUVE named = OrientWindowUVE(mesh, mesh.portals[window]);
                return window * 2U + (SamePointUVE(corner, named.left) ? 0U : 1U);
            };
            // Only a turn moves the apex, so a link that did not turn keeps the name the state came
            // with - recomputing it from the site the apex already had can only name the wrong window.
            if (turned) {
                apex = cornerOfUVE(wedge.apexSite, wedge.apex);
            }
            left = wedge.leftSite == wedge.apexSite ? apex : cornerOfUVE(wedge.leftSite, wedge.left);
            right = wedge.rightSite == wedge.apexSite ? apex : cornerOfUVE(wedge.rightSite, wedge.right);
            sequence.resize(suffixCount);

            const float goalDistance = PlanarDistanceToWindowUVE(goal->second, newWindow.left, newWindow.right);
            // What this chain can be worth: the taut walk to the apex, then the two distances every
            // walk from the apex to the goal adds up to at least - out to the window it still has to
            // cross, and from that window on to the goal.
            const float estimate =
                apexDistance + PlanarDistanceToWindowUVE(wedge.apex, newWindow.left, newWindow.right) + goalDistance;
            // The state this link belongs to: the door it crosses and the wedge it leaves behind. The
            // first chain to price a wedge keeps it until a strictly cheaper chain arrives with the
            // same one - a cheaper chain that wedges differently is a different state, because the
            // wedge, not the door, decides where the walk can go next.
            std::uint32_t target = kNoneUVE;
            for (const std::uint32_t existing : statesOfDoor[portalIndex]) {
                if (nodes[existing].apex == apex && nodes[existing].left == left && nodes[existing].right == right) {
                    target = existing;
                    break;
                }
            }
            if (target == kNoneUVE) {
                target = static_cast<std::uint32_t>(nodes.size());
                nodes.push_back(SearchNodeUVE{});
                statesOfDoor[portalIndex].push_back(target);
            } else if (estimate >= nodes[target].estimate) {
                continue;
            }
            if (nodes[target].closed) {
                // A cheaper chain reaching a state that was already expanded: the wedge it comes with
                // is not the one the state's links were priced with, so the state has to be walked
                // again. Without this the search keeps the successors of the chain it replaced.
                nodes[target].closed = false;
            }
            nodes[target].apexDistance = apexDistance;
            nodes[target].reachedGoalDistance = goalDistance;
            nodes[target].estimate = estimate;
            nodes[target].wedge = wedge;
            nodes[target].apex = apex;
            nodes[target].left = left;
            nodes[target].right = right;
            nodes[target].parent = current;
            nodes[target].portal = portalIndex;
            nodes[target].depth = state.depth + 1U;
            if (goalDistance < bestPartialRemaining) {
                bestPartialRemaining = goalDistance;
                bestPartialNode = target;
            }
            open.push(OpenNodeUVE{target, estimate});
        }
    }

    // Where the path ends: the requested goal when a chain reached its polygon, else the closest
    // point to the goal on the polygon the search got nearest - a partial path that stopped in
    // mid-air would be worse than one that stops at the nearest walkable ground to what was asked
    // for.
    Math::Vector3UVE endPoint = goal->second;
    if (foundGoal) {
        path.waypoints = bestWaypoints;
        path.status = NavPathStatusUVE::Found;
    } else {
        const std::size_t bestPolygon = polygonOfUVE(bestPartialNode);
        chainToUVE(bestPartialNode, chain);
        const std::optional<Math::Vector3UVE> clamped = mesh.ProjectPointUVE(goal->second, bestPolygon);
        endPoint = clamped.has_value() ? *clamped : mesh.PolygonCenterUVE(bestPolygon);
        StringPullUVE(mesh, chain, start->second, endPoint, path.waypoints);
        path.status = NavPathStatusUVE::Partial;
    }
    path.lengthMetres = PolylineLengthUVE(start->second, path.waypoints, endPoint);
    return path;
}

} // namespace UVE::Navigation
