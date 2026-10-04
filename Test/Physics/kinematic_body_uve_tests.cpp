// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Tests for Kinematic3D's mover: the body an author drives by target velocity - a lift, a moving
// platform, a door - through the engine's real collision world.
//
// The mover is one function, so these tests are one world: real components, the real collision
// system, the real scene graph, real physics steps for the bodies a platform shoves. Nothing here
// is mocked, because the questions worth asking about a platform are all about the world it moves
// in: does it stop at the wall, does it carry a rider, does it shove the crate.
//
// The suite is grouped by what an author would ask:
//   * the driven body        - it goes where it is told, at the speed it was authored with
//   * easing                 - interpolation, and why it is a curve and not a per-step fraction
//   * the world              - walls, floors, thin geometry, and never tunnelling through any of it
//   * switches               - `active`, the object's participation, and a body that stops dead
//   * refusals               - a body this mover cannot drive is left exactly where it was
//   * other bodies           - the crate a platform walks into, and the wall it does not

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

#include <gtest/gtest.h>

#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/physics_object_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"
#include "uve/objects/3d/kinematic_3d_uve.h"
#include "uve/objects/3d/rigid_3d_uve.h"
#include "uve/physics/character_controller_uve.h"
#include "uve/physics/character_world_query_uve.h"
#include "uve/physics/collision_system_uve.h"
#include "uve/physics/kinematic_body_uve.h"
#include "uve/physics/physics_system_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::Kinematic3DComponentUVE;
using Scene::Kinematic3DObjectDefinitionUVE;

// =================================================================================================
// A world with a floor, walls and crates, and one helper that builds a Kinematic3D the way the
// editor does - through the kind's own recipe - then puts it where the test wants it.
// =================================================================================================

class KinematicBodyUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    CollisionSystemUVE collisionSystem;
    /// Zero gravity, like every other physics test here: a kinematic body is never integrated, and
    /// the crates it pushes are tested for the shove and not for how they fall.
    PhysicsSystemUVE physicsSystem{collisionSystem, Math::Vector3UVE{}};

    static constexpr float kDeltaTimeUVE = 1.0F / 60.0F;

    /// A Kinematic3D through the real recipe: PhysicsObject3D base, collider, kinematic body and the
    /// kind's own component.
    [[nodiscard]] Scene::EntityUVE MakeKinematicUVE(
        const Math::Vector3UVE position,
        const Math::Vector3UVE halfExtents = {0.5F, 0.5F, 0.5F},
        const Kinematic3DComponentUVE& kinematics = {},
        const float mass = 1.0F) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Kinematic3DObjectDefinitionUVE definition;
        definition.collider.halfExtents = halfExtents;
        definition.body.mass = mass;
        definition.animatableBody = kinematics;
        Scene::ApplyKinematic3DObjectDefinitionUVE(entityManager, entity, definition);
        PlaceUVE(entity, position);
        return entity;
    }

    /// A collider-only obstacle - a wall or a floor - with no body at all.
    [[nodiscard]] Scene::EntityUVE MakeObstacleUVE(const Math::Vector3UVE position,
                                                   const Math::Vector3UVE halfExtents) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, entity, Scene::TransformComponentUVE{});
        Scene::ColliderComponentUVE collider{halfExtents};
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(entity, collider);
        PlaceUVE(entity, position);
        return entity;
    }

    /// A simulated Rigid3D through its own recipe: something a platform can shove.
    [[nodiscard]] Scene::EntityUVE MakeCrateUVE(const Math::Vector3UVE position,
                                                const Math::Vector3UVE halfExtents = {0.5F, 0.5F, 0.5F},
                                                const float mass = 1.0F) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::Rigid3DObjectDefinitionUVE definition;
        definition.collider.halfExtents = halfExtents;
        definition.body.mass = mass;
        Scene::ApplyRigid3DObjectDefinitionUVE(entityManager, entity, definition);
        PlaceUVE(entity, position);
        return entity;
    }

    void PlaceUVE(const Scene::EntityUVE entity, const Math::Vector3UVE position) {
        Scene::TransformComponentUVE transform =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        transform.localPosition = position;
        sceneGraph.SetLocalTransformUVE(entityManager, entity, transform);
        sceneGraph.UpdateUVE(entityManager);
    }

    [[nodiscard]] KinematicBodyStepResultUVE StepUVE(const Scene::EntityUVE entity,
                                                     const float deltaTimeSeconds = kDeltaTimeUVE) {
        return StepKinematicBodyUVE(entityManager, sceneGraph, collisionSystem, entity, deltaTimeSeconds);
    }

    /// One engine fixed step for the bodies a platform shoves: integrate, then drive the platform.
    /// This is the engine's own order (physics, then the kinematic sync), which is what makes the
    /// push test a test of the real arrangement and not of the mover alone.
    [[nodiscard]] KinematicBodyStepResultUVE StepWorldUVE(const Scene::EntityUVE platform) {
        physicsSystem.StepUVE(entityManager, sceneGraph, kDeltaTimeUVE);
        return StepUVE(platform);
    }

    [[nodiscard]] Math::Vector3UVE PositionUVE(const Scene::EntityUVE entity) {
        return entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localPosition;
    }

    [[nodiscard]] Scene::Rigid3DComponentUVE BodyUVE(const Scene::EntityUVE entity) {
        return entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(entity);
    }

    [[nodiscard]] Kinematic3DComponentUVE KinematicUVE(const Scene::EntityUVE entity) {
        return entityManager.GetComponentUVE<Kinematic3DComponentUVE>(entity);
    }
};

