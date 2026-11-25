// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Tests for the navigation runtime: the ECS half where a region becomes a cached navmesh and an
// agent becomes a steering state that publishes into its component.
//
// Everything here runs against the engine's real Physics::RaycastSystem and real collider
// entities, not a mocked query: the bake's whole contract is that it samples the collision world the
// way a body standing there would, and a fake raycast would test the fake instead of that contract.
// What is asserted is what a caller of EngineCore::SyncNavigation() can observe - the counters
// it reports, the mesh a region holds, and the fields the agent writes back.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "collider_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "vector3.h"
#include "memory_manager.h"
#include "navigation_runtime.h"
#include "nav_mesh_volume_3d.h"
#include "nav_seeker_3d.h"
#include "raycast_system.h"
#include "scene_graph.h"

namespace UVE::Navigation::Tests {
namespace {

using Scene::NavMeshVolume3DComponent;
using Scene::NavSeeker3DComponent;

constexpr float kEpsilon = 1e-3F;

class NavigationRuntimeTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    Physics::RaycastSystem raycastSystem;
    NavigationRuntime runtime;

    /// A floor whose top surface is exactly y = 0: a box half a metre thick sitting under the world
    /// origin, wide enough that no test's region reaches its edge unless it means to.
    Scene::Entity MakeFloor(const float halfExtentXZ = 10.0F) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = Math::Vector3{0.0F, -0.5F, 0.0F};
        sceneGraph.AttachTransform(entityManager, entity, local);
        entityManager.AddComponent<Scene::ColliderComponent>(
            entity, Scene::ColliderComponent{Math::Vector3{halfExtentXZ, 0.5F, halfExtentXZ}});
        sceneGraph.Update(entityManager);
        return entity;
    }

    /// A region centred on `position`, spanning `halfExtentXZ` in the plane. Its volume is tall
    /// enough above the floor for the default 1.8 m agent - a region whose roof is lower than the
    /// agent is tall bakes nothing, which the bake suite already covers.
    Scene::Entity MakeRegion(const Math::Vector3& position = Math::Vector3{0.0F, 0.5F, 0.0F},
                                   const float halfExtentXZ = 8.0F, const std::uint32_t layers = 1U) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, local);
        NavMeshVolume3DComponent region;
        region.boundsHalfExtents = Math::Vector3{halfExtentXZ, 2.5F, halfExtentXZ};
        region.navigationLayers = layers;
        entityManager.AddComponent<NavMeshVolume3DComponent>(entity, region);
        sceneGraph.Update(entityManager);
        return entity;
    }

    /// An agent standing at `position` and asked for `target`, with no collider of its own: what
    /// moves a seeker's body is some other component's business, and the steering only publishes.
    Scene::Entity MakeAgent(const Math::Vector3& position, const Math::Vector3& target,
                                  const bool avoidanceEnabled = true) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, local);
        NavSeeker3DComponent seeker;
        seeker.targetPosition = target;
        seeker.avoidanceEnabled = avoidanceEnabled;
        entityManager.AddComponent<NavSeeker3DComponent>(entity, seeker);
        sceneGraph.Update(entityManager);
        return entity;
    }

    /// Turns off an agent's velocity ramp, so the step that follows publishes the velocity the
    /// steering wants in one go.
    void MakeImmediate(const Scene::Entity entity) {
        entityManager.GetComponent<NavSeeker3DComponent>(entity).acceleration = 0.0F;
    }

    /// One navigation step. The report is returned rather than asserted on here: most cases care
    /// about what the agent's component says afterwards, and the few that care about the counters
    /// read them.
    NavigationSyncReport Step(const std::vector<Scene::Entity>& agents,
                                    const float deltaTimeSeconds = 0.1F) {
        return runtime.Sync(entityManager, raycastSystem, agents, deltaTimeSeconds);
    }

    void Move(const Scene::Entity entity, const Math::Vector3& position) {
        Scene::TransformComponent local;
        local.localPosition = position;
        sceneGraph.SetLocalTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
    }
};

