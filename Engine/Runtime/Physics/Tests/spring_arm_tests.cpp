// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Tests for the SpringArm3D step: the third-person camera boom, against the engine's real raycast
// system and the real scene graph.
//
// The arm's two pure halves - where it aims (`ResolveSpringArm3DTarget`) and how it gets there
// (`ResolveSpringArm3DLength`) - are pinned in the object-definition suite, where those functions
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

#include "collider_component.h"
#include "hierarchy_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "quaternion.h"
#include "vector3.h"
#include "memory_manager.h"
#include "spring_arm_3d.h"
#include "raycast_system.h"
#include "spring_arm.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::SpringArm3DComponent;

class SpringArm3DTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    RaycastSystem raycastSystem;

    static constexpr float kDeltaTime = 1.0F / 60.0F;
    static constexpr float kTolerance = 1.0e-4F;

    /// A SpringArm3D through its own recipe, placed and aimed.
    [[nodiscard]] Scene::Entity MakeArm(
        const Math::Vector3 position, const SpringArm3DComponent& springArm = {},
        const Math::Quaternion& rotation = {}) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::SpringArm3DObjectDefinition definition;
        definition.springArm = springArm;
        Scene::ApplySpringArm3DObjectDefinition(entityManager, entity, definition);
        Scene::TransformComponent local;
        local.localPosition = position;
        local.localRotation = rotation;
        sceneGraph.SetLocalTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        return entity;
    }

    /// A wall: a collider with a pose and nothing else. What the boom is supposed to find.
    [[nodiscard]] Scene::Entity MakeObstacle(
        const Math::Vector3 position, const Math::Vector3 halfExtents = {0.5F, 0.5F, 0.5F},
        const std::uint32_t layer = 1U) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, local);
        Scene::ColliderComponent collider{halfExtents};
        collider.collisionLayer = layer;
        entityManager.AddComponent<Scene::ColliderComponent>(entity, collider);
        sceneGraph.Update(entityManager);
        return entity;
    }

    /// A child of the arm - the camera the boom carries.
    [[nodiscard]] Scene::Entity AttachChild(const Scene::Entity arm,
                                                  const Math::Vector3 localPosition) {
        const Scene::Entity child = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = localPosition;
        sceneGraph.AttachTransform(entityManager, child, local);
        sceneGraph.SetParent(entityManager, child, arm);
        sceneGraph.Update(entityManager);
        return child;
    }

    [[nodiscard]] SpringArm3DStepResult Step(const Scene::Entity entity,
                                                   const float deltaTimeSeconds = kDeltaTime) {
        return StepSpringArm3D(entityManager, sceneGraph, raycastSystem, entity, deltaTimeSeconds);
    }

    [[nodiscard]] SpringArm3DComponent Arm(const Scene::Entity entity) {
        return entityManager.GetComponent<SpringArm3DComponent>(entity);
    }

    [[nodiscard]] Math::Vector3 ChildPosition(const Scene::Entity child) {
        return entityManager.GetComponent<Scene::TransformComponent>(child).localPosition;
    }

    /// Steps until the arm settles on its target, for the tests that are about where the arm ends
    /// up rather than about one particular step.
    [[nodiscard]] float Settle(const Scene::Entity arm, const int maximumSteps = 600) {
        float length = Arm(arm).currentLength;
        for (int step = 0; step < maximumSteps; ++step) {
            const float next = Step(arm).currentLength;
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

TEST_F(SpringArm3DTest, AnArmInOpenSpaceRunsItsCastAndKeepsItsFullReach) {
    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F});
    const SpringArm3DStepResult report = Step(arm);

    ASSERT_TRUE(report.IsStepped());
    EXPECT_TRUE(report.castRan);
    EXPECT_FALSE(report.hasHit);
    EXPECT_FLOAT_EQ(report.armLength, 4.0F);
    EXPECT_FLOAT_EQ(report.targetLength, 4.0F);
    EXPECT_FLOAT_EQ(report.currentLength, 4.0F);
    // Nothing to hand back: an unobstructed arm leaves its children exactly where they were.
    EXPECT_FLOAT_EQ(report.lengthDelta, 0.0F);
    EXPECT_FALSE(report.MovedChildren());
}