// =================================================================================================
// The driven body.
// =================================================================================================

TEST_F(KinematicBodyUVETest, ABodyDrivenAtItsTargetVelocityMovesExactlyThatFar) {
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 1.0F, true});

    KinematicBodyStepResultUVE report{};
    for (int step = 0; step < 60; ++step) {
        report = StepUVE(platform);
        ASSERT_TRUE(report.IsSteppedUVE()) << "step " << step;
    }

    // One second at one metre per second is one metre, in the direction the author wrote.
    EXPECT_NEAR(PositionUVE(platform).x, 1.0F, 1.0e-3F);
    EXPECT_NEAR(PositionUVE(platform).y, 0.0F, 1.0e-6F);
    EXPECT_NEAR(PositionUVE(platform).z, 0.0F, 1.0e-6F);
    // The body's velocity is the speed it is actually travelling at, which is what a rider reads.
    EXPECT_NEAR(BodyUVE(platform).velocity.x, 1.0F, 1.0e-3F);
    EXPECT_NEAR(report.appliedMotion.x * 60.0F, 1.0F, 1.0e-3F);
    EXPECT_FALSE(report.blocked);
}

TEST_F(KinematicBodyUVETest, TheBodyIsAtItsTargetSpeedOnTheVeryFirstStep) {
    // The default interpolation is 1: no easing, no wind-up, no "it starts moving next frame".
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{2.0F, 0.0F, 0.0F}, 1.0F, true});
    const KinematicBodyStepResultUVE report = StepUVE(platform);

    ASSERT_TRUE(report.IsSteppedUVE());
    EXPECT_NEAR(report.appliedMotion.x, 2.0F * kDeltaTimeUVE, 1.0e-6F);
    EXPECT_NEAR(PositionUVE(platform).x, 2.0F * kDeltaTimeUVE, 1.0e-6F);
}

TEST_F(KinematicBodyUVETest, TheBodyCanBeAuthoredToMoveInAnyDirection) {
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{0.0F, 3.0F, -4.0F}, 1.0F, true});
    for (int step = 0; step < 60; ++step) {
        static_cast<void>(StepUVE(platform));
    }

    const Math::Vector3UVE position = PositionUVE(platform);
    EXPECT_NEAR(position.x, 0.0F, 1.0e-6F);
    EXPECT_NEAR(position.y, 3.0F, 1.0e-3F);
    EXPECT_NEAR(position.z, -4.0F, 1.0e-3F);
}

TEST_F(KinematicBodyUVETest, AChangedTargetVelocitySteersTheBodyOnTheNextStep) {
    // A door that opens and then lifts, a patrol that turns a corner: the mover reads the authored
    // values every step and caches none of them, so a target written at runtime is obeyed at once.
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    for (int step = 0; step < 60; ++step) {
        static_cast<void>(StepUVE(platform));
    }
    ASSERT_NEAR(PositionUVE(platform).x, 1.0F, 1.0e-3F);

    entityManager.GetComponentUVE<Kinematic3DComponentUVE>(platform).targetVelocity =
        Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    for (int step = 0; step < 60; ++step) {
        static_cast<void>(StepUVE(platform));
    }

    const Math::Vector3UVE position = PositionUVE(platform);
    EXPECT_NEAR(position.x, 1.0F, 1.0e-3F) << "the old direction must not keep pushing";
    EXPECT_NEAR(position.y, 1.0F, 1.0e-3F);
}

TEST_F(KinematicBodyUVETest, ATargetOfZeroBringsTheBodyToAStop) {
    // Authored at rest is at rest: a body whose target is zero does not coast on a velocity the
    // world handed it (a script, a previous mode), because that velocity is not the authored one.
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{0.0F, 0.0F, 0.0F}, 1.0F, true});
    entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(platform).velocity =
        Math::Vector3UVE{5.0F, 0.0F, 0.0F};

    const KinematicBodyStepResultUVE report = StepUVE(platform);

    ASSERT_TRUE(report.IsSteppedUVE());
    EXPECT_NEAR(BodyUVE(platform).velocity.x, 0.0F, 1.0e-6F);
    EXPECT_NEAR(PositionUVE(platform).x, 0.0F, 1.0e-6F);
}

TEST_F(KinematicBodyUVETest, AnInterpolationOfZeroKeepsTheVelocityTheBodyAlreadyHas) {
    // The script-driven case: interpolation 0 means the mover never eases the body anywhere, so a
    // script that writes the body's velocity each step is the thing driving it. From rest it is
    // simply a body that never starts on its own.
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{9.0F, 0.0F, 0.0F}, 0.0F, true});
    entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(platform).velocity =
        Math::Vector3UVE{1.0F, 0.0F, 0.0F};

    for (int step = 0; step < 60; ++step) {
        static_cast<void>(StepUVE(platform));
    }

    // Carried by the velocity it had, not by the authored target.
    EXPECT_NEAR(PositionUVE(platform).x, 1.0F, 1.0e-3F);
    EXPECT_NEAR(BodyUVE(platform).velocity.x, 1.0F, 1.0e-3F);
}

