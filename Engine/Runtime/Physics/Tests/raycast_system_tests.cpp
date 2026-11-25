// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "raycast_system.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

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

class RaycastSystemTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    RaycastSystem raycastSystem;

    Scene::Entity MakeColliderEntity(Math::Vector3 position, Math::Vector3 halfExtents,
                                            std::uint32_t collisionLayer = 1, float friction = 0.0F,
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

    Scene::Entity MakeTypedColliderEntity(Math::Vector3 position,
                                                 const Scene::ColliderComponent& collider,
                                                 Math::Quaternion rotation = {}) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        local.localRotation = rotation;
        sceneGraph.AttachTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::ColliderComponent>(entity, collider);
        return entity;
    }

    [[nodiscard]] static RaycastQuery MakeXAxisQuery(float originX = -10.0F, float maxDistance = 100.0F) {
        RaycastQuery query;
        query.ray = Math::Ray{Math::Vector3{originX, 0.0F, 0.0F}, Math::Vector3{1.0F, 0.0F, 0.0F}};
        query.maxDistance = maxDistance;
        return query;
    }
};

TEST_F(RaycastSystemTest, Raycast_SingleCollider_ReturnsHit) {
    const Scene::Entity entity = MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const RaycastQuery query = MakeXAxisQuery(-5.0F);

    const std::optional<RaycastHit> hit = raycastSystem.Raycast(entityManager, query);

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, entity);
    EXPECT_NEAR(hit->distance, 4.0F, kEpsilon);
    EXPECT_NEAR(hit->point.x, -1.0F, kEpsilon);
    EXPECT_NEAR(hit->normal.x, -1.0F, kEpsilon);
}

TEST_F(RaycastSystemTest, Raycast_UsesExactSphereTargetGeometry) {
    Scene::ColliderComponent sphere;
    sphere.shapeType = Scene::ColliderShapeType::Sphere;
    sphere.radius = 1.0F;
    const Scene::Entity entity = MakeTypedColliderEntity({0.0F, 0.9F, 0.0F}, sphere);

    const std::optional<RaycastHit> hit = raycastSystem.Raycast(entityManager, MakeXAxisQuery(-5.0F));

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, entity);
    EXPECT_NEAR(hit->distance, 5.0F - std::sqrt(0.19F), kEpsilon);
    EXPECT_NEAR(hit->point.x, -std::sqrt(0.19F), kEpsilon);
    EXPECT_NEAR(hit->point.y, 0.0F, kEpsilon);
    EXPECT_NEAR(hit->normal.x, -std::sqrt(0.19F), kEpsilon);
    EXPECT_NEAR(hit->normal.y, -0.9F, kEpsilon);
}

TEST_F(RaycastSystemTest, Raycast_UsesExactCapsuleTargetGeometry) {
    Scene::ColliderComponent capsule;
    capsule.shapeType = Scene::ColliderShapeType::Capsule;
    capsule.radius = 0.5F;
    capsule.height = 4.0F;
    const Scene::Entity entity = MakeTypedColliderEntity({0.0F, 0.0F, 0.0F}, capsule);

    RaycastQuery query = MakeXAxisQuery(-5.0F);
    query.ray.origin.y = 1.9F;
    const std::optional<RaycastHit> hit = raycastSystem.Raycast(entityManager, query);

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, entity);
    EXPECT_NEAR(hit->distance, 4.7F, kEpsilon);
    EXPECT_NEAR(hit->normal.x, -0.6F, kEpsilon);
    EXPECT_NEAR(hit->normal.y, 0.8F, kEpsilon);
}

TEST_F(RaycastSystemTest, Raycast_UsesExactRotatedBoxTargetGeometry) {
    Scene::ColliderComponent box;
    box.shapeType = Scene::ColliderShapeType::Box;
    box.halfExtents = {0.25F, 1.0F, 0.25F};
    const float halfAngle = 0.3826834324F;
    const float quarterTurnZ = 0.9238795325F;
    const Scene::Entity entity = MakeTypedColliderEntity(
        {}, box, Math::Quaternion{0.0F, 0.0F, halfAngle, quarterTurnZ});

    RaycastQuery query = MakeXAxisQuery(-5.0F);
    query.ray.origin.y = 0.5F;
    const std::optional<RaycastHit> hit = raycastSystem.Raycast(entityManager, query);

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, entity);
    EXPECT_NEAR(hit->distance, 5.0F - (0.25F * std::sqrt(2.0F) + 0.5F), kEpsilon);
    EXPECT_NEAR(hit->normal.x, -std::sqrt(0.5F), kEpsilon);
    EXPECT_NEAR(hit->normal.y, -std::sqrt(0.5F), kEpsilon);
}

