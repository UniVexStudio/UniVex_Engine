// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "collider_component.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "event_system.h"
#include "memory_manager.h"
#include "collision_system.h"
#include "shape_narrow_phase.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

class ColliderShapeTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    CollisionSystem collisionSystem;

    Scene::Entity MakeColliderEntity(
        Math::Vector3 position, const Scene::ColliderComponent& collider,
        Math::Quaternion rotation = {}) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        transform.localRotation = rotation;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::ColliderComponent>(entity, collider);
        return entity;
    }
};

TEST(ColliderComponentTest, IsColliderComponentValid_AcceptsBoxSphereAndCapsule) {
    Scene::ColliderComponent box;
    EXPECT_TRUE(Scene::IsColliderComponentValid(box));

    Scene::ColliderComponent sphere;
    sphere.shapeType = Scene::ColliderShapeType::Sphere;
    sphere.radius = 1.0F;
    EXPECT_TRUE(Scene::IsColliderComponentValid(sphere));

    Scene::ColliderComponent capsule;
    capsule.shapeType = Scene::ColliderShapeType::Capsule;
    capsule.radius = 0.5F;
    capsule.height = 2.0F;
    EXPECT_TRUE(Scene::IsColliderComponentValid(capsule));
}

TEST(ColliderComponentTest, IsColliderComponentValid_RejectsInvalidShapeParameters) {
    Scene::ColliderComponent invalid;
    invalid.shapeType = static_cast<Scene::ColliderShapeType>(255U);
    EXPECT_FALSE(Scene::IsColliderComponentValid(invalid));

    invalid = {};
    invalid.shapeType = Scene::ColliderShapeType::Sphere;
    invalid.radius = 0.0F;
    EXPECT_FALSE(Scene::IsColliderComponentValid(invalid));

    invalid = {};
    invalid.shapeType = Scene::ColliderShapeType::Capsule;
    invalid.radius = 0.5F;
    invalid.height = 0.9F;
    EXPECT_FALSE(Scene::IsColliderComponentValid(invalid));
}

TEST(ShapeNarrowPhaseTest, MovingSphereSphereSweepReportsExactHitAndDeterministicNormal) {
    const Math::Ray ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const std::optional<Math::RayHit> hit =
        Detail::IntersectMovingSphereSphere(ray, 0.5F, Math::Sphere{{0.0F, 1.4F, 0.0F}, 1.0F}, 100.0F);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 5.0F - std::sqrt(0.29F), 1.0e-4F);
    EXPECT_LT(hit->normal.x, 0.0F);
    EXPECT_LT(hit->normal.y, 0.0F);
    EXPECT_NEAR(Math::LengthSquared(hit->normal), 1.0F, 1.0e-4F);

    const std::optional<Math::RayHit> initialOverlap =
        Detail::IntersectMovingSphereSphere(Math::Ray{{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}},
                                               0.5F, Math::Sphere{{0.0F, 0.0F, 0.0F}, 1.0F}, 1.0F);
    ASSERT_TRUE(initialOverlap.has_value());
    EXPECT_FLOAT_EQ(initialOverlap->distance, 0.0F);
    EXPECT_EQ(initialOverlap->normal, (Math::Vector3{}));

    EXPECT_FALSE(Detail::IntersectMovingSphereSphere(
                     Math::Ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}}, 0.5F,
                     Math::Sphere{{0.0F, 1.5F, 0.0F}, 1.0F}, 100.0F)
                     .has_value());
    EXPECT_FALSE(Detail::IntersectMovingSphereSphere(
                     ray, 0.5F, Math::Sphere{{0.0F, 1.4F, 0.0F}, 1.0F}, 1.0F)
                     .has_value());
}

