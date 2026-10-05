// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/projectile_3d_step_uve.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "uve/component/collider_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/projectile_3d_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::ColliderComponentUVE;
using Scene::EntityUVE;
using Scene::Projectile3DComponentUVE;
using Scene::Projectile3DHitPolicyUVE;

constexpr float kEpsilon = 1e-4F;
/// The 60Hz fixed step, for cases about integration and lifetime.
constexpr float kDeltaTime = 1.0F / 60.0F;
/// A 10Hz fixed step - a real scene setting - for the contact cases: at 6 m/s it is 0.6m of
/// travel, which is what lets a projectile actually reach a wall placed half a metre away while
/// staying inside the step's own sweep. The sweep is one segment per step by design; a projectile
/// only ever reaches what this step's motion reaches.
constexpr float kContactStep = 0.1F;

class Projectile3DStepUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;

    [[nodiscard]] EntityUVE MakeProjectileUVE(const Math::Vector3UVE& position,
                                              const Projectile3DComponentUVE& component) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<Projectile3DComponentUVE>(entity, component);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    [[nodiscard]] EntityUVE MakeColliderUVE(const Math::Vector3UVE& position,
                                            const Math::Vector3UVE& halfExtents = {0.5F, 0.5F, 0.5F},
                                            const std::uint32_t layer = 1U) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        ColliderComponentUVE collider{halfExtents};
        collider.collisionLayer = layer;
        entityManager.AddComponentUVE<ColliderComponentUVE>(entity, collider);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    [[nodiscard]] Projectile3DStepResultUVE StepUVE(const EntityUVE entity,
                                                    const float deltaTimeSeconds = kDeltaTime) {
        return StepProjectile3DUVE(entityManager, sceneGraph, entity, deltaTimeSeconds);
    }

    /// Steps and then re-resolves world transforms, which is what the engine's own frame does
    /// after every fixed step - a second step otherwise reads the first step's stale world pose.
    [[nodiscard]] Projectile3DStepResultUVE StepAndResolveUVE(const EntityUVE entity,
                                                              const float deltaTimeSeconds = kDeltaTime) {
        const Projectile3DStepResultUVE result =
            StepProjectile3DUVE(entityManager, sceneGraph, entity, deltaTimeSeconds);
        sceneGraph.UpdateUVE(entityManager);
        return result;
    }

    [[nodiscard]] Math::Vector3UVE LocalPositionUVE(const EntityUVE entity) {
        return entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localPosition;
    }

    [[nodiscard]] Projectile3DComponentUVE ComponentUVE(const EntityUVE entity) {
        return entityManager.GetComponentUVE<Projectile3DComponentUVE>(entity);
    }
};

TEST_F(Projectile3DStepUVETest, FliesTheFullStepWhenNothingIsInTheWay) {
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    const EntityUVE entity = MakeProjectileUVE({}, projectile);

    const Projectile3DStepResultUVE result = StepUVE(entity);

    EXPECT_TRUE(result.IsSteppedUVE());
    EXPECT_FALSE(result.IsRefusedUVE());
    EXPECT_FALSE(result.hasHit);
    EXPECT_NEAR(result.movedDistance, 0.1F, kEpsilon);
    EXPECT_NEAR(LocalPositionUVE(entity).x, 0.1F, kEpsilon);
    EXPECT_NEAR(ComponentUVE(entity).velocity.x, 6.0F, kEpsilon);
    EXPECT_TRUE(ComponentUVE(entity).active);
    EXPECT_FALSE(ComponentUVE(entity).hit);
    EXPECT_NEAR(ComponentUVE(entity).remainingLifetime, 10.0F - kDeltaTime, kEpsilon);
}

TEST_F(Projectile3DStepUVETest, AccelerationIsIntegratedAndIsWhatTheAuthoredVelocityAccumulates) {
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    projectile.acceleration = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    const EntityUVE entity = MakeProjectileUVE({}, projectile);

    static_cast<void>(StepAndResolveUVE(entity));
    static_cast<void>(StepAndResolveUVE(entity));

    // Two steps of +1 m/s^2 at 60Hz: velocity grew by 2 * dt, because acceleration is integrated
    // into the authored velocity before the step's motion is measured (the contract the pre-sweep
    // engine-core tick documented).
    EXPECT_NEAR(ComponentUVE(entity).velocity.x, 1.0F + 2.0F * kDeltaTime, kEpsilon);
    EXPECT_NEAR(LocalPositionUVE(entity).x, (1.0F + kDeltaTime) * kDeltaTime + (1.0F + 2.0F * kDeltaTime) * kDeltaTime,
                kEpsilon);
}

