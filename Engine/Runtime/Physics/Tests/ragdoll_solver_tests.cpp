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

constexpr float kStepUVE = kRagdollFixedStepSecondsUVE;

[[nodiscard]] RagdollBodyUVE MakeCapsuleUVE(const Math::Vector3UVE position, const float mass = 2.0F,
                                           const float radius = 0.1F, const float halfHeight = 0.2F) {
    RagdollBodyUVE body;
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
[[nodiscard]] float FloorDepthUVE(const RagdollBodyUVE& body) {
    const float bottom = body.position.y - (body.halfHeight + body.radius);
    return std::max(0.0F, -bottom);
}

struct TwoBodyRigUVE final {
    RagdollSolverUVE solver;
    std::uint32_t first = 0U;
    std::uint32_t second = 0U;
};

/// A parent capsule with a child jointed to its top end.
[[nodiscard]] TwoBodyRigUVE MakeTwoBodyRigUVE(const bool hinge) {
    TwoBodyRigUVE rig;
    RagdollBodyUVE parent = MakeCapsuleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    RagdollBodyUVE child = MakeCapsuleUVE(Math::Vector3UVE{0.0F, 1.6F, 0.0F});
    std::vector<RagdollBodyUVE> bodies{parent, child};
    EXPECT_TRUE(rig.solver.ResetUVE(bodies));

    RagdollJointUVE joint;
    if (hinge) {
        joint = MakeHingeJointUVE(0U, 1U, Math::Vector3UVE{0.0F, 0.2F, 0.0F},
                                  Math::Vector3UVE{0.0F, -0.4F, 0.0F}, /*twistLimitDegrees=*/150.0F);
        joint.twistAsymmetric = true;
        joint.twistLowerLimitDegrees = -5.0F;
        joint.twistUpperLimitDegrees = 150.0F;
    } else {
        joint = MakeBallSocketJointUVE(0U, 1U, Math::Vector3UVE{0.0F, 0.2F, 0.0F},
                                       Math::Vector3UVE{0.0F, -0.4F, 0.0F}, /*swingLimitDegrees=*/60.0F);
    }
    EXPECT_TRUE(rig.solver.AddJointUVE(joint));
    rig.first = 0U;
    rig.second = 1U;
    return rig;
}

// ---- Shape-derived inertia -------------------------------------------------------------------

TEST(RagdollSolverUVETest, CapsuleInverseInertiaUVE_MatchesASolidCylinder) {
    // A 2 kg capsule, 10 cm radius, 40 cm of cylinder: 60 cm of capsule once both caps count.
    // About its own axis a solid cylinder is 1/2 m r^2 = 0.01 kg m^2; across it,
    // m (3 r^2 + L^2) / 12 = 2 (0.03 + 0.36) / 12 = 0.065 kg m^2.
    const Math::Vector3UVE inverseInertia = ComputeCapsuleInverseInertiaUVE(2.0F, 0.1F, 0.2F);
    EXPECT_NEAR(inverseInertia.y, 100.0F, 1.0e-2F);
    EXPECT_NEAR(inverseInertia.x, 15.3846F, 1.0e-2F);
    EXPECT_FLOAT_EQ(inverseInertia.x, inverseInertia.z);
}

TEST(RagdollSolverUVETest, CapsuleInverseInertiaUVE_IsZeroForAnImmovableBody) {
    const Math::Vector3UVE zero = ComputeCapsuleInverseInertiaUVE(0.0F, 0.1F, 0.2F);
    EXPECT_FLOAT_EQ(zero.x, 0.0F);
    EXPECT_FLOAT_EQ(zero.y, 0.0F);
    EXPECT_FLOAT_EQ(zero.z, 0.0F);
    EXPECT_FLOAT_EQ(ComputeCapsuleInverseInertiaUVE(-1.0F, 0.1F, 0.2F).y, 0.0F);
    EXPECT_FLOAT_EQ(ComputeCapsuleInverseInertiaUVE(2.0F, 0.0F, 0.2F).y, 0.0F);

    // No barrel means a sphere, not an immovable body: 2/5 m r^2 = 0.002 kg m^2 on all three axes.
    const Math::Vector3UVE sphere = ComputeCapsuleInverseInertiaUVE(1.0F, 0.1F, 0.0F);
    EXPECT_NEAR(sphere.y, 250.0F, 1.0e-2F);
    EXPECT_FLOAT_EQ(sphere.x, sphere.y);
}

TEST(RagdollSolverUVETest, ResetUVE_SeedsMassAndInertiaFromTheShape) {
    RagdollSolverUVE solver;
    std::vector<RagdollBodyUVE> bodies{MakeCapsuleUVE({}, 2.0F, 0.1F, 0.2F)};
    ASSERT_TRUE(solver.ResetUVE(bodies));
    ASSERT_EQ(solver.GetBodyCountUVE(), 1U);
    EXPECT_FLOAT_EQ(solver.GetBodyUVE(0).inverseMass, 0.5F);
    EXPECT_NEAR(solver.GetBodyUVE(0).inverseInertiaLocal.y, 100.0F, 1.0e-2F);
    EXPECT_FLOAT_EQ(solver.GetTotalMassUVE(), 2.0F);
}

TEST(RagdollSolverUVETest, ResetUVE_RefusesMoreBodiesThanTheCap) {
    RagdollSolverUVE solver;
    std::vector<RagdollBodyUVE> bodies(kMaximumRagdollBodiesUVE + 1U, MakeCapsuleUVE({}));
    EXPECT_FALSE(solver.ResetUVE(bodies));
    EXPECT_EQ(solver.GetBodyCountUVE(), 0U);
    EXPECT_FLOAT_EQ(solver.GetTotalMassUVE(), 0.0F);
}

TEST(RagdollSolverUVETest, AddJointUVE_RefusesDegenerateOrOutOfRangeJoints) {
    RagdollSolverUVE solver;
    std::vector<RagdollBodyUVE> bodies(2U, MakeCapsuleUVE({}));
    ASSERT_TRUE(solver.ResetUVE(bodies));

    EXPECT_FALSE(solver.AddJointUVE(MakeBallSocketJointUVE(0U, 0U, {}, {}))) << "a joint to itself";
    EXPECT_FALSE(solver.AddJointUVE(MakeBallSocketJointUVE(0U, 2U, {}, {}))) << "body out of range";
    EXPECT_EQ(solver.GetJointCountUVE(), 0U);
    EXPECT_TRUE(solver.AddJointUVE(MakeBallSocketJointUVE(0U, 1U, {}, {})));
    EXPECT_EQ(solver.GetJointCountUVE(), 1U);

    // The table is a bound, not a suggestion: the 25th joint is refused and the count stays put.
    RagdollSolverUVE full;
    std::vector<RagdollBodyUVE> many(kMaximumRagdollBodiesUVE, MakeCapsuleUVE({}));
    ASSERT_TRUE(full.ResetUVE(many));
    for (std::size_t index = 0U; index < kMaximumRagdollJointsUVE; ++index) {
        ASSERT_TRUE(full.AddJointUVE(MakeBallSocketJointUVE(0U, 1U, {}, {})));
    }
    EXPECT_FALSE(full.AddJointUVE(MakeBallSocketJointUVE(0U, 1U, {}, {})));
    EXPECT_EQ(full.GetJointCountUVE(), kMaximumRagdollJointsUVE);
}

// ---- Integration, determinism, contacts -------------------------------------------------------

TEST(RagdollSolverUVETest, StepUVE_FallsExactlyLikeGravityOverOneSecond) {
    RagdollSolverUVE solver;
    std::vector<RagdollBodyUVE> bodies{MakeCapsuleUVE({0.0F, 10.0F, 0.0F})};
    ASSERT_TRUE(solver.ResetUVE(bodies));

    const Math::Vector3UVE gravity{0.0F, -9.81F, 0.0F};
    RagdollStepReportUVE report;
    for (int step = 0; step < 240; ++step) {
        report = solver.StepUVE(gravity, kStepUVE, {});
    }
    EXPECT_TRUE(report.stepped);
    EXPECT_NEAR(solver.GetBodyUVE(0).linearVelocity.y, -9.81F, 1.0e-3F);
    // Semi-implicit Euler: the position lags half a step behind the exact -4.905 m, which is the
    // reason a ragdoll drops a few millimetres differently than a textbook parabola over one second.
    EXPECT_NEAR(solver.GetBodyUVE(0).position.y, 10.0F - 4.925F, 1.0e-2F);
    EXPECT_FLOAT_EQ(solver.GetBodyUVE(0).position.x, 0.0F);
}

TEST(RagdollSolverUVETest, StepUVE_RefusesNonFiniteTimeAndGravity) {
    RagdollSolverUVE solver;
    std::vector<RagdollBodyUVE> bodies{MakeCapsuleUVE({})};
    ASSERT_TRUE(solver.ResetUVE(bodies));

    EXPECT_FALSE(solver.StepUVE({0.0F, -9.81F, 0.0F}, std::numeric_limits<float>::quiet_NaN(), {}).stepped);
    EXPECT_FALSE(solver.StepUVE({0.0F, -9.81F, 0.0F}, 0.0F, {}).stepped);
    EXPECT_FALSE(solver.StepUVE({0.0F, -9.81F, 0.0F}, -0.01F, {}).stepped);
    EXPECT_FALSE(solver.StepUVE({std::numeric_limits<float>::infinity(), 0.0F, 0.0F}, kStepUVE, {}).stepped);
    EXPECT_FLOAT_EQ(solver.GetBodyUVE(0).position.y, 0.0F);
}

TEST(RagdollSolverUVETest, StepUVE_ClampsAHugeStepInsteadOfTunneling) {
    RagdollSolverUVE solver;
    std::vector<RagdollBodyUVE> bodies{MakeCapsuleUVE({0.0F, 10.0F, 0.0F})};
    ASSERT_TRUE(solver.ResetUVE(bodies));
    // 10 seconds in one call is a bug upstream; the solver answers with its 0.1 s ceiling rather
    // than integrating a 10 s drop in one step.
    const RagdollStepReportUVE report = solver.StepUVE({0.0F, -10.0F, 0.0F}, 10.0F, {});
    EXPECT_TRUE(report.stepped);
    EXPECT_NEAR(solver.GetBodyUVE(0).linearVelocity.y, -1.0F, 1.0e-4F);
    EXPECT_GT(solver.GetBodyUVE(0).position.y, 9.0F);
}

TEST(RagdollSolverUVETest, StepUVE_IsBitwiseDeterministic) {
    const auto simulate = [](RagdollSolverUVE& solver) {
        std::vector<RagdollBodyUVE> bodies{MakeCapsuleUVE({0.0F, 2.0F, 0.0F}), MakeCapsuleUVE({0.4F, 2.0F, 0.1F})};
        EXPECT_TRUE(solver.ResetUVE(bodies));
        EXPECT_TRUE(solver.AddJointUVE(MakeBallSocketJointUVE(0U, 1U, {}, {})));
        solver.ApplyImpulseUVE(0U, Math::Vector3UVE{3.0F, 0.0F, 1.0F}, {0.0F, 2.0F, 0.0F});
        solver.ApplyImpulseUVE(1U, Math::Vector3UVE{-2.0F, 0.0F, 0.5F}, {0.4F, 2.0F, 0.1F});
        for (int step = 0; step < 600; ++step) {
            static_cast<void>(solver.StepUVE({0.0F, -9.81F, 0.0F}, kStepUVE, {}));
        }
    };

    RagdollSolverUVE first;
    RagdollSolverUVE second;
    simulate(first);
    simulate(second);
    ASSERT_EQ(first.GetBodyCountUVE(), second.GetBodyCountUVE());
    for (std::size_t body = 0U; body < first.GetBodyCountUVE(); ++body) {
        const RagdollBodyUVE& lhs = first.GetBodyUVE(body);
        const RagdollBodyUVE& rhs = second.GetBodyUVE(body);
        EXPECT_FLOAT_EQ(lhs.position.x, rhs.position.x);
        EXPECT_FLOAT_EQ(lhs.position.y, rhs.position.y);
        EXPECT_FLOAT_EQ(lhs.position.z, rhs.position.z);
        EXPECT_FLOAT_EQ(lhs.rotation.x, rhs.rotation.x);
        EXPECT_FLOAT_EQ(lhs.rotation.w, rhs.rotation.w);
        EXPECT_FLOAT_EQ(lhs.linearVelocity.y, rhs.linearVelocity.y);
        EXPECT_FLOAT_EQ(lhs.angularVelocity.x, rhs.angularVelocity.x);
    }
}

TEST(RagdollSolverUVETest, StepUVE_RestsOnAFloorWithoutSinkingOrJittering) {
    RagdollSolverUVE solver;
    std::vector<RagdollBodyUVE> bodies{MakeCapsuleUVE({0.0F, 0.5F, 0.0F})};
    bodies[0].linearDamping = 0.05F;
    ASSERT_TRUE(solver.ResetUVE(bodies));

    const float restingHeight = bodies[0].halfHeight + bodies[0].radius;
    float maximumHeight = -std::numeric_limits<float>::infinity();
    float minimumHeight = std::numeric_limits<float>::infinity();
    for (int step = 0; step < 480; ++step) {
        std::vector<RagdollContactUVE> contacts;
        const float depth = FloorDepthUVE(solver.GetBodyUVE(0));
        if (depth > 0.0F) contacts.push_back(RagdollContactUVE{0U, {}, {0.0F, 1.0F, 0.0F}, depth, 0.8F, 0.0F});
        static_cast<void>(solver.StepUVE({0.0F, -9.81F, 0.0F}, kStepUVE, contacts));
        if (step > 120) {
            maximumHeight = std::max(maximumHeight, solver.GetBodyUVE(0).position.y);
            minimumHeight = std::min(minimumHeight, solver.GetBodyUVE(0).position.y);
        }
    }
    // It stops at its own surface, not inside the floor and not hovering above it.
    EXPECT_NEAR(solver.GetBodyUVE(0).position.y, restingHeight, 0.01F);
    EXPECT_LT(maximumHeight - minimumHeight, 0.01F) << "a resting ragdoll should be still";
    EXPECT_LT(std::fabs(solver.GetBodyUVE(0).linearVelocity.y), 0.1F);
}

TEST(RagdollSolverUVETest, StepUVE_KeepsAJointedChainTogether) {
    TwoBodyRigUVE rig = MakeTwoBodyRigUVE(/*hinge=*/false);
    RagdollStepReportUVE report;
    for (int step = 0; step < 480; ++step) {
        report = rig.solver.StepUVE({0.0F, -9.81F, 0.0F}, kStepUVE, {});
    }
    EXPECT_TRUE(report.stepped);
    EXPECT_EQ(report.constrainedJointCount, 1U);
    EXPECT_LT(report.maximumJointDrift, 0.01F);
    EXPECT_EQ(report.jointDriftCount, 0U);
}

TEST(RagdollSolverUVETest, StepUVE_StopsAHingeAtItsAuthoredTwistLimit) {
    TwoBodyRigUVE rig = MakeTwoBodyRigUVE(/*hinge=*/true);
    // Spin the child hard about the hinge axis, backwards past the -5 degree limit.
    const Math::Vector3UVE hingeAxis{1.0F, 0.0F, 0.0F};
    rig.solver.GetBodyUVE(1).angularVelocity = hingeAxis * -40.0F;
    for (int step = 0; step < 480; ++step) {
        static_cast<void>(rig.solver.StepUVE({}, kStepUVE, {}));
    }

    const RagdollJointUVE& joint = rig.solver.GetJointUVE(0);
    const Math::QuaternionUVE firstFrame =
        Math::MultiplyUVE(rig.solver.GetBodyUVE(joint.firstBody).rotation, joint.firstFrame);
    const Math::QuaternionUVE secondFrame =
        Math::MultiplyUVE(rig.solver.GetBodyUVE(joint.secondBody).rotation, joint.secondFrame);
    Math::QuaternionUVE inverseFirst;
    ASSERT_TRUE(Math::TryInverseUVE(firstFrame, inverseFirst));
    const Math::QuaternionUVE relative = Math::MultiplyUVE(inverseFirst, secondFrame);
    const float twistRadians = 2.0F * std::atan2(relative.x, relative.w);
    const float lowerRadians = joint.twistLowerLimitDegrees * 3.14159265F / 180.0F;
    // Within a tenth of a radian of the authored stop: a limit that is respected in the limit, not
    // in the asymptote.
    EXPECT_GE(twistRadians, lowerRadians - 0.1F) << "a shin must not fold through the thigh";
    EXPECT_LT(twistRadians, 0.1F) << "the hinge only bends one way";
}

// ---- Impulses and sleeping --------------------------------------------------------------------

TEST(RagdollSolverUVETest, ApplyImpulseUVE_SpinsTheBodyFromAnOffCentreHit) {
    RagdollSolverUVE solver;
    std::vector<RagdollBodyUVE> bodies{MakeCapsuleUVE({0.0F, 1.0F, 0.0F})};
    ASSERT_TRUE(solver.ResetUVE(bodies));

    solver.ApplyImpulseUVE(0U, Math::Vector3UVE{10.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.2F, 0.0F});
    // 10 N.s on 2 kg is 5 m/s; the hit lands 0.2 m above the centre of mass, so it also turns.
    EXPECT_NEAR(solver.GetBodyUVE(0).linearVelocity.x, 5.0F, 1.0e-4F);
    EXPECT_GT(std::fabs(solver.GetBodyUVE(0).angularVelocity.z), 0.1F);
    EXPECT_TRUE(std::isfinite(solver.GetBodyUVE(0).angularVelocity.z));
}

TEST(RagdollSolverUVETest, ApplyImpulseUVE_WakesASleepingRagdollAndIgnoresBadInput) {
    RagdollSolverUVE solver;
    std::vector<RagdollBodyUVE> bodies{MakeCapsuleUVE({})};
    ASSERT_TRUE(solver.ResetUVE(bodies));
    solver.SleepUVE();
    ASSERT_TRUE(solver.IsSleepingUVE());

    solver.ApplyImpulseUVE(0U, Math::Vector3UVE{1.0F, 0.0F, 0.0F}, {});
    EXPECT_FALSE(solver.IsSleepingUVE()) << "a hit that did not wake the body would do nothing";

    const Math::Vector3UVE before = solver.GetBodyUVE(0).linearVelocity;
    solver.ApplyImpulseUVE(99U, Math::Vector3UVE{1.0F, 0.0F, 0.0F}, {});
    solver.ApplyImpulseUVE(0U, Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, {});
    EXPECT_FLOAT_EQ(solver.GetBodyUVE(0).linearVelocity.x, before.x);
    EXPECT_FLOAT_EQ(solver.GetBodyUVE(0).linearVelocity.y, before.y);
}

TEST(RagdollSolverUVETest, SleepUVE_StopsSteppingUntilWoken) {
    RagdollSolverUVE solver;
    std::vector<RagdollBodyUVE> bodies{MakeCapsuleUVE({0.0F, 1.0F, 0.0F})};
    ASSERT_TRUE(solver.ResetUVE(bodies));
    solver.SleepUVE();

    const Math::Vector3UVE position = solver.GetBodyUVE(0).position;
    const RagdollStepReportUVE report = solver.StepUVE({0.0F, -9.81F, 0.0F}, kStepUVE, {});
    EXPECT_FALSE(report.stepped);
    EXPECT_TRUE(report.sleeping);
    EXPECT_FLOAT_EQ(solver.GetBodyUVE(0).position.y, position.y) << "a sleeping body does not fall";

    solver.WakeUVE();
    EXPECT_TRUE(solver.StepUVE({0.0F, -9.81F, 0.0F}, kStepUVE, {}).stepped);
}

TEST(RagdollSolverUVETest, StepUVE_PutsARestingBodyToSleep) {
    RagdollSolverUVE solver;
    std::vector<RagdollBodyUVE> bodies{MakeCapsuleUVE({0.0F, 1.0F, 0.0F})};
    ASSERT_TRUE(solver.ResetUVE(bodies));

    RagdollStepSettingsUVE settings;
    settings.sleepDelaySeconds = 0.1F;
    RagdollStepReportUVE report;
    for (int step = 0; step < 120; ++step) {
        report = solver.StepUVE({}, kStepUVE, {}, settings);
    }
    EXPECT_TRUE(solver.IsSleepingUVE());
    EXPECT_TRUE(report.sleeping);
    EXPECT_FALSE(solver.StepUVE({}, kStepUVE, {}, settings).stepped);
}

// ---- Joint frames and builders ----------------------------------------------------------------

TEST(RagdollSolverUVETest, MakeJointFrameFromTwistAxisUVE_MapsXOntoTheAxis) {
    const Math::Vector3UVE axis{0.0F, 1.0F, 0.0F};
    const Math::QuaternionUVE frame = MakeJointFrameFromTwistAxisUVE(axis);
    const Math::Vector3UVE mapped = Math::RotateVectorUVE(frame, Math::Vector3UVE{1.0F, 0.0F, 0.0F});
    EXPECT_NEAR(mapped.x, axis.x, 1.0e-5F);
    EXPECT_NEAR(mapped.y, axis.y, 1.0e-5F);
    EXPECT_NEAR(mapped.z, axis.z, 1.0e-5F);

    // Degenerate input answers the identity rather than a NaN rotation.
    EXPECT_FLOAT_EQ(MakeJointFrameFromTwistAxisUVE({}).w, 1.0F);
    EXPECT_FLOAT_EQ(MakeJointFrameFromTwistAxisUVE({std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}).w,
                    1.0F);
}

TEST(RagdollSolverUVETest, ComputeJointSecondFrameUVE_LeavesTheBindPoseAtZeroDeviation) {
    const Math::QuaternionUVE firstBind = MakeJointFrameFromTwistAxisUVE({0.0F, 1.0F, 0.0F});
    const Math::QuaternionUVE secondBind = MakeJointFrameFromTwistAxisUVE({1.0F, 0.0F, 0.0F});
    const Math::QuaternionUVE secondFrame = ComputeJointSecondFrameUVE(firstBind, secondBind);
    const Math::QuaternionUVE firstFrameAtBind = Math::MultiplyUVE(firstBind, Math::QuaternionUVE{});
    const Math::QuaternionUVE secondFrameAtBind = Math::MultiplyUVE(secondBind, secondFrame);
    EXPECT_NEAR(firstFrameAtBind.x, secondFrameAtBind.x, 1.0e-5F);
    EXPECT_NEAR(firstFrameAtBind.y, secondFrameAtBind.y, 1.0e-5F);
    EXPECT_NEAR(firstFrameAtBind.z, secondFrameAtBind.z, 1.0e-5F);
    EXPECT_NEAR(firstFrameAtBind.w, secondFrameAtBind.w, 1.0e-5F);
}

TEST(RagdollSolverUVETest, MakeHingeJointUVE_LocksBothSwingsAndLimitsTheTwist) {
    const RagdollJointUVE hinge = MakeHingeJointUVE(0U, 1U, {}, {}, 70.0F);
    EXPECT_EQ(hinge.swing1Motion, RagdollMotionUVE::Locked);
    EXPECT_EQ(hinge.swing2Motion, RagdollMotionUVE::Locked);
    EXPECT_EQ(hinge.twistMotion, RagdollMotionUVE::Limited);
    EXPECT_FLOAT_EQ(hinge.twistLimitDegrees, 70.0F);

    const RagdollJointUVE socket = MakeBallSocketJointUVE(0U, 1U, {}, {}, 60.0F);
    EXPECT_EQ(socket.swing1Motion, RagdollMotionUVE::Limited);
    EXPECT_EQ(socket.swing2Motion, RagdollMotionUVE::Limited);
    EXPECT_FLOAT_EQ(socket.swing1LimitDegrees, 60.0F);
    EXPECT_FALSE(socket.twistAsymmetric) << "a ball socket has no one-way range";
}

// ---- AAA Solver Upgrades (Inertia Conditioning, Self-Collision, PD Motors, Exact Contacts, Debug) ----

TEST(RagdollSolverUVETest, StabilizeInertiaUVE_BoundsPrincipalAndChainInertiaRatios) {
    RagdollSolverUVE solver;
    RagdollBodyUVE pelvis = MakeCapsuleUVE({0.0F, 1.0F, 0.0F}, 15.0F, 0.14F, 0.10F);
    RagdollBodyUVE hand = MakeCapsuleUVE({0.0F, 1.4F, 0.0F}, 0.4F, 0.03F, 0.05F);
    ASSERT_TRUE(solver.ResetUVE(std::vector<RagdollBodyUVE>{pelvis, hand}));
    ASSERT_TRUE(solver.AddJointUVE(MakeBallSocketJointUVE(0U, 1U, {0.0F, 0.15F, 0.0F}, {0.0F, -0.25F, 0.0F}, 60.0F)));

    solver.StabilizeInertiaUVE(4.0F, 16.0F);
    const Math::Vector3UVE invPelvis = solver.GetBodyUVE(0U).inverseInertiaLocal;
    const Math::Vector3UVE invHand = solver.GetBodyUVE(1U).inverseInertiaLocal;
    const float pelvisMax = std::max({invPelvis.x, invPelvis.y, invPelvis.z});
    const float handMax = std::max({invHand.x, invHand.y, invHand.z});
    const float handMin = std::min({invHand.x, invHand.y, invHand.z});
    EXPECT_LE(handMax / handMin, 4.01F);
    EXPECT_LE(handMax / pelvisMax, 16.01F);

    // High-speed off-center impact on extreme mass-ratio chain remains finite and tightly coupled.
    solver.ApplyImpulseUVE(1U, {25.0F, 0.0F, 0.0F}, {0.0F, 1.45F, 0.0F});
    RagdollStepReportUVE report;
    for (int step = 0; step < 240; ++step) {
        report = solver.StepUVE({0.0F, -9.81F, 0.0F}, kStepUVE, {});
    }
    EXPECT_TRUE(Math::IsFiniteUVE(solver.GetBodyUVE(1U).position));
    EXPECT_TRUE(Math::IsFiniteUVE(solver.GetBodyUVE(1U).angularVelocity));
    EXPECT_LT(report.maximumJointDrift, 0.02F);
}

TEST(RagdollSolverUVETest, OneSidedJointLimit_DoesNotStickWhenMovingAwayFromLimit) {
    TwoBodyRigUVE rig = MakeTwoBodyRigUVE(/*hinge=*/true);
    // Place child slightly below the -5 deg lower limit, but give it POSITIVE velocity moving BACK
    // into the allowed range (towards +10 deg). A sticky two-sided limit would pull it back!
    Math::QuaternionUVE rotated{};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE({1.0F, 0.0F, 0.0F}, -8.0F * 3.14159265F / 180.0F, rotated));
    rig.solver.GetBodyUVE(1U).rotation = rotated;
    rig.solver.GetBodyUVE(1U).angularVelocity = {5.0F, 0.0F, 0.0F};

    RagdollStepSettingsUVE settings;
    settings.positionIterations = 0U;
    settings.sleepDelaySeconds = 0.0F;
    static_cast<void>(rig.solver.StepUVE({}, kStepUVE, {}, settings));
    // Child velocity moving away from the lower limit must remain strongly positive after linear
    // anchor coupling (not reversed or clamped to zero by a sticky bilateral limit!).
    EXPECT_GT(rig.solver.GetBodyUVE(1U).angularVelocity.x, 2.0F);
    EXPECT_GT(rig.solver.GetBodyUVE(1U).angularVelocity.x - rig.solver.GetBodyUVE(0U).angularVelocity.x, 3.0F);
}

