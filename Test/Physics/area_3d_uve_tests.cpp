// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/area_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/area_3d_uve.h"
#include "uve/physics/area_3d_runtime_uve.h"
#include "uve/physics/area_overlap_system_uve.h"
#include "uve/physics/collision_system_uve.h"
#include "uve/physics/physics_system_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::Area3DFieldUVE;
using Scene::Area3DOverlapUVE;
using Scene::Area3DSpaceResultUVE;
using Scene::Area3DUVE;
using Scene::AreaComponentUVE;
using Scene::AreaSpaceOverrideModeUVE;
using Scene::EntityUVE;
using Scene::kInvalidEntityUVE;
using Scene::kMaximumArea3DOverlapsUVE;

constexpr float kToleranceUVE = 1.0e-4F;

[[nodiscard]] EntityUVE MakeEntityUVE(const std::uint32_t index,
                                      const std::uint32_t generation = 1U) noexcept {
    EntityUVE entity;
    entity.index = index;
    entity.generation = generation;
    return entity;
}

[[nodiscard]] Area3DFieldUVE MakeDirectionalFieldUVE(const EntityUVE areaEntity,
                                                     const std::int32_t priority,
                                                     const AreaSpaceOverrideModeUVE gravityMode,
                                                     const Math::Vector3UVE& gravity,
                                                     const AreaSpaceOverrideModeUVE linearMode =
                                                         AreaSpaceOverrideModeUVE::Disabled,
                                                     const float linearDamp = 0.0F,
                                                     const AreaSpaceOverrideModeUVE angularMode =
                                                         AreaSpaceOverrideModeUVE::Disabled,
                                                     const float angularDamp = 0.0F) {
    Area3DFieldUVE field;
    field.areaEntity = areaEntity;
    field.priority = priority;
    field.gravityOverride = gravityMode;
    field.gravityDirection = gravity;
    field.gravityMagnitude = 1.0F;
    field.linearDampOverride = linearMode;
    field.linearDamp = linearDamp;
    field.angularDampOverride = angularMode;
    field.angularDamp = angularDamp;
    const float lengthSquared = Math::LengthSquaredUVE(gravity);
    if (lengthSquared > 0.0F) {
        field.gravityDirection = gravity;
        field.gravityMagnitude = std::sqrt(lengthSquared);
        field.gravityDirection = Math::NormalizeUVE(gravity);
    } else {
        field.gravityDirection = {};
        field.gravityMagnitude = 0.0F;
    }
    return field;
}

class Area3DRuntimeUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    CollisionSystemUVE collisionSystem;

    Scene::EntityUVE MakeAreaUVE(const Math::Vector3UVE& position, const Math::Vector3UVE& halfExtents,
                                 AreaComponentUVE area = {}) {
        area.halfExtents = halfExtents;
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform;
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<AreaComponentUVE>(entity, area);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    Scene::EntityUVE MakeBodyUVE(const Math::Vector3UVE& position,
                                 Scene::Rigid3DComponentUVE rigidBody = {},
                                 const Math::Vector3UVE& halfExtents = {0.5F, 0.5F, 0.5F}) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform;
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<Scene::Rigid3DComponentUVE>(entity, rigidBody);
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(
            entity, Scene::ColliderComponentUVE{halfExtents});
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    Scene::EntityUVE MakeColliderUVE(const Math::Vector3UVE& position,
                                     const Math::Vector3UVE& halfExtents = {0.5F, 0.5F, 0.5F}) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform;
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(
            entity, Scene::ColliderComponentUVE{halfExtents});
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }
};

TEST(AreaComponentUVETest, IsAreaComponentValidUVE_RejectsUnsafeSpaceOverride) {
    EXPECT_TRUE(Scene::IsAreaComponentValidUVE(AreaComponentUVE{}));
    EXPECT_TRUE(Scene::HasAreaSpaceOverrideUVE(AreaComponentUVE{}) == false);

    AreaComponentUVE invalid = {};
    invalid.linearDamp = -0.1F;
    EXPECT_FALSE(Scene::IsAreaComponentValidUVE(invalid));
    invalid = {};
    invalid.angularDamp = -0.1F;
    EXPECT_FALSE(Scene::IsAreaComponentValidUVE(invalid));
    invalid = {};
    invalid.gravityPointUnitDistance = -1.0F;
    EXPECT_FALSE(Scene::IsAreaComponentValidUVE(invalid));
    invalid = {};
    invalid.gravityMagnitude = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(Scene::IsAreaComponentValidUVE(invalid));
    invalid = {};
    invalid.gravityOverride = static_cast<AreaSpaceOverrideModeUVE>(9U);
    EXPECT_FALSE(Scene::IsAreaComponentValidUVE(invalid));
    invalid = {};
    invalid.gravityDirection.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Scene::IsAreaComponentValidUVE(invalid));
}

