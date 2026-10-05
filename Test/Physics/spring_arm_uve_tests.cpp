// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Tests for the SpringArm3D step: the third-person camera boom, against the engine's real raycast
// system and the real scene graph.
//
// The arm's two pure halves - where it aims (`ResolveSpringArm3DTargetUVE`) and how it gets there
// (`ResolveSpringArm3DLengthUVE`) - are pinned in the object-definition suite, where those functions
// live. What is tested here is the step that ties them to the world: the cast along the arm's own
// axis, the mask, the arm never hitting itself, the children riding the change in length, and the
// two rules that make a camera boom worth having at all - shortening snaps so nothing clips, and an
// arm switched off hands its length back instead of stranding the camera where it stopped.
//
// The child here is a camera in the real thing; in these tests it is an entity with a pose, because
// the arm never reads what a child is - only that it has somewhere to sit.

#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "uve/component/collider_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/spring_arm_3d_uve.h"
#include "uve/physics/raycast_system_uve.h"
#include "uve/physics/spring_arm_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::SpringArm3DComponentUVE;

class SpringArm3DUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    RaycastSystemUVE raycastSystem;

    static constexpr float kDeltaTimeUVE = 1.0F / 60.0F;
    static constexpr float kToleranceUVE = 1.0e-4F;

    /// A SpringArm3D through its own recipe, placed and aimed.
    [[nodiscard]] Scene::EntityUVE MakeArmUVE(
        const Math::Vector3UVE position, const SpringArm3DComponentUVE& springArm = {},
        const Math::QuaternionUVE& rotation = {}) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::SpringArm3DObjectDefinitionUVE definition;
        definition.springArm = springArm;
        Scene::ApplySpringArm3DObjectDefinitionUVE(entityManager, entity, definition);
        Scene::TransformComponentUVE local;
        local.localPosition = position;
        local.localRotation = rotation;
        sceneGraph.SetLocalTransformUVE(entityManager, entity, local);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    /// A wall: a collider with a pose and nothing else. What the boom is supposed to find.
    [[nodiscard]] Scene::EntityUVE MakeObstacleUVE(
        const Math::Vector3UVE position, const Math::Vector3UVE halfExtents = {0.5F, 0.5F, 0.5F},
        const std::uint32_t layer = 1U) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        Scene::ColliderComponentUVE collider{halfExtents};
        collider.collisionLayer = layer;
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(entity, collider);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    /// A child of the arm - the camera the boom carries.
    [[nodiscard]] Scene::EntityUVE AttachChildUVE(const Scene::EntityUVE arm,
                                                  const Math::Vector3UVE localPosition) {
        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = localPosition;
        sceneGraph.AttachTransformUVE(entityManager, child, local);
        sceneGraph.SetParentUVE(entityManager, child, arm);
        sceneGraph.UpdateUVE(entityManager);
        return child;
    }

    [[nodiscard]] SpringArm3DStepResultUVE StepUVE(const Scene::EntityUVE entity,
                                                   const float deltaTimeSeconds = kDeltaTimeUVE) {
        return StepSpringArm3DUVE(entityManager, sceneGraph, raycastSystem, entity, deltaTimeSeconds);
    }

    [[nodiscard]] SpringArm3DComponentUVE ArmUVE(const Scene::EntityUVE entity) {
        return entityManager.GetComponentUVE<SpringArm3DComponentUVE>(entity);
    }

    [[nodiscard]] Math::Vector3UVE ChildPositionUVE(const Scene::EntityUVE child) {
        return entityManager.GetComponentUVE<Scene::TransformComponentUVE>(child).localPosition;
    }

    /// Steps until the arm settles on its target, for the tests that are about where the arm ends
    /// up rather than about one particular step.
    [[nodiscard]] float SettleUVE(const Scene::EntityUVE arm, const int maximumSteps = 600) {
        float length = ArmUVE(arm).currentLength;
        for (int step = 0; step < maximumSteps; ++step) {
            const float next = StepUVE(arm).currentLength;
            if (next == length) {
                return next;
            }
            length = next;
        }
        return length;
    }
};

// =================================================================================================
// The cast.
// =================================================================================================

