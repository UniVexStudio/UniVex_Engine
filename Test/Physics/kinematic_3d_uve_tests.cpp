// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/kinematic_3d_uve.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kDeltaTimeUVE = 1.0F / 60.0F;

TEST(Kinematic3DUVETest, IsDrivingUVE_FollowsActive) {
    Kinematic3DComponentUVE kinematic{};
    EXPECT_TRUE(Kinematic3DUVE::IsDrivingUVE(kinematic));
    kinematic.active = false;
    EXPECT_FALSE(Kinematic3DUVE::IsDrivingUVE(kinematic));
}

TEST(Kinematic3DUVETest, EaseBlendUVE_OneIsNowZeroIsNever) {
    EXPECT_FLOAT_EQ(Kinematic3DUVE::EaseBlendUVE(1.0F, kDeltaTimeUVE), 1.0F);
    EXPECT_FLOAT_EQ(Kinematic3DUVE::EaseBlendUVE(0.0F, kDeltaTimeUVE), 0.0F);
    EXPECT_FLOAT_EQ(Kinematic3DUVE::EaseBlendUVE(4.0F, kDeltaTimeUVE), 1.0F);
    EXPECT_FLOAT_EQ(Kinematic3DUVE::EaseBlendUVE(-2.0F, kDeltaTimeUVE), 0.0F);
    EXPECT_FLOAT_EQ(Kinematic3DUVE::EaseBlendUVE(std::numeric_limits<float>::quiet_NaN(), kDeltaTimeUVE), 0.0F);
}

TEST(Kinematic3DUVETest, ResolveWorldTargetUVE_IdentityKeepsLocalAndRotationTurnsIt) {
    const Math::Vector3UVE local{1.0F, 0.0F, 0.0F};
    const Math::Vector3UVE identity = Kinematic3DUVE::ResolveWorldTargetUVE(local, {});
    EXPECT_NEAR(identity.x, 1.0F, 1.0e-5F);
    EXPECT_NEAR(identity.y, 0.0F, 1.0e-5F);
    EXPECT_NEAR(identity.z, 0.0F, 1.0e-5F);

    Math::QuaternionUVE quarterTurn{};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE({0.0F, 1.0F, 0.0F}, 1.5707963F, quarterTurn));
    const Math::Vector3UVE turned = Kinematic3DUVE::ResolveWorldTargetUVE(local, quarterTurn);
    const Math::Vector3UVE expected = Math::RotateVectorUVE(quarterTurn, local);
    EXPECT_NEAR(turned.x, expected.x, 1.0e-5F);
    EXPECT_NEAR(turned.y, expected.y, 1.0e-5F);
    EXPECT_NEAR(turned.z, expected.z, 1.0e-5F);
}

TEST(Kinematic3DUVETest, ResolveWorldTargetUVE_NonFiniteIsStanding) {
    const Math::Vector3UVE nan{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    const Math::Vector3UVE world = Kinematic3DUVE::ResolveWorldTargetUVE(nan, {});
    EXPECT_FLOAT_EQ(world.x, 0.0F);
    EXPECT_FLOAT_EQ(world.y, 0.0F);
    EXPECT_FLOAT_EQ(world.z, 0.0F);
}

TEST(Kinematic3DUVETest, EaseVelocityUVE_SnapsAtOneAndHoldsAtZero) {
    const Math::Vector3UVE current{1.0F, 0.0F, 0.0F};
    const Math::Vector3UVE target{3.0F, 0.0F, 0.0F};
    const Math::Vector3UVE snapped = Kinematic3DUVE::EaseVelocityUVE(current, target, 1.0F, kDeltaTimeUVE);
    EXPECT_NEAR(snapped.x, 3.0F, 1.0e-5F);
    const Math::Vector3UVE held = Kinematic3DUVE::EaseVelocityUVE(current, target, 0.0F, kDeltaTimeUVE);
    EXPECT_NEAR(held.x, 1.0F, 1.0e-5F);
}

} // namespace
} // namespace UVE::Scene::Tests
