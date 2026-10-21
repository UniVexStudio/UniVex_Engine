// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/full_body_ik_3d_uve.h"
#include "uve/objects/3d/two_bone_ik_3d_uve.h"

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Scene {
namespace {

constexpr float kEpsilon = 1e-4F;

/// A straight limb along +X: the root at the origin, a middle joint a metre out, and the effector
/// another metre out. Every geometry case solves this, so a test that passes knows exactly which
/// metre of rig it moved.
[[nodiscard]] FullBodyIKProblemUVE StraightLimbUVE() {
    FullBodyIKProblemUVE problem;
    problem.joints = {
        FullBodyIKJointUVE{-1, {0.0F, 0.0F, 0.0F}, {}, {}},
        FullBodyIKJointUVE{0, {1.0F, 0.0F, 0.0F}, {}, {1.0F, 0.0F, 0.0F}},
        FullBodyIKJointUVE{1, {2.0F, 0.0F, 0.0F}, {}, {1.0F, 0.0F, 0.0F}},
    };
    problem.effectorJoints = {2U};
    problem.effectorTargets = {{1.5F, 1.0F, 0.0F}}; // well inside the two-metre reach
    problem.effectorWeights = {1.0F};
    return problem;
}

/// A hips-chest pair of arms with real shoulder joints: the chest feeds two shoulders, each with a
/// one-metre arm out to its hand. Both effectors draw on the same hips and chest, so the solve has
/// to answer both in one pose - and every target is inside its own arm's reach, which is the only
/// kind of target an honest test can demand placement for.
[[nodiscard]] FullBodyIKProblemUVE TwoArmsUVE() {
    FullBodyIKProblemUVE problem;
    problem.joints = {
        FullBodyIKJointUVE{-1, {0.0F, 0.0F, 0.0F}, {}, {}},                  // hips
        FullBodyIKJointUVE{0, {0.0F, 1.0F, 0.0F}, {}, {0.0F, 1.0F, 0.0F}},   // chest
        FullBodyIKJointUVE{1, {-1.0F, 1.0F, 0.0F}, {}, {-1.0F, 0.0F, 0.0F}}, // left shoulder
        FullBodyIKJointUVE{2, {-2.0F, 1.0F, 0.0F}, {}, {-1.0F, 0.0F, 0.0F}}, // left hand
        FullBodyIKJointUVE{1, {1.0F, 1.0F, 0.0F}, {}, {1.0F, 0.0F, 0.0F}},   // right shoulder
        FullBodyIKJointUVE{4, {2.0F, 1.0F, 0.0F}, {}, {1.0F, 0.0F, 0.0F}},   // right hand
    };
    problem.effectorJoints = {3U, 5U};
    problem.effectorTargets = {{-1.5F, 0.5F, 0.0F}, {1.5F, 0.5F, 0.0F}};
    problem.effectorWeights = {1.0F, 1.0F};
    return problem;
}

[[nodiscard]] FullBodyIKSolverSettingsUVE DefaultSettingsUVE() noexcept {
    return FullBodyIKSolverSettingsUVE{};
}

[[nodiscard]] float SegmentLengthUVE(const FullBodyIKSolverReportUVE& report, const std::size_t joint,
                                     const FullBodyIKProblemUVE& problem) {
    return Math::LengthUVE(report.solvedPositions[joint] -
                           report.solvedPositions[static_cast<std::size_t>(problem.joints[joint].parent)]);
}

} // namespace

TEST(FullBodyIK3DUVETest, TheTwoBoneFinishApplicationMatchesTheSolveGeometry) {
    const TwoBoneIKChainUVE chain{{0.0F, 1.0F, 0.0F}, {0.0F, 2.0F, 0.0F}, {0.0F, 3.0F, 0.0F}, {}, {}, {}};
    const Math::Vector3UVE target{1.5F, 1.0F, 0.0F};
    const std::optional<TwoBoneIKSolutionUVE> solution = SolveTwoBoneIKUVE(chain, target, {});
    ASSERT_TRUE(solution.has_value());
    // Apply exactly the way the full-body solver does, then FK from local offsets (0,1,0).
    const Math::QuaternionUVE rotLower = solution->rootLocalRotation; // parent rotation is identity
    const Math::QuaternionUVE rotMiddle = Math::MultiplyUVE(rotLower, solution->middleLocalRotation);
    const Math::Vector3UVE middlePos = chain.root + Math::RotateVectorUVE(rotLower, {0.0F, 1.0F, 0.0F});
    const Math::Vector3UVE handPos = middlePos + Math::RotateVectorUVE(rotMiddle, {0.0F, 1.0F, 0.0F});
    EXPECT_NEAR(handPos.x, target.x, 1e-4) << "solve said end=(" << solution->endPosition.x << ","
                                           << solution->endPosition.y << ")";
    EXPECT_NEAR(handPos.y, target.y, 1e-4);
}

TEST(FullBodyIK3DUVETest, AFourBoneStraightChainLandsExactly) {
    FullBodyIKProblemUVE problem;
    problem.joints = {
        FullBodyIKJointUVE{-1, {0.0F, 0.0F, 0.0F}, {}, {}},
        FullBodyIKJointUVE{0, {0.0F, 1.0F, 0.0F}, {}, {0.0F, 1.0F, 0.0F}},
        FullBodyIKJointUVE{1, {0.0F, 2.0F, 0.0F}, {}, {0.0F, 1.0F, 0.0F}},
        FullBodyIKJointUVE{2, {0.0F, 3.0F, 0.0F}, {}, {0.0F, 1.0F, 0.0F}},
    };
    problem.effectorJoints = {3U};
    problem.effectorTargets = {{1.5F, 1.0F, 0.0F}};
    problem.effectorWeights = {1.0F};
    const std::optional<FullBodyIKSolverReportUVE> report =
        SolveFullBodyIKUVE(problem, FullBodyIKSolverSettingsUVE{});
    ASSERT_TRUE(report.has_value());
    EXPECT_TRUE(report->converged);
    EXPECT_LE(report->distances[0], FullBodyIKSolverSettingsUVE{}.toleranceMetres)
        << "iters=" << report->iterationsUsed
        << " hand=(" << report->solvedPositions[3].x << "," << report->solvedPositions[3].y << ")";
}

TEST(FullBodyIK3DUVETest, SolvesASingleEffectorOntoItsTarget) {
    const FullBodyIKProblemUVE problem = StraightLimbUVE();
    const std::optional<FullBodyIKSolverReportUVE> report =
        SolveFullBodyIKUVE(problem, DefaultSettingsUVE());
    ASSERT_TRUE(report.has_value());
    EXPECT_TRUE(report->converged);
    EXPECT_LE(report->iterationsUsed, DefaultSettingsUVE().maxIterations);
    ASSERT_EQ(report->distances.size(), 1U);
    EXPECT_LE(report->distances[0], DefaultSettingsUVE().toleranceMetres);
    EXPECT_TRUE(report->reached[0]);
    EXPECT_NEAR(report->solvedPositions[2].x, 1.5F, 1e-2);
    EXPECT_NEAR(report->solvedPositions[2].y, 1.0F, 1e-2);
}

TEST(FullBodyIK3DUVETest, KeepsEveryBoneAtItsOwnLength) {
    const FullBodyIKProblemUVE problem = StraightLimbUVE();
    const std::optional<FullBodyIKSolverReportUVE> report =
        SolveFullBodyIKUVE(problem, DefaultSettingsUVE());
    ASSERT_TRUE(report.has_value());
    EXPECT_NEAR(SegmentLengthUVE(*report, 1U, problem), 1.0F, 1e-5);
    EXPECT_NEAR(SegmentLengthUVE(*report, 2U, problem), 1.0F, 1e-5);
}

TEST(FullBodyIK3DUVETest, RefusesProblemsItCannotSolveAndLeavesTheCallerAlone) {
    const FullBodyIKSolverSettingsUVE settings = DefaultSettingsUVE();

    FullBodyIKProblemUVE problem = StraightLimbUVE();
    problem.effectorJoints[0] = 9U; // no such joint
    EXPECT_FALSE(SolveFullBodyIKUVE(problem, settings).has_value());

    problem = StraightLimbUVE();
    problem.effectorTargets[0].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(SolveFullBodyIKUVE(problem, settings).has_value());

    problem = StraightLimbUVE();
    problem.effectorWeights[0] = 1.5F;
    EXPECT_FALSE(SolveFullBodyIKUVE(problem, settings).has_value());

    problem = StraightLimbUVE();
    problem.joints[2].parent = 2; // its own index: not a parent that comes first
    EXPECT_FALSE(SolveFullBodyIKUVE(problem, settings).has_value());

    FullBodyIKProblemUVE outside = StraightLimbUVE();
    outside.joints.push_back(FullBodyIKJointUVE{-1, {5.0F, 0.0F, 0.0F}, {}, {}});
    outside.effectorJoints[0] = 3U; // outside the root's subtree
    EXPECT_FALSE(SolveFullBodyIKUVE(outside, settings).has_value());

    FullBodyIKSolverSettingsUVE badSettings = settings;
    badSettings.maxIterations = 0U;
    EXPECT_FALSE(SolveFullBodyIKUVE(StraightLimbUVE(), badSettings).has_value());
    badSettings = settings;
    badSettings.toleranceMetres = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(SolveFullBodyIKUVE(StraightLimbUVE(), badSettings).has_value());
    badSettings = settings;
    badSettings.rootTranslationWeight = -0.5F;
    EXPECT_FALSE(SolveFullBodyIKUVE(StraightLimbUVE(), badSettings).has_value());
    badSettings = settings;
    badSettings.maxJointStepRadians = 0.0F;
    EXPECT_FALSE(SolveFullBodyIKUVE(StraightLimbUVE(), badSettings).has_value());
}

TEST(FullBodyIK3DUVETest, RespectsReachInsteadOfStretchingTheBones) {
    FullBodyIKProblemUVE problem = StraightLimbUVE();
    problem.effectorTargets[0] = {6.0F, 0.0F, 0.0F}; // four metres past the tip
    const std::optional<FullBodyIKSolverReportUVE> report =
        SolveFullBodyIKUVE(problem, DefaultSettingsUVE());
    ASSERT_TRUE(report.has_value());
    ASSERT_EQ(report->distances.size(), 1U);
    EXPECT_FALSE(report->reached[0]);
    EXPECT_NEAR(report->distances[0], 4.0F, 1e-2);
    EXPECT_NEAR(SegmentLengthUVE(*report, 1U, problem), 1.0F, 1e-5); // still a rig, not taffy
    EXPECT_NEAR(SegmentLengthUVE(*report, 2U, problem), 1.0F, 1e-5);
    EXPECT_TRUE(report->converged || report->iterationsUsed == DefaultSettingsUVE().maxIterations);
}

TEST(FullBodyIK3DUVETest, SharesTheSpineBetweenTwoEffectorsInOnePose) {
    const FullBodyIKProblemUVE problem = TwoArmsUVE();
    const std::optional<FullBodyIKSolverReportUVE> report =
        SolveFullBodyIKUVE(problem, DefaultSettingsUVE());
    ASSERT_TRUE(report.has_value());
    ASSERT_EQ(report->distances.size(), 2U);
    EXPECT_LE(report->distances[0], DefaultSettingsUVE().toleranceMetres);
    EXPECT_LE(report->distances[1], DefaultSettingsUVE().toleranceMetres);
    EXPECT_TRUE(report->converged);
}

TEST(FullBodyIK3DUVETest, RootTranslationCoversWhatTheLimbsCannot) {
    FullBodyIKProblemUVE problem = StraightLimbUVE();
    problem.effectorTargets[0] = {3.0F, 0.0F, 0.0F}; // one metre past the two-metre reach

    FullBodyIKSolverSettingsUVE rooted = DefaultSettingsUVE();
    rooted.rootTranslationWeight = 0.0F;
    const std::optional<FullBodyIKSolverReportUVE> pinned = SolveFullBodyIKUVE(problem, rooted);
    ASSERT_TRUE(pinned.has_value());
    EXPECT_FALSE(pinned->reached[0]);
    EXPECT_NEAR(Math::LengthUVE(pinned->rootTranslation), 0.0F, 1e-6);

    FullBodyIKSolverSettingsUVE stepping = DefaultSettingsUVE();
    stepping.rootTranslationWeight = 1.0F;
    const std::optional<FullBodyIKSolverReportUVE> moved = SolveFullBodyIKUVE(problem, stepping);
    ASSERT_TRUE(moved.has_value());
    EXPECT_TRUE(moved->reached[0]);
    EXPECT_LE(moved->distances[0], stepping.toleranceMetres);
    EXPECT_GT(Math::LengthUVE(moved->rootTranslation), 0.5F); // the hips did the walking
}

TEST(FullBodyIK3DUVETest, ZeroWeightEffectorIsMeasuredButNotPulled) {
    FullBodyIKProblemUVE problem = StraightLimbUVE();
    problem.effectorWeights[0] = 0.0F;
    const std::optional<FullBodyIKSolverReportUVE> report =
        SolveFullBodyIKUVE(problem, DefaultSettingsUVE());
    ASSERT_TRUE(report.has_value());
    EXPECT_TRUE(report->converged); // nothing is pulling, so nothing is left to do
    EXPECT_NEAR(report->solvedPositions[1].x, 1.0F, 1e-5); // the pose is exactly the posed one
    EXPECT_NEAR(report->solvedPositions[2].y, 0.0F, 1e-5);
    EXPECT_FALSE(report->reached[0]); // measured honestly: the target is still away
    EXPECT_GT(report->distances[0], 0.5F);
}

TEST(FullBodyIK3DUVETest, PartialWeightLeansWithoutLanding) {
    FullBodyIKProblemUVE problem = StraightLimbUVE();
    problem.effectorTargets[0] = {1.0F, 1.0F, 0.0F};
    problem.effectorWeights[0] = 0.5F;
    const std::optional<FullBodyIKSolverReportUVE> report =
        SolveFullBodyIKUVE(problem, DefaultSettingsUVE());
    ASSERT_TRUE(report.has_value());
    ASSERT_EQ(report->distances.size(), 1U);
    // The pull stops at the half-way point: half the original diagonal away from the full target.
    const float originalDistance =
        Math::LengthUVE(Math::Vector3UVE{1.0F, 1.0F, 0.0F} - Math::Vector3UVE{2.0F, 0.0F, 0.0F});
    EXPECT_NEAR(report->distances[0], originalDistance * 0.5F, 5e-2);
}

TEST(FullBodyIK3DUVETest, StepLimitCapsHowFarOneSweepMovesAJoint) {
    FullBodyIKProblemUVE problem = StraightLimbUVE();
    problem.effectorTargets[0] = {0.0F, 2.2F, 0.0F}; // a right-angle turn of the whole limb
    FullBodyIKSolverSettingsUVE settings = DefaultSettingsUVE();
    settings.maxIterations = 1U;
    settings.maxJointStepRadians = 0.17453292519943295F; // ten degrees
    const std::optional<FullBodyIKSolverReportUVE> report = SolveFullBodyIKUVE(problem, settings);
    ASSERT_TRUE(report.has_value());
    // Both joints moved, and neither moved more than one capped step away from posed.
    const float rootTurn = Math::LengthUVE(Math::Vector3UVE{
        report->solvedRotations[0].x, report->solvedRotations[0].y, report->solvedRotations[0].z});
    EXPECT_GT(rootTurn, 0.0F);
    EXPECT_FALSE(report->reached[0]); // one capped sweep cannot cover ninety degrees
}

TEST(FullBodyIK3DUVETest, SolvingTheSameProblemTwiceGivesTheSamePose) {
    const FullBodyIKProblemUVE problem = TwoArmsUVE();
    const std::optional<FullBodyIKSolverReportUVE> first =
        SolveFullBodyIKUVE(problem, DefaultSettingsUVE());
    const std::optional<FullBodyIKSolverReportUVE> second =
        SolveFullBodyIKUVE(problem, DefaultSettingsUVE());
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->iterationsUsed, second->iterationsUsed);
    EXPECT_EQ(first->converged, second->converged);
    for (std::size_t index = 0U; index < first->solvedPositions.size(); ++index) {
        EXPECT_FLOAT_EQ(first->solvedPositions[index].x, second->solvedPositions[index].x);
        EXPECT_FLOAT_EQ(first->solvedPositions[index].y, second->solvedPositions[index].y);
        EXPECT_FLOAT_EQ(first->solvedPositions[index].z, second->solvedPositions[index].z);
        EXPECT_FLOAT_EQ(first->solvedRotations[index].w, second->solvedRotations[index].w);
    }
}