TEST(Area3DUVETest, RefreshOccupancyUVE_SplitsBodiesAndAreasCapsAndClearsWhenNotMonitoring) {
    AreaComponentUVE area{};
    const EntityUVE bodyA = MakeEntityUVE(1U);
    const EntityUVE bodyB = MakeEntityUVE(2U);
    const EntityUVE otherArea = MakeEntityUVE(3U);
    const std::vector<Area3DOverlapUVE> overlaps{
        Area3DOverlapUVE{bodyA, 0.2F, false},
        Area3DOverlapUVE{otherArea, 0.1F, true},
        Area3DOverlapUVE{bodyB, 0.3F, false},
        Area3DOverlapUVE{bodyA, 0.4F, false},
    };
    Area3DUVE::RefreshOccupancyUVE(area, overlaps);
    ASSERT_EQ(area.overlappingBodyCount, 2U);
    EXPECT_EQ(*Area3DUVE::GetOverlappingBodyUVE(area, 0U), bodyA);
    EXPECT_EQ(*Area3DUVE::GetOverlappingBodyUVE(area, 1U), bodyB);
    EXPECT_EQ(Area3DUVE::GetOverlappingBodyUVE(area, 2U), nullptr);
    ASSERT_EQ(area.overlappingAreaCount, 1U);
    EXPECT_EQ(*Area3DUVE::GetOverlappingAreaUVE(area, 0U), otherArea);
    EXPECT_TRUE(Area3DUVE::HasOverlappingBodyUVE(area, bodyA));
    EXPECT_FALSE(Area3DUVE::HasOverlappingAreaUVE(area, bodyA));
    EXPECT_FALSE(area.overlappingBodiesTruncated);

    area.monitoring = false;
    Area3DUVE::RefreshOccupancyUVE(area, overlaps);
    EXPECT_EQ(area.overlappingBodyCount, 0U);
    EXPECT_EQ(area.overlappingAreaCount, 0U);
    EXPECT_FALSE(Area3DUVE::HasOverlappingBodyUVE(area, bodyA));
}

TEST(Area3DUVETest, RefreshOccupancyUVE_ReportsTruncationPastTheBound) {
    AreaComponentUVE area{};
    std::vector<Area3DOverlapUVE> overlaps;
    overlaps.reserve(kMaximumArea3DOverlapsUVE + 1U);
    for (std::uint32_t index = 1U; index <= static_cast<std::uint32_t>(kMaximumArea3DOverlapsUVE) + 1U;
         ++index) {
        overlaps.push_back(Area3DOverlapUVE{MakeEntityUVE(index), 0.1F, false});
    }
    Area3DUVE::RefreshOccupancyUVE(area, overlaps);
    EXPECT_EQ(area.overlappingBodyCount, static_cast<std::uint8_t>(kMaximumArea3DOverlapsUVE));
    EXPECT_TRUE(area.overlappingBodiesTruncated);
    EXPECT_EQ(*Area3DUVE::GetOverlappingBodyUVE(area, 0U), MakeEntityUVE(1U));
}

