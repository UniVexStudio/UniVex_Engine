// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "shape_cast_system.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

#include <gtest/gtest.h>

#include "event_system.h"
#include "memory_manager.h"
#include "collider_component.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

constexpr float kEpsilon = 1e-3F;

class ShapeCastSystemTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;

    Scene::Entity MakeColliderEntity(Math::Vector3 position, Math::Vector3 halfExtents,
                                            std::uint32_t collisionLayer = 1U, float friction = 0.0F,
                                            float restitution = 0.0F, float density = 1.0F) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        Scene::ColliderComponent collider{halfExtents};
        collider.collisionLayer = collisionLayer;
        collider.friction = friction;
        collider.restitution = restitution;
        collider.density = density;
        entityManager.AddComponent<Scene::ColliderComponent>(entity, collider);
        return entity;
    }

    Scene::Entity MakeSphereColliderEntity(Math::Vector3 position, float radius,
                                                  std::uint32_t collisionLayer = 1U) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        Scene::ColliderComponent collider;
        collider.shapeType = Scene::ColliderShapeType::Sphere;
        collider.radius = radius;
        collider.collisionLayer = collisionLayer;
        entityManager.AddComponent<Scene::ColliderComponent>(entity, collider);
        return entity;
    }

    [[nodiscard]] static SphereCastQuery MakeXAxisQuery(float originX = -10.0F,
                                                               float radius = 0.5F,
                                                               float maxDistance = 100.0F) {
        SphereCastQuery query;
        query.ray = Math::Ray{Math::Vector3{originX, 0.0F, 0.0F}, Math::Vector3{1.0F, 0.0F, 0.0F}};
        query.radius = radius;
        query.maxDistance = maxDistance;
        return query;
    }
};

TEST_F(ShapeCastSystemTest, SphereCast_ExpandsAabbAndReportsMovingCenter) {
    const Scene::Entity entity = MakeColliderEntity(Math::Vector3{}, Math::Vector3{1.0F, 1.0F, 1.0F},
                                                           1U, 0.4F, 0.9F, 3.0F);
    SphereCastQuery query = MakeXAxisQuery(-5.0F, 1.0F);

    const std::optional<SphereCastHit> hit = ShapeCastSystem::SphereCast(entityManager, query);

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, entity);
    EXPECT_NEAR(hit->distance, 3.0F, kEpsilon);
    EXPECT_NEAR(hit->center.x, -2.0F, kEpsilon);
    EXPECT_NEAR(hit->normal.x, -1.0F, kEpsilon);
    EXPECT_NEAR(hit->material.friction, 0.4F, kEpsilon);
    EXPECT_NEAR(hit->material.restitution, 0.9F, kEpsilon);
    EXPECT_NEAR(hit->material.density, 3.0F, kEpsilon);
}

TEST_F(ShapeCastSystemTest, SphereCast_SphereTargetUsesExactSweepInsteadOfAabbFalsePositive) {
    const Scene::Entity entity = MakeSphereColliderEntity({0.0F, 1.4F, 0.0F}, 1.0F);
    SphereCastQuery query = MakeXAxisQuery(-5.0F, 0.5F);

    const std::optional<SphereCastHit> hit = ShapeCastSystem::SphereCast(entityManager, query);

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, entity);
    EXPECT_NEAR(hit->distance, 5.0F - std::sqrt(0.29F), kEpsilon);
    EXPECT_NEAR(hit->center.x, -std::sqrt(0.29F), kEpsilon);
    EXPECT_NEAR(hit->center.y, 0.0F, kEpsilon);
    EXPECT_LT(hit->normal.x, 0.0F);
}

