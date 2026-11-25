// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>

#include <gtest/gtest.h>

#include "entity.h"
#include "quaternion.h"
#include "vector3.h"
#include "ray_cast_3d.h"

namespace UVE::Scene::Tests {
namespace {

constexpr Entity kRay{1U, 1U};
constexpr Entity kOwner{2U, 1U};
constexpr Entity kWall{3U, 1U};

TEST(RayCast3DTest, IsCasting_FollowsEnabled) {
    RayCast3DComponent rayCast{};
    EXPECT_TRUE(RayCast3D::IsCasting(rayCast));
    rayCast.enabled = false;
    EXPECT_FALSE(RayCast3D::IsCasting(rayCast));
}

TEST(RayCast3DTest, AcceptsTarget_SkipsSelfAndExclusionPrefix) {
    RayCast3DComponent rayCast{};
    EXPECT_TRUE(RayCast3D::AcceptsTarget(kRay, rayCast, kWall));
    EXPECT_FALSE(RayCast3D::AcceptsTarget(kRay, rayCast, kRay));
    EXPECT_FALSE(RayCast3D::AcceptsTarget(kRay, rayCast, kInvalidEntity));

    rayCast.exclusions[0] = kOwner;
    EXPECT_EQ(RayCast3D::ExclusionCount(rayCast), 1U);
    EXPECT_EQ(RayCast3D::ExclusionSpan(rayCast).size(), 1U);
    EXPECT_FALSE(RayCast3D::AcceptsTarget(kRay, rayCast, kOwner));
    EXPECT_TRUE(RayCast3D::AcceptsTarget(kRay, rayCast, kWall));
}

TEST(RayCast3DTest, ResolveWorldDirection_IdentityKeepsLocalAndRotationTurnsIt) {
    const Math::Vector3 local{0.0F, -1.0F, 0.0F};
    const Math::Vector3 identity = RayCast3D::ResolveWorldDirection(local, {});
    EXPECT_NEAR(identity.x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(identity.y, -1.0F, 1.0e-5F);
    EXPECT_NEAR(identity.z, 0.0F, 1.0e-5F);

    Math::Quaternion quarterTurn{};
    ASSERT_TRUE(Math::TryMakeAxisAngle({0.0F, 1.0F, 0.0F}, 1.5707963F, quarterTurn));
    const Math::Vector3 turned = RayCast3D::ResolveWorldDirection({1.0F, 0.0F, 0.0F}, quarterTurn);
    const Math::Vector3 expected = Math::RotateVector(quarterTurn, {1.0F, 0.0F, 0.0F});
    EXPECT_NEAR(turned.x, expected.x, 1.0e-5F);
    EXPECT_NEAR(turned.y, expected.y, 1.0e-5F);
    EXPECT_NEAR(turned.z, expected.z, 1.0e-5F);
}

TEST(RayCast3DTest, ResolveWorldDirection_NonFiniteIsStanding) {
    const Math::Vector3 nan{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    const Math::Vector3 world = RayCast3D::ResolveWorldDirection(nan, {});
    EXPECT_FLOAT_EQ(world.x, 0.0F);
    EXPECT_FLOAT_EQ(world.y, 0.0F);
    EXPECT_FLOAT_EQ(world.z, 0.0F);
}

TEST(RayCast3DTest, ClearAndRecordHit_WriteThePerFrameResult) {
    RayCast3DComponent rayCast{};
    RayCast3D::RecordHit(rayCast, kWall, Math::Vector3{1.0F, 2.0F, 3.0F},
                               Math::Vector3{0.0F, 1.0F, 0.0F});
    EXPECT_TRUE(rayCast.hit);
    EXPECT_EQ(rayCast.hitEntity, kWall);
    EXPECT_EQ(rayCast.hitPosition, (Math::Vector3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(rayCast.hitNormal, (Math::Vector3{0.0F, 1.0F, 0.0F}));

    RayCast3D::ClearResult(rayCast);
    EXPECT_FALSE(rayCast.hit);
    EXPECT_EQ(rayCast.hitEntity, kInvalidEntity);
    EXPECT_EQ(rayCast.hitPosition, Math::Vector3{});
    EXPECT_EQ(rayCast.hitNormal, Math::Vector3{});
}

} // namespace
} // namespace UVE::Scene::Tests