TEST_F(SpringArm3DUVETest, AnArmInOpenSpaceRunsItsCastAndKeepsItsFullReach) {
    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F});
    const SpringArm3DStepResultUVE report = StepUVE(arm);

    ASSERT_TRUE(report.IsSteppedUVE());
    EXPECT_TRUE(report.castRan);
    EXPECT_FALSE(report.hasHit);
    EXPECT_FLOAT_EQ(report.armLength, 4.0F);
    EXPECT_FLOAT_EQ(report.targetLength, 4.0F);
    EXPECT_FLOAT_EQ(report.currentLength, 4.0F);
    // Nothing to hand back: an unobstructed arm leaves its children exactly where they were.
    EXPECT_FLOAT_EQ(report.lengthDelta, 0.0F);
    EXPECT_FALSE(report.MovedChildrenUVE());
}

TEST_F(SpringArm3DUVETest, AnArmShortensBehindAWallByItsMargin) {
    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F});
    const Scene::EntityUVE camera = AttachChildUVE(arm, {0.35F, 1.25F, 4.0F});
    // The wall's near face is at z = 1.5, so the margin takes the arm to 1.4.
    const Scene::EntityUVE wall = MakeObstacleUVE({0.0F, 0.0F, 2.0F});

    const SpringArm3DStepResultUVE report = StepUVE(arm);

    ASSERT_TRUE(report.IsSteppedUVE());
    EXPECT_TRUE(report.hasHit);
    EXPECT_EQ(report.hitEntity, wall);
    EXPECT_NEAR(report.hitDistance, 1.5F, kToleranceUVE);
    EXPECT_NEAR(report.hitPoint.z, 1.5F, kToleranceUVE);
    EXPECT_NEAR(report.targetLength, 1.4F, kToleranceUVE);
    // Retraction snaps: the arm is already at the wall in the step the wall is found, not gliding
    // into it over the next few frames.
    EXPECT_NEAR(report.currentLength, 1.4F, kToleranceUVE);
    EXPECT_NEAR(report.lengthDelta, -2.6F, kToleranceUVE);
    EXPECT_TRUE(report.ShortenedUVE());

    // The child rode the delta along the arm's own Z, and nothing else about it changed.
    const Math::Vector3UVE cameraPosition = ChildPositionUVE(camera);
    EXPECT_NEAR(cameraPosition.z, 1.4F, kToleranceUVE);
    EXPECT_FLOAT_EQ(cameraPosition.x, 0.35F);
    EXPECT_FLOAT_EQ(cameraPosition.y, 1.25F);
    EXPECT_EQ(report.movedChildCount, 1U);
}

TEST_F(SpringArm3DUVETest, AnArmNeverRetractsPastZeroEvenWithTheWallOnThePivot) {
    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F});
    const Scene::EntityUVE camera = AttachChildUVE(arm, {0.0F, 0.0F, 4.0F});
    // A face five centimetres in front of the pivot, well inside the margin.
    static_cast<void>(MakeObstacleUVE({0.0F, 0.0F, 0.55F}));

    const SpringArm3DStepResultUVE report = StepUVE(arm);

    ASSERT_TRUE(report.IsSteppedUVE());
    EXPECT_FLOAT_EQ(report.currentLength, 0.0F);
    EXPECT_NEAR(ChildPositionUVE(camera).z, 0.0F, kToleranceUVE);
}

TEST_F(SpringArm3DUVETest, TheArmOnlyCollidesWithWhatItsMaskLooksFor) {
    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F},
                                            SpringArm3DComponentUVE{4.0F, 0.1F, 8.0F, 0x1U, 4.0F, true});
    static_cast<void>(MakeObstacleUVE({0.0F, 0.0F, 2.0F}, {0.5F, 0.5F, 0.5F}, /*layer=*/2U));

    const SpringArm3DStepResultUVE through = StepUVE(arm);
    ASSERT_TRUE(through.IsSteppedUVE());
    EXPECT_TRUE(through.castRan);
    EXPECT_FALSE(through.hasHit) << "the wall is on a layer the arm does not look for";
    EXPECT_FLOAT_EQ(through.currentLength, 4.0F);

    // The same wall on a layer the arm does look for blocks it: the mask is the whole difference.
    const Scene::EntityUVE near = MakeObstacleUVE({0.0F, 0.0F, 3.0F}, {0.5F, 0.5F, 0.5F}, /*layer=*/1U);
    const SpringArm3DStepResultUVE blocked = StepUVE(arm);
    ASSERT_TRUE(blocked.hasHit);
    EXPECT_EQ(blocked.hitEntity, near);
    EXPECT_NEAR(blocked.currentLength, 2.4F, kToleranceUVE);
}