TEST_F(ShapeCastSystemTest, SphereCast_CapsuleTargetUsesExactSweepInsteadOfAabbFalsePositive) {
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::TransformComponent local;
    local.localPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, entity, local);
    sceneGraph.Update(entityManager);
    Scene::ColliderComponent collider;
    collider.shapeType = Scene::ColliderShapeType::Capsule;
    collider.radius = 0.5F;
    collider.height = 4.0F;
    entityManager.AddComponent<Scene::ColliderComponent>(entity, collider);

    SphereCastQuery query = MakeXAxisQuery(-5.0F, 0.5F);
    query.ray.origin.y = 2.0F;

    const std::optional<SphereCastHit> hit = ShapeCastSystem::SphereCast(entityManager, query);

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, entity);
    EXPECT_NEAR(hit->distance, 5.0F - std::sqrt(0.75F), kEpsilon);
    EXPECT_NEAR(hit->center.x, -std::sqrt(0.75F), kEpsilon);
    EXPECT_NEAR(hit->center.y, 2.0F, kEpsilon);
    EXPECT_LT(hit->normal.x, 0.0F);
    EXPECT_GT(hit->normal.y, 0.0F);
}

TEST_F(ShapeCastSystemTest, SphereCast_MultipleCollidersReturnsClosestDeterministically) {
    MakeColliderEntity(Math::Vector3{5.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Scene::Entity closer =
        MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});

    const std::optional<SphereCastHit> hit =
        ShapeCastSystem::SphereCast(entityManager, MakeXAxisQuery(-10.0F, 0.5F));

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, closer);
}

TEST_F(ShapeCastSystemTest, SphereCast_LayerAndIgnoreFiltersAreApplied) {
    const Scene::Entity ignored =
        MakeColliderEntity(Math::Vector3{}, Math::Vector3{1.0F, 1.0F, 1.0F}, 1U);
    MakeColliderEntity(Math::Vector3{4.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}, 2U);

    SphereCastQuery layerQuery = MakeXAxisQuery(-10.0F, 0.5F);
    layerQuery.layerMask = 1U;
    layerQuery.ignoreEntity = ignored;
    EXPECT_FALSE(ShapeCastSystem::SphereCast(entityManager, layerQuery).has_value());

    layerQuery.layerMask = 2U;
    const std::optional<SphereCastHit> hit = ShapeCastSystem::SphereCast(entityManager, layerQuery);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NE(hit->entity, ignored);
}

TEST_F(ShapeCastSystemTest, SphereCast_AlsoIgnoreSkipsASecondEntity) {
    const Scene::Entity first =
        MakeColliderEntity(Math::Vector3{}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Scene::Entity second =
        MakeColliderEntity(Math::Vector3{4.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});

    SphereCastQuery query = MakeXAxisQuery(-10.0F, 0.5F);
    query.ignoreEntity = first;
    query.alsoIgnoreEntity = second;
    EXPECT_FALSE(ShapeCastSystem::SphereCast(entityManager, query).has_value());

    query.alsoIgnoreEntity = {};
    const std::optional<SphereCastHit> hit = ShapeCastSystem::SphereCast(entityManager, query);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, second);
}

TEST_F(ShapeCastSystemTest, BoxCast_ExpandsTargetByMoverHalfExtentsAndReportsCenter) {
    const Scene::Entity entity = MakeColliderEntity(Math::Vector3{}, Math::Vector3{1.0F, 1.0F, 1.0F});
    BoxCastQuery query;
    query.ray = Math::Ray{Math::Vector3{-5.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 0.0F, 0.0F}};
    query.halfExtents = Math::Vector3{0.5F, 0.25F, 0.75F};
    query.maxDistance = 100.0F;

    const std::optional<BoxCastHit> hit = ShapeCastSystem::BoxCast(entityManager, query);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, entity);
    EXPECT_NEAR(hit->distance, 3.5F, kEpsilon);
    EXPECT_NEAR(hit->center.x, -1.5F, kEpsilon);
    EXPECT_NEAR(hit->normal.x, -1.0F, kEpsilon);
}

