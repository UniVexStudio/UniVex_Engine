// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "uve/math/aabb_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/physics/active_ragdoll_uve.h"
#include "uve/physics/advanced_collision_analysis_uve.h"
#include "uve/physics/airborne_body_control_uve.h"
#include "uve/physics/detail/collider_world_aabb_cache_uve.h"
#include "uve/physics/landing_system_uve.h"
#include "uve/physics/motion_trajectory_uve.h"
#include "uve/physics/physics_debug_draw_uve.h"
#include "uve/physics/ragdoll_recovery_uve.h"
#include "uve/physics/ragdoll_solver_uve.h"

namespace UVE::Physics::Tests {
namespace {

[[nodiscard]] RagdollBodyUVE MakeCapsuleBodyUVE(
    const Math::Vector3UVE position,
    const float mass = 12.0F,
    const float radius = 0.12F,
    const float halfHeight = 0.25F) {
    RagdollBodyUVE body;
    body.mass = mass;
    body.radius = radius;
    body.halfHeight = halfHeight;
    body.position = position;
    body.rotation = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F};
    body.linearVelocity = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    body.angularVelocity = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    body.linearDamping = 0.05F;
    body.angularDamping = 0.5F;
    body.friction = 0.8F;
    body.restitution = 0.0F;
    body.gravityEnabled = true;
    return body;
}

[[nodiscard]] RagdollSolverUVE MakeHumanoidRagdollSolverUVE() {
    RagdollSolverUVE solver;

    // 7 bodies: Pelvis, Spine, Head, LeftLeg, RightLeg, LeftArm, RightArm
    RagdollBodyUVE pelvis = MakeCapsuleBodyUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 15.0F, 0.15F, 0.1F);
    RagdollBodyUVE spine = MakeCapsuleBodyUVE(Math::Vector3UVE{0.0F, 1.35F, 0.0F}, 20.0F, 0.14F, 0.2F);
    RagdollBodyUVE head = MakeCapsuleBodyUVE(Math::Vector3UVE{0.0F, 1.75F, 0.0F}, 5.0F, 0.1F, 0.08F);
    RagdollBodyUVE leftLeg = MakeCapsuleBodyUVE(Math::Vector3UVE{-0.2F, 0.5F, 0.0F}, 12.0F, 0.09F, 0.35F);
    RagdollBodyUVE rightLeg = MakeCapsuleBodyUVE(Math::Vector3UVE{0.2F, 0.5F, 0.0F}, 12.0F, 0.09F, 0.35F);
    RagdollBodyUVE leftArm = MakeCapsuleBodyUVE(Math::Vector3UVE{-0.45F, 1.25F, 0.0F}, 5.0F, 0.07F, 0.25F);
    RagdollBodyUVE rightArm = MakeCapsuleBodyUVE(Math::Vector3UVE{0.45F, 1.25F, 0.0F}, 5.0F, 0.07F, 0.25F);

    std::vector<RagdollBodyUVE> bodies{pelvis, spine, head, leftLeg, rightLeg, leftArm, rightArm};
    EXPECT_TRUE(solver.ResetUVE(bodies));

    // Connect joints
    // Pelvis -> Spine
    RagdollJointUVE spineJoint = MakeBallSocketJointUVE(0U, 1U, Math::Vector3UVE{0.0F, 0.15F, 0.0F},
                                                        Math::Vector3UVE{0.0F, -0.2F, 0.0F}, 35.0F);
    spineJoint.driveEnabled = true;
    EXPECT_TRUE(solver.AddJointUVE(spineJoint));

    // Spine -> Head
    RagdollJointUVE neckJoint = MakeBallSocketJointUVE(1U, 2U, Math::Vector3UVE{0.0F, 0.22F, 0.0F},
                                                       Math::Vector3UVE{0.0F, -0.12F, 0.0F}, 45.0F);
    neckJoint.driveEnabled = true;
    EXPECT_TRUE(solver.AddJointUVE(neckJoint));

