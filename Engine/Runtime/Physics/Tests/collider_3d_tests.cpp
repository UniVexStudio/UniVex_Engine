// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include "collider_component.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "quaternion.h"
#include "ray.h"
#include "vector3.h"
#include "memory_manager.h"
#include "collider_3d.h"
#include "collision_system.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::Collider3D;
using Scene::ColliderComponent;
using Scene::ColliderShapeType;

constexpr float kTolerance = 1.0e-4F;

class Collider3DRuntimeTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    CollisionSystem collisionSystem;

    Scene::Entity MakeCollider(const Math::Vector3& position, ColliderComponent collider) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<ColliderComponent>(entity, collider);
        sceneGraph.Update(entityManager);
        return entity;
    }
};

TEST(Collider3DTest, MakeHelpers_SetShapeAndExtents) {
    const ColliderComponent box = Collider3D::MakeBox({1.0F, 2.0F, 3.0F});
    EXPECT_EQ(box.shapeType, ColliderShapeType::Box);
    EXPECT_EQ(box.halfExtents, Math::Vector3(1.0F, 2.0F, 3.0F));
    EXPECT_TRUE(Scene::IsColliderComponentValid(box));

    const ColliderComponent sphere = Collider3D::MakeSphere(0.75F);
    EXPECT_EQ(sphere.shapeType, ColliderShapeType::Sphere);
    EXPECT_FLOAT_EQ(sphere.radius, 0.75F);
    EXPECT_TRUE(Scene::IsColliderComponentValid(sphere));

    const ColliderComponent capsule = Collider3D::MakeCapsule(0.5F, 2.0F);
    EXPECT_EQ(capsule.shapeType, ColliderShapeType::Capsule);
    EXPECT_FLOAT_EQ(capsule.radius, 0.5F);
    EXPECT_FLOAT_EQ(capsule.height, 2.0F);
    EXPECT_TRUE(Scene::IsColliderComponentValid(capsule));
}

TEST(Collider3DTest, IsParticipating_FalseWhenDisabledOrInvalid) {
    ColliderComponent collider = Collider3D::MakeBox({0.5F, 0.5F, 0.5F});
    EXPECT_TRUE(Collider3D::IsParticipating(collider));
    collider.disabled = true;
    EXPECT_FALSE(Collider3D::IsParticipating(collider));
    collider = {};
    collider.collisionLayer = 0U;
    EXPECT_FALSE(Collider3D::IsParticipating(collider));
}