TEST_F(SpringArm3DUVETest, AnArmNeverCastsIntoItself) {
    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F});
    // A collider on the arm's own entity, right at the pivot: without the ignore it would be hit at
    // distance zero and the camera would sit on the boom's own origin.
    Scene::ColliderComponentUVE ownCollider{Math::Vector3UVE{0.5F, 0.5F, 0.5F}};
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(arm, ownCollider);

    const SpringArm3DStepResultUVE report = StepUVE(arm);

    ASSERT_TRUE(report.IsSteppedUVE());
    EXPECT_TRUE(report.castRan);
    EXPECT_FALSE(report.hasHit);
    EXPECT_FLOAT_EQ(report.currentLength, 4.0F);
}

TEST_F(SpringArm3DUVETest, TheArmCastsAlongItsOwnAxisNotTheWorldOne) {
    // A quarter turn about +Y aims the boom's local +Z along world +X. Both the convention and the
    // arm's use of it are pinned here: a step that cast along world +Z regardless of rotation would
    // find the wrong wall.
    Math::QuaternionUVE rotation{};
    ASSERT_TRUE(Math::TryMakeEulerUVE(Math::Vector3UVE{0.0F, 1.5707963F, 0.0F}, rotation));
    const Math::Vector3UVE armAxis = Math::RotateVectorUVE(rotation, Math::Vector3UVE{0.0F, 0.0F, 1.0F});
    EXPECT_NEAR(armAxis.x, 1.0F, 1.0e-4F);
    EXPECT_NEAR(armAxis.z, 0.0F, 1.0e-4F);

    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F}, {}, rotation);
    const Scene::EntityUVE behindInWorldTerms = MakeObstacleUVE({0.0F, 0.0F, 2.0F});
    static_cast<void>(behindInWorldTerms);
    const Scene::EntityUVE whereTheArmLooks = MakeObstacleUVE(armAxis * 2.0F);

    const SpringArm3DStepResultUVE report = StepUVE(arm);

    ASSERT_TRUE(report.IsSteppedUVE());
    ASSERT_TRUE(report.hasHit);
    EXPECT_EQ(report.hitEntity, whereTheArmLooks);
    EXPECT_NEAR(report.currentLength, 1.4F, kToleranceUVE);
}

// =================================================================================================
// The switch.
// =================================================================================================

TEST_F(SpringArm3DUVETest, AnArmThatIsSwitchedOffStopsCastingAndHandsItsLengthBack) {
    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F});
    const Scene::EntityUVE camera = AttachChildUVE(arm, {0.0F, 0.0F, 4.0F});
    const Scene::EntityUVE wall = MakeObstacleUVE({0.0F, 0.0F, 2.0F});
    ASSERT_NEAR(StepUVE(arm).currentLength, 1.4F, kToleranceUVE);
    ASSERT_NEAR(ChildPositionUVE(camera).z, 1.4F, kToleranceUVE);

    entityManager.GetComponentUVE<SpringArm3DComponentUVE>(arm).enabled = false;
    const SpringArm3DStepResultUVE first = StepUVE(arm);

    ASSERT_TRUE(first.IsDisabledUVE());
    EXPECT_FALSE(first.castRan) << "an arm that is off does not cast";
    EXPECT_GT(first.currentLength, 1.4F) << "and it starts handing its length back";

    // It keeps going home, monotonically, without ever passing its authored reach.
    float previous = first.currentLength;
    for (int step = 0; step < 300; ++step) {
        const float length = StepUVE(arm).currentLength;
        EXPECT_GE(length, previous) << "step " << step;
        EXPECT_LE(length, 4.0F) << "step " << step;
        previous = length;
    }
    EXPECT_FLOAT_EQ(ArmUVE(arm).currentLength, 4.0F);
    EXPECT_NEAR(ChildPositionUVE(camera).z, 4.0F, 1.0e-5F);

    // And with the wall still standing right there, a switched-off arm stays home.
    static_cast<void>(wall);
    EXPECT_FLOAT_EQ(StepUVE(arm).currentLength, 4.0F);
}

