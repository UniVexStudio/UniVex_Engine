// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/world/world_uve.h"

#include <gtest/gtest.h>

#include "uve/memory/heap_allocator_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/scene/components/hierarchy_component_uve.h"
#include "uve/scene/components/world_transform_component_uve.h"

namespace UVE::World::Tests {
namespace {

class WorldUVETest : public ::testing::Test {
protected:
    Memory::HeapAllocatorUVE allocator;
    Events::EventSystemUVE eventSystem;
    WorldUVE world{allocator, eventSystem};
};

TEST_F(WorldUVETest, TickUVE_AdvancesFrameCountAndAccumulatedTime) {
    EXPECT_EQ(world.GetFrameCountUVE(), 0U);
    EXPECT_FLOAT_EQ(world.GetTotalTimeSecondsUVE(), 0.0F);

    world.TickUVE(0.5F);
    EXPECT_EQ(world.GetFrameCountUVE(), 1U);
    EXPECT_FLOAT_EQ(world.GetTotalTimeSecondsUVE(), 0.5F);

    world.TickUVE(0.25F);
    EXPECT_EQ(world.GetFrameCountUVE(), 2U);
    EXPECT_FLOAT_EQ(world.GetTotalTimeSecondsUVE(), 0.75F);
}

TEST_F(WorldUVETest, TickUVE_PropagatesParentWorldTransformToChild) {
    Scene::IEntityManagerUVE& entityManager = world.GetEntityManagerUVE();

    const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
    const Scene::EntityUVE child = entityManager.CreateEntityUVE();

    Scene::SceneGraphUVE sceneGraph;
    sceneGraph.AttachTransformUVE(entityManager, parent,
                                   Scene::TransformComponentUVE{{2.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}});
    sceneGraph.AttachTransformUVE(entityManager, child,
                                   Scene::TransformComponentUVE{{1.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}});
    sceneGraph.SetParentUVE(entityManager, child, parent);

    world.TickUVE(1.0F / 60.0F);

    const auto& childWorldTransform = entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(child);
    EXPECT_FLOAT_EQ(childWorldTransform.worldPosition.x, 3.0F);
}

} // namespace
} // namespace UVE::World::Tests
