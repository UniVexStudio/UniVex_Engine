// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Tests for the steering half of navigation: what an agent publishes for whatever moves its body,
// when it reconsiders its route, and what it does when the route it had stops being one.
//
// The agent is deliberately tested without a mover. Everything the rest of the engine gets from it
// is what it publishes - a desired velocity, the waypoint it is heading for, a status and a
// changed-the-route flag - so those are the things asserted on, and a mover that honours them is
// what makes an object walk. Meshes are built by hand through the same rectangle and portal
// conventions the bake writes, so each case's geometry is readable in the test that uses it.

#include "uve/navigation/navmesh_agent_uve.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace UVE::Navigation::Tests {
namespace {

constexpr float kEpsilon = 1e-3F;

/// One axis-aligned room, wound the way the bake winds a merged rectangle and with no portals: the
/// floor a single-region level gives an agent.
[[nodiscard]] NavmeshUVE MakeRoomMeshUVE(const float minimumX, const float minimumZ, const float maximumX,
                                         const float maximumZ, const std::uint32_t layers = 1U) {
    NavmeshPolygonUVE polygon{};
    polygon.vertexCount = 4U;
    polygon.navigationLayers = layers;
    polygon.vertices[0] = Math::Vector3UVE{minimumX, 0.0F, minimumZ};
    polygon.vertices[1] = Math::Vector3UVE{maximumX, 0.0F, minimumZ};
    polygon.vertices[2] = Math::Vector3UVE{maximumX, 0.0F, maximumZ};
    polygon.vertices[3] = Math::Vector3UVE{minimumX, 0.0F, maximumZ};
    polygon.areaSquareMetres = (maximumX - minimumX) * (maximumZ - minimumZ);
    polygon.center = Math::Vector3UVE{(minimumX + maximumX) * 0.5F, 0.0F, (minimumZ + maximumZ) * 0.5F};

    NavmeshUVE mesh{};
    mesh.polygons.push_back(polygon);
    mesh.bounds = Math::AabbUVE::FromCenterExtentsUVE(
        Math::Vector3UVE{(minimumX + maximumX) * 0.5F, 0.0F, (minimumZ + maximumZ) * 0.5F},
        Math::Vector3UVE{(maximumX - minimumX) * 0.5F, 1.0F, (maximumZ - minimumZ) * 0.5F});
    mesh.maximumStepHeight = 0.4F;
    mesh.cellSize = 1.0F;
    mesh.agentRadius = 0.5F;
    mesh.agentHeight = 1.8F;
    return mesh;
}

/// Two rooms of the same size in a line along X, connected through the doorway between them, each
/// with its own layers so a test can close one to the agent.
[[nodiscard]] NavmeshUVE MakeTwoRoomMeshUVE(const std::uint32_t westLayers, const std::uint32_t eastLayers) {
    NavmeshUVE mesh = MakeRoomMeshUVE(0.0F, 0.0F, 4.0F, 4.0F, westLayers);
    NavmeshPolygonUVE east{};
    east.vertexCount = 4U;
    east.navigationLayers = eastLayers;
    east.vertices[0] = Math::Vector3UVE{4.0F, 0.0F, 0.0F};
    east.vertices[1] = Math::Vector3UVE{8.0F, 0.0F, 0.0F};
    east.vertices[2] = Math::Vector3UVE{8.0F, 0.0F, 4.0F};
    east.vertices[3] = Math::Vector3UVE{4.0F, 0.0F, 4.0F};
    east.areaSquareMetres = 16.0F;
    east.center = Math::Vector3UVE{6.0F, 0.0F, 2.0F};
    mesh.polygons.push_back(east);

    // The doorway: the whole shared side, walked west-to-east, with each direction's own left and
    // right. Travelling +X, +Z is on the walker's right, which is the ordering the bake writes.
    NavmeshPortalUVE westToEast{};
    westToEast.from = 0U;
    westToEast.to = 1U;
    westToEast.left = Math::Vector3UVE{4.0F, 0.0F, 0.0F};
    westToEast.right = Math::Vector3UVE{4.0F, 0.0F, 4.0F};
    NavmeshPortalUVE eastToWest{};
    eastToWest.from = 1U;
    eastToWest.to = 0U;
    eastToWest.left = Math::Vector3UVE{4.0F, 0.0F, 4.0F};
    eastToWest.right = Math::Vector3UVE{4.0F, 0.0F, 0.0F};
    mesh.portals.push_back(westToEast);
    mesh.portals.push_back(eastToWest);
    mesh.polygons[0].firstPortal = 0U;
    mesh.polygons[0].portalCount = 1U;
    mesh.polygons[1].firstPortal = 1U;
    mesh.polygons[1].portalCount = 1U;
    return mesh;
}

/// A corridor that turns a corner: a two-metre-wide bottom arm and a six-metre-tall right arm, the
/// same level the pathfinder's own L-shape test uses. The doorway constrains the walk - the straight
/// line between the far ends of the arms leaves the level - so a route here has a corner in it, which
/// is what the follower's own tests need.
[[nodiscard]] NavmeshUVE MakeLShapedMeshUVE() {
    NavmeshUVE mesh = MakeRoomMeshUVE(0.0F, 0.0F, 4.0F, 2.0F, 1U);
    NavmeshPolygonUVE up{};
    up.vertexCount = 4U;
    up.navigationLayers = 1U;
    up.vertices[0] = Math::Vector3UVE{4.0F, 0.0F, 0.0F};
    up.vertices[1] = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    up.vertices[2] = Math::Vector3UVE{6.0F, 0.0F, 6.0F};
    up.vertices[3] = Math::Vector3UVE{4.0F, 0.0F, 6.0F};
    up.areaSquareMetres = 12.0F;
    up.center = Math::Vector3UVE{5.0F, 0.0F, 3.0F};
    mesh.polygons.push_back(up);

    // The doorway is the overlap of the two arms' touching sides: z from 0 to 2 at x = 4, not the
    // whole of the taller arm's side.
    NavmeshPortalUVE bottomToUp{};
    bottomToUp.from = 0U;
    bottomToUp.to = 1U;
    bottomToUp.left = Math::Vector3UVE{4.0F, 0.0F, 0.0F};
    bottomToUp.right = Math::Vector3UVE{4.0F, 0.0F, 2.0F};
    NavmeshPortalUVE upToBottom{};
    upToBottom.from = 1U;
    upToBottom.to = 0U;
    upToBottom.left = Math::Vector3UVE{4.0F, 0.0F, 2.0F};
    upToBottom.right = Math::Vector3UVE{4.0F, 0.0F, 0.0F};
    mesh.portals.push_back(bottomToUp);
    mesh.portals.push_back(upToBottom);
    mesh.polygons[0].firstPortal = 0U;
    mesh.polygons[0].portalCount = 1U;
    mesh.polygons[1].firstPortal = 1U;
    mesh.polygons[1].portalCount = 1U;
    return mesh;
}

[[nodiscard]] NavAgentSettingsUVE MakeAgentSettingsUVE() {
    NavAgentSettingsUVE settings{};
    // Straight-line behaviour, so a test that asserts on a direction is asserting on the steering
    // and not on the ramp: a zero limit publishes the wanted velocity on the first step.
    settings.accelerationMetresPerSecondSquared = 0.0F;
    return settings;
}

TEST(NavAgentUVETest, StepUVE_WithoutATargetPublishesNothingAndSaysSo) {
    const NavmeshUVE mesh = MakeRoomMeshUVE(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgentUVE agent;
    agent.ConfigureUVE(MakeAgentSettingsUVE());
    agent.SetPositionUVE(Math::Vector3UVE{1.0F, 0.0F, 1.0F});

    agent.StepUVE(mesh, 0.1F);

    EXPECT_EQ(agent.GetStatusUVE(), NavAgentStatusUVE::Idle);
    EXPECT_EQ(agent.GetDesiredVelocityUVE(), Math::Vector3UVE{});
    EXPECT_EQ(agent.GetNextPathPositionUVE(), agent.GetPositionUVE());
    EXPECT_FALSE(agent.HasReachedTargetUVE());
    EXPECT_FALSE(agent.HasPathUVE());
}

TEST(NavAgentUVETest, StepUVE_WhileDisabledPublishesNothingEvenWithATargetSet) {
    const NavmeshUVE mesh = MakeRoomMeshUVE(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgentUVE agent;
    agent.ConfigureUVE(MakeAgentSettingsUVE());
    agent.SetPositionUVE(Math::Vector3UVE{1.0F, 0.0F, 1.0F});
    agent.SetTargetUVE(Math::Vector3UVE{9.0F, 0.0F, 1.0F});
    agent.SetEnabledUVE(false);

    agent.StepUVE(mesh, 0.1F);

    EXPECT_EQ(agent.GetStatusUVE(), NavAgentStatusUVE::Idle)
        << "a switched-off agent is not failing at anything, it is simply not being driven";
    EXPECT_EQ(agent.GetDesiredVelocityUVE(), Math::Vector3UVE{});
    EXPECT_FALSE(agent.HasPathUVE());

    // Switched back on, the target it was given while off is still its target and the next step
    // searches for it.
    agent.SetEnabledUVE(true);
    agent.StepUVE(mesh, 0.1F);
    EXPECT_EQ(agent.GetStatusUVE(), NavAgentStatusUVE::Following);
    EXPECT_TRUE(agent.HasPathUVE());
}

TEST(NavAgentUVETest, StepUVE_WalksStraightAtTheWaypointAtTheConfiguredSpeed) {
    const NavmeshUVE mesh = MakeRoomMeshUVE(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgentUVE agent;
    NavAgentSettingsUVE settings = MakeAgentSettingsUVE();
    settings.maxSpeed = 4.0F;
    agent.ConfigureUVE(settings);
    agent.SetPositionUVE(Math::Vector3UVE{1.0F, 0.0F, 1.0F});
    agent.SetTargetUVE(Math::Vector3UVE{9.0F, 0.0F, 1.0F});

    agent.StepUVE(mesh, 0.1F);

    EXPECT_EQ(agent.GetStatusUVE(), NavAgentStatusUVE::Following);
    EXPECT_EQ(agent.GetNextPathPositionUVE(), (Math::Vector3UVE{9.0F, 0.0F, 1.0F}));
    EXPECT_NEAR(agent.GetDesiredVelocityUVE().x, 4.0F, kEpsilon);
    EXPECT_NEAR(agent.GetDesiredVelocityUVE().z, 0.0F, kEpsilon);
    EXPECT_NEAR(agent.GetDesiredVelocityUVE().y, 0.0F, kEpsilon)
        << "the agent steers in the plane it walks on; what moves the body off it is the mover's job";
    EXPECT_NEAR(agent.GetRemainingDistanceUVE(), 8.0F, kEpsilon);
}

TEST(NavAgentUVETest, StepUVE_AcceleratesTowardsTheWantedVelocityInsteadOfSnapping_AndTheRampIsTheSetting) {
    const NavmeshUVE mesh = MakeRoomMeshUVE(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgentUVE agent;
    NavAgentSettingsUVE settings = MakeAgentSettingsUVE();
    settings.maxSpeed = 4.0F;
    settings.accelerationMetresPerSecondSquared = 20.0F;
    agent.ConfigureUVE(settings);
    agent.SetPositionUVE(Math::Vector3UVE{1.0F, 0.0F, 1.0F});
    agent.SetTargetUVE(Math::Vector3UVE{9.0F, 0.0F, 1.0F});

    agent.StepUVE(mesh, 0.1F);
    EXPECT_NEAR(agent.GetDesiredVelocityUVE().x, 2.0F, kEpsilon)
        << "20 m/s^2 over 0.1 s is 2 m/s, so the first step may not publish more than that";

    agent.StepUVE(mesh, 0.1F);
    EXPECT_NEAR(agent.GetDesiredVelocityUVE().x, 4.0F, kEpsilon) << "the second step reaches top speed";

    // A zero limit is the other contract, and the one a test that means to assert on a direction
    // wants: no ramp at all.
    NavAgentUVE immediate;
    immediate.ConfigureUVE(MakeAgentSettingsUVE());
    immediate.SetPositionUVE(Math::Vector3UVE{1.0F, 0.0F, 1.0F});
    immediate.SetTargetUVE(Math::Vector3UVE{9.0F, 0.0F, 1.0F});
    immediate.StepUVE(mesh, 0.001F);
    EXPECT_NEAR(immediate.GetDesiredVelocityUVE().x, 4.0F, kEpsilon);
}

TEST(NavAgentUVETest, StepUVE_SlowsDownOverTheLastStretchAndStopsOnTheTarget) {
    const NavmeshUVE mesh = MakeRoomMeshUVE(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgentUVE agent;
    NavAgentSettingsUVE settings = MakeAgentSettingsUVE();
    settings.maxSpeed = 4.0F;
    settings.slowDownRadiusMetres = 2.0F;
    settings.targetToleranceMetres = 0.5F;
    agent.ConfigureUVE(settings);
    agent.SetPositionUVE(Math::Vector3UVE{8.0F, 0.0F, 1.0F});
    agent.SetTargetUVE(Math::Vector3UVE{9.0F, 0.0F, 1.0F});

    agent.StepUVE(mesh, 0.1F);
    EXPECT_NEAR(agent.GetDesiredVelocityUVE().x, 2.0F, kEpsilon)
        << "one metre out of a two-metre approach is half speed, so it arrives rather than overshoots";

    // Arrival is measured against the agent's own position, which is what the mover hands back, so
    // moving the body onto the target is the whole of the last step.
    agent.SetPositionUVE(Math::Vector3UVE{8.75F, 0.0F, 1.0F});
    agent.StepUVE(mesh, 0.1F);
    EXPECT_EQ(agent.GetStatusUVE(), NavAgentStatusUVE::Finished);
    EXPECT_TRUE(agent.HasReachedTargetUVE());
    EXPECT_EQ(agent.GetDesiredVelocityUVE(), Math::Vector3UVE{});
    EXPECT_NEAR(agent.GetRemainingDistanceUVE(), 0.25F, kEpsilon);
}

TEST(NavAgentUVETest, StepUVE_SaysWhichEndIsOffTheMeshRatherThanAnsweringWithAPathFromSomewhereElse) {
    const NavmeshUVE mesh = MakeRoomMeshUVE(0.0F, 0.0F, 10.0F, 10.0F);

    NavAgentUVE stranded;
    stranded.ConfigureUVE(MakeAgentSettingsUVE());
    stranded.SetPositionUVE(Math::Vector3UVE{-20.0F, 0.0F, -20.0F});
    stranded.SetTargetUVE(Math::Vector3UVE{5.0F, 0.0F, 5.0F});
    stranded.StepUVE(mesh, 0.1F);
    EXPECT_EQ(stranded.GetStatusUVE(), NavAgentStatusUVE::Failed);
    EXPECT_FALSE(stranded.HasPathUVE());
    EXPECT_EQ(stranded.GetDesiredVelocityUVE(), Math::Vector3UVE{});

    // A target within the off-mesh tolerance is not stranded: a script that wrote a waypoint
    // slightly off the edge still gets an agent that walks to it.
    NavAgentUVE reaching;
    reaching.ConfigureUVE(MakeAgentSettingsUVE());
    reaching.SetPositionUVE(Math::Vector3UVE{5.0F, 0.0F, 5.0F});
    reaching.SetTargetUVE(Math::Vector3UVE{11.0F, 0.0F, 5.0F});
    reaching.StepUVE(mesh, 0.1F);
    EXPECT_EQ(reaching.GetStatusUVE(), NavAgentStatusUVE::Following);
    EXPECT_EQ(reaching.GetNextPathPositionUVE(), (Math::Vector3UVE{10.0F, 0.0F, 5.0F}))
        << "the goal is brought onto the mesh, so the agent walks to the edge rather than into the void";
}

TEST(NavAgentUVETest, StepUVE_TurnsTheCornerOfATurningCorridorAndTurnsToTheNextWaypoint) {
    const NavmeshUVE mesh = MakeLShapedMeshUVE();
    NavAgentUVE agent;
    NavAgentSettingsUVE settings = MakeAgentSettingsUVE();
    // Long enough that no step below searches again: this test is about walking the route it was
    // given, and a replan in the middle of it would be a different test.
    settings.pathUpdateInterval = 5.0F;
    agent.ConfigureUVE(settings);
    agent.SetPositionUVE(Math::Vector3UVE{0.5F, 0.0F, 1.0F});
    agent.SetTargetUVE(Math::Vector3UVE{5.0F, 0.0F, 5.0F});

    agent.StepUVE(mesh, 0.1F);
    ASSERT_EQ(agent.GetStatusUVE(), NavAgentStatusUVE::Following);
    ASSERT_TRUE(agent.HasPathUVE());
    EXPECT_TRUE(agent.GetPathChangedUVE())
        << "the first search found a route where there was none, which is the change a script waits for";
    ASSERT_GE(agent.GetPathUVE().waypoints.size(), 2U) << "the walk turns";
    EXPECT_EQ(agent.GetWaypointIndexUVE(), 0U);

    // The straight line to the goal leaves the level through the inside wall, so the waypoint the
    // agent steers at is the corner it has to walk around - not the goal, and not the middle of the
    // corridor it is standing in.
    EXPECT_NEAR(agent.GetNextPathPositionUVE().x, 4.0F, 0.05F);
    EXPECT_NEAR(agent.GetNextPathPositionUVE().z, 2.0F, 0.05F);
    EXPECT_NEAR(agent.GetDesiredVelocityUVE().y, 0.0F, kEpsilon);

    // Reaching the corner moves the agent to the next waypoint on the route; standing near it is
    // enough, which is what the waypoint radius is for.
    agent.SetPositionUVE(Math::Vector3UVE{4.2F, 0.0F, 2.1F});
    agent.StepUVE(mesh, 0.1F);
    EXPECT_EQ(agent.GetWaypointIndexUVE(), 1U);
    EXPECT_EQ(agent.GetNextPathPositionUVE(), (Math::Vector3UVE{5.0F, 0.0F, 5.0F}));
    EXPECT_TRUE(agent.HasPathUVE());
}

TEST(NavAgentUVETest, StepUVE_ReplansOnTheConfiguredIntervalAndOnlyThen) {
    // A corridor that turns: from the bottom arm the walk has a corner in it, from the top arm it is
    // a straight line, so moving the agent really does make a different route.
    const NavmeshUVE mesh = MakeLShapedMeshUVE();
    NavAgentUVE agent;
    NavAgentSettingsUVE settings = MakeAgentSettingsUVE();
    settings.pathUpdateInterval = 0.5F;
    agent.ConfigureUVE(settings);
    agent.SetPositionUVE(Math::Vector3UVE{0.5F, 0.0F, 1.0F});
    agent.SetTargetUVE(Math::Vector3UVE{5.0F, 0.0F, 5.0F});

    agent.StepUVE(mesh, 0.1F);
    ASSERT_EQ(agent.GetPathUVE().waypoints.size(), 2U) << "the bottom arm's route turns the corner";

    // Inside the interval nothing is searched, so the flag stays down however the agent moves: the
    // route it has is the route it walks.
    agent.SetPositionUVE(Math::Vector3UVE{0.6F, 0.0F, 1.0F});
    agent.StepUVE(mesh, 0.1F);
    EXPECT_FALSE(agent.GetPathChangedUVE());
    agent.StepUVE(mesh, 0.1F);
    EXPECT_FALSE(agent.GetPathChangedUVE());

    // Past the interval the route is searched again. It is the same route - the agent moved a
    // decimetre - so the flag is still down: the flag reports a change of route, not a search.
    agent.StepUVE(mesh, 0.3F);
    EXPECT_FALSE(agent.GetPathChangedUVE());

    // Now the route really changes: the agent is put in the arm the target is in, so the walk no
    // longer goes round the corner. Inside the interval that is not seen yet - which is the whole
    // point of the setting - and past it the search reports the new route.
    agent.SetPositionUVE(Math::Vector3UVE{4.5F, 0.0F, 3.0F});
    agent.StepUVE(mesh, 0.1F);
    EXPECT_FALSE(agent.GetPathChangedUVE()) << "inside the interval, the route is the route it has";
    agent.StepUVE(mesh, 0.7F);
    EXPECT_TRUE(agent.GetPathChangedUVE()) << "past it, the search sees the agent is somewhere else";
    ASSERT_EQ(agent.GetPathUVE().waypoints.size(), 1U);
    EXPECT_EQ(agent.GetNextPathPositionUVE(), (Math::Vector3UVE{5.0F, 0.0F, 5.0F}));
}

TEST(NavAgentUVETest, SetTargetUVE_SearchesOnTheNextStepWithoutWaitingOutTheInterval) {
    const NavmeshUVE mesh = MakeRoomMeshUVE(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgentUVE agent;
    NavAgentSettingsUVE settings = MakeAgentSettingsUVE();
    settings.pathUpdateInterval = 5.0F;
    agent.ConfigureUVE(settings);
    agent.SetPositionUVE(Math::Vector3UVE{1.0F, 0.0F, 1.0F});
    agent.SetTargetUVE(Math::Vector3UVE{4.0F, 0.0F, 1.0F});
    agent.StepUVE(mesh, 0.1F);
    EXPECT_EQ(agent.GetNextPathPositionUVE(), (Math::Vector3UVE{4.0F, 0.0F, 1.0F}));

    // Five seconds of interval must not hold a new order back: the target moved, so the route has to.
    agent.SetTargetUVE(Math::Vector3UVE{9.0F, 0.0F, 1.0F});
    agent.StepUVE(mesh, 0.1F);
    EXPECT_EQ(agent.GetNextPathPositionUVE(), (Math::Vector3UVE{9.0F, 0.0F, 1.0F}));
    EXPECT_TRUE(agent.GetPathChangedUVE());
    EXPECT_NEAR(agent.GetDesiredVelocityUVE().x, 4.0F, kEpsilon);
}

TEST(NavAgentUVETest, StepUVE_HonoursTheAgentsOwnLayersRatherThanTheMeshes) {
    // The east room is on layer 4 and this agent may walk layer 1 only, so the doorway is a wall as
    // far as it is concerned: a path that stops at the boundary, not one that walks through the room
    // and not a refusal either.
    const NavmeshUVE mesh = MakeTwoRoomMeshUVE(1U, 4U);
    NavAgentUVE agent;
    NavAgentSettingsUVE settings = MakeAgentSettingsUVE();
    settings.navigationLayers = 1U;
    agent.ConfigureUVE(settings);
    agent.SetPositionUVE(Math::Vector3UVE{1.0F, 0.0F, 2.0F});
    agent.SetTargetUVE(Math::Vector3UVE{7.0F, 0.0F, 2.0F});

    agent.StepUVE(mesh, 0.1F);
    EXPECT_EQ(agent.GetStatusUVE(), NavAgentStatusUVE::Following);
    EXPECT_NEAR(agent.GetNextPathPositionUVE().x, 4.0F, kEpsilon);

    // Widening the agent's own layers drops the route it had and searches again, and this time the
    // east room is walkable, so the goal becomes reachable without anything else changing.
    agent.SetNavigationLayersUVE(5U);
    agent.StepUVE(mesh, 0.1F);
    EXPECT_EQ(agent.GetNextPathPositionUVE(), (Math::Vector3UVE{7.0F, 0.0F, 2.0F}));
    EXPECT_TRUE(agent.GetPathChangedUVE());

    // Narrowing them again takes the room away, so the same walk stops at the doorway: the search
    // is re-run because the layers changed, and the agent does not keep a route it may not use.
    agent.SetNavigationLayersUVE(1U);
    agent.StepUVE(mesh, 0.1F);
    EXPECT_NEAR(agent.GetNextPathPositionUVE().x, 4.0F, kEpsilon);
    EXPECT_TRUE(agent.GetPathChangedUVE());
}

TEST(NavAgentUVETest, StepUVE_WithNeighboursStepsAsideInsteadOfWalkingThroughThem) {
    const NavmeshUVE mesh = MakeRoomMeshUVE(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgentUVE agent;
    NavAgentSettingsUVE settings = MakeAgentSettingsUVE();
    settings.maxSpeed = 4.0F;
    settings.avoidanceRadiusMetres = 2.0F;
    agent.ConfigureUVE(settings);
    agent.SetPositionUVE(Math::Vector3UVE{1.0F, 0.0F, 1.0F});
    agent.SetTargetUVE(Math::Vector3UVE{9.0F, 0.0F, 1.0F});

    // A colleague one metre to -Z, inside the two-metre avoidance radius: walking exactly at the
    // target would put this agent through it.
    const std::vector<NavAgentObstacleUVE> neighbours{NavAgentObstacleUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, 0.5F},
                                                      NavAgentObstacleUVE{Math::Vector3UVE{9.0F, 0.0F, 8.0F}, 0.5F}};
    agent.StepUVE(mesh, neighbours, 0.1F);

    EXPECT_GT(agent.GetDesiredVelocityUVE().z, 0.0F) << "the push is away from the colleague";
    EXPECT_LT(agent.GetDesiredVelocityUVE().x, 4.0F) << "and it comes out of forward speed";
    const Math::Vector3UVE velocity = agent.GetDesiredVelocityUVE();
    const float speedSquared = velocity.x * velocity.x + velocity.z * velocity.z;
    EXPECT_LE(speedSquared, 16.0F + kEpsilon) << "avoidance may not make the agent faster than it walks";
    EXPECT_EQ(agent.GetStatusUVE(), NavAgentStatusUVE::Following)
        << "stepping aside is still following the route: the push never becomes a new path";
    EXPECT_EQ(agent.GetNextPathPositionUVE(), (Math::Vector3UVE{9.0F, 0.0F, 1.0F}));
}

TEST(NavAgentUVETest, StepUVE_IgnoresAgentsOutsideTheAvoidanceRadiusAndItsOwnEntryInTheList) {
    const NavmeshUVE mesh = MakeRoomMeshUVE(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgentUVE agent;
    NavAgentSettingsUVE settings = MakeAgentSettingsUVE();
    settings.maxSpeed = 4.0F;
    settings.avoidanceRadiusMetres = 1.0F;
    agent.ConfigureUVE(settings);
    agent.SetPositionUVE(Math::Vector3UVE{1.0F, 0.0F, 1.0F});
    agent.SetTargetUVE(Math::Vector3UVE{9.0F, 0.0F, 1.0F});

    // Its own position, as a caller that passes every agent in the scene would give it, plus a
    // colleague far outside the radius: neither may push.
    const std::vector<NavAgentObstacleUVE> neighbours{
        NavAgentObstacleUVE{Math::Vector3UVE{1.0F, 0.0F, 1.0F}, 0.5F},
        NavAgentObstacleUVE{Math::Vector3UVE{1.0F, 0.0F, 4.0F}, 0.5F}};
    agent.StepUVE(mesh, neighbours, 0.1F);

    EXPECT_NEAR(agent.GetDesiredVelocityUVE().x, 4.0F, kEpsilon);
    EXPECT_NEAR(agent.GetDesiredVelocityUVE().z, 0.0F, kEpsilon);
}

TEST(NavAgentUVETest, ResetUVE_ForgetsTheTargetAndEverythingPublishedForIt) {
    const NavmeshUVE mesh = MakeRoomMeshUVE(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgentUVE agent;
    agent.ConfigureUVE(MakeAgentSettingsUVE());
    agent.SetPositionUVE(Math::Vector3UVE{1.0F, 0.0F, 1.0F});
    agent.SetTargetUVE(Math::Vector3UVE{9.0F, 0.0F, 1.0F});
    agent.StepUVE(mesh, 0.1F);
    ASSERT_TRUE(agent.HasPathUVE());

    agent.ResetUVE();

    EXPECT_FALSE(agent.HasTargetUVE());
    EXPECT_FALSE(agent.HasPathUVE());
    EXPECT_EQ(agent.GetStatusUVE(), NavAgentStatusUVE::Idle);
    EXPECT_EQ(agent.GetDesiredVelocityUVE(), Math::Vector3UVE{});
    EXPECT_EQ(agent.GetNextPathPositionUVE(), Math::Vector3UVE{});
    EXPECT_TRUE(agent.IsEnabledUVE()) << "a reset agent is a fresh agent, not a switched-off one";
}

} // namespace
} // namespace UVE::Navigation::Tests
