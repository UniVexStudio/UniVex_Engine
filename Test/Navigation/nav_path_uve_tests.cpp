// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Tests for the pathfinder: which polygons a search may use, what a complete path looks like
// compared to a truncated one, and - the case that matters most to a follower - whether the path
// hugs the corner it has to go round instead of zig-zagging between polygon centres.
//
// The meshes here are built by hand from rectangles and connected through the same portal
// convention the bake uses (both directions stored, each with its own left and right). A hand-built
// mesh makes the geometry of each case readable in the test itself, which is what a pathfinder's
// tests have to be.

#include "uve/navigation/nav_path_uve.h"

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
class MeshBuilderUVE final {
public:
    /// A rectangle at `height` spanning (minimumX, minimumZ) to (maximumX, maximumZ), wound the way
    /// the bake winds one.
    std::size_t AddRectangleUVE(const float minimumX, const float minimumZ, const float maximumX,
                                const float maximumZ, const float height = 0.0F,
                                const std::uint32_t layers = 1U) {
        NavmeshPolygonUVE polygon{};
        polygon.vertexCount = 4U;
        polygon.navigationLayers = layers;
        polygon.vertices[0] = Math::Vector3UVE{minimumX, height, minimumZ};
        polygon.vertices[1] = Math::Vector3UVE{maximumX, height, minimumZ};
        polygon.vertices[2] = Math::Vector3UVE{maximumX, height, maximumZ};
        polygon.vertices[3] = Math::Vector3UVE{minimumX, height, maximumZ};
        polygon.areaSquareMetres = std::fabs(SignedAreaXZUVE(polygon.vertices.data(), polygon.vertexCount));
        polygon.center = Math::Vector3UVE{(minimumX + maximumX) * 0.5F, height, (minimumZ + maximumZ) * 0.5F};
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
    void ConnectUVE(const std::size_t from, const std::size_t to) {
        const NavmeshPolygonUVE& first = mesh.polygons[from];
        const NavmeshPolygonUVE& second = mesh.polygons[to];
        const float firstMinimumX = first.vertices[0].x;
        const float firstMinimumZ = first.vertices[0].z;
        const float firstMaximumX = first.vertices[2].x;
        const float firstMaximumZ = first.vertices[2].z;
        const float secondMinimumX = second.vertices[0].x;
        const float secondMinimumZ = second.vertices[0].z;
        const float secondMaximumX = second.vertices[2].x;
        const float secondMaximumZ = second.vertices[2].z;

        Math::Vector3UVE start{};
        Math::Vector3UVE end{};
        if (std::fabs(firstMaximumX - secondMinimumX) <= kEpsilon) {
            start = Math::Vector3UVE{firstMaximumX, 0.0F, std::max(firstMinimumZ, secondMinimumZ)};
            end = Math::Vector3UVE{firstMaximumX, 0.0F, std::min(firstMaximumZ, secondMaximumZ)};
        } else if (std::fabs(secondMaximumX - firstMinimumX) <= kEpsilon) {
            start = Math::Vector3UVE{firstMinimumX, 0.0F, std::max(firstMinimumZ, secondMinimumZ)};
            end = Math::Vector3UVE{firstMinimumX, 0.0F, std::min(firstMaximumZ, secondMaximumZ)};
        } else if (std::fabs(firstMaximumZ - secondMinimumZ) <= kEpsilon) {
            start = Math::Vector3UVE{std::max(firstMinimumX, secondMinimumX), 0.0F, firstMaximumZ};
            end = Math::Vector3UVE{std::min(firstMaximumX, secondMaximumX), 0.0F, firstMaximumZ};
        } else if (std::fabs(secondMaximumZ - firstMinimumZ) <= kEpsilon) {
            start = Math::Vector3UVE{std::max(firstMinimumX, secondMinimumX), 0.0F, firstMinimumZ};
            end = Math::Vector3UVE{std::min(firstMaximumX, secondMaximumX), 0.0F, firstMinimumZ};
        } else {
            return;
        }
        const float sharedLength = (start.x == end.x) ? std::fabs(end.z - start.z) : std::fabs(end.x - start.x);
        if (sharedLength <= kEpsilon) {
            return;
        }

        // Ends told apart by which side of the crossing they fall on, so a portal's left really is
        // the walker's left - the same contract the bake writes.
        const Math::Vector3UVE fromCenter = first.center;
        const Math::Vector3UVE toCenter = second.center;
        const float directionX = toCenter.x - fromCenter.x;
        const float directionZ = toCenter.z - fromCenter.z;
        const float side = (directionX * (start.z - fromCenter.z)) - (directionZ * (start.x - fromCenter.x));
        const Math::Vector3UVE left = side >= 0.0F ? start : end;
        const Math::Vector3UVE right = side >= 0.0F ? end : start;
        AppendPortalUVE(from, to, left, right);
        AppendPortalUVE(to, from, right, left);
    }

    [[nodiscard]] NavmeshUVE FinishUVE() {
        // Flatten each polygon's portals into one window, exactly as the bake does, so what the
        // pathfinder reads here is the layout it reads there.
        std::vector<NavmeshPortalUVE> ordered;
        for (std::size_t polygonIndex = 0U; polygonIndex < mesh.polygons.size(); ++polygonIndex) {
            NavmeshPolygonUVE& polygon = mesh.polygons[polygonIndex];
            polygon.firstPortal = static_cast<std::uint32_t>(ordered.size());
            polygon.portalCount = static_cast<std::uint32_t>(portalsPerPolygon[polygonIndex].size());
            for (const NavmeshPortalUVE& portal : portalsPerPolygon[polygonIndex]) {
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
    void AppendPortalUVE(const std::size_t from, const std::size_t to, const Math::Vector3UVE& left,
                         const Math::Vector3UVE& right) {
        NavmeshPortalUVE portal{};
        portal.from = static_cast<std::uint32_t>(from);
        portal.to = static_cast<std::uint32_t>(to);
        portal.left = left;
        portal.right = right;
        portalsPerPolygon[from].push_back(portal);
    }

    NavmeshUVE mesh{};
    std::vector<std::vector<NavmeshPortalUVE>> portalsPerPolygon;
};

/// Every point of the path's polyline, from the start through the waypoints, has to be on the mesh.
///
/// This is the property a follower depends on and the one a string-pulling step can quietly break:
/// a path that is shorter than the corridor allows looks better in a length check and walks the
/// agent through a wall. It is checked by sampling rather than by arithmetic on purpose - it is the
/// follower's own point-in-mesh question, asked at enough points that a leg leaving the surface
/// cannot hide between two of them.
void ExpectPathStaysOnMeshUVE(const NavmeshUVE& mesh, const NavPathUVE& path, const Math::Vector3UVE& start) {
    constexpr int kSamplesPerLegUVE = 32;
    Math::Vector3UVE previous = start;
    for (const Math::Vector3UVE& waypoint : path.waypoints) {
        for (int sample = 0; sample <= kSamplesPerLegUVE; ++sample) {
            const float parameter = static_cast<float>(sample) / static_cast<float>(kSamplesPerLegUVE);
            const Math::Vector3UVE point{previous.x + (waypoint.x - previous.x) * parameter, 0.0F,
                                         previous.z + (waypoint.z - previous.z) * parameter};
            ASSERT_TRUE(mesh.FindPolygonUVE(point).has_value())
                << "the path left the mesh at (" << point.x << ", " << point.z << ")";
        }
        previous = waypoint;
    }
}

/// Finds the waypoint nearest to `point`, or `waypoints.size()` when none is within `tolerance`.
[[nodiscard]] std::size_t IndexOfWaypointNearUVE(const NavPathUVE& path, const Math::Vector3UVE& point,
                                                 const float tolerance = 0.05F) {
    for (std::size_t index = 0U; index < path.waypoints.size(); ++index) {
        const Math::Vector3UVE delta = path.waypoints[index] - point;
        if (std::fabs(delta.x) <= tolerance && std::fabs(delta.z) <= tolerance) {
            return index;
        }
    }
    return path.waypoints.size();
}

TEST(NavPathUVETest, FindNavPathUVE_WithinOnePolygonIsASingleStraightLeg) {
    MeshBuilderUVE builder;
    builder.AddRectangleUVE(0.0F, 0.0F, 10.0F, 10.0F);
    const NavmeshUVE mesh = builder.FinishUVE();

    NavPathRequestUVE request{};
    request.start = Math::Vector3UVE{1.0F, 0.0F, 1.0F};
    request.target = Math::Vector3UVE{8.0F, 0.0F, 5.0F};
    const NavPathUVE path = FindNavPathUVE(mesh, request);

    EXPECT_EQ(path.status, NavPathStatusUVE::Found);
    EXPECT_TRUE(path.IsUsableUVE());
    ASSERT_EQ(path.waypoints.size(), 1U) << "one convex polygon has nothing to turn around";
    EXPECT_EQ(path.waypoints.front(), request.target);
    EXPECT_NEAR(path.lengthMetres, std::sqrt(49.0F + 16.0F), kEpsilon);
}

TEST(NavPathUVETest, FindNavPathUVE_TwoRoomsInLineGiveOneLegThroughTheDoorway) {
    MeshBuilderUVE builder;
    const std::size_t west = builder.AddRectangleUVE(0.0F, 0.0F, 4.0F, 4.0F);
    const std::size_t east = builder.AddRectangleUVE(4.0F, 0.0F, 8.0F, 4.0F);
    builder.ConnectUVE(west, east);
    const NavmeshUVE mesh = builder.FinishUVE();

    NavPathRequestUVE request{};
    request.start = Math::Vector3UVE{1.0F, 0.0F, 2.0F};
    request.target = Math::Vector3UVE{7.0F, 0.0F, 2.0F};
    const NavPathUVE path = FindNavPathUVE(mesh, request);

    EXPECT_EQ(path.status, NavPathStatusUVE::Found);
    ASSERT_EQ(path.waypoints.size(), 1U)
        << "a straight walk through a doorway needs no corner, however many polygons it crosses";
    EXPECT_EQ(path.waypoints.front(), request.target);
    EXPECT_NEAR(path.lengthMetres, 6.0F, kEpsilon);
    ExpectPathStaysOnMeshUVE(mesh, path, request.start);
}

TEST(NavPathUVETest, FindNavPathUVE_AnLShapedRoomTurnsAtTheInnerCorner) {
    // The corridor turns: a body walking from the bottom-left to the top-right must go round the
    // corner at (4, 0), and the path has to say so. A search that answered with polygon centres
    // would send the agent through (2,1) and (5,3) instead, which is the zig-zag this test exists to
    // catch - and a funnel whose portal sides were swapped would turn at (4, 4) instead, outside
    // the corridor.
    MeshBuilderUVE builder;
    const std::size_t bottom = builder.AddRectangleUVE(0.0F, 0.0F, 4.0F, 2.0F);
    const std::size_t up = builder.AddRectangleUVE(4.0F, 0.0F, 6.0F, 6.0F);
    builder.ConnectUVE(bottom, up);
    const NavmeshUVE mesh = builder.FinishUVE();

    NavPathRequestUVE request{};
    request.start = Math::Vector3UVE{0.5F, 0.0F, 1.0F};
    request.target = Math::Vector3UVE{5.0F, 0.0F, 5.0F};
    const NavPathUVE path = FindNavPathUVE(mesh, request);

    ASSERT_EQ(path.status, NavPathStatusUVE::Found);
    ASSERT_GE(path.waypoints.size(), 2U) << "the walk turns";
    EXPECT_EQ(path.waypoints.back(), request.target);
    // The corner of the doorway the walk keeps to: the path leaves the bottom corridor at its own
    // end (x = 4) and must not cut the corner of the right-hand room.
    const std::size_t corner = IndexOfWaypointNearUVE(path, Math::Vector3UVE{4.0F, 0.0F, 2.0F}, 0.25F);
    EXPECT_LT(corner, path.waypoints.size()) << "the path hugs the inner corner at (4, 2)";
    EXPECT_FLOAT_EQ(path.waypoints[corner].y, 0.0F) << "and stays on the surface it was baked from";
    EXPECT_GE(path.lengthMetres, 5.4F) << "the walk around the corner is longer than the straight line";
    EXPECT_NEAR(path.lengthMetres, 6.802F, 0.01F) << "and no longer than the taut walk around it";
    ExpectPathStaysOnMeshUVE(mesh, path, request.start);
}

TEST(NavPathUVETest, FindNavPathUVE_WalksAZigzagWithoutEverLeavingTheMesh) {
    // A corridor that doubles back on itself: east, north, west, north, east. Every corner of it is
    // a place a string-pulling step can cut across the wall, and the walk that goes round them all
    // is the one this asserts on.
    MeshBuilderUVE builder;
    const std::size_t a = builder.AddRectangleUVE(0.0F, 0.0F, 4.0F, 2.0F);
    const std::size_t b = builder.AddRectangleUVE(0.0F, 2.0F, 6.0F, 4.0F);
    const std::size_t c = builder.AddRectangleUVE(2.0F, 4.0F, 6.0F, 6.0F);
    const std::size_t d = builder.AddRectangleUVE(2.0F, 6.0F, 8.0F, 8.0F);
    builder.ConnectUVE(a, b);
    builder.ConnectUVE(b, c);
    builder.ConnectUVE(c, d);
    const NavmeshUVE mesh = builder.FinishUVE();

    NavPathRequestUVE request{};
    request.start = Math::Vector3UVE{0.5F, 0.0F, 1.0F};
    request.target = Math::Vector3UVE{7.5F, 0.0F, 7.0F};
    const NavPathUVE path = FindNavPathUVE(mesh, request);

    ASSERT_EQ(path.status, NavPathStatusUVE::Found);
    EXPECT_EQ(path.expandedNodes, 4U) << "every doorway on the way is opened once";
    ASSERT_FALSE(path.waypoints.empty());
    EXPECT_EQ(path.waypoints.back(), request.target);
    ExpectPathStaysOnMeshUVE(mesh, path, request.start);
    // The walk is not the straight line the start and the goal would allow if the level were open: it
    // has to leave through the far end of each room.
    EXPECT_GT(path.lengthMetres, 9.0F);
}

TEST(NavPathUVETest, FindNavPathUVE_StaysOnTheLayersTheRequestAllows) {
    MeshBuilderUVE builder;
    const std::size_t ground = builder.AddRectangleUVE(0.0F, 0.0F, 4.0F, 4.0F, 0.0F, 1U);
    const std::size_t restricted = builder.AddRectangleUVE(4.0F, 0.0F, 8.0F, 4.0F, 0.0F, 4U);
    builder.ConnectUVE(ground, restricted);
    const NavmeshUVE mesh = builder.FinishUVE();

    NavPathRequestUVE open{};
    open.start = Math::Vector3UVE{1.0F, 0.0F, 2.0F};
    open.target = Math::Vector3UVE{7.0F, 0.0F, 2.0F};
    EXPECT_EQ(FindNavPathUVE(mesh, open).status, NavPathStatusUVE::Found);

    // The same request for an agent that may only walk layer 1: the polygon on layer 4 is not
    // somewhere this agent can go, so the answer is a path that stops at the doorway - not a path
    // through a room the agent may not enter, and not a refusal either, because the start is fine.
    NavPathRequestUVE restrictedRequest = open;
    restrictedRequest.navigationLayers = 0x1U;
    const NavPathUVE restrictedPath = FindNavPathUVE(mesh, restrictedRequest);
    EXPECT_EQ(restrictedPath.status, NavPathStatusUVE::Partial);
    ASSERT_FALSE(restrictedPath.waypoints.empty());
    EXPECT_FLOAT_EQ(restrictedPath.waypoints.back().x, 4.0F) << "the path ends at the layer boundary";

    // An agent that may walk neither polygon gets no path at all, and says which end was the
    // problem.
    NavPathRequestUVE noLayers = open;
    noLayers.navigationLayers = 0x8U;
    EXPECT_EQ(FindNavPathUVE(mesh, noLayers).status, NavPathStatusUVE::StartOffMesh);
}

TEST(NavPathUVETest, FindNavPathUVE_AnswersAPartialPathToTheClosestPointOnAnUnconnectedIsland) {
    MeshBuilderUVE builder;
    builder.AddRectangleUVE(0.0F, 0.0F, 4.0F, 4.0F);
    builder.AddRectangleUVE(20.0F, 0.0F, 24.0F, 4.0F);  // an island across a gap, connected to nothing
    const NavmeshUVE mesh = builder.FinishUVE();

    NavPathRequestUVE request{};
    request.start = Math::Vector3UVE{1.0F, 0.0F, 1.0F};
    request.target = Math::Vector3UVE{22.0F, 0.0F, 3.0F};
    const NavPathUVE path = FindNavPathUVE(mesh, request);

    EXPECT_EQ(path.status, NavPathStatusUVE::Partial)
        << "the goal is real and on the mesh; this navmesh simply does not connect to it";
    EXPECT_TRUE(path.IsUsableUVE());
    ASSERT_FALSE(path.waypoints.empty());
    // The closest reachable point to the goal is the near corner of the island's own polygon, so
    // the path ends there rather than in the middle of the gap.
    EXPECT_NEAR(path.waypoints.back().x, 4.0F, kEpsilon);
    EXPECT_NEAR(path.waypoints.back().z, 3.0F, kEpsilon);
}

TEST(NavPathUVETest, FindNavPathUVE_RefusesEndsThatAreNotNearTheMesh) {
    MeshBuilderUVE builder;
    builder.AddRectangleUVE(0.0F, 0.0F, 4.0F, 4.0F);
    const NavmeshUVE mesh = builder.FinishUVE();

    NavPathRequestUVE request{};
    request.start = Math::Vector3UVE{50.0F, 0.0F, 50.0F};
    request.target = Math::Vector3UVE{2.0F, 0.0F, 2.0F};
    EXPECT_EQ(FindNavPathUVE(mesh, request).status, NavPathStatusUVE::StartOffMesh);

    request.start = Math::Vector3UVE{2.0F, 0.0F, 2.0F};
    request.target = Math::Vector3UVE{50.0F, 0.0F, 50.0F};
    EXPECT_EQ(FindNavPathUVE(mesh, request).status, NavPathStatusUVE::TargetOffMesh);

    // A target a little off the mesh - beside an edge, or written a metre above the floor by a
    // script - is brought onto it instead of refused.
    request.target = Math::Vector3UVE{5.5F, 0.0F, 2.0F};
    request.offMeshToleranceMetres = 2.0F;
    const NavPathUVE projected = FindNavPathUVE(mesh, request);
    EXPECT_EQ(projected.status, NavPathStatusUVE::Found);
    ASSERT_FALSE(projected.waypoints.empty());
    EXPECT_NEAR(projected.waypoints.back().x, 4.0F, kEpsilon) << "clamped onto the polygon's edge";

    NavmeshUVE empty{};
    EXPECT_EQ(FindNavPathUVE(empty, request).status, NavPathStatusUVE::NoMesh);
}

TEST(NavPathUVETest, FindNavPathUVE_StopsAtItsOwnExpansionBudgetAndSaysSo) {
    // A strip of small rooms, connected end to end: a search from one end to the other has to expand
    // its way along. With a budget of one polygon it cannot, and the answer has to be a partial path
    // with the count that says the budget was the reason.
    MeshBuilderUVE builder;
    std::size_t previous = builder.AddRectangleUVE(0.0F, 0.0F, 2.0F, 2.0F);
    for (std::size_t room = 1U; room < 8U; ++room) {
        const float near = static_cast<float>(room) * 2.0F;
        const std::size_t current = builder.AddRectangleUVE(near, 0.0F, near + 2.0F, 2.0F);
        builder.ConnectUVE(previous, current);
        previous = current;
    }
    const NavmeshUVE mesh = builder.FinishUVE();

    NavPathRequestUVE request{};
    request.start = Math::Vector3UVE{1.0F, 0.0F, 1.0F};
    request.target = Math::Vector3UVE{15.0F, 0.0F, 1.0F};
    request.maximumExpandedNodes = 1U;
    const NavPathUVE budgeted = FindNavPathUVE(mesh, request);
    EXPECT_EQ(budgeted.status, NavPathStatusUVE::Partial);
    EXPECT_EQ(budgeted.expandedNodes, 1U);

    // The same request without the budget walks the whole strip.
    NavPathRequestUVE unbudgeted = request;
    unbudgeted.maximumExpandedNodes = 4096U;
    const NavPathUVE complete = FindNavPathUVE(mesh, unbudgeted);
    EXPECT_EQ(complete.status, NavPathStatusUVE::Found);
    EXPECT_EQ(complete.expandedNodes, 8U) << "every doorway on the way is opened once";
    ASSERT_FALSE(complete.waypoints.empty());
    EXPECT_EQ(complete.waypoints.back(), unbudgeted.target);
}

} // namespace
} // namespace UVE::Navigation::Tests