TEST_F(Projectile3DStepUVETest, StopsAtTheContactWhenTheSphereMeetsAWall) {
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.1F;
    const EntityUVE projectileEntity = MakeProjectileUVE({}, projectile);
    const EntityUVE wall = MakeColliderUVE({1.0F, 0.0F, 0.0F});

    const Projectile3DStepResultUVE result = StepUVE(projectileEntity, kContactStep);

    EXPECT_TRUE(result.StoppedUVE());
    EXPECT_TRUE(result.hasHit);
    EXPECT_EQ(result.hitEntity, wall);
    // The wall's near face is at x = 0.5; the sphere's centre stops its own radius short of it.
    EXPECT_NEAR(result.hitPosition.x, 0.5F, kEpsilon);
    EXPECT_NEAR(result.hitPosition.y, 0.0F, kEpsilon);
    EXPECT_NEAR(result.hitNormal.x, -1.0F, kEpsilon);
    EXPECT_NEAR(result.impactSpeed, 6.0F, kEpsilon);
    EXPECT_NEAR(result.movedDistance, 0.4F, kEpsilon);

    const Math::Vector3UVE position = LocalPositionUVE(projectileEntity);
    EXPECT_NEAR(position.x, 0.4F, kEpsilon);
    const Projectile3DComponentUVE after = ComponentUVE(projectileEntity);
    EXPECT_FALSE(after.active);
    EXPECT_NEAR(after.velocity.x, 0.0F, kEpsilon);
    // The result is sticky: it is the last contact, not this frame's, and it is written down.
    EXPECT_TRUE(after.hit);
    EXPECT_EQ(after.hitEntity, wall);
    EXPECT_NEAR(after.hitPosition.x, 0.5F, kEpsilon);
    EXPECT_NEAR(after.hitNormal.x, -1.0F, kEpsilon);
    EXPECT_NEAR(after.impactSpeed, 6.0F, kEpsilon);
}

TEST_F(Projectile3DStepUVETest, TheRadiusIsWhatSweepsAndNotAnInfinitelyThinRay) {
    // The wall's box sits at (1.2, 0.8, 0): its near face is at x = 0.7 and its lowest face at
    // y = 0.3. A 0.1m sphere sweeping along +X at y = 0 misses it (its expanded box starts at
    // y = 0.2); a 0.6m sphere does not (its expanded box reaches y = -0.3).
    const EntityUVE wall = MakeColliderUVE({1.2F, 0.8F, 0.0F});

    Projectile3DComponentUVE thin;
    thin.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    thin.radius = 0.1F;
    const EntityUVE thinEntity = MakeProjectileUVE({}, thin);
    const Projectile3DStepResultUVE thinResult = StepUVE(thinEntity, kContactStep);
    EXPECT_FALSE(thinResult.hasHit);
    EXPECT_NEAR(LocalPositionUVE(thinEntity).x, 0.6F, kEpsilon);

    Projectile3DComponentUVE fat;
    fat.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    fat.radius = 0.6F;
    const EntityUVE fatEntity = MakeProjectileUVE({}, fat);
    const Projectile3DStepResultUVE fatResult = StepUVE(fatEntity, kContactStep);
    EXPECT_TRUE(fatResult.hasHit);
    EXPECT_EQ(fatResult.hitEntity, wall);
    // The sphere's centre stops where the expanded AABB begins - 0.7 - 0.6 = 0.1 - and the
    // reported surface point is that face, half a radius further along the sweep.
    EXPECT_NEAR(LocalPositionUVE(fatEntity).x, 0.1F, kEpsilon);
    EXPECT_NEAR(fatResult.hitPosition.x, 0.7F, kEpsilon);
}

TEST_F(Projectile3DStepUVETest, TheMaskDecidesWhichLayersCanBeHit) {
    const EntityUVE wall = MakeColliderUVE({1.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F}, 4U);

    Projectile3DComponentUVE masked;
    masked.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    masked.collisionMask = 1U;
    const EntityUVE maskedEntity = MakeProjectileUVE({}, masked);
    EXPECT_FALSE(StepUVE(maskedEntity, kContactStep).hasHit);

    Projectile3DComponentUVE open;
    open.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    open.collisionMask = 1U | 4U;
    const EntityUVE openEntity = MakeProjectileUVE({}, open);
    const Projectile3DStepResultUVE openResult = StepUVE(openEntity, kContactStep);
    EXPECT_TRUE(openResult.hasHit);
    EXPECT_EQ(openResult.hitEntity, wall);

    // A mask of zero matches no layer at all, so nothing is swept and nothing is hit.
    Projectile3DComponentUVE blind;
    blind.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    blind.collisionMask = 0U;
    const EntityUVE blindEntity = MakeProjectileUVE({}, blind);
    EXPECT_FALSE(StepUVE(blindEntity, kContactStep).hasHit);
}