// =================================================================================================
// Easing: what `interpolation` means, and why it is a curve rather than a per-step fraction.
// =================================================================================================

TEST(KinematicEaseUVETest, OneIsTheWholeGapAndZeroIsNothing) {
    EXPECT_FLOAT_EQ(KinematicEaseBlendUVE(1.0F, 1.0F / 60.0F), 1.0F);
    EXPECT_FLOAT_EQ(KinematicEaseBlendUVE(0.0F, 1.0F / 60.0F), 0.0F);
    // Anything above 1 is still "now", anything below 0 is still "never": the authored value is
    // clamped by what it means, so a slider dragged past its range cannot invent a negative blend.
    EXPECT_FLOAT_EQ(KinematicEaseBlendUVE(4.0F, 1.0F / 60.0F), 1.0F);
    EXPECT_FLOAT_EQ(KinematicEaseBlendUVE(-2.0F, 1.0F / 60.0F), 0.0F);
}

TEST(KinematicEaseUVETest, ANonFiniteInterpolationOrStepIsNothingRatherThanNaN) {
    // The blend multiplies a velocity: a NaN here would be a body that ends up nowhere at all.
    EXPECT_FLOAT_EQ(KinematicEaseBlendUVE(std::numeric_limits<float>::quiet_NaN(), 1.0F / 60.0F), 0.0F);
    EXPECT_FLOAT_EQ(KinematicEaseBlendUVE(std::numeric_limits<float>::infinity(), 1.0F / 60.0F), 0.0F)
        << "an authored value nobody meant moves the body nowhere, exactly like its NaN cousin";
    EXPECT_FLOAT_EQ(KinematicEaseBlendUVE(0.5F, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(KinematicEaseBlendUVE(0.5F, -1.0F), 0.0F);
    EXPECT_FLOAT_EQ(KinematicEaseBlendUVE(0.5F, std::numeric_limits<float>::quiet_NaN()), 0.0F);
}

TEST(KinematicEaseUVETest, HalfTheGapPerSecondIsHalfTheGapPerSecondAtAnyFrameRate) {
    // The property that makes this a duration and not a per-step fraction: one second of easing at
    // 2 Hz has to leave the same gap as sixty steps of the same second at 60 Hz. Expressing the
    // curve per step instead would make a lift ease five times faster on a 300 Hz machine.
    const float perSecond = KinematicEaseBlendUVE(0.5F, 1.0F);
    const float perStep = KinematicEaseBlendUVE(0.5F, 1.0F / 60.0F);
    float composed = 1.0F;
    for (int step = 0; step < 60; ++step) {
        composed *= (1.0F - perStep);
    }
    EXPECT_NEAR(1.0F - composed, perSecond, 1.0e-4F);
    EXPECT_NEAR(1.0F - composed, 0.5F, 1.0e-4F);
}

TEST_F(KinematicBodyUVETest, AnEasedBodyStartsSlowAndSettlesAtItsTargetSpeed) {
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 0.5F, true});

    const KinematicBodyStepResultUVE first = StepUVE(platform);
    ASSERT_TRUE(first.IsSteppedUVE());
    EXPECT_GT(first.velocity.x, 0.0F) << "an easing body starts moving, it just does not start at speed";
    EXPECT_LT(first.velocity.x, 0.1F);

    float previousSpeed = first.velocity.x;
    for (int step = 0; step < 119; ++step) {
        const float speed = StepUVE(platform).velocity.x;
        EXPECT_GE(speed, previousSpeed - 1.0e-6F) << "step " << step << ": easing must not oscillate";
        previousSpeed = speed;
    }
    // Two seconds of "half the gap per second" is three quarters of the way there - the curve is
    // the authored value, not a tuned ramp.
    EXPECT_NEAR(previousSpeed, 0.75F, 2.0e-2F);

    // Given a second of easing, the body is at its target - and stays there.
    for (int step = 0; step < 600; ++step) {
        previousSpeed = StepUVE(platform).velocity.x;
    }
    EXPECT_NEAR(previousSpeed, 1.0F, 1.0e-3F);
    EXPECT_NEAR(BodyUVE(platform).velocity.x, 1.0F, 1.0e-3F);
}

TEST_F(KinematicBodyUVETest, AnEasedBodyEasesAgainAfterItsTargetChanges) {
    // Easing is not a one-time ramp the body plays on creation: it is the rule that ties the body's
    // velocity to the authored target, every step, for the whole life of the object.
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 0.5F, true});
    for (int step = 0; step < 360; ++step) {
        static_cast<void>(StepUVE(platform));
    }
    ASSERT_NEAR(BodyUVE(platform).velocity.x, 1.0F, 0.05F);

    entityManager.GetComponentUVE<Kinematic3DComponentUVE>(platform).targetVelocity =
        Math::Vector3UVE{3.0F, 0.0F, 0.0F};
    const KinematicBodyStepResultUVE report = StepUVE(platform);

    EXPECT_GT(report.velocity.x, 1.0F) << "the new target has to pull the body toward it";
    EXPECT_LT(report.velocity.x, 3.0F) << "and it eases there rather than snapping";
}