TEST(Area3DUVETest, EvaluateGravityAtUVE_DirectionalNormalizesAndPointUsesInverseSquare) {
    Area3DFieldUVE directional;
    directional.gravityDirection = {0.0F, -2.0F, 0.0F};
    directional.gravityMagnitude = 10.0F;
    const Math::Vector3UVE down =
        Area3DUVE::EvaluateGravityAtUVE(directional, Math::Vector3UVE{1.0F, 2.0F, 3.0F});
    EXPECT_NEAR(down.x, 0.0F, kToleranceUVE);
    EXPECT_NEAR(down.y, -10.0F, kToleranceUVE);
    EXPECT_NEAR(down.z, 0.0F, kToleranceUVE);

    Area3DFieldUVE zeroDirection;
    zeroDirection.gravityDirection = {};
    zeroDirection.gravityMagnitude = 10.0F;
    EXPECT_EQ(Area3DUVE::EvaluateGravityAtUVE(zeroDirection, {}), Math::Vector3UVE{});

    Area3DFieldUVE point;
    point.gravityPoint = true;
    point.gravityPointWorldCenter = {};
    point.gravityMagnitude = 10.0F;
    point.gravityPointUnitDistance = 2.0F;
    const Math::Vector3UVE atUnit =
        Area3DUVE::EvaluateGravityAtUVE(point, Math::Vector3UVE{2.0F, 0.0F, 0.0F});
    EXPECT_NEAR(atUnit.x, -10.0F, kToleranceUVE);
    EXPECT_NEAR(atUnit.y, 0.0F, kToleranceUVE);
    const Math::Vector3UVE farther =
        Area3DUVE::EvaluateGravityAtUVE(point, Math::Vector3UVE{4.0F, 0.0F, 0.0F});
    EXPECT_NEAR(farther.x, -2.5F, kToleranceUVE);

    point.gravityPointUnitDistance = 0.0F;
    const Math::Vector3UVE constant =
        Area3DUVE::EvaluateGravityAtUVE(point, Math::Vector3UVE{4.0F, 0.0F, 0.0F});
    EXPECT_NEAR(constant.x, -10.0F, kToleranceUVE);

    EXPECT_EQ(Area3DUVE::EvaluateGravityAtUVE(point, {}), Math::Vector3UVE{});
}

TEST(Area3DUVETest, ResolveSpaceUVE_DisabledLeavesWorldAndBodyDamping) {
    const Math::Vector3UVE world{0.0F, -10.0F, 0.0F};
    Area3DFieldUVE field = MakeDirectionalFieldUVE(MakeEntityUVE(1U), 0, AreaSpaceOverrideModeUVE::Disabled,
                                                  Math::Vector3UVE{0.0F, -4.0F, 0.0F});
    const Area3DSpaceResultUVE result =
        Area3DUVE::ResolveSpaceUVE(world, 0.2F, 0.0F, {}, std::span<const Area3DFieldUVE>(&field, 1U));
    EXPECT_EQ(result.gravity, world);
    EXPECT_FLOAT_EQ(result.linearDamp, 0.2F);
    EXPECT_FALSE(result.gravityFromArea);
}