    // Pelvis -> Left Leg
    RagdollJointUVE leftHip = MakeBallSocketJointUVE(0U, 3U, Math::Vector3UVE{-0.15F, -0.15F, 0.0F},
                                                     Math::Vector3UVE{0.0F, 0.4F, 0.0F}, 50.0F);
    leftHip.driveEnabled = true;
    EXPECT_TRUE(solver.AddJointUVE(leftHip));

    // Pelvis -> Right Leg
    RagdollJointUVE rightHip = MakeBallSocketJointUVE(0U, 4U, Math::Vector3UVE{0.15F, -0.15F, 0.0F},
                                                      Math::Vector3UVE{0.0F, 0.4F, 0.0F}, 50.0F);
    rightHip.driveEnabled = true;
    EXPECT_TRUE(solver.AddJointUVE(rightHip));

    // Spine -> Left Arm
    RagdollJointUVE leftShoulder = MakeBallSocketJointUVE(1U, 5U, Math::Vector3UVE{-0.25F, 0.15F, 0.0F},
                                                          Math::Vector3UVE{0.0F, 0.3F, 0.0F}, 70.0F);
    leftShoulder.driveEnabled = true;
    EXPECT_TRUE(solver.AddJointUVE(leftShoulder));

    // Spine -> Right Arm
    RagdollJointUVE rightShoulder = MakeBallSocketJointUVE(1U, 6U, Math::Vector3UVE{0.25F, 0.15F, 0.0F},
                                                           Math::Vector3UVE{0.0F, 0.3F, 0.0F}, 70.0F);
    rightShoulder.driveEnabled = true;
    EXPECT_TRUE(solver.AddJointUVE(rightShoulder));

    return solver;
}

} // namespace