// =================================================================================================
// The world: the reason a kinematic body moves through it instead of simply being teleported.
// =================================================================================================

TEST_F(KinematicBodyUVETest, TheMoveStopsAtAWallInsteadOfPassingThroughIt) {
    // The wall's near face is at x = 1.9, the platform's half width is 0.5, so the furthest it may
    // get is 1.4 - and a body that stops at 1.4 is a body a rider can stand on without being
    // pushed into the wall.
    const Scene::EntityUVE wall = MakeObstacleUVE({2.0F, 0.0F, 0.0F}, {0.1F, 0.5F, 0.5F});
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 1.0F, true});

    KinematicBodyStepResultUVE report{};
    for (int step = 0; step < 240; ++step) {
        report = StepUVE(platform);
    }

    const float restingX = PositionUVE(platform).x;
    EXPECT_LE(restingX, 1.4F + 1.0e-3F) << "the platform may not overlap the wall";
    EXPECT_GE(restingX, 1.35F) << "and it should reach it, not stall in mid-air";
    EXPECT_TRUE(report.blocked);
    EXPECT_GT(report.remainingMotion.x, 0.0F);
    // The velocity handed to anything standing on it is what it actually did, which is nothing.
    EXPECT_LT(BodyUVE(platform).velocity.x, 0.05F);
    // A collider with no body is an obstacle, not a shove-able crate.
    EXPECT_NEAR(PositionUVE(wall).x, 2.0F, 1.0e-6F);
}

TEST_F(KinematicBodyUVETest, AFastBodyIsStoppedByAThinWallRatherThanTunnellingThroughIt) {
    // A platform authored at 60 m/s crosses a whole metre per step. Stepping it positionally would
    // put it on the far side of a thin wall with nothing to report; the swept move stops it at the
    // wall, which is the entire reason this mover goes through the controller's move.
    const Scene::EntityUVE wall = MakeObstacleUVE({3.0F, 0.0F, 0.0F}, {0.05F, 0.5F, 0.5F});
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{60.0F, 0.0F, 0.0F}, 1.0F, true});

    for (int step = 0; step < 10; ++step) {
        static_cast<void>(StepUVE(platform));
    }

    const float restingX = PositionUVE(platform).x;
    EXPECT_LE(restingX, 2.45F + 1.0e-3F);
    EXPECT_GE(restingX, 2.40F);
    EXPECT_NEAR(PositionUVE(wall).x, 3.0F, 1.0e-6F);
}

TEST_F(KinematicBodyUVETest, APlatformDrivenDownComesToRestOnTheFloorAndStopsThere) {
    // A lift going down. It is never integrated - no gravity, no sinking - and the move is what
    // keeps it on top of the floor instead of inside it.
    const Scene::EntityUVE floor = MakeObstacleUVE({0.0F, -0.5F, 0.0F}, {10.0F, 0.5F, 10.0F});
    static_cast<void>(floor);
    const Scene::EntityUVE lift = MakeKinematicUVE({0.0F, 2.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                   Kinematic3DComponentUVE{{0.0F, -2.0F, 0.0F}, 1.0F, true});

    KinematicBodyStepResultUVE report{};
    for (int step = 0; step < 120; ++step) {
        report = StepUVE(lift);
    }

    EXPECT_NEAR(PositionUVE(lift).y, 0.5F, 2.0e-3F);
    EXPECT_GE(PositionUVE(lift).y, 0.5F - 1.0e-3F) << "the lift may not sink into the floor";
    EXPECT_TRUE(report.blocked);
    EXPECT_TRUE(report.grounded) << "a body resting on the floor knows it is standing on something";
    EXPECT_GT(report.groundNormal.y, 0.5F);
}

TEST_F(KinematicBodyUVETest, AStandingBodyIsNotToldToMoveIntoTheFloorItRestsOn) {
    // The degenerate case every level has: a platform with no authored motion, sitting on the
    // floor. It must not sink, drift, or jitter - and it must report that it is standing on
    // something, because a rider and a script both ask.
    const Scene::EntityUVE floor = MakeObstacleUVE({0.0F, -0.5F, 0.0F}, {10.0F, 0.5F, 10.0F});
    static_cast<void>(floor);
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{0.0F, 0.0F, 0.0F}, 1.0F, true});

    for (int step = 0; step < 120; ++step) {
        const KinematicBodyStepResultUVE report = StepUVE(platform);
        ASSERT_TRUE(report.IsSteppedUVE()) << "step " << step;
        EXPECT_NEAR(PositionUVE(platform).y, 0.5F, 2.0e-3F) << "step " << step;
    }
    EXPECT_NEAR(BodyUVE(platform).velocity.y, 0.0F, 1.0e-6F);
}