TEST_F(Projectile3DStepUVETest, NeverHitsTheEntityItIsAttachedTo) {
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.5F;
    const EntityUVE entity = MakeProjectileUVE({}, projectile);
    // Give the projectile its own collider: it is in the world's collider cache too, and a sweep
    // that did not ignore its own entity would report the projectile as its own obstacle.
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, ColliderComponentUVE{{0.5F, 0.5F, 0.5F}});
    sceneGraph.UpdateUVE(entityManager);

    const Projectile3DStepResultUVE result = StepUVE(entity);

    EXPECT_FALSE(result.hasHit);
    EXPECT_TRUE(result.IsSteppedUVE());
}

TEST_F(Projectile3DStepUVETest, BounceReflectsTheMotionAndKeepsFlying) {
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.1F;
    projectile.hitPolicy = Projectile3DHitPolicyUVE::Bounce;
    projectile.restitution = 0.5F;
    projectile.friction = 0.0F;
    const EntityUVE entity = MakeProjectileUVE({}, projectile);
    const EntityUVE wall = MakeColliderUVE({1.0F, 0.0F, 0.0F});

    const Projectile3DStepResultUVE result = StepAndResolveUVE(entity, kContactStep);

    EXPECT_TRUE(result.BouncedUVE());
    EXPECT_FALSE(result.StoppedUVE());
    EXPECT_TRUE(result.hasHit);
    EXPECT_EQ(result.hitEntity, wall);
    EXPECT_EQ(result.bounceCount, 1U);
    const Projectile3DComponentUVE after = ComponentUVE(entity);
    EXPECT_TRUE(after.active);
    EXPECT_NEAR(after.velocity.x, -3.0F, kEpsilon);
    EXPECT_EQ(after.bounceCount, 1U);
    // It leaves the surface instead of resting on it, so the next step's sweep starts outside.
    EXPECT_LT(LocalPositionUVE(entity).x, 0.4F);

    const Projectile3DStepResultUVE second = StepAndResolveUVE(entity);
    EXPECT_TRUE(second.IsSteppedUVE());
    EXPECT_FALSE(second.hasHit);
    EXPECT_LT(LocalPositionUVE(entity).x, 0.4F);
}

TEST_F(Projectile3DStepUVETest, BounceFrictionDampsTheComponentAlongTheSurface) {
    Projectile3DComponentUVE projectile;
    // Down and forward at a floor box whose top face is at y = -0.5.
    projectile.velocity = Math::Vector3UVE{0.0F, -6.0F, 6.0F};
    projectile.radius = 0.1F;
    projectile.hitPolicy = Projectile3DHitPolicyUVE::Bounce;
    projectile.restitution = 0.5F;
    projectile.friction = 0.5F;
    const EntityUVE entity = MakeProjectileUVE({}, projectile);
    static_cast<void>(MakeColliderUVE({0.0F, -1.0F, 0.0F}, {4.0F, 0.5F, 4.0F}));

    const Projectile3DStepResultUVE result = StepUVE(entity, kContactStep);

    EXPECT_TRUE(result.BouncedUVE());
    // The speed into the floor comes back at 0.5, and the speed along it at 1 - 0.5.
    EXPECT_NEAR(ComponentUVE(entity).velocity.y, 3.0F, kEpsilon);
    EXPECT_NEAR(ComponentUVE(entity).velocity.z, 3.0F, kEpsilon);
    EXPECT_NEAR(result.impactSpeed, 6.0F, kEpsilon);
}

TEST_F(Projectile3DStepUVETest, ABounceThatLeavesNothingToFlyWithIsAStop) {
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    projectile.hitPolicy = Projectile3DHitPolicyUVE::Bounce;
    projectile.restitution = 0.0F;
    projectile.friction = 1.0F;
    const EntityUVE entity = MakeProjectileUVE({}, projectile);
    static_cast<void>(MakeColliderUVE({1.0F, 0.0F, 0.0F}));

    const Projectile3DStepResultUVE result = StepUVE(entity, kContactStep);

    EXPECT_TRUE(result.hasHit);
    EXPECT_TRUE(result.StoppedUVE());
    EXPECT_FALSE(result.BouncedUVE());
    EXPECT_EQ(result.bounceCount, 0U);
    const Projectile3DComponentUVE after = ComponentUVE(entity);
    EXPECT_FALSE(after.active);
    EXPECT_NEAR(after.velocity.x, 0.0F, kEpsilon);
}

