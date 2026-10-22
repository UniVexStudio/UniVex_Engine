// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/collider_component_uve.h"
#include "uve/component/disabled_component_uve.h"
#include "uve/component/physics_object_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/collider_debug_scene_uve.h"

namespace UVE::Scene::Tests {
namespace {

/// Room for four capsules and then some: every test below stays well inside it, so a truncation
/// assertion can only come from the setting under test.
constexpr std::size_t kBufferLinesUVE = 1024U;

} // namespace

class ColliderDebugSceneUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    std::vector<Physics::ColliderDebugLineUVE> lines{kBufferLinesUVE};

    [[nodiscard]] EntityUVE MakeColliderUVE(const Math::Vector3UVE position,
                                            const ColliderShapeTypeUVE shape = ColliderShapeTypeUVE::Box,
                                            const std::uint32_t layer = 1U) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<TransformComponentUVE>(entity, TransformComponentUVE{});
        WorldTransformComponentUVE world;
        world.worldPosition = position;
        world.dirty = false;
        entityManager.AddComponentUVE<WorldTransformComponentUVE>(entity, world);
        ColliderComponentUVE collider;
        collider.shapeType = shape;
        collider.collisionLayer = layer;
        entityManager.AddComponentUVE<ColliderComponentUVE>(entity, collider);
        return entity;
    }

    [[nodiscard]] ColliderDebugSceneStatsUVE Build(const ColliderDebugSceneSettingsUVE& settings = {}) {
        return BuildColliderDebugSceneUVE(entityManager, settings,
                                          std::span<Physics::ColliderDebugLineUVE>{lines.data(), lines.size()});
    }
};

TEST_F(ColliderDebugSceneUVETest, DrawsOneWireframePerCollider) {
    static_cast<void>(MakeColliderUVE({0.0F, 0.0F, 0.0F}));
    static_cast<void>(MakeColliderUVE({5.0F, 0.0F, 0.0F}));
    static_cast<void>(MakeColliderUVE({10.0F, 0.0F, 0.0F}));

    const ColliderDebugSceneStatsUVE stats = Build();
    EXPECT_EQ(stats.colliderCount, 3U);
    EXPECT_EQ(stats.lineCount, 36U) << "12 edges each";
    EXPECT_FALSE(stats.truncated);

    // The geometry really follows the entity it belongs to: the third box is centred at x = 10.
    bool sawFarBox = false;
    for (std::size_t index = 0U; index < stats.lineCount; ++index) {
        if (lines[index].from.x > 9.0F) sawFarBox = true;
    }
    EXPECT_TRUE(sawFarBox);
}

TEST_F(ColliderDebugSceneUVETest, SkipsCollidersThatAreNotInTheSimulation) {
    static_cast<void>(MakeColliderUVE({}));

    // A collider the author switched off contributes nothing to the simulation, so it must not be
    // drawn: a debug view that over-reports is one an author cannot trust.
    const EntityUVE disabledCollider = MakeColliderUVE({5.0F, 0.0F, 0.0F});
    entityManager.GetComponentUVE<ColliderComponentUVE>(disabledCollider).disabled = true;

    // An entity disabled outright is skipped for the same reason, and one the Process mode has
    // taken out of the physics world is not there to be hit either.
    const EntityUVE disabledEntity = MakeColliderUVE({10.0F, 0.0F, 0.0F});
    entityManager.AddComponentUVE<DisabledComponentUVE>(disabledEntity, DisabledComponentUVE{});

    const EntityUVE removedFromWorld = MakeColliderUVE({15.0F, 0.0F, 0.0F});
    entityManager.AddComponentUVE<PhysicsObjectComponentUVE>(removedFromWorld,
                                                             PhysicsObjectComponentUVE{});
    ProcessComponentUVE stopped;
    stopped.mode = TickModeUVE::Never;
    stopped.resolvedModeInHierarchy = TickModeUVE::Never;
    entityManager.AddComponentUVE<ProcessComponentUVE>(removedFromWorld, stopped);

    const ColliderDebugSceneStatsUVE stats = Build();
    EXPECT_EQ(stats.colliderCount, 1U) << "only the live collider is drawn";
    EXPECT_EQ(stats.lineCount, 12U);
}