TEST(ShapeNarrowPhaseTest, RayTargetHelpersUseExactGeometryAndFailureClosedBounds) {
    const Math::Ray ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const std::optional<Math::RayHit> sphereHit =
        Detail::IntersectRaySphere(ray, Math::Sphere{{0.0F, 0.9F, 0.0F}, 1.0F}, 100.0F);
    ASSERT_TRUE(sphereHit.has_value());
    EXPECT_NEAR(sphereHit->distance, 5.0F - std::sqrt(0.19F), 1.0e-4F);
    EXPECT_NEAR(sphereHit->normal.x, -std::sqrt(0.19F), 1.0e-4F);
    EXPECT_NEAR(sphereHit->normal.y, -0.9F, 1.0e-4F);

    const std::optional<Math::RayHit> capsuleHit = Detail::IntersectRayCapsule(
        Math::Ray{{-5.0F, 1.9F, 0.0F}, {1.0F, 0.0F, 0.0F}}, {0.0F, -1.5F, 0.0F},
        {0.0F, 1.5F, 0.0F}, 0.5F, 100.0F);
    ASSERT_TRUE(capsuleHit.has_value());
    EXPECT_NEAR(capsuleHit->distance, 4.7F, 1.0e-4F);
    EXPECT_NEAR(capsuleHit->normal.x, -0.6F, 1.0e-4F);
    EXPECT_NEAR(capsuleHit->normal.y, 0.8F, 1.0e-4F);

    const std::optional<Math::RayHit> boxHit = Detail::IntersectRayOrientedBox(
        Math::Ray{{-5.0F, 0.5F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        Math::OrientedBox{{}, {0.25F, 1.0F, 0.25F},
                             Math::Quaternion{0.0F, 0.0F, 0.3826834324F, 0.9238795325F}},
        100.0F);
    ASSERT_TRUE(boxHit.has_value());
    EXPECT_NEAR(boxHit->distance, 5.0F - (0.25F * std::sqrt(2.0F) + 0.5F), 1.0e-4F);
    EXPECT_NEAR(boxHit->normal.x, -std::sqrt(0.5F), 1.0e-4F);
    EXPECT_NEAR(boxHit->normal.y, -std::sqrt(0.5F), 1.0e-4F);

    const std::optional<Math::RayHit> initialSphere =
        Detail::IntersectRaySphere(ray, Math::Sphere{{-4.0F, 0.0F, 0.0F}, 2.0F}, 100.0F);
    ASSERT_TRUE(initialSphere.has_value());
    EXPECT_FLOAT_EQ(initialSphere->distance, 0.0F);
    EXPECT_EQ(initialSphere->normal, (Math::Vector3{}));
    EXPECT_FALSE(Detail::IntersectRaySphere(ray, Math::Sphere{{}, 1.0F}, -1.0F).has_value());
}

TEST(ShapeNarrowPhaseTest, RadiusSumOverflowFailsClosedBeforePenetrationPublication) {
    const float maximumRadius = std::numeric_limits<float>::max();
    EXPECT_FALSE(Detail::ComputeSphereSpherePenetration(
                     {}, maximumRadius, {}, maximumRadius)
                     .has_value());
    EXPECT_FALSE(Detail::ComputeCapsuleSpherePenetration(
                     {}, {}, maximumRadius, {}, maximumRadius)
                     .has_value());
    EXPECT_FALSE(Detail::ComputeCapsuleCapsulePenetration(
                     {}, {}, maximumRadius, {}, {}, maximumRadius)
                     .has_value());
}

TEST(ShapeNarrowPhaseTest, OrientedBoxOrientedBoxUsesFinitePrecisionForLargeCenterDelta) {
    const float maximumExtent = std::numeric_limits<float>::max();
    const float centerMagnitude = maximumExtent * 0.99F;
    const Math::Quaternion identityRotation{};
    const std::optional<Math::Penetration> penetration =
        Detail::ComputeOrientedBoxOrientedBoxPenetration(
            {-centerMagnitude, 0.0F, 0.0F}, {maximumExtent, 1.0F, 1.0F}, identityRotation,
            {centerMagnitude, 0.0F, 0.0F}, {maximumExtent, 1.0F, 1.0F}, identityRotation);
    ASSERT_TRUE(penetration.has_value());
    EXPECT_TRUE(std::isfinite(penetration->depth));
    EXPECT_FLOAT_EQ(penetration->depth, 2.0F);
    EXPECT_FLOAT_EQ(penetration->axis.x, 0.0F);
    EXPECT_FLOAT_EQ(penetration->axis.y, 1.0F);
    EXPECT_FLOAT_EQ(penetration->axis.z, 0.0F);
}

TEST(ShapeNarrowPhaseTest, SphereOrientedBoxUsesFinitePrecisionForLargeCenterDelta) {
    const float maximumValue = std::numeric_limits<float>::max();
    const float centerMagnitude = maximumValue * 0.99F;
    const Math::Quaternion identityRotation{};
    const std::optional<Math::Penetration> penetration =
        Detail::ComputeSphereOrientedBoxPenetration(
            {centerMagnitude, 0.0F, 0.0F}, {maximumValue, 1.0F, 1.0F}, identityRotation,
            {-centerMagnitude, 0.0F, 0.0F}, maximumValue);
    ASSERT_TRUE(penetration.has_value());
    EXPECT_TRUE(std::isfinite(penetration->depth));
    EXPECT_GT(penetration->depth, 0.0F);
    EXPECT_GT(penetration->axis.x, 0.0F);
    EXPECT_FLOAT_EQ(penetration->axis.y, 0.0F);
    EXPECT_FLOAT_EQ(penetration->axis.z, 0.0F);
}

TEST(ShapeNarrowPhaseTest, CapsuleOrientedBoxUsesFinitePrecisionForLargeCenterDelta) {
    const float maximumValue = std::numeric_limits<float>::max();
    const float centerMagnitude = maximumValue * 0.99F;
    const Math::Quaternion identityRotation{};
    const std::optional<Math::Penetration> penetration =
        Detail::ComputeCapsuleOrientedBoxPenetration(
            {centerMagnitude, 0.0F, 0.0F}, {maximumValue, 1.0F, 1.0F}, identityRotation,
            {-centerMagnitude, 0.0F, 0.0F}, {-centerMagnitude, 0.0F, 0.0F}, maximumValue);
    ASSERT_TRUE(penetration.has_value());
    EXPECT_TRUE(std::isfinite(penetration->depth));
    EXPECT_GT(penetration->depth, 0.0F);
    EXPECT_GT(penetration->axis.x, 0.0F);
    EXPECT_FLOAT_EQ(penetration->axis.y, 0.0F);
    EXPECT_FLOAT_EQ(penetration->axis.z, 0.0F);
}

TEST(ShapeNarrowPhaseTest, SphereSphereUsesFinitePrecisionForLargeRadiusOverlap) {
    const float radius = std::numeric_limits<float>::max() * 0.5F;
    const std::optional<Math::Penetration> penetration =
        Detail::ComputeSphereSpherePenetration({-radius, 0.0F, 0.0F}, radius,
                                                   {0.0F, 0.0F, 0.0F}, radius);
    ASSERT_TRUE(penetration.has_value());
    EXPECT_TRUE(std::isfinite(penetration->depth));
    EXPECT_FLOAT_EQ(penetration->depth, radius);
    EXPECT_FLOAT_EQ(penetration->axis.x, 1.0F);
    EXPECT_FLOAT_EQ(penetration->axis.y, 0.0F);
    EXPECT_FLOAT_EQ(penetration->axis.z, 0.0F);
}

TEST(ShapeNarrowPhaseTest, CapsuleSphereUsesFinitePrecisionForLargeRadiusOverlap) {
    const float radius = std::numeric_limits<float>::max() * 0.5F;
    const std::optional<Math::Penetration> penetration =
        Detail::ComputeCapsuleSpherePenetration({-radius, 0.0F, 0.0F}, {radius, 0.0F, 0.0F},
                                                    radius, {std::numeric_limits<float>::max(), 0.0F, 0.0F},
                                                    radius);
    ASSERT_TRUE(penetration.has_value());
    EXPECT_TRUE(std::isfinite(penetration->depth));
    EXPECT_FLOAT_EQ(penetration->depth, radius);
    EXPECT_FLOAT_EQ(penetration->axis.x, 1.0F);
    EXPECT_FLOAT_EQ(penetration->axis.y, 0.0F);
    EXPECT_FLOAT_EQ(penetration->axis.z, 0.0F);
}

TEST(ShapeNarrowPhaseTest, CapsuleCapsuleUsesFinitePrecisionForLargeRadiusOverlap) {
    const float radius = std::numeric_limits<float>::max() * 0.5F;
    const std::optional<Math::Penetration> penetration =
        Detail::ComputeCapsuleCapsulePenetration(
            {-radius, 0.0F, 0.0F}, {radius, 0.0F, 0.0F}, radius,
            {-radius, radius, 0.0F}, {radius, radius, 0.0F}, radius);
    ASSERT_TRUE(penetration.has_value());
    EXPECT_TRUE(std::isfinite(penetration->depth));
    EXPECT_FLOAT_EQ(penetration->depth, radius);
    EXPECT_FLOAT_EQ(penetration->axis.x, 0.0F);
    EXPECT_FLOAT_EQ(penetration->axis.y, 1.0F);
    EXPECT_FLOAT_EQ(penetration->axis.z, 0.0F);
}

TEST(ShapeNarrowPhaseTest, CapsuleAabbUsesFinitePrecisionForLargeRadiusOverlap) {
    const float maximumRadius = std::numeric_limits<float>::max();
    const float expectedDepth = 2.0e30F;
    const Math::Aabb box{{0.0F, -1.0F, -1.0F}, {expectedDepth, 1.0F, 1.0F}};
    const std::optional<Math::Penetration> penetration =
        Detail::ComputeCapsuleAabbPenetration(
            box, {maximumRadius, 0.0F, 0.0F}, {maximumRadius, 0.0F, 0.0F}, maximumRadius);
    ASSERT_TRUE(penetration.has_value());
    EXPECT_TRUE(std::isfinite(penetration->depth));
    EXPECT_FLOAT_EQ(penetration->depth, expectedDepth);
    EXPECT_FLOAT_EQ(penetration->axis.x, -1.0F);
    EXPECT_FLOAT_EQ(penetration->axis.y, 0.0F);
    EXPECT_FLOAT_EQ(penetration->axis.z, 0.0F);
}

TEST(ShapeNarrowPhaseTest, SphereAabbUsesFinitePrecisionForLargeRadiusOverlap) {
    const float maximumRadius = std::numeric_limits<float>::max();
    const Math::Aabb box{{0.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}};
    const std::optional<Math::Penetration> penetration =
        Detail::ComputeSphereAabbPenetration(box, Math::Sphere{{2.0e38F, 0.0F, 0.0F}, maximumRadius});
    ASSERT_TRUE(penetration.has_value());
    EXPECT_TRUE(std::isfinite(penetration->depth));
    EXPECT_GT(penetration->depth, 0.0F);
}

TEST(ColliderComponentTest, GetColliderLocalHalfExtents_UsesConservativeShapeBounds) {
    Scene::ColliderComponent sphere;
    sphere.shapeType = Scene::ColliderShapeType::Sphere;
    sphere.radius = 1.25F;
    const Math::Vector3 expectedSphereExtents{1.25F, 1.25F, 1.25F};
    EXPECT_EQ(Scene::GetColliderLocalHalfExtents(sphere), expectedSphereExtents);

    Scene::ColliderComponent capsule;
    capsule.shapeType = Scene::ColliderShapeType::Capsule;
    capsule.radius = 0.4F;
    capsule.height = 2.4F;
    const Math::Vector3 expectedCapsuleExtents{0.4F, 1.2F, 0.4F};
    EXPECT_EQ(Scene::GetColliderLocalHalfExtents(capsule), expectedCapsuleExtents);
}

TEST_F(ColliderShapeTest, DetectCollisions_SphereUsesExactSphereBoxAndCapsuleRemainsConservative) {
    Scene::ColliderComponent sphere;
    sphere.shapeType = Scene::ColliderShapeType::Sphere;
    sphere.radius = 1.0F;
    const Scene::Entity sphereEntity = MakeColliderEntity({0.0F, 0.0F, 0.0F}, sphere);
    const Scene::Entity sphereTarget = MakeColliderEntity(
        {1.4F, 0.0F, 0.0F}, Scene::ColliderComponent{Math::Vector3{0.5F, 0.5F, 0.5F}});

    const std::vector<CollisionPair> spherePairs = collisionSystem.DetectCollisions(entityManager);
    ASSERT_EQ(spherePairs.size(), 1U);
    EXPECT_TRUE((spherePairs[0].first == sphereEntity && spherePairs[0].second == sphereTarget) ||
                (spherePairs[0].first == sphereTarget && spherePairs[0].second == sphereEntity));

    const Scene::Entity capsuleEntity = entityManager.CreateEntity();
    Scene::TransformComponent capsuleTransform;
    capsuleTransform.localPosition = {0.0F, 0.0F, 4.0F};
    sceneGraph.AttachTransform(entityManager, capsuleEntity, capsuleTransform);
    sceneGraph.Update(entityManager);
    Scene::ColliderComponent capsule;
    capsule.shapeType = Scene::ColliderShapeType::Capsule;
    capsule.radius = 0.5F;
    capsule.height = 3.0F;
    entityManager.AddComponent<Scene::ColliderComponent>(capsuleEntity, capsule);
    const Scene::Entity capsuleTarget = MakeColliderEntity(
        {0.0F, 1.8F, 4.0F}, Scene::ColliderComponent{Math::Vector3{0.5F, 0.5F, 0.5F}});

    const std::vector<CollisionPair> allPairs = collisionSystem.DetectCollisions(entityManager);
    EXPECT_EQ(allPairs.size(), 2U);
    EXPECT_TRUE(std::any_of(allPairs.begin(), allPairs.end(), [&](const CollisionPair& pair) {
        return (pair.first == capsuleEntity && pair.second == capsuleTarget) ||
               (pair.first == capsuleTarget && pair.second == capsuleEntity);
    }));
}

TEST_F(ColliderShapeTest, DetectCollisions_SphereSphereUsesExactDistance) {
    Scene::ColliderComponent firstSphere;
    firstSphere.shapeType = Scene::ColliderShapeType::Sphere;
    firstSphere.radius = 1.0F;
    MakeColliderEntity({0.0F, 0.0F, 0.0F}, firstSphere);

    Scene::ColliderComponent secondSphere;
    secondSphere.shapeType = Scene::ColliderShapeType::Sphere;
    secondSphere.radius = 1.0F;
    MakeColliderEntity({1.5F, 0.0F, 0.0F}, secondSphere);

    const std::vector<CollisionPair> pairs = collisionSystem.DetectCollisions(entityManager);
    ASSERT_EQ(pairs.size(), 1U);
    EXPECT_NEAR(pairs[0].penetrationDepth, 0.5F, 1.0e-5F);
    EXPECT_NEAR(pairs[0].separationAxis.x, 1.0F, 1.0e-5F);
    EXPECT_NEAR(pairs[0].separationAxis.y, 0.0F, 1.0e-5F);
    EXPECT_NEAR(pairs[0].separationAxis.z, 0.0F, 1.0e-5F);
}

TEST_F(ColliderShapeTest, DetectCollisions_SphereSphereRejectsTouchingAndDiagonalAabbFalsePositive) {
    Scene::ColliderComponent sphere;
    sphere.shapeType = Scene::ColliderShapeType::Sphere;
    sphere.radius = 0.5F;
    MakeColliderEntity({0.0F, 0.0F, 0.0F}, sphere);

    Scene::ColliderComponent touchingSphere;
    touchingSphere.shapeType = Scene::ColliderShapeType::Sphere;
    touchingSphere.radius = 0.5F;
    MakeColliderEntity({10.0F, 0.0F, 0.0F}, touchingSphere);

    Scene::ColliderComponent diagonalSphere;
    diagonalSphere.shapeType = Scene::ColliderShapeType::Sphere;
    diagonalSphere.radius = 0.5F;
    MakeColliderEntity({0.9F, 0.9F, 0.0F}, diagonalSphere);

    // The diagonal pair's conservative AABBs overlap, but its center distance is greater than
    // the combined radius. The touching pair is isolated at x=10 and is also intentionally
    // excluded.
    EXPECT_TRUE(collisionSystem.DetectCollisions(entityManager).empty());
}

TEST_F(ColliderShapeTest, DetectCollisions_RotatedCapsuleUsesWorldSegmentForSphereContact) {
    Scene::ColliderComponent capsule;
    capsule.shapeType = Scene::ColliderShapeType::Capsule;
    capsule.radius = 0.5F;
    capsule.height = 4.0F;
    const Math::Quaternion quarterTurnZ{0.0F, 0.0F, 0.70710678118F, 0.70710678118F};
    MakeColliderEntity({0.0F, 0.0F, 0.0F}, capsule, quarterTurnZ);

    Scene::ColliderComponent sphere;
    sphere.shapeType = Scene::ColliderShapeType::Sphere;
    sphere.radius = 0.5F;
    const Scene::Entity sphereEntity = MakeColliderEntity({0.0F, 0.75F, 0.0F}, sphere);

    const std::vector<CollisionPair> pairs = collisionSystem.DetectCollisions(entityManager);
    ASSERT_EQ(pairs.size(), 1U);
    EXPECT_EQ(pairs[0].second, sphereEntity);
    EXPECT_NEAR(pairs[0].penetrationDepth, 0.25F, 1.0e-5F);
    EXPECT_NEAR(pairs[0].separationAxis.x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(pairs[0].separationAxis.y, 1.0F, 1.0e-5F);
    EXPECT_NEAR(pairs[0].separationAxis.z, 0.0F, 1.0e-5F);
}

TEST_F(ColliderShapeTest, DetectCollisions_RotatedBoxSphereUsesExactLocalFrame) {
    Scene::ColliderComponent box;
    box.shapeType = Scene::ColliderShapeType::Box;
    box.halfExtents = {0.25F, 1.0F, 0.25F};
    const Math::Quaternion quarterTurnZ{0.0F, 0.0F, 0.3826834324F, 0.9238795325F};
    MakeColliderEntity({0.0F, 0.0F, 0.0F}, box, quarterTurnZ);

    Scene::ColliderComponent sphere;
    sphere.shapeType = Scene::ColliderShapeType::Sphere;
    sphere.radius = 0.5F;
    const Scene::Entity sphereEntity = MakeColliderEntity({0.5F, 0.5F, 0.0F}, sphere);

    const std::vector<CollisionPair> pairs = collisionSystem.DetectCollisions(entityManager);
    ASSERT_EQ(pairs.size(), 1U);
    EXPECT_EQ(pairs[0].second, sphereEntity);
    EXPECT_GT(pairs[0].penetrationDepth, 0.0F);
    EXPECT_NEAR(pairs[0].separationAxis.x, 0.70710678F, 1.0e-4F);
    EXPECT_NEAR(pairs[0].separationAxis.y, 0.70710678F, 1.0e-4F);
    EXPECT_NEAR(pairs[0].separationAxis.z, 0.0F, 1.0e-5F);
}

TEST_F(ColliderShapeTest, DetectCollisions_RotatedBoxCapsuleRejectsDiagonalAabbFalsePositive) {
    Scene::ColliderComponent box;
    box.shapeType = Scene::ColliderShapeType::Box;
    box.halfExtents = {0.2F, 1.0F, 0.2F};
    const Math::Quaternion quarterTurnZ{0.0F, 0.0F, 0.3826834324F, 0.9238795325F};
    MakeColliderEntity({0.0F, 0.0F, 0.0F}, box, quarterTurnZ);

    Scene::ColliderComponent capsule;
    capsule.shapeType = Scene::ColliderShapeType::Capsule;
    capsule.radius = 0.2F;
    capsule.height = 2.0F;
    MakeColliderEntity({0.8F, 0.8F, 0.0F}, capsule);

    EXPECT_TRUE(collisionSystem.DetectCollisions(entityManager).empty());
}

TEST_F(ColliderShapeTest, DetectCollisions_OrientedBoxesUseExactSatAndOrientation) {
    Scene::ColliderComponent firstBox;
    firstBox.shapeType = Scene::ColliderShapeType::Box;
    firstBox.halfExtents = {1.0F, 0.4F, 0.4F};
    const Math::Quaternion quarterTurnY{0.0F, 0.3826834324F, 0.0F, 0.9238795325F};
    MakeColliderEntity({0.0F, 0.0F, 0.0F}, firstBox, quarterTurnY);

    Scene::ColliderComponent secondBox = firstBox;
    const Scene::Entity secondEntity = MakeColliderEntity({0.5F, 0.0F, -0.5F}, secondBox, quarterTurnY);

    const std::vector<CollisionPair> pairs = collisionSystem.DetectCollisions(entityManager);
    ASSERT_EQ(pairs.size(), 1U);
    EXPECT_EQ(pairs[0].second, secondEntity);
    EXPECT_GT(pairs[0].penetrationDepth, 0.0F);
    EXPECT_GT(Math::Dot(pairs[0].separationAxis, {1.0F, 0.0F, -1.0F}), 0.0F);
    EXPECT_NEAR(Math::LengthSquared(pairs[0].separationAxis), 1.0F, 1.0e-4F);
}

TEST_F(ColliderShapeTest, DetectCollisions_OrientedBoxesRejectDiagonalAabbFalsePositive) {
    Scene::ColliderComponent box;
    box.shapeType = Scene::ColliderShapeType::Box;
    box.halfExtents = {1.0F, 0.2F, 0.2F};
    const Math::Quaternion quarterTurnY{0.0F, 0.3826834324F, 0.0F, 0.9238795325F};
    MakeColliderEntity({0.0F, 0.0F, 0.0F}, box, quarterTurnY);
    MakeColliderEntity({0.49497475F, 0.0F, 0.49497475F}, box, quarterTurnY);

    EXPECT_TRUE(collisionSystem.DetectCollisions(entityManager).empty());
}

TEST(ShapeNarrowPhaseTest, AreaAabbOrientedBoxUsesExactSatForDiagonalSeparation) {
    const std::optional<Math::Penetration> penetration =
        Detail::ComputeOrientedBoxOrientedBoxPenetration(
            {}, {0.5F, 0.5F, 0.5F}, {}, {0.7F, 0.0F, 0.7F}, {1.0F, 0.2F, 0.2F},
            Math::Quaternion{0.0F, 0.3826834324F, 0.0F, 0.9238795325F});

    EXPECT_FALSE(penetration.has_value());
}

TEST_F(ColliderShapeTest, DetectCollisions_CapsuleCapsuleUsesExactSegmentDistanceAndOrientation) {
    Scene::ColliderComponent firstCapsule;
    firstCapsule.shapeType = Scene::ColliderShapeType::Capsule;
    firstCapsule.radius = 0.5F;
    firstCapsule.height = 4.0F;
    const Scene::Entity firstEntity = MakeColliderEntity({0.0F, 0.0F, 0.0F}, firstCapsule);

    Scene::ColliderComponent secondCapsule;
    secondCapsule.shapeType = Scene::ColliderShapeType::Capsule;
    secondCapsule.radius = 0.5F;
    secondCapsule.height = 4.0F;
    const Scene::Entity secondEntity = MakeColliderEntity({0.75F, 0.0F, 0.0F}, secondCapsule);

    const std::vector<CollisionPair> pairs = collisionSystem.DetectCollisions(entityManager);
    ASSERT_EQ(pairs.size(), 1U);
    EXPECT_EQ(pairs[0].first, firstEntity);
    EXPECT_EQ(pairs[0].second, secondEntity);
    EXPECT_NEAR(pairs[0].penetrationDepth, 0.25F, 1.0e-5F);
    EXPECT_NEAR(pairs[0].separationAxis.x, 1.0F, 1.0e-5F);
    EXPECT_NEAR(pairs[0].separationAxis.y, 0.0F, 1.0e-5F);
    EXPECT_NEAR(pairs[0].separationAxis.z, 0.0F, 1.0e-5F);
}

TEST_F(ColliderShapeTest, DetectCollisions_CapsuleCapsuleRejectsTouchingAndDiagonalAabbFalsePositive) {
    Scene::ColliderComponent firstCapsule;
    firstCapsule.shapeType = Scene::ColliderShapeType::Capsule;
    firstCapsule.radius = 0.5F;
    firstCapsule.height = 4.0F;
    MakeColliderEntity({0.0F, 0.0F, 0.0F}, firstCapsule);

    Scene::ColliderComponent touchingCapsule;
    touchingCapsule.shapeType = Scene::ColliderShapeType::Capsule;
    touchingCapsule.radius = 0.5F;
    touchingCapsule.height = 4.0F;
    MakeColliderEntity({10.0F, 0.0F, 0.0F}, touchingCapsule);

    Scene::ColliderComponent diagonalCapsule;
    diagonalCapsule.shapeType = Scene::ColliderShapeType::Capsule;
    diagonalCapsule.radius = 0.5F;
    diagonalCapsule.height = 4.0F;
    MakeColliderEntity({0.9F, 0.0F, 0.9F}, diagonalCapsule);

    // The touching pair is isolated at x=10. Touching centerlines at distance 1.0 and diagonal
    // centerlines at sqrt(0.9^2 + 0.9^2) must both be rejected even though their conservative
    // capsule AABBs overlap.
    EXPECT_TRUE(collisionSystem.DetectCollisions(entityManager).empty());
}

TEST_F(ColliderShapeTest, DetectCollisions_CapsuleSphereUsesExactCenterlineDistance) {
    Scene::ColliderComponent capsule;
    capsule.shapeType = Scene::ColliderShapeType::Capsule;
    capsule.radius = 0.5F;
    capsule.height = 4.0F;
    const Scene::Entity capsuleEntity = MakeColliderEntity({0.0F, 0.0F, 0.0F}, capsule);

    Scene::ColliderComponent sphere;
    sphere.shapeType = Scene::ColliderShapeType::Sphere;
    sphere.radius = 0.5F;
    const Scene::Entity sphereEntity = MakeColliderEntity({0.75F, 0.0F, 0.0F}, sphere);

    const std::vector<CollisionPair> pairs = collisionSystem.DetectCollisions(entityManager);
    ASSERT_EQ(pairs.size(), 1U);
    EXPECT_EQ(pairs[0].first, capsuleEntity);
    EXPECT_EQ(pairs[0].second, sphereEntity);
    EXPECT_NEAR(pairs[0].penetrationDepth, 0.25F, 1.0e-5F);
    EXPECT_NEAR(pairs[0].separationAxis.x, 1.0F, 1.0e-5F);
    EXPECT_NEAR(pairs[0].separationAxis.y, 0.0F, 1.0e-5F);
    EXPECT_NEAR(pairs[0].separationAxis.z, 0.0F, 1.0e-5F);
}

TEST_F(ColliderShapeTest, DetectCollisions_SphereCapsuleRejectsTouchingAndDiagonalAabbFalsePositive) {
    Scene::ColliderComponent touchingSphere;
    touchingSphere.shapeType = Scene::ColliderShapeType::Sphere;
    touchingSphere.radius = 0.5F;
    MakeColliderEntity({1.0F, 0.0F, 0.0F}, touchingSphere);

    Scene::ColliderComponent diagonalSphere;
    diagonalSphere.shapeType = Scene::ColliderShapeType::Sphere;
    diagonalSphere.radius = 0.5F;
    MakeColliderEntity({0.9F, 1.4F, 0.9F}, diagonalSphere);

    Scene::ColliderComponent capsule;
    capsule.shapeType = Scene::ColliderShapeType::Capsule;
    capsule.radius = 0.5F;
    capsule.height = 4.0F;
    MakeColliderEntity({0.0F, 0.0F, 0.0F}, capsule);

    // The touching sphere reaches the capsule AABB boundary but has no strict overlap. The
    // diagonal sphere overlaps the conservative AABB in X/Z while its centerline distance is
    // greater than the combined radius. The two spheres are also separated from one another.
    EXPECT_TRUE(collisionSystem.DetectCollisions(entityManager).empty());
}

TEST_F(ColliderShapeTest, DetectCollisions_CapsuleRejectsDiagonalExpandedAabbFalsePositive) {
    Scene::ColliderComponent capsule;
    capsule.shapeType = Scene::ColliderShapeType::Capsule;
    capsule.radius = 0.5F;
    capsule.height = 4.0F;
    MakeColliderEntity({0.0F, 0.0F, 0.0F}, capsule);

    Scene::ColliderComponent box;
    box.halfExtents = {0.5F, 0.5F, 0.5F};
    MakeColliderEntity({0.9F, 0.0F, 0.9F}, box);

    // The conservative AABBs overlap by 0.1 on X/Z, but the capsule centerline-to-box distance
    // is sqrt(0.4^2 + 0.4^2) > radius, so the exact narrow phase must reject the pair.
    EXPECT_TRUE(collisionSystem.DetectCollisions(entityManager).empty());
}

TEST_F(ColliderShapeTest, DetectCollisions_SphereRejectsDiagonalExpandedAabbFalsePositive) {
    Scene::ColliderComponent sphere;
    sphere.shapeType = Scene::ColliderShapeType::Sphere;
    sphere.radius = 1.0F;
    MakeColliderEntity({0.0F, 0.0F, 0.0F}, sphere);

    Scene::ColliderComponent box;
    box.halfExtents = {0.5F, 0.5F, 0.5F};
    MakeColliderEntity({1.4F, 1.4F, 0.0F}, box);

    // The conservative AABBs overlap by 0.1 on X/Y, but the sphere-to-box distance is
    // sqrt(0.9^2 + 0.9^2) > radius, so the exact narrow phase must reject the pair.
    EXPECT_TRUE(collisionSystem.DetectCollisions(entityManager).empty());
}

} // namespace
} // namespace UVE::Physics::Tests