TEST_F(ShapeCastSystemTest, BoxCast_ReturnsClosestHitDeterministically) {
    MakeColliderEntity(Math::Vector3{5.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Scene::Entity closer =
        MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    BoxCastQuery query;
    query.ray = Math::Ray{Math::Vector3{-10.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 0.0F, 0.0F}};
    query.halfExtents = Math::Vector3{0.5F, 0.5F, 0.5F};
    query.maxDistance = 100.0F;

    const std::optional<BoxCastHit> hit = ShapeCastSystem::BoxCast(entityManager, query);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, closer);
}

TEST_F(ShapeCastSystemTest, BoxCast_AppliesLayerIgnoreAndMaterialFacts) {
    const Scene::Entity ignored =
        MakeColliderEntity(Math::Vector3{}, Math::Vector3{1.0F, 1.0F, 1.0F}, 1U);
    const Scene::Entity visible = MakeColliderEntity(
        Math::Vector3{4.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}, 2U, 0.4F, 0.9F, 3.0F);
    BoxCastQuery query;
    query.ray = Math::Ray{Math::Vector3{-10.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 0.0F, 0.0F}};
    query.halfExtents = Math::Vector3{0.5F, 0.5F, 0.5F};
    query.maxDistance = 100.0F;
    query.layerMask = 2U;
    query.ignoreEntity = ignored;

    const std::optional<BoxCastHit> hit = ShapeCastSystem::BoxCast(entityManager, query);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, visible);
    EXPECT_NEAR(hit->material.friction, 0.4F, kEpsilon);
    EXPECT_NEAR(hit->material.restitution, 0.9F, kEpsilon);
    EXPECT_NEAR(hit->material.density, 3.0F, kEpsilon);
}

TEST_F(ShapeCastSystemTest, BoxCast_RejectsInvalidFiniteInputs) {
    MakeColliderEntity(Math::Vector3{}, Math::Vector3{1.0F, 1.0F, 1.0F});
    BoxCastQuery negativeExtents;
    negativeExtents.ray = Math::Ray{Math::Vector3{-5.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 0.0F, 0.0F}};
    negativeExtents.halfExtents = Math::Vector3{-1.0F, 0.0F, 0.0F};
    negativeExtents.maxDistance = 100.0F;
    EXPECT_FALSE(ShapeCastSystem::BoxCast(entityManager, negativeExtents).has_value());

    BoxCastQuery zeroDirection = negativeExtents;
    zeroDirection.halfExtents = Math::Vector3{0.5F, 0.5F, 0.5F};
    zeroDirection.ray.direction = Math::Vector3{};
    EXPECT_FALSE(ShapeCastSystem::BoxCast(entityManager, zeroDirection).has_value());

    BoxCastQuery nonFiniteDistance = zeroDirection;
    nonFiniteDistance.ray.direction = Math::Vector3{1.0F, 0.0F, 0.0F};
    nonFiniteDistance.maxDistance = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ShapeCastSystem::BoxCast(entityManager, nonFiniteDistance).has_value());
}

TEST_F(ShapeCastSystemTest, CapsuleCast_ExpandsTargetByLocalYCapsuleBounds) {
    const Scene::Entity entity = MakeColliderEntity(Math::Vector3{}, Math::Vector3{1.0F, 1.0F, 1.0F});
    CapsuleCastQuery query;
    query.ray = Math::Ray{Math::Vector3{-5.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 0.0F, 0.0F}};
    query.radius = 0.5F;
    query.height = 2.0F;
    query.maxDistance = 100.0F;

    const std::optional<CapsuleCastHit> hit = ShapeCastSystem::CapsuleCast(entityManager, query);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, entity);
    EXPECT_NEAR(hit->distance, 3.5F, kEpsilon);
    EXPECT_NEAR(hit->center.x, -1.5F, kEpsilon);
    EXPECT_NEAR(hit->normal.x, -1.0F, kEpsilon);
}

