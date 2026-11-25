// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Tests for the pathfinder: which polygons a search may use, what a complete path looks like
// compared to a truncated one, and - the case that matters most to a follower - whether the path
// hugs the corner it has to go round instead of zig-zagging between polygon centres.
//
// The meshes here are built by hand from rectangles and connected through the same portal
// convention the bake uses (both directions stored, each with its own left and right). A hand-built
// mesh makes the geometry of each case readable in the test itself, which is what a pathfinder's
// tests have to be.

#include "nav_path.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Navigation::Tests {
namespace {

constexpr float kEpsilon = 1e-3F;

/// Builds a navmesh out of axis-aligned rectangles and explicit connections between them, using the
/// mesh's own winding and portal conventions so a test can state a level's layout.
class MeshBuilder final {
public:
    /// A rectangle at `height` spanning (minimumX, minimumZ) to (maximumX, maximumZ), wound the way
    /// the bake winds one.
    std::size_t AddRectangle(const float minimumX, const float minimumZ, const float maximumX,
                                const float maximumZ, const float height = 0.0F,
                                const std::uint32_t layers = 1U) {
        NavmeshPolygon polygon{};
        polygon.vertexCount = 4U;
        polygon.navigationLayers = layers;
        polygon.vertices[0] = Math::Vector3{minimumX, height, minimumZ};
        polygon.vertices[1] = Math::Vector3{maximumX, height, minimumZ};
        polygon.vertices[2] = Math::Vector3{maximumX, height, maximumZ};
        polygon.vertices[3] = Math::Vector3{minimumX, height, maximumZ};
        polygon.areaSquareMetres = std::fabs(SignedAreaXZ(polygon.vertices.data(), polygon.vertexCount));
        polygon.center = Math::Vector3{(minimumX + maximumX) * 0.5F, height, (minimumZ + maximumZ) * 0.5F};
        mesh.polygons.push_back(polygon);
        portalsPerPolygon.emplace_back();
        return mesh.polygons.size() - 1U;
    }

    /// Connects two rectangles that touch along an edge, the way the bake does: one portal per
    /// direction, each carrying the shared edge's own two ends.
    ///
    /// The edge is the overlap of the two touching sides, which is *not* the pair of corners they
    /// have in common: an L-shaped level is made of rooms of different sizes, and there the far end
    /// of the shared edge is a corner of the smaller room and a point in the middle of the larger
    /// one's side. Getting that wrong does not misplace the portal, it removes it, and the level
    /// becomes two levels that a path can never cross.
    void Connect(const std::size_t from, const std::size_t to) {
        const NavmeshPolygon& first = mesh.polygons[from];
        const NavmeshPolygon& second = mesh.polygons[to];
        const float firstMinimumX = first.vertices[0].x;
        const float firstMinimumZ = first.vertices[0].z;
        const float firstMaximumX = first.vertices[2].x;
        const float firstMaximumZ = first.vertices[2].z;
        const float secondMinimumX = second.vertices[0].x;
        const float secondMinimumZ = second.vertices[0].z;
        const float secondMaximumX = second.vertices[2].x;
        const float secondMaximumZ = second.vertices[2].z;

        Math::Vector3 start{};
        Math::Vector3 end{};
        if (std::fabs(firstMaximumX - secondMinimumX) <= kEpsilon) {
            start = Math::Vector3{firstMaximumX, 0.0F, std::max(firstMinimumZ, secondMinimumZ)};
            end = Math::Vector3{firstMaximumX, 0.0F, std::min(firstMaximumZ, secondMaximumZ)};
        } else if (std::fabs(secondMaximumX - firstMinimumX) <= kEpsilon) {
            start = Math::Vector3{firstMinimumX, 0.0F, std::max(firstMinimumZ, secondMinimumZ)};
            end = Math::Vector3{firstMinimumX, 0.0F, std::min(firstMaximumZ, secondMaximumZ)};
        } else if (std::fabs(firstMaximumZ - secondMinimumZ) <= kEpsilon) {
            start = Math::Vector3{std::max(firstMinimumX, secondMinimumX), 0.0F, firstMaximumZ};
            end = Math::Vector3{std::min(firstMaximumX, secondMaximumX), 0.0F, firstMaximumZ};
        } else if (std::fabs(secondMaximumZ - firstMinimumZ) <= kEpsilon) {
            start = Math::Vector3{std::max(firstMinimumX, secondMinimumX), 0.0F, firstMinimumZ};
            end = Math::Vector3{std::min(firstMaximumX, secondMaximumX), 0.0F, firstMinimumZ};
        } else {
            return;
        }
        const float sharedLength = (start.x == end.x) ? std::fabs(end.z - start.z) : std::fabs(end.x - start.x);
        if (sharedLength <= kEpsilon) {
            return;
        }

        // Ends told apart by which side of the crossing they fall on, so a portal's left really is
        // the walker's left - the same contract the bake writes.
        const Math::Vector3 fromCenter = first.center;
        const Math::Vector3 toCenter = second.center;
        const float directionX = toCenter.x - fromCenter.x;
        const float directionZ = toCenter.z - fromCenter.z;
        const float side = (directionX * (start.z - fromCenter.z)) - (directionZ * (start.x - fromCenter.x));
        const Math::Vector3 left = side >= 0.0F ? start : end;
        const Math::Vector3 right = side >= 0.0F ? end : start;
        AppendPortal(from, to, left, right);
        AppendPortal(to, from, right, left);
    }

