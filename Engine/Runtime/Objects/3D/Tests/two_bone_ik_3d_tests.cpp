// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "two_bone_ik_3d.h"

#include <cmath>
#include <limits>
#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Scene {
namespace {

constexpr float kEpsilon = 1e-4F;

/// The limb every geometry test solves: a metre from the root to the middle joint along +X, a metre
/// from there to the effector along +Y - so both bones are exactly one metre and a target one metre
/// from the root folds the limb into the equilateral triangle the two circles intersect at.
[[nodiscard]] TwoBoneIKChain BentArm() {
    TwoBoneIKChain chain{};
    chain.root = Math::Vector3{0.0F, 0.0F, 0.0F};
    chain.middle = Math::Vector3{1.0F, 0.0F, 0.0F};
    chain.end = Math::Vector3{1.0F, 1.0F, 0.0F};
    return chain;
}

/// Where a bone's own origin lands once the solve has been applied, composed the way the skeleton
/// resolver composes a bone: the child's offset is stored in its PARENT's frame, so it is the parent's
/// world rotation - the posed one the solve turned, not the local rotation the solve wrote - that moves
/// it. Rebuilt from the solution and the chain rather than read back from the solver, so a solve that
/// reports one thing and means another cannot pass.
[[nodiscard]] Math::Vector3 PlacedBoneOrigin(const Math::Vector3& placedJoint,
                                                   const Math::Vector3& posedOffset,
                                                   const Math::Quaternion& posedParentRotation,
                                                   const Math::Quaternion& solvedParentWorldRotation) {
    Math::Quaternion inversePosed{};
    if (!Math::TryInverse(posedParentRotation, inversePosed)) {
        return placedJoint;
    }
    const Math::Quaternion turn = Math::Multiply(solvedParentWorldRotation, inversePosed);
    return placedJoint + Math::RotateVector(turn, posedOffset);
}

/// The world rotations a solve leaves the two driven bones with, in the frame the chain was posed in.
[[nodiscard]] Math::Quaternion RootWorldRotation(const TwoBoneIKChain& chain,
                                                       const TwoBoneIKSolution& solution) {
    return Math::Multiply(chain.parentRotation, solution.rootLocalRotation);
}

[[nodiscard]] Math::Quaternion MiddleWorldRotation(const Math::Quaternion& rootWorld,
                                                         const TwoBoneIKSolution& solution) {
    return Math::Multiply(rootWorld, solution.middleLocalRotation);
}

TEST(TwoBoneIK3DTest, SolvesTheElbowOntoTheTargetWithBothBonesAtTheirOwnLength) {
    const TwoBoneIKChain chain = BentArm();
    const Math::Vector3 target{1.4F, 1.2F, 0.0F};
    const std::optional<TwoBoneIKSolution> solution = SolveTwoBoneIK(chain, target, {0.0F, 1.0F, 0.0F});
    ASSERT_TRUE(solution.has_value());

    EXPECT_TRUE(solution->reached);
    EXPECT_NEAR(solution->endToTargetDistanceMetres, 0.0F, kEpsilon);
    EXPECT_NEAR(solution->endPosition.x, target.x, kEpsilon);
    EXPECT_NEAR(solution->endPosition.z, target.z, kEpsilon);

    // The solution is a rotation, so applying it to the two bones is the only way to check that the
    // limb still has its own length and that the elbow really is where the effector's answer says it
    // is. `rootLocalRotation` is relative to the parent's frame, which is the identity in this chain.
    const Math::Quaternion rootWorld = RootWorldRotation(chain, *solution);
    const Math::Vector3 placedMiddle =
        PlacedBoneOrigin(chain.root, chain.middle - chain.root, chain.rootRotation, rootWorld);
    const Math::Quaternion middleWorld = MiddleWorldRotation(rootWorld, *solution);
    const Math::Vector3 placedEnd =
        PlacedBoneOrigin(placedMiddle, chain.end - chain.middle, chain.middleRotation, middleWorld);
    EXPECT_NEAR(Math::Length(placedMiddle - chain.root), 1.0F, kEpsilon) << "the upper bone keeps its metre";
    EXPECT_NEAR(Math::Length(placedEnd - placedMiddle), 1.0F, kEpsilon) << "the lower bone keeps its metre";
    EXPECT_NEAR(Math::Length(placedEnd - target), 0.0F, kEpsilon) << "the effector lands on the target";
}

TEST(TwoBoneIK3DTest, PicksTheSideOfTheTargetLineThePoleNames) {
    // The joint's circle is a whole circle of answers; the pole is what turns it into one. Both runs
    // are handed the same target and the same pose, so the only difference between them is which
    // side of the root-to-target line the pole points to.
    const TwoBoneIKChain chain = BentArm();
    const Math::Vector3 target{1.4F, 1.2F, 0.0F};
    const std::optional<TwoBoneIKSolution> upwards = SolveTwoBoneIK(chain, target, {0.0F, 1.0F, 0.0F});
    const std::optional<TwoBoneIKSolution> downwards = SolveTwoBoneIK(chain, target, {0.0F, -1.0F, 0.0F});
    ASSERT_TRUE(upwards.has_value());
    ASSERT_TRUE(downwards.has_value());

    const Math::Vector3 upwardElbow = PlacedBoneOrigin(
        chain.root, chain.middle - chain.root, chain.rootRotation, RootWorldRotation(chain, *upwards));
    const Math::Vector3 downwardElbow = PlacedBoneOrigin(
        chain.root, chain.middle - chain.root, chain.rootRotation, RootWorldRotation(chain, *downwards));
    // A +Y pole puts the elbow above the line, a -Y pole below it.
    EXPECT_NEAR(upwardElbow.z, 0.0F, kEpsilon) << "an XY pole keeps the joint in the XY plane";
    EXPECT_NEAR(downwardElbow.z, 0.0F, kEpsilon);
    EXPECT_GT(upwardElbow.y - (upwardElbow.x * (target.y / target.x)), 0.0F)
        << "the +Y solution bends toward +Y";
    EXPECT_LT(downwardElbow.y - (downwardElbow.x * (target.y / target.x)), 0.0F)
        << "the -Y solution bends toward -Y";

    // The two answers are mirror images across the root-to-target line: their midpoint is ON that
    // line, which is what makes the flip a mirror rather than a different solution.
    const Math::Vector3 midpoint = (upwardElbow + downwardElbow) * 0.5F;
    const Math::Vector3 aim = Math::Normalize(target - chain.root);
    EXPECT_NEAR(Math::Length(Math::Cross(midpoint, aim)), 0.0F, kEpsilon);
    EXPECT_NEAR(upwards->endToTargetDistanceMetres, downwards->endToTargetDistanceMetres, kEpsilon);
}

TEST(TwoBoneIK3DTest, KeepsTheBendDirectionThePoseAlreadyHadWhenNoPoleIsAuthored) {
    // A zero pole is the honest default: the chain has nowhere it was asked to bend, so it keeps the
    // bend it already has instead of popping to a pole the author never gave. The pose here is bent
    // in +X (its joint is offset from the root-to-target line along +X), so the solved joint has to
    // come out on that same side.
    TwoBoneIKChain chain{};
    chain.root = Math::Vector3{0.0F, 0.0F, 0.0F};
    chain.middle = Math::Vector3{1.0F, 0.0F, 0.0F};
    chain.end = Math::Vector3{1.0F, 0.0F, 1.0F};
    const Math::Vector3 target{0.6F, 1.2F, 0.0F};
    const std::optional<TwoBoneIKSolution> solution = SolveTwoBoneIK(chain, target, {});
    ASSERT_TRUE(solution.has_value());

    const Math::Vector3 placedMiddle = PlacedBoneOrigin(
        chain.root, chain.middle - chain.root, chain.rootRotation, RootWorldRotation(chain, *solution));
    const Math::Vector3 aim = Math::Normalize(target - chain.root);
    // The pose's own bend direction: the part of the posed joint that is perpendicular to the aim.
    const Math::Vector3 posedBend =
        Math::Normalize(chain.middle - aim * Math::Dot(chain.middle, aim));
    const Math::Vector3 solvedBend = placedMiddle - aim * Math::Dot(placedMiddle, aim);
    EXPECT_GT(Math::Length(solvedBend), kEpsilon) << "a target off the posed axis has to bend something";
    EXPECT_NEAR(Math::Dot(Math::Normalize(solvedBend), posedBend), 1.0F, kEpsilon)
        << "the solved bend stays parallel to the posed one";
}

TEST(TwoBoneIK3DTest, AimsStraightAtATargetBeyondItsReachAndSaysItDidNotReach) {
    const TwoBoneIKChain chain = BentArm();
    const Math::Vector3 target{5.0F, 0.0F, 0.0F};
    const std::optional<TwoBoneIKSolution> solution = SolveTwoBoneIK(chain, target, {0.0F, 1.0F, 0.0F});
    ASSERT_TRUE(solution.has_value());

    // Out of reach is an ordinary pose, not an error: the limb straightens toward the target and
    // stops at its own length, and the report carries how short it fell.
    EXPECT_FALSE(solution->reached);
    EXPECT_NEAR(solution->endPosition.x, 2.0F, kEpsilon) << "two one-metre bones reach two metres";
    EXPECT_NEAR(solution->endPosition.y, 0.0F, kEpsilon);
    EXPECT_NEAR(solution->endToTargetDistanceMetres, 3.0F, kEpsilon);

    const Math::Vector3 placedMiddle = PlacedBoneOrigin(
        chain.root, chain.middle - chain.root, chain.rootRotation, RootWorldRotation(chain, *solution));
    EXPECT_NEAR(placedMiddle.x, 1.0F, kEpsilon) << "equal bones fold flat when the chain is straight";
    EXPECT_NEAR(placedMiddle.y, 0.0F, kEpsilon);
}

TEST(TwoBoneIK3DTest, FoldsTowardATargetItCannotShortenEnoughToReach) {
    // Bones of different lengths cannot fold to a point: the closest the effector can come to the
    // root is the difference between them. A target inside that band is as unreachable as one beyond
    // the far end, and the answer is the folded pose an animator would key.
    TwoBoneIKChain chain{};
    chain.root = Math::Vector3{0.0F, 0.0F, 0.0F};
    chain.middle = Math::Vector3{1.0F, 0.0F, 0.0F};
    chain.end = Math::Vector3{1.0F, 0.5F, 0.0F};
    const std::optional<TwoBoneIKSolution> solution =
        SolveTwoBoneIK(chain, {0.2F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
    ASSERT_TRUE(solution.has_value());

    EXPECT_FALSE(solution->reached);
    EXPECT_NEAR(solution->endPosition.x, 0.5F, kEpsilon) << "the effector stops a half-metre out";
    EXPECT_NEAR(solution->endToTargetDistanceMetres, 0.3F, kEpsilon);

    const Math::Quaternion foldRootWorld = RootWorldRotation(chain, *solution);
    const Math::Vector3 placedMiddle =
        PlacedBoneOrigin(chain.root, chain.middle - chain.root, chain.rootRotation, foldRootWorld);
    const Math::Vector3 placedEnd =
        PlacedBoneOrigin(placedMiddle, chain.end - chain.middle, chain.middleRotation,
                            MiddleWorldRotation(foldRootWorld, *solution));
    EXPECT_NEAR(Math::Length(placedEnd - placedMiddle), 0.5F, kEpsilon) << "the short bone keeps its length";
    EXPECT_NEAR(placedEnd.x, solution->endPosition.x, kEpsilon);
}

TEST(TwoBoneIK3DTest, RefusesChainsAndTargetsThatAreNotAPoseAtAll) {
    // Each of these means the data or the caller is wrong rather than that a limb came up short, so
    // the answer is no solution - the caller leaves the pose exactly as it was.
    TwoBoneIKChain zeroUpper{};
    zeroUpper.middle = Math::Vector3{0.0F, 0.0F, 0.0F};
    zeroUpper.end = Math::Vector3{1.0F, 0.0F, 0.0F};
    EXPECT_FALSE(SolveTwoBoneIK(zeroUpper, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}).has_value());

    TwoBoneIKChain zeroLower = BentArm();
    zeroLower.end = zeroLower.middle;
    EXPECT_FALSE(SolveTwoBoneIK(zeroLower, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}).has_value());

    EXPECT_FALSE(SolveTwoBoneIK(BentArm(), {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}).has_value())
        << "a target on the root names no direction to aim along";
    EXPECT_FALSE(SolveTwoBoneIK(BentArm(), {std::numeric_limits<float>::infinity(), 0.0F, 0.0F},
                                  {0.0F, 1.0F, 0.0F})
                     .has_value());

    TwoBoneIKChain badRotation = BentArm();
    badRotation.parentRotation.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(SolveTwoBoneIK(badRotation, {1.4F, 1.2F, 0.0F}, {0.0F, 1.0F, 0.0F}).has_value());
}

TEST(TwoBoneIK3DTest, BlendsTheSolvedRotationOverThePosedOneByInfluence) {
    Math::Quaternion ninety{};
    ASSERT_TRUE(Math::TryMakeAxisAngle({0.0F, 0.0F, 1.0F}, 1.5707963F, ninety));
    const Math::Quaternion posed{};

    Math::Quaternion blended{};
    ASSERT_TRUE(TryBlendTwoBoneIKRotation(posed, ninety, 0.0F, blended));
    EXPECT_EQ(blended, posed) << "zero influence is the animation's own rotation";
    ASSERT_TRUE(TryBlendTwoBoneIKRotation(posed, ninety, 1.0F, blended));
    const Math::Vector3 fullySolved = Math::RotateVector(blended, {1.0F, 0.0F, 0.0F});
    EXPECT_NEAR(fullySolved.y, 1.0F, kEpsilon);

    // Halfway eases the limb a quarter-turn: the shortest arc, so a reach does not swing through the
    // body on its way to the prop.
    ASSERT_TRUE(TryBlendTwoBoneIKRotation(posed, ninety, 0.5F, blended));
    const Math::Vector3 halfway = Math::RotateVector(blended, {1.0F, 0.0F, 0.0F});
    EXPECT_NEAR(halfway.x, std::sqrt(0.5F), kEpsilon);
    EXPECT_NEAR(halfway.y, std::sqrt(0.5F), kEpsilon);
    EXPECT_NEAR(halfway.z, 0.0F, kEpsilon);

    // An influence outside the unit range is clamped by the same two ends rather than extrapolated.
    ASSERT_TRUE(TryBlendTwoBoneIKRotation(posed, ninety, -1.0F, blended));
    EXPECT_EQ(blended, posed);
    ASSERT_TRUE(TryBlendTwoBoneIKRotation(posed, ninety, 2.0F, blended));
    EXPECT_NEAR(Math::RotateVector(blended, {1.0F, 0.0F, 0.0F}).y, 1.0F, kEpsilon);
}

TEST(TwoBoneIK3DTest, RefusesToBlendANonFiniteRotationAndLeavesTheOutputAlone) {
    Math::Quaternion posed{};
    posed.x = std::numeric_limits<float>::infinity();
    const Math::Quaternion solved{};
    Math::Quaternion untouched{0.0F, 0.0F, 0.0F, 1.0F};
    EXPECT_FALSE(TryBlendTwoBoneIKRotation(posed, solved, 0.5F, untouched));
    EXPECT_EQ(untouched, Math::Quaternion{});

    Math::Quaternion badSolved{};
    badSolved.w = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(TryBlendTwoBoneIKRotation(solved, badSolved, 1.0F, untouched));
    EXPECT_EQ(untouched, Math::Quaternion{});
}

TEST(TwoBoneIK3DTest, ValidityAndResolvabilityTravelWithTheComponent) {
    const TwoBoneIK3DComponent authored{};
    EXPECT_TRUE(IsTwoBoneIK3DObjectComponentValid(authored))
        << "a fresh chain is valid scene data - it is simply not resolvable yet";
    EXPECT_FALSE(IsTwoBoneIK3DObjectComponentResolvable(authored));

    TwoBoneIK3DComponent named{};
    named.skeleton = Entity{7U, 0U};
    named.rootBoneName = "shoulder";
    named.middleBoneName = "elbow";
    named.endBoneName = "wrist";
    EXPECT_TRUE(IsTwoBoneIK3DObjectComponentResolvable(named));
    EXPECT_TRUE(IsTwoBoneIK3DObjectComponentResolvable(named))
        << "names alone are enough to bind a re-exported rig whose bone order moved";

    // A byte in a name is what a corrupt file or a forged payload looks like; the declaration's own
    // validity rule has to reject it before a chain is built from it.
    TwoBoneIK3DComponent nullByte = named;
    nullByte.rootBoneName = std::string("should\0er", 9U);
    EXPECT_FALSE(IsTwoBoneIK3DObjectComponentValid(nullByte));

    TwoBoneIK3DComponent tooLong = named;
    tooLong.middleBoneName = std::string(kMaximum3DObjectStringLength + 1U, 'b');
    EXPECT_FALSE(IsTwoBoneIK3DObjectComponentValid(tooLong));

    TwoBoneIK3DComponent notFinite = named;
    notFinite.poleDirection.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsTwoBoneIK3DObjectComponentValid(notFinite));

    named.enabled = false;
    EXPECT_FALSE(IsTwoBoneIK3DObjectComponentResolvable(named)) << "a chain switched off is inert";
}

} // namespace
} // namespace UVE::Scene
