// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "area_component.h"
#include "collider_component.h"
#include "rigid_3d_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "vector3.h"
#include "memory_manager.h"
#include "area_3d.h"
#include "area_3d_runtime.h"
#include "area_overlap_system.h"
#include "collision_system.h"
#include "physics_system.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::Area3DField;
using Scene::Area3DOverlap;
using Scene::Area3DSpaceResult;
using Scene::Area3D;
using Scene::AreaComponent;
using Scene::AreaSpaceOverrideMode;
using Scene::Entity;
using Scene::kInvalidEntity;
using Scene::kMaximumArea3DOverlaps;

constexpr float kTolerance = 1.0e-4F;

[[nodiscard]] Entity MakeEntity(const std::uint32_t index,
                                      const std::uint32_t generation = 1U) noexcept {
    Entity entity;
    entity.index = index;
    entity.generation = generation;
    return entity;
}

[[nodiscard]] Area3DField MakeDirectionalField(const Entity areaEntity,
                                                     const std::int32_t priority,
                                                     const AreaSpaceOverrideMode gravityMode,
                                                     const Math::Vector3& gravity,
                                                     const AreaSpaceOverrideMode linearMode =
                                                         AreaSpaceOverrideMode::Disabled,
                                                     const float linearDamp = 0.0F,
                                                     const AreaSpaceOverrideMode angularMode =
                                                         AreaSpaceOverrideMode::Disabled,
                                                     const float angularDamp = 0.0F) {
    Area3DField field;
    field.areaEntity = areaEntity;
    field.priority = priority;
    field.gravityOverride = gravityMode;
    field.gravityDirection = gravity;
    field.gravityMagnitude = 1.0F;
    field.linearDampOverride = linearMode;
    field.linearDamp = linearDamp;
    field.angularDampOverride = angularMode;
    field.angularDamp = angularDamp;
    const float lengthSquared = Math::LengthSquared(gravity);
    if (lengthSquared > 0.0F) {
        field.gravityDirection = gravity;
        field.gravityMagnitude = std::sqrt(lengthSquared);
        field.gravityDirection = Math::Normalize(gravity);
    } else {
        field.gravityDirection = {};
        field.gravityMagnitude = 0.0F;
    }
    return field;
}

class Area3DRuntimeTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    CollisionSystem collisionSystem;

    Scene::Entity MakeArea(const Math::Vector3& position, const Math::Vector3& halfExtents,
                                 AreaComponent area = {}) {
        area.halfExtents = halfExtents;
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<AreaComponent>(entity, area);
        sceneGraph.Update(entityManager);
        return entity;
    }

    Scene::Entity MakeBody(const Math::Vector3& position,
                                 Scene::Rigid3DComponent rigidBody = {},
                                 const Math::Vector3& halfExtents = {0.5F, 0.5F, 0.5F}) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<Scene::Rigid3DComponent>(entity, rigidBody);
        entityManager.AddComponent<Scene::ColliderComponent>(
            entity, Scene::ColliderComponent{halfExtents});
        sceneGraph.Update(entityManager);
        return entity;
    }

    Scene::Entity MakeCollider(const Math::Vector3& position,
                                     const Math::Vector3& halfExtents = {0.5F, 0.5F, 0.5F}) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<Scene::ColliderComponent>(
            entity, Scene::ColliderComponent{halfExtents});
        sceneGraph.Update(entityManager);
        return entity;
    }
};

TEST(AreaComponentTest, IsAreaComponentValid_RejectsUnsafeSpaceOverride) {
    EXPECT_TRUE(Scene::IsAreaComponentValid(AreaComponent{}));
    EXPECT_TRUE(Scene::HasAreaSpaceOverride(AreaComponent{}) == false);

    AreaComponent invalid = {};
    invalid.linearDamp = -0.1F;
    EXPECT_FALSE(Scene::IsAreaComponentValid(invalid));
    invalid = {};
    invalid.angularDamp = -0.1F;
    EXPECT_FALSE(Scene::IsAreaComponentValid(invalid));
    invalid = {};
    invalid.gravityPointUnitDistance = -1.0F;
    EXPECT_FALSE(Scene::IsAreaComponentValid(invalid));
    invalid = {};
    invalid.gravityMagnitude = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(Scene::IsAreaComponentValid(invalid));
    invalid = {};
    invalid.gravityOverride = static_cast<AreaSpaceOverrideMode>(9U);
    EXPECT_FALSE(Scene::IsAreaComponentValid(invalid));
    invalid = {};
    invalid.gravityDirection.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Scene::IsAreaComponentValid(invalid));
}

