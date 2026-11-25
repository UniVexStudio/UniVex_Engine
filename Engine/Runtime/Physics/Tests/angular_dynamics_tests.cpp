// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "angular_dynamics.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace UVE::Physics::Tests {
namespace {

constexpr float kEpsilon = 1.0e-5F;

TEST(AngularDynamicsTest, ComputeBoxInverseInertia_MatchesDiagonalBoxFormula) {
    const auto inverseInertia = ComputeBoxInverseInertia(2.0F, Math::Vector3{1.0F, 2.0F, 3.0F});

    ASSERT_TRUE(inverseInertia.has_value());
    EXPECT_NEAR(inverseInertia->x, 3.0F / 26.0F, kEpsilon);
    EXPECT_NEAR(inverseInertia->y, 3.0F / 20.0F, kEpsilon);
    EXPECT_NEAR(inverseInertia->z, 3.0F / 10.0F, kEpsilon);
}

TEST(AngularDynamicsTest, ComputeBoxInverseInertia_PreservesLargeFiniteHalfExtents) {
    const auto inverseInertia =
        ComputeBoxInverseInertia(1.0F, Math::Vector3{1.0e20F, 1.0F, 1.0F});

    ASSERT_TRUE(inverseInertia.has_value());
    EXPECT_NEAR(inverseInertia->x, 1.5F, 1.0e-5F);
    EXPECT_TRUE(std::isfinite(inverseInertia->y));
    EXPECT_TRUE(std::isfinite(inverseInertia->z));
    EXPECT_GT(inverseInertia->y, 0.0F);
    EXPECT_GT(inverseInertia->z, 0.0F);
}

TEST(AngularDynamicsTest, ComputeBoxInverseInertia_ZeroMassReturnsStaticZeroTensor) {
    const auto inverseInertia = ComputeBoxInverseInertia(0.0F, Math::Vector3{1.0F, 1.0F, 1.0F});

    ASSERT_TRUE(inverseInertia.has_value());
    EXPECT_EQ(*inverseInertia, Math::Vector3{});
}

TEST(AngularDynamicsTest, IntegrateAngularVelocity_AppliesTorqueOverFiniteStep) {
    const auto result = IntegrateAngularVelocity(
        Math::Vector3{1.0F, 2.0F, 3.0F}, Math::Vector3{4.0F, 5.0F, 6.0F},
        Math::Vector3{0.5F, 0.25F, 0.1F}, 0.2F);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->x, 1.4F, kEpsilon);
    EXPECT_NEAR(result->y, 2.25F, kEpsilon);
    EXPECT_NEAR(result->z, 3.12F, kEpsilon);
}

TEST(AngularDynamicsTest, ApplyAngularImpulse_AppliesDiagonalInverseInertia) {
    const auto result = ApplyAngularImpulse(
        Math::Vector3{1.0F, 2.0F, 3.0F}, Math::Vector3{4.0F, 5.0F, 6.0F},
        Math::Vector3{0.5F, 0.25F, 0.1F});

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->x, 3.0F, kEpsilon);
    EXPECT_NEAR(result->y, 3.25F, kEpsilon);
    EXPECT_NEAR(result->z, 3.6F, kEpsilon);
}

TEST(AngularDynamicsTest, EvaluateGyroscopicTorque_UsesDiagonalInertiaCrossTerm) {
    const auto result = EvaluateGyroscopicTorque(
        Math::Vector3{1.0F, 2.0F, 3.0F}, Math::Vector3{0.5F, 0.25F, 0.1F});
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->x, 36.0F, kEpsilon);
    EXPECT_NEAR(result->y, -24.0F, kEpsilon);
    EXPECT_NEAR(result->z, 4.0F, kEpsilon);
}

TEST(AngularDynamicsTest, EvaluateGyroscopicTorque_RejectsUnsafeInputs) {
    EXPECT_FALSE(EvaluateGyroscopicTorque(
        Math::Vector3{1.0F, 2.0F, 3.0F}, Math::Vector3{-1.0F, 0.0F, 0.0F}).has_value());
    EXPECT_FALSE(EvaluateGyroscopicTorque(
        Math::Vector3{std::numeric_limits<float>::infinity(), 0.0F, 0.0F},
        Math::Vector3{1.0F, 1.0F, 1.0F}).has_value());
}

TEST(AngularDynamicsTest, AngularHelpers_RejectUnsafeOrNonFiniteInputs) {
    EXPECT_FALSE(ComputeBoxInverseInertia(-1.0F, Math::Vector3{1.0F, 1.0F, 1.0F}).has_value());
    EXPECT_FALSE(ComputeBoxInverseInertia(1.0F, Math::Vector3{0.0F, 1.0F, 1.0F}).has_value());
    EXPECT_FALSE(ComputeBoxInverseInertia(
        std::numeric_limits<float>::quiet_NaN(), Math::Vector3{1.0F, 1.0F, 1.0F}).has_value());
    EXPECT_FALSE(ComputeBoxInverseInertia(
        1.0F, Math::Vector3{std::numeric_limits<float>::max(), 1.0F, 1.0F}).has_value());
    EXPECT_FALSE(IntegrateAngularVelocity(
        Math::Vector3{}, Math::Vector3{}, Math::Vector3{}, -0.1F).has_value());
    EXPECT_FALSE(ApplyAngularImpulse(
        Math::Vector3{}, Math::Vector3{}, Math::Vector3{-1.0F, 0.0F, 0.0F}).has_value());
    EXPECT_FALSE(ApplyAngularImpulse(
        Math::Vector3{std::numeric_limits<float>::infinity(), 0.0F, 0.0F},
        Math::Vector3{}, Math::Vector3{}).has_value());
}

} // namespace
} // namespace UVE::Physics::Tests
