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

#include "character_controller_component.h"
#include "collider_component.h"
#include "physics_object_component.h"
#include "process_component.h"
#include "rigid_3d_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "quaternion.h"
#include "vector3.h"
#include "memory_manager.h"
#include "abstract_physics_objects_3d.h"
#include "kinematic_3d.h"
#include "rigid_3d.h"
#include "character_controller.h"
#include "character_world_query.h"
#include "collision_system.h"
#include "kinematic_body.h"
#include "physics_system.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::Kinematic3DComponent;
using Scene::Kinematic3DObjectDefinition;

// =================================================================================================
// A world with a floor, walls and crates, and one helper that builds a Kinematic3D the way the
// editor does - through the kind's own recipe - then puts it where the test wants it.
// =================================================================================================

class KinematicBodyTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    CollisionSystem collisionSystem;
    /// Zero gravity, like every other physics test here: a kinematic body is never integrated, and
    /// the crates it pushes are tested for the shove and not for how they fall.
    PhysicsSystem physicsSystem{collisionSystem, Math::Vector3{}};

    static constexpr float kDeltaTime = 1.0F / 60.0F;

    /// A Kinematic3D through the real recipe: PhysicsObject3D base, collider, kinematic body and the
    /// kind's own component.
    [[nodiscard]] Scene::Entity MakeKinematic(
        const Math::Vector3 position,
        const Math::Vector3 halfExtents = {0.5F, 0.5F, 0.5F},
        const Kinematic3DComponent& kinematics = {},
        const float mass = 1.0F) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Kinematic3DObjectDefinition definition;
        definition.collider.halfExtents = halfExtents;
        definition.body.mass = mass;
        definition.animatableBody = kinematics;
        Scene::ApplyKinematic3DObjectDefinition(entityManager, entity, definition);
        Place(entity, position);
        return entity;
    }

    /// A collider-only obstacle - a wall or a floor - with no body at all.
    [[nodiscard]] Scene::Entity MakeObstacle(const Math::Vector3 position,
                                                   const Math::Vector3 halfExtents) {
        const Scene::Entity entity = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, entity, Scene::TransformComponent{});
        Scene::ColliderComponent collider{halfExtents};
        entityManager.AddComponent<Scene::ColliderComponent>(entity, collider);
        Place(entity, position);
        return entity;
    }

    /// A simulated Rigid3D through its own recipe: something a platform can shove.
    [[nodiscard]] Scene::Entity MakeCrate(const Math::Vector3 position,
                                                const Math::Vector3 halfExtents = {0.5F, 0.5F, 0.5F},
                                                const float mass = 1.0F) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::Rigid3DObjectDefinition definition;
        definition.collider.halfExtents = halfExtents;
        definition.body.mass = mass;
        Scene::ApplyRigid3DObjectDefinition(entityManager, entity, definition);
        Place(entity, position);
        return entity;
    }

    void Place(const Scene::Entity entity, const Math::Vector3 position) {
        Scene::TransformComponent transform =
            entityManager.GetComponent<Scene::TransformComponent>(entity);
        transform.localPosition = position;
        sceneGraph.SetLocalTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
    }

    [[nodiscard]] KinematicBodyStepResult Step(const Scene::Entity entity,
                                                     const float deltaTimeSeconds = kDeltaTime) {
        return StepKinematicBody(entityManager, sceneGraph, collisionSystem, entity, deltaTimeSeconds);
    }

    /// One engine fixed step for the bodies a platform shoves: integrate, then drive the platform.
    /// This is the engine's own order (physics, then the kinematic sync), which is what makes the
    /// push test a test of the real arrangement and not of the mover alone.
    [[nodiscard]] KinematicBodyStepResult StepWorld(const Scene::Entity platform) {
        physicsSystem.Step(entityManager, sceneGraph, kDeltaTime);
        return Step(platform);
    }

    [[nodiscard]] Math::Vector3 Position(const Scene::Entity entity) {
        return entityManager.GetComponent<Scene::TransformComponent>(entity).localPosition;
    }

    [[nodiscard]] Scene::Rigid3DComponent Body(const Scene::Entity entity) {
        return entityManager.GetComponent<Scene::Rigid3DComponent>(entity);
    }

    [[nodiscard]] Kinematic3DComponent Kinematic(const Scene::Entity entity) {
        return entityManager.GetComponent<Kinematic3DComponent>(entity);
    }
};

// =================================================================================================
// The driven body.
// =================================================================================================

TEST_F(KinematicBodyTest, ABodyDrivenAtItsTargetVelocityMovesExactlyThatFar) {
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, true});

    KinematicBodyStepResult report{};
    for (int step = 0; step < 60; ++step) {
        report = Step(platform);
        ASSERT_TRUE(report.IsStepped()) << "step " << step;
    }

    // One second at one metre per second is one metre, in the direction the author wrote.
    EXPECT_NEAR(Position(platform).x, 1.0F, 1.0e-3F);
    EXPECT_NEAR(Position(platform).y, 0.0F, 1.0e-6F);
    EXPECT_NEAR(Position(platform).z, 0.0F, 1.0e-6F);
    // The body's velocity is the speed it is actually travelling at, which is what a rider reads.
    EXPECT_NEAR(Body(platform).velocity.x, 1.0F, 1.0e-3F);
    EXPECT_NEAR(report.appliedMotion.x * 60.0F, 1.0F, 1.0e-3F);
    EXPECT_FALSE(report.blocked);
}

