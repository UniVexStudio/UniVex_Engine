// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "world.h"

#include <gtest/gtest.h>

#include "heap_allocator.h"
#include "event_system.h"
#include "hierarchy_component.h"
#include "world_transform_component.h"

namespace UVE::World::Tests {
namespace {

class WorldTest : public ::testing::Test {
protected:
    Memory::HeapAllocator allocator;
    Events::EventSystem eventSystem;
    World world{allocator, eventSystem};
};

TEST_F(WorldTest, Tick_AdvancesFrameCountAndAccumulatedTime) {
    EXPECT_EQ(world.GetFrameCount(), 0U);
    EXPECT_FLOAT_EQ(world.GetTotalTimeSeconds(), 0.0F);

    world.Tick(0.5F);
    EXPECT_EQ(world.GetFrameCount(), 1U);
    EXPECT_FLOAT_EQ(world.GetTotalTimeSeconds(), 0.5F);

    world.Tick(0.25F);
    EXPECT_EQ(world.GetFrameCount(), 2U);
    EXPECT_FLOAT_EQ(world.GetTotalTimeSeconds(), 0.75F);
}

TEST_F(WorldTest, Tick_PropagatesParentWorldTransformToChild) {
    UVE::Scene::IEntityManager& entityManager = world.GetEntityManager();

    const UVE::Scene::Entity parent = entityManager.CreateEntity();
    const UVE::Scene::Entity child = entityManager.CreateEntity();

    UVE::Scene::SceneGraph sceneGraph;
    sceneGraph.AttachTransform(entityManager, parent,
                                   UVE::Scene::TransformComponent{{2.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}});
    sceneGraph.AttachTransform(entityManager, child,
                                   UVE::Scene::TransformComponent{{1.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}});
    sceneGraph.SetParent(entityManager, child, parent);

    world.Tick(1.0F / 60.0F);

    const auto& childWorldTransform = entityManager.GetComponent<UVE::Scene::WorldTransformComponent>(child);
    EXPECT_FLOAT_EQ(childWorldTransform.worldPosition.x, 3.0F);
}

} // namespace
} // namespace UVE::World::Tests