TEST(RagdollSolverUVETest, SelfCollision_SeparatesNonAdjacentBodiesWhileIgnoringParentChild) {
    RagdollSolverUVE solver;
    RagdollBodyUVE body0 = MakeCapsuleUVE({0.0F, 1.0F, 0.0F}, 5.0F, 0.12F, 0.2F);
    RagdollBodyUVE body1 = MakeCapsuleUVE({0.0F, 1.5F, 0.0F}, 2.0F, 0.08F, 0.15F);
    // Body 2 is non-adjacent (not jointed to body 0) and initially separated at bind, then pushed into body 0.
    RagdollBodyUVE body2 = MakeCapsuleUVE({0.5F, 1.0F, 0.0F}, 2.0F, 0.10F, 0.2F);
    ASSERT_TRUE(solver.ResetUVE(std::vector<RagdollBodyUVE>{body0, body1, body2}));
    ASSERT_TRUE(solver.AddJointUVE(MakeBallSocketJointUVE(0U, 1U, {0.0F, 0.25F, 0.0F}, {0.0F, -0.25F, 0.0F})));
    solver.RebuildSelfCollisionFilterUVE(/*disableCurrentPoseOverlaps=*/true, /*minimumJointHops=*/2U);

    EXPECT_FALSE(solver.CanSelfCollideUVE(0U, 1U)) << "parent-child pair must be filtered";
    EXPECT_TRUE(solver.CanSelfCollideUVE(0U, 2U)) << "non-adjacent separated pair must collide";

    // Drive body 2 straight into body 0.
    solver.GetBodyUVE(2U).position = {0.10F, 1.0F, 0.0F};
    solver.GetBodyUVE(2U).linearVelocity = {-4.0F, 0.0F, 0.0F};

    RagdollStepSettingsUVE settings;
    settings.enableSelfCollision = true;
    settings.sleepDelaySeconds = 0.0F;
    RagdollStepReportUVE report;
    for (int step = 0; step < 60; ++step) {
        report = solver.StepUVE({}, kStepUVE, {}, settings);
    }
    const float separation = std::fabs(solver.GetBodyUVE(2U).position.x - solver.GetBodyUVE(0U).position.x);
    EXPECT_GE(separation, body0.radius + body2.radius - 0.02F)
        << "self-collision must push non-adjacent capsules apart";
}