TEST_F(KinematicBodyTest, TheBodyIsAtItsTargetSpeedOnTheVeryFirstStep) {
    // The default interpolation is 1: no easing, no wind-up, no "it starts moving next frame".
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{2.0F, 0.0F, 0.0F}, 1.0F, true});
    const KinematicBodyStepResult report = Step(platform);

    ASSERT_TRUE(report.IsStepped());
    EXPECT_NEAR(report.appliedMotion.x, 2.0F * kDeltaTime, 1.0e-6F);
    EXPECT_NEAR(Position(platform).x, 2.0F * kDeltaTime, 1.0e-6F);
}

TEST_F(KinematicBodyTest, TheAuthoredTargetIsTakenInLocalAxes) {
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    Math::Quaternion quarterTurn{};
    ASSERT_TRUE(Math::TryMakeAxisAngle({0.0F, 1.0F, 0.0F}, 1.5707963F, quarterTurn));
    Scene::TransformComponent transform = entityManager.GetComponent<Scene::TransformComponent>(platform);
    transform.localRotation = quarterTurn;
    sceneGraph.SetLocalTransform(entityManager, platform, transform);
    sceneGraph.Update(entityManager);

    const KinematicBodyStepResult report = Step(platform);
    ASSERT_TRUE(report.IsStepped());
    const Math::Vector3 expected = Math::RotateVector(quarterTurn, Math::Vector3{1.0F, 0.0F, 0.0F}) *
                                      kDeltaTime;
    EXPECT_NEAR(report.appliedMotion.x, expected.x, 1.0e-4F);
    EXPECT_NEAR(report.appliedMotion.y, expected.y, 1.0e-4F);
    EXPECT_NEAR(report.appliedMotion.z, expected.z, 1.0e-4F);
    EXPECT_NEAR(Position(platform).x, expected.x, 1.0e-4F);
    EXPECT_NEAR(Position(platform).z, expected.z, 1.0e-4F);
}

TEST_F(KinematicBodyTest, ADisabledColliderIsNotAWorldTheMoverCanDrive) {
    const Scene::Entity platform = MakeKinematic({1.0F, 2.0F, 3.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    entityManager.GetComponent<Scene::ColliderComponent>(platform).disabled = true;
    const KinematicBodyStepResult report = Step(platform);
    EXPECT_EQ(report.code, KinematicBodyStepCode::InvalidWorld);
    EXPECT_NEAR(Position(platform).x, 1.0F, 1.0e-6F);
    EXPECT_NEAR(Position(platform).y, 2.0F, 1.0e-6F);
    EXPECT_NEAR(Position(platform).z, 3.0F, 1.0e-6F);
}

TEST_F(KinematicBodyTest, TheBodyCanBeAuthoredToMoveInAnyDirection) {
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{0.0F, 3.0F, -4.0F}, 1.0F, true});
    for (int step = 0; step < 60; ++step) {
        static_cast<void>(Step(platform));
    }

    const Math::Vector3 position = Position(platform);
    EXPECT_NEAR(position.x, 0.0F, 1.0e-6F);
    EXPECT_NEAR(position.y, 3.0F, 1.0e-3F);
    EXPECT_NEAR(position.z, -4.0F, 1.0e-3F);
}

TEST_F(KinematicBodyTest, AChangedTargetVelocitySteersTheBodyOnTheNextStep) {
    // A door that opens and then lifts, a patrol that turns a corner: the mover reads the authored
    // values every step and caches none of them, so a target written at runtime is obeyed at once.
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    for (int step = 0; step < 60; ++step) {
        static_cast<void>(Step(platform));
    }
    ASSERT_NEAR(Position(platform).x, 1.0F, 1.0e-3F);

    entityManager.GetComponent<Kinematic3DComponent>(platform).targetVelocity =
        Math::Vector3{0.0F, 1.0F, 0.0F};
    for (int step = 0; step < 60; ++step) {
        static_cast<void>(Step(platform));
    }

    const Math::Vector3 position = Position(platform);
    EXPECT_NEAR(position.x, 1.0F, 1.0e-3F) << "the old direction must not keep pushing";
    EXPECT_NEAR(position.y, 1.0F, 1.0e-3F);
}

TEST_F(KinematicBodyTest, ATargetOfZeroBringsTheBodyToAStop) {
    // Authored at rest is at rest: a body whose target is zero does not coast on a velocity the
    // world handed it (a script, a previous mode), because that velocity is not the authored one.
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{0.0F, 0.0F, 0.0F}, 1.0F, true});
    entityManager.GetComponent<Scene::Rigid3DComponent>(platform).velocity =
        Math::Vector3{5.0F, 0.0F, 0.0F};

    const KinematicBodyStepResult report = Step(platform);

    ASSERT_TRUE(report.IsStepped());
    EXPECT_NEAR(Body(platform).velocity.x, 0.0F, 1.0e-6F);
    EXPECT_NEAR(Position(platform).x, 0.0F, 1.0e-6F);
}