    [[nodiscard]] Navmesh Finish() {
        // Flatten each polygon's portals into one window, exactly as the bake does, so what the
        // pathfinder reads here is the layout it reads there.
        std::vector<NavmeshPortal> ordered;
        for (std::size_t polygonIndex = 0U; polygonIndex < mesh.polygons.size(); ++polygonIndex) {
            NavmeshPolygon& polygon = mesh.polygons[polygonIndex];
            polygon.firstPortal = static_cast<std::uint32_t>(ordered.size());
            polygon.portalCount = static_cast<std::uint32_t>(portalsPerPolygon[polygonIndex].size());
            for (const NavmeshPortal& portal : portalsPerPolygon[polygonIndex]) {
                ordered.push_back(portal);
            }
        }
        mesh.portals = std::move(ordered);
        mesh.maximumStepHeight = 0.4F;
        mesh.cellSize = 1.0F;
        mesh.agentRadius = 0.5F;
        mesh.agentHeight = 1.8F;
        return mesh;
    }

private:
    void AppendPortal(const std::size_t from, const std::size_t to, const Math::Vector3& left,
                         const Math::Vector3& right) {
        NavmeshPortal portal{};
        portal.from = static_cast<std::uint32_t>(from);
        portal.to = static_cast<std::uint32_t>(to);
        portal.left = left;
        portal.right = right;
        portalsPerPolygon[from].push_back(portal);
    }