TEST(Area3DUVETest, ResolveSpaceUVE_CombineAddsAndReplaceStops) {
    const Math::Vector3UVE world{0.0F, -10.0F, 0.0F};
    const Area3DFieldUVE combine = MakeDirectionalFieldUVE(
        MakeEntityUVE(1U), 0, AreaSpaceOverrideModeUVE::Combine, Math::Vector3UVE{0.0F, -5.0F, 0.0F});
    const Area3DSpaceResultUVE combined =
        Area3DUVE::ResolveSpaceUVE(world, 0.0F, 0.0F, {}, std::span<const Area3DFieldUVE>(&combine, 1U));
    EXPECT_NEAR(combined.gravity.y, -15.0F, kToleranceUVE);
    EXPECT_TRUE(combined.gravityFromArea);

    const Area3DFieldUVE replace = MakeDirectionalFieldUVE(
        MakeEntityUVE(1U), 0, AreaSpaceOverrideModeUVE::Replace, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    const Area3DSpaceResultUVE replaced =
        Area3DUVE::ResolveSpaceUVE(world, 0.4F, 0.0F, {}, std::span<const Area3DFieldUVE>(&replace, 1U));
    EXPECT_NEAR(replaced.gravity.y, 0.0F, kToleranceUVE);
    EXPECT_FLOAT_EQ(replaced.linearDamp, 0.4F);
}

TEST(Area3DUVETest, ResolveSpaceUVE_PriorityAndTheFiveModes) {
    const Math::Vector3UVE world{0.0F, -10.0F, 0.0F};
    const Area3DFieldUVE highReplace = MakeDirectionalFieldUVE(
        MakeEntityUVE(2U), 10, AreaSpaceOverrideModeUVE::Replace, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    const Area3DFieldUVE lowCombine = MakeDirectionalFieldUVE(
        MakeEntityUVE(1U), 0, AreaSpaceOverrideModeUVE::Combine, Math::Vector3UVE{0.0F, -8.0F, 0.0F});
    const std::vector<Area3DFieldUVE> replaceWins{lowCombine, highReplace};
    const Area3DSpaceResultUVE replaced =
        Area3DUVE::ResolveSpaceUVE(world, 0.0F, 0.0F, {}, replaceWins);
    EXPECT_NEAR(replaced.gravity.y, 0.0F, kToleranceUVE);

    const Area3DFieldUVE highReplaceCombine = MakeDirectionalFieldUVE(
        MakeEntityUVE(2U), 10, AreaSpaceOverrideModeUVE::ReplaceCombine,
        Math::Vector3UVE{0.0F, -5.0F, 0.0F});
    const std::vector<Area3DFieldUVE> replaceThenCombine{lowCombine, highReplaceCombine};
    const Area3DSpaceResultUVE mixed =
        Area3DUVE::ResolveSpaceUVE(world, 0.0F, 0.0F, {}, replaceThenCombine);
    EXPECT_NEAR(mixed.gravity.y, -13.0F, kToleranceUVE);

    const Area3DFieldUVE highCombineReplace = MakeDirectionalFieldUVE(
        MakeEntityUVE(2U), 10, AreaSpaceOverrideModeUVE::CombineReplace,
        Math::Vector3UVE{0.0F, -1.0F, 0.0F});
    const std::vector<Area3DFieldUVE> combineThenStop{lowCombine, highCombineReplace};
    const Area3DSpaceResultUVE stopped =
        Area3DUVE::ResolveSpaceUVE(world, 0.0F, 0.0F, {}, combineThenStop);
    EXPECT_NEAR(stopped.gravity.y, -11.0F, kToleranceUVE);
}

TEST(Area3DUVETest, ResolveSpaceUVE_EqualPriorityBreaksTiesByEntityId) {
    const Math::Vector3UVE world{0.0F, -10.0F, 0.0F};
    const Area3DFieldUVE later = MakeDirectionalFieldUVE(
        MakeEntityUVE(8U), 1, AreaSpaceOverrideModeUVE::Replace, Math::Vector3UVE{0.0F, -1.0F, 0.0F});
    const Area3DFieldUVE earlier = MakeDirectionalFieldUVE(
        MakeEntityUVE(3U), 1, AreaSpaceOverrideModeUVE::Replace, Math::Vector3UVE{0.0F, -3.0F, 0.0F});
    const std::vector<Area3DFieldUVE> fields{later, earlier};
    const Area3DSpaceResultUVE result = Area3DUVE::ResolveSpaceUVE(world, 0.0F, 0.0F, {}, fields);
    EXPECT_NEAR(result.gravity.y, -3.0F, kToleranceUVE);
}

TEST(Area3DUVETest, ResolveSpaceUVE_LinearDampReplaceCancelsBodyDrag) {
    Area3DFieldUVE field = MakeDirectionalFieldUVE(MakeEntityUVE(1U), 0,
                                                   AreaSpaceOverrideModeUVE::Disabled, {});
    field.linearDampOverride = AreaSpaceOverrideModeUVE::Replace;
    field.linearDamp = 0.0F;
    const Area3DSpaceResultUVE result =
        Area3DUVE::ResolveSpaceUVE({0.0F, -10.0F, 0.0F}, 0.9F, 0.0F, {},
                                   std::span<const Area3DFieldUVE>(&field, 1U));
    EXPECT_FLOAT_EQ(result.linearDamp, 0.0F);
    EXPECT_TRUE(result.linearDampFromArea);
}

TEST(Area3DUVETest, MakeFieldUVE_PosesPointGravityInWorld) {
    AreaComponentUVE area{};
    area.gravityPoint = true;
    area.gravityPointOffset = {1.0F, 2.0F, 3.0F};
    area.priority = 4;
    const Area3DFieldUVE field =
        Area3DUVE::MakeFieldUVE(MakeEntityUVE(9U), area, Math::Vector3UVE{10.0F, 0.0F, 0.0F});
    EXPECT_EQ(field.areaEntity, MakeEntityUVE(9U));
    EXPECT_EQ(field.priority, 4);
    EXPECT_EQ(field.gravityPointWorldCenter, Math::Vector3UVE(11.0F, 2.0F, 3.0F));
}

TEST_F(Area3DRuntimeUVETest, SyncOccupancy_ListsOverlappingColliderAndOtherArea) {
    const Scene::EntityUVE area = MakeAreaUVE({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F});
    const Scene::EntityUVE otherArea = MakeAreaUVE({0.5F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F});
    const Scene::EntityUVE body = MakeColliderUVE({0.25F, 0.0F, 0.0F});
    const Scene::EntityUVE outside = MakeColliderUVE({8.0F, 0.0F, 0.0F});

    const Area3DOccupancySyncResultUVE report = SyncArea3DOccupancyUVE(entityManager);
    EXPECT_EQ(report.areaCount, 2U);
    EXPECT_EQ(report.refreshedAreaCount, 2U);

    const AreaComponentUVE& first = entityManager.GetComponentUVE<AreaComponentUVE>(area);
    EXPECT_TRUE(Area3DUVE::HasOverlappingBodyUVE(first, body));
    EXPECT_FALSE(Area3DUVE::HasOverlappingBodyUVE(first, outside));
    EXPECT_TRUE(Area3DUVE::HasOverlappingAreaUVE(first, otherArea));
}

TEST_F(Area3DRuntimeUVETest, SyncOccupancy_NotMonitoringReportsNobody) {
    AreaComponentUVE authored{};
    authored.monitoring = false;
    const Scene::EntityUVE area = MakeAreaUVE({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, authored);
    MakeColliderUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(SyncArea3DOccupancyUVE(entityManager));
    const AreaComponentUVE& stored = entityManager.GetComponentUVE<AreaComponentUVE>(area);
    EXPECT_EQ(stored.overlappingBodyCount, 0U);
}

TEST_F(Area3DRuntimeUVETest, QueryUVE_AllValidAreasIncludesNonMonitoringGravityVolume) {
    AreaComponentUVE authored{};
    authored.monitoring = false;
    authored.gravityOverride = AreaSpaceOverrideModeUVE::Replace;
    const Scene::EntityUVE area = MakeAreaUVE({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, authored);
    const Scene::EntityUVE body = MakeColliderUVE({0.0F, 0.0F, 0.0F});

    EXPECT_TRUE(AreaOverlapSystemUVE::QueryUVE(entityManager).overlaps.empty());

    const AreaOverlapQueryResultUVE all = AreaOverlapSystemUVE::QueryUVE(
        entityManager, kMaximumAreaOverlapResultsUVE, AreaOverlapParticipationUVE::AllValidAreas);
    ASSERT_EQ(all.overlaps.size(), 1U);
    EXPECT_EQ(all.overlaps.front().area, area);
    EXPECT_EQ(all.overlaps.front().other, body);
}

TEST_F(Area3DRuntimeUVETest, PhysicsSystem_ReplaceZeroGravityHoldsTheBodyUp) {
    AreaComponentUVE authored{};
    authored.gravityOverride = AreaSpaceOverrideModeUVE::Replace;
    authored.gravityMagnitude = 0.0F;
    MakeAreaUVE({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, authored);
    const Scene::EntityUVE body = MakeBodyUVE({0.0F, 0.0F, 0.0F});

    PhysicsSystemUVE physicsSystem(collisionSystem, Math::Vector3UVE{0.0F, -10.0F, 0.0F});
    physicsSystem.StepUVE(entityManager, sceneGraph, 0.1F);

    EXPECT_NEAR(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(body).velocity.y, 0.0F,
                kToleranceUVE);
    EXPECT_NEAR(entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(body).worldPosition.y,
                0.0F, kToleranceUVE);
}

TEST_F(Area3DRuntimeUVETest, PhysicsSystem_CombineDoublesWorldGravity) {
    AreaComponentUVE authored{};
    authored.gravityOverride = AreaSpaceOverrideModeUVE::Combine;
    authored.gravityDirection = {0.0F, -1.0F, 0.0F};
    authored.gravityMagnitude = 10.0F;
    MakeAreaUVE({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, authored);
    const Scene::EntityUVE body = MakeBodyUVE({0.0F, 0.0F, 0.0F});

    PhysicsSystemUVE physicsSystem(collisionSystem, Math::Vector3UVE{0.0F, -10.0F, 0.0F});
    physicsSystem.StepUVE(entityManager, sceneGraph, 0.1F);

    EXPECT_NEAR(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(body).velocity.y, -2.0F,
                kToleranceUVE);
}

TEST_F(Area3DRuntimeUVETest, PhysicsSystem_HigherPriorityReplaceWinsOverCombine) {
    AreaComponentUVE low{};
    low.gravityOverride = AreaSpaceOverrideModeUVE::Combine;
    low.gravityDirection = {0.0F, -1.0F, 0.0F};
    low.gravityMagnitude = 10.0F;
    low.priority = 0;
    MakeAreaUVE({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, low);
    AreaComponentUVE high{};
    high.gravityOverride = AreaSpaceOverrideModeUVE::Replace;
    high.gravityMagnitude = 0.0F;
    high.priority = 5;
    MakeAreaUVE({0.25F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, high);
    const Scene::EntityUVE body = MakeBodyUVE({0.0F, 0.0F, 0.0F});

    PhysicsSystemUVE physicsSystem(collisionSystem, Math::Vector3UVE{0.0F, -10.0F, 0.0F});
    physicsSystem.StepUVE(entityManager, sceneGraph, 0.1F);
    EXPECT_NEAR(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(body).velocity.y, 0.0F,
                kToleranceUVE);
}

TEST_F(Area3DRuntimeUVETest, PhysicsSystem_NonMonitoringGravityAreaStillInfluencesBodies) {
    AreaComponentUVE authored{};
    authored.monitoring = false;
    authored.gravityOverride = AreaSpaceOverrideModeUVE::Replace;
    authored.gravityMagnitude = 0.0F;
    MakeAreaUVE({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, authored);
    const Scene::EntityUVE body = MakeBodyUVE({0.0F, 0.0F, 0.0F});

    PhysicsSystemUVE physicsSystem(collisionSystem, Math::Vector3UVE{0.0F, -10.0F, 0.0F});
    physicsSystem.StepUVE(entityManager, sceneGraph, 0.1F);
    EXPECT_NEAR(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(body).velocity.y, 0.0F,
                kToleranceUVE);
}

TEST_F(Area3DRuntimeUVETest, PhysicsSystem_KinematicBodyIgnoresAreaGravity) {
    AreaComponentUVE authored{};
    authored.gravityOverride = AreaSpaceOverrideModeUVE::Replace;
    authored.gravityDirection = {0.0F, 1.0F, 0.0F};
    authored.gravityMagnitude = 50.0F;
    MakeAreaUVE({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, authored);
    Scene::Rigid3DComponentUVE kinematic;
    kinematic.isKinematic = true;
    kinematic.velocity = {0.0F, 3.0F, 0.0F};
    const Scene::EntityUVE body = MakeBodyUVE({0.0F, 0.0F, 0.0F}, kinematic);

    PhysicsSystemUVE physicsSystem(collisionSystem, Math::Vector3UVE{0.0F, -10.0F, 0.0F});
    physicsSystem.StepUVE(entityManager, sceneGraph, 0.1F);
    EXPECT_NEAR(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(body).velocity.y, 3.0F,
                kToleranceUVE);
}

TEST_F(Area3DRuntimeUVETest, PhysicsSystem_LayerMismatchDoesNotOverrideGravity) {
    AreaComponentUVE authored{};
    authored.gravityOverride = AreaSpaceOverrideModeUVE::Replace;
    authored.gravityMagnitude = 0.0F;
    authored.collisionMask = 2U;
    MakeAreaUVE({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, authored);
    const Scene::EntityUVE body = MakeBodyUVE({0.0F, 0.0F, 0.0F});

    PhysicsSystemUVE physicsSystem(collisionSystem, Math::Vector3UVE{0.0F, -10.0F, 0.0F});
    physicsSystem.StepUVE(entityManager, sceneGraph, 0.1F);
    EXPECT_NEAR(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(body).velocity.y, -1.0F,
                kToleranceUVE);
}

TEST_F(Area3DRuntimeUVETest, PhysicsSystem_PointGravityPullsTowardTheArea) {
    AreaComponentUVE authored{};
    authored.gravityOverride = AreaSpaceOverrideModeUVE::Replace;
    authored.gravityPoint = true;
    authored.gravityMagnitude = 10.0F;
    authored.gravityPointUnitDistance = 0.0F;
    MakeAreaUVE({0.0F, 0.0F, 0.0F}, {4.0F, 4.0F, 4.0F}, authored);
    const Scene::EntityUVE body = MakeBodyUVE({2.0F, 0.0F, 0.0F});

    PhysicsSystemUVE physicsSystem(collisionSystem, Math::Vector3UVE{0.0F, -10.0F, 0.0F});
    physicsSystem.StepUVE(entityManager, sceneGraph, 0.1F);
    EXPECT_NEAR(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(body).velocity.x, -1.0F,
                kToleranceUVE);
    EXPECT_NEAR(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(body).velocity.y, 0.0F,
                kToleranceUVE);
}

TEST_F(Area3DRuntimeUVETest, PhysicsSystem_AreaLinearDampScalesVelocity) {
    AreaComponentUVE authored{};
    authored.linearDampOverride = AreaSpaceOverrideModeUVE::Replace;
    authored.linearDamp = 1.0F;
    MakeAreaUVE({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, authored);
    Scene::Rigid3DComponentUVE rigidBody;
    rigidBody.velocity = {10.0F, 0.0F, 0.0F};
    rigidBody.drag = 0.0F;
    const Scene::EntityUVE body = MakeBodyUVE({0.0F, 0.0F, 0.0F}, rigidBody);

    PhysicsSystemUVE physicsSystem(collisionSystem, Math::Vector3UVE{});
    physicsSystem.StepUVE(entityManager, sceneGraph, 0.5F);
    EXPECT_NEAR(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(body).velocity.x, 5.0F,
                kToleranceUVE);
}

TEST_F(Area3DRuntimeUVETest, PhysicsSystem_AreaAngularDampScalesAngularVelocity) {
    AreaComponentUVE authored{};
    authored.angularDampOverride = AreaSpaceOverrideModeUVE::Replace;
    authored.angularDamp = 1.0F;
    MakeAreaUVE({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, authored);
    Scene::Rigid3DComponentUVE rigidBody;
    rigidBody.angularVelocity = {0.0F, 4.0F, 0.0F};
    rigidBody.inverseInertia = {0.0F, 1.0F, 0.0F};
    const Scene::EntityUVE body = MakeBodyUVE({0.0F, 0.0F, 0.0F}, rigidBody);

    PhysicsSystemUVE physicsSystem(collisionSystem, Math::Vector3UVE{});
    physicsSystem.StepUVE(entityManager, sceneGraph, 0.5F);
    EXPECT_NEAR(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(body).velocity.y, 0.0F,
                kToleranceUVE);
    EXPECT_NEAR(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(body).angularVelocity.y, 2.0F,
                kToleranceUVE);
}

TEST_F(Area3DRuntimeUVETest, CollectArea3DBodySpacesUVE_SortedAndFindable) {
    AreaComponentUVE authored{};
    authored.gravityOverride = AreaSpaceOverrideModeUVE::Replace;
    authored.gravityMagnitude = 0.0F;
    MakeAreaUVE({0.0F, 0.0F, 0.0F}, {3.0F, 3.0F, 3.0F}, authored);
    const Scene::EntityUVE first = MakeBodyUVE({0.5F, 0.0F, 0.0F});
    const Scene::EntityUVE second = MakeBodyUVE({-0.5F, 0.0F, 0.0F});

    const std::vector<Area3DBodySpaceUVE> spaces =
        CollectArea3DBodySpacesUVE(entityManager, Math::Vector3UVE{0.0F, -10.0F, 0.0F});
    ASSERT_EQ(spaces.size(), 2U);
    EXPECT_LE(spaces[0].body.index, spaces[1].body.index);
    ASSERT_NE(FindArea3DBodySpaceUVE(spaces, first), nullptr);
    ASSERT_NE(FindArea3DBodySpaceUVE(spaces, second), nullptr);
    EXPECT_EQ(FindArea3DBodySpaceUVE(spaces, kInvalidEntityUVE), nullptr);
}

} // namespace
} // namespace UVE::Physics::Tests