TEST(Collider3DTest, ContainsPoint_BoxSphereCapsuleAndDisabled) {
    const ColliderComponent box = Collider3D::MakeBox({1.0F, 0.5F, 1.0F});
    EXPECT_TRUE(Collider3D::ContainsPoint(box, {}, {}, {0.5F, 0.0F, 0.0F}));
    EXPECT_TRUE(Collider3D::ContainsPoint(box, {}, {}, {1.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(Collider3D::ContainsPoint(box, {}, {}, {1.1F, 0.0F, 0.0F}));

    const ColliderComponent sphere = Collider3D::MakeSphere(1.0F);
    EXPECT_TRUE(Collider3D::ContainsPoint(sphere, {}, {}, {0.5F, 0.0F, 0.0F}));
    EXPECT_FALSE(Collider3D::ContainsPoint(sphere, {}, {}, {1.1F, 0.0F, 0.0F}));

    const ColliderComponent capsule = Collider3D::MakeCapsule(0.5F, 2.0F);
    EXPECT_TRUE(Collider3D::ContainsPoint(capsule, {}, {}, {0.0F, 0.0F, 0.0F}));
    EXPECT_TRUE(Collider3D::ContainsPoint(capsule, {}, {}, {0.0F, 0.9F, 0.0F}));
    EXPECT_FALSE(Collider3D::ContainsPoint(capsule, {}, {}, {0.6F, 0.0F, 0.0F}));

    ColliderComponent disabled = box;
    disabled.disabled = true;
    EXPECT_FALSE(Collider3D::ContainsPoint(disabled, {}, {}, {}));
}

TEST(Collider3DTest, ContainsPoint_RotatedBoxUsesLocalAxes) {
    Math::Quaternion rotation{};
    ASSERT_TRUE(Math::TryMakeAxisAngle({0.0F, 0.0F, 1.0F}, std::numbers::pi_v<float> * 0.5F, rotation));
    const ColliderComponent box = Collider3D::MakeBox({2.0F, 0.5F, 0.5F});
    EXPECT_TRUE(Collider3D::ContainsPoint(box, {}, rotation, {0.0F, 1.5F, 0.0F}));
    EXPECT_FALSE(Collider3D::ContainsPoint(box, {}, rotation, {1.5F, 0.0F, 0.0F}));
}

TEST(Collider3DTest, ClosestPoint_InsideIsThePointOutsideIsOnTheSurface) {
    const ColliderComponent box = Collider3D::MakeBox({1.0F, 1.0F, 1.0F});
    const auto inside = Collider3D::ClosestPoint(box, {}, {}, {0.25F, 0.0F, 0.0F});
    ASSERT_TRUE(inside.has_value());
    EXPECT_EQ(*inside, Math::Vector3(0.25F, 0.0F, 0.0F));

    const auto outside = Collider3D::ClosestPoint(box, {}, {}, {4.0F, 0.0F, 0.0F});
    ASSERT_TRUE(outside.has_value());
    EXPECT_NEAR(outside->x, 1.0F, kTolerance);
    EXPECT_NEAR(outside->y, 0.0F, kTolerance);

    const ColliderComponent sphere = Collider3D::MakeSphere(1.0F);
    const auto onSphere = Collider3D::ClosestPoint(sphere, {}, {}, {4.0F, 0.0F, 0.0F});
    ASSERT_TRUE(onSphere.has_value());
    EXPECT_NEAR(onSphere->x, 1.0F, kTolerance);

    ColliderComponent disabled = box;
    disabled.disabled = true;
    EXPECT_FALSE(Collider3D::ClosestPoint(disabled, {}, {}, {4.0F, 0.0F, 0.0F}).has_value());
}

TEST(Collider3DTest, IntersectRay_BoxAndSphereAndMissAndInside) {
    const ColliderComponent box = Collider3D::MakeBox({1.0F, 1.0F, 1.0F});
    const Math::Ray intoBox{{-4.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const auto boxHit = Collider3D::IntersectRay(box, {}, {}, intoBox, 10.0F);
    ASSERT_TRUE(boxHit.has_value());
    EXPECT_NEAR(boxHit->distance, 3.0F, kTolerance);
    EXPECT_NEAR(boxHit->normal.x, -1.0F, kTolerance);

    const auto miss = Collider3D::IntersectRay(box, {}, {}, intoBox, 1.0F);
    EXPECT_FALSE(miss.has_value());

    const auto inside = Collider3D::IntersectRay(box, {}, {}, {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}}, 10.0F);
    ASSERT_TRUE(inside.has_value());
    EXPECT_FLOAT_EQ(inside->distance, 0.0F);

    const ColliderComponent sphere = Collider3D::MakeSphere(1.0F);
    const auto sphereHit = Collider3D::IntersectRay(sphere, {}, {}, intoBox, 10.0F);
    ASSERT_TRUE(sphereHit.has_value());
    EXPECT_NEAR(sphereHit->distance, 3.0F, kTolerance);
    EXPECT_NEAR(sphereHit->normal.x, -1.0F, kTolerance);
}

TEST(Collider3DTest, IntersectRay_CapsuleHitsTheSide) {
    const ColliderComponent capsule = Collider3D::MakeCapsule(0.5F, 2.0F);
    const Math::Ray intoSide{{-4.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const auto hit = Collider3D::IntersectRay(capsule, {}, {}, intoSide, 10.0F);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 3.5F, kTolerance);
    EXPECT_NEAR(hit->normal.x, -1.0F, kTolerance);
}

TEST(Collider3DTest, GetWorldAabb_UnrotatedBoxMatchesLocal) {
    const ColliderComponent box = Collider3D::MakeBox({1.0F, 2.0F, 3.0F});
    const Math::Aabb world =
        Collider3D::GetWorldAabb(box, {10.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F});
    EXPECT_NEAR(world.min.x, 9.0F, kTolerance);
    EXPECT_NEAR(world.max.x, 11.0F, kTolerance);
    EXPECT_NEAR(world.min.y, -2.0F, kTolerance);
    EXPECT_NEAR(world.max.y, 2.0F, kTolerance);
}

TEST(Collider3DTest, GetWorldAabb_FollowsTheScaleTheMeshIsDrawnAt) {
    // The bug this pins: a cube scaled out to a 500 x 1 x 500 floor kept its authored 1 x 1 x 1
    // collider, so anything standing on it at y = 1 fell through a surface that looked solid.
    const ColliderComponent box = Collider3D::MakeBox({0.5F, 0.5F, 0.5F});
    const Math::Aabb world = Collider3D::GetWorldAabb(
        box, {}, {}, {500.0F, 1.0F, 500.0F});
    EXPECT_NEAR(world.min.x, -250.0F, kTolerance);
    EXPECT_NEAR(world.max.x, 250.0F, kTolerance);
    EXPECT_NEAR(world.min.y, -0.5F, kTolerance) << "an axis left alone must stay untouched";
    EXPECT_NEAR(world.max.z, 250.0F, kTolerance);
}

TEST(Collider3DTest, ScaleCollider_ScalesEachShapeTheWayItCanBeScaled) {
    const ColliderComponent box = Collider3D::MakeBox({1.0F, 2.0F, 3.0F});
    const ColliderComponent scaledBox = ScaleCollider(box, {2.0F, 3.0F, 4.0F});
    EXPECT_NEAR(scaledBox.halfExtents.x, 2.0F, kTolerance);
    EXPECT_NEAR(scaledBox.halfExtents.y, 6.0F, kTolerance);
    EXPECT_NEAR(scaledBox.halfExtents.z, 12.0F, kTolerance);

    // A sphere has one radius for three axes, so it takes the widest - never an ellipse the rest
    // of the engine has no shape for.
    const ColliderComponent sphere = ScaleCollider(Collider3D::MakeSphere(1.0F), {3.0F, 1.0F, 2.0F});
    EXPECT_NEAR(sphere.radius, 3.0F, kTolerance);
    EXPECT_NEAR(sphere.halfExtents.y, 3.0F, kTolerance);

    // Capsule: the barrel follows its own axis, the cross-section follows the widest lateral one.
    const ColliderComponent capsule =
        ScaleCollider(Collider3D::MakeCapsule(0.5F, 2.0F), {2.0F, 4.0F, 1.0F});
    EXPECT_NEAR(capsule.radius, 1.0F, kTolerance);
    EXPECT_NEAR(capsule.height, 8.0F, kTolerance);

    // Squashed below its own caps, the capsule becomes the shortest one that still exists instead
    // of a shape that fails validation and drops the body out of the world.
    const ColliderComponent squashed =
        ScaleCollider(Collider3D::MakeCapsule(0.5F, 2.0F), {1.0F, 0.1F, 1.0F});
    EXPECT_NEAR(squashed.height, 1.0F, kTolerance) << "floored at 2 * radius";
}

TEST(Collider3DTest, ScaleCollider_LeavesAnUnscaledShapeBitIdentical) {
    const ColliderComponent capsule = Collider3D::MakeCapsule(0.4F, 1.8F);
    const ColliderComponent same = ScaleCollider(capsule, {1.0F, 1.0F, 1.0F});
    EXPECT_FLOAT_EQ(same.radius, capsule.radius);
    EXPECT_FLOAT_EQ(same.height, capsule.height);
    EXPECT_FLOAT_EQ(same.halfExtents.y, capsule.halfExtents.y);
}

TEST(Collider3DTest, GetVolume_BoxSphereCapsule) {
    EXPECT_NEAR(Collider3D::GetVolume(Collider3D::MakeBox({0.5F, 0.5F, 0.5F})), 1.0F,
                kTolerance);
    EXPECT_NEAR(Collider3D::GetVolume(Collider3D::MakeSphere(1.0F)),
                (4.0F / 3.0F) * std::numbers::pi_v<float>, kTolerance);
    const float capsule = Collider3D::GetVolume(Collider3D::MakeCapsule(0.5F, 2.0F));
    const float expected = std::numbers::pi_v<float> * 0.25F * 1.0F +
                           (4.0F / 3.0F) * std::numbers::pi_v<float> * 0.125F;
    EXPECT_NEAR(capsule, expected, kTolerance);
}

TEST_F(Collider3DRuntimeTest, CollisionSystem_DisabledColliderIsNotAPair) {
    ColliderComponent box = Collider3D::MakeBox({1.0F, 1.0F, 1.0F});
    MakeCollider({0.0F, 0.0F, 0.0F}, box);
    box.disabled = true;
    MakeCollider({0.5F, 0.0F, 0.0F}, box);
    EXPECT_TRUE(collisionSystem.DetectCollisions(entityManager).empty());
}

TEST_F(Collider3DRuntimeTest, CollisionSystem_EnabledOverlappingBoxesStillCollide) {
    const ColliderComponent box = Collider3D::MakeBox({1.0F, 1.0F, 1.0F});
    MakeCollider({0.0F, 0.0F, 0.0F}, box);
    MakeCollider({0.5F, 0.0F, 0.0F}, box);
    EXPECT_EQ(collisionSystem.DetectCollisions(entityManager).size(), 1U);
}

} // namespace
} // namespace UVE::Physics::Tests
