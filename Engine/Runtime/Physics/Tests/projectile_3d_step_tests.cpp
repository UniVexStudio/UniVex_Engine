// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "projectile_3d_step.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "collider_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "quaternion.h"
#include "vector3.h"
#include "memory_manager.h"
#include "projectile_3d.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::ColliderComponent;
using Scene::Entity;
using Scene::Projectile3DComponent;
using Scene::Projectile3DHitPolicy;

constexpr float kEpsilon = 1e-4F;
/// The 60Hz fixed step, for cases about integration and lifetime.
constexpr float kDeltaTime = 1.0F / 60.0F;
/// A 10Hz fixed step - a real scene setting - for the contact cases: at 6 m/s it is 0.6m of
/// travel, which is what lets a projectile actually reach a wall placed half a metre away while
/// staying inside the step's own sweep. The sweep is one segment per step by design; a projectile
/// only ever reaches what this step's motion reaches.
constexpr float kContactStep = 0.1F;

class Projectile3DStepTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;

    [[nodiscard]] Entity MakeProjectile(const Math::Vector3& position,
                                              const Projectile3DComponent& component) {
        const Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<Projectile3DComponent>(entity, component);
        sceneGraph.Update(entityManager);
        return entity;
    }

    [[nodiscard]] Entity MakeCollider(const Math::Vector3& position,
                                            const Math::Vector3& halfExtents = {0.5F, 0.5F, 0.5F},
                                            const std::uint32_t layer = 1U) {
        const Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        ColliderComponent collider{halfExtents};
        collider.collisionLayer = layer;
        entityManager.AddComponent<ColliderComponent>(entity, collider);
        sceneGraph.Update(entityManager);
        return entity;
    }

    [[nodiscard]] Projectile3DStepResult Step(const Entity entity,
                                                    const float deltaTimeSeconds = kDeltaTime) {
        return StepProjectile3D(entityManager, sceneGraph, entity, deltaTimeSeconds);
    }

    /// Steps and then re-resolves world transforms, which is what the engine's own frame does
    /// after every fixed step - a second step otherwise reads the first step's stale world pose.
    [[nodiscard]] Projectile3DStepResult StepAndResolve(const Entity entity,
                                                              const float deltaTimeSeconds = kDeltaTime) {
        const Projectile3DStepResult result =
            StepProjectile3D(entityManager, sceneGraph, entity, deltaTimeSeconds);
        sceneGraph.Update(entityManager);
        return result;
    }

    [[nodiscard]] Math::Vector3 LocalPosition(const Entity entity) {
        return entityManager.GetComponent<Scene::TransformComponent>(entity).localPosition;
    }

    [[nodiscard]] Projectile3DComponent Component(const Entity entity) {
        return entityManager.GetComponent<Projectile3DComponent>(entity);
    }
};

TEST_F(Projectile3DStepTest, FliesTheFullStepWhenNothingIsInTheWay) {
    Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    const Entity entity = MakeProjectile({}, projectile);

    const Projectile3DStepResult result = Step(entity);

    EXPECT_TRUE(result.IsStepped());
    EXPECT_FALSE(result.IsRefused());
    EXPECT_FALSE(result.hasHit);
    EXPECT_NEAR(result.movedDistance, 0.1F, kEpsilon);
    EXPECT_NEAR(LocalPosition(entity).x, 0.1F, kEpsilon);
    EXPECT_NEAR(Component(entity).velocity.x, 6.0F, kEpsilon);
    EXPECT_TRUE(Component(entity).active);
    EXPECT_FALSE(Component(entity).hit);
    EXPECT_NEAR(Component(entity).remainingLifetime, 10.0F - kDeltaTime, kEpsilon);
}