TEST_F(SpringArm3DTest, AnArmShortensBehindAWallByItsMargin) {
    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F});
    const Scene::Entity camera = AttachChild(arm, {0.35F, 1.25F, 4.0F});
    // The wall's near face is at z = 1.5, so the margin takes the arm to 1.4.
    const Scene::Entity wall = MakeObstacle({0.0F, 0.0F, 2.0F});

    const SpringArm3DStepResult report = Step(arm);

    ASSERT_TRUE(report.IsStepped());
    EXPECT_TRUE(report.hasHit);
    EXPECT_EQ(report.hitEntity, wall);
    EXPECT_NEAR(report.hitDistance, 1.5F, kTolerance);
    EXPECT_NEAR(report.hitPoint.z, 1.5F, kTolerance);
    EXPECT_NEAR(report.targetLength, 1.4F, kTolerance);
    // Retraction snaps: the arm is already at the wall in the step the wall is found, not gliding
    // into it over the next few frames.
    EXPECT_NEAR(report.currentLength, 1.4F, kTolerance);
    EXPECT_NEAR(report.lengthDelta, -2.6F, kTolerance);
    EXPECT_TRUE(report.Shortened());

    // The child rode the delta along the arm's own Z, and nothing else about it changed.
    const Math::Vector3 cameraPosition = ChildPosition(camera);
    EXPECT_NEAR(cameraPosition.z, 1.4F, kTolerance);
    EXPECT_FLOAT_EQ(cameraPosition.x, 0.35F);
    EXPECT_FLOAT_EQ(cameraPosition.y, 1.25F);
    EXPECT_EQ(report.movedChildCount, 1U);
}

TEST_F(SpringArm3DTest, AnArmNeverRetractsPastZeroEvenWithTheWallOnThePivot) {
    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F});
    const Scene::Entity camera = AttachChild(arm, {0.0F, 0.0F, 4.0F});
    // A face five centimetres in front of the pivot, well inside the margin.
    static_cast<void>(MakeObstacle({0.0F, 0.0F, 0.55F}));

    const SpringArm3DStepResult report = Step(arm);

    ASSERT_TRUE(report.IsStepped());
    EXPECT_FLOAT_EQ(report.currentLength, 0.0F);
    EXPECT_NEAR(ChildPosition(camera).z, 0.0F, kTolerance);
}

TEST_F(SpringArm3DTest, TheArmOnlyCollidesWithWhatItsMaskLooksFor) {
    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F},
                                            SpringArm3DComponent{4.0F, 0.1F, 8.0F, 0x1U, 4.0F, true});
    static_cast<void>(MakeObstacle({0.0F, 0.0F, 2.0F}, {0.5F, 0.5F, 0.5F}, /*layer=*/2U));

    const SpringArm3DStepResult through = Step(arm);
    ASSERT_TRUE(through.IsStepped());
    EXPECT_TRUE(through.castRan);
    EXPECT_FALSE(through.hasHit) << "the wall is on a layer the arm does not look for";
    EXPECT_FLOAT_EQ(through.currentLength, 4.0F);

    // The same wall on a layer the arm does look for blocks it: the mask is the whole difference.
    const Scene::Entity near = MakeObstacle({0.0F, 0.0F, 3.0F}, {0.5F, 0.5F, 0.5F}, /*layer=*/1U);
    const SpringArm3DStepResult blocked = Step(arm);
    ASSERT_TRUE(blocked.hasHit);
    EXPECT_EQ(blocked.hitEntity, near);
    EXPECT_NEAR(blocked.currentLength, 2.4F, kTolerance);
}