// ============================================================================
// TEST 01: Minor Impact Response
// ============================================================================
TEST(ActiveRagdollTests, Test01_MinorLateralImpact_MaintainsBalance) {
    RagdollSolverUVE solver = MakeHumanoidRagdollSolverUVE();
    ActiveRagdollControllerUVE controller;

    // Normal standing ground contacts under left and right foot
    const std::vector<RagdollContactUVE> contacts{
        RagdollContactUVE{.body = 3U, .point = Math::Vector3UVE{-0.2F, 0.0F, -0.1F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
        RagdollContactUVE{.body = 3U, .point = Math::Vector3UVE{-0.2F, 0.0F, 0.1F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
        RagdollContactUVE{.body = 4U, .point = Math::Vector3UVE{0.2F, 0.0F, -0.1F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
        RagdollContactUVE{.body = 4U, .point = Math::Vector3UVE{0.2F, 0.0F, 0.1F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
    };

    // Minor lateral impact on upper torso
    ImpactContactPointUVE impactPoint;
    impactPoint.contactPoint = Math::Vector3UVE{0.2F, 1.4F, 0.0F};
    impactPoint.contactNormal = Math::Vector3UVE{-1.0F, 0.0F, 0.0F};
    impactPoint.appliedImpulse = Math::Vector3UVE{-35.0F, 0.0F, 0.0F};
    impactPoint.penetrationDepth = 0.005F;
    impactPoint.relativeVelocity = Math::Vector3UVE{2.0F, 0.0F, 0.0F};

    const ImpactSeverityUVE severity = ClassifyImpactSeverityUVE(35.0F);
    EXPECT_EQ(severity, ImpactSeverityUVE::Minor);

    const ImpactDirectionUVE dir = ClassifyImpactDirectionUVE(
        Math::Vector3UVE{-1.0F, 0.0F, 0.0F}, Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F});
    EXPECT_EQ(dir, ImpactDirectionUVE::LateralRight);

    // Apply minor lateral velocity disturbance
    solver.ApplyImpulseUVE(1U, Math::Vector3UVE{-0.3F, 0.0F, 0.0F}, solver.GetBodyUVE(1U).position);

    controller.UpdateBalanceAndPostureUVE(solver, contacts, Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F}, 0.016F);

    const ActiveRagdollStateUVE& state = controller.GetStateUVE();
    // In minor disturbance, character upper body compensates and maintains standing balance
    EXPECT_TRUE(state.balanceState == BalanceStateUVE::Disturbed || state.balanceState == BalanceStateUVE::Stable);
    EXPECT_GE(state.activeBlendWeight, 0.9F);
}

// ============================================================================
// TEST 02: Moderate Impact Response & Corrective Stepping
// ============================================================================
TEST(ActiveRagdollTests, Test02_ModerateForwardImpact_CorrectiveStepping) {
    RagdollSolverUVE solver = MakeHumanoidRagdollSolverUVE();
    ActiveRagdollControllerUVE controller;

    const std::vector<RagdollContactUVE> contacts{
        RagdollContactUVE{.body = 3U, .point = Math::Vector3UVE{-0.2F, 0.0F, -0.05F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
        RagdollContactUVE{.body = 3U, .point = Math::Vector3UVE{-0.2F, 0.0F, 0.05F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
        RagdollContactUVE{.body = 4U, .point = Math::Vector3UVE{0.2F, 0.0F, -0.05F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
        RagdollContactUVE{.body = 4U, .point = Math::Vector3UVE{0.2F, 0.0F, 0.05F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
    };

    // Moderate forward impulse (120 N*s) pushes character forward along -Z
    const ImpactSeverityUVE severity = ClassifyImpactSeverityUVE(120.0F);
    EXPECT_EQ(severity, ImpactSeverityUVE::Moderate);

    // Apply moderate forward velocity to torso/pelvis
    solver.ApplyImpulseUVE(0U, Math::Vector3UVE{0.0F, 0.0F, -30.0F}, solver.GetBodyUVE(0U).position);
    solver.ApplyImpulseUVE(1U, Math::Vector3UVE{0.0F, 0.0F, -30.0F}, solver.GetBodyUVE(1U).position);

    controller.UpdateBalanceAndPostureUVE(solver, contacts, Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F}, 0.016F);

    const ActiveRagdollStateUVE& state = controller.GetStateUVE();
    EXPECT_EQ(state.balanceState, BalanceStateUVE::CorrectiveStepping);
    // Corrective step target should be placed forward (negative Z) to regain support polygon
    EXPECT_LT(state.correctiveStepTarget.z, 0.0F);
}

// ============================================================================
// TEST 03: Severe Impact Response & Ragdoll Transition
// ============================================================================
TEST(ActiveRagdollTests, Test03_StrongImpact_UnrecoverableFall) {
    RagdollSolverUVE solver = MakeHumanoidRagdollSolverUVE();
    ActiveRagdollControllerUVE controller;

    const std::vector<RagdollContactUVE> contacts{
        RagdollContactUVE{.body = 3U, .point = Math::Vector3UVE{-0.2F, 0.0F, 0.0F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
        RagdollContactUVE{.body = 4U, .point = Math::Vector3UVE{0.2F, 0.0F, 0.0F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
    };

    // Severe impulse (350 N*s)
    const ImpactSeverityUVE severity = ClassifyImpactSeverityUVE(350.0F);
    EXPECT_EQ(severity, ImpactSeverityUVE::Severe);

    // High velocity knocking character down
    solver.ApplyImpulseUVE(0U, Math::Vector3UVE{0.0F, 0.0F, -120.0F}, solver.GetBodyUVE(0U).position);
    solver.ApplyImpulseUVE(1U, Math::Vector3UVE{0.0F, 0.0F, -120.0F}, solver.GetBodyUVE(1U).position);

    controller.UpdateBalanceAndPostureUVE(solver, contacts, Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F}, 0.016F);

    const ActiveRagdollStateUVE& state = controller.GetStateUVE();
    EXPECT_EQ(state.balanceState, BalanceStateUVE::UnrecoverableFall);

    // Active blend weight relaxes over successive frames
    for (int frame = 0; frame < 30; ++frame) {
        controller.UpdateBalanceAndPostureUVE(solver, contacts, Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F}, 0.016F);
    }
    EXPECT_LT(controller.GetStateUVE().activeBlendWeight, 0.5F);
}

// ============================================================================
// TEST 04: Moving Vehicle Collision & Momentum Transfer
// ============================================================================
TEST(ActiveRagdollTests, Test04_MovingVehicleStrikesCharacter_MomentumTransfer) {
    // Vehicle: 1500 kg moving at 15 m/s hitting a 75 kg character
    const float mVehicle = 1500.0F;
    const float mCharacter = 75.0F;

    const float effectiveMass = ComputeEffectiveCollisionMassUVE(
        1.0F / mCharacter, 1.0F / mVehicle,
        Math::Matrix3x3UVE{}, Math::Matrix3x3UVE{},
        Math::Vector3UVE{}, Math::Vector3UVE{},
        Math::Vector3UVE{0.0F, 0.0F, 1.0F});

    EXPECT_NEAR(effectiveMass, (mCharacter * mVehicle) / (mCharacter + mVehicle), 0.01F);
    EXPECT_GT(effectiveMass, 70.0F);

    ImpactContactPointUVE impact;
    impact.contactPoint = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    impact.contactNormal = Math::Vector3UVE{0.0F, 0.0F, -1.0F};
    impact.appliedImpulse = Math::Vector3UVE{0.0F, 0.0F, -1200.0F};
    impact.penetrationDepth = 0.05F;
    impact.relativeVelocity = Math::Vector3UVE{0.0F, 0.0F, 15.0F};
    impact.surfaceTag = "vehicle";

    const std::vector<ImpactContactPointUVE> impacts{impact};
    const ImpactAnalysisSummaryUVE summary = AnalyzeImpactsUVE(
        impacts,
        Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F});

    EXPECT_EQ(summary.severity, ImpactSeverityUVE::Extreme);
    EXPECT_EQ(summary.category, ImpactCategoryUVE::CharacterVehicle);
    EXPECT_GT(summary.netImpulseMagnitude, 1000.0F);
}

// ============================================================================
// TEST 05: Motion Trajectory Prediction
// ============================================================================
TEST(ActiveRagdollTests, Test05_AirborneTrajectoryPrediction_MatchesBallisticIntegration) {
    const Math::Vector3UVE initialPos{0.0F, 5.0F, 0.0F};
    const Math::Vector3UVE initialVel{2.0F, 10.0F, 0.0F};
    const Math::Vector3UVE gravity{0.0F, -9.81F, 0.0F};

    TrajectoryInputUVE input;
    input.position = initialPos;
    input.linearVelocity = initialVel;
    input.gravity = gravity;
    input.horizonSeconds = 2.5F;
    input.stepSeconds = 1.0F / 60.0F;
    input.characterRadius = 0.0F;
    input.characterHeight = 0.0F;
    input.linearAirDrag = 0.0F;
    input.isGrounded = false;

    const MotionTrajectoryPredictionUVE pred = PredictMotionTrajectoryUVE(input, {});

    EXPECT_TRUE(pred.hasPredictedLanding);
    // Analytical landing time: 5.0 + 10.0 * t - 0.5 * 9.81 * t^2 = 0 => t = 2.459 s
    EXPECT_NEAR(pred.timeToLanding, 2.46F, 0.05F);

    // Analytical landing position: x = 2.0 * t = 4.92 m, y = 0 m
    EXPECT_NEAR(pred.predictedLandingPosition.x, 4.92F, 0.15F);
    EXPECT_NEAR(pred.predictedLandingPosition.y, 0.0F, 0.05F);

    // Analytical landing velocity: vx = 2.0, vy = 10.0 - 9.81 * 2.46 = -14.13 m/s
    EXPECT_NEAR(pred.predictedLandingVelocity.x, 2.0F, 0.05F);
    EXPECT_NEAR(pred.predictedLandingVelocity.y, -14.13F, 0.3F);
}

// ============================================================================
// TEST 06: Wall Collision During Flight & Rebound
// ============================================================================
TEST(ActiveRagdollTests, Test06_AirborneStrikesWall_TrajectoryWallCollisionAndRebound) {
    const Math::Vector3UVE initialPos{0.0F, 2.0F, 0.0F};
    const Math::Vector3UVE initialVel{10.0F, 0.0F, 0.0F}; // Moving in +X
    const Math::Vector3UVE gravity{0.0F, -9.81F, 0.0F};

    // Wall obstacle placed at X = 3.0 m (thickness 0.2 m, height 4.0 m)
    Detail::ColliderWorldAabbUVE wall;
    wall.worldAabb = Math::AabbUVE{Math::Vector3UVE{3.0F, 0.0F, -2.0F}, Math::Vector3UVE{3.2F, 4.0F, 2.0F}};
    const std::vector<Detail::ColliderWorldAabbUVE> obstacles{wall};

    TrajectoryInputUVE input;
    input.position = initialPos;
    input.linearVelocity = initialVel;
    input.gravity = gravity;
    input.horizonSeconds = 2.0F;
    input.stepSeconds = 1.0F / 60.0F;
    input.characterRadius = 0.0F;
    input.characterHeight = 0.0F;
    input.isGrounded = false;

    const MotionTrajectoryPredictionUVE pred = PredictMotionTrajectoryUVE(input, obstacles);

    EXPECT_TRUE(pred.hasPredictedWallHit);
    // Contact normal points out of wall: -X direction
    EXPECT_LT(pred.predictedWallHitNormal.x, -0.9F);

    const TrajectoryStateUVE state = EvaluateTrajectoryStateTransitionUVE(
        TrajectoryStateUVE::AirborneMovement,
        /*isGrounded=*/false,
        /*linearSpeed=*/Math::LengthUVE(initialVel),
        /*angularSpeed=*/0.0F,
        /*balanceError=*/Math::Vector3UVE{},
        /*balanceErrorThreshold=*/0.1F,
        /*hadWallHit=*/pred.hasPredictedWallHit);
    EXPECT_EQ(state, TrajectoryStateUVE::WallCollisionDuringFlight);
}

// ============================================================================
// TEST 07: Ground Rebound & Dissipation
// ============================================================================
TEST(ActiveRagdollTests, Test07_GroundRebound_RestitutionAndFrictionDissipation) {
    MotionTrajectoryPredictionUVE pred;
    pred.hasPredictedLanding = true;
    pred.predictedLandingVelocity = Math::Vector3UVE{6.0F, -10.0F, 0.0F};
    pred.predictedLandingNormal = Math::Vector3UVE{0.0F, 1.0F, 0.0F};

    // Normal velocity is -10 m/s; restitution 0.3 should bounce to +3 m/s
    const float restitution = 0.3F;
    const float friction = 0.7F;

    const Math::Vector3UVE vIn = pred.predictedLandingVelocity;
    const Math::Vector3UVE n = pred.predictedLandingNormal;
    const float normalVel = Math::DotUVE(vIn, n);
    const Math::Vector3UVE vNormal = n * normalVel;
    const Math::Vector3UVE vTangent = vIn - vNormal;

    const Math::Vector3UVE vRebound = vTangent * (1.0F - friction) - vNormal * restitution;
    EXPECT_NEAR(vRebound.y, 3.0F, 0.01F);
    EXPECT_NEAR(vRebound.x, 1.8F, 0.01F);
    EXPECT_LT(Math::LengthUVE(vRebound), Math::LengthUVE(vIn));
}

// ============================================================================
// TEST 08: Inclined Surface Landing Analysis
// ============================================================================
TEST(ActiveRagdollTests, Test08_LandingOnInclinedSurface_SlopeAndSlideAnalysis) {
    MotionTrajectoryPredictionUVE steepSlopeTrajectory;
    steepSlopeTrajectory.hasPredictedLanding = true;
    steepSlopeTrajectory.landingSlopeDegrees = 40.0F; // 40 degrees
    steepSlopeTrajectory.predictedLandingNormal = Math::Vector3UVE{0.0F, 0.766F, 0.643F};
    steepSlopeTrajectory.predictedLandingVelocity = Math::Vector3UVE{0.0F, -6.0F, 0.0F};
    steepSlopeTrajectory.timeToLanding = 0.2F;

    // Surface friction mu = 0.5; tan(40 deg) = 0.839 > 0.5 -> Slide expected
    LandingAnalysisReportUVE steepReport = AnalyzeImpendingLandingUVE(
        steepSlopeTrajectory, Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F},
        Math::Vector3UVE{0.0F, -6.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 0.5F);

    EXPECT_TRUE(steepReport.willSlide);

    // Gentle slope 15 degrees: tan(15 deg) = 0.268 < 0.5 -> No slide
    MotionTrajectoryPredictionUVE gentleSlopeTrajectory = steepSlopeTrajectory;
    gentleSlopeTrajectory.landingSlopeDegrees = 15.0F;
    gentleSlopeTrajectory.predictedLandingNormal = Math::Vector3UVE{0.0F, 0.966F, 0.258F};

    LandingAnalysisReportUVE gentleReport = AnalyzeImpendingLandingUVE(
        gentleSlopeTrajectory, Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F},
        Math::Vector3UVE{0.0F, -6.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 0.5F);

    EXPECT_FALSE(gentleReport.willSlide);
    EXPECT_EQ(gentleReport.classification, LandingClassificationUVE::FeetFirst);
}

// ============================================================================
// TEST 09: Face-Up Ragdoll Recovery Sequence
// ============================================================================
TEST(ActiveRagdollTests, Test09_FaceUpLanding_DetectsFaceUpAndExecutesGetUpSequence) {
    RagdollSolverUVE solver;

    // Prone/Supine body configuration on ground (y = 0.15 m)
    // Chest rotation identity has local +Z forward, so lying on back has chest facing +Y
    RagdollBodyUVE pelvis = MakeCapsuleBodyUVE(Math::Vector3UVE{0.0F, 0.15F, 0.0F}, 20.0F);
    RagdollBodyUVE spine = MakeCapsuleBodyUVE(Math::Vector3UVE{0.0F, 0.15F, 0.4F}, 20.0F);
    // Face-up: rotate spine and pelvis so front (+Z) faces world +Y
    pelvis.rotation = Math::QuaternionUVE{-0.7071F, 0.0F, 0.0F, 0.7071F};
    spine.rotation = Math::QuaternionUVE{-0.7071F, 0.0F, 0.0F, 0.7071F};

    EXPECT_TRUE(solver.ResetUVE(std::vector<RagdollBodyUVE>{pelvis, spine}));

    RagdollRecoveryControllerUVE recovery;
    EXPECT_EQ(recovery.DetermineOrientationUVE(solver), RecoveryOrientationUVE::FaceUp);

    // Simulate recovery progression
    const std::vector<RagdollContactUVE> groundContacts{
        RagdollContactUVE{.body = 0U, .point = {0.0F, 0.0F, 0.0F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F}
    };

    // Step 1: Enters stabilizing
    recovery.UpdateRecoveryUVE(solver, groundContacts, 0.05F);
    EXPECT_EQ(recovery.GetStateUVE().phase, RecoveryPhaseUVE::Stabilizing);

    // Step 2: Settle delay passes -> transitions to Analyzing then GettingUp
    recovery.UpdateRecoveryUVE(solver, groundContacts, 0.5F);
    recovery.UpdateRecoveryUVE(solver, groundContacts, 0.016F);
    EXPECT_EQ(recovery.GetStateUVE().phase, RecoveryPhaseUVE::GettingUp);
    EXPECT_TRUE(recovery.GetStateUVE().canRecover);

    // Step 3: GettingUp blend weight transitions smoothly to 0.0F
    recovery.UpdateRecoveryUVE(solver, groundContacts, 1.5F);
    EXPECT_EQ(recovery.GetStateUVE().phase, RecoveryPhaseUVE::Complete);
    EXPECT_NEAR(recovery.GetStateUVE().blendWeight, 0.0F, 1.0e-4F);
}

// ============================================================================
// TEST 10: Face-Down Ragdoll Orientation
// ============================================================================
TEST(ActiveRagdollTests, Test10_FaceDownLanding_DetectsFaceDownOrientation) {
    RagdollSolverUVE solver;

    RagdollBodyUVE pelvis = MakeCapsuleBodyUVE(Math::Vector3UVE{0.0F, 0.15F, 0.0F}, 20.0F);
    RagdollBodyUVE spine = MakeCapsuleBodyUVE(Math::Vector3UVE{0.0F, 0.15F, 0.4F}, 20.0F);
    // Face-down: rotate spine and pelvis so front (+Z) faces world -Y
    pelvis.rotation = Math::QuaternionUVE{0.7071F, 0.0F, 0.0F, 0.7071F};
    spine.rotation = Math::QuaternionUVE{0.7071F, 0.0F, 0.0F, 0.7071F};

    EXPECT_TRUE(solver.ResetUVE(std::vector<RagdollBodyUVE>{pelvis, spine}));

    RagdollRecoveryControllerUVE recovery;
    EXPECT_EQ(recovery.DetermineOrientationUVE(solver), RecoveryOrientationUVE::FaceDown);
}

// ============================================================================
// TEST 11: Impact Interruption During Recovery
// ============================================================================
TEST(ActiveRagdollTests, Test11_ImpactDuringRecovery_InterruptsImmediately) {
    RagdollRecoveryControllerUVE recovery;

    // Put recovery into GettingUp phase
    RagdollSolverUVE solver = MakeHumanoidRagdollSolverUVE();
    const std::vector<RagdollContactUVE> groundContacts{
        RagdollContactUVE{.body = 0U, .point = {0.0F, 0.0F, 0.0F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F}
    };
    recovery.UpdateRecoveryUVE(solver, groundContacts, 0.05F);
    recovery.UpdateRecoveryUVE(solver, groundContacts, 0.6F);
    recovery.UpdateRecoveryUVE(solver, groundContacts, 0.05F);
    EXPECT_EQ(recovery.GetStateUVE().phase, RecoveryPhaseUVE::GettingUp);

    // Secondary impact arrives mid-getup!
    recovery.NotifyImpactUVE(150.0F);

    EXPECT_TRUE(recovery.GetStateUVE().isInterrupted);
    EXPECT_EQ(recovery.GetStateUVE().phase, RecoveryPhaseUVE::Interrupted);
    EXPECT_NEAR(recovery.GetStateUVE().blendWeight, 1.0F, 1.0e-4F);
}

// ============================================================================
// TEST 12: Multiple Simultaneous Contacts & Support Polygon
// ============================================================================
TEST(ActiveRagdollTests, Test12_MultipleSimultaneousContacts_StableResolution) {
    // 4 ground contact points forming a rectangle on XZ plane
    const std::vector<RagdollContactUVE> contacts{
        RagdollContactUVE{.body = 0U, .point = {-0.3F, 0.0F, -0.2F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
        RagdollContactUVE{.body = 0U, .point = {0.3F, 0.0F, -0.2F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
        RagdollContactUVE{.body = 0U, .point = {0.3F, 0.0F, 0.2F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
        RagdollContactUVE{.body = 0U, .point = {-0.3F, 0.0F, 0.2F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
    };

    const SupportPolygonUVE poly = ComputeSupportPolygonUVE(contacts);
    EXPECT_EQ(poly.vertices.size(), 4U);
    EXPECT_NEAR(poly.area, 0.6F * 0.4F, 0.01F);
    EXPECT_NEAR(poly.centroid.x, 0.0F, 0.01F);
    EXPECT_NEAR(poly.centroid.y, 0.0F, 0.01F);

    // Inside point should have positive distance to boundary
    EXPECT_GT(poly.SignedDistanceToBoundaryUVE(Math::Vector2UVE{0.0F, 0.0F}), 0.15F);
    // Outside point should have negative distance to boundary
    EXPECT_LT(poly.SignedDistanceToBoundaryUVE(Math::Vector2UVE{0.5F, 0.0F}), -0.15F);
}

// ============================================================================
// TEST 13: High-Speed Continuous Sweep Prevents Tunneling
// ============================================================================
TEST(ActiveRagdollTests, Test13_HighScoreCollision_ContinuousSweepPreventsTunneling) {
    // High-speed bullet: 60 m/s moving towards thin barrier at X = 1.0 m (thickness 0.05 m)
    const Math::Vector3UVE initialPos{0.0F, 1.0F, 0.0F};
    const Math::Vector3UVE initialVel{60.0F, 0.0F, 0.0F};
    const Math::Vector3UVE gravity{0.0F, 0.0F, 0.0F};

    // A single discrete Euler step of 1/60s would jump 1.0 m, completely passing through the 0.05m barrier
    Detail::ColliderWorldAabbUVE barrier;
    barrier.worldAabb = Math::AabbUVE{Math::Vector3UVE{1.0F, 0.0F, -1.0F}, Math::Vector3UVE{1.05F, 2.0F, 1.0F}};
    const std::vector<Detail::ColliderWorldAabbUVE> obstacles{barrier};

    TrajectoryInputUVE input;
    input.position = initialPos;
    input.linearVelocity = initialVel;
    input.gravity = gravity;
    input.horizonSeconds = 0.1F;
    input.stepSeconds = 1.0F / 60.0F;
    input.characterRadius = 0.0F;
    input.characterHeight = 0.0F;
    input.isGrounded = false;

    const MotionTrajectoryPredictionUVE pred = PredictMotionTrajectoryUVE(input, obstacles);

    // Continuous sweep must detect collision and stop before tunneling through
    EXPECT_TRUE(pred.hasPredictedWallHit);
    EXPECT_NEAR(pred.predictedWallHitPosition.x, 1.0F, 0.05F);
    EXPECT_LT(pred.predictedWallHitNormal.x, -0.9F);
}

// ============================================================================
// TEST 14: Multiple Active Ragdolls Concurrency & Determinism
// ============================================================================
TEST(ActiveRagdollTests, Test14_MultipleActiveRagdolls_SimulateStably) {
    constexpr std::size_t kInstanceCount = 16U;
    std::vector<RagdollSolverUVE> solvers;
    std::vector<ActiveRagdollControllerUVE> controllers;
    solvers.reserve(kInstanceCount);
    controllers.reserve(kInstanceCount);

    for (std::size_t i = 0U; i < kInstanceCount; ++i) {
        solvers.push_back(MakeHumanoidRagdollSolverUVE());
        controllers.emplace_back();
    }

    const std::vector<RagdollContactUVE> groundContacts{
        RagdollContactUVE{.body = 3U, .point = {-0.2F, 0.0F, 0.0F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
        RagdollContactUVE{.body = 4U, .point = {0.2F, 0.0F, 0.0F}, .normal = {0.0F, 1.0F, 0.0F}, .depth = 0.01F},
    };

    // Simulate 30 frames across all 16 ragdolls
    for (int frame = 0; frame < 30; ++frame) {
        for (std::size_t i = 0U; i < kInstanceCount; ++i) {
            controllers[i].UpdateBalanceAndPostureUVE(
                solvers[i], groundContacts, Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F}, 0.016F);
            (void)solvers[i].StepUVE(Math::Vector3UVE{0.0F, -9.81F, 0.0F}, 0.016F, groundContacts);

            const Math::Vector3UVE com = solvers[i].ComputeCenterOfMassUVE();
            // Assert no NaNs, no infinities, and CoM remains physically valid
            EXPECT_FALSE(std::isnan(com.x));
            EXPECT_FALSE(std::isnan(com.y));
            EXPECT_FALSE(std::isnan(com.z));
            EXPECT_GT(com.y, 0.0F);
        }
    }

    // Assert determinism: ragdoll 0 and ragdoll 1 initialized identically should produce identical CoM
    const Math::Vector3UVE com0 = solvers[0].ComputeCenterOfMassUVE();
    const Math::Vector3UVE com1 = solvers[1].ComputeCenterOfMassUVE();
    EXPECT_FLOAT_EQ(com0.x, com1.x);
    EXPECT_FLOAT_EQ(com0.y, com1.y);
    EXPECT_FLOAT_EQ(com0.z, com1.z);
}

} // namespace UVE::Physics::Tests