TEST_F(SpringArm3DUVETest, ASwitchedOffArmComesHomeInOneStepWhenNothingSmoothsIt) {
    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F},
                                            SpringArm3DComponentUVE{4.0F, 0.1F, 0.0F, 0xFFFFFFFFU, 4.0F, true});
    const Scene::EntityUVE camera = AttachChildUVE(arm, {0.0F, 0.0F, 4.0F});
    static_cast<void>(MakeObstacleUVE({0.0F, 0.0F, 2.0F}));
    ASSERT_NEAR(StepUVE(arm).currentLength, 1.4F, kToleranceUVE);

    entityManager.GetComponentUVE<SpringArm3DComponentUVE>(arm).enabled = false;
    const SpringArm3DStepResultUVE report = StepUVE(arm);

    EXPECT_TRUE(report.IsDisabledUVE());
    EXPECT_FLOAT_EQ(report.currentLength, 4.0F);
    EXPECT_FLOAT_EQ(ChildPositionUVE(camera).z, 4.0F);
}

// =================================================================================================
// The children.
// =================================================================================================

TEST_F(SpringArm3DUVETest, TheChildRidesTheArmAndComesHomeExactly) {
    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F});
    const Scene::EntityUVE camera = AttachChildUVE(arm, {0.35F, 1.25F, 4.0F});
    const Scene::EntityUVE wall = MakeObstacleUVE({0.0F, 0.0F, 2.0F});

    // Pressed against the wall, then the doorway is gone.
    for (int step = 0; step < 5; ++step) {
        static_cast<void>(StepUVE(arm));
    }
    ASSERT_NEAR(ArmUVE(arm).currentLength, 1.4F, kToleranceUVE);
    entityManager.DestroyEntityUVE(wall);

    EXPECT_FLOAT_EQ(SettleUVE(arm), 4.0F);
    // The whole point of riding the delta instead of rewriting a position: after an obstruct-then-
    // clear cycle the child is back at the pose the author gave it, to within the float association
    // error of a few hundred accumulated deltas - and the arm itself is exact.
    EXPECT_NEAR(ChildPositionUVE(camera).z, 4.0F, 1.0e-5F);
    EXPECT_FLOAT_EQ(ChildPositionUVE(camera).x, 0.35F);
    EXPECT_FLOAT_EQ(ChildPositionUVE(camera).y, 1.25F);
}

TEST_F(SpringArm3DUVETest, EveryDirectChildRidesTheArm) {
    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F});
    const Scene::EntityUVE camera = AttachChildUVE(arm, {0.0F, 0.0F, 4.0F});
    const Scene::EntityUVE lamp = AttachChildUVE(arm, {1.0F, 0.0F, 3.0F});
    static_cast<void>(MakeObstacleUVE({0.0F, 0.0F, 2.0F}));

    const SpringArm3DStepResultUVE report = StepUVE(arm);

    ASSERT_TRUE(report.IsSteppedUVE());
    EXPECT_EQ(report.movedChildCount, 2U);
    EXPECT_NEAR(ChildPositionUVE(camera).z, 1.4F, kToleranceUVE);
    EXPECT_NEAR(ChildPositionUVE(lamp).z, 0.4F, kToleranceUVE);
    EXPECT_FLOAT_EQ(ChildPositionUVE(lamp).x, 1.0F);
}

TEST_F(SpringArm3DUVETest, AChildWithNoPoseOfItsOwnIsSkippedRatherThanRefused) {
    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F});
    const Scene::EntityUVE camera = AttachChildUVE(arm, {0.0F, 0.0F, 4.0F});
    // A pure object in the hierarchy: it belongs to the arm but has no transform of its own, so
    // there is nothing about it to move.
    const Scene::EntityUVE bare = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Scene::HierarchyComponentUVE>(bare, Scene::HierarchyComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, bare, arm);
    sceneGraph.UpdateUVE(entityManager);
    static_cast<void>(MakeObstacleUVE({0.0F, 0.0F, 2.0F}));

    const SpringArm3DStepResultUVE report = StepUVE(arm);

    ASSERT_TRUE(report.IsSteppedUVE());
    EXPECT_EQ(report.movedChildCount, 1U);
    EXPECT_NEAR(report.currentLength, 1.4F, kToleranceUVE);
    EXPECT_NEAR(ChildPositionUVE(camera).z, 1.4F, kToleranceUVE);
}

