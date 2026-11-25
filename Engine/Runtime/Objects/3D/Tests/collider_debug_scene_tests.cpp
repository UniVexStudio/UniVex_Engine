// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "collider_component.h"
#include "disabled_component.h"
#include "physics_object_component.h"
#include "process_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "vector3.h"
#include "memory_manager.h"
#include "collider_debug_scene.h"

namespace UVE::Scene::Tests {
namespace {

/// Room for four capsules and then some: every test below stays well inside it, so a truncation
/// assertion can only come from the setting under test.
constexpr std::size_t kBufferLines = 1024U;

} // namespace

class ColliderDebugSceneTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    std::vector<Physics::ColliderDebugLine> lines{kBufferLines};

    [[nodiscard]] Entity MakeCollider(const Math::Vector3 position,
                                            const ColliderShapeType shape = ColliderShapeType::Box,
                                            const std::uint32_t layer = 1U) {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<TransformComponent>(entity, TransformComponent{});
        WorldTransformComponent world;
        world.worldPosition = position;
        world.dirty = false;
        entityManager.AddComponent<WorldTransformComponent>(entity, world);
        ColliderComponent collider;
        collider.shapeType = shape;
        collider.collisionLayer = layer;
        entityManager.AddComponent<ColliderComponent>(entity, collider);
        return entity;
    }

    [[nodiscard]] ColliderDebugSceneStats Build(const ColliderDebugSceneSettings& settings = {}) {
        return BuildColliderDebugScene(entityManager, settings,
                                          std::span<Physics::ColliderDebugLine>{lines.data(), lines.size()});
    }
};

TEST_F(ColliderDebugSceneTest, DrawsOneWireframePerCollider) {
    static_cast<void>(MakeCollider({0.0F, 0.0F, 0.0F}));
    static_cast<void>(MakeCollider({5.0F, 0.0F, 0.0F}));
    static_cast<void>(MakeCollider({10.0F, 0.0F, 0.0F}));

    const ColliderDebugSceneStats stats = Build();
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

TEST_F(ColliderDebugSceneTest, SkipsCollidersThatAreNotInTheSimulation) {
    static_cast<void>(MakeCollider({}));

    // A collider the author switched off contributes nothing to the simulation, so it must not be
    // drawn: a debug view that over-reports is one an author cannot trust.
    const Entity disabledCollider = MakeCollider({5.0F, 0.0F, 0.0F});
    entityManager.GetComponent<ColliderComponent>(disabledCollider).disabled = true;

    // An entity disabled outright is skipped for the same reason, and one the Process mode has
    // taken out of the physics world is not there to be hit either.
    const Entity disabledEntity = MakeCollider({10.0F, 0.0F, 0.0F});
    entityManager.AddComponent<DisabledComponent>(disabledEntity, DisabledComponent{});

    const Entity removedFromWorld = MakeCollider({15.0F, 0.0F, 0.0F});
    entityManager.AddComponent<PhysicsObjectComponent>(removedFromWorld,
                                                             PhysicsObjectComponent{});
    ProcessComponent stopped;
    stopped.mode = TickMode::Never;
    stopped.resolvedModeInHierarchy = TickMode::Never;
    entityManager.AddComponent<ProcessComponent>(removedFromWorld, stopped);

    const ColliderDebugSceneStats stats = Build();
    EXPECT_EQ(stats.colliderCount, 1U) << "only the live collider is drawn";
    EXPECT_EQ(stats.lineCount, 12U);
}

TEST_F(ColliderDebugSceneTest, IncludeDisabledDrawsTheColliderItself) {
    const Entity disabledCollider = MakeCollider({});
    entityManager.GetComponent<ColliderComponent>(disabledCollider).disabled = true;

    ColliderDebugSceneSettings settings;
    settings.includeDisabled = true;
    // The switch is honoured only while the object is still in the world: `includeDisabled` speaks
    // for the collider's own checkbox, not for a body the Process mode has taken away.
    EXPECT_EQ(Build(settings).colliderCount, 1U);
}

TEST_F(ColliderDebugSceneTest, FiltersByLayerMask) {
    static_cast<void>(MakeCollider({}, ColliderShapeType::Box, 0x1U));
    static_cast<void>(MakeCollider({}, ColliderShapeType::Box, 0x2U));

    ColliderDebugSceneSettings settings;
    settings.layerMask = 0x2U;
    const ColliderDebugSceneStats stats = Build(settings);
    EXPECT_EQ(stats.colliderCount, 1U);

    settings.layerMask = 0U; // zero means "no filter", not "nothing matches"
    EXPECT_EQ(Build(settings).colliderCount, 2U);
}

TEST_F(ColliderDebugSceneTest, StopsAtTheColliderCap) {
    for (int index = 0; index < 10; ++index) {
        static_cast<void>(MakeCollider({static_cast<float>(index) * 3.0F, 0.0F, 0.0F}));
    }
    ColliderDebugSceneSettings settings;
    settings.maximumColliders = 4U;
    const ColliderDebugSceneStats stats = Build(settings);
    EXPECT_EQ(stats.colliderCount, 4U);
    EXPECT_TRUE(stats.truncated);
    EXPECT_TRUE(stats.reachedColliderLimit);
}

TEST_F(ColliderDebugSceneTest, StopsRatherThanDrawingAHalfShape) {
    static_cast<void>(MakeCollider({}));
    static_cast<void>(MakeCollider({5.0F, 0.0F, 0.0F}));

    // Room for one box and four edges of the next: the second must be skipped whole, so the caller
    // never sees a shape with a missing side.
    std::vector<Physics::ColliderDebugLine> small(16U);
    const ColliderDebugSceneStats stats = BuildColliderDebugScene(
        entityManager, ColliderDebugSceneSettings{},
        std::span<Physics::ColliderDebugLine>{small.data(), small.size()});
    EXPECT_EQ(stats.lineCount, 12U);
    EXPECT_EQ(stats.colliderCount, 1U);
    EXPECT_TRUE(stats.truncated);
    EXPECT_FALSE(stats.reachedColliderLimit) << "the buffer, not the entity cap, was the limit";
}

TEST_F(ColliderDebugSceneTest, RefusesAnEmptyBufferOrZeroCap) {
    static_cast<void>(MakeCollider({}));
    EXPECT_EQ(BuildColliderDebugScene(entityManager, ColliderDebugSceneSettings{}, {}).lineCount, 0U);

    ColliderDebugSceneSettings settings;
    settings.maximumColliders = 0U;
    const ColliderDebugSceneStats stats = Build(settings);
    EXPECT_EQ(stats.lineCount, 0U);
    EXPECT_TRUE(stats.reachedColliderLimit);
}

} // namespace UVE::Scene::Tests
