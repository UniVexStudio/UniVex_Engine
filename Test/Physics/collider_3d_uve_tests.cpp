// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include "uve/component/collider_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/ray_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/collider_3d_uve.h"
#include "uve/physics/collision_system_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::Collider3DUVE;
using Scene::ColliderComponentUVE;
using Scene::ColliderShapeTypeUVE;

constexpr float kToleranceUVE = 1.0e-4F;

class Collider3DRuntimeUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    CollisionSystemUVE collisionSystem;

    Scene::EntityUVE MakeColliderUVE(const Math::Vector3UVE& position, ColliderComponentUVE collider) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform;
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<ColliderComponentUVE>(entity, collider);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }
};

TEST(Collider3DUVETest, MakeHelpers_SetShapeAndExtents) {
    const ColliderComponentUVE box = Collider3DUVE::MakeBoxUVE({1.0F, 2.0F, 3.0F});
    EXPECT_EQ(box.shapeType, ColliderShapeTypeUVE::Box);
    EXPECT_EQ(box.halfExtents, Math::Vector3UVE(1.0F, 2.0F, 3.0F));
    EXPECT_TRUE(Scene::IsColliderComponentValidUVE(box));

    const ColliderComponentUVE sphere = Collider3DUVE::MakeSphereUVE(0.75F);
    EXPECT_EQ(sphere.shapeType, ColliderShapeTypeUVE::Sphere);
    EXPECT_FLOAT_EQ(sphere.radius, 0.75F);
    EXPECT_TRUE(Scene::IsColliderComponentValidUVE(sphere));

    const ColliderComponentUVE capsule = Collider3DUVE::MakeCapsuleUVE(0.5F, 2.0F);
    EXPECT_EQ(capsule.shapeType, ColliderShapeTypeUVE::Capsule);
    EXPECT_FLOAT_EQ(capsule.radius, 0.5F);
    EXPECT_FLOAT_EQ(capsule.height, 2.0F);
    EXPECT_TRUE(Scene::IsColliderComponentValidUVE(capsule));
}

TEST(Collider3DUVETest, IsParticipatingUVE_FalseWhenDisabledOrInvalid) {
    ColliderComponentUVE collider = Collider3DUVE::MakeBoxUVE({0.5F, 0.5F, 0.5F});
    EXPECT_TRUE(Collider3DUVE::IsParticipatingUVE(collider));
    collider.disabled = true;
    EXPECT_FALSE(Collider3DUVE::IsParticipatingUVE(collider));
    collider = {};
    collider.collisionLayer = 0U;
    EXPECT_FALSE(Collider3DUVE::IsParticipatingUVE(collider));
}

