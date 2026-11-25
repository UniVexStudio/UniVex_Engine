// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Tests for the steering half of navigation: what an agent publishes for whatever moves its body,
// when it reconsiders its route, and what it does when the route it had stops being one.
//
// The agent is deliberately tested without a mover. Everything the rest of the engine gets from it
// is what it publishes - a desired velocity, the waypoint it is heading for, a status and a
// changed-the-route flag - so those are the things asserted on, and a mover that honours them is
// what makes an object walk. Meshes are built by hand through the same rectangle and portal
// conventions the bake writes, so each case's geometry is readable in the test that uses it.

#include "navmesh_agent.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace UVE::Navigation::Tests {
namespace {

constexpr float kEpsilon = 1e-3F;

/// One axis-aligned room, wound the way the bake winds a merged rectangle and with no portals: the
/// floor a single-region level gives an agent.
[[nodiscard]] Navmesh MakeRoomMesh(const float minimumX, const float minimumZ, const float maximumX,
                                         const float maximumZ, const std::uint32_t layers = 1U) {
    NavmeshPolygon polygon{};
    polygon.vertexCount = 4U;
    polygon.navigationLayers = layers;
    polygon.vertices[0] = Math::Vector3{minimumX, 0.0F, minimumZ};
    polygon.vertices[1] = Math::Vector3{maximumX, 0.0F, minimumZ};
    polygon.vertices[2] = Math::Vector3{maximumX, 0.0F, maximumZ};
    polygon.vertices[3] = Math::Vector3{minimumX, 0.0F, maximumZ};
    polygon.areaSquareMetres = (maximumX - minimumX) * (maximumZ - minimumZ);
    polygon.center = Math::Vector3{(minimumX + maximumX) * 0.5F, 0.0F, (minimumZ + maximumZ) * 0.5F};

    Navmesh mesh{};
    mesh.polygons.push_back(polygon);
    mesh.bounds = Math::Aabb::FromCenterExtents(
        Math::Vector3{(minimumX + maximumX) * 0.5F, 0.0F, (minimumZ + maximumZ) * 0.5F},
        Math::Vector3{(maximumX - minimumX) * 0.5F, 1.0F, (maximumZ - minimumZ) * 0.5F});
    mesh.maximumStepHeight = 0.4F;
    mesh.cellSize = 1.0F;
    mesh.agentRadius = 0.5F;
    mesh.agentHeight = 1.8F;
    return mesh;
}

/// Two rooms of the same size in a line along X, connected through the doorway between them, each
/// with its own layers so a test can close one to the agent.
[[nodiscard]] Navmesh MakeTwoRoomMesh(const std::uint32_t westLayers, const std::uint32_t eastLayers) {
    Navmesh mesh = MakeRoomMesh(0.0F, 0.0F, 4.0F, 4.0F, westLayers);
    NavmeshPolygon east{};
    east.vertexCount = 4U;
    east.navigationLayers = eastLayers;
    east.vertices[0] = Math::Vector3{4.0F, 0.0F, 0.0F};
    east.vertices[1] = Math::Vector3{8.0F, 0.0F, 0.0F};
    east.vertices[2] = Math::Vector3{8.0F, 0.0F, 4.0F};
    east.vertices[3] = Math::Vector3{4.0F, 0.0F, 4.0F};
    east.areaSquareMetres = 16.0F;
    east.center = Math::Vector3{6.0F, 0.0F, 2.0F};
    mesh.polygons.push_back(east);

    // The doorway: the whole shared side, walked west-to-east, with each direction's own left and
    // right. Travelling +X, +Z is on the walker's right, which is the ordering the bake writes.
    NavmeshPortal westToEast{};
    westToEast.from = 0U;
    westToEast.to = 1U;
    westToEast.left = Math::Vector3{4.0F, 0.0F, 0.0F};
    westToEast.right = Math::Vector3{4.0F, 0.0F, 4.0F};
    NavmeshPortal eastToWest{};
    eastToWest.from = 1U;
    eastToWest.to = 0U;
    eastToWest.left = Math::Vector3{4.0F, 0.0F, 4.0F};
    eastToWest.right = Math::Vector3{4.0F, 0.0F, 0.0F};
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
[[nodiscard]] Navmesh MakeLShapedMesh() {
    Navmesh mesh = MakeRoomMesh(0.0F, 0.0F, 4.0F, 2.0F, 1U);
    NavmeshPolygon up{};
    up.vertexCount = 4U;
    up.navigationLayers = 1U;
    up.vertices[0] = Math::Vector3{4.0F, 0.0F, 0.0F};
    up.vertices[1] = Math::Vector3{6.0F, 0.0F, 0.0F};
    up.vertices[2] = Math::Vector3{6.0F, 0.0F, 6.0F};
    up.vertices[3] = Math::Vector3{4.0F, 0.0F, 6.0F};
    up.areaSquareMetres = 12.0F;
    up.center = Math::Vector3{5.0F, 0.0F, 3.0F};
    mesh.polygons.push_back(up);

    // The doorway is the overlap of the two arms' touching sides: z from 0 to 2 at x = 4, not the
    // whole of the taller arm's side.
    NavmeshPortal bottomToUp{};
    bottomToUp.from = 0U;
    bottomToUp.to = 1U;
    bottomToUp.left = Math::Vector3{4.0F, 0.0F, 0.0F};
    bottomToUp.right = Math::Vector3{4.0F, 0.0F, 2.0F};
    NavmeshPortal upToBottom{};
    upToBottom.from = 1U;
    upToBottom.to = 0U;
    upToBottom.left = Math::Vector3{4.0F, 0.0F, 2.0F};
    upToBottom.right = Math::Vector3{4.0F, 0.0F, 0.0F};
    mesh.portals.push_back(bottomToUp);
    mesh.portals.push_back(upToBottom);
    mesh.polygons[0].firstPortal = 0U;
    mesh.polygons[0].portalCount = 1U;
    mesh.polygons[1].firstPortal = 1U;
    mesh.polygons[1].portalCount = 1U;
    return mesh;
}

[[nodiscard]] NavAgentSettings MakeAgentSettings() {
    NavAgentSettings settings{};
    // Straight-line behaviour, so a test that asserts on a direction is asserting on the steering
    // and not on the ramp: a zero limit publishes the wanted velocity on the first step.
    settings.accelerationMetresPerSecondSquared = 0.0F;
    return settings;
}

TEST(NavAgentTest, Step_WithoutATargetPublishesNothingAndSaysSo) {
    const Navmesh mesh = MakeRoomMesh(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgent agent;
    agent.Configure(MakeAgentSettings());
    agent.SetPosition(Math::Vector3{1.0F, 0.0F, 1.0F});

    agent.Step(mesh, 0.1F);

    EXPECT_EQ(agent.GetStatus(), NavAgentStatus::Idle);
    EXPECT_EQ(agent.GetDesiredVelocity(), Math::Vector3{});
    EXPECT_EQ(agent.GetNextPathPosition(), agent.GetPosition());
    EXPECT_FALSE(agent.HasReachedTarget());
    EXPECT_FALSE(agent.HasPath());
}

TEST(NavAgentTest, Step_WhileDisabledPublishesNothingEvenWithATargetSet) {
    const Navmesh mesh = MakeRoomMesh(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgent agent;
    agent.Configure(MakeAgentSettings());
    agent.SetPosition(Math::Vector3{1.0F, 0.0F, 1.0F});
    agent.SetTarget(Math::Vector3{9.0F, 0.0F, 1.0F});
    agent.SetEnabled(false);

    agent.Step(mesh, 0.1F);

    EXPECT_EQ(agent.GetStatus(), NavAgentStatus::Idle)
        << "a switched-off agent is not failing at anything, it is simply not being driven";
    EXPECT_EQ(agent.GetDesiredVelocity(), Math::Vector3{});
    EXPECT_FALSE(agent.HasPath());

    // Switched back on, the target it was given while off is still its target and the next step
    // searches for it.
    agent.SetEnabled(true);
    agent.Step(mesh, 0.1F);
    EXPECT_EQ(agent.GetStatus(), NavAgentStatus::Following);
    EXPECT_TRUE(agent.HasPath());
}

TEST(NavAgentTest, Step_WalksStraightAtTheWaypointAtTheConfiguredSpeed) {
    const Navmesh mesh = MakeRoomMesh(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgent agent;
    NavAgentSettings settings = MakeAgentSettings();
    settings.maxSpeed = 4.0F;
    agent.Configure(settings);
    agent.SetPosition(Math::Vector3{1.0F, 0.0F, 1.0F});
    agent.SetTarget(Math::Vector3{9.0F, 0.0F, 1.0F});

    agent.Step(mesh, 0.1F);

    EXPECT_EQ(agent.GetStatus(), NavAgentStatus::Following);
    EXPECT_EQ(agent.GetNextPathPosition(), (Math::Vector3{9.0F, 0.0F, 1.0F}));
    EXPECT_NEAR(agent.GetDesiredVelocity().x, 4.0F, kEpsilon);
    EXPECT_NEAR(agent.GetDesiredVelocity().z, 0.0F, kEpsilon);
    EXPECT_NEAR(agent.GetDesiredVelocity().y, 0.0F, kEpsilon)
        << "the agent steers in the plane it walks on; what moves the body off it is the mover's job";
    EXPECT_NEAR(agent.GetRemainingDistance(), 8.0F, kEpsilon);
}

TEST(NavAgentTest, Step_AcceleratesTowardsTheWantedVelocityInsteadOfSnapping_AndTheRampIsTheSetting) {
    const Navmesh mesh = MakeRoomMesh(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgent agent;
    NavAgentSettings settings = MakeAgentSettings();
    settings.maxSpeed = 4.0F;
    settings.accelerationMetresPerSecondSquared = 20.0F;
    agent.Configure(settings);
    agent.SetPosition(Math::Vector3{1.0F, 0.0F, 1.0F});
    agent.SetTarget(Math::Vector3{9.0F, 0.0F, 1.0F});

    agent.Step(mesh, 0.1F);
    EXPECT_NEAR(agent.GetDesiredVelocity().x, 2.0F, kEpsilon)
        << "20 m/s^2 over 0.1 s is 2 m/s, so the first step may not publish more than that";

    agent.Step(mesh, 0.1F);
    EXPECT_NEAR(agent.GetDesiredVelocity().x, 4.0F, kEpsilon) << "the second step reaches top speed";

    // A zero limit is the other contract, and the one a test that means to assert on a direction
    // wants: no ramp at all.
    NavAgent immediate;
    immediate.Configure(MakeAgentSettings());
    immediate.SetPosition(Math::Vector3{1.0F, 0.0F, 1.0F});
    immediate.SetTarget(Math::Vector3{9.0F, 0.0F, 1.0F});
    immediate.Step(mesh, 0.001F);
    EXPECT_NEAR(immediate.GetDesiredVelocity().x, 4.0F, kEpsilon);
}

TEST(NavAgentTest, Step_SlowsDownOverTheLastStretchAndStopsOnTheTarget) {
    const Navmesh mesh = MakeRoomMesh(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgent agent;
    NavAgentSettings settings = MakeAgentSettings();
    settings.maxSpeed = 4.0F;
    settings.slowDownRadiusMetres = 2.0F;
    settings.targetToleranceMetres = 0.5F;
    agent.Configure(settings);
    agent.SetPosition(Math::Vector3{8.0F, 0.0F, 1.0F});
    agent.SetTarget(Math::Vector3{9.0F, 0.0F, 1.0F});

    agent.Step(mesh, 0.1F);
    EXPECT_NEAR(agent.GetDesiredVelocity().x, 2.0F, kEpsilon)
        << "one metre out of a two-metre approach is half speed, so it arrives rather than overshoots";

    // Arrival is measured against the agent's own position, which is what the mover hands back, so
    // moving the body onto the target is the whole of the last step.
    agent.SetPosition(Math::Vector3{8.75F, 0.0F, 1.0F});
    agent.Step(mesh, 0.1F);
    EXPECT_EQ(agent.GetStatus(), NavAgentStatus::Finished);
    EXPECT_TRUE(agent.HasReachedTarget());
    EXPECT_EQ(agent.GetDesiredVelocity(), Math::Vector3{});
    EXPECT_NEAR(agent.GetRemainingDistance(), 0.25F, kEpsilon);
}

TEST(NavAgentTest, Step_SaysWhichEndIsOffTheMeshRatherThanAnsweringWithAPathFromSomewhereElse) {
    const Navmesh mesh = MakeRoomMesh(0.0F, 0.0F, 10.0F, 10.0F);

    NavAgent stranded;
    stranded.Configure(MakeAgentSettings());
    stranded.SetPosition(Math::Vector3{-20.0F, 0.0F, -20.0F});
    stranded.SetTarget(Math::Vector3{5.0F, 0.0F, 5.0F});
    stranded.Step(mesh, 0.1F);
    EXPECT_EQ(stranded.GetStatus(), NavAgentStatus::Failed);
    EXPECT_FALSE(stranded.HasPath());
    EXPECT_EQ(stranded.GetDesiredVelocity(), Math::Vector3{});

    // A target within the off-mesh tolerance is not stranded: a script that wrote a waypoint
    // slightly off the edge still gets an agent that walks to it.
    NavAgent reaching;
    reaching.Configure(MakeAgentSettings());
    reaching.SetPosition(Math::Vector3{5.0F, 0.0F, 5.0F});
    reaching.SetTarget(Math::Vector3{11.0F, 0.0F, 5.0F});
    reaching.Step(mesh, 0.1F);
    EXPECT_EQ(reaching.GetStatus(), NavAgentStatus::Following);
    EXPECT_EQ(reaching.GetNextPathPosition(), (Math::Vector3{10.0F, 0.0F, 5.0F}))
        << "the goal is brought onto the mesh, so the agent walks to the edge rather than into the void";
}

TEST(NavAgentTest, Step_TurnsTheCornerOfATurningCorridorAndTurnsToTheNextWaypoint) {
    const Navmesh mesh = MakeLShapedMesh();
    NavAgent agent;
    NavAgentSettings settings = MakeAgentSettings();
    // Long enough that no step below searches again: this test is about walking the route it was
    // given, and a replan in the middle of it would be a different test.
    settings.pathUpdateInterval = 5.0F;
    agent.Configure(settings);
    agent.SetPosition(Math::Vector3{0.5F, 0.0F, 1.0F});
    agent.SetTarget(Math::Vector3{5.0F, 0.0F, 5.0F});

    agent.Step(mesh, 0.1F);
    ASSERT_EQ(agent.GetStatus(), NavAgentStatus::Following);
    ASSERT_TRUE(agent.HasPath());
    EXPECT_TRUE(agent.GetPathChanged())
        << "the first search found a route where there was none, which is the change a script waits for";
    ASSERT_GE(agent.GetPath().waypoints.size(), 2U) << "the walk turns";
    EXPECT_EQ(agent.GetWaypointIndex(), 0U);

    // The straight line to the goal leaves the level through the inside wall, so the waypoint the
    // agent steers at is the corner it has to walk around - not the goal, and not the middle of the
    // corridor it is standing in.
    EXPECT_NEAR(agent.GetNextPathPosition().x, 4.0F, 0.05F);
    EXPECT_NEAR(agent.GetNextPathPosition().z, 2.0F, 0.05F);
    EXPECT_NEAR(agent.GetDesiredVelocity().y, 0.0F, kEpsilon);

    // Reaching the corner moves the agent to the next waypoint on the route; standing near it is
    // enough, which is what the waypoint radius is for.
    agent.SetPosition(Math::Vector3{4.2F, 0.0F, 2.1F});
    agent.Step(mesh, 0.1F);
    EXPECT_EQ(agent.GetWaypointIndex(), 1U);
    EXPECT_EQ(agent.GetNextPathPosition(), (Math::Vector3{5.0F, 0.0F, 5.0F}));
    EXPECT_TRUE(agent.HasPath());
}

TEST(NavAgentTest, Step_ReplansOnTheConfiguredIntervalAndOnlyThen) {
    // A corridor that turns: from the bottom arm the walk has a corner in it, from the top arm it is
    // a straight line, so moving the agent really does make a different route.
    const Navmesh mesh = MakeLShapedMesh();
    NavAgent agent;
    NavAgentSettings settings = MakeAgentSettings();
    settings.pathUpdateInterval = 0.5F;
    agent.Configure(settings);
    agent.SetPosition(Math::Vector3{0.5F, 0.0F, 1.0F});
    agent.SetTarget(Math::Vector3{5.0F, 0.0F, 5.0F});

    agent.Step(mesh, 0.1F);
    ASSERT_EQ(agent.GetPath().waypoints.size(), 2U) << "the bottom arm's route turns the corner";

    // Inside the interval nothing is searched, so the flag stays down however the agent moves: the
    // route it has is the route it walks.
    agent.SetPosition(Math::Vector3{0.6F, 0.0F, 1.0F});
    agent.Step(mesh, 0.1F);
    EXPECT_FALSE(agent.GetPathChanged());
    agent.Step(mesh, 0.1F);
    EXPECT_FALSE(agent.GetPathChanged());

    // Past the interval the route is searched again. It is the same route - the agent moved a
    // decimetre - so the flag is still down: the flag reports a change of route, not a search.
    agent.Step(mesh, 0.3F);
    EXPECT_FALSE(agent.GetPathChanged());

    // Now the route really changes: the agent is put in the arm the target is in, so the walk no
    // longer goes round the corner. Inside the interval that is not seen yet - which is the whole
    // point of the setting - and past it the search reports the new route.
    agent.SetPosition(Math::Vector3{4.5F, 0.0F, 3.0F});
    agent.Step(mesh, 0.1F);
    EXPECT_FALSE(agent.GetPathChanged()) << "inside the interval, the route is the route it has";
    agent.Step(mesh, 0.7F);
    EXPECT_TRUE(agent.GetPathChanged()) << "past it, the search sees the agent is somewhere else";
    ASSERT_EQ(agent.GetPath().waypoints.size(), 1U);
    EXPECT_EQ(agent.GetNextPathPosition(), (Math::Vector3{5.0F, 0.0F, 5.0F}));
}

TEST(NavAgentTest, SetTarget_SearchesOnTheNextStepWithoutWaitingOutTheInterval) {
    const Navmesh mesh = MakeRoomMesh(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgent agent;
    NavAgentSettings settings = MakeAgentSettings();
    settings.pathUpdateInterval = 5.0F;
    agent.Configure(settings);
    agent.SetPosition(Math::Vector3{1.0F, 0.0F, 1.0F});
    agent.SetTarget(Math::Vector3{4.0F, 0.0F, 1.0F});
    agent.Step(mesh, 0.1F);
    EXPECT_EQ(agent.GetNextPathPosition(), (Math::Vector3{4.0F, 0.0F, 1.0F}));

    // Five seconds of interval must not hold a new order back: the target moved, so the route has to.
    agent.SetTarget(Math::Vector3{9.0F, 0.0F, 1.0F});
    agent.Step(mesh, 0.1F);
    EXPECT_EQ(agent.GetNextPathPosition(), (Math::Vector3{9.0F, 0.0F, 1.0F}));
    EXPECT_TRUE(agent.GetPathChanged());
    EXPECT_NEAR(agent.GetDesiredVelocity().x, 4.0F, kEpsilon);
}

TEST(NavAgentTest, Step_HonoursTheAgentsOwnLayersRatherThanTheMeshes) {
    // The east room is on layer 4 and this agent may walk layer 1 only, so the doorway is a wall as
    // far as it is concerned: a path that stops at the boundary, not one that walks through the room
    // and not a refusal either.
    const Navmesh mesh = MakeTwoRoomMesh(1U, 4U);
    NavAgent agent;
    NavAgentSettings settings = MakeAgentSettings();
    settings.navigationLayers = 1U;
    agent.Configure(settings);
    agent.SetPosition(Math::Vector3{1.0F, 0.0F, 2.0F});
    agent.SetTarget(Math::Vector3{7.0F, 0.0F, 2.0F});

    agent.Step(mesh, 0.1F);
    EXPECT_EQ(agent.GetStatus(), NavAgentStatus::Following);
    EXPECT_NEAR(agent.GetNextPathPosition().x, 4.0F, kEpsilon);

    // Widening the agent's own layers drops the route it had and searches again, and this time the
    // east room is walkable, so the goal becomes reachable without anything else changing.
    agent.SetNavigationLayers(5U);
    agent.Step(mesh, 0.1F);
    EXPECT_EQ(agent.GetNextPathPosition(), (Math::Vector3{7.0F, 0.0F, 2.0F}));
    EXPECT_TRUE(agent.GetPathChanged());

    // Narrowing them again takes the room away, so the same walk stops at the doorway: the search
    // is re-run because the layers changed, and the agent does not keep a route it may not use.
    agent.SetNavigationLayers(1U);
    agent.Step(mesh, 0.1F);
    EXPECT_NEAR(agent.GetNextPathPosition().x, 4.0F, kEpsilon);
    EXPECT_TRUE(agent.GetPathChanged());
}

TEST(NavAgentTest, Step_WithNeighboursStepsAsideInsteadOfWalkingThroughThem) {
    const Navmesh mesh = MakeRoomMesh(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgent agent;
    NavAgentSettings settings = MakeAgentSettings();
    settings.maxSpeed = 4.0F;
    settings.avoidanceRadiusMetres = 2.0F;
    agent.Configure(settings);
    agent.SetPosition(Math::Vector3{1.0F, 0.0F, 1.0F});
    agent.SetTarget(Math::Vector3{9.0F, 0.0F, 1.0F});

    // A colleague one metre to -Z, inside the two-metre avoidance radius: walking exactly at the
    // target would put this agent through it.
    const std::vector<NavAgentObstacle> neighbours{NavAgentObstacle{Math::Vector3{1.0F, 0.0F, 0.0F}, 0.5F},
                                                      NavAgentObstacle{Math::Vector3{9.0F, 0.0F, 8.0F}, 0.5F}};
    agent.Step(mesh, neighbours, 0.1F);

    EXPECT_GT(agent.GetDesiredVelocity().z, 0.0F) << "the push is away from the colleague";
    EXPECT_LT(agent.GetDesiredVelocity().x, 4.0F) << "and it comes out of forward speed";
    const Math::Vector3 velocity = agent.GetDesiredVelocity();
    const float speedSquared = velocity.x * velocity.x + velocity.z * velocity.z;
    EXPECT_LE(speedSquared, 16.0F + kEpsilon) << "avoidance may not make the agent faster than it walks";
    EXPECT_EQ(agent.GetStatus(), NavAgentStatus::Following)
        << "stepping aside is still following the route: the push never becomes a new path";
    EXPECT_EQ(agent.GetNextPathPosition(), (Math::Vector3{9.0F, 0.0F, 1.0F}));
}

TEST(NavAgentTest, Step_IgnoresAgentsOutsideTheAvoidanceRadiusAndItsOwnEntryInTheList) {
    const Navmesh mesh = MakeRoomMesh(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgent agent;
    NavAgentSettings settings = MakeAgentSettings();
    settings.maxSpeed = 4.0F;
    settings.avoidanceRadiusMetres = 1.0F;
    agent.Configure(settings);
    agent.SetPosition(Math::Vector3{1.0F, 0.0F, 1.0F});
    agent.SetTarget(Math::Vector3{9.0F, 0.0F, 1.0F});

    // Its own position, as a caller that passes every agent in the scene would give it, plus a
    // colleague far outside the radius: neither may push.
    const std::vector<NavAgentObstacle> neighbours{
        NavAgentObstacle{Math::Vector3{1.0F, 0.0F, 1.0F}, 0.5F},
        NavAgentObstacle{Math::Vector3{1.0F, 0.0F, 4.0F}, 0.5F}};
    agent.Step(mesh, neighbours, 0.1F);

    EXPECT_NEAR(agent.GetDesiredVelocity().x, 4.0F, kEpsilon);
    EXPECT_NEAR(agent.GetDesiredVelocity().z, 0.0F, kEpsilon);
}

TEST(NavAgentTest, Reset_ForgetsTheTargetAndEverythingPublishedForIt) {
    const Navmesh mesh = MakeRoomMesh(0.0F, 0.0F, 10.0F, 10.0F);
    NavAgent agent;
    agent.Configure(MakeAgentSettings());
    agent.SetPosition(Math::Vector3{1.0F, 0.0F, 1.0F});
    agent.SetTarget(Math::Vector3{9.0F, 0.0F, 1.0F});
    agent.Step(mesh, 0.1F);
    ASSERT_TRUE(agent.HasPath());

    agent.Reset();

    EXPECT_FALSE(agent.HasTarget());
    EXPECT_FALSE(agent.HasPath());
    EXPECT_EQ(agent.GetStatus(), NavAgentStatus::Idle);
    EXPECT_EQ(agent.GetDesiredVelocity(), Math::Vector3{});
    EXPECT_EQ(agent.GetNextPathPosition(), Math::Vector3{});
    EXPECT_TRUE(agent.IsEnabled()) << "a reset agent is a fresh agent, not a switched-off one";
}

} // namespace
} // namespace UVE::Navigation::Tests