TEST_F(KinematicBodyTest, AnInterpolationOfZeroKeepsTheVelocityTheBodyAlreadyHas) {
    // The script-driven case: interpolation 0 means the mover never eases the body anywhere, so a
    // script that writes the body's velocity each step is the thing driving it. From rest it is
    // simply a body that never starts on its own.
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{9.0F, 0.0F, 0.0F}, 0.0F, true});
    entityManager.GetComponent<Scene::Rigid3DComponent>(platform).velocity =
        Math::Vector3{1.0F, 0.0F, 0.0F};

    for (int step = 0; step < 60; ++step) {
        static_cast<void>(Step(platform));
    }

    // Carried by the velocity it had, not by the authored target.
    EXPECT_NEAR(Position(platform).x, 1.0F, 1.0e-3F);
    EXPECT_NEAR(Body(platform).velocity.x, 1.0F, 1.0e-3F);
}

// =================================================================================================
// Easing: what `interpolation` means, and why it is a curve rather than a per-step fraction.
// =================================================================================================

TEST(KinematicEaseTest, OneIsTheWholeGapAndZeroIsNothing) {
    EXPECT_FLOAT_EQ(KinematicEaseBlend(1.0F, 1.0F / 60.0F), 1.0F);
    EXPECT_FLOAT_EQ(KinematicEaseBlend(0.0F, 1.0F / 60.0F), 0.0F);
    // Anything above 1 is still "now", anything below 0 is still "never": the authored value is
    // clamped by what it means, so a slider dragged past its range cannot invent a negative blend.
    EXPECT_FLOAT_EQ(KinematicEaseBlend(4.0F, 1.0F / 60.0F), 1.0F);
    EXPECT_FLOAT_EQ(KinematicEaseBlend(-2.0F, 1.0F / 60.0F), 0.0F);
}

TEST(KinematicEaseTest, ANonFiniteInterpolationOrStepIsNothingRatherThanNaN) {
    // The blend multiplies a velocity: a NaN here would be a body that ends up nowhere at all.
    EXPECT_FLOAT_EQ(KinematicEaseBlend(std::numeric_limits<float>::quiet_NaN(), 1.0F / 60.0F), 0.0F);
    EXPECT_FLOAT_EQ(KinematicEaseBlend(std::numeric_limits<float>::infinity(), 1.0F / 60.0F), 0.0F)
        << "an authored value nobody meant moves the body nowhere, exactly like its NaN cousin";
    EXPECT_FLOAT_EQ(KinematicEaseBlend(0.5F, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(KinematicEaseBlend(0.5F, -1.0F), 0.0F);
    EXPECT_FLOAT_EQ(KinematicEaseBlend(0.5F, std::numeric_limits<float>::quiet_NaN()), 0.0F);
}

TEST(KinematicEaseTest, HalfTheGapPerSecondIsHalfTheGapPerSecondAtAnyFrameRate) {
    // The property that makes this a duration and not a per-step fraction: one second of easing at
    // 2 Hz has to leave the same gap as sixty steps of the same second at 60 Hz. Expressing the
    // curve per step instead would make a lift ease five times faster on a 300 Hz machine.
    const float perSecond = KinematicEaseBlend(0.5F, 1.0F);
    const float perStep = KinematicEaseBlend(0.5F, 1.0F / 60.0F);
    float composed = 1.0F;
    for (int step = 0; step < 60; ++step) {
        composed *= (1.0F - perStep);
    }
    EXPECT_NEAR(1.0F - composed, perSecond, 1.0e-4F);
    EXPECT_NEAR(1.0F - composed, 0.5F, 1.0e-4F);
}

TEST_F(KinematicBodyTest, AnEasedBodyStartsSlowAndSettlesAtItsTargetSpeed) {
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 0.5F, true});

    const KinematicBodyStepResult first = Step(platform);
    ASSERT_TRUE(first.IsStepped());
    EXPECT_GT(first.velocity.x, 0.0F) << "an easing body starts moving, it just does not start at speed";
    EXPECT_LT(first.velocity.x, 0.1F);

    float previousSpeed = first.velocity.x;
    for (int step = 0; step < 119; ++step) {
        const float speed = Step(platform).velocity.x;
        EXPECT_GE(speed, previousSpeed - 1.0e-6F) << "step " << step << ": easing must not oscillate";
        previousSpeed = speed;
    }
    // Two seconds of "half the gap per second" is three quarters of the way there - the curve is
    // the authored value, not a tuned ramp.
    EXPECT_NEAR(previousSpeed, 0.75F, 2.0e-2F);

    // Given a second of easing, the body is at its target - and stays there.
    for (int step = 0; step < 600; ++step) {
        previousSpeed = Step(platform).velocity.x;
    }
    EXPECT_NEAR(previousSpeed, 1.0F, 1.0e-3F);
    EXPECT_NEAR(Body(platform).velocity.x, 1.0F, 1.0e-3F);
}