TEST_F(SpringArm3DTest, AnArmNeverCastsIntoItself) {
    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F});
    // A collider on the arm's own entity, right at the pivot: without the ignore it would be hit at
    // distance zero and the camera would sit on the boom's own origin.
    Scene::ColliderComponent ownCollider{Math::Vector3{0.5F, 0.5F, 0.5F}};
    entityManager.AddComponent<Scene::ColliderComponent>(arm, ownCollider);

    const SpringArm3DStepResult report = Step(arm);

    ASSERT_TRUE(report.IsStepped());
    EXPECT_TRUE(report.castRan);
    EXPECT_FALSE(report.hasHit);
    EXPECT_FLOAT_EQ(report.currentLength, 4.0F);
}

TEST_F(SpringArm3DTest, TheArmCastsAlongItsOwnAxisNotTheWorldOne) {
    // A quarter turn about +Y aims the boom's local +Z along world +X. Both the convention and the
    // arm's use of it are pinned here: a step that cast along world +Z regardless of rotation would
    // find the wrong wall.
    Math::Quaternion rotation{};
    ASSERT_TRUE(Math::TryMakeEuler(Math::Vector3{0.0F, 1.5707963F, 0.0F}, rotation));
    const Math::Vector3 armAxis = Math::RotateVector(rotation, Math::Vector3{0.0F, 0.0F, 1.0F});
    EXPECT_NEAR(armAxis.x, 1.0F, 1.0e-4F);
    EXPECT_NEAR(armAxis.z, 0.0F, 1.0e-4F);

    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F}, {}, rotation);
    const Scene::Entity behindInWorldTerms = MakeObstacle({0.0F, 0.0F, 2.0F});
    static_cast<void>(behindInWorldTerms);
    const Scene::Entity whereTheArmLooks = MakeObstacle(armAxis * 2.0F);

    const SpringArm3DStepResult report = Step(arm);

    ASSERT_TRUE(report.IsStepped());
    ASSERT_TRUE(report.hasHit);
    EXPECT_EQ(report.hitEntity, whereTheArmLooks);
    EXPECT_NEAR(report.currentLength, 1.4F, kTolerance);
}

// =================================================================================================
// The switch.
// =================================================================================================

TEST_F(SpringArm3DTest, AnArmThatIsSwitchedOffStopsCastingAndHandsItsLengthBack) {
    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F});
    const Scene::Entity camera = AttachChild(arm, {0.0F, 0.0F, 4.0F});
    const Scene::Entity wall = MakeObstacle({0.0F, 0.0F, 2.0F});
    ASSERT_NEAR(Step(arm).currentLength, 1.4F, kTolerance);
    ASSERT_NEAR(ChildPosition(camera).z, 1.4F, kTolerance);

    entityManager.GetComponent<SpringArm3DComponent>(arm).enabled = false;
    const SpringArm3DStepResult first = Step(arm);

    ASSERT_TRUE(first.IsDisabled());
    EXPECT_FALSE(first.castRan) << "an arm that is off does not cast";
    EXPECT_GT(first.currentLength, 1.4F) << "and it starts handing its length back";

    // It keeps going home, monotonically, without ever passing its authored reach.
    float previous = first.currentLength;
    for (int step = 0; step < 300; ++step) {
        const float length = Step(arm).currentLength;
        EXPECT_GE(length, previous) << "step " << step;
        EXPECT_LE(length, 4.0F) << "step " << step;
        previous = length;
    }
    EXPECT_FLOAT_EQ(Arm(arm).currentLength, 4.0F);
    EXPECT_NEAR(ChildPosition(camera).z, 4.0F, 1.0e-5F);

    // And with the wall still standing right there, a switched-off arm stays home.
    static_cast<void>(wall);
    EXPECT_FLOAT_EQ(Step(arm).currentLength, 4.0F);
}