TEST_F(SpringArm3DUVETest, AnArmWithNoChildrenStillResolvesItsLength) {
    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeObstacleUVE({0.0F, 0.0F, 2.0F}));

    const SpringArm3DStepResultUVE report = StepUVE(arm);

    ASSERT_TRUE(report.IsSteppedUVE());
    EXPECT_NEAR(report.currentLength, 1.4F, kToleranceUVE);
    EXPECT_EQ(report.movedChildCount, 0U);
    EXPECT_FALSE(report.MovedChildrenUVE());
}

// =================================================================================================
// The motion law as the world drives it, and the refusals.
// =================================================================================================

TEST_F(SpringArm3DUVETest, RetractionSnapsInOneStepAndExtensionSpringsBackOverTime) {
    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F});
    const Scene::EntityUVE wall = MakeObstacleUVE({0.0F, 0.0F, 2.0F});

    // One step, the whole way in: a camera is never given the chance to clip.
    const SpringArm3DStepResultUVE retracted = StepUVE(arm);
    EXPECT_NEAR(retracted.currentLength, 1.4F, kToleranceUVE);

    entityManager.DestroyEntityUVE(wall);
    const SpringArm3DStepResultUVE clearing = StepUVE(arm);
    ASSERT_TRUE(clearing.LengthenedUVE());
    EXPECT_LT(clearing.currentLength, 4.0F) << "and it springs back rather than popping";

    float previous = clearing.currentLength;
    for (int step = 0; step < 60; ++step) {
        const float length = StepUVE(arm).currentLength;
        EXPECT_GE(length, previous) << "step " << step;
        EXPECT_LE(length, 4.0F) << "step " << step;
        previous = length;
    }
    EXPECT_FLOAT_EQ(ArmUVE(arm).currentLength, 4.0F);
}

TEST_F(SpringArm3DUVETest, TheSpringBackIsAFunctionOfElapsedTimeNotOfFrameRate) {
    // The extension law is exponential in elapsed seconds, so two half-steps and one whole step
    // leave the arm in the same place. A per-step fraction would make a camera spring back twice as
    // fast on a 120 Hz machine.
    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F});
    const Scene::EntityUVE wall = MakeObstacleUVE({0.0F, 0.0F, 2.0F});
    static_cast<void>(StepUVE(arm));
    entityManager.DestroyEntityUVE(wall);
    ASSERT_NEAR(ArmUVE(arm).currentLength, 1.4F, kToleranceUVE);

    static_cast<void>(StepUVE(arm, kDeltaTimeUVE / 2.0F));
    static_cast<void>(StepUVE(arm, kDeltaTimeUVE / 2.0F));
    const float twoHalves = ArmUVE(arm).currentLength;

    entityManager.GetComponentUVE<SpringArm3DComponentUVE>(arm).currentLength = 1.4F;
    static_cast<void>(StepUVE(arm, kDeltaTimeUVE));
    const float oneWhole = ArmUVE(arm).currentLength;

    EXPECT_NEAR(twoHalves, oneWhole, 1.0e-4F);
    EXPECT_GT(twoHalves, 1.4F);
}

TEST_F(SpringArm3DUVETest, AnArmPressedAgainstAWallDoesNotMoveItsChildrenAgain) {
    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F});
    const Scene::EntityUVE camera = AttachChildUVE(arm, {0.0F, 0.0F, 4.0F});
    static_cast<void>(MakeObstacleUVE({0.0F, 0.0F, 2.0F}));
    ASSERT_TRUE(StepUVE(arm).hasHit);
    const float cameraZ = ChildPositionUVE(camera).z;

    for (int step = 0; step < 30; ++step) {
        const SpringArm3DStepResultUVE report = StepUVE(arm);
        EXPECT_FLOAT_EQ(report.lengthDelta, 0.0F) << "step " << step;
        EXPECT_EQ(report.movedChildCount, 0U) << "step " << step;
    }
    EXPECT_FLOAT_EQ(ChildPositionUVE(camera).z, cameraZ);
}