TEST_F(Projectile3DStepUVETest, AnOverlapTheStepBeginsInsideIsNotAContact) {
    // The projectile starts inside the wall's expanded box - exactly what happens to a projectile
    // fired from inside a launcher's own volume. There is no normal and no surface, so it is
    // allowed to leave instead of dying on the spot.
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.5F;
    const EntityUVE entity = MakeProjectileUVE({}, projectile);
    static_cast<void>(MakeColliderUVE({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}));

    const Projectile3DStepResultUVE result = StepUVE(entity);

    EXPECT_FALSE(result.hasHit);
    EXPECT_TRUE(result.IsSteppedUVE());
    EXPECT_NEAR(result.movedDistance, 0.1F, kEpsilon);
    EXPECT_NEAR(LocalPositionUVE(entity).x, 0.1F, kEpsilon);
}

TEST_F(Projectile3DStepUVETest, LifetimeExpiryEndsTheMotionAndClearsActive) {
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    projectile.maxLifetime = 0.02F;
    projectile.remainingLifetime = 0.02F;
    const EntityUVE entity = MakeProjectileUVE({}, projectile);

    const Projectile3DStepResultUVE first = StepAndResolveUVE(entity);
    EXPECT_TRUE(first.IsSteppedUVE());
    EXPECT_FALSE(first.ExpiredUVE());
    EXPECT_NEAR(ComponentUVE(entity).remainingLifetime, 0.02F - kDeltaTime, kEpsilon);

    const Projectile3DStepResultUVE second = StepAndResolveUVE(entity);
    EXPECT_TRUE(second.ExpiredUVE());
    EXPECT_FALSE(second.hasHit);
    EXPECT_FLOAT_EQ(ComponentUVE(entity).remainingLifetime, 0.0F);
    EXPECT_FALSE(ComponentUVE(entity).active);
    const Math::Vector3UVE restingPosition = LocalPositionUVE(entity);

    const Projectile3DStepResultUVE third = StepUVE(entity);
    EXPECT_TRUE(third.IsDisabledUVE());
    EXPECT_EQ(LocalPositionUVE(entity), restingPosition);
}

TEST_F(Projectile3DStepUVETest, ExpiryAndABounceInTheSameStepAreBothReported) {
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.1F;
    projectile.maxLifetime = 0.01F;
    projectile.remainingLifetime = 0.01F;
    projectile.hitPolicy = Projectile3DHitPolicyUVE::Bounce;
    projectile.restitution = 1.0F;
    projectile.friction = 0.0F;
    const EntityUVE entity = MakeProjectileUVE({}, projectile);
    static_cast<void>(MakeColliderUVE({1.0F, 0.0F, 0.0F}));

    // The step is longer than the lifetime on purpose: the sweep and the bounce happen first, and
    // the countdown reaches zero in the same step.
    const Projectile3DStepResultUVE result = StepUVE(entity, kContactStep);

    // `code` names the most consequential thing that happened; the flags stay independent, so a
    // step that bounced and expired in the same step loses neither fact.
    EXPECT_EQ(result.code, Projectile3DStepCodeUVE::Expired);
    EXPECT_TRUE(result.BouncedUVE());
    EXPECT_TRUE(result.ExpiredUVE());
    EXPECT_TRUE(result.hasHit);
    EXPECT_EQ(result.bounceCount, 1U);
    const Projectile3DComponentUVE after = ComponentUVE(entity);
    EXPECT_FALSE(after.active);
    EXPECT_NEAR(after.velocity.x, -6.0F, kEpsilon);
    EXPECT_TRUE(after.hit);
}