TEST_F(KinematicBodyUVETest, APlatformMovingSidewaysOnTopOfAFloorIsNotDraggedDownByIt) {
    const Scene::EntityUVE floor = MakeObstacleUVE({0.0F, -0.5F, 0.0F}, {10.0F, 0.5F, 10.0F});
    static_cast<void>(floor);
    const Scene::EntityUVE platform = MakeKinematicUVE({-4.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 1.0F, true});

    for (int step = 0; step < 120; ++step) {
        static_cast<void>(StepUVE(platform));
    }

    const Math::Vector3UVE position = PositionUVE(platform);
    EXPECT_NEAR(position.x, -2.0F, 1.0e-3F) << "sliding along the floor keeps its speed";
    EXPECT_NEAR(position.y, 0.5F, 2.0e-3F) << "and never climbs or sinks";
}

// =================================================================================================
// Switches: `active`, and the object's own participation in the simulation.
// =================================================================================================

TEST_F(KinematicBodyUVETest, AStoppedBodyStaysExactlyWhereItIsAndSaysSo) {
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 1.0F, false});

    KinematicBodyStepResultUVE report{};
    for (int step = 0; step < 60; ++step) {
        report = StepUVE(platform);
        ASSERT_TRUE(report.IsIdleUVE()) << "step " << step;
    }

    EXPECT_EQ(report.code, KinematicBodyStepCodeUVE::Idle);
    EXPECT_NEAR(PositionUVE(platform).x, 0.0F, 1.0e-6F);
    // What a rider reads: a body that is not moving has no velocity to hand anyone.
    EXPECT_NEAR(BodyUVE(platform).velocity.x, 0.0F, 1.0e-6F);
}

TEST_F(KinematicBodyUVETest, StoppingABodyMidFlightTakesAwayTheVelocityItHad) {
    // The switch is exactly where an author wants it to be - nothing to delete, nothing to rebuild,
    // and the body left standing where it was when the power went out.
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    for (int step = 0; step < 60; ++step) {
        static_cast<void>(StepUVE(platform));
    }
    const Math::Vector3UVE stoppedAt = PositionUVE(platform);
    ASSERT_NEAR(BodyUVE(platform).velocity.x, 1.0F, 1.0e-3F);

    entityManager.GetComponentUVE<Kinematic3DComponentUVE>(platform).active = false;
    const KinematicBodyStepResultUVE report = StepUVE(platform);

    EXPECT_TRUE(report.IsIdleUVE());
    EXPECT_NEAR(BodyUVE(platform).velocity.x, 0.0F, 1.0e-6F);
    EXPECT_NEAR(PositionUVE(platform).x, stoppedAt.x, 1.0e-6F);

    // And switching it back on resumes from where it stopped - the component is the only state.
    entityManager.GetComponentUVE<Kinematic3DComponentUVE>(platform).active = true;
    static_cast<void>(StepUVE(platform));
    EXPECT_NEAR(PositionUVE(platform).x, stoppedAt.x + kDeltaTimeUVE, 1.0e-3F);
}

TEST_F(KinematicBodyUVETest, APhysicsObjectTakenOutOfTheSimulationDoesNotMoveAndIsNotMoved) {
    // Two switches, one meaning: the component's own active flag, and the object's participation -
    // a platform whose object is stopped with disable mode Remove is out of the world, and one kept
    // as MakeStatic is an obstacle. Neither is driven.
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    ASSERT_TRUE(entityManager.HasComponentUVE<Scene::ProcessComponentUVE>(platform));
    ASSERT_TRUE(entityManager.HasComponentUVE<Scene::PhysicsObjectComponentUVE>(platform));

    entityManager.GetComponentUVE<Scene::ProcessComponentUVE>(platform).mode = Scene::TickModeUVE::Never;
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_FALSE(Scene::IsPhysicsObjectSimulatedUVE(entityManager, platform));

    EXPECT_TRUE(StepUVE(platform).IsIdleUVE());
    EXPECT_NEAR(PositionUVE(platform).x, 0.0F, 1.0e-6F);
    EXPECT_NEAR(BodyUVE(platform).velocity.x, 0.0F, 1.0e-6F);

    // Kept in the world as an immovable obstacle is still not simulated, so still not driven.
    entityManager.GetComponentUVE<Scene::PhysicsObjectComponentUVE>(platform).disableMode =
        Scene::PhysicsObjectDisableModeUVE::MakeStatic;
    EXPECT_TRUE(Scene::IsPhysicsObjectInWorldUVE(entityManager, platform));
    EXPECT_TRUE(StepUVE(platform).IsIdleUVE());

    // Running again: the platform picks up exactly where it was told to.
    entityManager.GetComponentUVE<Scene::ProcessComponentUVE>(platform).mode =
        Scene::TickModeUVE::Running;
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_TRUE(StepUVE(platform).IsSteppedUVE());
    EXPECT_NEAR(PositionUVE(platform).x, kDeltaTimeUVE, 1.0e-3F);
}

TEST_F(KinematicBodyUVETest, AnAuthoredTargetThatIsNotANumberLeavesTheBodyStandingRatherThanLost) {
    // The component validator refuses a non-finite target on save and in the Inspector; a script
    // can still write one at runtime, and the mover's job is to keep that out of the transform.
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    entityManager.GetComponentUVE<Kinematic3DComponentUVE>(platform).targetVelocity =
        Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};

    const KinematicBodyStepResultUVE report = StepUVE(platform);

    ASSERT_TRUE(report.IsSteppedUVE());
    EXPECT_NEAR(PositionUVE(platform).x, 0.0F, 1.0e-6F);
    EXPECT_NEAR(BodyUVE(platform).velocity.x, 0.0F, 1.0e-6F);
    EXPECT_TRUE(std::isfinite(PositionUVE(platform).x));
}