TEST_F(RaycastSystemTest, Raycast_NoColliders_ReturnsNullopt) {
    EXPECT_FALSE(raycastSystem.Raycast(entityManager, MakeXAxisQuery()).has_value());
}

TEST_F(RaycastSystemTest, Raycast_MultipleCollidersAlongRay_ReturnsClosest) {
    MakeColliderEntity(Math::Vector3{5.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Scene::Entity closer = MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});

    const std::optional<RaycastHit> hit = raycastSystem.Raycast(entityManager, MakeXAxisQuery());

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, closer);
}

TEST_F(RaycastSystemTest, Raycast_EntityMissingColliderComponent_IsExcluded) {
    const Scene::Entity entity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, entity, Scene::TransformComponent{});
    sceneGraph.Update(entityManager);

    EXPECT_FALSE(raycastSystem.Raycast(entityManager, MakeXAxisQuery()).has_value());
}

TEST_F(RaycastSystemTest, Raycast_LayerMaskExcludesNonMatchingCollider) {
    MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}, /*collisionLayer=*/2U);
    RaycastQuery query = MakeXAxisQuery();
    query.layerMask = 1U; // (2 & 1) == 0 — collider's layer isn't in the query's mask.

    EXPECT_FALSE(raycastSystem.Raycast(entityManager, query).has_value());
}

TEST_F(RaycastSystemTest, Raycast_IgnoreEntity_ExcludesSpecificCollider) {
    const Scene::Entity entity = MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    RaycastQuery query = MakeXAxisQuery();
    query.ignoreEntity = entity;

    EXPECT_FALSE(raycastSystem.Raycast(entityManager, query).has_value());
}

TEST_F(RaycastSystemTest, Raycast_ExcludedEntities_SkipEveryNamedCollider) {
    // Three colliders on the ray, nearest first: -8, -4, then 0. Every exclusion is a real entity
    // reference, so each call must land on the next collider the list did not name.
    const Scene::Entity nearest =
        MakeColliderEntity(Math::Vector3{-8.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Scene::Entity middle =
        MakeColliderEntity(Math::Vector3{-4.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Scene::Entity farthest =
        MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});

    const std::array<Scene::Entity, 1U> skipNearest{nearest};
    RaycastQuery query = MakeXAxisQuery(-10.0F);
    query.excludedEntities = skipNearest;
    std::optional<RaycastHit> hit = raycastSystem.Raycast(entityManager, query);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, middle);

    const std::array<Scene::Entity, 2U> skipFirstTwo{nearest, middle};
    query.excludedEntities = skipFirstTwo;
    hit = raycastSystem.Raycast(entityManager, query);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, farthest);

    const std::array<Scene::Entity, 3U> skipAll{nearest, middle, farthest};
    query.excludedEntities = skipAll;
    EXPECT_FALSE(raycastSystem.Raycast(entityManager, query).has_value());
}

TEST_F(RaycastSystemTest, Raycast_AnExclusionIsNotAMask_ItSkipsEvenWhereTheMaskWouldAccept) {
    // The collider is on layer 2 and the query's mask accepts layer 2, so the mask alone would let
    // it through - the exclusion is what has to stop it. Skipping is decided before the mask, so no
    // layer can bring an excluded object back.
    const Scene::Entity entity = MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                          Math::Vector3{1.0F, 1.0F, 1.0F}, 2U);
    RaycastQuery query = MakeXAxisQuery(-5.0F);
    query.layerMask = 0x2U;
    ASSERT_TRUE(raycastSystem.Raycast(entityManager, query).has_value());

    const std::array<Scene::Entity, 1U> skip{entity};
    query.excludedEntities = skip;
    EXPECT_FALSE(raycastSystem.Raycast(entityManager, query).has_value());
}

