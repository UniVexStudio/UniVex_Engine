// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Tests for the *length* of the path a search returns, not just for whether it stays on the mesh: an
// implementation can hug every polygon boundary, never leave the walkable region, and still lead an
// agent the long way round a room. Only a reference that knows the region can tell those two apart,
// so these tests carry one.
//
// The reference is a visibility graph over the region's corners. Two corners are joined when the
// segment between them stays inside the region: the segment is split at every rectangle-edge crossing
// and each piece's midpoint is tested against the closed union. Dijkstra over that graph is the
// shortest walk a point agent could take, so it is a yardstick - not another implementation of the
// same search, and not a tolerance around one. The closed union is the right set because the mesh is
// the surface an agent's *centre* may occupy (the bake erodes it by the agent's radius), so a centre
// path that runs along a polygon boundary is exactly a path an agent can walk. The one place the
// region and the mesh disagree on purpose is a pinch: two rectangles meeting at a single point are a
// zero-width passage, the bake writes no portal there, and the reference refuses to turn on one (see
// RegionIsPinchedUVE).
//
// The graph is built twice, over two node sets, because the two answer different questions:
//
//  * Every corner of the region's rectangles. This is the strongest walk that exists, and the one a
//    fuzzer comparing against it is entitled to call "the shortest".
//  * Only the corners the mesh's doors end on - the ends of its portals. A path through a navmesh is
//    a taut path through a sequence of polygons, so it turns where polygons meet, and a door end is
//    the only place two polygons meet *without* the path changing polygon. A partition corner that
//    is not a door end is a corner of the mesh's *bookkeeping*: the region has a corner there, but
//    neither of the polygons that own it shares a door with the other, so no taut path through the
//    mesh can bend on it. A search that is optimal for the mesh it was given matches this reference
//    exactly, which is what the sweeps below lock in; the gap to the stronger reference is the
//    mesh's own expressiveness, not the search's.
//
// The maps come from a grid of unit cells at an occupancy, reduced to its largest 4-connected
// component and merged into rectangles, so a trial is a maze with no islands and no diagonal
// contacts to argue about. A sweep reports, over every reachable trial, how much longer the search's
// path is than the reference's. Measured on the two sweeps this file runs, against a search that
// priced its chains by the distance between portal centres - the cost model Recast's Detour, and
// with it most engines, uses:
//
//   sweep                    priced by portal centres  this search, vs door ends  vs every corner
//   8x8  @ 0.72, 300 trials  14 over 5 cm, 1.080 m       0 over 5 cm, 0.000 m       0 over 5 cm, 0.000 m
//   12x12 @ 0.72, 150 trials 13 over 5 cm, 1.493 m       0 over 5 cm, 0.000 m       0 over 5 cm, 0.000 m
//
// What the sweeps have to hold, then, is both: the search returns the cheapest walk the mesh can
// express, matching the door-end reference on every trial it answers, and it gives the strongest
// reference - the shortest walk the region admits at all - nothing beyond floating-point noise. The
// bounds below are the measured values with room for a different floating-point unit.

#include "uve/navigation/nav_path_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Navigation::Tests {
namespace {

using Math::Vector3UVE;
using Math::AabbUVE;

/// The generator's randomness: splitmix64 seeded with the trial's own number, so a sweep is a fixed
/// sequence of mazes and a failure can be re-run by its trial index alone.
class MazeRngUVE final {
public:
    explicit MazeRngUVE(const std::uint64_t seed) noexcept : m_state(seed) {}

    [[nodiscard]] std::uint64_t NextUVE() noexcept {
        m_state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t value = m_state;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
        return value ^ (value >> 31);
    }