TEST(Area3DTest, RefreshOccupancy_SplitsBodiesAndAreasCapsAndClearsWhenNotMonitoring) {
    AreaComponent area{};
    const Entity bodyA = MakeEntity(1U);
    const Entity bodyB = MakeEntity(2U);
    const Entity otherArea = MakeEntity(3U);
    const std::vector<Area3DOverlap> overlaps{
        Area3DOverlap{bodyA, 0.2F, false},
        Area3DOverlap{otherArea, 0.1F, true},
        Area3DOverlap{bodyB, 0.3F, false},
        Area3DOverlap{bodyA, 0.4F, false},
    };
    Area3D::RefreshOccupancy(area, overlaps);
    ASSERT_EQ(area.overlappingBodyCount, 2U);
    EXPECT_EQ(*Area3D::GetOverlappingBody(area, 0U), bodyA);
    EXPECT_EQ(*Area3D::GetOverlappingBody(area, 1U), bodyB);
    EXPECT_EQ(Area3D::GetOverlappingBody(area, 2U), nullptr);
    ASSERT_EQ(area.overlappingAreaCount, 1U);
    EXPECT_EQ(*Area3D::GetOverlappingArea(area, 0U), otherArea);
    EXPECT_TRUE(Area3D::HasOverlappingBody(area, bodyA));
    EXPECT_FALSE(Area3D::HasOverlappingArea(area, bodyA));
    EXPECT_FALSE(area.overlappingBodiesTruncated);

    area.monitoring = false;
    Area3D::RefreshOccupancy(area, overlaps);
    EXPECT_EQ(area.overlappingBodyCount, 0U);
    EXPECT_EQ(area.overlappingAreaCount, 0U);
    EXPECT_FALSE(Area3D::HasOverlappingBody(area, bodyA));
}

TEST(Area3DTest, RefreshOccupancy_ReportsTruncationPastTheBound) {
    AreaComponent area{};
    std::vector<Area3DOverlap> overlaps;
    overlaps.reserve(kMaximumArea3DOverlaps + 1U);
    for (std::uint32_t index = 1U; index <= static_cast<std::uint32_t>(kMaximumArea3DOverlaps) + 1U;
         ++index) {
        overlaps.push_back(Area3DOverlap{MakeEntity(index), 0.1F, false});
    }
    Area3D::RefreshOccupancy(area, overlaps);
    EXPECT_EQ(area.overlappingBodyCount, static_cast<std::uint8_t>(kMaximumArea3DOverlaps));
    EXPECT_TRUE(area.overlappingBodiesTruncated);
    EXPECT_EQ(*Area3D::GetOverlappingBody(area, 0U), MakeEntity(1U));
}

TEST(Area3DTest, EvaluateGravityAt_DirectionalNormalizesAndPointUsesInverseSquare) {
    Area3DField directional;
    directional.gravityDirection = {0.0F, -2.0F, 0.0F};
    directional.gravityMagnitude = 10.0F;
    const Math::Vector3 down =
        Area3D::EvaluateGravityAt(directional, Math::Vector3{1.0F, 2.0F, 3.0F});
    EXPECT_NEAR(down.x, 0.0F, kTolerance);
    EXPECT_NEAR(down.y, -10.0F, kTolerance);
    EXPECT_NEAR(down.z, 0.0F, kTolerance);

    Area3DField zeroDirection;
    zeroDirection.gravityDirection = {};
    zeroDirection.gravityMagnitude = 10.0F;
    EXPECT_EQ(Area3D::EvaluateGravityAt(zeroDirection, {}), Math::Vector3{});

    Area3DField point;
    point.gravityPoint = true;
    point.gravityPointWorldCenter = {};
    point.gravityMagnitude = 10.0F;
    point.gravityPointUnitDistance = 2.0F;
    const Math::Vector3 atUnit =
        Area3D::EvaluateGravityAt(point, Math::Vector3{2.0F, 0.0F, 0.0F});
    EXPECT_NEAR(atUnit.x, -10.0F, kTolerance);
    EXPECT_NEAR(atUnit.y, 0.0F, kTolerance);
    const Math::Vector3 farther =
        Area3D::EvaluateGravityAt(point, Math::Vector3{4.0F, 0.0F, 0.0F});
    EXPECT_NEAR(farther.x, -2.5F, kTolerance);

    point.gravityPointUnitDistance = 0.0F;
    const Math::Vector3 constant =
        Area3D::EvaluateGravityAt(point, Math::Vector3{4.0F, 0.0F, 0.0F});
    EXPECT_NEAR(constant.x, -10.0F, kTolerance);

    EXPECT_EQ(Area3D::EvaluateGravityAt(point, {}), Math::Vector3{});
}