TEST_F(SpringArm3DTest, ASwitchedOffArmComesHomeInOneStepWhenNothingSmoothsIt) {
    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F},
                                            SpringArm3DComponent{4.0F, 0.1F, 0.0F, 0xFFFFFFFFU, 4.0F, true});
    const Scene::Entity camera = AttachChild(arm, {0.0F, 0.0F, 4.0F});
    static_cast<void>(MakeObstacle({0.0F, 0.0F, 2.0F}));
    ASSERT_NEAR(Step(arm).currentLength, 1.4F, kTolerance);

    entityManager.GetComponent<SpringArm3DComponent>(arm).enabled = false;
    const SpringArm3DStepResult report = Step(arm);

    EXPECT_TRUE(report.IsDisabled());
    EXPECT_FLOAT_EQ(report.currentLength, 4.0F);
    EXPECT_FLOAT_EQ(ChildPosition(camera).z, 4.0F);
}

// =================================================================================================
// The children.
// =================================================================================================

TEST_F(SpringArm3DTest, TheChildRidesTheArmAndComesHomeExactly) {
    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F});
    const Scene::Entity camera = AttachChild(arm, {0.35F, 1.25F, 4.0F});
    const Scene::Entity wall = MakeObstacle({0.0F, 0.0F, 2.0F});

    // Pressed against the wall, then the doorway is gone.
    for (int step = 0; step < 5; ++step) {
        static_cast<void>(Step(arm));
    }
    ASSERT_NEAR(Arm(arm).currentLength, 1.4F, kTolerance);
    entityManager.DestroyEntity(wall);

    EXPECT_FLOAT_EQ(Settle(arm), 4.0F);
    // The whole point of riding the delta instead of rewriting a position: after an obstruct-then-
    // clear cycle the child is back at the pose the author gave it, to within the float association
    // error of a few hundred accumulated deltas - and the arm itself is exact.
    EXPECT_NEAR(ChildPosition(camera).z, 4.0F, 1.0e-5F);
    EXPECT_FLOAT_EQ(ChildPosition(camera).x, 0.35F);
    EXPECT_FLOAT_EQ(ChildPosition(camera).y, 1.25F);
}

TEST_F(SpringArm3DTest, EveryDirectChildRidesTheArm) {
    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F});
    const Scene::Entity camera = AttachChild(arm, {0.0F, 0.0F, 4.0F});
    const Scene::Entity lamp = AttachChild(arm, {1.0F, 0.0F, 3.0F});
    static_cast<void>(MakeObstacle({0.0F, 0.0F, 2.0F}));

    const SpringArm3DStepResult report = Step(arm);

    ASSERT_TRUE(report.IsStepped());
    EXPECT_EQ(report.movedChildCount, 2U);
    EXPECT_NEAR(ChildPosition(camera).z, 1.4F, kTolerance);
    EXPECT_NEAR(ChildPosition(lamp).z, 0.4F, kTolerance);
    EXPECT_FLOAT_EQ(ChildPosition(lamp).x, 1.0F);
}

TEST_F(SpringArm3DTest, AChildWithNoPoseOfItsOwnIsSkippedRatherThanRefused) {
    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F});
    const Scene::Entity camera = AttachChild(arm, {0.0F, 0.0F, 4.0F});
    // A pure object in the hierarchy: it belongs to the arm but has no transform of its own, so
    // there is nothing about it to move.
    const Scene::Entity bare = entityManager.CreateEntity();
    entityManager.AddComponent<Scene::HierarchyComponent>(bare, Scene::HierarchyComponent{});
    sceneGraph.SetParent(entityManager, bare, arm);
    sceneGraph.Update(entityManager);
    static_cast<void>(MakeObstacle({0.0F, 0.0F, 2.0F}));

    const SpringArm3DStepResult report = Step(arm);

    ASSERT_TRUE(report.IsStepped());
    EXPECT_EQ(report.movedChildCount, 1U);
    EXPECT_NEAR(report.currentLength, 1.4F, kTolerance);
    EXPECT_NEAR(ChildPosition(camera).z, 1.4F, kTolerance);
}