TEST(RagdollSolverUVETest, ActiveJointMotor_DrivesLimbToTargetAngleAndStaysStableAtHighStiffness) {
    TwoBodyRigUVE rig = MakeTwoBodyRigUVE(/*hinge=*/true);
    rig.solver.GetBodyUVE(0U).kinematic = true; // Anchor parent like a partial ragdoll root!

    Math::QuaternionUVE targetRel{};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE({1.0F, 0.0F, 0.0F}, 60.0F * 3.14159265F / 180.0F, targetRel));
    RagdollJointUVE& joint = rig.solver.GetJointUVE(0U);
    joint.motor.enabled = true;
    joint.motor.targetRelativeRotation = targetRel;
    joint.motor.angularStiffness = 5000.0F; // Extreme stiffness must stay unconditionally stable!
    joint.motor.angularDamping = 60.0F;
    joint.motor.maxTorque = 250.0F;

    RagdollStepSettingsUVE settings;
    settings.sleepDelaySeconds = 0.0F;
    for (int step = 0; step < 240; ++step) {
        static_cast<void>(rig.solver.StepUVE({0.0F, -9.81F, 0.0F}, kStepUVE, {}, settings));
    }

    const Math::QuaternionUVE rel = Math::MultiplyUVE(
        Math::ConjugateUVE(Math::MultiplyUVE(rig.solver.GetBodyUVE(0U).rotation, joint.firstFrame)),
        Math::MultiplyUVE(rig.solver.GetBodyUVE(1U).rotation, joint.secondFrame));
    const float twistDeg = (2.0F * std::atan2(rel.x, rel.w)) * 180.0F / 3.14159265F;
    EXPECT_TRUE(Math::IsFiniteUVE(rig.solver.GetBodyUVE(1U).rotation));
    EXPECT_NEAR(twistDeg, 60.0F, 8.0F) << "PD motor must drive limb to 60 deg target against gravity";
}