TEST_F(Projectile3DStepTest, AccelerationIsIntegratedAndIsWhatTheAuthoredVelocityAccumulates) {
    Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{1.0F, 0.0F, 0.0F};
    projectile.acceleration = Math::Vector3{1.0F, 0.0F, 0.0F};
    const Entity entity = MakeProjectile({}, projectile);

    static_cast<void>(StepAndResolve(entity));
    static_cast<void>(StepAndResolve(entity));

    // Two steps of +1 m/s^2 at 60Hz: velocity grew by 2 * dt, because acceleration is integrated
    // into the authored velocity before the step's motion is measured (the contract the pre-sweep
    // engine-core tick documented).
    EXPECT_NEAR(Component(entity).velocity.x, 1.0F + 2.0F * kDeltaTime, kEpsilon);
    EXPECT_NEAR(LocalPosition(entity).x, (1.0F + kDeltaTime) * kDeltaTime + (1.0F + 2.0F * kDeltaTime) * kDeltaTime,
                kEpsilon);
}

TEST_F(Projectile3DStepTest, StopsAtTheContactWhenTheSphereMeetsAWall) {
    Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.1F;
    const Entity projectileEntity = MakeProjectile({}, projectile);
    const Entity wall = MakeCollider({1.0F, 0.0F, 0.0F});

    const Projectile3DStepResult result = Step(projectileEntity, kContactStep);

    EXPECT_TRUE(result.Stopped());
    EXPECT_TRUE(result.hasHit);
    EXPECT_EQ(result.hitEntity, wall);
    // The wall's near face is at x = 0.5; the sphere's centre stops its own radius short of it.
    EXPECT_NEAR(result.hitPosition.x, 0.5F, kEpsilon);
    EXPECT_NEAR(result.hitPosition.y, 0.0F, kEpsilon);
    EXPECT_NEAR(result.hitNormal.x, -1.0F, kEpsilon);
    EXPECT_NEAR(result.impactSpeed, 6.0F, kEpsilon);
    EXPECT_NEAR(result.movedDistance, 0.4F, kEpsilon);

    const Math::Vector3 position = LocalPosition(projectileEntity);
    EXPECT_NEAR(position.x, 0.4F, kEpsilon);
    const Projectile3DComponent after = Component(projectileEntity);
    EXPECT_FALSE(after.active);
    EXPECT_NEAR(after.velocity.x, 0.0F, kEpsilon);
    // The result is sticky: it is the last contact, not this frame's, and it is written down.
    EXPECT_TRUE(after.hit);
    EXPECT_EQ(after.hitEntity, wall);
    EXPECT_NEAR(after.hitPosition.x, 0.5F, kEpsilon);
    EXPECT_NEAR(after.hitNormal.x, -1.0F, kEpsilon);
    EXPECT_NEAR(after.impactSpeed, 6.0F, kEpsilon);
}

TEST_F(Projectile3DStepTest, TheRadiusIsWhatSweepsAndNotAnInfinitelyThinRay) {
    // The wall's box sits at (1.2, 0.8, 0): its near face is at x = 0.7 and its lowest face at
    // y = 0.3. A 0.1m sphere sweeping along +X at y = 0 misses it (its expanded box starts at
    // y = 0.2); a 0.6m sphere does not (its expanded box reaches y = -0.3).
    const Entity wall = MakeCollider({1.2F, 0.8F, 0.0F});

    Projectile3DComponent thin;
    thin.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    thin.radius = 0.1F;
    const Entity thinEntity = MakeProjectile({}, thin);
    const Projectile3DStepResult thinResult = Step(thinEntity, kContactStep);
    EXPECT_FALSE(thinResult.hasHit);
    EXPECT_NEAR(LocalPosition(thinEntity).x, 0.6F, kEpsilon);

    Projectile3DComponent fat;
    fat.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    fat.radius = 0.6F;
    const Entity fatEntity = MakeProjectile({}, fat);
    const Projectile3DStepResult fatResult = Step(fatEntity, kContactStep);
    EXPECT_TRUE(fatResult.hasHit);
    EXPECT_EQ(fatResult.hitEntity, wall);
    // The sphere's centre stops where the expanded AABB begins - 0.7 - 0.6 = 0.1 - and the
    // reported surface point is that face, half a radius further along the sweep.
    EXPECT_NEAR(LocalPosition(fatEntity).x, 0.1F, kEpsilon);
    EXPECT_NEAR(fatResult.hitPosition.x, 0.7F, kEpsilon);
}