TEST_F(Projectile3DStepUVETest, RefusesWhatItCannotDriveWithoutTouchingAnything) {
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    const EntityUVE entity = MakeProjectileUVE({}, projectile);

    const Projectile3DStepResultUVE zeroTime = StepUVE(entity, 0.0F);
    EXPECT_EQ(zeroTime.code, Projectile3DStepCodeUVE::InvalidDeltaTime);
    EXPECT_TRUE(zeroTime.IsRefusedUVE());
    const Projectile3DStepResultUVE nanTime = StepUVE(entity, std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(nanTime.code, Projectile3DStepCodeUVE::InvalidDeltaTime);
    EXPECT_EQ(LocalPositionUVE(entity), Math::Vector3UVE{});
    EXPECT_NEAR(ComponentUVE(entity).remainingLifetime, 10.0F, kEpsilon);

    const Projectile3DStepResultUVE unknown =
        StepUVE(EntityUVE{9999U, 0U});
    EXPECT_EQ(unknown.code, Projectile3DStepCodeUVE::UnknownEntity);

    // An entity that is not a projectile at all.
    const EntityUVE plain = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, plain, Scene::TransformComponentUVE{});
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_EQ(StepUVE(plain).code, Projectile3DStepCodeUVE::NotAProjectile);

    // A projectile with no transform at all: there is nowhere to sweep from and nowhere to write.
    const EntityUVE untransformed = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Projectile3DComponentUVE>(untransformed, projectile);
    EXPECT_EQ(StepUVE(untransformed).code, Projectile3DStepCodeUVE::MissingTransform);

    // A component past its own contract: a bounce coefficient outside 0..1 is refused, and the
    // entity is left exactly where it was.
    Projectile3DComponentUVE malformed = projectile;
    malformed.restitution = 2.0F;
    const EntityUVE malformedEntity = MakeProjectileUVE({}, malformed);
    const Projectile3DStepResultUVE refused = StepUVE(malformedEntity);
    EXPECT_EQ(refused.code, Projectile3DStepCodeUVE::InvalidComponent);
    EXPECT_TRUE(refused.IsRefusedUVE());
    EXPECT_EQ(LocalPositionUVE(malformedEntity), Math::Vector3UVE{});
}

TEST_F(Projectile3DStepUVETest, ADisabledProjectileIsNotIntegratedOrCountedDown) {
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    projectile.active = false;
    const EntityUVE entity = MakeProjectileUVE({}, projectile);

    const Projectile3DStepResultUVE result = StepUVE(entity);

    EXPECT_TRUE(result.IsDisabledUVE());
    EXPECT_FALSE(result.hasHit);
    EXPECT_EQ(LocalPositionUVE(entity), Math::Vector3UVE{});
    EXPECT_FLOAT_EQ(ComponentUVE(entity).remainingLifetime, 10.0F);
    EXPECT_NEAR(ComponentUVE(entity).velocity.x, 6.0F, kEpsilon);
}

TEST_F(Projectile3DStepUVETest, TheSweepRunsAlongTheObjectsOwnRotationNotItsLocalAxes) {
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.1F;
    const EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transform{};
    transform.localRotation = Math::QuaternionUVE{0.0F, 0.70710678F, 0.0F, 0.70710678F};  // +90 deg about Y
    sceneGraph.AttachTransformUVE(entityManager, entity, transform);
    entityManager.AddComponentUVE<Projectile3DComponentUVE>(entity, projectile);
    sceneGraph.UpdateUVE(entityManager);
    // Local +X is world -Z after that rotation, so the wall is the one at -Z.
    const EntityUVE wall = MakeColliderUVE({0.0F, 0.0F, -1.0F});

    const Projectile3DStepResultUVE result = StepUVE(entity, kContactStep);

    EXPECT_TRUE(result.hasHit);
    EXPECT_EQ(result.hitEntity, wall);
    EXPECT_NEAR(result.hitNormal.z, 1.0F, kEpsilon);
    // The committed motion is the object's own: its local +X moved, and its world motion was -Z.
    EXPECT_NEAR(LocalPositionUVE(entity).x, 0.4F, kEpsilon);
    EXPECT_NEAR(LocalPositionUVE(entity).z, 0.0F, kEpsilon);
}

TEST_F(Projectile3DStepUVETest, BouncesTheContactNormalBackIntoTheObjectsOwnAxes) {
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{6.0F, 0.0F, 0.0F};
    projectile.radius = 0.1F;
    projectile.hitPolicy = Projectile3DHitPolicyUVE::Bounce;
    projectile.restitution = 1.0F;
    projectile.friction = 0.0F;
    const EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transform{};
    transform.localRotation = Math::QuaternionUVE{0.0F, 0.70710678F, 0.0F, 0.70710678F};  // +90 deg about Y
    sceneGraph.AttachTransformUVE(entityManager, entity, transform);
    entityManager.AddComponentUVE<Projectile3DComponentUVE>(entity, projectile);
    sceneGraph.UpdateUVE(entityManager);
    static_cast<void>(MakeColliderUVE({0.0F, 0.0F, -1.0F}));

    const Projectile3DStepResultUVE result = StepUVE(entity, kContactStep);

    EXPECT_TRUE(result.BouncedUVE());
    // The surface's world normal faces +Z; in the projectile's own axes that is -X, and a
    // reflection of local +X motion about it is local -X motion.
    EXPECT_NEAR(ComponentUVE(entity).velocity.x, -6.0F, kEpsilon);
    EXPECT_NEAR(ComponentUVE(entity).velocity.z, 0.0F, kEpsilon);
}

} // namespace
} // namespace UVE::Physics::Tests