TEST_F(KinematicBodyTest, AnEasedBodyEasesAgainAfterItsTargetChanges) {
    // Easing is not a one-time ramp the body plays on creation: it is the rule that ties the body's
    // velocity to the authored target, every step, for the whole life of the object.
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 0.5F, true});
    for (int step = 0; step < 360; ++step) {
        static_cast<void>(Step(platform));
    }
    ASSERT_NEAR(Body(platform).velocity.x, 1.0F, 0.05F);

    entityManager.GetComponent<Kinematic3DComponent>(platform).targetVelocity =
        Math::Vector3{3.0F, 0.0F, 0.0F};
    const KinematicBodyStepResult report = Step(platform);

    EXPECT_GT(report.velocity.x, 1.0F) << "the new target has to pull the body toward it";
    EXPECT_LT(report.velocity.x, 3.0F) << "and it eases there rather than snapping";
}

// =================================================================================================
// The world: the reason a kinematic body moves through it instead of simply being teleported.
// =================================================================================================

TEST_F(KinematicBodyTest, TheMoveStopsAtAWallInsteadOfPassingThroughIt) {
    // The wall's near face is at x = 1.9, the platform's half width is 0.5, so the furthest it may
    // get is 1.4 - and a body that stops at 1.4 is a body a rider can stand on without being
    // pushed into the wall.
    const Scene::Entity wall = MakeObstacle({2.0F, 0.0F, 0.0F}, {0.1F, 0.5F, 0.5F});
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, true});

    KinematicBodyStepResult report{};
    for (int step = 0; step < 240; ++step) {
        report = Step(platform);
    }

    const float restingX = Position(platform).x;
    EXPECT_LE(restingX, 1.4F + 1.0e-3F) << "the platform may not overlap the wall";
    EXPECT_GE(restingX, 1.35F) << "and it should reach it, not stall in mid-air";
    EXPECT_TRUE(report.blocked);
    EXPECT_GT(report.remainingMotion.x, 0.0F);
    // The velocity handed to anything standing on it is what it actually did, which is nothing.
    EXPECT_LT(Body(platform).velocity.x, 0.05F);
    // A collider with no body is an obstacle, not a shove-able crate.
    EXPECT_NEAR(Position(wall).x, 2.0F, 1.0e-6F);
}

TEST_F(KinematicBodyTest, AFastBodyIsStoppedByAThinWallRatherThanTunnellingThroughIt) {
    // A platform authored at 60 m/s crosses a whole metre per step. Stepping it positionally would
    // put it on the far side of a thin wall with nothing to report; the swept move stops it at the
    // wall, which is the entire reason this mover goes through the controller's move.
    const Scene::Entity wall = MakeObstacle({3.0F, 0.0F, 0.0F}, {0.05F, 0.5F, 0.5F});
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{60.0F, 0.0F, 0.0F}, 1.0F, true});

    for (int step = 0; step < 10; ++step) {
        static_cast<void>(Step(platform));
    }

    const float restingX = Position(platform).x;
    EXPECT_LE(restingX, 2.45F + 1.0e-3F);
    EXPECT_GE(restingX, 2.40F);
    EXPECT_NEAR(Position(wall).x, 3.0F, 1.0e-6F);
}