TEST_F(Projectile3DStepTest, TheMaskDecidesWhichLayersCanBeHit) {
    const Entity wall = MakeCollider({1.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F}, 4U);

    Projectile3DComponent masked;
    masked.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    masked.collisionMask = 1U;
    const Entity maskedEntity = MakeProjectile({}, masked);
    EXPECT_FALSE(Step(maskedEntity, kContactStep).hasHit);

    Projectile3DComponent open;
    open.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    open.collisionMask = 1U | 4U;
    const Entity openEntity = MakeProjectile({}, open);
    const Projectile3DStepResult openResult = Step(openEntity, kContactStep);
    EXPECT_TRUE(openResult.hasHit);
    EXPECT_EQ(openResult.hitEntity, wall);

    // A mask of zero matches no layer at all, so nothing is swept and nothing is hit.
    Projectile3DComponent blind;
    blind.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    blind.collisionMask = 0U;
    const Entity blindEntity = MakeProjectile({}, blind);
    EXPECT_FALSE(Step(blindEntity, kContactStep).hasHit);
}

TEST_F(Projectile3DStepTest, NeverHitsTheIgnoreEntity) {
    Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.1F;
    const Entity owner = MakeCollider({1.0F, 0.0F, 0.0F});
    projectile.ignoreEntity = owner;
    const Entity entity = MakeProjectile({}, projectile);

    const Projectile3DStepResult result = Step(entity, kContactStep);

    EXPECT_FALSE(result.hasHit);
    EXPECT_TRUE(result.IsStepped());
    EXPECT_NEAR(LocalPosition(entity).x, 0.6F, kEpsilon);
    EXPECT_TRUE(Component(entity).active);
}

TEST_F(Projectile3DStepTest, HitsTheWallPastTheIgnoredOwner) {
    Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.1F;
    const Entity owner = MakeCollider({0.3F, 0.0F, 0.0F}, {0.1F, 0.5F, 0.5F});
    const Entity wall = MakeCollider({1.0F, 0.0F, 0.0F});
    projectile.ignoreEntity = owner;
    const Entity entity = MakeProjectile({}, projectile);

    const Projectile3DStepResult result = Step(entity, kContactStep);

    EXPECT_TRUE(result.Stopped());
    EXPECT_TRUE(result.hasHit);
    EXPECT_EQ(result.hitEntity, wall);
    EXPECT_NE(result.hitEntity, owner);
    EXPECT_NEAR(LocalPosition(entity).x, 0.4F, kEpsilon);
}

TEST_F(Projectile3DStepTest, NeverHitsTheEntityItIsAttachedTo) {
    Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.5F;
    const Entity entity = MakeProjectile({}, projectile);
    // Give the projectile its own collider: it is in the world's collider cache too, and a sweep
    // that did not ignore its own entity would report the projectile as its own obstacle.
    entityManager.AddComponent<ColliderComponent>(entity, ColliderComponent{{0.5F, 0.5F, 0.5F}});
    sceneGraph.Update(entityManager);

    const Projectile3DStepResult result = Step(entity);

    EXPECT_FALSE(result.hasHit);
    EXPECT_TRUE(result.IsStepped());
}

