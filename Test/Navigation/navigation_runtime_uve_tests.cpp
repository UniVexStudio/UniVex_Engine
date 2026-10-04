// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Tests for the navigation runtime: the ECS half where a region becomes a cached navmesh and an
// agent becomes a steering state that publishes into its component.
//
// Everything here runs against the engine's real Physics::RaycastSystemUVE and real collider
// entities, not a mocked query: the bake's whole contract is that it samples the collision world the
// way a body standing there would, and a fake raycast would test the fake instead of that contract.
// What is asserted is what a caller of EngineCoreUVE::SyncNavigationUVE() can observe - the counters
// it reports, the mesh a region holds, and the fields the agent writes back.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/collider_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/navigation/navigation_runtime_uve.h"
#include "uve/objects/3d/nav_mesh_volume_3d_uve.h"
#include "uve/objects/3d/nav_seeker_3d_uve.h"
#include "uve/physics/raycast_system_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Navigation::Tests {
namespace {

using Scene::NavMeshVolume3DComponentUVE;
using Scene::NavSeeker3DComponentUVE;

constexpr float kEpsilon = 1e-3F;

class NavigationRuntimeUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    Physics::RaycastSystemUVE raycastSystem;
    NavigationRuntimeUVE runtime;

    /// A floor whose top surface is exactly y = 0: a box half a metre thick sitting under the world
    /// origin, wide enough that no test's region reaches its edge unless it means to.
    Scene::EntityUVE MakeFloorUVE(const float halfExtentXZ = 10.0F) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = Math::Vector3UVE{0.0F, -0.5F, 0.0F};
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(
            entity, Scene::ColliderComponentUVE{Math::Vector3UVE{halfExtentXZ, 0.5F, halfExtentXZ}});
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    /// A region centred on `position`, spanning `halfExtentXZ` in the plane. Its volume is tall
    /// enough above the floor for the default 1.8 m agent - a region whose roof is lower than the
    /// agent is tall bakes nothing, which the bake suite already covers.
    Scene::EntityUVE MakeRegionUVE(const Math::Vector3UVE& position = Math::Vector3UVE{0.0F, 0.5F, 0.0F},
                                   const float halfExtentXZ = 8.0F, const std::uint32_t layers = 1U) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        NavMeshVolume3DComponentUVE region;
        region.boundsHalfExtents = Math::Vector3UVE{halfExtentXZ, 2.5F, halfExtentXZ};
        region.navigationLayers = layers;
        entityManager.AddComponentUVE<NavMeshVolume3DComponentUVE>(entity, region);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    /// An agent standing at `position` and asked for `target`, with no collider of its own: what
    /// moves a seeker's body is some other component's business, and the steering only publishes.
    Scene::EntityUVE MakeAgentUVE(const Math::Vector3UVE& position, const Math::Vector3UVE& target,
                                  const bool avoidanceEnabled = true) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        NavSeeker3DComponentUVE seeker;
        seeker.targetPosition = target;
        seeker.avoidanceEnabled = avoidanceEnabled;
        entityManager.AddComponentUVE<NavSeeker3DComponentUVE>(entity, seeker);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    /// Turns off an agent's velocity ramp, so the step that follows publishes the velocity the
    /// steering wants in one go.
    void MakeImmediateUVE(const Scene::EntityUVE entity) {
        entityManager.GetComponentUVE<NavSeeker3DComponentUVE>(entity).acceleration = 0.0F;
    }

    /// One navigation step. The report is returned rather than asserted on here: most cases care
    /// about what the agent's component says afterwards, and the few that care about the counters
    /// read them.
    NavigationSyncReportUVE StepUVE(const std::vector<Scene::EntityUVE>& agents,
                                    const float deltaTimeSeconds = 0.1F) {
        return runtime.SyncUVE(entityManager, raycastSystem, agents, deltaTimeSeconds);
    }

    void MoveUVE(const Scene::EntityUVE entity, const Math::Vector3UVE& position) {
        Scene::TransformComponentUVE local;
        local.localPosition = position;
        sceneGraph.SetLocalTransformUVE(entityManager, entity, local);
        sceneGraph.UpdateUVE(entityManager);
    }
};

