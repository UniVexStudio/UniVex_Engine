// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "collider_debug_geometry.h"
#include "quaternion.h"
#include "vector3.h"
#include "ragdoll_solver.h"
#include "ragdoll_world.h"

namespace UVE::Physics::Tests {
namespace {

constexpr float kStep = kRagdollFixedStepSeconds;

[[nodiscard]] RagdollBody MakeCapsule(const Math::Vector3 position, const float mass = 2.0F,
                                           const float radius = 0.1F, const float halfHeight = 0.2F) {
    RagdollBody body;
    body.mass = mass;
    body.radius = radius;
    body.halfHeight = halfHeight;
    // Zero damping, so an analytic check of gravity is a check of the integrator only.
    body.linearDamping = 0.0F;
    body.angularDamping = 0.0F;
    body.position = position;
    return body;
}

/// A floor at y = 0 that the capsule cannot pass: the depth is recomputed from the body's own
/// position each step, which is what the world layer does for static geometry.
[[nodiscard]] float FloorDepth(const RagdollBody& body) {
    const float bottom = body.position.y - (body.halfHeight + body.radius);
    return std::max(0.0F, -bottom);
}

struct TwoBodyRig final {
    RagdollSolver solver;
    std::uint32_t first = 0U;
    std::uint32_t second = 0U;
};

/// A parent capsule with a child jointed to its top end.
[[nodiscard]] TwoBodyRig MakeTwoBodyRig(const bool hinge) {
    TwoBodyRig rig;
    RagdollBody parent = MakeCapsule(Math::Vector3{0.0F, 1.0F, 0.0F});
    RagdollBody child = MakeCapsule(Math::Vector3{0.0F, 1.6F, 0.0F});
    std::vector<RagdollBody> bodies{parent, child};
    EXPECT_TRUE(rig.solver.Reset(bodies));

    RagdollJoint joint;
    if (hinge) {
        joint = MakeHingeJoint(0U, 1U, Math::Vector3{0.0F, 0.2F, 0.0F},
                                  Math::Vector3{0.0F, -0.4F, 0.0F}, /*twistLimitDegrees=*/150.0F);
        joint.twistAsymmetric = true;
        joint.twistLowerLimitDegrees = -5.0F;
        joint.twistUpperLimitDegrees = 150.0F;
    } else {
        joint = MakeBallSocketJoint(0U, 1U, Math::Vector3{0.0F, 0.2F, 0.0F},
                                       Math::Vector3{0.0F, -0.4F, 0.0F}, /*swingLimitDegrees=*/60.0F);
    }
    EXPECT_TRUE(rig.solver.AddJoint(joint));
    rig.first = 0U;
    rig.second = 1U;
    return rig;
}

// ---- Shape-derived inertia -------------------------------------------------------------------

TEST(RagdollSolverTest, CapsuleInverseInertia_MatchesASolidCylinder) {
    // A 2 kg capsule, 10 cm radius, 40 cm of cylinder: 60 cm of capsule once both caps count.
    // About its own axis a solid cylinder is 1/2 m r^2 = 0.01 kg m^2; across it,
    // m (3 r^2 + L^2) / 12 = 2 (0.03 + 0.36) / 12 = 0.065 kg m^2.
    const Math::Vector3 inverseInertia = ComputeCapsuleInverseInertia(2.0F, 0.1F, 0.2F);
    EXPECT_NEAR(inverseInertia.y, 100.0F, 1.0e-2F);
    EXPECT_NEAR(inverseInertia.x, 15.3846F, 1.0e-2F);
    EXPECT_FLOAT_EQ(inverseInertia.x, inverseInertia.z);
}

TEST(RagdollSolverTest, CapsuleInverseInertia_IsZeroForAnImmovableBody) {
    const Math::Vector3 zero = ComputeCapsuleInverseInertia(0.0F, 0.1F, 0.2F);
    EXPECT_FLOAT_EQ(zero.x, 0.0F);
    EXPECT_FLOAT_EQ(zero.y, 0.0F);
    EXPECT_FLOAT_EQ(zero.z, 0.0F);
    EXPECT_FLOAT_EQ(ComputeCapsuleInverseInertia(-1.0F, 0.1F, 0.2F).y, 0.0F);
    EXPECT_FLOAT_EQ(ComputeCapsuleInverseInertia(2.0F, 0.0F, 0.2F).y, 0.0F);

    // No barrel means a sphere, not an immovable body: 2/5 m r^2 = 0.002 kg m^2 on all three axes.
    const Math::Vector3 sphere = ComputeCapsuleInverseInertia(1.0F, 0.1F, 0.0F);
    EXPECT_NEAR(sphere.y, 250.0F, 1.0e-2F);
    EXPECT_FLOAT_EQ(sphere.x, sphere.y);
}

TEST(RagdollSolverTest, Reset_SeedsMassAndInertiaFromTheShape) {
    RagdollSolver solver;
    std::vector<RagdollBody> bodies{MakeCapsule({}, 2.0F, 0.1F, 0.2F)};
    ASSERT_TRUE(solver.Reset(bodies));
    ASSERT_EQ(solver.GetBodyCount(), 1U);
    EXPECT_FLOAT_EQ(solver.GetBody(0).inverseMass, 0.5F);
    EXPECT_NEAR(solver.GetBody(0).inverseInertiaLocal.y, 100.0F, 1.0e-2F);
    EXPECT_FLOAT_EQ(solver.GetTotalMass(), 2.0F);
}

TEST(RagdollSolverTest, Reset_RefusesMoreBodiesThanTheCap) {
    RagdollSolver solver;
    std::vector<RagdollBody> bodies(kMaximumRagdollBodies + 1U, MakeCapsule({}));
    EXPECT_FALSE(solver.Reset(bodies));
    EXPECT_EQ(solver.GetBodyCount(), 0U);
    EXPECT_FLOAT_EQ(solver.GetTotalMass(), 0.0F);
}

TEST(RagdollSolverTest, AddJoint_RefusesDegenerateOrOutOfRangeJoints) {
    RagdollSolver solver;
    std::vector<RagdollBody> bodies(2U, MakeCapsule({}));
    ASSERT_TRUE(solver.Reset(bodies));

    EXPECT_FALSE(solver.AddJoint(MakeBallSocketJoint(0U, 0U, {}, {}))) << "a joint to itself";
    EXPECT_FALSE(solver.AddJoint(MakeBallSocketJoint(0U, 2U, {}, {}))) << "body out of range";
    EXPECT_EQ(solver.GetJointCount(), 0U);
    EXPECT_TRUE(solver.AddJoint(MakeBallSocketJoint(0U, 1U, {}, {})));
    EXPECT_EQ(solver.GetJointCount(), 1U);

    // The table is a bound, not a suggestion: the 25th joint is refused and the count stays put.
    RagdollSolver full;
    std::vector<RagdollBody> many(kMaximumRagdollBodies, MakeCapsule({}));
    ASSERT_TRUE(full.Reset(many));
    for (std::size_t index = 0U; index < kMaximumRagdollJoints; ++index) {
        ASSERT_TRUE(full.AddJoint(MakeBallSocketJoint(0U, 1U, {}, {})));
    }
    EXPECT_FALSE(full.AddJoint(MakeBallSocketJoint(0U, 1U, {}, {})));
    EXPECT_EQ(full.GetJointCount(), kMaximumRagdollJoints);
}

// ---- Integration, determinism, contacts -------------------------------------------------------

TEST(RagdollSolverTest, Step_FallsExactlyLikeGravityOverOneSecond) {
    RagdollSolver solver;
    std::vector<RagdollBody> bodies{MakeCapsule({0.0F, 10.0F, 0.0F})};
    ASSERT_TRUE(solver.Reset(bodies));

    const Math::Vector3 gravity{0.0F, -9.81F, 0.0F};
    RagdollStepReport report;
    for (int step = 0; step < 240; ++step) {
        report = solver.Step(gravity, kStep, {});
    }
    EXPECT_TRUE(report.stepped);
    EXPECT_NEAR(solver.GetBody(0).linearVelocity.y, -9.81F, 1.0e-3F);
    // Semi-implicit Euler: the position lags half a step behind the exact -4.905 m, which is the
    // reason a ragdoll drops a few millimetres differently than a textbook parabola over one second.
    EXPECT_NEAR(solver.GetBody(0).position.y, 10.0F - 4.925F, 1.0e-2F);
    EXPECT_FLOAT_EQ(solver.GetBody(0).position.x, 0.0F);
}

TEST(RagdollSolverTest, Step_RefusesNonFiniteTimeAndGravity) {
    RagdollSolver solver;
    std::vector<RagdollBody> bodies{MakeCapsule({})};
    ASSERT_TRUE(solver.Reset(bodies));

    EXPECT_FALSE(solver.Step({0.0F, -9.81F, 0.0F}, std::numeric_limits<float>::quiet_NaN(), {}).stepped);
    EXPECT_FALSE(solver.Step({0.0F, -9.81F, 0.0F}, 0.0F, {}).stepped);
    EXPECT_FALSE(solver.Step({0.0F, -9.81F, 0.0F}, -0.01F, {}).stepped);
    EXPECT_FALSE(solver.Step({std::numeric_limits<float>::infinity(), 0.0F, 0.0F}, kStep, {}).stepped);
    EXPECT_FLOAT_EQ(solver.GetBody(0).position.y, 0.0F);
}

TEST(RagdollSolverTest, Step_ClampsAHugeStepInsteadOfTunneling) {
    RagdollSolver solver;
    std::vector<RagdollBody> bodies{MakeCapsule({0.0F, 10.0F, 0.0F})};
    ASSERT_TRUE(solver.Reset(bodies));
    // 10 seconds in one call is a bug upstream; the solver answers with its 0.1 s ceiling rather
    // than integrating a 10 s drop in one step.
    const RagdollStepReport report = solver.Step({0.0F, -10.0F, 0.0F}, 10.0F, {});
    EXPECT_TRUE(report.stepped);
    EXPECT_NEAR(solver.GetBody(0).linearVelocity.y, -1.0F, 1.0e-4F);
    EXPECT_GT(solver.GetBody(0).position.y, 9.0F);
}

TEST(RagdollSolverTest, Step_IsBitwiseDeterministic) {
    const auto simulate = [](RagdollSolver& solver) {
        std::vector<RagdollBody> bodies{MakeCapsule({0.0F, 2.0F, 0.0F}), MakeCapsule({0.4F, 2.0F, 0.1F})};
        EXPECT_TRUE(solver.Reset(bodies));
        EXPECT_TRUE(solver.AddJoint(MakeBallSocketJoint(0U, 1U, {}, {})));
        solver.ApplyImpulse(0U, Math::Vector3{3.0F, 0.0F, 1.0F}, {0.0F, 2.0F, 0.0F});
        solver.ApplyImpulse(1U, Math::Vector3{-2.0F, 0.0F, 0.5F}, {0.4F, 2.0F, 0.1F});
        for (int step = 0; step < 600; ++step) {
            static_cast<void>(solver.Step({0.0F, -9.81F, 0.0F}, kStep, {}));
        }
    };

    RagdollSolver first;
    RagdollSolver second;
    simulate(first);
    simulate(second);
    ASSERT_EQ(first.GetBodyCount(), second.GetBodyCount());
    for (std::size_t body = 0U; body < first.GetBodyCount(); ++body) {
        const RagdollBody& lhs = first.GetBody(body);
        const RagdollBody& rhs = second.GetBody(body);
        EXPECT_FLOAT_EQ(lhs.position.x, rhs.position.x);
        EXPECT_FLOAT_EQ(lhs.position.y, rhs.position.y);
        EXPECT_FLOAT_EQ(lhs.position.z, rhs.position.z);
        EXPECT_FLOAT_EQ(lhs.rotation.x, rhs.rotation.x);
        EXPECT_FLOAT_EQ(lhs.rotation.w, rhs.rotation.w);
        EXPECT_FLOAT_EQ(lhs.linearVelocity.y, rhs.linearVelocity.y);
        EXPECT_FLOAT_EQ(lhs.angularVelocity.x, rhs.angularVelocity.x);
    }
}

TEST(RagdollSolverTest, Step_RestsOnAFloorWithoutSinkingOrJittering) {
    RagdollSolver solver;
    std::vector<RagdollBody> bodies{MakeCapsule({0.0F, 0.5F, 0.0F})};
    bodies[0].linearDamping = 0.05F;
    ASSERT_TRUE(solver.Reset(bodies));

    const float restingHeight = bodies[0].halfHeight + bodies[0].radius;
    float maximumHeight = -std::numeric_limits<float>::infinity();
    float minimumHeight = std::numeric_limits<float>::infinity();
    for (int step = 0; step < 480; ++step) {
        std::vector<RagdollContact> contacts;
        const float depth = FloorDepth(solver.GetBody(0));
        if (depth > 0.0F) contacts.push_back(RagdollContact{0U, {}, {0.0F, 1.0F, 0.0F}, depth, 0.8F, 0.0F});
        static_cast<void>(solver.Step({0.0F, -9.81F, 0.0F}, kStep, contacts));
        if (step > 120) {
            maximumHeight = std::max(maximumHeight, solver.GetBody(0).position.y);
            minimumHeight = std::min(minimumHeight, solver.GetBody(0).position.y);
        }
    }
    // It stops at its own surface, not inside the floor and not hovering above it.
    EXPECT_NEAR(solver.GetBody(0).position.y, restingHeight, 0.01F);
    EXPECT_LT(maximumHeight - minimumHeight, 0.01F) << "a resting ragdoll should be still";
    EXPECT_LT(std::fabs(solver.GetBody(0).linearVelocity.y), 0.1F);
}

TEST(RagdollSolverTest, Step_KeepsAJointedChainTogether) {
    TwoBodyRig rig = MakeTwoBodyRig(/*hinge=*/false);
    RagdollStepReport report;
    for (int step = 0; step < 480; ++step) {
        report = rig.solver.Step({0.0F, -9.81F, 0.0F}, kStep, {});
    }
    EXPECT_TRUE(report.stepped);
    EXPECT_EQ(report.constrainedJointCount, 1U);
    EXPECT_LT(report.maximumJointDrift, 0.01F);
    EXPECT_EQ(report.jointDriftCount, 0U);
}

TEST(RagdollSolverTest, Step_StopsAHingeAtItsAuthoredTwistLimit) {
    TwoBodyRig rig = MakeTwoBodyRig(/*hinge=*/true);
    // Spin the child hard about the hinge axis, backwards past the -5 degree limit.
    const Math::Vector3 hingeAxis{1.0F, 0.0F, 0.0F};
    rig.solver.GetBody(1).angularVelocity = hingeAxis * -40.0F;
    for (int step = 0; step < 480; ++step) {
        static_cast<void>(rig.solver.Step({}, kStep, {}));
    }

    const RagdollJoint& joint = rig.solver.GetJoint(0);
    const Math::Quaternion firstFrame =
        Math::Multiply(rig.solver.GetBody(joint.firstBody).rotation, joint.firstFrame);
    const Math::Quaternion secondFrame =
        Math::Multiply(rig.solver.GetBody(joint.secondBody).rotation, joint.secondFrame);
    Math::Quaternion inverseFirst;
    ASSERT_TRUE(Math::TryInverse(firstFrame, inverseFirst));
    const Math::Quaternion relative = Math::Multiply(inverseFirst, secondFrame);
    const float twistRadians = 2.0F * std::atan2(relative.x, relative.w);
    const float lowerRadians = joint.twistLowerLimitDegrees * 3.14159265F / 180.0F;
    // Within a tenth of a radian of the authored stop: a limit that is respected in the limit, not
    // in the asymptote.
    EXPECT_GE(twistRadians, lowerRadians - 0.1F) << "a shin must not fold through the thigh";
    EXPECT_LT(twistRadians, 0.1F) << "the hinge only bends one way";
}

// ---- Impulses and sleeping --------------------------------------------------------------------

TEST(RagdollSolverTest, ApplyImpulse_SpinsTheBodyFromAnOffCentreHit) {
    RagdollSolver solver;
    std::vector<RagdollBody> bodies{MakeCapsule({0.0F, 1.0F, 0.0F})};
    ASSERT_TRUE(solver.Reset(bodies));

    solver.ApplyImpulse(0U, Math::Vector3{10.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.2F, 0.0F});
    // 10 N.s on 2 kg is 5 m/s; the hit lands 0.2 m above the centre of mass, so it also turns.
    EXPECT_NEAR(solver.GetBody(0).linearVelocity.x, 5.0F, 1.0e-4F);
    EXPECT_GT(std::fabs(solver.GetBody(0).angularVelocity.z), 0.1F);
    EXPECT_TRUE(std::isfinite(solver.GetBody(0).angularVelocity.z));
}

TEST(RagdollSolverTest, ApplyImpulse_WakesASleepingRagdollAndIgnoresBadInput) {
    RagdollSolver solver;
    std::vector<RagdollBody> bodies{MakeCapsule({})};
    ASSERT_TRUE(solver.Reset(bodies));
    solver.Sleep();
    ASSERT_TRUE(solver.IsSleeping());

    solver.ApplyImpulse(0U, Math::Vector3{1.0F, 0.0F, 0.0F}, {});
    EXPECT_FALSE(solver.IsSleeping()) << "a hit that did not wake the body would do nothing";

    const Math::Vector3 before = solver.GetBody(0).linearVelocity;
    solver.ApplyImpulse(99U, Math::Vector3{1.0F, 0.0F, 0.0F}, {});
    solver.ApplyImpulse(0U, Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, {});
    EXPECT_FLOAT_EQ(solver.GetBody(0).linearVelocity.x, before.x);
    EXPECT_FLOAT_EQ(solver.GetBody(0).linearVelocity.y, before.y);
}

TEST(RagdollSolverTest, Sleep_StopsSteppingUntilWoken) {
    RagdollSolver solver;
    std::vector<RagdollBody> bodies{MakeCapsule({0.0F, 1.0F, 0.0F})};
    ASSERT_TRUE(solver.Reset(bodies));
    solver.Sleep();

    const Math::Vector3 position = solver.GetBody(0).position;
    const RagdollStepReport report = solver.Step({0.0F, -9.81F, 0.0F}, kStep, {});
    EXPECT_FALSE(report.stepped);
    EXPECT_TRUE(report.sleeping);
    EXPECT_FLOAT_EQ(solver.GetBody(0).position.y, position.y) << "a sleeping body does not fall";

    solver.Wake();
    EXPECT_TRUE(solver.Step({0.0F, -9.81F, 0.0F}, kStep, {}).stepped);
}

TEST(RagdollSolverTest, Step_PutsARestingBodyToSleep) {
    RagdollSolver solver;
    std::vector<RagdollBody> bodies{MakeCapsule({0.0F, 1.0F, 0.0F})};
    ASSERT_TRUE(solver.Reset(bodies));

    RagdollStepSettings settings;
    settings.sleepDelaySeconds = 0.1F;
    RagdollStepReport report;
    for (int step = 0; step < 120; ++step) {
        report = solver.Step({}, kStep, {}, settings);
    }
    EXPECT_TRUE(solver.IsSleeping());
    EXPECT_TRUE(report.sleeping);
    EXPECT_FALSE(solver.Step({}, kStep, {}, settings).stepped);
}

// ---- Joint frames and builders ----------------------------------------------------------------

TEST(RagdollSolverTest, MakeJointFrameFromTwistAxis_MapsXOntoTheAxis) {
    const Math::Vector3 axis{0.0F, 1.0F, 0.0F};
    const Math::Quaternion frame = MakeJointFrameFromTwistAxis(axis);
    const Math::Vector3 mapped = Math::RotateVector(frame, Math::Vector3{1.0F, 0.0F, 0.0F});
    EXPECT_NEAR(mapped.x, axis.x, 1.0e-5F);
    EXPECT_NEAR(mapped.y, axis.y, 1.0e-5F);
    EXPECT_NEAR(mapped.z, axis.z, 1.0e-5F);

    // Degenerate input answers the identity rather than a NaN rotation.
    EXPECT_FLOAT_EQ(MakeJointFrameFromTwistAxis({}).w, 1.0F);
    EXPECT_FLOAT_EQ(MakeJointFrameFromTwistAxis({std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}).w,
                    1.0F);
}

TEST(RagdollSolverTest, ComputeJointSecondFrame_LeavesTheBindPoseAtZeroDeviation) {
    const Math::Quaternion firstBind = MakeJointFrameFromTwistAxis({0.0F, 1.0F, 0.0F});
    const Math::Quaternion secondBind = MakeJointFrameFromTwistAxis({1.0F, 0.0F, 0.0F});
    const Math::Quaternion secondFrame = ComputeJointSecondFrame(firstBind, secondBind);
    const Math::Quaternion firstFrameAtBind = Math::Multiply(firstBind, Math::Quaternion{});
    const Math::Quaternion secondFrameAtBind = Math::Multiply(secondBind, secondFrame);
    EXPECT_NEAR(firstFrameAtBind.x, secondFrameAtBind.x, 1.0e-5F);
    EXPECT_NEAR(firstFrameAtBind.y, secondFrameAtBind.y, 1.0e-5F);
    EXPECT_NEAR(firstFrameAtBind.z, secondFrameAtBind.z, 1.0e-5F);
    EXPECT_NEAR(firstFrameAtBind.w, secondFrameAtBind.w, 1.0e-5F);
}

TEST(RagdollSolverTest, MakeHingeJoint_LocksBothSwingsAndLimitsTheTwist) {
    const RagdollJoint hinge = MakeHingeJoint(0U, 1U, {}, {}, 70.0F);
    EXPECT_EQ(hinge.swing1Motion, RagdollMotion::Locked);
    EXPECT_EQ(hinge.swing2Motion, RagdollMotion::Locked);
    EXPECT_EQ(hinge.twistMotion, RagdollMotion::Limited);
    EXPECT_FLOAT_EQ(hinge.twistLimitDegrees, 70.0F);

    const RagdollJoint socket = MakeBallSocketJoint(0U, 1U, {}, {}, 60.0F);
    EXPECT_EQ(socket.swing1Motion, RagdollMotion::Limited);
    EXPECT_EQ(socket.swing2Motion, RagdollMotion::Limited);
    EXPECT_FLOAT_EQ(socket.swing1LimitDegrees, 60.0F);
    EXPECT_FALSE(socket.twistAsymmetric) << "a ball socket has no one-way range";
}

// ---- AAA Solver Upgrades (Inertia Conditioning, Self-Collision, PD Motors, Exact Contacts, Debug) ----

TEST(RagdollSolverTest, StabilizeInertia_BoundsPrincipalAndChainInertiaRatios) {
    RagdollSolver solver;
    RagdollBody pelvis = MakeCapsule({0.0F, 1.0F, 0.0F}, 15.0F, 0.14F, 0.10F);
    RagdollBody hand = MakeCapsule({0.0F, 1.4F, 0.0F}, 0.4F, 0.03F, 0.05F);
    ASSERT_TRUE(solver.Reset(std::vector<RagdollBody>{pelvis, hand}));
    ASSERT_TRUE(solver.AddJoint(MakeBallSocketJoint(0U, 1U, {0.0F, 0.15F, 0.0F}, {0.0F, -0.25F, 0.0F}, 60.0F)));

    solver.StabilizeInertia(4.0F, 16.0F);
    const Math::Vector3 invPelvis = solver.GetBody(0U).inverseInertiaLocal;
    const Math::Vector3 invHand = solver.GetBody(1U).inverseInertiaLocal;
    const float pelvisMax = std::max({invPelvis.x, invPelvis.y, invPelvis.z});
    const float handMax = std::max({invHand.x, invHand.y, invHand.z});
    const float handMin = std::min({invHand.x, invHand.y, invHand.z});
    EXPECT_LE(handMax / handMin, 4.01F);
    EXPECT_LE(handMax / pelvisMax, 16.01F);

    // High-speed off-center impact on extreme mass-ratio chain remains finite and tightly coupled.
    solver.ApplyImpulse(1U, {25.0F, 0.0F, 0.0F}, {0.0F, 1.45F, 0.0F});
    RagdollStepReport report;
    for (int step = 0; step < 240; ++step) {
        report = solver.Step({0.0F, -9.81F, 0.0F}, kStep, {});
    }
    EXPECT_TRUE(Math::IsFinite(solver.GetBody(1U).position));
    EXPECT_TRUE(Math::IsFinite(solver.GetBody(1U).angularVelocity));
    EXPECT_LT(report.maximumJointDrift, 0.02F);
}

TEST(RagdollSolverTest, OneSidedJointLimit_DoesNotStickWhenMovingAwayFromLimit) {
    TwoBodyRig rig = MakeTwoBodyRig(/*hinge=*/true);
    // Place child slightly below the -5 deg lower limit, but give it POSITIVE velocity moving BACK
    // into the allowed range (towards +10 deg). A sticky two-sided limit would pull it back!
    Math::Quaternion rotated{};
    ASSERT_TRUE(Math::TryMakeAxisAngle({1.0F, 0.0F, 0.0F}, -8.0F * 3.14159265F / 180.0F, rotated));
    rig.solver.GetBody(1U).rotation = rotated;
    rig.solver.GetBody(1U).angularVelocity = {5.0F, 0.0F, 0.0F};

    RagdollStepSettings settings;
    settings.positionIterations = 0U;
    settings.sleepDelaySeconds = 0.0F;
    static_cast<void>(rig.solver.Step({}, kStep, {}, settings));
    // Child velocity moving away from the lower limit must remain strongly positive after linear
    // anchor coupling (not reversed or clamped to zero by a sticky bilateral limit!).
    EXPECT_GT(rig.solver.GetBody(1U).angularVelocity.x, 2.0F);
    EXPECT_GT(rig.solver.GetBody(1U).angularVelocity.x - rig.solver.GetBody(0U).angularVelocity.x, 3.0F);
}

TEST(RagdollSolverTest, SelfCollision_SeparatesNonAdjacentBodiesWhileIgnoringParentChild) {
    RagdollSolver solver;
    RagdollBody body0 = MakeCapsule({0.0F, 1.0F, 0.0F}, 5.0F, 0.12F, 0.2F);
    RagdollBody body1 = MakeCapsule({0.0F, 1.5F, 0.0F}, 2.0F, 0.08F, 0.15F);
    // Body 2 is non-adjacent (not jointed to body 0) and initially separated at bind, then pushed into body 0.
    RagdollBody body2 = MakeCapsule({0.5F, 1.0F, 0.0F}, 2.0F, 0.10F, 0.2F);
    ASSERT_TRUE(solver.Reset(std::vector<RagdollBody>{body0, body1, body2}));
    ASSERT_TRUE(solver.AddJoint(MakeBallSocketJoint(0U, 1U, {0.0F, 0.25F, 0.0F}, {0.0F, -0.25F, 0.0F})));
    solver.RebuildSelfCollisionFilter(/*disableCurrentPoseOverlaps=*/true, /*minimumJointHops=*/2U);

    EXPECT_FALSE(solver.CanSelfCollide(0U, 1U)) << "parent-child pair must be filtered";
    EXPECT_TRUE(solver.CanSelfCollide(0U, 2U)) << "non-adjacent separated pair must collide";

    // Drive body 2 straight into body 0.
    solver.GetBody(2U).position = {0.10F, 1.0F, 0.0F};
    solver.GetBody(2U).linearVelocity = {-4.0F, 0.0F, 0.0F};

    RagdollStepSettings settings;
    settings.enableSelfCollision = true;
    settings.sleepDelaySeconds = 0.0F;
    RagdollStepReport report;
    for (int step = 0; step < 60; ++step) {
        report = solver.Step({}, kStep, {}, settings);
    }
    const float separation = std::fabs(solver.GetBody(2U).position.x - solver.GetBody(0U).position.x);
    EXPECT_GE(separation, body0.radius + body2.radius - 0.02F)
        << "self-collision must push non-adjacent capsules apart";
}

TEST(RagdollSolverTest, ActiveJointMotor_DrivesLimbToTargetAngleAndStaysStableAtHighStiffness) {
    TwoBodyRig rig = MakeTwoBodyRig(/*hinge=*/true);
    rig.solver.GetBody(0U).kinematic = true; // Anchor parent like a partial ragdoll root!

    Math::Quaternion targetRel{};
    ASSERT_TRUE(Math::TryMakeAxisAngle({1.0F, 0.0F, 0.0F}, 60.0F * 3.14159265F / 180.0F, targetRel));
    RagdollJoint& joint = rig.solver.GetJoint(0U);
    joint.motor.enabled = true;
    joint.motor.targetRelativeRotation = targetRel;
    joint.motor.angularStiffness = 5000.0F; // Extreme stiffness must stay unconditionally stable!
    joint.motor.angularDamping = 60.0F;
    joint.motor.maxTorque = 250.0F;

    RagdollStepSettings settings;
    settings.sleepDelaySeconds = 0.0F;
    for (int step = 0; step < 240; ++step) {
        static_cast<void>(rig.solver.Step({0.0F, -9.81F, 0.0F}, kStep, {}, settings));
    }

    const Math::Quaternion rel = Math::Multiply(
        Math::Conjugate(Math::Multiply(rig.solver.GetBody(0U).rotation, joint.firstFrame)),
        Math::Multiply(rig.solver.GetBody(1U).rotation, joint.secondFrame));
    const float twistDeg = (2.0F * std::atan2(rel.x, rel.w)) * 180.0F / 3.14159265F;
    EXPECT_TRUE(Math::IsFinite(rig.solver.GetBody(1U).rotation));
    EXPECT_NEAR(twistDeg, 60.0F, 8.0F) << "PD motor must drive limb to 60 deg target against gravity";
}

TEST(RagdollSolverTest, ExactBoxContactPoint_ProducesZeroSpuriousTorqueFarFromOrigin) {
    // Regression test for Bug #4: a limb standing at x = +25 m on a 100 m floor box centered at
    // (0, -0.5, 0) used to pick the endpoint closest to (0, -0.5, 0) instead of the floor surface!
    RagdollSolver solver;
    RagdollBody limb = MakeCapsule({25.0F, 0.35F, 0.0F}, 3.0F, 0.1F, 0.2F);
    limb.linearDamping = 0.05F;
    limb.angularDamping = 0.2F;
    ASSERT_TRUE(solver.Reset(std::vector<RagdollBody>{limb}));

    Detail::ColliderWorldAabb floorCollider{};
    floorCollider.entity = Scene::Entity{99U, 1U};
    floorCollider.collisionLayer = 1U;
    floorCollider.collisionMask = 0xFFFFFFFFU;
    floorCollider.shapeType = Scene::ColliderShapeType::Box;
    floorCollider.shapeHalfExtents = {50.0F, 0.5F, 50.0F};
    floorCollider.shapeRotation = Math::Quaternion{};
    floorCollider.worldAabb = Math::Aabb{{-50.0F, -1.0F, -50.0F}, {50.0F, 0.0F, 50.0F}};

    const std::array<Detail::ColliderWorldAabb, 1U> colliders{floorCollider};
    RagdollCollisionFilter filter{};
    std::array<RagdollContact, 16U> contacts{};
    for (int step = 0; step < 240; ++step) {
        const RagdollContactStats stats =
            BuildRagdollContacts(solver, colliders, filter, std::span<RagdollContact>{contacts});
        static_cast<void>(solver.Step(
            {0.0F, -9.81F, 0.0F}, kStep,
            std::span<const RagdollContact>{contacts.data(), stats.generatedContactCount}));
    }
    EXPECT_NEAR(solver.GetBody(0U).position.y, 0.3F, 0.015F);
    EXPECT_LT(std::sqrt(Math::LengthSquared(solver.GetBody(0U).angularVelocity)), 0.05F)
        << "standing vertically at x=+25m on a centered floor box must generate zero fake origin torque";
}

TEST(RagdollSolverTest, BuildRagdollDebugLines_EmitsFiniteCapsulesAndLimitCones) {
    TwoBodyRig hingeRig = MakeTwoBodyRig(/*hinge=*/true);
    std::array<ColliderDebugLine, 512U> buffer{};
    const ColliderDebugBuildResult res =
        BuildRagdollDebugLines(hingeRig.solver, std::span<ColliderDebugLine>{buffer});
    EXPECT_GT(res.lineCount, 40U);
    EXPECT_FALSE(res.truncated);
    for (std::size_t i = 0U; i < res.lineCount; ++i) {
        EXPECT_TRUE(Math::IsFinite(buffer[i].from));
        EXPECT_TRUE(Math::IsFinite(buffer[i].to));
    }

    // Small buffer degrades cleanly via truncation without writing out of bounds.
    std::array<ColliderDebugLine, 8U> tiny{};
    const ColliderDebugBuildResult trunc =
        BuildRagdollDebugLines(hingeRig.solver, std::span<ColliderDebugLine>{tiny});
    EXPECT_EQ(trunc.lineCount, 8U);
    EXPECT_TRUE(trunc.truncated);
}

} // namespace
} // namespace UVE::Physics::Tests