TEST_F(Projectile3DStepTest, BounceReflectsTheMotionAndKeepsFlying) {
    Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.1F;
    projectile.hitPolicy = Projectile3DHitPolicy::Bounce;
    projectile.restitution = 0.5F;
    projectile.friction = 0.0F;
    const Entity entity = MakeProjectile({}, projectile);
    const Entity wall = MakeCollider({1.0F, 0.0F, 0.0F});

    const Projectile3DStepResult result = StepAndResolve(entity, kContactStep);

    EXPECT_TRUE(result.Bounced());
    EXPECT_FALSE(result.Stopped());
    EXPECT_TRUE(result.hasHit);
    EXPECT_EQ(result.hitEntity, wall);
    EXPECT_EQ(result.bounceCount, 1U);
    const Projectile3DComponent after = Component(entity);
    EXPECT_TRUE(after.active);
    EXPECT_NEAR(after.velocity.x, -3.0F, kEpsilon);
    EXPECT_EQ(after.bounceCount, 1U);
    // It leaves the surface instead of resting on it, so the next step's sweep starts outside.
    EXPECT_LT(LocalPosition(entity).x, 0.4F);

    const Projectile3DStepResult second = StepAndResolve(entity);
    EXPECT_TRUE(second.IsStepped());
    EXPECT_FALSE(second.hasHit);
    EXPECT_LT(LocalPosition(entity).x, 0.4F);
}

TEST_F(Projectile3DStepTest, BounceFrictionDampsTheComponentAlongTheSurface) {
    Projectile3DComponent projectile;
    // Down and forward at a floor box whose top face is at y = -0.5.
    projectile.velocity = Math::Vector3{0.0F, -6.0F, 6.0F};
    projectile.radius = 0.1F;
    projectile.hitPolicy = Projectile3DHitPolicy::Bounce;
    projectile.restitution = 0.5F;
    projectile.friction = 0.5F;
    const Entity entity = MakeProjectile({}, projectile);
    static_cast<void>(MakeCollider({0.0F, -1.0F, 0.0F}, {4.0F, 0.5F, 4.0F}));

    const Projectile3DStepResult result = Step(entity, kContactStep);

    EXPECT_TRUE(result.Bounced());
    // The speed into the floor comes back at 0.5, and the speed along it at 1 - 0.5.
    EXPECT_NEAR(Component(entity).velocity.y, 3.0F, kEpsilon);
    EXPECT_NEAR(Component(entity).velocity.z, 3.0F, kEpsilon);
    EXPECT_NEAR(result.impactSpeed, 6.0F, kEpsilon);
}

TEST_F(Projectile3DStepTest, ABounceThatLeavesNothingToFlyWithIsAStop) {
    Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    projectile.hitPolicy = Projectile3DHitPolicy::Bounce;
    projectile.restitution = 0.0F;
    projectile.friction = 1.0F;
    const Entity entity = MakeProjectile({}, projectile);
    static_cast<void>(MakeCollider({1.0F, 0.0F, 0.0F}));

    const Projectile3DStepResult result = Step(entity, kContactStep);

    EXPECT_TRUE(result.hasHit);
    EXPECT_TRUE(result.Stopped());
    EXPECT_FALSE(result.Bounced());
    EXPECT_EQ(result.bounceCount, 0U);
    const Projectile3DComponent after = Component(entity);
    EXPECT_FALSE(after.active);
    EXPECT_NEAR(after.velocity.x, 0.0F, kEpsilon);
}

TEST_F(Projectile3DStepTest, AnOverlapTheStepBeginsInsideIsNotAContact) {
    // The projectile starts inside the wall's expanded box - exactly what happens to a projectile
    // fired from inside a launcher's own volume. There is no normal and no surface, so it is
    // allowed to leave instead of dying on the spot.
    Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.5F;
    const Entity entity = MakeProjectile({}, projectile);
    static_cast<void>(MakeCollider({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}));

    const Projectile3DStepResult result = Step(entity);

    EXPECT_FALSE(result.hasHit);
    EXPECT_TRUE(result.IsStepped());
    EXPECT_NEAR(result.movedDistance, 0.1F, kEpsilon);
    EXPECT_NEAR(LocalPosition(entity).x, 0.1F, kEpsilon);
}