TEST_F(ColliderDebugSceneUVETest, IncludeDisabledDrawsTheColliderItself) {
    const EntityUVE disabledCollider = MakeColliderUVE({});
    entityManager.GetComponentUVE<ColliderComponentUVE>(disabledCollider).disabled = true;

    ColliderDebugSceneSettingsUVE settings;
    settings.includeDisabled = true;
    // The switch is honoured only while the object is still in the world: `includeDisabled` speaks
    // for the collider's own checkbox, not for a body the Process mode has taken away.
    EXPECT_EQ(Build(settings).colliderCount, 1U);
}

TEST_F(ColliderDebugSceneUVETest, FiltersByLayerMask) {
    static_cast<void>(MakeColliderUVE({}, ColliderShapeTypeUVE::Box, 0x1U));
    static_cast<void>(MakeColliderUVE({}, ColliderShapeTypeUVE::Box, 0x2U));

    ColliderDebugSceneSettingsUVE settings;
    settings.layerMask = 0x2U;
    const ColliderDebugSceneStatsUVE stats = Build(settings);
    EXPECT_EQ(stats.colliderCount, 1U);

    settings.layerMask = 0U; // zero means "no filter", not "nothing matches"
    EXPECT_EQ(Build(settings).colliderCount, 2U);
}

TEST_F(ColliderDebugSceneUVETest, StopsAtTheColliderCap) {
    for (int index = 0; index < 10; ++index) {
        static_cast<void>(MakeColliderUVE({static_cast<float>(index) * 3.0F, 0.0F, 0.0F}));
    }
    ColliderDebugSceneSettingsUVE settings;
    settings.maximumColliders = 4U;
    const ColliderDebugSceneStatsUVE stats = Build(settings);
    EXPECT_EQ(stats.colliderCount, 4U);
    EXPECT_TRUE(stats.truncated);
    EXPECT_TRUE(stats.reachedColliderLimit);
}

TEST_F(ColliderDebugSceneUVETest, StopsRatherThanDrawingAHalfShape) {
    static_cast<void>(MakeColliderUVE({}));
    static_cast<void>(MakeColliderUVE({5.0F, 0.0F, 0.0F}));

    // Room for one box and four edges of the next: the second must be skipped whole, so the caller
    // never sees a shape with a missing side.
    std::vector<Physics::ColliderDebugLineUVE> small(16U);
    const ColliderDebugSceneStatsUVE stats = BuildColliderDebugSceneUVE(
        entityManager, ColliderDebugSceneSettingsUVE{},
        std::span<Physics::ColliderDebugLineUVE>{small.data(), small.size()});
    EXPECT_EQ(stats.lineCount, 12U);
    EXPECT_EQ(stats.colliderCount, 1U);
    EXPECT_TRUE(stats.truncated);
    EXPECT_FALSE(stats.reachedColliderLimit) << "the buffer, not the entity cap, was the limit";
}

TEST_F(ColliderDebugSceneUVETest, RefusesAnEmptyBufferOrZeroCap) {
    static_cast<void>(MakeColliderUVE({}));
    EXPECT_EQ(BuildColliderDebugSceneUVE(entityManager, ColliderDebugSceneSettingsUVE{}, {}).lineCount, 0U);

    ColliderDebugSceneSettingsUVE settings;
    settings.maximumColliders = 0U;
    const ColliderDebugSceneStatsUVE stats = Build(settings);
    EXPECT_EQ(stats.lineCount, 0U);
    EXPECT_TRUE(stats.reachedColliderLimit);
}

} // namespace UVE::Scene::Tests