TEST_F(ShapeCastSystemTest, CapsuleCast_ReturnsClosestHitAndHonorsFilters) {
    const Scene::Entity ignored =
        MakeColliderEntity(Math::Vector3{}, Math::Vector3{1.0F, 1.0F, 1.0F}, 1U);
    const Scene::Entity visible =
        MakeColliderEntity(Math::Vector3{4.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}, 2U);
    CapsuleCastQuery query;
    query.ray = Math::Ray{Math::Vector3{-10.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 0.0F, 0.0F}};
    query.radius = 0.5F;
    query.height = 3.0F;
    query.maxDistance = 100.0F;
    query.layerMask = 2U;
    query.ignoreEntity = ignored;

    const std::optional<CapsuleCastHit> hit = ShapeCastSystem::CapsuleCast(entityManager, query);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, visible);
}

TEST_F(ShapeCastSystemTest, CapsuleCast_RejectsInvalidDimensionsAndFiniteInputs) {
    MakeColliderEntity(Math::Vector3{}, Math::Vector3{1.0F, 1.0F, 1.0F});
    CapsuleCastQuery tooShort;
    tooShort.ray = Math::Ray{Math::Vector3{-5.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 0.0F, 0.0F}};
    tooShort.radius = 1.0F;
    tooShort.height = 1.5F;
    tooShort.maxDistance = 100.0F;
    EXPECT_FALSE(ShapeCastSystem::CapsuleCast(entityManager, tooShort).has_value());

    CapsuleCastQuery nonFinite = tooShort;
    nonFinite.height = 2.0F;
    nonFinite.radius = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ShapeCastSystem::CapsuleCast(entityManager, nonFinite).has_value());
}

TEST_F(ShapeCastSystemTest, ShapeCasts_SkipNonFiniteExpandedAabbs) {
    MakeColliderEntity(Math::Vector3{1.0e38F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    SphereCastQuery sphere = MakeXAxisQuery(0.0F, std::numeric_limits<float>::max(),
                                                   std::numeric_limits<float>::max());
    EXPECT_FALSE(ShapeCastSystem::SphereCast(entityManager, sphere).has_value());

    BoxCastQuery box;
    box.ray = Math::Ray{Math::Vector3{}, Math::Vector3{1.0F, 0.0F, 0.0F}};
    box.halfExtents = Math::Vector3{std::numeric_limits<float>::max(), 0.0F, 0.0F};
    box.maxDistance = std::numeric_limits<float>::max();
    EXPECT_FALSE(ShapeCastSystem::BoxCast(entityManager, box).has_value());

    MakeColliderEntity(Math::Vector3{0.0F, 2.0e38F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    CapsuleCastQuery capsule;
    capsule.ray = Math::Ray{Math::Vector3{}, Math::Vector3{0.0F, 1.0F, 0.0F}};
    capsule.radius = 1.0F;
    capsule.height = std::numeric_limits<float>::max();
    capsule.maxDistance = std::numeric_limits<float>::max();
    EXPECT_FALSE(ShapeCastSystem::CapsuleCast(entityManager, capsule).has_value());
}

TEST_F(ShapeCastSystemTest, SphereCast_RejectsInvalidFiniteInputs) {
    MakeColliderEntity(Math::Vector3{}, Math::Vector3{1.0F, 1.0F, 1.0F});

    SphereCastQuery negativeRadius = MakeXAxisQuery();
    negativeRadius.radius = -1.0F;
    EXPECT_FALSE(ShapeCastSystem::SphereCast(entityManager, negativeRadius).has_value());

    SphereCastQuery nonFiniteDistance = MakeXAxisQuery();
    nonFiniteDistance.maxDistance = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ShapeCastSystem::SphereCast(entityManager, nonFiniteDistance).has_value());

    SphereCastQuery emptyMask = MakeXAxisQuery();
    emptyMask.layerMask = 0U;
    EXPECT_FALSE(ShapeCastSystem::SphereCast(entityManager, emptyMask).has_value());
}

} // namespace
} // namespace UVE::Physics::Tests
