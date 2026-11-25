// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>

#include <gtest/gtest.h>

#include "entity.h"
#include "quaternion.h"
#include "vector3.h"
#include "projectile_3d.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kDeltaTime = 1.0F / 60.0F;
constexpr Entity kProjectile{1U, 1U};
constexpr Entity kOwner{2U, 1U};
constexpr Entity kWall{3U, 1U};

TEST(Projectile3DTest, IsFlying_FollowsActive) {
    Projectile3DComponent projectile{};
    EXPECT_TRUE(Projectile3D::IsFlying(projectile));
    projectile.active = false;
    EXPECT_FALSE(Projectile3D::IsFlying(projectile));
}

TEST(Projectile3DTest, AcceptsObstacle_SkipsSelfAndIgnore) {
    Projectile3DComponent projectile{};
    EXPECT_TRUE(Projectile3D::AcceptsObstacle(kProjectile, projectile, kWall));
    EXPECT_FALSE(Projectile3D::AcceptsObstacle(kProjectile, projectile, kProjectile));
    EXPECT_FALSE(Projectile3D::AcceptsObstacle(kProjectile, projectile, kInvalidEntity));

    projectile.ignoreEntity = kOwner;
    EXPECT_FALSE(Projectile3D::AcceptsObstacle(kProjectile, projectile, kOwner));
    EXPECT_TRUE(Projectile3D::AcceptsObstacle(kProjectile, projectile, kWall));
}

TEST(Projectile3DTest, IntegrateVelocity_AddsAccelerationAndHoldsOnBadTime) {
    const Math::Vector3 velocity{1.0F, 0.0F, 0.0F};
    const Math::Vector3 acceleration{2.0F, 0.0F, 0.0F};
    const Math::Vector3 integrated =
        Projectile3D::IntegrateVelocity(velocity, acceleration, kDeltaTime);
    EXPECT_NEAR(integrated.x, 1.0F + 2.0F * kDeltaTime, 1.0e-5F);

    const Math::Vector3 held = Projectile3D::IntegrateVelocity(velocity, acceleration, 0.0F);
    EXPECT_NEAR(held.x, 1.0F, 1.0e-5F);
}

TEST(Projectile3DTest, ResolveWorldVelocity_IdentityKeepsLocalAndRotationTurnsIt) {
    const Math::Vector3 local{1.0F, 0.0F, 0.0F};
    const Math::Vector3 identity = Projectile3D::ResolveWorldVelocity(local, {});
    EXPECT_NEAR(identity.x, 1.0F, 1.0e-5F);
    EXPECT_NEAR(identity.y, 0.0F, 1.0e-5F);
    EXPECT_NEAR(identity.z, 0.0F, 1.0e-5F);

    Math::Quaternion quarterTurn{};
    ASSERT_TRUE(Math::TryMakeAxisAngle({0.0F, 1.0F, 0.0F}, 1.5707963F, quarterTurn));
    const Math::Vector3 turned = Projectile3D::ResolveWorldVelocity(local, quarterTurn);
    const Math::Vector3 expected = Math::RotateVector(quarterTurn, local);
    EXPECT_NEAR(turned.x, expected.x, 1.0e-5F);
    EXPECT_NEAR(turned.y, expected.y, 1.0e-5F);
    EXPECT_NEAR(turned.z, expected.z, 1.0e-5F);
}

TEST(Projectile3DTest, ResolveWorldVelocity_NonFiniteIsStanding) {
    const Math::Vector3 nan{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    const Math::Vector3 world = Projectile3D::ResolveWorldVelocity(nan, {});
    EXPECT_FLOAT_EQ(world.x, 0.0F);
    EXPECT_FLOAT_EQ(world.y, 0.0F);
    EXPECT_FLOAT_EQ(world.z, 0.0F);
}

TEST(Projectile3DTest, ResolveContact_StopHaltsAndBounceReflects) {
    Projectile3DComponent projectile{};
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    const Math::Vector3 wallNormal{-1.0F, 0.0F, 0.0F};

    const Projectile3D::ContactMotion stopped =
        Projectile3D::ResolveContact(projectile, projectile.velocity, wallNormal);
    EXPECT_TRUE(stopped.stopped);
    EXPECT_FALSE(stopped.bounced);
    EXPECT_NEAR(stopped.velocity.x, 0.0F, 1.0e-5F);

    projectile.hitPolicy = Projectile3DHitPolicy::Bounce;
    projectile.restitution = 0.5F;
    projectile.friction = 0.0F;
    const Projectile3D::ContactMotion bounced =
        Projectile3D::ResolveContact(projectile, projectile.velocity, wallNormal);
    EXPECT_TRUE(bounced.bounced);
    EXPECT_FALSE(bounced.stopped);
    EXPECT_NEAR(bounced.velocity.x, -3.0F, 1.0e-5F);

    projectile.restitution = 0.0F;
    projectile.friction = 1.0F;
    const Projectile3D::ContactMotion dead =
        Projectile3D::ResolveContact(projectile, projectile.velocity, wallNormal);
    EXPECT_TRUE(dead.stopped);
    EXPECT_FALSE(dead.bounced);
    EXPECT_NEAR(dead.velocity.x, 0.0F, 1.0e-5F);
}

TEST(Projectile3DTest, TickLifetime_CountsDownAndExpiryIsZero) {
    EXPECT_NEAR(Projectile3D::TickLifetime(10.0F, kDeltaTime), 10.0F - kDeltaTime, 1.0e-5F);
    EXPECT_FALSE(Projectile3D::HasExpired(10.0F - kDeltaTime));
    EXPECT_FLOAT_EQ(Projectile3D::TickLifetime(0.01F, kDeltaTime), 0.0F);
    EXPECT_TRUE(Projectile3D::HasExpired(0.0F));
}

TEST(Projectile3DTest, RecordHit_WritesTheStickyContact) {
    Projectile3DComponent projectile{};
    Projectile3D::RecordHit(projectile, kWall, Math::Vector3{1.0F, 2.0F, 3.0F},
                                  Math::Vector3{0.0F, 1.0F, 0.0F}, 12.0F);
    EXPECT_TRUE(projectile.hit);
    EXPECT_EQ(projectile.hitEntity, kWall);
    EXPECT_EQ(projectile.hitPosition, (Math::Vector3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(projectile.hitNormal, (Math::Vector3{0.0F, 1.0F, 0.0F}));
    EXPECT_FLOAT_EQ(projectile.impactSpeed, 12.0F);
}

} // namespace
} // namespace UVE::Scene::Tests