TEST(Area3DTest, ResolveSpace_DisabledLeavesWorldAndBodyDamping) {
    const Math::Vector3 world{0.0F, -10.0F, 0.0F};
    Area3DField field = MakeDirectionalField(MakeEntity(1U), 0, AreaSpaceOverrideMode::Disabled,
                                                  Math::Vector3{0.0F, -4.0F, 0.0F});
    const Area3DSpaceResult result =
        Area3D::ResolveSpace(world, 0.2F, 0.0F, {}, std::span<const Area3DField>(&field, 1U));
    EXPECT_EQ(result.gravity, world);
    EXPECT_FLOAT_EQ(result.linearDamp, 0.2F);
    EXPECT_FALSE(result.gravityFromArea);
}

TEST(Area3DTest, ResolveSpace_CombineAddsAndReplaceStops) {
    const Math::Vector3 world{0.0F, -10.0F, 0.0F};
    const Area3DField combine = MakeDirectionalField(
        MakeEntity(1U), 0, AreaSpaceOverrideMode::Combine, Math::Vector3{0.0F, -5.0F, 0.0F});
    const Area3DSpaceResult combined =
        Area3D::ResolveSpace(world, 0.0F, 0.0F, {}, std::span<const Area3DField>(&combine, 1U));
    EXPECT_NEAR(combined.gravity.y, -15.0F, kTolerance);
    EXPECT_TRUE(combined.gravityFromArea);

    const Area3DField replace = MakeDirectionalField(
        MakeEntity(1U), 0, AreaSpaceOverrideMode::Replace, Math::Vector3{0.0F, 0.0F, 0.0F});
    const Area3DSpaceResult replaced =
        Area3D::ResolveSpace(world, 0.4F, 0.0F, {}, std::span<const Area3DField>(&replace, 1U));
    EXPECT_NEAR(replaced.gravity.y, 0.0F, kTolerance);
    EXPECT_FLOAT_EQ(replaced.linearDamp, 0.4F);
}

TEST(Area3DTest, ResolveSpace_PriorityAndTheFiveModes) {
    const Math::Vector3 world{0.0F, -10.0F, 0.0F};
    const Area3DField highReplace = MakeDirectionalField(
        MakeEntity(2U), 10, AreaSpaceOverrideMode::Replace, Math::Vector3{0.0F, 0.0F, 0.0F});
    const Area3DField lowCombine = MakeDirectionalField(
        MakeEntity(1U), 0, AreaSpaceOverrideMode::Combine, Math::Vector3{0.0F, -8.0F, 0.0F});
    const std::vector<Area3DField> replaceWins{lowCombine, highReplace};
    const Area3DSpaceResult replaced =
        Area3D::ResolveSpace(world, 0.0F, 0.0F, {}, replaceWins);
    EXPECT_NEAR(replaced.gravity.y, 0.0F, kTolerance);

    const Area3DField highReplaceCombine = MakeDirectionalField(
        MakeEntity(2U), 10, AreaSpaceOverrideMode::ReplaceCombine,
        Math::Vector3{0.0F, -5.0F, 0.0F});
    const std::vector<Area3DField> replaceThenCombine{lowCombine, highReplaceCombine};
    const Area3DSpaceResult mixed =
        Area3D::ResolveSpace(world, 0.0F, 0.0F, {}, replaceThenCombine);
    EXPECT_NEAR(mixed.gravity.y, -13.0F, kTolerance);

    const Area3DField highCombineReplace = MakeDirectionalField(
        MakeEntity(2U), 10, AreaSpaceOverrideMode::CombineReplace,
        Math::Vector3{0.0F, -1.0F, 0.0F});
    const std::vector<Area3DField> combineThenStop{lowCombine, highCombineReplace};
    const Area3DSpaceResult stopped =
        Area3D::ResolveSpace(world, 0.0F, 0.0F, {}, combineThenStop);
    EXPECT_NEAR(stopped.gravity.y, -11.0F, kTolerance);
}