TEST_F(RaycastSystemTest, Raycast_IgnoreEntityAndExclusionsAreBothHonoured) {
    const Scene::Entity origin =
        MakeColliderEntity(Math::Vector3{-9.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Scene::Entity second =
        MakeColliderEntity(Math::Vector3{-6.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Scene::Entity third =
        MakeColliderEntity(Math::Vector3{-2.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});

    // The ray's own entity comes in through `ignoreEntity`; the authored exclusions through the
    // span. A query that carries both must skip both, and still see the one nobody named.
    const std::array<Scene::Entity, 1U> skipSecond{second};
    RaycastQuery query = MakeXAxisQuery(-10.0F);
    query.ignoreEntity = origin;
    query.excludedEntities = skipSecond;
    std::optional<RaycastHit> hit = raycastSystem.Raycast(entityManager, query);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, third);

    query.ignoreEntity = Scene::kInvalidEntity;
    hit = raycastSystem.Raycast(entityManager, query);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, origin);
}

TEST_F(RaycastSystemTest, Raycast_AnEmptyExclusionListIsTheUnchangedQuery) {
    const Scene::Entity entity =
        MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const RaycastQuery query = MakeXAxisQuery(-5.0F);

    ASSERT_EQ(query.excludedEntities.size(), 0U);
    const std::optional<RaycastHit> hit = raycastSystem.Raycast(entityManager, query);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, entity);
    EXPECT_NEAR(hit->distance, 4.0F, kEpsilon);
}

TEST_F(RaycastSystemTest, Raycast_AnEmptySpanSlotMatchesNoEntity) {
    // The engine hands over the authored prefix of a fixed-capacity array, so a caller that passes
    // the whole array with its tail still empty must not accidentally exclude anything: an empty
    // slot is kInvalidEntity, which no live entity ever compares equal to.
    const Scene::Entity entity =
        MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const std::array<Scene::Entity, 4U> slots{Scene::kInvalidEntity, Scene::kInvalidEntity,
                                                Scene::kInvalidEntity, Scene::kInvalidEntity};
    RaycastQuery query = MakeXAxisQuery(-5.0F);
    query.excludedEntities = std::span<const Scene::Entity>(slots);

    const std::optional<RaycastHit> hit = raycastSystem.Raycast(entityManager, query);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, entity);
}

TEST_F(RaycastSystemTest, Raycast_EqualDistanceHits_ResolveDeterministically) {
    const Scene::Entity first = MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}); // identical duplicate
    const RaycastQuery query = MakeXAxisQuery();

    const std::optional<RaycastHit> firstCall = raycastSystem.Raycast(entityManager, query);
    const std::optional<RaycastHit> secondCall = raycastSystem.Raycast(entityManager, query);

    ASSERT_TRUE(firstCall.has_value());
    ASSERT_TRUE(secondCall.has_value());
    EXPECT_EQ(firstCall->entity, first);
    EXPECT_EQ(secondCall->entity, first);
}

TEST_F(RaycastSystemTest, Raycast_HitMaterial_MatchesColliderFields) {
    const Scene::Entity entity = MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F},
                                                            1U, 0.4F, 0.9F, 3.0F);

    const std::optional<RaycastHit> hit = raycastSystem.Raycast(entityManager, MakeXAxisQuery());

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, entity);
    EXPECT_NEAR(hit->material.friction, 0.4F, kEpsilon);
    EXPECT_NEAR(hit->material.restitution, 0.9F, kEpsilon);
    EXPECT_NEAR(hit->material.density, 3.0F, kEpsilon);
}

TEST_F(RaycastSystemTest, Raycast_HitMaterial_ClampsOutOfRangeRestitution) {
    MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}, 1U, 0.0F, 5.0F, 1.0F);

    const std::optional<RaycastHit> hit = raycastSystem.Raycast(entityManager, MakeXAxisQuery());

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->material.restitution, 1.0F, kEpsilon);
}

} // namespace
} // namespace UVE::Physics::Tests
