// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "uve/component/collider_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/ray_cast_3d_uve.h"
#include "uve/physics/ray_cast_3d_step_uve.h"
#include "uve/physics/raycast_system_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::EntityUVE;
using Scene::RayCast3DComponentUVE;

constexpr float kEpsilon = 1e-3F;

class RayCast3DStepUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    RaycastSystemUVE raycastSystem;

    [[nodiscard]] EntityUVE MakeRayUVE(const Math::Vector3UVE& position, const RayCast3DComponentUVE& component) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<RayCast3DComponentUVE>(entity, component);
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
        Scene::ColliderComponentUVE collider{halfExtents};
        collider.collisionLayer = layer;
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(entity, collider);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    [[nodiscard]] RayCast3DStepResultUVE StepUVE(const EntityUVE entity) {
        return StepRayCast3DUVE(entityManager, raycastSystem, entity);
    }

    [[nodiscard]] RayCast3DComponentUVE ComponentUVE(const EntityUVE entity) {
        return entityManager.GetComponentUVE<RayCast3DComponentUVE>(entity);
    }
};

TEST_F(RayCast3DStepUVETest, HitsTheClosestColliderAlongTheLocalAxis) {
    RayCast3DComponentUVE rayCast;
    rayCast.direction = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
    rayCast.length = 10.0F;
    const EntityUVE caster = MakeRayUVE({0.0F, 5.0F, 0.0F}, rayCast);
    const EntityUVE ground = MakeColliderUVE({0.0F, 0.0F, 0.0F}, {10.0F, 0.5F, 10.0F});

    const RayCast3DStepResultUVE result = StepUVE(caster);

    EXPECT_TRUE(result.HitUVE());
    EXPECT_TRUE(result.hasHit);
    EXPECT_EQ(result.hitEntity, ground);
    EXPECT_NEAR(result.hitPosition.y, 0.5F, kEpsilon);
    EXPECT_NEAR(result.hitNormal.y, 1.0F, kEpsilon);
    EXPECT_TRUE(ComponentUVE(caster).hit);
    EXPECT_EQ(ComponentUVE(caster).hitEntity, ground);
}

TEST_F(RayCast3DStepUVETest, MissesWhenTheLengthFallsShortAndClearsAStaleHit) {
    RayCast3DComponentUVE rayCast;
    rayCast.direction = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
    rayCast.length = 10.0F;
    const EntityUVE caster = MakeRayUVE({0.0F, 5.0F, 0.0F}, rayCast);
    static_cast<void>(MakeColliderUVE({0.0F, 0.0F, 0.0F}, {10.0F, 0.5F, 10.0F}));
    EXPECT_TRUE(StepUVE(caster).HitUVE());

    entityManager.GetComponentUVE<RayCast3DComponentUVE>(caster).length = 1.0F;
    const RayCast3DStepResultUVE miss = StepUVE(caster);
    EXPECT_TRUE(miss.MissedUVE());
    EXPECT_FALSE(miss.hasHit);
    EXPECT_FALSE(ComponentUVE(caster).hit);
    EXPECT_EQ(ComponentUVE(caster).hitEntity, Scene::kInvalidEntityUVE);
}

TEST_F(RayCast3DStepUVETest, NeverHitsTheEntityItIsAttachedTo) {
    RayCast3DComponentUVE rayCast;
    rayCast.direction = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
    rayCast.length = 10.0F;
    const EntityUVE caster = MakeRayUVE({0.0F, 5.0F, 0.0F}, rayCast);
    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(caster, Scene::ColliderComponentUVE{});
    sceneGraph.UpdateUVE(entityManager);
    const EntityUVE ground = MakeColliderUVE({0.0F, 0.0F, 0.0F}, {10.0F, 0.5F, 10.0F});

    const RayCast3DStepResultUVE result = StepUVE(caster);
    EXPECT_TRUE(result.HitUVE());
    EXPECT_EQ(result.hitEntity, ground);
    EXPECT_NE(result.hitEntity, caster);
}

