// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/spring_arm_3d_uve.h"

namespace UVE::Scene::Tests {
namespace {

TEST(SpringArm3DNodeUVETest, IsCastingUVE_FollowsEnabled) {
    SpringArm3DComponentUVE arm{};
    EXPECT_TRUE(SpringArm3DUVE::IsCastingUVE(arm));
    arm.enabled = false;
    EXPECT_FALSE(SpringArm3DUVE::IsCastingUVE(arm));
}

TEST(SpringArm3DNodeUVETest, ResolveWorldAxisUVE_IdentityIsLocalZAndRotationTurnsIt) {
    const Math::Vector3UVE identity = SpringArm3DUVE::ResolveWorldAxisUVE({});
    EXPECT_NEAR(identity.x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(identity.y, 0.0F, 1.0e-5F);
    EXPECT_NEAR(identity.z, 1.0F, 1.0e-5F);

    Math::QuaternionUVE quarterTurn{};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE({0.0F, 1.0F, 0.0F}, 1.5707963F, quarterTurn));
    const Math::Vector3UVE turned = SpringArm3DUVE::ResolveWorldAxisUVE(quarterTurn);
    const Math::Vector3UVE expected = Math::RotateVectorUVE(quarterTurn, SpringArm3DUVE::kArmAxisUVE);
    EXPECT_NEAR(turned.x, expected.x, 1.0e-5F);
    EXPECT_NEAR(turned.y, expected.y, 1.0e-5F);
    EXPECT_NEAR(turned.z, expected.z, 1.0e-5F);
}

TEST(SpringArm3DNodeUVETest, ResolveTargetUVE_ClearIsFullReachAndAHitTakesTheMargin) {
    EXPECT_FLOAT_EQ(SpringArm3DUVE::ResolveTargetUVE(std::nullopt, 0.1F, 4.0F), 4.0F);
    EXPECT_NEAR(SpringArm3DUVE::ResolveTargetUVE(1.5F, 0.1F, 4.0F), 1.4F, 1.0e-6F);
    EXPECT_FLOAT_EQ(SpringArm3DUVE::ResolveTargetUVE(0.05F, 0.1F, 4.0F), 0.0F);
}

TEST(SpringArm3DNodeUVETest, ResolveLengthUVE_RetractionSnapsAndBadTimeHolds) {
    EXPECT_FLOAT_EQ(SpringArm3DUVE::ResolveLengthUVE(4.0F, 1.4F, 8.0F, 1.0F / 60.0F), 1.4F);
    EXPECT_FLOAT_EQ(SpringArm3DUVE::ResolveLengthUVE(2.0F, 4.0F, 8.0F, 0.0F), 2.0F);
    EXPECT_FLOAT_EQ(SpringArm3DUVE::ResolveLengthUVE(2.0F, 4.0F, 8.0F, -1.0F), 2.0F);
    EXPECT_TRUE(std::isnan(SpringArm3DUVE::ResolveLengthUVE(std::numeric_limits<float>::quiet_NaN(), 4.0F, 8.0F,
                                                            1.0F / 60.0F)));
}

} // namespace
} // namespace UVE::Scene::Tests