// =================================================================================================
// Refusals: a body this mover cannot drive is left exactly where it was.
// =================================================================================================

TEST_F(KinematicBodyUVETest, ARefusedBodyIsLeftExactlyWhereItWas) {
    const Math::Vector3UVE where{1.0F, 2.0F, 3.0F};

    // No such entity.
    EXPECT_EQ(StepUVE(Scene::EntityUVE{}).code, KinematicBodyStepCodeUVE::UnknownEntity);

    // A plain obstacle: a transform, a collider, and no Kinematic3D component at all.
    const Scene::EntityUVE obstacle = MakeObstacleUVE(where, {0.5F, 0.5F, 0.5F});
    EXPECT_EQ(StepUVE(obstacle).code, KinematicBodyStepCodeUVE::NotAKinematicBody);
    EXPECT_NEAR(PositionUVE(obstacle).x, where.x, 1.0e-6F);

    // A kinematic body with no collider: nothing to move through the world with.
    const Scene::EntityUVE noCollider = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, noCollider, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Kinematic3DComponentUVE>(noCollider, Kinematic3DComponentUVE{});
    Scene::Rigid3DComponentUVE kinematicBody{};
    kinematicBody.isKinematic = true;
    entityManager.AddComponentUVE<Scene::Rigid3DComponentUVE>(noCollider, kinematicBody);
    PlaceUVE(noCollider, where);
    EXPECT_EQ(StepUVE(noCollider).code, KinematicBodyStepCodeUVE::MissingCollider);
    EXPECT_NEAR(PositionUVE(noCollider).x, where.x, 1.0e-6F);

    // A dynamic body at the same transform: the simulation owns it, and this mover refuses to
    // fight the simulation over one transform.
    const Scene::EntityUVE dynamicBody = MakeKinematicUVE(where, {0.5F, 0.5F, 0.5F},
                                                          Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(dynamicBody).isKinematic = false;
    EXPECT_EQ(StepUVE(dynamicBody).code, KinematicBodyStepCodeUVE::NonKinematicBody);
    EXPECT_NEAR(PositionUVE(dynamicBody).x, where.x, 1.0e-6F);

    // A body with the Kinematic3D component and no rigid body at all: there is nothing to write the
    // velocity back to, so there is nothing to drive.
    const Scene::EntityUVE noBody = entityManager.CreateEntityUVE();
    Scene::Kinematic3DObjectDefinitionUVE definition;
    definition.collider.halfExtents = Math::Vector3UVE{0.5F, 0.5F, 0.5F};
    Scene::ApplyKinematic3DObjectDefinitionUVE(entityManager, noBody, definition);
    PlaceUVE(noBody, where);
    entityManager.RemoveComponentUVE<Scene::Rigid3DComponentUVE>(noBody);
    EXPECT_EQ(StepUVE(noBody).code, KinematicBodyStepCodeUVE::NonKinematicBody);
    EXPECT_NEAR(PositionUVE(noBody).x, where.x, 1.0e-6F);

    // Delta times the mover cannot honour.
    const Scene::EntityUVE platform = MakeKinematicUVE(where, {0.5F, 0.5F, 0.5F},
                                                      Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    EXPECT_EQ(StepUVE(platform, 0.0F).code, KinematicBodyStepCodeUVE::InvalidDeltaTime);
    EXPECT_EQ(StepUVE(platform, -1.0F).code, KinematicBodyStepCodeUVE::InvalidDeltaTime);
    EXPECT_EQ(StepUVE(platform, std::numeric_limits<float>::quiet_NaN()).code,
              KinematicBodyStepCodeUVE::InvalidDeltaTime);
    EXPECT_NEAR(PositionUVE(platform).x, where.x, 1.0e-6F);
}

TEST_F(KinematicBodyUVETest, TheMoverLeavesTheAuthoredComponentAndBodyContractUntouched) {
    // The mover owns one thing: the body's velocity. The authored target, the easing curve and the
    // switch belong to whoever wrote them - an Inspector session, a script, a save file.
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.25F, 0.5F, 0.75F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.5F, -0.5F}, 0.25F, true},
                                                       /*mass=*/2.5F);
    const Scene::Rigid3DComponentUVE bodyBefore = BodyUVE(platform);
    const Scene::ColliderComponentUVE colliderBefore =
        entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(platform);

    for (int step = 0; step < 60; ++step) {
        static_cast<void>(StepUVE(platform));
    }

    const Kinematic3DComponentUVE authored = KinematicUVE(platform);
    EXPECT_NEAR(authored.targetVelocity.x, 1.0F, 1.0e-6F);
    EXPECT_NEAR(authored.targetVelocity.y, 0.5F, 1.0e-6F);
    EXPECT_NEAR(authored.targetVelocity.z, -0.5F, 1.0e-6F);
    EXPECT_FLOAT_EQ(authored.interpolation, 0.25F);
    EXPECT_TRUE(authored.active);

    const Scene::Rigid3DComponentUVE bodyAfter = BodyUVE(platform);
    EXPECT_FLOAT_EQ(bodyAfter.mass, bodyBefore.mass);
    EXPECT_TRUE(bodyAfter.isKinematic);
    EXPECT_FLOAT_EQ(bodyAfter.gravityScale, bodyBefore.gravityScale);
    // Only the velocity moved, and it moved toward the authored target along the authored curve:
    // one second of a quarter-per-second interpolation is a quarter of the way there.
    EXPECT_NEAR(bodyAfter.velocity.x, 0.25F, 2.0e-2F);
    EXPECT_NEAR(bodyAfter.velocity.y, 0.125F, 2.0e-2F);
    EXPECT_NEAR(bodyAfter.velocity.z, -0.125F, 2.0e-2F);

    const Scene::ColliderComponentUVE colliderAfter =
        entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(platform);
    EXPECT_NEAR(colliderAfter.halfExtents.x, colliderBefore.halfExtents.x, 1.0e-6F);
    EXPECT_NEAR(colliderAfter.halfExtents.z, colliderBefore.halfExtents.z, 1.0e-6F);
    EXPECT_EQ(colliderAfter.collisionLayer, colliderBefore.collisionLayer);
    EXPECT_EQ(colliderAfter.collisionMask, colliderBefore.collisionMask);
}