TEST_F(RayCast3DStepUVETest, ExclusionsSkipNamedColliders) {
    RayCast3DComponentUVE rayCast;
    rayCast.direction = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
    rayCast.length = 10.0F;
    const EntityUVE nearer = MakeColliderUVE({0.0F, 4.0F, 0.0F}, {10.0F, 0.5F, 10.0F});
    const EntityUVE farther = MakeColliderUVE({0.0F, 3.0F, 0.0F}, {10.0F, 0.5F, 10.0F});
    const EntityUVE caster = MakeRayUVE({0.0F, 5.0F, 0.0F}, rayCast);

    EXPECT_EQ(StepUVE(caster).hitEntity, nearer);

    entityManager.GetComponentUVE<RayCast3DComponentUVE>(caster).exclusions[0] = nearer;
    const RayCast3DStepResultUVE skipped = StepUVE(caster);
    EXPECT_TRUE(skipped.HitUVE());
    EXPECT_EQ(skipped.hitEntity, farther);
    EXPECT_NEAR(skipped.hitPosition.y, 3.5F, kEpsilon);
}

TEST_F(RayCast3DStepUVETest, DisabledAndInvalidListsClearTheWholeResult) {
    RayCast3DComponentUVE rayCast;
    rayCast.direction = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
    rayCast.length = 10.0F;
    const EntityUVE caster = MakeRayUVE({0.0F, 5.0F, 0.0F}, rayCast);
    static_cast<void>(MakeColliderUVE({0.0F, 0.0F, 0.0F}, {10.0F, 0.5F, 10.0F}));
    EXPECT_TRUE(StepUVE(caster).HitUVE());

    entityManager.GetComponentUVE<RayCast3DComponentUVE>(caster).enabled = false;
    const RayCast3DStepResultUVE disabled = StepUVE(caster);
    EXPECT_TRUE(disabled.IsDisabledUVE());
    EXPECT_FALSE(ComponentUVE(caster).hit);
    EXPECT_EQ(ComponentUVE(caster).hitEntity, Scene::kInvalidEntityUVE);

    RayCast3DComponentUVE& live = entityManager.GetComponentUVE<RayCast3DComponentUVE>(caster);
    live.enabled = true;
    live.exclusions[1] = EntityUVE{9U, 1U};
    const RayCast3DStepResultUVE invalid = StepUVE(caster);
    EXPECT_EQ(invalid.code, RayCast3DStepCodeUVE::InvalidComponent);
    EXPECT_FALSE(ComponentUVE(caster).hit);
    EXPECT_EQ(ComponentUVE(caster).hitPosition, Math::Vector3UVE{});
}

TEST_F(RayCast3DStepUVETest, TheCastRunsAlongTheObjectsOwnRotation) {
    RayCast3DComponentUVE rayCast;
    rayCast.direction = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    rayCast.length = 10.0F;
    const EntityUVE caster = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transform{};
    transform.localPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    transform.localRotation = Math::QuaternionUVE{0.0F, 0.70710678F, 0.0F, 0.70710678F};
    sceneGraph.AttachTransformUVE(entityManager, caster, transform);
    entityManager.AddComponentUVE<RayCast3DComponentUVE>(caster, rayCast);
    sceneGraph.UpdateUVE(entityManager);
    const EntityUVE wall = MakeColliderUVE({0.0F, 0.0F, -1.0F});

    const RayCast3DStepResultUVE result = StepUVE(caster);
    EXPECT_TRUE(result.HitUVE());
    EXPECT_EQ(result.hitEntity, wall);
}

TEST_F(RayCast3DStepUVETest, RefusesUnknownAndNonRayEntitiesWithoutWriting) {
    const RayCast3DStepResultUVE unknown = StepUVE(EntityUVE{9999U, 0U});
    EXPECT_EQ(unknown.code, RayCast3DStepCodeUVE::UnknownEntity);
    EXPECT_TRUE(unknown.IsRefusedUVE());

    const EntityUVE plain = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, plain, Scene::TransformComponentUVE{});
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_EQ(StepUVE(plain).code, RayCast3DStepCodeUVE::NotARayCast);

    const EntityUVE untransformed = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<RayCast3DComponentUVE>(untransformed, RayCast3DComponentUVE{});
    EXPECT_EQ(StepUVE(untransformed).code, RayCast3DStepCodeUVE::MissingTransform);
    EXPECT_FALSE(ComponentUVE(untransformed).hit);
}

} // namespace
} // namespace UVE::Physics::Tests
