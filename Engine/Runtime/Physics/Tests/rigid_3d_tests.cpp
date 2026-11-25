// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>

#include <gtest/gtest.h>

#include "rigid_3d_component.h"
#include "vector3.h"
#include "rigid_3d.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kDeltaTime = 0.1F;

TEST(Rigid3DTest, IsDynamic_FollowsKinematic) {
    Rigid3DComponent body{};
    EXPECT_TRUE(Rigid3D::IsDynamic(body));
    body.isKinematic = true;
    EXPECT_FALSE(Rigid3D::IsDynamic(body));
}

TEST(Rigid3DTest, InverseMass_ZeroWhenKinematicOrMassless) {
    Rigid3DComponent body{};
    body.mass = 2.0F;
    EXPECT_FLOAT_EQ(Rigid3D::InverseMass(body), 0.5F);

    body.mass = 0.0F;
    EXPECT_FLOAT_EQ(Rigid3D::InverseMass(body), 0.0F);

    body.mass = 2.0F;
    body.isKinematic = true;
    EXPECT_FLOAT_EQ(Rigid3D::InverseMass(body), 0.0F);
}

TEST(Rigid3DTest, IntegrateLinearVelocity_AddsScaledGravityThenDrag) {
    const Math::Vector3 gravity{0.0F, -10.0F, 0.0F};
    const auto fallen =
        Rigid3D::IntegrateLinearVelocity({}, gravity, 1.0F, 0.0F, kDeltaTime);
    ASSERT_TRUE(fallen.has_value());
    EXPECT_NEAR(fallen->y, -1.0F, 1.0e-5F);

    const auto scaled =
        Rigid3D::IntegrateLinearVelocity({}, gravity, 0.0F, 0.0F, kDeltaTime);
    ASSERT_TRUE(scaled.has_value());
    EXPECT_NEAR(scaled->y, 0.0F, 1.0e-5F);

    const auto dragged =
        Rigid3D::IntegrateLinearVelocity({10.0F, 0.0F, 0.0F}, {}, 1.0F, 1.0F, kDeltaTime);
    ASSERT_TRUE(dragged.has_value());
    EXPECT_NEAR(dragged->x, 9.0F, 1.0e-5F);
}

TEST(Rigid3DTest, IntegrateLinearVelocity_RefusesNonFiniteTime) {
    EXPECT_FALSE(Rigid3D::IntegrateLinearVelocity({}, {0.0F, -10.0F, 0.0F}, 1.0F, 0.0F,
                                                        std::numeric_limits<float>::quiet_NaN())
                     .has_value());
    EXPECT_FALSE(
        Rigid3D::IntegrateLinearVelocity({}, {0.0F, -10.0F, 0.0F}, 1.0F, 0.0F, -0.1F).has_value());
}

TEST(Rigid3DTest, DeflectVelocity_RejectsIntoTheSurfaceAndLeavesSeparating) {
    const Math::Vector3 intoTheWall{6.0F, 0.0F, 0.0F};
    const Math::Vector3 towardOther{1.0F, 0.0F, 0.0F};
    const Math::Vector3 bounced = Rigid3D::DeflectVelocity(intoTheWall, towardOther, 0.0F, 0.5F);
    EXPECT_NEAR(bounced.x, -3.0F, 1.0e-5F);

    const Math::Vector3 sliding = Rigid3D::DeflectVelocity({6.0F, 2.0F, 0.0F}, towardOther, 0.5F, 0.0F);
    EXPECT_NEAR(sliding.x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(sliding.y, 1.0F, 1.0e-5F);

    const Math::Vector3 separating = Rigid3D::DeflectVelocity({-2.0F, 0.0F, 0.0F}, towardOther, 0.0F, 1.0F);
    EXPECT_NEAR(separating.x, -2.0F, 1.0e-5F);
}

} // namespace
} // namespace UVE::Scene::Tests