    /// A value in [0, 1).
    [[nodiscard]] float UnitUVE() noexcept {
        return static_cast<float>(NextUVE() >> 40) / static_cast<float>(1U << 24);
    }

private:
    std::uint64_t m_state;
};

struct RectangleUVE final {
    float x0 = 0.0F;
    float z0 = 0.0F;
    float x1 = 0.0F;
    float z1 = 0.0F;
};

struct PointUVE final {
    float x = 0.0F;
    float z = 0.0F;
};

/// The reach of the two reference tolerances: how close a point must be to count as on a rectangle,
/// and how close two crossings must be to count as the same one.
constexpr float kOnEdgeUVE = 1.0e-4F;
constexpr float kSameUVE = 1.0e-6F;

[[nodiscard]] bool RegionContainsUVE(const std::vector<RectangleUVE>& rectangles, const float x, const float z) {
    for (const RectangleUVE& rectangle : rectangles) {
        if (x >= rectangle.x0 - kOnEdgeUVE && x <= rectangle.x1 + kOnEdgeUVE && z >= rectangle.z0 - kOnEdgeUVE &&
            z <= rectangle.z1 + kOnEdgeUVE) {
            return true;
        }
    }
    return false;
}

/// True when the region around (x, z) is pinched: the union meets a small circle around the point in
/// more than one arc. A diagonal contact between two rectangles is exactly that - two opposite
/// quarter-arcs - and a path may not turn there, because walking through the point needs a zero-width
/// body. A convex corner, a straight wall and a reflex corner are one arc each, however sharp, and
/// those are places a path may bend.
[[nodiscard]] bool RegionIsPinchedUVE(const std::vector<RectangleUVE>& rectangles, const float x, const float z) {
    constexpr int kSamplesUVE = 32;
    constexpr float kRadiusUVE = 0.02F;
    constexpr float kTwoPiUVE = 6.28318530717958647692F;
    std::vector<bool> inside(static_cast<std::size_t>(kSamplesUVE), false);
    for (int sample = 0; sample < kSamplesUVE; ++sample) {
        const float angle = (static_cast<float>(sample) / static_cast<float>(kSamplesUVE)) * kTwoPiUVE;
        inside[static_cast<std::size_t>(sample)] =
            RegionContainsUVE(rectangles, x + kRadiusUVE * std::cos(angle), z + kRadiusUVE * std::sin(angle));
    }
    int arcs = 0;
    for (int sample = 0; sample < kSamplesUVE; ++sample) {
        const bool previous = inside[static_cast<std::size_t>((sample + kSamplesUVE - 1) % kSamplesUVE)];
        if (inside[static_cast<std::size_t>(sample)] && !previous) {
            ++arcs;
        }
    }
    // Nothing around it at all is not a pinched passage, it is a point outside the region.
    return arcs > 1;
}

/// Every pinched point of the region: corners of its rectangles where the union meets a small circle
/// around the point in more than one arc, which is a passage no body can walk through however thin
/// the search is willing to be about it.
[[nodiscard]] std::vector<PointUVE> RegionPinchPointsUVE(const std::vector<RectangleUVE>& rectangles) {
    std::vector<PointUVE> pinches;
    const auto addUVE = [&pinches, &rectangles](const float x, const float z) {
        for (const PointUVE& pinch : pinches) {
            if (std::fabs(pinch.x - x) <= kOnEdgeUVE && std::fabs(pinch.z - z) <= kOnEdgeUVE) {
                return;
            }
        }
        if (RegionIsPinchedUVE(rectangles, x, z)) {
            pinches.push_back(PointUVE{x, z});
        }
    };
    for (const RectangleUVE& rectangle : rectangles) {
        addUVE(rectangle.x0, rectangle.z0);
        addUVE(rectangle.x1, rectangle.z0);
        addUVE(rectangle.x0, rectangle.z1);
        addUVE(rectangle.x1, rectangle.z1);
    }
    return pinches;
}

/// True when the segment stays on the walkable region: split at every rectangle-edge crossing, test
/// each piece's midpoint against the union, and refuse a crossing that lands on a pinch.
[[nodiscard]] bool LegStaysInsideRegionUVE(const std::vector<RectangleUVE>& rectangles,
                                           const std::vector<PointUVE>& pinchPoints, const float ax, const float az,
                                           const float bx, const float bz) {
    if (std::fabs(bx - ax) <= kSameUVE && std::fabs(bz - az) <= kSameUVE) {
        return RegionContainsUVE(rectangles, ax, az);
    }
    std::vector<float> crossings{0.0F, 1.0F};
    const auto addCrossingUVE = [&crossings](const float t) {
        if (t > kSameUVE && t < 1.0F - kSameUVE) {
            crossings.push_back(t);
        }
    };
    for (const RectangleUVE& rectangle : rectangles) {
        for (const float x : {rectangle.x0, rectangle.x1}) {
            if (std::fabs(bx - ax) > 1.0e-9F) {
                const float t = (x - ax) / (bx - ax);
                if (t > 0.0F && t < 1.0F) {
                    const float z = az + (bz - az) * t;
                    if (z >= rectangle.z0 - kOnEdgeUVE && z <= rectangle.z1 + kOnEdgeUVE) {
                        addCrossingUVE(t);
                    }
                }
            }
        }
        for (const float z : {rectangle.z0, rectangle.z1}) {
            if (std::fabs(bz - az) > 1.0e-9F) {
                const float t = (z - az) / (bz - az);
                if (t > 0.0F && t < 1.0F) {
                    const float x = ax + (bx - ax) * t;
                    if (x >= rectangle.x0 - kOnEdgeUVE && x <= rectangle.x1 + kOnEdgeUVE) {
                        addCrossingUVE(t);
                    }
                }
            }
        }
    }
    std::sort(crossings.begin(), crossings.end());
    // Two rectangles sharing an edge contribute the same crossing twice, and a zero-length interval
    // has a "midpoint" that lies exactly on the boundary - which the test below rightly refuses. One
    // crossing is one split.
    crossings.erase(std::unique(crossings.begin(), crossings.end(),
                                [](const float left, const float right) { return std::fabs(left - right) <= kSameUVE; }),
                    crossings.end());
    for (const float t : crossings) {
        if (t <= kSameUVE || t >= 1.0F - kSameUVE) {
            continue;
        }
        const float x = ax + (bx - ax) * t;
        const float z = az + (bz - az) * t;
        for (const PointUVE& pinch : pinchPoints) {
            if (std::fabs(x - pinch.x) <= kOnEdgeUVE && std::fabs(z - pinch.z) <= kOnEdgeUVE) {
                return false;
            }
        }
    }
    for (std::size_t crossing = 1U; crossing < crossings.size(); ++crossing) {
        const float middle = (crossings[crossing - 1U] + crossings[crossing]) * 0.5F;
        if (!RegionContainsUVE(rectangles, ax + (bx - ax) * middle, az + (bz - az) * middle)) {
            return false;
        }
    }
    return true;
}

/// Which corners a reference walk is allowed to turn on.
enum class ReferenceCornersUVE : std::uint8_t {
    /// Every corner of the region's rectangles. The strongest walk that exists, and the one to call
    /// "the shortest" when asking how much of the level's own geometry an answer gives away.
    EveryCornerOfTheRegion,
    /// Only the corners the mesh's doors end on. A taut path through a mesh turns at doors, so this
    /// is the shortest walk the mesh can express at all, and the one a search that is optimal for the
    /// mesh it was given has to match exactly.
    DoorEndsOnly,
};

/// The shortest walk through the region, by Dijkstra over the chosen corners plus the two ends.
/// Returns `infinity` when the two ends are not connected through the region.
[[nodiscard]] float ReferencePathLengthUVE(const std::vector<RectangleUVE>& rectangles, const NavmeshUVE& mesh,
                                           const ReferenceCornersUVE corners, const float ax, const float az,
                                           const float bx, const float bz) {
    std::vector<PointUVE> nodes;
    if (corners == ReferenceCornersUVE::EveryCornerOfTheRegion) {
        nodes.reserve(rectangles.size() * 4U + 2U);
        for (const RectangleUVE& rectangle : rectangles) {
            nodes.push_back(PointUVE{rectangle.x0, rectangle.z0});
            nodes.push_back(PointUVE{rectangle.x1, rectangle.z0});
            nodes.push_back(PointUVE{rectangle.x0, rectangle.z1});
            nodes.push_back(PointUVE{rectangle.x1, rectangle.z1});
        }
    } else {
        nodes.reserve(mesh.portals.size() * 2U + 2U);
        for (const NavmeshPortalUVE& portal : mesh.portals) {
            nodes.push_back(PointUVE{portal.left.x, portal.left.z});
            nodes.push_back(PointUVE{portal.right.x, portal.right.z});
        }
    }
    const std::size_t startIndex = nodes.size();
    nodes.push_back(PointUVE{ax, az});
    const std::size_t goalIndex = nodes.size();
    nodes.push_back(PointUVE{bx, bz});

    // A pinched point is not a place a path may turn or cross - it is the whole reason the mesh has no
    // portal there - and the set of them belongs to the region, not to the node set a reference was
    // built over: a reference that only knew the pinches its own nodes sat on would be free to cut
    // through every other one, and would then call the result a shorter walk.
    const std::vector<PointUVE> pinchPoints = RegionPinchPointsUVE(rectangles);
    std::vector<bool> usable(nodes.size(), true);
    for (std::size_t node = 0U; node < startIndex; ++node) {
        for (const PointUVE& pinch : pinchPoints) {
            if (std::fabs(pinch.x - nodes[node].x) <= kOnEdgeUVE && std::fabs(pinch.z - nodes[node].z) <= kOnEdgeUVE) {
                usable[node] = false;
                break;
            }
        }
    }

    const std::size_t count = nodes.size();
    const float infinity = std::numeric_limits<float>::max();
    std::vector<float> distance(count, infinity);
    std::vector<bool> settled(count, false);
    distance[startIndex] = 0.0F;
    for (std::size_t iteration = 0U; iteration < count; ++iteration) {
        std::size_t best = count;
        float bestDistance = infinity;
        for (std::size_t node = 0U; node < count; ++node) {
            if (!settled[node] && distance[node] < bestDistance) {
                bestDistance = distance[node];
                best = node;
            }
        }
        if (best == count || best == goalIndex) {
            break;
        }
        settled[best] = true;
        for (std::size_t other = 0U; other < count; ++other) {
            if (settled[other] || !usable[other]) {
                continue;
            }
            const float deltaX = nodes[other].x - nodes[best].x;
            const float deltaZ = nodes[other].z - nodes[best].z;
            const float step = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
            if (step <= 0.0F) {
                continue;
            }
            const float candidate = distance[best] + step;
            if (candidate >= distance[other]) {
                continue;
            }
            if (!LegStaysInsideRegionUVE(rectangles, pinchPoints, nodes[best].x, nodes[best].z, nodes[other].x,
                                         nodes[other].z)) {
                continue;
            }
            distance[other] = candidate;
        }
    }
    return distance[goalIndex];
}

/// Builds the navmesh the bake would write for a set of rectangles: one polygon per rectangle, and a
/// portal in both directions between every pair of rectangles sharing part of an edge.
[[nodiscard]] NavmeshUVE BuildMazeMeshUVE(const std::vector<RectangleUVE>& rectangles) {
    NavmeshUVE mesh{};
    mesh.cellSize = 1.0F;
    mesh.agentRadius = 0.0F;
    mesh.agentHeight = 2.0F;
    mesh.maximumStepHeight = 0.4F;
    mesh.navigationLayers = 1U;
    mesh.bounds = AabbUVE{Vector3UVE{-8.0F, -1.0F, -8.0F}, Vector3UVE{64.0F, 1.0F, 64.0F}};
    for (const RectangleUVE& rectangle : rectangles) {
        NavmeshPolygonUVE polygon{};
        polygon.vertexCount = 4U;
        polygon.vertices[0] = Vector3UVE{rectangle.x0, 0.0F, rectangle.z0};
        polygon.vertices[1] = Vector3UVE{rectangle.x1, 0.0F, rectangle.z0};
        polygon.vertices[2] = Vector3UVE{rectangle.x1, 0.0F, rectangle.z1};
        polygon.vertices[3] = Vector3UVE{rectangle.x0, 0.0F, rectangle.z1};
        polygon.center = Vector3UVE{(rectangle.x0 + rectangle.x1) * 0.5F, 0.0F, (rectangle.z0 + rectangle.z1) * 0.5F};
        // Axis-aligned, so the signed area is the product of the sides - the same number the bake
        // writes, and what the mesh's nearest-polygon prune reads.
        polygon.areaSquareMetres = (rectangle.x1 - rectangle.x0) * (rectangle.z1 - rectangle.z0);
        polygon.navigationLayers = 1U;
        mesh.polygons.push_back(polygon);
    }

    std::vector<std::vector<NavmeshPortalUVE>> perPolygon(mesh.polygons.size());
    const auto connectUVE = [&mesh, &perPolygon](const std::size_t from, const std::size_t to, const Vector3UVE end0,
                                                 const Vector3UVE end1) {
        const float lengthSquared = (end1.x - end0.x) * (end1.x - end0.x) + (end1.z - end0.z) * (end1.z - end0.z);
        if (lengthSquared <= kSameUVE) {
            return;
        }
        // The portal's ends are ordered by the walker's own left and right: the mesh stores them the
        // way the bake writes them, and the pathfinder re-derives the order anyway, but a test mesh
        // that stored them backwards would be testing a mesh the bake never produces.
        const Vector3UVE fromCenter = mesh.polygons[from].center;
        const Vector3UVE toCenter = mesh.polygons[to].center;
        const float side = (toCenter.x - fromCenter.x) * (end0.z - fromCenter.z) -
                           (toCenter.z - fromCenter.z) * (end0.x - fromCenter.x);
        NavmeshPortalUVE portal{};
        portal.from = static_cast<std::uint32_t>(from);
        portal.to = static_cast<std::uint32_t>(to);
        portal.left = side >= 0.0F ? end0 : end1;
        portal.right = side >= 0.0F ? end1 : end0;
        perPolygon[from].push_back(portal);
    };

    for (std::size_t first = 0U; first < rectangles.size(); ++first) {
        for (std::size_t second = first + 1U; second < rectangles.size(); ++second) {
            const RectangleUVE& a = rectangles[first];
            const RectangleUVE& b = rectangles[second];
            if (std::fabs(a.x1 - b.x0) <= kOnEdgeUVE || std::fabs(b.x1 - a.x0) <= kOnEdgeUVE) {
                const float shared = std::fabs(a.x1 - b.x0) <= kOnEdgeUVE ? a.x1 : a.x0;
                const float low = std::max(a.z0, b.z0);
                const float high = std::min(a.z1, b.z1);
                if (high - low > kOnEdgeUVE) {
                    connectUVE(first, second, Vector3UVE{shared, 0.0F, low}, Vector3UVE{shared, 0.0F, high});
                    connectUVE(second, first, Vector3UVE{shared, 0.0F, low}, Vector3UVE{shared, 0.0F, high});
                }
            } else if (std::fabs(a.z1 - b.z0) <= kOnEdgeUVE || std::fabs(b.z1 - a.z0) <= kOnEdgeUVE) {
                const float shared = std::fabs(a.z1 - b.z0) <= kOnEdgeUVE ? a.z1 : a.z0;
                const float low = std::max(a.x0, b.x0);
                const float high = std::min(a.x1, b.x1);
                if (high - low > kOnEdgeUVE) {
                    connectUVE(first, second, Vector3UVE{low, 0.0F, shared}, Vector3UVE{high, 0.0F, shared});
                    connectUVE(second, first, Vector3UVE{low, 0.0F, shared}, Vector3UVE{high, 0.0F, shared});
                }
            }
        }
    }

    std::vector<NavmeshPortalUVE> portals;
    for (std::size_t index = 0U; index < mesh.polygons.size(); ++index) {
        mesh.polygons[index].firstPortal = static_cast<std::uint32_t>(portals.size());
        mesh.polygons[index].portalCount = static_cast<std::uint32_t>(perPolygon[index].size());
        for (const NavmeshPortalUVE& portal : perPolygon[index]) {
            portals.push_back(portal);
        }
    }
    mesh.portals = std::move(portals);
    return mesh;
}

/// One generated maze: a mesh, two cell centres on it, and the two shortest walks between them - the
/// one over every corner of the region, and the one over the corners the mesh's doors end on.
struct MazeUVE final {
    std::vector<RectangleUVE> rectangles;
    NavmeshUVE mesh;
    PointUVE start{};
    PointUVE goal{};
    float referenceLengthEveryCorner = std::numeric_limits<float>::max();
    float referenceLengthDoorEnds = std::numeric_limits<float>::max();
};

/// Generates one maze on a grid of unit cells: keep the cells at this occupancy, drop everything but
/// the largest 4-connected component, then merge the runs of kept cells into rectangles.
///
/// The 4-connected reduction is what keeps a trial about pathfinding: cells that touch only at a
/// corner are not a passage, and a maze made of islands would spend the trial proving that. Merging
/// the runs is what keeps the meshes small enough for the reference's all-pairs visibility work.
[[nodiscard]] bool GenerateMazeUVE(MazeRngUVE& rng, const int width, const int height, const float occupancy,
                                   MazeUVE& outMaze) {
    const std::size_t cellCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<bool> cells(cellCount, false);
    for (std::size_t cell = 0U; cell < cellCount; ++cell) {
        cells[cell] = rng.UnitUVE() < occupancy;
    }
    const auto cellAtUVE = [&cells, width](const int x, const int z) {
        return cells[static_cast<std::size_t>(z) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
    };

    std::vector<int> component(cellCount, -1);
    std::vector<std::size_t> largest;
    int label = -1;
    for (std::size_t seed = 0U; seed < cellCount; ++seed) {
        if (!cells[seed] || component[seed] >= 0) {
            continue;
        }
        ++label;
        std::vector<std::size_t> stack{seed};
        std::vector<std::size_t> members{seed};
        component[seed] = label;
        while (!stack.empty()) {
            const std::size_t cell = stack.back();
            stack.pop_back();
            const int x = static_cast<int>(cell % static_cast<std::size_t>(width));
            const int z = static_cast<int>(cell / static_cast<std::size_t>(width));
            for (const std::pair<int, int>& step : {std::pair<int, int>{x - 1, z}, {x + 1, z}, {x, z - 1}, {x, z + 1}}) {
                const int nextX = step.first;
                const int nextZ = step.second;
                if (nextX < 0 || nextX >= width || nextZ < 0 || nextZ >= height) {
                    continue;
                }
                const std::size_t neighbour =
                    static_cast<std::size_t>(nextZ) * static_cast<std::size_t>(width) + static_cast<std::size_t>(nextX);
                if (cells[neighbour] && component[neighbour] < 0) {
                    component[neighbour] = label;
                    stack.push_back(neighbour);
                    members.push_back(neighbour);
                }
            }
        }
        if (members.size() > largest.size()) {
            largest = std::move(members);
        }
    }
    // A handful of cells cannot hold a path worth measuring against a reference.
    if (largest.size() < 8U) {
        return false;
    }
    std::vector<bool> keep(cellCount, false);
    for (const std::size_t cell : largest) {
        keep[cell] = true;
    }
    cells = std::move(keep);

    struct RunUVE final {
        int x0 = -1;
        int x1 = -1;
    };
    std::vector<std::vector<RunUVE>> runsPerRow(static_cast<std::size_t>(height));
    for (int row = 0; row < height; ++row) {
        int column = 0;
        while (column < width) {
            if (!cellAtUVE(column, row)) {
                ++column;
                continue;
            }
            const int runStart = column;
            while (column < width && cellAtUVE(column, row)) {
                ++column;
            }
            runsPerRow[static_cast<std::size_t>(row)].push_back(RunUVE{runStart, column});
        }
    }
    for (int row = 0; row < height; ++row) {
        for (RunUVE& run : runsPerRow[static_cast<std::size_t>(row)]) {
            if (run.x0 < 0) {
                continue;
            }
            int top = row;
            for (int next = row + 1; next < height; ++next) {
                bool matched = false;
                for (RunUVE& candidate : runsPerRow[static_cast<std::size_t>(next)]) {
                    if (candidate.x0 == run.x0 && candidate.x1 == run.x1) {
                        candidate.x0 = -1;
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    break;
                }
                top = next;
            }
            outMaze.rectangles.push_back(RectangleUVE{static_cast<float>(run.x0), static_cast<float>(row),
                                                      static_cast<float>(run.x1), static_cast<float>(top + 1)});
            run.x0 = -1;
        }
    }
    outMaze.mesh = BuildMazeMeshUVE(outMaze.rectangles);

    const std::size_t startCell = largest[rng.NextUVE() % largest.size()];
    const std::size_t goalCell = largest[rng.NextUVE() % largest.size()];
    if (startCell == goalCell) {
        return false;
    }
    outMaze.start = PointUVE{static_cast<float>(startCell % static_cast<std::size_t>(width)) + 0.5F,
                             static_cast<float>(startCell / static_cast<std::size_t>(width)) + 0.5F};
    outMaze.goal = PointUVE{static_cast<float>(goalCell % static_cast<std::size_t>(width)) + 0.5F,
                            static_cast<float>(goalCell / static_cast<std::size_t>(width)) + 0.5F};
    const float doorEnds = ReferencePathLengthUVE(outMaze.rectangles, outMaze.mesh, ReferenceCornersUVE::DoorEndsOnly,
                                                  outMaze.start.x, outMaze.start.z, outMaze.goal.x, outMaze.goal.z);
    if (!(doorEnds < std::numeric_limits<float>::max())) {
        return false;  // the two cells are on the map but not connected through it
    }
    const float everyCorner =
        ReferencePathLengthUVE(outMaze.rectangles, outMaze.mesh, ReferenceCornersUVE::EveryCornerOfTheRegion,
                               outMaze.start.x, outMaze.start.z, outMaze.goal.x, outMaze.goal.z);
    outMaze.referenceLengthDoorEnds = doorEnds;
    // Turning is never worse with more corners to turn on, so the strongest walk is the shorter one.
    outMaze.referenceLengthEveryCorner = std::min(doorEnds, everyCorner);
    return true;
}

/// What one sweep of generated mazes measured, against both references.
struct MazeSweepUVE final {
    std::size_t reachable = 0U;
    /// Trials where the returned path left the mesh - the search being wrong, not merely slow.
    std::size_t invalid = 0U;
    /// Trials where the search answered a reachable request with something unusable.
    std::size_t unanswered = 0U;
    /// Trials where the returned path was *shorter* than the shortest walk the mesh can express,
    /// which can only mean the reference is wrong or the path left the region.
    std::size_t shorterThanTheWalk = 0U;
    /// Against the walk a taut path through the mesh can take: this is where the search itself shows
    /// up, and it has to be zero.
    std::size_t overFiveCentimetresAgainstDoorEnds = 0U;
    float meanDeltaAgainstDoorEnds = 0.0F;
    float worstDeltaAgainstDoorEnds = 0.0F;
    /// Against the strongest walk in the region: the extra is the mesh's own expressiveness, which no
    /// search over the mesh's doors can recover.
    std::size_t overFiveCentimetresAgainstEveryCorner = 0U;
    float worstDeltaAgainstEveryCorner = 0.0F;
};

/// Runs `trials` generated mazes and measures the returned path against the reference walk. A path is
/// sampled along every leg to check it never leaves the mesh, because a shorter-than-reference path
/// is exactly what cutting a corner of the region would look like.
[[nodiscard]] MazeSweepUVE MeasureMazeSweepUVE(const std::size_t trials, const int width, const int height,
                                               const float occupancy) {
    constexpr int kSamplesPerLegUVE = 64;
    MazeSweepUVE sweep{};
    double totalDelta = 0.0;
    for (std::size_t trial = 0U; trial < trials; ++trial) {
        MazeRngUVE rng(static_cast<std::uint64_t>(trial));
        MazeUVE maze{};
        if (!GenerateMazeUVE(rng, width, height, occupancy, maze)) {
            continue;
        }
        ++sweep.reachable;

        NavPathRequestUVE request{};
        request.start = Vector3UVE{maze.start.x, 0.0F, maze.start.z};
        request.target = Vector3UVE{maze.goal.x, 0.0F, maze.goal.z};
        const NavPathUVE path = FindNavPathUVE(maze.mesh, request);
        if (!path.IsUsableUVE()) {
            ++sweep.unanswered;
            ++sweep.invalid;
            continue;
        }
        bool onMesh = true;
        Vector3UVE previous = request.start;
        for (const Vector3UVE& waypoint : path.waypoints) {
            for (int sample = 0; sample <= kSamplesPerLegUVE; ++sample) {
                const float t = static_cast<float>(sample) / static_cast<float>(kSamplesPerLegUVE);
                const Vector3UVE point{previous.x + (waypoint.x - previous.x) * t, 0.0F,
                                       previous.z + (waypoint.z - previous.z) * t};
                if (!maze.mesh.FindPolygonUVE(point).has_value()) {
                    onMesh = false;
                    break;
                }
            }
            previous = waypoint;
            if (!onMesh) {
                break;
            }
        }
        if (!onMesh) {
            ++sweep.invalid;
            continue;
        }

        const float againstDoorEnds = path.lengthMetres - maze.referenceLengthDoorEnds;
        const float againstEveryCorner = path.lengthMetres - maze.referenceLengthEveryCorner;
        if (againstDoorEnds < -0.05F) {
            ++sweep.shorterThanTheWalk;
        }
        if (againstDoorEnds > 0.05F) {
            ++sweep.overFiveCentimetresAgainstDoorEnds;
        }
        if (againstEveryCorner > 0.05F) {
            ++sweep.overFiveCentimetresAgainstEveryCorner;
        }
        if (againstDoorEnds > sweep.worstDeltaAgainstDoorEnds) {
            sweep.worstDeltaAgainstDoorEnds = againstDoorEnds;
        }
        if (againstEveryCorner > sweep.worstDeltaAgainstEveryCorner) {
            sweep.worstDeltaAgainstEveryCorner = againstEveryCorner;
        }
        totalDelta += static_cast<double>(againstDoorEnds);
    }
    if (sweep.reachable > 0U) {
        sweep.meanDeltaAgainstDoorEnds = static_cast<float>(totalDelta / static_cast<double>(sweep.reachable));
    }
    return sweep;
}

/// What every sweep has to hold, whichever map it walked: the path is usable, it stays on the mesh,
/// and it is never shorter than the true shortest walk through the region.
void ExpectSweepIsSoundUVE(const MazeSweepUVE& sweep) {
    EXPECT_GT(sweep.reachable, 0U) << "the sweep has to reach something to measure";
    EXPECT_EQ(sweep.invalid, 0U) << "every returned path stays on the mesh";
    EXPECT_EQ(sweep.unanswered, 0U) << "a reachable request gets an answer";
    EXPECT_EQ(sweep.shorterThanTheWalk, 0U)
        << "a path shorter than the shortest walk the mesh can express would have left the region";
}

}  // namespace

TEST(NavPathQualityUVETest, MazePathsStayCloseToTheShortestWalkThroughTheRegion) {
    // A dense small maze: 300 unit cells at 72% occupancy, the case where a chain of rooms offers a
    // search plenty of ways to commit to the wrong corridor.
    const MazeSweepUVE sweep = MeasureMazeSweepUVE(300U, 8, 8, 0.72F);
    ExpectSweepIsSoundUVE(sweep);
    EXPECT_EQ(sweep.overFiveCentimetresAgainstDoorEnds, 0U)
        << "the search returns the cheapest walk the mesh can express, not merely a sound one";
    EXPECT_LT(sweep.meanDeltaAgainstDoorEnds, 0.005F) << "and it does so on the average trial";
    EXPECT_LT(sweep.worstDeltaAgainstDoorEnds, 0.05F) << "the worst miss against the mesh's own walk";
    EXPECT_EQ(sweep.overFiveCentimetresAgainstEveryCorner, 0U)
        << "and neither may it give away five centimetres to the strongest walk that exists";
    EXPECT_LT(sweep.worstDeltaAgainstEveryCorner, 0.05F)
        << "the margin behind that count, which the region's own corners set";
}

TEST(NavPathQualityUVETest, LargerMazePathsKeepTheSameQuality) {
    // The same measurement on a map four times the area: the more rooms a chain can be routed
    // through, the more the search's own bounds have to hold up.
    const MazeSweepUVE sweep = MeasureMazeSweepUVE(150U, 12, 12, 0.72F);
    ExpectSweepIsSoundUVE(sweep);
    EXPECT_EQ(sweep.overFiveCentimetresAgainstDoorEnds, 0U)
        << "the search returns the cheapest walk the mesh can express, not merely a sound one";
    EXPECT_LT(sweep.meanDeltaAgainstDoorEnds, 0.005F) << "and it does so on the average trial";
    EXPECT_LT(sweep.worstDeltaAgainstDoorEnds, 0.05F) << "the worst miss against the mesh's own walk";
    EXPECT_EQ(sweep.overFiveCentimetresAgainstEveryCorner, 0U)
        << "and neither may it give away five centimetres to the strongest walk that exists";
    EXPECT_LT(sweep.worstDeltaAgainstEveryCorner, 0.05F)
        << "the margin behind that count, which the region's own corners set";
}

}  // namespace UVE::Navigation::Tests