    Navmesh mesh{};
    std::vector<std::vector<NavmeshPortal>> portalsPerPolygon;
};

/// Every point of the path's polyline, from the start through the waypoints, has to be on the mesh.
///
/// This is the property a follower depends on and the one a string-pulling step can quietly break:
/// a path that is shorter than the corridor allows looks better in a length check and walks the
/// agent through a wall. It is checked by sampling rather than by arithmetic on purpose - it is the
/// follower's own point-in-mesh question, asked at enough points that a leg leaving the surface
/// cannot hide between two of them.
void ExpectPathStaysOnMesh(const Navmesh& mesh, const NavPath& path, const Math::Vector3& start) {
    constexpr int kSamplesPerLeg = 32;
    Math::Vector3 previous = start;
    for (const Math::Vector3& waypoint : path.waypoints) {
        for (int sample = 0; sample <= kSamplesPerLeg; ++sample) {
            const float parameter = static_cast<float>(sample) / static_cast<float>(kSamplesPerLeg);
            const Math::Vector3 point{previous.x + (waypoint.x - previous.x) * parameter, 0.0F,
                                         previous.z + (waypoint.z - previous.z) * parameter};
            ASSERT_TRUE(mesh.FindPolygon(point).has_value())
                << "the path left the mesh at (" << point.x << ", " << point.z << ")";
        }
        previous = waypoint;
    }
}

/// Finds the waypoint nearest to `point`, or `waypoints.size()` when none is within `tolerance`.
[[nodiscard]] std::size_t IndexOfWaypointNear(const NavPath& path, const Math::Vector3& point,
                                                 const float tolerance = 0.05F) {
    for (std::size_t index = 0U; index < path.waypoints.size(); ++index) {
        const Math::Vector3 delta = path.waypoints[index] - point;
        if (std::fabs(delta.x) <= tolerance && std::fabs(delta.z) <= tolerance) {
            return index;
        }
    }
    return path.waypoints.size();
}

TEST(NavPathTest, FindNavPath_WithinOnePolygonIsASingleStraightLeg) {
    MeshBuilder builder;
    builder.AddRectangle(0.0F, 0.0F, 10.0F, 10.0F);
    const Navmesh mesh = builder.Finish();

    NavPathRequest request{};
    request.start = Math::Vector3{1.0F, 0.0F, 1.0F};
    request.target = Math::Vector3{8.0F, 0.0F, 5.0F};
    const NavPath path = FindNavPath(mesh, request);

    EXPECT_EQ(path.status, NavPathStatus::Found);
    EXPECT_TRUE(path.IsUsable());
    ASSERT_EQ(path.waypoints.size(), 1U) << "one convex polygon has nothing to turn around";
    EXPECT_EQ(path.waypoints.front(), request.target);
    EXPECT_NEAR(path.lengthMetres, std::sqrt(49.0F + 16.0F), kEpsilon);
}

TEST(NavPathTest, FindNavPath_TwoRoomsInLineGiveOneLegThroughTheDoorway) {
    MeshBuilder builder;
    const std::size_t west = builder.AddRectangle(0.0F, 0.0F, 4.0F, 4.0F);
    const std::size_t east = builder.AddRectangle(4.0F, 0.0F, 8.0F, 4.0F);
    builder.Connect(west, east);
    const Navmesh mesh = builder.Finish();

    NavPathRequest request{};
    request.start = Math::Vector3{1.0F, 0.0F, 2.0F};
    request.target = Math::Vector3{7.0F, 0.0F, 2.0F};
    const NavPath path = FindNavPath(mesh, request);

    EXPECT_EQ(path.status, NavPathStatus::Found);
    ASSERT_EQ(path.waypoints.size(), 1U)
        << "a straight walk through a doorway needs no corner, however many polygons it crosses";
    EXPECT_EQ(path.waypoints.front(), request.target);
    EXPECT_NEAR(path.lengthMetres, 6.0F, kEpsilon);
    ExpectPathStaysOnMesh(mesh, path, request.start);
}

TEST(NavPathTest, FindNavPath_AnLShapedRoomTurnsAtTheInnerCorner) {
    // The corridor turns: a body walking from the bottom-left to the top-right must go round the
    // corner at (4, 0), and the path has to say so. A search that answered with polygon centres
    // would send the agent through (2,1) and (5,3) instead, which is the zig-zag this test exists to
    // catch - and a funnel whose portal sides were swapped would turn at (4, 4) instead, outside
    // the corridor.
    MeshBuilder builder;
    const std::size_t bottom = builder.AddRectangle(0.0F, 0.0F, 4.0F, 2.0F);
    const std::size_t up = builder.AddRectangle(4.0F, 0.0F, 6.0F, 6.0F);
    builder.Connect(bottom, up);
    const Navmesh mesh = builder.Finish();

    NavPathRequest request{};
    request.start = Math::Vector3{0.5F, 0.0F, 1.0F};
    request.target = Math::Vector3{5.0F, 0.0F, 5.0F};
    const NavPath path = FindNavPath(mesh, request);

    ASSERT_EQ(path.status, NavPathStatus::Found);
    ASSERT_GE(path.waypoints.size(), 2U) << "the walk turns";
    EXPECT_EQ(path.waypoints.back(), request.target);
    // The corner of the doorway the walk keeps to: the path leaves the bottom corridor at its own
    // end (x = 4) and must not cut the corner of the right-hand room.
    const std::size_t corner = IndexOfWaypointNear(path, Math::Vector3{4.0F, 0.0F, 2.0F}, 0.25F);
    EXPECT_LT(corner, path.waypoints.size()) << "the path hugs the inner corner at (4, 2)";
    EXPECT_FLOAT_EQ(path.waypoints[corner].y, 0.0F) << "and stays on the surface it was baked from";
    EXPECT_GE(path.lengthMetres, 5.4F) << "the walk around the corner is longer than the straight line";
    EXPECT_NEAR(path.lengthMetres, 6.802F, 0.01F) << "and no longer than the taut walk around it";
    ExpectPathStaysOnMesh(mesh, path, request.start);
}

TEST(NavPathTest, FindNavPath_WalksAZigzagWithoutEverLeavingTheMesh) {
    // A corridor that doubles back on itself: east, north, west, north, east. Every corner of it is
    // a place a string-pulling step can cut across the wall, and the walk that goes round them all
    // is the one this asserts on.
    MeshBuilder builder;
    const std::size_t a = builder.AddRectangle(0.0F, 0.0F, 4.0F, 2.0F);
    const std::size_t b = builder.AddRectangle(0.0F, 2.0F, 6.0F, 4.0F);
    const std::size_t c = builder.AddRectangle(2.0F, 4.0F, 6.0F, 6.0F);
    const std::size_t d = builder.AddRectangle(2.0F, 6.0F, 8.0F, 8.0F);
    builder.Connect(a, b);
    builder.Connect(b, c);
    builder.Connect(c, d);
    const Navmesh mesh = builder.Finish();

    NavPathRequest request{};
    request.start = Math::Vector3{0.5F, 0.0F, 1.0F};
    request.target = Math::Vector3{7.5F, 0.0F, 7.0F};
    const NavPath path = FindNavPath(mesh, request);

    ASSERT_EQ(path.status, NavPathStatus::Found);
    EXPECT_EQ(path.expandedNodes, 4U) << "every doorway on the way is opened once";
    ASSERT_FALSE(path.waypoints.empty());
    EXPECT_EQ(path.waypoints.back(), request.target);
    ExpectPathStaysOnMesh(mesh, path, request.start);
    // The walk is not the straight line the start and the goal would allow if the level were open: it
    // has to leave through the far end of each room.
    EXPECT_GT(path.lengthMetres, 9.0F);
}

TEST(NavPathTest, FindNavPath_StaysOnTheLayersTheRequestAllows) {
    MeshBuilder builder;
    const std::size_t ground = builder.AddRectangle(0.0F, 0.0F, 4.0F, 4.0F, 0.0F, 1U);
    const std::size_t restricted = builder.AddRectangle(4.0F, 0.0F, 8.0F, 4.0F, 0.0F, 4U);
    builder.Connect(ground, restricted);
    const Navmesh mesh = builder.Finish();

    NavPathRequest open{};
    open.start = Math::Vector3{1.0F, 0.0F, 2.0F};
    open.target = Math::Vector3{7.0F, 0.0F, 2.0F};
    EXPECT_EQ(FindNavPath(mesh, open).status, NavPathStatus::Found);

    // The same request for an agent that may only walk layer 1: the polygon on layer 4 is not
    // somewhere this agent can go, so the answer is a path that stops at the doorway - not a path
    // through a room the agent may not enter, and not a refusal either, because the start is fine.
    NavPathRequest restrictedRequest = open;
    restrictedRequest.navigationLayers = 0x1U;
    const NavPath restrictedPath = FindNavPath(mesh, restrictedRequest);
    EXPECT_EQ(restrictedPath.status, NavPathStatus::Partial);
    ASSERT_FALSE(restrictedPath.waypoints.empty());
    EXPECT_FLOAT_EQ(restrictedPath.waypoints.back().x, 4.0F) << "the path ends at the layer boundary";

    // An agent that may walk neither polygon gets no path at all, and says which end was the
    // problem.
    NavPathRequest noLayers = open;
    noLayers.navigationLayers = 0x8U;
    EXPECT_EQ(FindNavPath(mesh, noLayers).status, NavPathStatus::StartOffMesh);
}

TEST(NavPathTest, FindNavPath_AnswersAPartialPathToTheClosestPointOnAnUnconnectedIsland) {
    MeshBuilder builder;
    builder.AddRectangle(0.0F, 0.0F, 4.0F, 4.0F);
    builder.AddRectangle(20.0F, 0.0F, 24.0F, 4.0F);  // an island across a gap, connected to nothing
    const Navmesh mesh = builder.Finish();

    NavPathRequest request{};
    request.start = Math::Vector3{1.0F, 0.0F, 1.0F};
    request.target = Math::Vector3{22.0F, 0.0F, 3.0F};
    const NavPath path = FindNavPath(mesh, request);

    EXPECT_EQ(path.status, NavPathStatus::Partial)
        << "the goal is real and on the mesh; this navmesh simply does not connect to it";
    EXPECT_TRUE(path.IsUsable());
    ASSERT_FALSE(path.waypoints.empty());
    // The closest reachable point to the goal is the near corner of the island's own polygon, so
    // the path ends there rather than in the middle of the gap.
    EXPECT_NEAR(path.waypoints.back().x, 4.0F, kEpsilon);
    EXPECT_NEAR(path.waypoints.back().z, 3.0F, kEpsilon);
}

TEST(NavPathTest, FindNavPath_RefusesEndsThatAreNotNearTheMesh) {
    MeshBuilder builder;
    builder.AddRectangle(0.0F, 0.0F, 4.0F, 4.0F);
    const Navmesh mesh = builder.Finish();

    NavPathRequest request{};
    request.start = Math::Vector3{50.0F, 0.0F, 50.0F};
    request.target = Math::Vector3{2.0F, 0.0F, 2.0F};
    EXPECT_EQ(FindNavPath(mesh, request).status, NavPathStatus::StartOffMesh);

    request.start = Math::Vector3{2.0F, 0.0F, 2.0F};
    request.target = Math::Vector3{50.0F, 0.0F, 50.0F};
    EXPECT_EQ(FindNavPath(mesh, request).status, NavPathStatus::TargetOffMesh);

    // A target a little off the mesh - beside an edge, or written a metre above the floor by a
    // script - is brought onto it instead of refused.
    request.target = Math::Vector3{5.5F, 0.0F, 2.0F};
    request.offMeshToleranceMetres = 2.0F;
    const NavPath projected = FindNavPath(mesh, request);
    EXPECT_EQ(projected.status, NavPathStatus::Found);
    ASSERT_FALSE(projected.waypoints.empty());
    EXPECT_NEAR(projected.waypoints.back().x, 4.0F, kEpsilon) << "clamped onto the polygon's edge";

    Navmesh empty{};
    EXPECT_EQ(FindNavPath(empty, request).status, NavPathStatus::NoMesh);
}

TEST(NavPathTest, FindNavPath_StopsAtItsOwnExpansionBudgetAndSaysSo) {
    // A strip of small rooms, connected end to end: a search from one end to the other has to expand
    // its way along. With a budget of one polygon it cannot, and the answer has to be a partial path
    // with the count that says the budget was the reason.
    MeshBuilder builder;
    std::size_t previous = builder.AddRectangle(0.0F, 0.0F, 2.0F, 2.0F);
    for (std::size_t room = 1U; room < 8U; ++room) {
        const float near = static_cast<float>(room) * 2.0F;
        const std::size_t current = builder.AddRectangle(near, 0.0F, near + 2.0F, 2.0F);
        builder.Connect(previous, current);
        previous = current;
    }
    const Navmesh mesh = builder.Finish();

    NavPathRequest request{};
    request.start = Math::Vector3{1.0F, 0.0F, 1.0F};
    request.target = Math::Vector3{15.0F, 0.0F, 1.0F};
    request.maximumExpandedNodes = 1U;
    const NavPath budgeted = FindNavPath(mesh, request);
    EXPECT_EQ(budgeted.status, NavPathStatus::Partial);
    EXPECT_EQ(budgeted.expandedNodes, 1U);

    // The same request without the budget walks the whole strip.
    NavPathRequest unbudgeted = request;
    unbudgeted.maximumExpandedNodes = 4096U;
    const NavPath complete = FindNavPath(mesh, unbudgeted);
    EXPECT_EQ(complete.status, NavPathStatus::Found);
    EXPECT_EQ(complete.expandedNodes, 8U) << "every doorway on the way is opened once";
    ASSERT_FALSE(complete.waypoints.empty());
    EXPECT_EQ(complete.waypoints.back(), unbudgeted.target);
}

} // namespace
} // namespace UVE::Navigation::Tests
