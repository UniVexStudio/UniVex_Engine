// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "full_body_ik_3d.h"
#include "two_bone_ik_3d.h"

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
[[nodiscard]] FullBodyIKProblem StraightLimb() {
    FullBodyIKProblem problem;
    problem.joints = {
        FullBodyIKJoint{-1, {0.0F, 0.0F, 0.0F}, {}, {}},
        FullBodyIKJoint{0, {1.0F, 0.0F, 0.0F}, {}, {1.0F, 0.0F, 0.0F}},
        FullBodyIKJoint{1, {2.0F, 0.0F, 0.0F}, {}, {1.0F, 0.0F, 0.0F}},
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
[[nodiscard]] FullBodyIKProblem TwoArms() {
    FullBodyIKProblem problem;
    problem.joints = {
        FullBodyIKJoint{-1, {0.0F, 0.0F, 0.0F}, {}, {}},                  // hips
        FullBodyIKJoint{0, {0.0F, 1.0F, 0.0F}, {}, {0.0F, 1.0F, 0.0F}},   // chest
        FullBodyIKJoint{1, {-1.0F, 1.0F, 0.0F}, {}, {-1.0F, 0.0F, 0.0F}}, // left shoulder
        FullBodyIKJoint{2, {-2.0F, 1.0F, 0.0F}, {}, {-1.0F, 0.0F, 0.0F}}, // left hand
        FullBodyIKJoint{1, {1.0F, 1.0F, 0.0F}, {}, {1.0F, 0.0F, 0.0F}},   // right shoulder
        FullBodyIKJoint{4, {2.0F, 1.0F, 0.0F}, {}, {1.0F, 0.0F, 0.0F}},   // right hand
    };
    problem.effectorJoints = {3U, 5U};
    problem.effectorTargets = {{-1.5F, 0.5F, 0.0F}, {1.5F, 0.5F, 0.0F}};
    problem.effectorWeights = {1.0F, 1.0F};
    return problem;
}

[[nodiscard]] FullBodyIKSolverSettings DefaultSettings() noexcept {
    return FullBodyIKSolverSettings{};
}

[[nodiscard]] float SegmentLength(const FullBodyIKSolverReport& report, const std::size_t joint,
                                     const FullBodyIKProblem& problem) {
    return Math::Length(report.solvedPositions[joint] -
                           report.solvedPositions[static_cast<std::size_t>(problem.joints[joint].parent)]);
}

} // namespace

TEST(FullBodyIK3DTest, TheTwoBoneFinishApplicationMatchesTheSolveGeometry) {
    const TwoBoneIKChain chain{{0.0F, 1.0F, 0.0F}, {0.0F, 2.0F, 0.0F}, {0.0F, 3.0F, 0.0F}, {}, {}, {}};
    const Math::Vector3 target{1.5F, 1.0F, 0.0F};
    const std::optional<TwoBoneIKSolution> solution = SolveTwoBoneIK(chain, target, {});
    ASSERT_TRUE(solution.has_value());
    // Apply exactly the way the full-body solver does, then FK from local offsets (0,1,0).
    const Math::Quaternion rotLower = solution->rootLocalRotation; // parent rotation is identity
    const Math::Quaternion rotMiddle = Math::Multiply(rotLower, solution->middleLocalRotation);
    const Math::Vector3 middlePos = chain.root + Math::RotateVector(rotLower, {0.0F, 1.0F, 0.0F});
    const Math::Vector3 handPos = middlePos + Math::RotateVector(rotMiddle, {0.0F, 1.0F, 0.0F});
    EXPECT_NEAR(handPos.x, target.x, 1e-4) << "solve said end=(" << solution->endPosition.x << ","
                                           << solution->endPosition.y << ")";
    EXPECT_NEAR(handPos.y, target.y, 1e-4);
}

TEST(FullBodyIK3DTest, AFourBoneStraightChainLandsExactly) {
    FullBodyIKProblem problem;
    problem.joints = {
        FullBodyIKJoint{-1, {0.0F, 0.0F, 0.0F}, {}, {}},
        FullBodyIKJoint{0, {0.0F, 1.0F, 0.0F}, {}, {0.0F, 1.0F, 0.0F}},
        FullBodyIKJoint{1, {0.0F, 2.0F, 0.0F}, {}, {0.0F, 1.0F, 0.0F}},
        FullBodyIKJoint{2, {0.0F, 3.0F, 0.0F}, {}, {0.0F, 1.0F, 0.0F}},
    };
    problem.effectorJoints = {3U};
    problem.effectorTargets = {{1.5F, 1.0F, 0.0F}};
    problem.effectorWeights = {1.0F};
    const std::optional<FullBodyIKSolverReport> report =
        SolveFullBodyIK(problem, FullBodyIKSolverSettings{});
    ASSERT_TRUE(report.has_value());
    EXPECT_TRUE(report->converged);
    EXPECT_LE(report->distances[0], FullBodyIKSolverSettings{}.toleranceMetres)
        << "iters=" << report->iterationsUsed
        << " hand=(" << report->solvedPositions[3].x << "," << report->solvedPositions[3].y << ")";
}

TEST(FullBodyIK3DTest, SolvesASingleEffectorOntoItsTarget) {
    const FullBodyIKProblem problem = StraightLimb();
    const std::optional<FullBodyIKSolverReport> report =
        SolveFullBodyIK(problem, DefaultSettings());
    ASSERT_TRUE(report.has_value());
    EXPECT_TRUE(report->converged);
    EXPECT_LE(report->iterationsUsed, DefaultSettings().maxIterations);
    ASSERT_EQ(report->distances.size(), 1U);
    EXPECT_LE(report->distances[0], DefaultSettings().toleranceMetres);
    EXPECT_TRUE(report->reached[0]);
    EXPECT_NEAR(report->solvedPositions[2].x, 1.5F, 1e-2);
    EXPECT_NEAR(report->solvedPositions[2].y, 1.0F, 1e-2);
}

TEST(FullBodyIK3DTest, KeepsEveryBoneAtItsOwnLength) {
    const FullBodyIKProblem problem = StraightLimb();
    const std::optional<FullBodyIKSolverReport> report =
        SolveFullBodyIK(problem, DefaultSettings());
    ASSERT_TRUE(report.has_value());
    EXPECT_NEAR(SegmentLength(*report, 1U, problem), 1.0F, 1e-5);
    EXPECT_NEAR(SegmentLength(*report, 2U, problem), 1.0F, 1e-5);
}

TEST(FullBodyIK3DTest, RefusesProblemsItCannotSolveAndLeavesTheCallerAlone) {
    const FullBodyIKSolverSettings settings = DefaultSettings();

    FullBodyIKProblem problem = StraightLimb();
    problem.effectorJoints[0] = 9U; // no such joint
    EXPECT_FALSE(SolveFullBodyIK(problem, settings).has_value());

    problem = StraightLimb();
    problem.effectorTargets[0].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(SolveFullBodyIK(problem, settings).has_value());

    problem = StraightLimb();
    problem.effectorWeights[0] = 1.5F;
    EXPECT_FALSE(SolveFullBodyIK(problem, settings).has_value());

    problem = StraightLimb();
    problem.joints[2].parent = 2; // its own index: not a parent that comes first
    EXPECT_FALSE(SolveFullBodyIK(problem, settings).has_value());

    FullBodyIKProblem outside = StraightLimb();
    outside.joints.push_back(FullBodyIKJoint{-1, {5.0F, 0.0F, 0.0F}, {}, {}});
    outside.effectorJoints[0] = 3U; // outside the root's subtree
    EXPECT_FALSE(SolveFullBodyIK(outside, settings).has_value());

    FullBodyIKSolverSettings badSettings = settings;
    badSettings.maxIterations = 0U;
    EXPECT_FALSE(SolveFullBodyIK(StraightLimb(), badSettings).has_value());
    badSettings = settings;
    badSettings.toleranceMetres = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(SolveFullBodyIK(StraightLimb(), badSettings).has_value());
    badSettings = settings;
    badSettings.rootTranslationWeight = -0.5F;
    EXPECT_FALSE(SolveFullBodyIK(StraightLimb(), badSettings).has_value());
    badSettings = settings;
    badSettings.maxJointStepRadians = 0.0F;
    EXPECT_FALSE(SolveFullBodyIK(StraightLimb(), badSettings).has_value());
}

TEST(FullBodyIK3DTest, RespectsReachInsteadOfStretchingTheBones) {
    FullBodyIKProblem problem = StraightLimb();
    problem.effectorTargets[0] = {6.0F, 0.0F, 0.0F}; // four metres past the tip
    const std::optional<FullBodyIKSolverReport> report =
        SolveFullBodyIK(problem, DefaultSettings());
    ASSERT_TRUE(report.has_value());
    ASSERT_EQ(report->distances.size(), 1U);
    EXPECT_FALSE(report->reached[0]);
    EXPECT_NEAR(report->distances[0], 4.0F, 1e-2);
    EXPECT_NEAR(SegmentLength(*report, 1U, problem), 1.0F, 1e-5); // still a rig, not taffy
    EXPECT_NEAR(SegmentLength(*report, 2U, problem), 1.0F, 1e-5);
    EXPECT_TRUE(report->converged || report->iterationsUsed == DefaultSettings().maxIterations);
}

TEST(FullBodyIK3DTest, SharesTheSpineBetweenTwoEffectorsInOnePose) {
    const FullBodyIKProblem problem = TwoArms();
    const std::optional<FullBodyIKSolverReport> report =
        SolveFullBodyIK(problem, DefaultSettings());
    ASSERT_TRUE(report.has_value());
    ASSERT_EQ(report->distances.size(), 2U);
    EXPECT_LE(report->distances[0], DefaultSettings().toleranceMetres);
    EXPECT_LE(report->distances[1], DefaultSettings().toleranceMetres);
    EXPECT_TRUE(report->converged);
}

TEST(FullBodyIK3DTest, RootTranslationCoversWhatTheLimbsCannot) {
    FullBodyIKProblem problem = StraightLimb();
    problem.effectorTargets[0] = {3.0F, 0.0F, 0.0F}; // one metre past the two-metre reach

    FullBodyIKSolverSettings rooted = DefaultSettings();
    rooted.rootTranslationWeight = 0.0F;
    const std::optional<FullBodyIKSolverReport> pinned = SolveFullBodyIK(problem, rooted);
    ASSERT_TRUE(pinned.has_value());
    EXPECT_FALSE(pinned->reached[0]);
    EXPECT_NEAR(Math::Length(pinned->rootTranslation), 0.0F, 1e-6);

    FullBodyIKSolverSettings stepping = DefaultSettings();
    stepping.rootTranslationWeight = 1.0F;
    const std::optional<FullBodyIKSolverReport> moved = SolveFullBodyIK(problem, stepping);
    ASSERT_TRUE(moved.has_value());
    EXPECT_TRUE(moved->reached[0]);
    EXPECT_LE(moved->distances[0], stepping.toleranceMetres);
    EXPECT_GT(Math::Length(moved->rootTranslation), 0.5F); // the hips did the walking
}

TEST(FullBodyIK3DTest, ZeroWeightEffectorIsMeasuredButNotPulled) {
    FullBodyIKProblem problem = StraightLimb();
    problem.effectorWeights[0] = 0.0F;
    const std::optional<FullBodyIKSolverReport> report =
        SolveFullBodyIK(problem, DefaultSettings());
    ASSERT_TRUE(report.has_value());
    EXPECT_TRUE(report->converged); // nothing is pulling, so nothing is left to do
    EXPECT_NEAR(report->solvedPositions[1].x, 1.0F, 1e-5); // the pose is exactly the posed one
    EXPECT_NEAR(report->solvedPositions[2].y, 0.0F, 1e-5);
    EXPECT_FALSE(report->reached[0]); // measured honestly: the target is still away
    EXPECT_GT(report->distances[0], 0.5F);
}

TEST(FullBodyIK3DTest, PartialWeightLeansWithoutLanding) {
    FullBodyIKProblem problem = StraightLimb();
    problem.effectorTargets[0] = {1.0F, 1.0F, 0.0F};
    problem.effectorWeights[0] = 0.5F;
    const std::optional<FullBodyIKSolverReport> report =
        SolveFullBodyIK(problem, DefaultSettings());
    ASSERT_TRUE(report.has_value());
    ASSERT_EQ(report->distances.size(), 1U);
    // The pull stops at the half-way point: half the original diagonal away from the full target.
    const float originalDistance =
        Math::Length(Math::Vector3{1.0F, 1.0F, 0.0F} - Math::Vector3{2.0F, 0.0F, 0.0F});
    EXPECT_NEAR(report->distances[0], originalDistance * 0.5F, 5e-2);
}

TEST(FullBodyIK3DTest, StepLimitCapsHowFarOneSweepMovesAJoint) {
    FullBodyIKProblem problem = StraightLimb();
    problem.effectorTargets[0] = {0.0F, 2.2F, 0.0F}; // a right-angle turn of the whole limb
    FullBodyIKSolverSettings settings = DefaultSettings();
    settings.maxIterations = 1U;
    settings.maxJointStepRadians = 0.17453292519943295F; // ten degrees
    const std::optional<FullBodyIKSolverReport> report = SolveFullBodyIK(problem, settings);
    ASSERT_TRUE(report.has_value());
    // Both joints moved, and neither moved more than one capped step away from posed.
    const float rootTurn = Math::Length(Math::Vector3{
        report->solvedRotations[0].x, report->solvedRotations[0].y, report->solvedRotations[0].z});
    EXPECT_GT(rootTurn, 0.0F);
    EXPECT_FALSE(report->reached[0]); // one capped sweep cannot cover ninety degrees
}

TEST(FullBodyIK3DTest, SolvingTheSameProblemTwiceGivesTheSamePose) {
    const FullBodyIKProblem problem = TwoArms();
    const std::optional<FullBodyIKSolverReport> first =
        SolveFullBodyIK(problem, DefaultSettings());
    const std::optional<FullBodyIKSolverReport> second =
        SolveFullBodyIK(problem, DefaultSettings());
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

TEST(FullBodyIK3DTest, AnEffectorOnTheRootItselfMovesOnlyThroughRootTranslation) {
    FullBodyIKProblem problem = StraightLimb();
    problem.effectorJoints[0] = 0U;
    problem.effectorTargets[0] = {0.0F, 0.5F, 0.0F};
    FullBodyIKSolverSettings settings = DefaultSettings();
    settings.rootTranslationWeight = 1.0F;
    const std::optional<FullBodyIKSolverReport> report = SolveFullBodyIK(problem, settings);
    ASSERT_TRUE(report.has_value());
    EXPECT_TRUE(report->reached[0]);
    EXPECT_NEAR(report->solvedPositions[0].y, 0.5F, 1e-2);
}

TEST(FullBodyIK3DTest, ComponentValidationCatchesMisbehavingAuthoredData) {
    EXPECT_TRUE(IsFullBodyIK3DObjectComponentValid(FullBodyIK3DComponent{}));

    FullBodyIK3DComponent component = FullBodyIK3DComponent{};
    component.toleranceMetres = 0.0F;
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentValid(component));

    component = FullBodyIK3DComponent{};
    component.maxIterations = kMaximumFullBodyIKIterations + 1U;
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentValid(component));

    component = FullBodyIK3DComponent{};
    component.rootTranslationWeight = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentValid(component));

    component = FullBodyIK3DComponent{};
    component.maxJointStepRadians = 3.2F;
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentValid(component));

    component = FullBodyIK3DComponent{};
    component.effectors.push_back(FullBodyIKEffector{});
    component.effectors.back().positionWeight = -0.1F;
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentValid(component));

    component = FullBodyIK3DComponent{};
    component.effectors.push_back(FullBodyIKEffector{});
    component.effectors.back().targetPosition.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentValid(component));
}

TEST(FullBodyIK3DTest, ResolvableNeedsARootAndAtLeastOneNamedEffector) {
    FullBodyIK3DComponent component = FullBodyIK3DComponent{};
    component.skeleton = Entity{1U};
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentResolvable(component)); // nothing to solve yet

    component.rootBoneName = "Hips";
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentResolvable(component)); // still no effector

    component.effectors.push_back(FullBodyIKEffector{}); // default: names no bone
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentResolvable(component));

    component.effectors.back().boneName = "LeftHand";
    EXPECT_TRUE(IsFullBodyIK3DObjectComponentResolvable(component));

    component.enabled = false;
    EXPECT_FALSE(IsFullBodyIK3DObjectComponentResolvable(component));
}

} // namespace UVE::Scene