TEST(Area3DTest, ResolveSpace_EqualPriorityBreaksTiesByEntityId) {
    const Math::Vector3 world{0.0F, -10.0F, 0.0F};
    const Area3DField later = MakeDirectionalField(
        MakeEntity(8U), 1, AreaSpaceOverrideMode::Replace, Math::Vector3{0.0F, -1.0F, 0.0F});
    const Area3DField earlier = MakeDirectionalField(
        MakeEntity(3U), 1, AreaSpaceOverrideMode::Replace, Math::Vector3{0.0F, -3.0F, 0.0F});
    const std::vector<Area3DField> fields{later, earlier};
    const Area3DSpaceResult result = Area3D::ResolveSpace(world, 0.0F, 0.0F, {}, fields);
    EXPECT_NEAR(result.gravity.y, -3.0F, kTolerance);
}

TEST(Area3DTest, ResolveSpace_LinearDampReplaceCancelsBodyDrag) {
    Area3DField field = MakeDirectionalField(MakeEntity(1U), 0,
                                                   AreaSpaceOverrideMode::Disabled, {});
    field.linearDampOverride = AreaSpaceOverrideMode::Replace;
    field.linearDamp = 0.0F;
    const Area3DSpaceResult result =
        Area3D::ResolveSpace({0.0F, -10.0F, 0.0F}, 0.9F, 0.0F, {},
                                   std::span<const Area3DField>(&field, 1U));
    EXPECT_FLOAT_EQ(result.linearDamp, 0.0F);
    EXPECT_TRUE(result.linearDampFromArea);
}

TEST(Area3DTest, MakeField_PosesPointGravityInWorld) {
    AreaComponent area{};
    area.gravityPoint = true;
    area.gravityPointOffset = {1.0F, 2.0F, 3.0F};
    area.priority = 4;
    const Area3DField field =
        Area3D::MakeField(MakeEntity(9U), area, Math::Vector3{10.0F, 0.0F, 0.0F});
    EXPECT_EQ(field.areaEntity, MakeEntity(9U));
    EXPECT_EQ(field.priority, 4);
    EXPECT_EQ(field.gravityPointWorldCenter, Math::Vector3(11.0F, 2.0F, 3.0F));
}

TEST_F(Area3DRuntimeTest, SyncOccupancy_ListsOverlappingColliderAndOtherArea) {
    const Scene::Entity area = MakeArea({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F});
    const Scene::Entity otherArea = MakeArea({0.5F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F});
    const Scene::Entity body = MakeCollider({0.25F, 0.0F, 0.0F});
    const Scene::Entity outside = MakeCollider({8.0F, 0.0F, 0.0F});

    const Area3DOccupancySyncResult report = SyncArea3DOccupancy(entityManager);
    EXPECT_EQ(report.areaCount, 2U);
    EXPECT_EQ(report.refreshedAreaCount, 2U);

    const AreaComponent& first = entityManager.GetComponent<AreaComponent>(area);
    EXPECT_TRUE(Area3D::HasOverlappingBody(first, body));
    EXPECT_FALSE(Area3D::HasOverlappingBody(first, outside));
    EXPECT_TRUE(Area3D::HasOverlappingArea(first, otherArea));
}