TEST_F(NavigationRuntimeUVETest, SyncUVE_BakesARegionOnFirstSightThenStepsAnAgentAlongItsPath) {
    MakeFloorUVE();
    const Scene::EntityUVE region = MakeRegionUVE();
    const Scene::EntityUVE agent = MakeAgentUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F},
                                                Math::Vector3UVE{5.0F, 0.0F, 0.0F});

    const NavigationSyncReportUVE report = StepUVE({agent});
    EXPECT_EQ(report.regions, 1U);
    EXPECT_EQ(report.bakes, 1U) << "a region is rasterized the first time it is seen";
    EXPECT_EQ(report.agents, 1U);
    EXPECT_EQ(report.agentsWithoutMesh, 0U);

    const NavmeshUVE* mesh = runtime.FindRegionMeshUVE(region);
    ASSERT_NE(mesh, nullptr);
    EXPECT_GT(mesh->polygons.size(), 0U) << "a flat floor one region wide is walkable ground";
    EXPECT_FALSE(mesh->IsEmptyUVE());

    const NavSeeker3DComponentUVE& seeker = entityManager.GetComponentUVE<NavSeeker3DComponentUVE>(agent);
    EXPECT_EQ(seeker.pathStatus, Scene::NavigationAgentPathStatusUVE::Following);
    EXPECT_TRUE(seeker.pathChanged) << "the first search found a route where there was none";
    EXPECT_FALSE(seeker.targetReached);
    EXPECT_GT(seeker.desiredVelocity.x, 0.0F) << "the target is straight ahead";
    EXPECT_NEAR(seeker.desiredVelocity.z, 0.0F, kEpsilon);
    EXPECT_NEAR(seeker.nextPathPosition.x, 5.0F, 0.1F);

    // The second step finds the mesh already there and the route unchanged: no bake, no change flag.
    const NavigationSyncReportUVE second = StepUVE({agent});
    EXPECT_EQ(second.bakes, 0U) << "a navmesh is a property of the level, not of the frame";
    EXPECT_EQ(second.agents, 1U);
    EXPECT_FALSE(entityManager.GetComponentUVE<NavSeeker3DComponentUVE>(agent).pathChanged);
}

TEST_F(NavigationRuntimeUVETest, SyncUVE_RebakesAnUnchangedRegionOnlyWhenItsOwnRequestAsks) {
    MakeFloorUVE();
    const Scene::EntityUVE region = MakeRegionUVE();
    const Scene::EntityUVE agent = MakeAgentUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F},
                                                Math::Vector3UVE{5.0F, 0.0F, 0.0F});
    StepUVE({agent});
    ASSERT_EQ(StepUVE({agent}).bakes, 0U);

    NavMeshVolume3DComponentUVE& component = entityManager.GetComponentUVE<NavMeshVolume3DComponentUVE>(region);
    component.rebuildRequested = true;
    const NavigationSyncReportUVE asked = StepUVE({agent});
    EXPECT_EQ(asked.bakes, 1U)
        << "an author asking for a rebuild gets one even though nothing this class can see changed";
    EXPECT_FALSE(entityManager.GetComponentUVE<NavMeshVolume3DComponentUVE>(region).rebuildRequested)
        << "the request is a request: the runtime clears it once the bake has run";
    EXPECT_EQ(StepUVE({agent}).bakes, 0U) << "and it is not re-raised by being read";
}

TEST_F(NavigationRuntimeUVETest, SyncUVE_FollowsARegionThatMovesInsteadOfServingAMeshFromWhereItWas) {
    MakeFloorUVE(24.0F);
    const Scene::EntityUVE region = MakeRegionUVE();
    const Scene::EntityUVE agent = MakeAgentUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F},
                                                Math::Vector3UVE{5.0F, 0.0F, 0.0F});
    StepUVE({agent});
    const NavmeshUVE first = *runtime.FindRegionMeshUVE(region);

    MoveUVE(region, Math::Vector3UVE{12.0F, 0.5F, 0.0F});
    const NavigationSyncReportUVE moved = StepUVE({agent});
    EXPECT_EQ(moved.bakes, 1U) << "the volume's world bounds changed, so the mesh has to";
    const NavmeshUVE* after = runtime.FindRegionMeshUVE(region);
    ASSERT_NE(after, nullptr);
    EXPECT_NE(after->bounds.min.x, first.bounds.min.x);
    EXPECT_NEAR(after->bounds.min.x, 4.0F, kEpsilon) << "12 - 8: the mesh followed its region";
}

TEST_F(NavigationRuntimeUVETest, SyncUVE_ASwitchedOffRegionTakesItsMeshAwayAndFailsItsAgents) {
    MakeFloorUVE();
    const Scene::EntityUVE region = MakeRegionUVE();
    const Scene::EntityUVE agent = MakeAgentUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F},
                                                Math::Vector3UVE{5.0F, 0.0F, 0.0F});
    StepUVE({agent});
    ASSERT_NE(runtime.FindRegionMeshUVE(region), nullptr);

    entityManager.GetComponentUVE<NavMeshVolume3DComponentUVE>(region).enabled = false;
    const NavigationSyncReportUVE off = StepUVE({agent});
    EXPECT_EQ(off.regions, 0U);
    EXPECT_EQ(runtime.FindRegionMeshUVE(region), nullptr)
        << "a region switched off is not a region an agent may walk on";
    EXPECT_EQ(off.agentsWithoutMesh, 1U);

    const NavSeeker3DComponentUVE& seeker = entityManager.GetComponentUVE<NavSeeker3DComponentUVE>(agent);
    EXPECT_EQ(seeker.pathStatus, Scene::NavigationAgentPathStatusUVE::Failed);
    EXPECT_EQ(seeker.desiredVelocity, Math::Vector3UVE{})
        << "the agent stops rather than keeping the velocity it had when the ground went away";
}

