// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "quaternion.h"
#include "vector3.h"
#include "spring_arm_3d.h"

namespace UVE::Scene::Tests {
namespace {

TEST(SpringArm3DNodeTest, IsCasting_FollowsEnabled) {
    SpringArm3DComponent arm{};
    EXPECT_TRUE(SpringArm3D::IsCasting(arm));
    arm.enabled = false;
    EXPECT_FALSE(SpringArm3D::IsCasting(arm));
}

TEST(SpringArm3DNodeTest, ResolveWorldAxis_IdentityIsLocalZAndRotationTurnsIt) {
    const Math::Vector3 identity = SpringArm3D::ResolveWorldAxis({});
    EXPECT_NEAR(identity.x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(identity.y, 0.0F, 1.0e-5F);
    EXPECT_NEAR(identity.z, 1.0F, 1.0e-5F);

    Math::Quaternion quarterTurn{};
    ASSERT_TRUE(Math::TryMakeAxisAngle({0.0F, 1.0F, 0.0F}, 1.5707963F, quarterTurn));
    const Math::Vector3 turned = SpringArm3D::ResolveWorldAxis(quarterTurn);
    const Math::Vector3 expected = Math::RotateVector(quarterTurn, SpringArm3D::kArmAxis);
    EXPECT_NEAR(turned.x, expected.x, 1.0e-5F);
    EXPECT_NEAR(turned.y, expected.y, 1.0e-5F);
    EXPECT_NEAR(turned.z, expected.z, 1.0e-5F);
}

TEST(SpringArm3DNodeTest, ResolveTarget_ClearIsFullReachAndAHitTakesTheMargin) {
    EXPECT_FLOAT_EQ(SpringArm3D::ResolveTarget(std::nullopt, 0.1F, 4.0F), 4.0F);
    EXPECT_NEAR(SpringArm3D::ResolveTarget(1.5F, 0.1F, 4.0F), 1.4F, 1.0e-6F);
    EXPECT_FLOAT_EQ(SpringArm3D::ResolveTarget(0.05F, 0.1F, 4.0F), 0.0F);
}

TEST(SpringArm3DNodeTest, ResolveLength_RetractionSnapsAndBadTimeHolds) {
    EXPECT_FLOAT_EQ(SpringArm3D::ResolveLength(4.0F, 1.4F, 8.0F, 1.0F / 60.0F), 1.4F);
    EXPECT_FLOAT_EQ(SpringArm3D::ResolveLength(2.0F, 4.0F, 8.0F, 0.0F), 2.0F);
    EXPECT_FLOAT_EQ(SpringArm3D::ResolveLength(2.0F, 4.0F, 8.0F, -1.0F), 2.0F);
    EXPECT_TRUE(std::isnan(SpringArm3D::ResolveLength(std::numeric_limits<float>::quiet_NaN(), 4.0F, 8.0F,
                                                            1.0F / 60.0F)));
}

} // namespace
} // namespace UVE::Scene::Tests
