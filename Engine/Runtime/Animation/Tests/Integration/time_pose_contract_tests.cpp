// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "time_pose_contract.h"

#include <gtest/gtest.h>

#include <limits>

namespace UVE::Core {

TEST(UnifiedTimeContractTest, Advance_UpdatesScaledDomainsAndFixedStepsDeterministically) {
    std::size_t fixedSteps = 0U;
    bool inputClamped = false;
    const UnifiedTimeState state = UnifiedTimeContract::Advance(
        {}, UnifiedTimeAdvanceInput{0.1, 2.0, 0.5, 0.05, 8U, false}, fixedSteps, inputClamped);

    EXPECT_EQ(state, (UnifiedTimeState{1U, 0.1, 0.2, 0.1, 0.05, 0.0, 0.1, 0.2, 0.05, false}));
    EXPECT_EQ(fixedSteps, 2U);
    EXPECT_FALSE(inputClamped);
}

TEST(UnifiedTimeContractTest, Advance_ClampsDeltaAndFixedStepBudget) {
    std::size_t fixedSteps = 0U;
    bool inputClamped = false;
    const UnifiedTimeState state = UnifiedTimeContract::Advance(
        {}, UnifiedTimeAdvanceInput{1.0, 1.0, 1.0, 0.01, 100U, false}, fixedSteps, inputClamped);

    EXPECT_DOUBLE_EQ(state.realDeltaSeconds, UnifiedTimeContract::kMaximumFrameDeltaSeconds);
    EXPECT_EQ(fixedSteps, UnifiedTimeContract::kMaximumFixedStepsPerAdvance);
    EXPECT_TRUE(inputClamped);
    EXPECT_NEAR(state.fixedAccumulatorSeconds, 0.17, 1.0e-12);
}

TEST(UnifiedTimeContractTest, Advance_RejectsDerivedTimeOverflowAtomically) {
    const UnifiedTimeState previous{
        7U, 1.0, 1.0, 0.5, std::numeric_limits<double>::max(), 0.01, 0.1, 0.2, 0.3, false};
    std::size_t fixedSteps = 99U;
    bool inputClamped = false;
    const UnifiedTimeState next = UnifiedTimeContract::Advance(
        previous, UnifiedTimeAdvanceInput{0.25, 1.0, std::numeric_limits<double>::max(), 0.05, 8U, false},
        fixedSteps, inputClamped);
    EXPECT_EQ(next, previous);
    EXPECT_EQ(fixedSteps, 0U);
    EXPECT_TRUE(inputClamped);

    const UnifiedTimeState scaledPrevious{
        8U, 1.0, std::numeric_limits<double>::max(), 0.5, 0.75, 0.01, 0.1, 0.2, 0.3, false};
    fixedSteps = 99U;
    inputClamped = false;
    const UnifiedTimeState scaledNext = UnifiedTimeContract::Advance(
        scaledPrevious, UnifiedTimeAdvanceInput{0.25, std::numeric_limits<double>::max(), 1.0, 0.05, 8U, false},
        fixedSteps, inputClamped);
    EXPECT_EQ(scaledNext, scaledPrevious);
    EXPECT_EQ(fixedSteps, 0U);
    EXPECT_TRUE(inputClamped);
}

TEST(UnifiedTimeContractTest, Advance_PauseFreezesSimulationDomainsButAdvancesRealDomain) {
    const UnifiedTimeState previous{4U, 2.0, 1.0, 0.9, 1.5, 0.02, 0.1, 0.2, 0.3, false};
    std::size_t fixedSteps = 0U;
    bool inputClamped = false;
    const UnifiedTimeState state = UnifiedTimeContract::Advance(
        previous, UnifiedTimeAdvanceInput{0.1, 2.0, 2.0, 0.05, 8U, true}, fixedSteps, inputClamped);

    EXPECT_EQ(state.frameNumber, 5U);
    EXPECT_DOUBLE_EQ(state.realTimeSeconds, 2.1);
    EXPECT_DOUBLE_EQ(state.gameTimeSeconds, previous.gameTimeSeconds);
    EXPECT_DOUBLE_EQ(state.fixedTimeSeconds, previous.fixedTimeSeconds);
    EXPECT_DOUBLE_EQ(state.animationTimeSeconds, previous.animationTimeSeconds);
    EXPECT_DOUBLE_EQ(state.fixedAccumulatorSeconds, previous.fixedAccumulatorSeconds);
    EXPECT_DOUBLE_EQ(state.gameDeltaSeconds, 0.0);
    EXPECT_DOUBLE_EQ(state.animationDeltaSeconds, 0.0);
    EXPECT_EQ(fixedSteps, 0U);
    EXPECT_FALSE(inputClamped);
}

TEST(TransformPoseTest, TryNormalizeTransformPose_NormalizesRotationAndPreservesValues) {
    const TransformPose input{{1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 0.0F, 2.0F}, {2.0F, 3.0F, 4.0F}};
    TransformPose normalized;

    ASSERT_TRUE(TryNormalizeTransformPose(input, normalized));
    EXPECT_EQ(normalized.position, input.position);
    EXPECT_EQ(normalized.scale, input.scale);
    EXPECT_EQ(normalized.rotation, (Math::Quaternion{0.0F, 0.0F, 0.0F, 1.0F}));
    EXPECT_TRUE(IsFiniteTransformPose(normalized));
}

TEST(AnimationPoseContractTest, ValidatesSkeletonPoseAndEvaluationContextTogether) {
    const SkeletonDefinition skeleton{
        "humanoid", {SkeletonJoint{"root", ""}, SkeletonJoint{"hand", "root"}}};
    const PoseBuffer pose{
        "humanoid",
        {TransformPose{{}, {}, {1.0F, 1.0F, 1.0F}},
         TransformPose{{1.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}}};
    AnimationEvaluationContext context;
    context.time.animationTimeSeconds = 1.25;
    context.time.animationDeltaSeconds = 1.0 / 60.0;
    context.sampleTimeSeconds = 0.5;

    EXPECT_TRUE(ValidateSkeletonDefinition(skeleton).IsValid());
    EXPECT_TRUE(ValidatePoseBuffer(pose, skeleton).IsValid());
    EXPECT_TRUE(ValidateAnimationEvaluationContext(context).IsValid());
}

TEST(AnimationPoseContractTest, RejectsDuplicateOrUnknownJointsAndMismatchedPose) {
    const SkeletonDefinition duplicate{
        "humanoid", {SkeletonJoint{"root", ""}, SkeletonJoint{"root", ""}}};
    EXPECT_EQ(ValidateSkeletonDefinition(duplicate).code,
              AnimationContractValidationCode::DuplicateJoint);

    const SkeletonDefinition unknownParent{
        "humanoid", {SkeletonJoint{"root", "missing"}}};
    EXPECT_EQ(ValidateSkeletonDefinition(unknownParent).code,
              AnimationContractValidationCode::UnknownParent);

    const SkeletonDefinition skeleton{
        "humanoid", {SkeletonJoint{"root", ""}}};
    const PoseBuffer mismatched{"other", {TransformPose{}}};
    EXPECT_EQ(ValidatePoseBuffer(mismatched, skeleton).code,
              AnimationContractValidationCode::SkeletonMismatch);
}

TEST(AnimationPoseContractTest, RejectsEmbeddedNulAnimationIdentifiers) {
    SkeletonDefinition nulSkeleton{
        std::string{"humanoid"} + std::string(1U, static_cast<char>(0)) + "suffix",
        {SkeletonJoint{"root", ""}}};
    EXPECT_EQ(ValidateSkeletonDefinition(nulSkeleton).code,
              AnimationContractValidationCode::InvalidIdentifier);

    SkeletonDefinition nulJoint{
        "humanoid",
        {SkeletonJoint{std::string{"root"} + std::string(1U, static_cast<char>(0)) + "suffix", ""}}};
    EXPECT_EQ(ValidateSkeletonDefinition(nulJoint).code,
              AnimationContractValidationCode::InvalidIdentifier);

    SkeletonDefinition nulParent{
        "humanoid",
        {SkeletonJoint{"root", "root"},
         SkeletonJoint{"hand", std::string{"root"} + std::string(1U, static_cast<char>(0)) + "suffix"}}};
    EXPECT_EQ(ValidateSkeletonDefinition(nulParent).code,
              AnimationContractValidationCode::InvalidIdentifier);

    const SkeletonDefinition skeleton{
        "humanoid", {SkeletonJoint{"root", ""}}};
    const PoseBuffer nulPose{
        std::string{"humanoid"} + std::string(1U, static_cast<char>(0)) + "suffix", {TransformPose{}}};
    EXPECT_EQ(ValidatePoseBuffer(nulPose, skeleton).code,
              AnimationContractValidationCode::InvalidIdentifier);

    const SkeletonDefinition target{
        "target", {SkeletonJoint{"pelvis", ""}}};
    const auto nulMap = ValidateSkeletonRetargetMap(
        skeleton, target,
        std::vector<SkeletonRetargetMapEntry>{{
            std::string{"root"} + std::string(1U, static_cast<char>(0)) + "suffix", "pelvis"}});
    EXPECT_EQ(nulMap.code, AnimationContractValidationCode::InvalidIdentifier);
}

TEST(AnimationPoseContractTest, ValidatesOneToOneSkeletonRetargetMap) {
    const SkeletonDefinition source{
        "source", {SkeletonJoint{"root", ""}, SkeletonJoint{"hand", "root"}}};
    const SkeletonDefinition target{
        "target", {SkeletonJoint{"pelvis", ""}, SkeletonJoint{"arm", "pelvis"}}};
    const std::vector<SkeletonRetargetMapEntry> validMap{
        {"root", "pelvis"}, {"hand", "arm"}};
    EXPECT_TRUE(ValidateSkeletonRetargetMap(source, target, validMap).IsValid());

    const auto unknown = ValidateSkeletonRetargetMap(
        source, target, std::vector<SkeletonRetargetMapEntry>{{"missing", "pelvis"}});
    EXPECT_EQ(unknown.code, AnimationContractValidationCode::SkeletonMismatch);

    const auto duplicate = ValidateSkeletonRetargetMap(
        source, target, std::vector<SkeletonRetargetMapEntry>{{"root", "pelvis"}, {"root", "arm"}});
    EXPECT_EQ(duplicate.code, AnimationContractValidationCode::DuplicateJoint);
}

TEST(AnimationPoseContractTest, RejectsNegativeEvaluationTime) {
    AnimationEvaluationContext context;
    context.time.animationDeltaSeconds = -0.01;
    EXPECT_EQ(ValidateAnimationEvaluationContext(context).code,
              AnimationContractValidationCode::InvalidTime);
}

TEST(TransformPoseTest, TryNormalizeTransformPose_RejectsNonFiniteAndZeroRotation) {
    TransformPose output;
    EXPECT_FALSE(TryNormalizeTransformPose(
        TransformPose{{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 0.0F}, {}}, output));
    EXPECT_FALSE(TryNormalizeTransformPose(
        TransformPose{{std::numeric_limits<float>::infinity(), 0.0F, 0.0F}, {}, {}}, output));
    EXPECT_FALSE(IsFiniteTransformPose(
        TransformPose{{0.0F, 0.0F, 0.0F}, {}, {std::numeric_limits<float>::quiet_NaN(), 1.0F, 1.0F}}));
}

} // namespace UVE::Core