TEST_F(NavigationRuntimeUVETest, SyncUVE_AnAgentWithNoRegionUnderItFailsInsteadOfWalkingOffTheMesh) {
    MakeFloorUVE();
    MakeRegionUVE(Math::Vector3UVE{0.0F, 0.5F, 0.0F}, 3.0F);
    const Scene::EntityUVE stranded = MakeAgentUVE(Math::Vector3UVE{40.0F, 0.0F, 40.0F},
                                                   Math::Vector3UVE{45.0F, 0.0F, 40.0F});

    const NavigationSyncReportUVE report = StepUVE({stranded});
    EXPECT_EQ(report.bakes, 1U) << "the region still bakes; it is the agent that is nowhere near it";
    EXPECT_EQ(report.agents, 1U);
    EXPECT_EQ(report.agentsWithoutMesh, 1U);

    const NavSeeker3DComponentUVE& seeker = entityManager.GetComponentUVE<NavSeeker3DComponentUVE>(stranded);
    EXPECT_EQ(seeker.pathStatus, Scene::NavigationAgentPathStatusUVE::Failed);
    EXPECT_EQ(seeker.desiredVelocity, Math::Vector3UVE{});
    EXPECT_FALSE(seeker.pathChanged) << "nothing was found, so nothing changed";
}

TEST_F(NavigationRuntimeUVETest, SyncUVE_LeavesAnAgentWithoutATransformExactlyAsAuthored) {
    MakeFloorUVE();
    MakeRegionUVE();
    // A seeker component on an entity with no pose at all: the runtime never adds the transform it
    // would need, so the entity - and the values an author or a script put there - stay untouched.
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    NavSeeker3DComponentUVE seeker;
    seeker.desiredVelocity = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    seeker.pathStatus = Scene::NavigationAgentPathStatusUVE::Finished;
    entityManager.AddComponentUVE<NavSeeker3DComponentUVE>(entity, seeker);

    const NavigationSyncReportUVE report = StepUVE({entity});
    EXPECT_EQ(report.agents, 0U);
    const NavSeeker3DComponentUVE& after = entityManager.GetComponentUVE<NavSeeker3DComponentUVE>(entity);
    EXPECT_EQ(after.desiredVelocity, (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(after.pathStatus, Scene::NavigationAgentPathStatusUVE::Finished);
}

TEST_F(NavigationRuntimeUVETest, SyncUVE_SwitchedOffAgentsPublishNothingAndForgetTheirRoute) {
    MakeFloorUVE();
    MakeRegionUVE();
    const Scene::EntityUVE agent = MakeAgentUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F},
                                                Math::Vector3UVE{5.0F, 0.0F, 0.0F});
    StepUVE({agent});
    ASSERT_EQ(runtime.GetAgentCountUVE(), 1U);

    entityManager.GetComponentUVE<NavSeeker3DComponentUVE>(agent).enabled = false;
    StepUVE({agent});

    const NavSeeker3DComponentUVE& seeker = entityManager.GetComponentUVE<NavSeeker3DComponentUVE>(agent);
    EXPECT_EQ(seeker.pathStatus, Scene::NavigationAgentPathStatusUVE::Idle);
    EXPECT_EQ(seeker.desiredVelocity, Math::Vector3UVE{});
    EXPECT_EQ(seeker.nextPathPosition, entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(agent).worldPosition);
    EXPECT_FALSE(seeker.targetReached);
    EXPECT_EQ(runtime.GetAgentCountUVE(), 0U)
        << "the steering state is dropped, so enabling it again starts from a clean search";
}