TEST_F(Projectile3DStepTest, LifetimeExpiryEndsTheMotionAndClearsActive) {
    Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    projectile.maxLifetime = 0.02F;
    projectile.remainingLifetime = 0.02F;
    const Entity entity = MakeProjectile({}, projectile);

    const Projectile3DStepResult first = StepAndResolve(entity);
    EXPECT_TRUE(first.IsStepped());
    EXPECT_FALSE(first.Expired());
    EXPECT_NEAR(Component(entity).remainingLifetime, 0.02F - kDeltaTime, kEpsilon);

    const Projectile3DStepResult second = StepAndResolve(entity);
    EXPECT_TRUE(second.Expired());
    EXPECT_FALSE(second.hasHit);
    EXPECT_FLOAT_EQ(Component(entity).remainingLifetime, 0.0F);
    EXPECT_FALSE(Component(entity).active);
    const Math::Vector3 restingPosition = LocalPosition(entity);

    const Projectile3DStepResult third = Step(entity);
    EXPECT_TRUE(third.IsDisabled());
    EXPECT_EQ(LocalPosition(entity), restingPosition);
}

TEST_F(Projectile3DStepTest, ExpiryAndABounceInTheSameStepAreBothReported) {
    Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.1F;
    projectile.maxLifetime = 0.01F;
    projectile.remainingLifetime = 0.01F;
    projectile.hitPolicy = Projectile3DHitPolicy::Bounce;
    projectile.restitution = 1.0F;
    projectile.friction = 0.0F;
    const Entity entity = MakeProjectile({}, projectile);
    static_cast<void>(MakeCollider({1.0F, 0.0F, 0.0F}));

    // The step is longer than the lifetime on purpose: the sweep and the bounce happen first, and
    // the countdown reaches zero in the same step.
    const Projectile3DStepResult result = Step(entity, kContactStep);

    // `code` names the most consequential thing that happened; the flags stay independent, so a
    // step that bounced and expired in the same step loses neither fact.
    EXPECT_EQ(result.code, Projectile3DStepCode::Expired);
    EXPECT_TRUE(result.Bounced());
    EXPECT_TRUE(result.Expired());
    EXPECT_TRUE(result.hasHit);
    EXPECT_EQ(result.bounceCount, 1U);
    const Projectile3DComponent after = Component(entity);
    EXPECT_FALSE(after.active);
    EXPECT_NEAR(after.velocity.x, -6.0F, kEpsilon);
    EXPECT_TRUE(after.hit);
}

TEST_F(Projectile3DStepTest, RefusesWhatItCannotDriveWithoutTouchingAnything) {
    Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    const Entity entity = MakeProjectile({}, projectile);

    const Projectile3DStepResult zeroTime = Step(entity, 0.0F);
    EXPECT_EQ(zeroTime.code, Projectile3DStepCode::InvalidDeltaTime);
    EXPECT_TRUE(zeroTime.IsRefused());
    const Projectile3DStepResult nanTime = Step(entity, std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(nanTime.code, Projectile3DStepCode::InvalidDeltaTime);
    EXPECT_EQ(LocalPosition(entity), Math::Vector3{});
    EXPECT_NEAR(Component(entity).remainingLifetime, 10.0F, kEpsilon);

    const Projectile3DStepResult unknown =
        Step(Entity{9999U, 0U});
    EXPECT_EQ(unknown.code, Projectile3DStepCode::UnknownEntity);

    // An entity that is not a projectile at all.
    const Entity plain = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, plain, Scene::TransformComponent{});
    sceneGraph.Update(entityManager);
    EXPECT_EQ(Step(plain).code, Projectile3DStepCode::NotAProjectile);

    // A projectile with no transform at all: there is nowhere to sweep from and nowhere to write.
    const Entity untransformed = entityManager.CreateEntity();
    entityManager.AddComponent<Projectile3DComponent>(untransformed, projectile);
    EXPECT_EQ(Step(untransformed).code, Projectile3DStepCode::MissingTransform);

    // A component past its own contract: a bounce coefficient outside 0..1 is refused, and the
    // entity is left exactly where it was.
    Projectile3DComponent malformed = projectile;
    malformed.restitution = 2.0F;
    const Entity malformedEntity = MakeProjectile({}, malformed);
    const Projectile3DStepResult refused = Step(malformedEntity);
    EXPECT_EQ(refused.code, Projectile3DStepCode::InvalidComponent);
    EXPECT_TRUE(refused.IsRefused());
    EXPECT_EQ(LocalPosition(malformedEntity), Math::Vector3{});
}

