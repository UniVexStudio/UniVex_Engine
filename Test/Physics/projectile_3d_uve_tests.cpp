// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/projectile_3d_uve.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kDeltaTimeUVE = 1.0F / 60.0F;
constexpr EntityUVE kProjectile{1U, 1U};
constexpr EntityUVE kOwner{2U, 1U};
constexpr EntityUVE kWall{3U, 1U};

TEST(Projectile3DUVETest, IsFlyingUVE_FollowsActive) {
    Projectile3DComponentUVE projectile{};
    EXPECT_TRUE(Projectile3DUVE::IsFlyingUVE(projectile));
    projectile.active = false;
    EXPECT_FALSE(Projectile3DUVE::IsFlyingUVE(projectile));
}

TEST(Projectile3DUVETest, AcceptsObstacleUVE_SkipsSelfAndIgnore) {
    Projectile3DComponentUVE projectile{};
    EXPECT_TRUE(Projectile3DUVE::AcceptsObstacleUVE(kProjectile, projectile, kWall));
    EXPECT_FALSE(Projectile3DUVE::AcceptsObstacleUVE(kProjectile, projectile, kProjectile));
    EXPECT_FALSE(Projectile3DUVE::AcceptsObstacleUVE(kProjectile, projectile, kInvalidEntityUVE));

    projectile.ignoreEntity = kOwner;
    EXPECT_FALSE(Projectile3DUVE::AcceptsObstacleUVE(kProjectile, projectile, kOwner));
    EXPECT_TRUE(Projectile3DUVE::AcceptsObstacleUVE(kProjectile, projectile, kWall));
}

TEST(Projectile3DUVETest, IntegrateVelocityUVE_AddsAccelerationAndHoldsOnBadTime) {
    const Math::Vector3UVE velocity{1.0F, 0.0F, 0.0F};
    const Math::Vector3UVE acceleration{2.0F, 0.0F, 0.0F};
    const Math::Vector3UVE integrated =
        Projectile3DUVE::IntegrateVelocityUVE(velocity, acceleration, kDeltaTimeUVE);
    EXPECT_NEAR(integrated.x, 1.0F + 2.0F * kDeltaTimeUVE, 1.0e-5F);

    const Math::Vector3UVE held = Projectile3DUVE::IntegrateVelocityUVE(velocity, acceleration, 0.0F);
    EXPECT_NEAR(held.x, 1.0F, 1.0e-5F);
}

TEST(Projectile3DUVETest, ResolveWorldVelocityUVE_IdentityKeepsLocalAndRotationTurnsIt) {
    const Math::Vector3UVE local{1.0F, 0.0F, 0.0F};
    const Math::Vector3UVE identity = Projectile3DUVE::ResolveWorldVelocityUVE(local, {});
    EXPECT_NEAR(identity.x, 1.0F, 1.0e-5F);
    EXPECT_NEAR(identity.y, 0.0F, 1.0e-5F);
    EXPECT_NEAR(identity.z, 0.0F, 1.0e-5F);

    Math::QuaternionUVE quarterTurn{};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE({0.0F, 1.0F, 0.0F}, 1.5707963F, quarterTurn));
    const Math::Vector3UVE turned = Projectile3DUVE::ResolveWorldVelocityUVE(local, quarterTurn);
    const Math::Vector3UVE expected = Math::RotateVectorUVE(quarterTurn, local);
    EXPECT_NEAR(turned.x, expected.x, 1.0e-5F);
    EXPECT_NEAR(turned.y, expected.y, 1.0e-5F);
    EXPECT_NEAR(turned.z, expected.z, 1.0e-5F);
}

TEST(Projectile3DUVETest, ResolveWorldVelocityUVE_NonFiniteIsStanding) {
    const Math::Vector3UVE nan{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    const Math::Vector3UVE world = Projectile3DUVE::ResolveWorldVelocityUVE(nan, {});
    EXPECT_FLOAT_EQ(world.x, 0.0F);
    EXPECT_FLOAT_EQ(world.y, 0.0F);
    EXPECT_FLOAT_EQ(world.z, 0.0F);
}

TEST(Projectile3DUVETest, ResolveContactUVE_StopHaltsAndBounceReflects) {
    Projectile3DComponentUVE projectile{};
    projectile.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    const Math::Vector3UVE wallNormal{-1.0F, 0.0F, 0.0F};

    const Projectile3DUVE::ContactMotionUVE stopped =
        Projectile3DUVE::ResolveContactUVE(projectile, projectile.velocity, wallNormal);
    EXPECT_TRUE(stopped.stopped);
    EXPECT_FALSE(stopped.bounced);
    EXPECT_NEAR(stopped.velocity.x, 0.0F, 1.0e-5F);

    projectile.hitPolicy = Projectile3DHitPolicyUVE::Bounce;
    projectile.restitution = 0.5F;
    projectile.friction = 0.0F;
    const Projectile3DUVE::ContactMotionUVE bounced =
        Projectile3DUVE::ResolveContactUVE(projectile, projectile.velocity, wallNormal);
    EXPECT_TRUE(bounced.bounced);
    EXPECT_FALSE(bounced.stopped);
    EXPECT_NEAR(bounced.velocity.x, -3.0F, 1.0e-5F);

    projectile.restitution = 0.0F;
    projectile.friction = 1.0F;
    const Projectile3DUVE::ContactMotionUVE dead =
        Projectile3DUVE::ResolveContactUVE(projectile, projectile.velocity, wallNormal);
    EXPECT_TRUE(dead.stopped);
    EXPECT_FALSE(dead.bounced);
    EXPECT_NEAR(dead.velocity.x, 0.0F, 1.0e-5F);
}

TEST(Projectile3DUVETest, TickLifetimeUVE_CountsDownAndExpiryIsZero) {
    EXPECT_NEAR(Projectile3DUVE::TickLifetimeUVE(10.0F, kDeltaTimeUVE), 10.0F - kDeltaTimeUVE, 1.0e-5F);
    EXPECT_FALSE(Projectile3DUVE::HasExpiredUVE(10.0F - kDeltaTimeUVE));
    EXPECT_FLOAT_EQ(Projectile3DUVE::TickLifetimeUVE(0.01F, kDeltaTimeUVE), 0.0F);
    EXPECT_TRUE(Projectile3DUVE::HasExpiredUVE(0.0F));
}

TEST(Projectile3DUVETest, RecordHitUVE_WritesTheStickyContact) {
    Projectile3DComponentUVE projectile{};
    Projectile3DUVE::RecordHitUVE(projectile, kWall, Math::Vector3UVE{1.0F, 2.0F, 3.0F},
                                  Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 12.0F);
    EXPECT_TRUE(projectile.hit);
    EXPECT_EQ(projectile.hitEntity, kWall);
    EXPECT_EQ(projectile.hitPosition, (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(projectile.hitNormal, (Math::Vector3UVE{0.0F, 1.0F, 0.0F}));
    EXPECT_FLOAT_EQ(projectile.impactSpeed, 12.0F);
}

} // namespace
} // namespace UVE::Scene::Tests