TEST(RagdollSolverUVETest, ExactBoxContactPoint_ProducesZeroSpuriousTorqueFarFromOrigin) {
    // Regression test for Bug #4: a limb standing at x = +25 m on a 100 m floor box centered at
    // (0, -0.5, 0) used to pick the endpoint closest to (0, -0.5, 0) instead of the floor surface!
    RagdollSolverUVE solver;
    RagdollBodyUVE limb = MakeCapsuleUVE({25.0F, 0.35F, 0.0F}, 3.0F, 0.1F, 0.2F);
    limb.linearDamping = 0.05F;
    limb.angularDamping = 0.2F;
    ASSERT_TRUE(solver.ResetUVE(std::vector<RagdollBodyUVE>{limb}));

    Detail::ColliderWorldAabbUVE floorCollider{};
    floorCollider.entity = Scene::EntityUVE{99U, 1U};
    floorCollider.collisionLayer = 1U;
    floorCollider.collisionMask = 0xFFFFFFFFU;
    floorCollider.shapeType = Scene::ColliderShapeTypeUVE::Box;
    floorCollider.shapeHalfExtents = {50.0F, 0.5F, 50.0F};
    floorCollider.shapeRotation = Math::QuaternionUVE{};
    floorCollider.worldAabb = Math::AabbUVE{{-50.0F, -1.0F, -50.0F}, {50.0F, 0.0F, 50.0F}};

    const std::array<Detail::ColliderWorldAabbUVE, 1U> colliders{floorCollider};
    RagdollCollisionFilterUVE filter{};
    std::array<RagdollContactUVE, 16U> contacts{};
    for (int step = 0; step < 240; ++step) {
        const RagdollContactStatsUVE stats =
            BuildRagdollContactsUVE(solver, colliders, filter, std::span<RagdollContactUVE>{contacts});
        static_cast<void>(solver.StepUVE(
            {0.0F, -9.81F, 0.0F}, kStepUVE,
            std::span<const RagdollContactUVE>{contacts.data(), stats.generatedContactCount}));
    }
    EXPECT_NEAR(solver.GetBodyUVE(0U).position.y, 0.3F, 0.015F);
    EXPECT_LT(std::sqrt(Math::LengthSquaredUVE(solver.GetBodyUVE(0U).angularVelocity)), 0.05F)
        << "standing vertically at x=+25m on a centered floor box must generate zero fake origin torque";
}

