// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>

#include <gtest/gtest.h>

#include "uve/component/rigid_3d_component_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/rigid_3d_uve.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kDeltaTimeUVE = 0.1F;

TEST(Rigid3DUVETest, IsDynamicUVE_FollowsKinematic) {
    Rigid3DComponentUVE body{};
    EXPECT_TRUE(Rigid3DUVE::IsDynamicUVE(body));
    body.isKinematic = true;
    EXPECT_FALSE(Rigid3DUVE::IsDynamicUVE(body));
}

TEST(Rigid3DUVETest, InverseMassUVE_ZeroWhenKinematicOrMassless) {
    Rigid3DComponentUVE body{};
    body.mass = 2.0F;
    EXPECT_FLOAT_EQ(Rigid3DUVE::InverseMassUVE(body), 0.5F);

    body.mass = 0.0F;
    EXPECT_FLOAT_EQ(Rigid3DUVE::InverseMassUVE(body), 0.0F);

    body.mass = 2.0F;
    body.isKinematic = true;
    EXPECT_FLOAT_EQ(Rigid3DUVE::InverseMassUVE(body), 0.0F);
}

TEST(Rigid3DUVETest, IntegrateLinearVelocityUVE_AddsScaledGravityThenDrag) {
    const Math::Vector3UVE gravity{0.0F, -10.0F, 0.0F};
    const auto fallen =
        Rigid3DUVE::IntegrateLinearVelocityUVE({}, gravity, 1.0F, 0.0F, kDeltaTimeUVE);
    ASSERT_TRUE(fallen.has_value());
    EXPECT_NEAR(fallen->y, -1.0F, 1.0e-5F);

    const auto scaled =
        Rigid3DUVE::IntegrateLinearVelocityUVE({}, gravity, 0.0F, 0.0F, kDeltaTimeUVE);
    ASSERT_TRUE(scaled.has_value());
    EXPECT_NEAR(scaled->y, 0.0F, 1.0e-5F);

    const auto dragged =
        Rigid3DUVE::IntegrateLinearVelocityUVE({10.0F, 0.0F, 0.0F}, {}, 1.0F, 1.0F, kDeltaTimeUVE);
    ASSERT_TRUE(dragged.has_value());
    EXPECT_NEAR(dragged->x, 9.0F, 1.0e-5F);
}

TEST(Rigid3DUVETest, IntegrateLinearVelocityUVE_RefusesNonFiniteTime) {
    EXPECT_FALSE(Rigid3DUVE::IntegrateLinearVelocityUVE({}, {0.0F, -10.0F, 0.0F}, 1.0F, 0.0F,
                                                        std::numeric_limits<float>::quiet_NaN())
                     .has_value());
    EXPECT_FALSE(
        Rigid3DUVE::IntegrateLinearVelocityUVE({}, {0.0F, -10.0F, 0.0F}, 1.0F, 0.0F, -0.1F).has_value());
}

TEST(Rigid3DUVETest, DeflectVelocityUVE_RejectsIntoTheSurfaceAndLeavesSeparating) {
    const Math::Vector3UVE intoTheWall{6.0F, 0.0F, 0.0F};
    const Math::Vector3UVE towardOther{1.0F, 0.0F, 0.0F};
    const Math::Vector3UVE bounced = Rigid3DUVE::DeflectVelocityUVE(intoTheWall, towardOther, 0.0F, 0.5F);
    EXPECT_NEAR(bounced.x, -3.0F, 1.0e-5F);

    const Math::Vector3UVE sliding = Rigid3DUVE::DeflectVelocityUVE({6.0F, 2.0F, 0.0F}, towardOther, 0.5F, 0.0F);
    EXPECT_NEAR(sliding.x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(sliding.y, 1.0F, 1.0e-5F);

    const Math::Vector3UVE separating = Rigid3DUVE::DeflectVelocityUVE({-2.0F, 0.0F, 0.0F}, towardOther, 0.0F, 1.0F);
    EXPECT_NEAR(separating.x, -2.0F, 1.0e-5F);
}

} // namespace
} // namespace UVE::Scene::Tests