TEST_F(Area3DRuntimeTest, SyncOccupancy_NotMonitoringReportsNobody) {
    AreaComponent authored{};
    authored.monitoring = false;
    const Scene::Entity area = MakeArea({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, authored);
    MakeCollider({0.0F, 0.0F, 0.0F});
    static_cast<void>(SyncArea3DOccupancy(entityManager));
    const AreaComponent& stored = entityManager.GetComponent<AreaComponent>(area);
    EXPECT_EQ(stored.overlappingBodyCount, 0U);
}

TEST_F(Area3DRuntimeTest, Query_AllValidAreasIncludesNonMonitoringGravityVolume) {
    AreaComponent authored{};
    authored.monitoring = false;
    authored.gravityOverride = AreaSpaceOverrideMode::Replace;
    const Scene::Entity area = MakeArea({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, authored);
    const Scene::Entity body = MakeCollider({0.0F, 0.0F, 0.0F});

    EXPECT_TRUE(AreaOverlapSystem::Query(entityManager).overlaps.empty());

    const AreaOverlapQueryResult all = AreaOverlapSystem::Query(
        entityManager, kMaximumAreaOverlapResults, AreaOverlapParticipation::AllValidAreas);
    ASSERT_EQ(all.overlaps.size(), 1U);
    EXPECT_EQ(all.overlaps.front().area, area);
    EXPECT_EQ(all.overlaps.front().other, body);
}

TEST_F(Area3DRuntimeTest, PhysicsSystem_ReplaceZeroGravityHoldsTheBodyUp) {
    AreaComponent authored{};
    authored.gravityOverride = AreaSpaceOverrideMode::Replace;
    authored.gravityMagnitude = 0.0F;
    MakeArea({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, authored);
    const Scene::Entity body = MakeBody({0.0F, 0.0F, 0.0F});

    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, -10.0F, 0.0F});
    physicsSystem.Step(entityManager, sceneGraph, 0.1F);

    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity.y, 0.0F,
                kTolerance);
    EXPECT_NEAR(entityManager.GetComponent<Scene::WorldTransformComponent>(body).worldPosition.y,
                0.0F, kTolerance);
}

TEST_F(Area3DRuntimeTest, PhysicsSystem_CombineDoublesWorldGravity) {
    AreaComponent authored{};
    authored.gravityOverride = AreaSpaceOverrideMode::Combine;
    authored.gravityDirection = {0.0F, -1.0F, 0.0F};
    authored.gravityMagnitude = 10.0F;
    MakeArea({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, authored);
    const Scene::Entity body = MakeBody({0.0F, 0.0F, 0.0F});

    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, -10.0F, 0.0F});
    physicsSystem.Step(entityManager, sceneGraph, 0.1F);

    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity.y, -2.0F,
                kTolerance);
}

TEST_F(Area3DRuntimeTest, PhysicsSystem_HigherPriorityReplaceWinsOverCombine) {
    AreaComponent low{};
    low.gravityOverride = AreaSpaceOverrideMode::Combine;
    low.gravityDirection = {0.0F, -1.0F, 0.0F};
    low.gravityMagnitude = 10.0F;
    low.priority = 0;
    MakeArea({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, low);
    AreaComponent high{};
    high.gravityOverride = AreaSpaceOverrideMode::Replace;
    high.gravityMagnitude = 0.0F;
    high.priority = 5;
    MakeArea({0.25F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, high);
    const Scene::Entity body = MakeBody({0.0F, 0.0F, 0.0F});

    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, -10.0F, 0.0F});
    physicsSystem.Step(entityManager, sceneGraph, 0.1F);
    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity.y, 0.0F,
                kTolerance);
}

TEST_F(Area3DRuntimeTest, PhysicsSystem_NonMonitoringGravityAreaStillInfluencesBodies) {
    AreaComponent authored{};
    authored.monitoring = false;
    authored.gravityOverride = AreaSpaceOverrideMode::Replace;
    authored.gravityMagnitude = 0.0F;
    MakeArea({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, authored);
    const Scene::Entity body = MakeBody({0.0F, 0.0F, 0.0F});

    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, -10.0F, 0.0F});
    physicsSystem.Step(entityManager, sceneGraph, 0.1F);
    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity.y, 0.0F,
                kTolerance);
}

TEST_F(Area3DRuntimeTest, PhysicsSystem_KinematicBodyIgnoresAreaGravity) {
    AreaComponent authored{};
    authored.gravityOverride = AreaSpaceOverrideMode::Replace;
    authored.gravityDirection = {0.0F, 1.0F, 0.0F};
    authored.gravityMagnitude = 50.0F;
    MakeArea({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, authored);
    Scene::Rigid3DComponent kinematic;
    kinematic.isKinematic = true;
    kinematic.velocity = {0.0F, 3.0F, 0.0F};
    const Scene::Entity body = MakeBody({0.0F, 0.0F, 0.0F}, kinematic);

    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, -10.0F, 0.0F});
    physicsSystem.Step(entityManager, sceneGraph, 0.1F);
    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity.y, 3.0F,
                kTolerance);
}