TEST_F(SpringArm3DUVETest, TheArmRefusesWhatItCannotEvaluate) {
    // No such entity.
    EXPECT_EQ(StepUVE(Scene::EntityUVE{}).code, SpringArm3DStepCodeUVE::UnknownEntity);

    // A wall is not a spring arm.
    const Scene::EntityUVE obstacle = MakeObstacleUVE({1.0F, 2.0F, 3.0F});
    EXPECT_EQ(StepUVE(obstacle).code, SpringArm3DStepCodeUVE::NotASpringArm);

    // A spring arm outside the scene graph has no world pose to cast from.
    const Scene::EntityUVE homeless = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<SpringArm3DComponentUVE>(homeless, SpringArm3DComponentUVE{});
    EXPECT_EQ(StepUVE(homeless).code, SpringArm3DStepCodeUVE::MissingTransform);

    // Malformed authored values refuse rather than latch a camera somewhere, and nothing moves.
    const Scene::EntityUVE arm = MakeArmUVE({1.0F, 2.0F, 3.0F});
    const Scene::EntityUVE camera = AttachChildUVE(arm, {0.0F, 0.0F, 4.0F});
    static_cast<void>(MakeObstacleUVE({1.0F, 2.0F, 5.0F}));

    entityManager.GetComponentUVE<SpringArm3DComponentUVE>(arm).armLength = 0.0F;
    EXPECT_EQ(StepUVE(arm).code, SpringArm3DStepCodeUVE::InvalidComponent);
    entityManager.GetComponentUVE<SpringArm3DComponentUVE>(arm).armLength = 4.0F;

    // A runtime length past the authored reach (a script, a stale scene file) is the same refusal.
    entityManager.GetComponentUVE<SpringArm3DComponentUVE>(arm).currentLength = 5.0F;
    EXPECT_EQ(StepUVE(arm).code, SpringArm3DStepCodeUVE::InvalidComponent);
    EXPECT_FLOAT_EQ(ArmUVE(arm).currentLength, 5.0F) << "a refused arm is not quietly rewritten";
    entityManager.GetComponentUVE<SpringArm3DComponentUVE>(arm).currentLength = 4.0F;

    // A step duration the arm cannot honour.
    EXPECT_EQ(StepUVE(arm, 0.0F).code, SpringArm3DStepCodeUVE::InvalidDeltaTime);
    EXPECT_EQ(StepUVE(arm, -1.0F).code, SpringArm3DStepCodeUVE::InvalidDeltaTime);
    EXPECT_EQ(StepUVE(arm, std::numeric_limits<float>::quiet_NaN()).code,
              SpringArm3DStepCodeUVE::InvalidDeltaTime);

    EXPECT_FLOAT_EQ(ArmUVE(arm).currentLength, 4.0F);
    EXPECT_FLOAT_EQ(ChildPositionUVE(camera).z, 4.0F);
}

TEST_F(SpringArm3DUVETest, TheReportSaysWhatTheCastFoundAndWhatItCost) {
    const Scene::EntityUVE arm = MakeArmUVE({0.0F, 0.0F, 0.0F});
    const Scene::EntityUVE wall = MakeObstacleUVE({0.0F, 0.0F, 2.0F});

    const SpringArm3DStepResultUVE report = StepUVE(arm);

    ASSERT_TRUE(report.IsSteppedUVE());
    EXPECT_TRUE(report.castRan);
    EXPECT_TRUE(report.hasHit);
    EXPECT_EQ(report.hitEntity, wall);
    EXPECT_NEAR(report.hitDistance, 1.5F, kToleranceUVE);
    EXPECT_NEAR(report.hitPoint.z, 1.5F, kToleranceUVE);
    EXPECT_FLOAT_EQ(report.armLength, 4.0F);
    EXPECT_FLOAT_EQ(report.previousLength, 4.0F);
    EXPECT_NEAR(report.targetLength, 1.4F, kToleranceUVE);
    EXPECT_NEAR(report.currentLength, 1.4F, kToleranceUVE);
    EXPECT_TRUE(report.ShortenedUVE());
    EXPECT_FALSE(report.LengthenedUVE());
    EXPECT_FALSE(report.IsDisabledUVE());
}

} // namespace
} // namespace UVE::Physics::Tests