TEST_F(SpringArm3DTest, AnArmWithNoChildrenStillResolvesItsLength) {
    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeObstacle({0.0F, 0.0F, 2.0F}));

    const SpringArm3DStepResult report = Step(arm);

    ASSERT_TRUE(report.IsStepped());
    EXPECT_NEAR(report.currentLength, 1.4F, kTolerance);
    EXPECT_EQ(report.movedChildCount, 0U);
    EXPECT_FALSE(report.MovedChildren());
}

// =================================================================================================
// The motion law as the world drives it, and the refusals.
// =================================================================================================

TEST_F(SpringArm3DTest, RetractionSnapsInOneStepAndExtensionSpringsBackOverTime) {
    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F});
    const Scene::Entity wall = MakeObstacle({0.0F, 0.0F, 2.0F});

    // One step, the whole way in: a camera is never given the chance to clip.
    const SpringArm3DStepResult retracted = Step(arm);
    EXPECT_NEAR(retracted.currentLength, 1.4F, kTolerance);

    entityManager.DestroyEntity(wall);
    const SpringArm3DStepResult clearing = Step(arm);
    ASSERT_TRUE(clearing.Lengthened());
    EXPECT_LT(clearing.currentLength, 4.0F) << "and it springs back rather than popping";

    float previous = clearing.currentLength;
    for (int step = 0; step < 60; ++step) {
        const float length = Step(arm).currentLength;
        EXPECT_GE(length, previous) << "step " << step;
        EXPECT_LE(length, 4.0F) << "step " << step;
        previous = length;
    }
    EXPECT_FLOAT_EQ(Arm(arm).currentLength, 4.0F);
}

TEST_F(SpringArm3DTest, TheSpringBackIsAFunctionOfElapsedTimeNotOfFrameRate) {
    // The extension law is exponential in elapsed seconds, so two half-steps and one whole step
    // leave the arm in the same place. A per-step fraction would make a camera spring back twice as
    // fast on a 120 Hz machine.
    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F});
    const Scene::Entity wall = MakeObstacle({0.0F, 0.0F, 2.0F});
    static_cast<void>(Step(arm));
    entityManager.DestroyEntity(wall);
    ASSERT_NEAR(Arm(arm).currentLength, 1.4F, kTolerance);

    static_cast<void>(Step(arm, kDeltaTime / 2.0F));
    static_cast<void>(Step(arm, kDeltaTime / 2.0F));
    const float twoHalves = Arm(arm).currentLength;

    entityManager.GetComponent<SpringArm3DComponent>(arm).currentLength = 1.4F;
    static_cast<void>(Step(arm, kDeltaTime));
    const float oneWhole = Arm(arm).currentLength;

    EXPECT_NEAR(twoHalves, oneWhole, 1.0e-4F);
    EXPECT_GT(twoHalves, 1.4F);
}

TEST_F(SpringArm3DTest, AnArmPressedAgainstAWallDoesNotMoveItsChildrenAgain) {
    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F});
    const Scene::Entity camera = AttachChild(arm, {0.0F, 0.0F, 4.0F});
    static_cast<void>(MakeObstacle({0.0F, 0.0F, 2.0F}));
    ASSERT_TRUE(Step(arm).hasHit);
    const float cameraZ = ChildPosition(camera).z;

    for (int step = 0; step < 30; ++step) {
        const SpringArm3DStepResult report = Step(arm);
        EXPECT_FLOAT_EQ(report.lengthDelta, 0.0F) << "step " << step;
        EXPECT_EQ(report.movedChildCount, 0U) << "step " << step;
    }
    EXPECT_FLOAT_EQ(ChildPosition(camera).z, cameraZ);
}