TEST_F(NavigationRuntimeUVETest, SyncUVE_AvoidancePartsAgentsThatWouldOtherwiseShareAPath) {
    MakeFloorUVE();
    MakeRegionUVE();
    // Two agents one metre apart in Z, both walking along X. The one behind the other's shoulder has
    // somewhere to step aside to; with avoidance off it walks straight and the two overlap.
    const Scene::EntityUVE steppingAside = MakeAgentUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F},
                                                        Math::Vector3UVE{5.0F, 0.0F, 0.0F});
    const Scene::EntityUVE neighbour = MakeAgentUVE(Math::Vector3UVE{0.0F, 0.0F, -1.2F},
                                                   Math::Vector3UVE{0.0F, 0.0F, -5.0F});
    // The velocity ramp is the steering suite's own subject; this case is about the push, so these
    // agents publish the velocity they want rather than one step of reaching it.
    MakeImmediateUVE(steppingAside);
    MakeImmediateUVE(neighbour);
    StepUVE({steppingAside, neighbour});

    const NavSeeker3DComponentUVE& avoided = entityManager.GetComponentUVE<NavSeeker3DComponentUVE>(steppingAside);
    EXPECT_GT(avoided.desiredVelocity.z, 0.0F)
        << "the push is away from the neighbour, which is on the -Z side";
    EXPECT_LT(avoided.desiredVelocity.x, 4.0F) << "and it costs forward speed rather than adding any";
    const float avoidedSpeedSquared = avoided.desiredVelocity.x * avoided.desiredVelocity.x +
                                      avoided.desiredVelocity.z * avoided.desiredVelocity.z;
    EXPECT_LE(avoidedSpeedSquared, 4.0F * 4.0F + kEpsilon)
        << "a neighbour is something to walk around, not a reason to exceed the agent's own top speed";

    // The same layout with avoidance switched off: the agent walks straight at its target.
    const Scene::EntityUVE straight = MakeAgentUVE(Math::Vector3UVE{5.0F, 0.0F, 6.0F},
                                                   Math::Vector3UVE{9.0F, 0.0F, 6.0F}, /*avoidanceEnabled=*/false);
    const Scene::EntityUVE ignoredNeighbour = MakeAgentUVE(Math::Vector3UVE{5.0F, 0.0F, 4.8F},
                                                           Math::Vector3UVE{5.0F, 0.0F, 0.0F});
    MakeImmediateUVE(straight);
    MakeImmediateUVE(ignoredNeighbour);
    StepUVE({straight, ignoredNeighbour});
    const NavSeeker3DComponentUVE& unaware = entityManager.GetComponentUVE<NavSeeker3DComponentUVE>(straight);
    EXPECT_NEAR(unaware.desiredVelocity.z, 0.0F, kEpsilon);
    EXPECT_NEAR(unaware.desiredVelocity.x, 4.0F, kEpsilon);
}

TEST_F(NavigationRuntimeUVETest, FindRegionForPositionUVE_PrefersTheVolumeItIsInsideOverTheNearerEdge) {
    MakeFloorUVE(24.0F);
    const Scene::EntityUVE west = MakeRegionUVE(Math::Vector3UVE{0.0F, 0.5F, 0.0F}, 4.0F);
    const Scene::EntityUVE east = MakeRegionUVE(Math::Vector3UVE{20.0F, 0.5F, 0.0F}, 4.0F);
    StepUVE({});

    EXPECT_EQ(runtime.FindRegionForPositionUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 2.0F), west);
    EXPECT_EQ(runtime.FindRegionForPositionUVE(Math::Vector3UVE{20.0F, 0.0F, 0.0F}, 2.0F), east);
    // Beside the east volume, inside the tolerance: the nearest edge wins.
    EXPECT_EQ(runtime.FindRegionForPositionUVE(Math::Vector3UVE{25.0F, 0.0F, 0.0F}, 2.0F), east);
    // Past the tolerance there is nothing to navigate on, and saying so beats returning a stale edge.
    EXPECT_EQ(runtime.FindRegionForPositionUVE(Math::Vector3UVE{40.0F, 0.0F, 0.0F}, 2.0F), Scene::kInvalidEntityUVE);
}

TEST_F(NavigationRuntimeUVETest, ClearUVE_ForgetsEveryMeshAndAgent) {
    MakeFloorUVE();
    const Scene::EntityUVE region = MakeRegionUVE();
    const Scene::EntityUVE agent = MakeAgentUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F},
                                                Math::Vector3UVE{5.0F, 0.0F, 0.0F});
    StepUVE({agent});
    ASSERT_EQ(runtime.GetRegionCountUVE(), 1U);
    ASSERT_EQ(runtime.GetAgentCountUVE(), 1U);

    runtime.ClearUVE();

    EXPECT_EQ(runtime.GetRegionCountUVE(), 0U);
    EXPECT_EQ(runtime.GetAgentCountUVE(), 0U);
    EXPECT_EQ(runtime.FindRegionMeshUVE(region), nullptr);
    // The next step rebuilds from nothing, since it has nothing left to serve.
    EXPECT_EQ(StepUVE({agent}).bakes, 1U);
}

} // namespace
} // namespace UVE::Navigation::Tests