TEST_F(NavigationRuntimeTest, Sync_BakesARegionOnFirstSightThenStepsAnAgentAlongItsPath) {
    MakeFloor();
    const Scene::Entity region = MakeRegion();
    const Scene::Entity agent = MakeAgent(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                Math::Vector3{5.0F, 0.0F, 0.0F});

    const NavigationSyncReport report = Step({agent});
    EXPECT_EQ(report.regions, 1U);
    EXPECT_EQ(report.bakes, 1U) << "a region is rasterized the first time it is seen";
    EXPECT_EQ(report.agents, 1U);
    EXPECT_EQ(report.agentsWithoutMesh, 0U);

    const Navmesh* mesh = runtime.FindRegionMesh(region);
    ASSERT_NE(mesh, nullptr);
    EXPECT_GT(mesh->polygons.size(), 0U) << "a flat floor one region wide is walkable ground";
    EXPECT_FALSE(mesh->IsEmpty());

    const NavSeeker3DComponent& seeker = entityManager.GetComponent<NavSeeker3DComponent>(agent);
    EXPECT_EQ(seeker.pathStatus, Scene::NavigationAgentPathStatus::Following);
    EXPECT_TRUE(seeker.pathChanged) << "the first search found a route where there was none";
    EXPECT_FALSE(seeker.targetReached);
    EXPECT_GT(seeker.desiredVelocity.x, 0.0F) << "the target is straight ahead";
    EXPECT_NEAR(seeker.desiredVelocity.z, 0.0F, kEpsilon);
    EXPECT_NEAR(seeker.nextPathPosition.x, 5.0F, 0.1F);

    // The second step finds the mesh already there and the route unchanged: no bake, no change flag.
    const NavigationSyncReport second = Step({agent});
    EXPECT_EQ(second.bakes, 0U) << "a navmesh is a property of the level, not of the frame";
    EXPECT_EQ(second.agents, 1U);
    EXPECT_FALSE(entityManager.GetComponent<NavSeeker3DComponent>(agent).pathChanged);
}

TEST_F(NavigationRuntimeTest, Sync_RebakesAnUnchangedRegionOnlyWhenItsOwnRequestAsks) {
    MakeFloor();
    const Scene::Entity region = MakeRegion();
    const Scene::Entity agent = MakeAgent(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                Math::Vector3{5.0F, 0.0F, 0.0F});
    Step({agent});
    ASSERT_EQ(Step({agent}).bakes, 0U);

    NavMeshVolume3DComponent& component = entityManager.GetComponent<NavMeshVolume3DComponent>(region);
    component.rebuildRequested = true;
    const NavigationSyncReport asked = Step({agent});
    EXPECT_EQ(asked.bakes, 1U)
        << "an author asking for a rebuild gets one even though nothing this class can see changed";
    EXPECT_FALSE(entityManager.GetComponent<NavMeshVolume3DComponent>(region).rebuildRequested)
        << "the request is a request: the runtime clears it once the bake has run";
    EXPECT_EQ(Step({agent}).bakes, 0U) << "and it is not re-raised by being read";
}

TEST_F(NavigationRuntimeTest, Sync_FollowsARegionThatMovesInsteadOfServingAMeshFromWhereItWas) {
    MakeFloor(24.0F);
    const Scene::Entity region = MakeRegion();
    const Scene::Entity agent = MakeAgent(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                Math::Vector3{5.0F, 0.0F, 0.0F});
    Step({agent});
    const Navmesh first = *runtime.FindRegionMesh(region);

    Move(region, Math::Vector3{12.0F, 0.5F, 0.0F});
    const NavigationSyncReport moved = Step({agent});
    EXPECT_EQ(moved.bakes, 1U) << "the volume's world bounds changed, so the mesh has to";
    const Navmesh* after = runtime.FindRegionMesh(region);
    ASSERT_NE(after, nullptr);
    EXPECT_NE(after->bounds.min.x, first.bounds.min.x);
    EXPECT_NEAR(after->bounds.min.x, 4.0F, kEpsilon) << "12 - 8: the mesh followed its region";
}

TEST_F(NavigationRuntimeTest, Sync_ASwitchedOffRegionTakesItsMeshAwayAndFailsItsAgents) {
    MakeFloor();
    const Scene::Entity region = MakeRegion();
    const Scene::Entity agent = MakeAgent(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                Math::Vector3{5.0F, 0.0F, 0.0F});
    Step({agent});
    ASSERT_NE(runtime.FindRegionMesh(region), nullptr);

    entityManager.GetComponent<NavMeshVolume3DComponent>(region).enabled = false;
    const NavigationSyncReport off = Step({agent});
    EXPECT_EQ(off.regions, 0U);
    EXPECT_EQ(runtime.FindRegionMesh(region), nullptr)
        << "a region switched off is not a region an agent may walk on";
    EXPECT_EQ(off.agentsWithoutMesh, 1U);

    const NavSeeker3DComponent& seeker = entityManager.GetComponent<NavSeeker3DComponent>(agent);
    EXPECT_EQ(seeker.pathStatus, Scene::NavigationAgentPathStatus::Failed);
    EXPECT_EQ(seeker.desiredVelocity, Math::Vector3{})
        << "the agent stops rather than keeping the velocity it had when the ground went away";
}