TEST_F(Area3DRuntimeTest, PhysicsSystem_LayerMismatchDoesNotOverrideGravity) {
    AreaComponent authored{};
    authored.gravityOverride = AreaSpaceOverrideMode::Replace;
    authored.gravityMagnitude = 0.0F;
    authored.collisionMask = 2U;
    MakeArea({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, authored);
    const Scene::Entity body = MakeBody({0.0F, 0.0F, 0.0F});

    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, -10.0F, 0.0F});
    physicsSystem.Step(entityManager, sceneGraph, 0.1F);
    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity.y, -1.0F,
                kTolerance);
}

TEST_F(Area3DRuntimeTest, PhysicsSystem_PointGravityPullsTowardTheArea) {
    AreaComponent authored{};
    authored.gravityOverride = AreaSpaceOverrideMode::Replace;
    authored.gravityPoint = true;
    authored.gravityMagnitude = 10.0F;
    authored.gravityPointUnitDistance = 0.0F;
    MakeArea({0.0F, 0.0F, 0.0F}, {4.0F, 4.0F, 4.0F}, authored);
    const Scene::Entity body = MakeBody({2.0F, 0.0F, 0.0F});

    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, -10.0F, 0.0F});
    physicsSystem.Step(entityManager, sceneGraph, 0.1F);
    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity.x, -1.0F,
                kTolerance);
    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity.y, 0.0F,
                kTolerance);
}

TEST_F(Area3DRuntimeTest, PhysicsSystem_AreaLinearDampScalesVelocity) {
    AreaComponent authored{};
    authored.linearDampOverride = AreaSpaceOverrideMode::Replace;
    authored.linearDamp = 1.0F;
    MakeArea({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, authored);
    Scene::Rigid3DComponent rigidBody;
    rigidBody.velocity = {10.0F, 0.0F, 0.0F};
    rigidBody.drag = 0.0F;
    const Scene::Entity body = MakeBody({0.0F, 0.0F, 0.0F}, rigidBody);

    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{});
    physicsSystem.Step(entityManager, sceneGraph, 0.5F);
    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity.x, 5.0F,
                kTolerance);
}

TEST_F(Area3DRuntimeTest, PhysicsSystem_AreaAngularDampScalesAngularVelocity) {
    AreaComponent authored{};
    authored.angularDampOverride = AreaSpaceOverrideMode::Replace;
    authored.angularDamp = 1.0F;
    MakeArea({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, authored);
    Scene::Rigid3DComponent rigidBody;
    rigidBody.angularVelocity = {0.0F, 4.0F, 0.0F};
    rigidBody.inverseInertia = {0.0F, 1.0F, 0.0F};
    const Scene::Entity body = MakeBody({0.0F, 0.0F, 0.0F}, rigidBody);

    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{});
    physicsSystem.Step(entityManager, sceneGraph, 0.5F);
    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity.y, 0.0F,
                kTolerance);
    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(body).angularVelocity.y, 2.0F,
                kTolerance);
}

TEST_F(Area3DRuntimeTest, CollectArea3DBodySpaces_SortedAndFindable) {
    AreaComponent authored{};
    authored.gravityOverride = AreaSpaceOverrideMode::Replace;
    authored.gravityMagnitude = 0.0F;
    MakeArea({0.0F, 0.0F, 0.0F}, {3.0F, 3.0F, 3.0F}, authored);
    const Scene::Entity first = MakeBody({0.5F, 0.0F, 0.0F});
    const Scene::Entity second = MakeBody({-0.5F, 0.0F, 0.0F});

    const std::vector<Area3DBodySpace> spaces =
        CollectArea3DBodySpaces(entityManager, Math::Vector3{0.0F, -10.0F, 0.0F});
    ASSERT_EQ(spaces.size(), 2U);
    EXPECT_LE(spaces[0].body.index, spaces[1].body.index);
    ASSERT_NE(FindArea3DBodySpace(spaces, first), nullptr);
    ASSERT_NE(FindArea3DBodySpace(spaces, second), nullptr);
    EXPECT_EQ(FindArea3DBodySpace(spaces, kInvalidEntity), nullptr);
}

} // namespace
} // namespace UVE::Physics::Tests