TEST(RagdollSolverUVETest, BuildRagdollDebugLinesUVE_EmitsFiniteCapsulesAndLimitCones) {
    TwoBodyRigUVE hingeRig = MakeTwoBodyRigUVE(/*hinge=*/true);
    std::array<ColliderDebugLineUVE, 512U> buffer{};
    const ColliderDebugBuildResultUVE res =
        BuildRagdollDebugLinesUVE(hingeRig.solver, std::span<ColliderDebugLineUVE>{buffer});
    EXPECT_GT(res.lineCount, 40U);
    EXPECT_FALSE(res.truncated);
    for (std::size_t i = 0U; i < res.lineCount; ++i) {
        EXPECT_TRUE(Math::IsFiniteUVE(buffer[i].from));
        EXPECT_TRUE(Math::IsFiniteUVE(buffer[i].to));
    }

    // Small buffer degrades cleanly via truncation without writing out of bounds.
    std::array<ColliderDebugLineUVE, 8U> tiny{};
    const ColliderDebugBuildResultUVE trunc =
        BuildRagdollDebugLinesUVE(hingeRig.solver, std::span<ColliderDebugLineUVE>{tiny});
    EXPECT_EQ(trunc.lineCount, 8U);
    EXPECT_TRUE(trunc.truncated);
}

} // namespace
} // namespace UVE::Physics::Tests