TEST_F(KinematicBodyTest, APlatformDrivenDownComesToRestOnTheFloorAndStopsThere) {
    // A lift going down. It is never integrated - no gravity, no sinking - and the move is what
    // keeps it on top of the floor instead of inside it.
    const Scene::Entity floor = MakeObstacle({0.0F, -0.5F, 0.0F}, {10.0F, 0.5F, 10.0F});
    static_cast<void>(floor);
    const Scene::Entity lift = MakeKinematic({0.0F, 2.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                   Kinematic3DComponent{{0.0F, -2.0F, 0.0F}, 1.0F, true});

    KinematicBodyStepResult report{};
    for (int step = 0; step < 120; ++step) {
        report = Step(lift);
    }

    EXPECT_NEAR(Position(lift).y, 0.5F, 2.0e-3F);
    EXPECT_GE(Position(lift).y, 0.5F - 1.0e-3F) << "the lift may not sink into the floor";
    EXPECT_TRUE(report.blocked);
    EXPECT_TRUE(report.grounded) << "a body resting on the floor knows it is standing on something";
    EXPECT_GT(report.groundNormal.y, 0.5F);
}

TEST_F(KinematicBodyTest, AStandingBodyIsNotToldToMoveIntoTheFloorItRestsOn) {
    // The degenerate case every level has: a platform with no authored motion, sitting on the
    // floor. It must not sink, drift, or jitter - and it must report that it is standing on
    // something, because a rider and a script both ask.
    const Scene::Entity floor = MakeObstacle({0.0F, -0.5F, 0.0F}, {10.0F, 0.5F, 10.0F});
    static_cast<void>(floor);
    const Scene::Entity platform = MakeKinematic({0.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{0.0F, 0.0F, 0.0F}, 1.0F, true});

    for (int step = 0; step < 120; ++step) {
        const KinematicBodyStepResult report = Step(platform);
        ASSERT_TRUE(report.IsStepped()) << "step " << step;
        EXPECT_NEAR(Position(platform).y, 0.5F, 2.0e-3F) << "step " << step;
    }
    EXPECT_NEAR(Body(platform).velocity.y, 0.0F, 1.0e-6F);
}

TEST_F(KinematicBodyTest, APlatformMovingSidewaysOnTopOfAFloorIsNotDraggedDownByIt) {
    const Scene::Entity floor = MakeObstacle({0.0F, -0.5F, 0.0F}, {10.0F, 0.5F, 10.0F});
    static_cast<void>(floor);
    const Scene::Entity platform = MakeKinematic({-4.0F, 0.5F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, true});

    for (int step = 0; step < 120; ++step) {
        static_cast<void>(Step(platform));
    }

    const Math::Vector3 position = Position(platform);
    EXPECT_NEAR(position.x, -2.0F, 1.0e-3F) << "sliding along the floor keeps its speed";
    EXPECT_NEAR(position.y, 0.5F, 2.0e-3F) << "and never climbs or sinks";
}

// =================================================================================================
// Switches: `active`, and the object's own participation in the simulation.
// =================================================================================================

TEST_F(KinematicBodyTest, AStoppedBodyStaysExactlyWhereItIsAndSaysSo) {
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, false});

    KinematicBodyStepResult report{};
    for (int step = 0; step < 60; ++step) {
        report = Step(platform);
        ASSERT_TRUE(report.IsIdle()) << "step " << step;
    }

    EXPECT_EQ(report.code, KinematicBodyStepCode::Idle);
    EXPECT_NEAR(Position(platform).x, 0.0F, 1.0e-6F);
    // What a rider reads: a body that is not moving has no velocity to hand anyone.
    EXPECT_NEAR(Body(platform).velocity.x, 0.0F, 1.0e-6F);
}

TEST_F(KinematicBodyTest, StoppingABodyMidFlightTakesAwayTheVelocityItHad) {
    // The switch is exactly where an author wants it to be - nothing to delete, nothing to rebuild,
    // and the body left standing where it was when the power went out.
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    for (int step = 0; step < 60; ++step) {
        static_cast<void>(Step(platform));
    }
    const Math::Vector3 stoppedAt = Position(platform);
    ASSERT_NEAR(Body(platform).velocity.x, 1.0F, 1.0e-3F);

    entityManager.GetComponent<Kinematic3DComponent>(platform).active = false;
    const KinematicBodyStepResult report = Step(platform);

    EXPECT_TRUE(report.IsIdle());
    EXPECT_NEAR(Body(platform).velocity.x, 0.0F, 1.0e-6F);
    EXPECT_NEAR(Position(platform).x, stoppedAt.x, 1.0e-6F);

    // And switching it back on resumes from where it stopped - the component is the only state.
    entityManager.GetComponent<Kinematic3DComponent>(platform).active = true;
    static_cast<void>(Step(platform));
    EXPECT_NEAR(Position(platform).x, stoppedAt.x + kDeltaTime, 1.0e-3F);
}

TEST_F(KinematicBodyTest, APhysicsObjectTakenOutOfTheSimulationDoesNotMoveAndIsNotMoved) {
    // Two switches, one meaning: the component's own active flag, and the object's participation -
    // a platform whose object is stopped with disable mode Remove is out of the world, and one kept
    // as MakeStatic is an obstacle. Neither is driven.
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    ASSERT_TRUE(entityManager.HasComponent<Scene::ProcessComponent>(platform));
    ASSERT_TRUE(entityManager.HasComponent<Scene::PhysicsObjectComponent>(platform));

    entityManager.GetComponent<Scene::ProcessComponent>(platform).mode = Scene::TickMode::Never;
    sceneGraph.Update(entityManager);
    EXPECT_FALSE(Scene::IsPhysicsObjectSimulated(entityManager, platform));

    EXPECT_TRUE(Step(platform).IsIdle());
    EXPECT_NEAR(Position(platform).x, 0.0F, 1.0e-6F);
    EXPECT_NEAR(Body(platform).velocity.x, 0.0F, 1.0e-6F);

    // Kept in the world as an immovable obstacle is still not simulated, so still not driven.
    entityManager.GetComponent<Scene::PhysicsObjectComponent>(platform).disableMode =
        Scene::PhysicsObjectDisableMode::MakeStatic;
    EXPECT_TRUE(Scene::IsPhysicsObjectInWorld(entityManager, platform));
    EXPECT_TRUE(Step(platform).IsIdle());

    // Running again: the platform picks up exactly where it was told to.
    entityManager.GetComponent<Scene::ProcessComponent>(platform).mode =
        Scene::TickMode::Running;
    sceneGraph.Update(entityManager);
    EXPECT_TRUE(Step(platform).IsStepped());
    EXPECT_NEAR(Position(platform).x, kDeltaTime, 1.0e-3F);
}