TEST(Collider3DUVETest, ContainsPointUVE_BoxSphereCapsuleAndDisabled) {
    const ColliderComponentUVE box = Collider3DUVE::MakeBoxUVE({1.0F, 0.5F, 1.0F});
    EXPECT_TRUE(Collider3DUVE::ContainsPointUVE(box, {}, {}, {0.5F, 0.0F, 0.0F}));
    EXPECT_TRUE(Collider3DUVE::ContainsPointUVE(box, {}, {}, {1.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(Collider3DUVE::ContainsPointUVE(box, {}, {}, {1.1F, 0.0F, 0.0F}));

    const ColliderComponentUVE sphere = Collider3DUVE::MakeSphereUVE(1.0F);
    EXPECT_TRUE(Collider3DUVE::ContainsPointUVE(sphere, {}, {}, {0.5F, 0.0F, 0.0F}));
    EXPECT_FALSE(Collider3DUVE::ContainsPointUVE(sphere, {}, {}, {1.1F, 0.0F, 0.0F}));

    const ColliderComponentUVE capsule = Collider3DUVE::MakeCapsuleUVE(0.5F, 2.0F);
    EXPECT_TRUE(Collider3DUVE::ContainsPointUVE(capsule, {}, {}, {0.0F, 0.0F, 0.0F}));
    EXPECT_TRUE(Collider3DUVE::ContainsPointUVE(capsule, {}, {}, {0.0F, 0.9F, 0.0F}));
    EXPECT_FALSE(Collider3DUVE::ContainsPointUVE(capsule, {}, {}, {0.6F, 0.0F, 0.0F}));

    ColliderComponentUVE disabled = box;
    disabled.disabled = true;
    EXPECT_FALSE(Collider3DUVE::ContainsPointUVE(disabled, {}, {}, {}));
}

TEST(Collider3DUVETest, ContainsPointUVE_RotatedBoxUsesLocalAxes) {
    Math::QuaternionUVE rotation{};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE({0.0F, 0.0F, 1.0F}, std::numbers::pi_v<float> * 0.5F, rotation));
    const ColliderComponentUVE box = Collider3DUVE::MakeBoxUVE({2.0F, 0.5F, 0.5F});
    EXPECT_TRUE(Collider3DUVE::ContainsPointUVE(box, {}, rotation, {0.0F, 1.5F, 0.0F}));
    EXPECT_FALSE(Collider3DUVE::ContainsPointUVE(box, {}, rotation, {1.5F, 0.0F, 0.0F}));
}

TEST(Collider3DUVETest, ClosestPointUVE_InsideIsThePointOutsideIsOnTheSurface) {
    const ColliderComponentUVE box = Collider3DUVE::MakeBoxUVE({1.0F, 1.0F, 1.0F});
    const auto inside = Collider3DUVE::ClosestPointUVE(box, {}, {}, {0.25F, 0.0F, 0.0F});
    ASSERT_TRUE(inside.has_value());
    EXPECT_EQ(*inside, Math::Vector3UVE(0.25F, 0.0F, 0.0F));

    const auto outside = Collider3DUVE::ClosestPointUVE(box, {}, {}, {4.0F, 0.0F, 0.0F});
    ASSERT_TRUE(outside.has_value());
    EXPECT_NEAR(outside->x, 1.0F, kToleranceUVE);
    EXPECT_NEAR(outside->y, 0.0F, kToleranceUVE);

    const ColliderComponentUVE sphere = Collider3DUVE::MakeSphereUVE(1.0F);
    const auto onSphere = Collider3DUVE::ClosestPointUVE(sphere, {}, {}, {4.0F, 0.0F, 0.0F});
    ASSERT_TRUE(onSphere.has_value());
    EXPECT_NEAR(onSphere->x, 1.0F, kToleranceUVE);

    ColliderComponentUVE disabled = box;
    disabled.disabled = true;
    EXPECT_FALSE(Collider3DUVE::ClosestPointUVE(disabled, {}, {}, {4.0F, 0.0F, 0.0F}).has_value());
}

TEST(Collider3DUVETest, IntersectRayUVE_BoxAndSphereAndMissAndInside) {
    const ColliderComponentUVE box = Collider3DUVE::MakeBoxUVE({1.0F, 1.0F, 1.0F});
    const Math::RayUVE intoBox{{-4.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const auto boxHit = Collider3DUVE::IntersectRayUVE(box, {}, {}, intoBox, 10.0F);
    ASSERT_TRUE(boxHit.has_value());
    EXPECT_NEAR(boxHit->distance, 3.0F, kToleranceUVE);
    EXPECT_NEAR(boxHit->normal.x, -1.0F, kToleranceUVE);

    const auto miss = Collider3DUVE::IntersectRayUVE(box, {}, {}, intoBox, 1.0F);
    EXPECT_FALSE(miss.has_value());

    const auto inside = Collider3DUVE::IntersectRayUVE(box, {}, {}, {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}}, 10.0F);
    ASSERT_TRUE(inside.has_value());
    EXPECT_FLOAT_EQ(inside->distance, 0.0F);

    const ColliderComponentUVE sphere = Collider3DUVE::MakeSphereUVE(1.0F);
    const auto sphereHit = Collider3DUVE::IntersectRayUVE(sphere, {}, {}, intoBox, 10.0F);
    ASSERT_TRUE(sphereHit.has_value());
    EXPECT_NEAR(sphereHit->distance, 3.0F, kToleranceUVE);
    EXPECT_NEAR(sphereHit->normal.x, -1.0F, kToleranceUVE);
}

TEST(Collider3DUVETest, IntersectRayUVE_CapsuleHitsTheSide) {
    const ColliderComponentUVE capsule = Collider3DUVE::MakeCapsuleUVE(0.5F, 2.0F);
    const Math::RayUVE intoSide{{-4.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const auto hit = Collider3DUVE::IntersectRayUVE(capsule, {}, {}, intoSide, 10.0F);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 3.5F, kToleranceUVE);
    EXPECT_NEAR(hit->normal.x, -1.0F, kToleranceUVE);
}

TEST(Collider3DUVETest, GetWorldAabbUVE_UnrotatedBoxMatchesLocal) {
    const ColliderComponentUVE box = Collider3DUVE::MakeBoxUVE({1.0F, 2.0F, 3.0F});
    const Math::AabbUVE world = Collider3DUVE::GetWorldAabbUVE(box, {10.0F, 0.0F, 0.0F}, {});
    EXPECT_NEAR(world.min.x, 9.0F, kToleranceUVE);
    EXPECT_NEAR(world.max.x, 11.0F, kToleranceUVE);
    EXPECT_NEAR(world.min.y, -2.0F, kToleranceUVE);
    EXPECT_NEAR(world.max.y, 2.0F, kToleranceUVE);
}

TEST(Collider3DUVETest, GetVolumeUVE_BoxSphereCapsule) {
    EXPECT_NEAR(Collider3DUVE::GetVolumeUVE(Collider3DUVE::MakeBoxUVE({0.5F, 0.5F, 0.5F})), 1.0F,
                kToleranceUVE);
    EXPECT_NEAR(Collider3DUVE::GetVolumeUVE(Collider3DUVE::MakeSphereUVE(1.0F)),
                (4.0F / 3.0F) * std::numbers::pi_v<float>, kToleranceUVE);
    const float capsule = Collider3DUVE::GetVolumeUVE(Collider3DUVE::MakeCapsuleUVE(0.5F, 2.0F));
    const float expected = std::numbers::pi_v<float> * 0.25F * 1.0F +
                           (4.0F / 3.0F) * std::numbers::pi_v<float> * 0.125F;
    EXPECT_NEAR(capsule, expected, kToleranceUVE);
}

TEST_F(Collider3DRuntimeUVETest, CollisionSystem_DisabledColliderIsNotAPair) {
    ColliderComponentUVE box = Collider3DUVE::MakeBoxUVE({1.0F, 1.0F, 1.0F});
    MakeColliderUVE({0.0F, 0.0F, 0.0F}, box);
    box.disabled = true;
    MakeColliderUVE({0.5F, 0.0F, 0.0F}, box);
    EXPECT_TRUE(collisionSystem.DetectCollisionsUVE(entityManager).empty());
}

TEST_F(Collider3DRuntimeUVETest, CollisionSystem_EnabledOverlappingBoxesStillCollide) {
    const ColliderComponentUVE box = Collider3DUVE::MakeBoxUVE({1.0F, 1.0F, 1.0F});
    MakeColliderUVE({0.0F, 0.0F, 0.0F}, box);
    MakeColliderUVE({0.5F, 0.0F, 0.0F}, box);
    EXPECT_EQ(collisionSystem.DetectCollisionsUVE(entityManager).size(), 1U);
}

} // namespace
} // namespace UVE::Physics::Tests