TEST_F(SpringArm3DTest, TheArmRefusesWhatItCannotEvaluate) {
    // No such entity.
    EXPECT_EQ(Step(Scene::Entity{}).code, SpringArm3DStepCode::UnknownEntity);

    // A wall is not a spring arm.
    const Scene::Entity obstacle = MakeObstacle({1.0F, 2.0F, 3.0F});
    EXPECT_EQ(Step(obstacle).code, SpringArm3DStepCode::NotASpringArm);

    // A spring arm outside the scene graph has no world pose to cast from.
    const Scene::Entity homeless = entityManager.CreateEntity();
    entityManager.AddComponent<SpringArm3DComponent>(homeless, SpringArm3DComponent{});
    EXPECT_EQ(Step(homeless).code, SpringArm3DStepCode::MissingTransform);

    // Malformed authored values refuse rather than latch a camera somewhere, and nothing moves.
    const Scene::Entity arm = MakeArm({1.0F, 2.0F, 3.0F});
    const Scene::Entity camera = AttachChild(arm, {0.0F, 0.0F, 4.0F});
    static_cast<void>(MakeObstacle({1.0F, 2.0F, 5.0F}));

    entityManager.GetComponent<SpringArm3DComponent>(arm).armLength = 0.0F;
    EXPECT_EQ(Step(arm).code, SpringArm3DStepCode::InvalidComponent);
    entityManager.GetComponent<SpringArm3DComponent>(arm).armLength = 4.0F;

    // A runtime length past the authored reach (a script, a stale scene file) is the same refusal.
    entityManager.GetComponent<SpringArm3DComponent>(arm).currentLength = 5.0F;
    EXPECT_EQ(Step(arm).code, SpringArm3DStepCode::InvalidComponent);
    EXPECT_FLOAT_EQ(Arm(arm).currentLength, 5.0F) << "a refused arm is not quietly rewritten";
    entityManager.GetComponent<SpringArm3DComponent>(arm).currentLength = 4.0F;

    // A step duration the arm cannot honour.
    EXPECT_EQ(Step(arm, 0.0F).code, SpringArm3DStepCode::InvalidDeltaTime);
    EXPECT_EQ(Step(arm, -1.0F).code, SpringArm3DStepCode::InvalidDeltaTime);
    EXPECT_EQ(Step(arm, std::numeric_limits<float>::quiet_NaN()).code,
              SpringArm3DStepCode::InvalidDeltaTime);

    EXPECT_FLOAT_EQ(Arm(arm).currentLength, 4.0F);
    EXPECT_FLOAT_EQ(ChildPosition(camera).z, 4.0F);
}

TEST_F(SpringArm3DTest, TheReportSaysWhatTheCastFoundAndWhatItCost) {
    const Scene::Entity arm = MakeArm({0.0F, 0.0F, 0.0F});
    const Scene::Entity wall = MakeObstacle({0.0F, 0.0F, 2.0F});

    const SpringArm3DStepResult report = Step(arm);

    ASSERT_TRUE(report.IsStepped());
    EXPECT_TRUE(report.castRan);
    EXPECT_TRUE(report.hasHit);
    EXPECT_EQ(report.hitEntity, wall);
    EXPECT_NEAR(report.hitDistance, 1.5F, kTolerance);
    EXPECT_NEAR(report.hitPoint.z, 1.5F, kTolerance);
    EXPECT_FLOAT_EQ(report.armLength, 4.0F);
    EXPECT_FLOAT_EQ(report.previousLength, 4.0F);
    EXPECT_NEAR(report.targetLength, 1.4F, kTolerance);
    EXPECT_NEAR(report.currentLength, 1.4F, kTolerance);
    EXPECT_TRUE(report.Shortened());
    EXPECT_FALSE(report.Lengthened());
    EXPECT_FALSE(report.IsDisabled());
}

} // namespace
} // namespace UVE::Physics::Tests
