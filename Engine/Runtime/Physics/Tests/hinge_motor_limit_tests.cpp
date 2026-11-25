// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hinge_motor_limit.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace UVE::Physics::Tests {
namespace {

TEST(HingeMotorLimitTest, Evaluate_MotorSpeedCorrectionIsCappedByTorqueAndInertia) {
    HingeMotorLimitResult result;
    const bool accepted = EvaluateHingeMotorLimit(
        HingeMotorLimitInput{0.0F, 1.0F, 0.5F, 2.0F, true, 5.0F, 3.0F, false, 0.0F, 0.0F}, result);

    ASSERT_TRUE(accepted);
    EXPECT_FLOAT_EQ(result.angularSpeedDeltaRadians, 3.0F);
    EXPECT_TRUE(result.motorApplied);
    EXPECT_FALSE(result.limitApplied);
}

TEST(HingeMotorLimitTest, Evaluate_MotorCapBeyondFloatRangeStillAcceptsFiniteRequest) {
    HingeMotorLimitResult result;
    const bool accepted = EvaluateHingeMotorLimit(
        HingeMotorLimitInput{0.0F, 0.0F, 2.0F, 2.0F, true, 1.0F,
                                std::numeric_limits<float>::max(), false, 0.0F, 0.0F},
        result);

    ASSERT_TRUE(accepted);
    EXPECT_FLOAT_EQ(result.angularSpeedDeltaRadians, 1.0F);
    EXPECT_TRUE(result.motorApplied);
    EXPECT_FALSE(result.limitApplied);
}

TEST(HingeMotorLimitTest, Evaluate_MotorMovesTowardTargetWithoutOvershoot) {
    HingeMotorLimitResult result;
    const bool accepted = EvaluateHingeMotorLimit(
        HingeMotorLimitInput{0.0F, 4.0F, 0.1F, 1.0F, true, 2.0F, 10.0F, false, 0.0F, 0.0F}, result);

    ASSERT_TRUE(accepted);
    EXPECT_FLOAT_EQ(result.angularSpeedDeltaRadians, -1.0F);
    EXPECT_TRUE(result.motorApplied);
}

TEST(HingeMotorLimitTest, Evaluate_LowerAndUpperLimitsReturnDeterministicCorrections) {
    HingeMotorLimitResult lower;
    ASSERT_TRUE(EvaluateHingeMotorLimit(
        HingeMotorLimitInput{-2.0F, 0.0F, 0.0F, 0.0F, false, 0.0F, 0.0F, true, -1.0F, 1.0F}, lower));
    EXPECT_FLOAT_EQ(lower.angleCorrectionRadians, 1.0F);
    EXPECT_TRUE(lower.limitApplied);

    HingeMotorLimitResult upper;
    ASSERT_TRUE(EvaluateHingeMotorLimit(
        HingeMotorLimitInput{2.0F, 0.0F, 0.0F, 0.0F, false, 0.0F, 0.0F, true, -1.0F, 1.0F}, upper));
    EXPECT_FLOAT_EQ(upper.angleCorrectionRadians, -1.0F);
    EXPECT_TRUE(upper.limitApplied);
}

TEST(HingeMotorLimitTest, Evaluate_DisabledPoliciesProduceNoOp) {
    HingeMotorLimitResult result{7.0F, 8.0F, true, true};
    ASSERT_TRUE(EvaluateHingeMotorLimit(
        HingeMotorLimitInput{2.0F, 3.0F, 0.1F, 1.0F, false, 0.0F, 0.0F, false, 0.0F, 0.0F}, result));
    EXPECT_FLOAT_EQ(result.angularSpeedDeltaRadians, 0.0F);
    EXPECT_FLOAT_EQ(result.angleCorrectionRadians, 0.0F);
    EXPECT_FALSE(result.motorApplied);
    EXPECT_FALSE(result.limitApplied);
}

TEST(HingeMotorLimitTest, Evaluate_RejectsInvalidInputsWithoutChangingOutput) {
    HingeMotorLimitResult result{7.0F, 8.0F, true, true};
    const bool accepted = EvaluateHingeMotorLimit(
        HingeMotorLimitInput{0.0F, 0.0F, -0.1F, 1.0F, true, 1.0F, 1.0F, false, 0.0F, 0.0F}, result);

    EXPECT_FALSE(accepted);
    EXPECT_FLOAT_EQ(result.angularSpeedDeltaRadians, 7.0F);
    EXPECT_FLOAT_EQ(result.angleCorrectionRadians, 8.0F);
    EXPECT_TRUE(result.motorApplied);
    EXPECT_TRUE(result.limitApplied);

    EXPECT_FALSE(EvaluateHingeMotorLimit(
        HingeMotorLimitInput{0.0F, 0.0F, 0.1F, 1.0F, false, 0.0F, 0.0F, true, 2.0F, 1.0F}, result));
    EXPECT_FALSE(EvaluateHingeMotorLimit(
        HingeMotorLimitInput{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.1F, 1.0F}, result));
}

} // namespace
} // namespace UVE::Physics::Tests