TEST_F(KinematicBodyTest, AnAuthoredTargetThatIsNotANumberLeavesTheBodyStandingRatherThanLost) {
    // The component validator refuses a non-finite target on save and in the Inspector; a script
    // can still write one at runtime, and the mover's job is to keep that out of the transform.
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    entityManager.GetComponent<Kinematic3DComponent>(platform).targetVelocity =
        Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};

    const KinematicBodyStepResult report = Step(platform);

    ASSERT_TRUE(report.IsStepped());
    EXPECT_NEAR(Position(platform).x, 0.0F, 1.0e-6F);
    EXPECT_NEAR(Body(platform).velocity.x, 0.0F, 1.0e-6F);
    EXPECT_TRUE(std::isfinite(Position(platform).x));
}

// =================================================================================================
// Refusals: a body this mover cannot drive is left exactly where it was.
// =================================================================================================

TEST_F(KinematicBodyTest, ARefusedBodyIsLeftExactlyWhereItWas) {
    const Math::Vector3 where{1.0F, 2.0F, 3.0F};

    // No such entity.
    EXPECT_EQ(Step(Scene::Entity{}).code, KinematicBodyStepCode::UnknownEntity);

    // A plain obstacle: a transform, a collider, and no Kinematic3D component at all.
    const Scene::Entity obstacle = MakeObstacle(where, {0.5F, 0.5F, 0.5F});
    EXPECT_EQ(Step(obstacle).code, KinematicBodyStepCode::NotAKinematicBody);
    EXPECT_NEAR(Position(obstacle).x, where.x, 1.0e-6F);

    // A kinematic body with no collider: nothing to move through the world with.
    const Scene::Entity noCollider = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, noCollider, Scene::TransformComponent{});
    entityManager.AddComponent<Kinematic3DComponent>(noCollider, Kinematic3DComponent{});
    Scene::Rigid3DComponent kinematicBody{};
    kinematicBody.isKinematic = true;
    entityManager.AddComponent<Scene::Rigid3DComponent>(noCollider, kinematicBody);
    Place(noCollider, where);
    EXPECT_EQ(Step(noCollider).code, KinematicBodyStepCode::MissingCollider);
    EXPECT_NEAR(Position(noCollider).x, where.x, 1.0e-6F);

    // A dynamic body at the same transform: the simulation owns it, and this mover refuses to
    // fight the simulation over one transform.
    const Scene::Entity dynamicBody = MakeKinematic(where, {0.5F, 0.5F, 0.5F},
                                                          Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    entityManager.GetComponent<Scene::Rigid3DComponent>(dynamicBody).isKinematic = false;
    EXPECT_EQ(Step(dynamicBody).code, KinematicBodyStepCode::NonKinematicBody);
    EXPECT_NEAR(Position(dynamicBody).x, where.x, 1.0e-6F);

    // A body with the Kinematic3D component and no rigid body at all: there is nothing to write the
    // velocity back to, so there is nothing to drive.
    const Scene::Entity noBody = entityManager.CreateEntity();
    Scene::Kinematic3DObjectDefinition definition;
    definition.collider.halfExtents = Math::Vector3{0.5F, 0.5F, 0.5F};
    Scene::ApplyKinematic3DObjectDefinition(entityManager, noBody, definition);
    Place(noBody, where);
    entityManager.RemoveComponent<Scene::Rigid3DComponent>(noBody);
    EXPECT_EQ(Step(noBody).code, KinematicBodyStepCode::NonKinematicBody);
    EXPECT_NEAR(Position(noBody).x, where.x, 1.0e-6F);

    // Delta times the mover cannot honour.
    const Scene::Entity platform = MakeKinematic(where, {0.5F, 0.5F, 0.5F},
                                                      Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    EXPECT_EQ(Step(platform, 0.0F).code, KinematicBodyStepCode::InvalidDeltaTime);
    EXPECT_EQ(Step(platform, -1.0F).code, KinematicBodyStepCode::InvalidDeltaTime);
    EXPECT_EQ(Step(platform, std::numeric_limits<float>::quiet_NaN()).code,
              KinematicBodyStepCode::InvalidDeltaTime);
    EXPECT_NEAR(Position(platform).x, where.x, 1.0e-6F);
}

TEST_F(KinematicBodyTest, TheMoverLeavesTheAuthoredComponentAndBodyContractUntouched) {
    // The mover owns one thing: the body's velocity. The authored target, the easing curve and the
    // switch belong to whoever wrote them - an Inspector session, a script, a save file.
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.25F, 0.5F, 0.75F},
                                                       Kinematic3DComponent{{1.0F, 0.5F, -0.5F}, 0.25F, true},
                                                       /*mass=*/2.5F);
    const Scene::Rigid3DComponent bodyBefore = Body(platform);
    const Scene::ColliderComponent colliderBefore =
        entityManager.GetComponent<Scene::ColliderComponent>(platform);

    for (int step = 0; step < 60; ++step) {
        static_cast<void>(Step(platform));
    }

    const Kinematic3DComponent authored = Kinematic(platform);
    EXPECT_NEAR(authored.targetVelocity.x, 1.0F, 1.0e-6F);
    EXPECT_NEAR(authored.targetVelocity.y, 0.5F, 1.0e-6F);
    EXPECT_NEAR(authored.targetVelocity.z, -0.5F, 1.0e-6F);
    EXPECT_FLOAT_EQ(authored.interpolation, 0.25F);
    EXPECT_TRUE(authored.active);

    const Scene::Rigid3DComponent bodyAfter = Body(platform);
    EXPECT_FLOAT_EQ(bodyAfter.mass, bodyBefore.mass);
    EXPECT_TRUE(bodyAfter.isKinematic);
    EXPECT_FLOAT_EQ(bodyAfter.gravityScale, bodyBefore.gravityScale);
    // Only the velocity moved, and it moved toward the authored target along the authored curve:
    // one second of a quarter-per-second interpolation is a quarter of the way there.
    EXPECT_NEAR(bodyAfter.velocity.x, 0.25F, 2.0e-2F);
    EXPECT_NEAR(bodyAfter.velocity.y, 0.125F, 2.0e-2F);
    EXPECT_NEAR(bodyAfter.velocity.z, -0.125F, 2.0e-2F);

    const Scene::ColliderComponent colliderAfter =
        entityManager.GetComponent<Scene::ColliderComponent>(platform);
    EXPECT_NEAR(colliderAfter.halfExtents.x, colliderBefore.halfExtents.x, 1.0e-6F);
    EXPECT_NEAR(colliderAfter.halfExtents.z, colliderBefore.halfExtents.z, 1.0e-6F);
    EXPECT_EQ(colliderAfter.collisionLayer, colliderBefore.collisionLayer);
    EXPECT_EQ(colliderAfter.collisionMask, colliderBefore.collisionMask);
}