// =================================================================================================
// Other bodies: the crate a platform walks into, and the wall it does not.
// =================================================================================================

TEST_F(KinematicBodyUVETest, AMovingPlatformShovesTheCrateItWalksInto) {
    // The reason a platform is a *kinematic* body and not a script that writes a transform: the
    // bodies it meets get pushed, and the push goes through the same policy a character's shove
    // does - so a crate cannot tell whether a character or a platform shoved it.
    const Scene::EntityUVE crate = MakeCrateUVE({1.25F, 0.0F, 0.0F});
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 1.0F, true});

    std::size_t pushedOnSomeStep = 0U;
    KinematicBodyStepResultUVE report{};
    for (int step = 0; step < 240; ++step) {
        report = StepWorldUVE(platform);
        pushedOnSomeStep = std::max(pushedOnSomeStep, report.pushedBodyCount);
    }

    EXPECT_GE(pushedOnSomeStep, 1U) << "the platform has to report the bodies it moved";
    EXPECT_GT(PositionUVE(crate).x, 1.5F) << "the crate must have been carried along, not driven through";
    // And the two never end up inside each other.
    EXPECT_LE(PositionUVE(platform).x + 0.5F, PositionUVE(crate).x - 0.5F + 2.0e-2F);
}

TEST_F(KinematicBodyUVETest, APlatformNeverShovesItselfOrAStaticObstacle) {
    // The push is for things the simulation may move. An obstacle with no body is not one of them,
    // and the platform is not one of them either - it is the body doing the shoving.
    const Scene::EntityUVE wall = MakeObstacleUVE({1.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    const Scene::EntityUVE platform = MakeKinematicUVE({-3.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 1.0F, true});

    for (int step = 0; step < 240; ++step) {
        const KinematicBodyStepResultUVE report = StepWorldUVE(platform);
        EXPECT_EQ(report.pushedBodyCount, 0U) << "step " << step;
    }

    EXPECT_NEAR(PositionUVE(wall).x, 1.0F, 1.0e-6F);
    EXPECT_LE(PositionUVE(platform).x, 0.5F + 1.0e-3F);
}

TEST_F(KinematicBodyUVETest, APlatformDoesNotPushABodyItIsSwitchedOffBeside) {
    // A stopped body does not shove anything: the crate in front of an idle platform stays exactly
    // where it is, which is what makes "stop the machine" a thing an author can rely on.
    const Scene::EntityUVE crate = MakeCrateUVE({1.25F, 0.0F, 0.0F});
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 1.0F, false});

    for (int step = 0; step < 120; ++step) {
        const KinematicBodyStepResultUVE report = StepWorldUVE(platform);
        ASSERT_TRUE(report.IsIdleUVE());
    }

    EXPECT_NEAR(PositionUVE(crate).x, 1.25F, 1.0e-6F);
    EXPECT_NEAR(BodyUVE(crate).velocity.x, 0.0F, 1.0e-6F);
    EXPECT_NEAR(PositionUVE(platform).x, 0.0F, 1.0e-6F);
}

TEST_F(KinematicBodyUVETest, APlatformAuthoredNotToPushLeavesTheCrateWhereItIs) {
    // The policy is the caller's, so a lift that must not disturb the cargo in its path can be
    // authored that way - and the crate stays put even while the platform presses up against it.
    const Scene::EntityUVE crate = MakeCrateUVE({1.25F, 0.0F, 0.0F});
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    KinematicBodyPushPolicyUVE noPush;
    noPush.enabled = false;

    for (int step = 0; step < 240; ++step) {
        physicsSystem.StepUVE(entityManager, sceneGraph, kDeltaTimeUVE);
        const KinematicBodyStepResultUVE report =
            StepKinematicBodyUVE(entityManager, sceneGraph, collisionSystem, platform, kDeltaTimeUVE, noPush);
        EXPECT_EQ(report.pushedBodyCount, 0U) << "step " << step;
    }

    EXPECT_NEAR(PositionUVE(crate).x, 1.25F, 1.0e-6F);
    EXPECT_NEAR(BodyUVE(crate).velocity.x, 0.0F, 1.0e-6F);
    // The platform still stopped - the crate is a wall to it either way.
    EXPECT_LE(PositionUVE(platform).x, 0.75F + 1.0e-3F);
}