TEST_F(NavigationRuntimeTest, Sync_AnAgentWithNoRegionUnderItFailsInsteadOfWalkingOffTheMesh) {
    MakeFloor();
    MakeRegion(Math::Vector3{0.0F, 0.5F, 0.0F}, 3.0F);
    const Scene::Entity stranded = MakeAgent(Math::Vector3{40.0F, 0.0F, 40.0F},
                                                   Math::Vector3{45.0F, 0.0F, 40.0F});

    const NavigationSyncReport report = Step({stranded});
    EXPECT_EQ(report.bakes, 1U) << "the region still bakes; it is the agent that is nowhere near it";
    EXPECT_EQ(report.agents, 1U);
    EXPECT_EQ(report.agentsWithoutMesh, 1U);

    const NavSeeker3DComponent& seeker = entityManager.GetComponent<NavSeeker3DComponent>(stranded);
    EXPECT_EQ(seeker.pathStatus, Scene::NavigationAgentPathStatus::Failed);
    EXPECT_EQ(seeker.desiredVelocity, Math::Vector3{});
    EXPECT_FALSE(seeker.pathChanged) << "nothing was found, so nothing changed";
}

TEST_F(NavigationRuntimeTest, Sync_LeavesAnAgentWithoutATransformExactlyAsAuthored) {
    MakeFloor();
    MakeRegion();
    // A seeker component on an entity with no pose at all: the runtime never adds the transform it
    // would need, so the entity - and the values an author or a script put there - stay untouched.
    const Scene::Entity entity = entityManager.CreateEntity();
    NavSeeker3DComponent seeker;
    seeker.desiredVelocity = Math::Vector3{1.0F, 2.0F, 3.0F};
    seeker.pathStatus = Scene::NavigationAgentPathStatus::Finished;
    entityManager.AddComponent<NavSeeker3DComponent>(entity, seeker);

    const NavigationSyncReport report = Step({entity});
    EXPECT_EQ(report.agents, 0U);
    const NavSeeker3DComponent& after = entityManager.GetComponent<NavSeeker3DComponent>(entity);
    EXPECT_EQ(after.desiredVelocity, (Math::Vector3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(after.pathStatus, Scene::NavigationAgentPathStatus::Finished);
}

TEST_F(NavigationRuntimeTest, Sync_SwitchedOffAgentsPublishNothingAndForgetTheirRoute) {
    MakeFloor();
    MakeRegion();
    const Scene::Entity agent = MakeAgent(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                Math::Vector3{5.0F, 0.0F, 0.0F});
    Step({agent});
    ASSERT_EQ(runtime.GetAgentCount(), 1U);

    entityManager.GetComponent<NavSeeker3DComponent>(agent).enabled = false;
    Step({agent});

    const NavSeeker3DComponent& seeker = entityManager.GetComponent<NavSeeker3DComponent>(agent);
    EXPECT_EQ(seeker.pathStatus, Scene::NavigationAgentPathStatus::Idle);
    EXPECT_EQ(seeker.desiredVelocity, Math::Vector3{});
    EXPECT_EQ(seeker.nextPathPosition, entityManager.GetComponent<Scene::WorldTransformComponent>(agent).worldPosition);
    EXPECT_FALSE(seeker.targetReached);
    EXPECT_EQ(runtime.GetAgentCount(), 0U)
        << "the steering state is dropped, so enabling it again starts from a clean search";
}

TEST_F(NavigationRuntimeTest, Sync_AvoidancePartsAgentsThatWouldOtherwiseShareAPath) {
    MakeFloor();
    MakeRegion();
    // Two agents one metre apart in Z, both walking along X. The one behind the other's shoulder has
    // somewhere to step aside to; with avoidance off it walks straight and the two overlap.
    const Scene::Entity steppingAside = MakeAgent(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                        Math::Vector3{5.0F, 0.0F, 0.0F});
    const Scene::Entity neighbour = MakeAgent(Math::Vector3{0.0F, 0.0F, -1.2F},
                                                   Math::Vector3{0.0F, 0.0F, -5.0F});
    // The velocity ramp is the steering suite's own subject; this case is about the push, so these
    // agents publish the velocity they want rather than one step of reaching it.
    MakeImmediate(steppingAside);
    MakeImmediate(neighbour);
    Step({steppingAside, neighbour});

    const NavSeeker3DComponent& avoided = entityManager.GetComponent<NavSeeker3DComponent>(steppingAside);
    EXPECT_GT(avoided.desiredVelocity.z, 0.0F)
        << "the push is away from the neighbour, which is on the -Z side";
    EXPECT_LT(avoided.desiredVelocity.x, 4.0F) << "and it costs forward speed rather than adding any";
    const float avoidedSpeedSquared = avoided.desiredVelocity.x * avoided.desiredVelocity.x +
                                      avoided.desiredVelocity.z * avoided.desiredVelocity.z;
    EXPECT_LE(avoidedSpeedSquared, 4.0F * 4.0F + kEpsilon)
        << "a neighbour is something to walk around, not a reason to exceed the agent's own top speed";

    // The same layout with avoidance switched off: the agent walks straight at its target.
    const Scene::Entity straight = MakeAgent(Math::Vector3{5.0F, 0.0F, 6.0F},
                                                   Math::Vector3{9.0F, 0.0F, 6.0F}, /*avoidanceEnabled=*/false);
    const Scene::Entity ignoredNeighbour = MakeAgent(Math::Vector3{5.0F, 0.0F, 4.8F},
                                                           Math::Vector3{5.0F, 0.0F, 0.0F});
    MakeImmediate(straight);
    MakeImmediate(ignoredNeighbour);
    Step({straight, ignoredNeighbour});
    const NavSeeker3DComponent& unaware = entityManager.GetComponent<NavSeeker3DComponent>(straight);
    EXPECT_NEAR(unaware.desiredVelocity.z, 0.0F, kEpsilon);
    EXPECT_NEAR(unaware.desiredVelocity.x, 4.0F, kEpsilon);
}

TEST_F(NavigationRuntimeTest, FindRegionForPosition_PrefersTheVolumeItIsInsideOverTheNearerEdge) {
    MakeFloor(24.0F);
    const Scene::Entity west = MakeRegion(Math::Vector3{0.0F, 0.5F, 0.0F}, 4.0F);
    const Scene::Entity east = MakeRegion(Math::Vector3{20.0F, 0.5F, 0.0F}, 4.0F);
    Step({});

    EXPECT_EQ(runtime.FindRegionForPosition(Math::Vector3{0.0F, 0.0F, 0.0F}, 2.0F), west);
    EXPECT_EQ(runtime.FindRegionForPosition(Math::Vector3{20.0F, 0.0F, 0.0F}, 2.0F), east);
    // Beside the east volume, inside the tolerance: the nearest edge wins.
    EXPECT_EQ(runtime.FindRegionForPosition(Math::Vector3{25.0F, 0.0F, 0.0F}, 2.0F), east);
    // Past the tolerance there is nothing to navigate on, and saying so beats returning a stale edge.
    EXPECT_EQ(runtime.FindRegionForPosition(Math::Vector3{40.0F, 0.0F, 0.0F}, 2.0F), Scene::kInvalidEntity);
}

TEST_F(NavigationRuntimeTest, Clear_ForgetsEveryMeshAndAgent) {
    MakeFloor();
    const Scene::Entity region = MakeRegion();
    const Scene::Entity agent = MakeAgent(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                Math::Vector3{5.0F, 0.0F, 0.0F});
    Step({agent});
    ASSERT_EQ(runtime.GetRegionCount(), 1U);
    ASSERT_EQ(runtime.GetAgentCount(), 1U);

    runtime.Clear();

    EXPECT_EQ(runtime.GetRegionCount(), 0U);
    EXPECT_EQ(runtime.GetAgentCount(), 0U);
    EXPECT_EQ(runtime.FindRegionMesh(region), nullptr);
    // The next step rebuilds from nothing, since it has nothing left to serve.
    EXPECT_EQ(Step({agent}).bakes, 1U);
}

} // namespace
} // namespace UVE::Navigation::Tests