// =================================================================================================
// Other bodies: the crate a platform walks into, and the wall it does not.
// =================================================================================================

TEST_F(KinematicBodyTest, AMovingPlatformShovesTheCrateItWalksInto) {
    // The reason a platform is a *kinematic* body and not a script that writes a transform: the
    // bodies it meets get pushed, and the push goes through the same policy a character's shove
    // does - so a crate cannot tell whether a character or a platform shoved it.
    const Scene::Entity crate = MakeCrate({1.25F, 0.0F, 0.0F});
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, true});

    std::size_t pushedOnSomeStep = 0U;
    KinematicBodyStepResult report{};
    for (int step = 0; step < 240; ++step) {
        report = StepWorld(platform);
        pushedOnSomeStep = std::max(pushedOnSomeStep, report.pushedBodyCount);
    }

    EXPECT_GE(pushedOnSomeStep, 1U) << "the platform has to report the bodies it moved";
    EXPECT_GT(Position(crate).x, 1.5F) << "the crate must have been carried along, not driven through";
    // And the two never end up inside each other.
    EXPECT_LE(Position(platform).x + 0.5F, Position(crate).x - 0.5F + 2.0e-2F);
}

TEST_F(KinematicBodyTest, APlatformNeverShovesItselfOrAStaticObstacle) {
    // The push is for things the simulation may move. An obstacle with no body is not one of them,
    // and the platform is not one of them either - it is the body doing the shoving.
    const Scene::Entity wall = MakeObstacle({1.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    const Scene::Entity platform = MakeKinematic({-3.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, true});

    for (int step = 0; step < 240; ++step) {
        const KinematicBodyStepResult report = StepWorld(platform);
        EXPECT_EQ(report.pushedBodyCount, 0U) << "step " << step;
    }

    EXPECT_NEAR(Position(wall).x, 1.0F, 1.0e-6F);
    EXPECT_LE(Position(platform).x, 0.5F + 1.0e-3F);
}

TEST_F(KinematicBodyTest, APlatformDoesNotPushABodyItIsSwitchedOffBeside) {
    // A stopped body does not shove anything: the crate in front of an idle platform stays exactly
    // where it is, which is what makes "stop the machine" a thing an author can rely on.
    const Scene::Entity crate = MakeCrate({1.25F, 0.0F, 0.0F});
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, false});

    for (int step = 0; step < 120; ++step) {
        const KinematicBodyStepResult report = StepWorld(platform);
        ASSERT_TRUE(report.IsIdle());
    }

    EXPECT_NEAR(Position(crate).x, 1.25F, 1.0e-6F);
    EXPECT_NEAR(Body(crate).velocity.x, 0.0F, 1.0e-6F);
    EXPECT_NEAR(Position(platform).x, 0.0F, 1.0e-6F);
}