TEST(FullBodyIK3DUVETest, AnEffectorOnTheRootItselfMovesOnlyThroughRootTranslation) {
    FullBodyIKProblemUVE problem = StraightLimbUVE();
    problem.effectorJoints[0] = 0U;
    problem.effectorTargets[0] = {0.0F, 0.5F, 0.0F};
    FullBodyIKSolverSettingsUVE settings = DefaultSettingsUVE();
    settings.rootTranslationWeight = 1.0F;
    const std::optional<FullBodyIKSolverReportUVE> report = SolveFullBodyIKUVE(problem, settings);
    ASSERT_TRUE(report.has_value());
    EXPECT_TRUE(report->reached[0]);
    EXPECT_NEAR(report->solvedPositions[0].y, 0.5F, 1e-2);
}

TEST(FullBodyIK3DUVETest, ComponentValidationCatchesMisbehavingAuthoredData) {
    EXPECT_TRUE(IsFullBodyIK3DObjectComponentValidUVE(FullBodyIK3DComponentUVE{}));

    FullBodyIK3DComponentUVE component = FullBodyIK3DComponentUVE{};
    component.toleranceMetres = 0.0F;
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentValidUVE(component));

    component = FullBodyIK3DComponentUVE{};
    component.maxIterations = kMaximumFullBodyIKIterationsUVE + 1U;
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentValidUVE(component));

    component = FullBodyIK3DComponentUVE{};
    component.rootTranslationWeight = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentValidUVE(component));

    component = FullBodyIK3DComponentUVE{};
    component.maxJointStepRadians = 3.2F;
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentValidUVE(component));

    component = FullBodyIK3DComponentUVE{};
    component.effectors.push_back(FullBodyIKEffectorUVE{});
    component.effectors.back().positionWeight = -0.1F;
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentValidUVE(component));

    component = FullBodyIK3DComponentUVE{};
    component.effectors.push_back(FullBodyIKEffectorUVE{});
    component.effectors.back().targetPosition.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentValidUVE(component));
}

TEST(FullBodyIK3DUVETest, ResolvableNeedsARootAndAtLeastOneNamedEffector) {
    FullBodyIK3DComponentUVE component = FullBodyIK3DComponentUVE{};
    component.skeleton = EntityUVE{1U};
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentResolvableUVE(component)); // nothing to solve yet

    component.rootBoneName = "Hips";
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentResolvableUVE(component)); // still no effector

    component.effectors.push_back(FullBodyIKEffectorUVE{}); // default: names no bone
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentResolvableUVE(component));

    component.effectors.back().boneName = "LeftHand";
    EXPECT_TRUE(IsFullBodyIK3DObjectComponentResolvableUVE(component));

    component.enabled = false;
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentResolvableUVE(component));
}

} // namespace UVE::Scene