TEST_F(Projectile3DStepTest, ADisabledProjectileIsNotIntegratedOrCountedDown) {
    Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    projectile.active = false;
    const Entity entity = MakeProjectile({}, projectile);

    const Projectile3DStepResult result = Step(entity);

    EXPECT_TRUE(result.IsDisabled());
    EXPECT_FALSE(result.hasHit);
    EXPECT_EQ(LocalPosition(entity), Math::Vector3{});
    EXPECT_FLOAT_EQ(Component(entity).remainingLifetime, 10.0F);
    EXPECT_NEAR(Component(entity).velocity.x, 6.0F, kEpsilon);
}

TEST_F(Projectile3DStepTest, TheSweepRunsAlongTheObjectsOwnRotationNotItsLocalAxes) {
    Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.1F;
    const Entity entity = entityManager.CreateEntity();
    Scene::TransformComponent transform{};
    transform.localRotation = Math::Quaternion{0.0F, 0.70710678F, 0.0F, 0.70710678F};  // +90 deg about Y
    sceneGraph.AttachTransform(entityManager, entity, transform);
    entityManager.AddComponent<Projectile3DComponent>(entity, projectile);
    sceneGraph.Update(entityManager);
    // Local +X is world -Z after that rotation, so the wall is the one at -Z.
    const Entity wall = MakeCollider({0.0F, 0.0F, -1.0F});

    const Projectile3DStepResult result = Step(entity, kContactStep);

    EXPECT_TRUE(result.hasHit);
    EXPECT_EQ(result.hitEntity, wall);
    EXPECT_NEAR(result.hitNormal.z, 1.0F, kEpsilon);
    // The committed motion is the object's own: its local +X moved, and its world motion was -Z.
    EXPECT_NEAR(LocalPosition(entity).x, 0.4F, kEpsilon);
    EXPECT_NEAR(LocalPosition(entity).z, 0.0F, kEpsilon);
}

TEST_F(Projectile3DStepTest, BouncesTheContactNormalBackIntoTheObjectsOwnAxes) {
    Projectile3DComponent projectile;
    projectile.velocity = Math::Vector3{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.1F;
    projectile.hitPolicy = Projectile3DHitPolicy::Bounce;
    projectile.restitution = 1.0F;
    projectile.friction = 0.0F;
    const Entity entity = entityManager.CreateEntity();
    Scene::TransformComponent transform{};
    transform.localRotation = Math::Quaternion{0.0F, 0.70710678F, 0.0F, 0.70710678F};  // +90 deg about Y
    sceneGraph.AttachTransform(entityManager, entity, transform);
    entityManager.AddComponent<Projectile3DComponent>(entity, projectile);
    sceneGraph.Update(entityManager);
    static_cast<void>(MakeCollider({0.0F, 0.0F, -1.0F}));

    const Projectile3DStepResult result = Step(entity, kContactStep);

    EXPECT_TRUE(result.Bounced());
    // The surface's world normal faces +Z; in the projectile's own axes that is -X, and a
    // reflection of local +X motion about it is local -X motion.
    EXPECT_NEAR(Component(entity).velocity.x, -6.0F, kEpsilon);
    EXPECT_NEAR(Component(entity).velocity.z, 0.0F, kEpsilon);
}

} // namespace
} // namespace UVE::Physics::Tests