TEST_F(KinematicBodyTest, APlatformAuthoredNotToPushLeavesTheCrateWhereItIs) {
    // The policy is the caller's, so a lift that must not disturb the cargo in its path can be
    // authored that way - and the crate stays put even while the platform presses up against it.
    const Scene::Entity crate = MakeCrate({1.25F, 0.0F, 0.0F});
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    KinematicBodyPushPolicy noPush;
    noPush.enabled = false;

    for (int step = 0; step < 240; ++step) {
        physicsSystem.Step(entityManager, sceneGraph, kDeltaTime);
        const KinematicBodyStepResult report =
            StepKinematicBody(entityManager, sceneGraph, collisionSystem, platform, kDeltaTime, noPush);
        EXPECT_EQ(report.pushedBodyCount, 0U) << "step " << step;
    }

    EXPECT_NEAR(Position(crate).x, 1.25F, 1.0e-6F);
    EXPECT_NEAR(Body(crate).velocity.x, 0.0F, 1.0e-6F);
    // The platform still stopped - the crate is a wall to it either way.
    EXPECT_LE(Position(platform).x, 0.75F + 1.0e-3F);
}

// =================================================================================================
// The push policy itself.
// =================================================================================================

TEST(KinematicBodyPushPolicyTest, ThePlatformShoveIsTheCharacterShove) {
    // One policy for "I am moving and something is in my way", used by both, so a crate that a
    // character can nudge is a crate a platform can nudge - and neither can launch it.
    const KinematicBodyPushPolicy policy = DefaultKinematicBodyPushPolicy();
    const CharacterControllerInput characterInput;

    EXPECT_TRUE(policy.enabled);
    EXPECT_FLOAT_EQ(policy.strength, characterInput.dynamicBodyPushStrength);
    EXPECT_FLOAT_EQ(policy.maximumSpeed, characterInput.maximumDynamicBodyPushSpeed);
}

TEST(KinematicBodyStepCodeTest, OnlyAMovedStepClaimsToHaveStepped) {
    KinematicBodyStepResult report;
    report.code = KinematicBodyStepCode::Idle;
    EXPECT_TRUE(report.IsIdle());
    EXPECT_FALSE(report.IsStepped());

    report.code = KinematicBodyStepCode::Moved;
    EXPECT_TRUE(report.IsStepped());
    EXPECT_FALSE(report.IsIdle());
    EXPECT_FALSE(report.PushedAnything());

    report.pushedBodyCount = 1U;
    EXPECT_TRUE(report.PushedAnything());
}

// =================================================================================================
// The engine's own wiring: the fixed step drives Kinematic3D objects, in the order the sync
// declares. The sync itself lives in the engine core; what this pins is the mover it calls, with
// the arrangement the engine uses it in (physics first, then the platform, then whatever rides it).
// =================================================================================================

TEST_F(KinematicBodyTest, TheFixedStepArrangementCarriesARiderTheSameStepThePlatformMoves) {
    // A character standing on a platform reads how far its platform moved since the last step. That
    // is only true if the platform's move for this step is already in the world when the character
    // asks - which is why the engine runs the kinematic sync before the character step, and why
    // this test runs them in that order, out of one physics step.
    const Scene::Entity platform = MakeKinematic({0.0F, 0.0F, 0.0F}, {2.0F, 0.5F, 2.0F},
                                                       Kinematic3DComponent{{1.0F, 0.0F, 0.0F}, 1.0F, true});
    const Scene::Entity rider = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, rider, Scene::TransformComponent{});
    Scene::ColliderComponent riderCollider{Math::Vector3{0.4F, 0.4F, 0.4F}};
    entityManager.AddComponent<Scene::ColliderComponent>(rider, riderCollider);
    entityManager.AddComponent<Scene::CharacterControllerComponent>(
        rider, Scene::CharacterControllerComponent{});
    Place(rider, {0.0F, 0.9F, 0.0F});

    // Ride for a moment so the body is genuinely standing on the platform it will be carried by.
    // The engine's arrangement, one step: physics, the platform, then the character.
    const auto stepWorld = [&]() {
        physicsSystem.Step(entityManager, sceneGraph, kDeltaTime);
        const KinematicBodyStepResult platformReport = Step(platform);
        const Character3DStepResult riderReport = StepCharacter3D(
            entityManager, sceneGraph, collisionSystem, rider, CharacterMotionInput{},
            /*gravityY=*/-9.81F, kDeltaTime);
        return std::pair{platformReport, riderReport};
    };

    for (int step = 0; step < 10; ++step) {
        const auto [platformReport, riderReport] = stepWorld();
        ASSERT_TRUE(platformReport.IsStepped());
        ASSERT_TRUE(riderReport.stepped) << "step " << step;
    }
    const float startX = Position(rider).x;

    bool carried = false;
    Math::Vector3 platformVelocity{};
    for (int step = 0; step < 30; ++step) {
        const auto [platformReport, riderReport] = stepWorld();
        ASSERT_TRUE(platformReport.IsStepped());
        ASSERT_TRUE(riderReport.stepped) << "step " << step;
        carried = carried || riderReport.motion.carriedByPlatform;
        platformVelocity = platformReport.velocity;
    }

    // Half a second at one metre per second: the rider went with the platform, not left behind.
    EXPECT_TRUE(carried) << "the platform's own move is what the rider reads";
    EXPECT_NEAR(Position(rider).x - startX, 0.5F, 5.0e-2F);
    EXPECT_NEAR(platformVelocity.x, 1.0F, 1.0e-3F);
}

} // namespace
} // namespace UVE::Physics::Tests
