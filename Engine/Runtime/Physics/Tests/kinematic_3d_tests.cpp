// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "quaternion.h"
#include "vector3.h"
#include "kinematic_3d.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kDeltaTime = 1.0F / 60.0F;

TEST(Kinematic3DTest, IsDriving_FollowsActive) {
    Kinematic3DComponent kinematic{};
    EXPECT_TRUE(Kinematic3D::IsDriving(kinematic));
    kinematic.active = false;
    EXPECT_FALSE(Kinematic3D::IsDriving(kinematic));
}

TEST(Kinematic3DTest, EaseBlend_OneIsNowZeroIsNever) {
    EXPECT_FLOAT_EQ(Kinematic3D::EaseBlend(1.0F, kDeltaTime), 1.0F);
    EXPECT_FLOAT_EQ(Kinematic3D::EaseBlend(0.0F, kDeltaTime), 0.0F);
    EXPECT_FLOAT_EQ(Kinematic3D::EaseBlend(4.0F, kDeltaTime), 1.0F);
    EXPECT_FLOAT_EQ(Kinematic3D::EaseBlend(-2.0F, kDeltaTime), 0.0F);
    EXPECT_FLOAT_EQ(Kinematic3D::EaseBlend(std::numeric_limits<float>::quiet_NaN(), kDeltaTime), 0.0F);
}

TEST(Kinematic3DTest, ResolveWorldTarget_IdentityKeepsLocalAndRotationTurnsIt) {
    const Math::Vector3 local{1.0F, 0.0F, 0.0F};
    const Math::Vector3 identity = Kinematic3D::ResolveWorldTarget(local, {});
    EXPECT_NEAR(identity.x, 1.0F, 1.0e-5F);
    EXPECT_NEAR(identity.y, 0.0F, 1.0e-5F);
    EXPECT_NEAR(identity.z, 0.0F, 1.0e-5F);

    Math::Quaternion quarterTurn{};
    ASSERT_TRUE(Math::TryMakeAxisAngle({0.0F, 1.0F, 0.0F}, 1.5707963F, quarterTurn));
    const Math::Vector3 turned = Kinematic3D::ResolveWorldTarget(local, quarterTurn);
    const Math::Vector3 expected = Math::RotateVector(quarterTurn, local);
    EXPECT_NEAR(turned.x, expected.x, 1.0e-5F);
    EXPECT_NEAR(turned.y, expected.y, 1.0e-5F);
    EXPECT_NEAR(turned.z, expected.z, 1.0e-5F);
}

TEST(Kinematic3DTest, ResolveWorldTarget_NonFiniteIsStanding) {
    const Math::Vector3 nan{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    const Math::Vector3 world = Kinematic3D::ResolveWorldTarget(nan, {});
    EXPECT_FLOAT_EQ(world.x, 0.0F);
    EXPECT_FLOAT_EQ(world.y, 0.0F);
    EXPECT_FLOAT_EQ(world.z, 0.0F);
}

TEST(Kinematic3DTest, EaseVelocity_SnapsAtOneAndHoldsAtZero) {
    const Math::Vector3 current{1.0F, 0.0F, 0.0F};
    const Math::Vector3 target{3.0F, 0.0F, 0.0F};
    const Math::Vector3 snapped = Kinematic3D::EaseVelocity(current, target, 1.0F, kDeltaTime);
    EXPECT_NEAR(snapped.x, 3.0F, 1.0e-5F);
    const Math::Vector3 held = Kinematic3D::EaseVelocity(current, target, 0.0F, kDeltaTime);
    EXPECT_NEAR(held.x, 1.0F, 1.0e-5F);
}

} // namespace
} // namespace UVE::Scene::Tests