// =================================================================================================
// The push policy itself.
// =================================================================================================

TEST(KinematicBodyPushPolicyUVETest, ThePlatformShoveIsTheCharacterShove) {
    // One policy for "I am moving and something is in my way", used by both, so a crate that a
    // character can nudge is a crate a platform can nudge - and neither can launch it.
    const KinematicBodyPushPolicyUVE policy = DefaultKinematicBodyPushPolicyUVE();
    const CharacterControllerInputUVE characterInput;

    EXPECT_TRUE(policy.enabled);
    EXPECT_FLOAT_EQ(policy.strength, characterInput.dynamicBodyPushStrength);
    EXPECT_FLOAT_EQ(policy.maximumSpeed, characterInput.maximumDynamicBodyPushSpeed);
}

TEST(KinematicBodyStepCodeUVETest, OnlyAMovedStepClaimsToHaveStepped) {
    KinematicBodyStepResultUVE report;
    report.code = KinematicBodyStepCodeUVE::Idle;
    EXPECT_TRUE(report.IsIdleUVE());
    EXPECT_FALSE(report.IsSteppedUVE());

    report.code = KinematicBodyStepCodeUVE::Moved;
    EXPECT_TRUE(report.IsSteppedUVE());
    EXPECT_FALSE(report.IsIdleUVE());
    EXPECT_FALSE(report.PushedAnythingUVE());

    report.pushedBodyCount = 1U;
    EXPECT_TRUE(report.PushedAnythingUVE());
}

// =================================================================================================
// The engine's own wiring: the fixed step drives Kinematic3D objects, in the order the sync
// declares. The sync itself lives in the engine core; what this pins is the mover it calls, with
// the arrangement the engine uses it in (physics first, then the platform, then whatever rides it).
// =================================================================================================

TEST_F(KinematicBodyUVETest, TheFixedStepArrangementCarriesARiderTheSameStepThePlatformMoves) {
    // A character standing on a platform reads how far its platform moved since the last step. That
    // is only true if the platform's move for this step is already in the world when the character
    // asks - which is why the engine runs the kinematic sync before the character step, and why
    // this test runs them in that order, out of one physics step.
    const Scene::EntityUVE platform = MakeKinematicUVE({0.0F, 0.0F, 0.0F}, {2.0F, 0.5F, 2.0F},
                                                       Kinematic3DComponentUVE{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    const Scene::EntityUVE rider = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, rider, Scene::TransformComponentUVE{});
    Scene::ColliderComponentUVE riderCollider{Math::Vector3UVE{0.4F, 0.4F, 0.4F}};
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(rider, riderCollider);
    entityManager.AddComponentUVE<Scene::CharacterControllerComponentUVE>(
        rider, Scene::CharacterControllerComponentUVE{});
    PlaceUVE(rider, {0.0F, 0.9F, 0.0F});

    // Ride for a moment so the body is genuinely standing on the platform it will be carried by.
    // The engine's arrangement, one step: physics, the platform, then the character.
    const auto stepWorldUVE = [&]() {
        physicsSystem.StepUVE(entityManager, sceneGraph, kDeltaTimeUVE);
        const KinematicBodyStepResultUVE platformReport = StepUVE(platform);
        const Character3DStepResultUVE riderReport = StepCharacter3DUVE(
            entityManager, sceneGraph, collisionSystem, rider, CharacterMotionInputUVE{},
            /*gravityY=*/-9.81F, kDeltaTimeUVE);
        return std::pair{platformReport, riderReport};
    };

    for (int step = 0; step < 10; ++step) {
        const auto [platformReport, riderReport] = stepWorldUVE();
        ASSERT_TRUE(platformReport.IsSteppedUVE());
        ASSERT_TRUE(riderReport.stepped) << "step " << step;
    }
    const float startX = PositionUVE(rider).x;

    bool carried = false;
    Math::Vector3UVE platformVelocity{};
    for (int step = 0; step < 30; ++step) {
        const auto [platformReport, riderReport] = stepWorldUVE();
        ASSERT_TRUE(platformReport.IsSteppedUVE());
        ASSERT_TRUE(riderReport.stepped) << "step " << step;
        carried = carried || riderReport.motion.carriedByPlatform;
        platformVelocity = platformReport.velocity;
    }

    // Half a second at one metre per second: the rider went with the platform, not left behind.
    EXPECT_TRUE(carried) << "the platform's own move is what the rider reads";
    EXPECT_NEAR(PositionUVE(rider).x - startX, 0.5F, 5.0e-2F);
    EXPECT_NEAR(platformVelocity.x, 1.0F, 1.0e-3F);
}

} // namespace
} // namespace UVE::Physics::Tests
