// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "entity_command_buffer.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"

namespace UVE::Scene::Tests {
namespace {

struct Health final {
    int hitPoints = 0;
};

struct Label final {
    std::string text;
};

class EntityCommandBufferTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
};

TEST_F(EntityCommandBufferTest, DeferredCreate_PlaysBackWithPayloadsAttached) {
    EntityCommandBuffer commands;
    EXPECT_TRUE(commands.IsEmpty());
    const DeferredEntity deferred = commands.CreateEntity();
    commands.AddComponent<Health>(deferred, 42);
    commands.AddComponent<Label>(deferred, Label{"spawned"});
    EXPECT_EQ(commands.GetCommandCount(), 3U);
    // Nothing applied yet: playback is what creates.
    EXPECT_EQ(entityManager.GetEntityCount(), 0U);
    commands.Playback(entityManager);
    EXPECT_TRUE(commands.IsEmpty());
    const Entity entity = commands.Resolve(deferred);
    EXPECT_TRUE(entityManager.IsAlive(entity));
    EXPECT_EQ(entityManager.GetComponent<Health>(entity).hitPoints, 42);
    EXPECT_EQ(entityManager.GetComponent<Label>(entity).text, "spawned");
}

TEST_F(EntityCommandBufferTest, RecordDuringForEach_StructuralChangesApplyAfterTheLoop) {
    for (int i = 0; i < 4; ++i) {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<Health>(entity, i % 2 == 0 ? 0 : 10);
    }
    EntityCommandBuffer commands;
    entityManager.ForEach<Health>([&commands](const Entity entity, const Health& health) {
        if (health.hitPoints == 0) {
            commands.DestroyEntity(entity); // Deferred: the loop keeps iterating safely.
        } else {
            commands.RemoveComponent<Health>(entity);
        }
    });
    EXPECT_EQ(commands.GetCommandCount(), 4U);
    commands.Playback(entityManager);
    EXPECT_EQ(entityManager.GetEntityCount(), 2U);
    entityManager.ForEach<Health>(
        [](const Entity, const Health&) { FAIL() << "All Health components were removed."; });
}

TEST_F(EntityCommandBufferTest, AddToLiveEntity_StagesPayloadByValue) {
    const Entity entity = entityManager.CreateEntity();
    EntityCommandBuffer commands;
    Label staged{"original"};
    commands.AddComponent<Label>(entity, staged);
    staged.text = "mutated-after-record"; // Must not leak into the staged payload.
    commands.Playback(entityManager);
    EXPECT_EQ(entityManager.GetComponent<Label>(entity).text, "original");
}

TEST_F(EntityCommandBufferTest, CreateThenDestroy_NetsNothingAlive) {
    EntityCommandBuffer commands;
    const DeferredEntity deferred = commands.CreateEntity();
    commands.DestroyEntity(deferred);
    commands.Playback(entityManager);
    EXPECT_EQ(entityManager.GetEntityCount(), 0U);
    // The mapping still resolves (to a now-dead handle): resolve-after-playback is unconditional.
    EXPECT_FALSE(entityManager.IsAlive(commands.Resolve(deferred)));
}

TEST_F(EntityCommandBufferTest, Clear_DiscardsWithoutRunning) {
    const Entity entity = entityManager.CreateEntity();
    EntityCommandBuffer commands;
    commands.DestroyEntity(entity);
    commands.CreateEntity();
    commands.Clear();
    EXPECT_TRUE(commands.IsEmpty());
    commands.Playback(entityManager); // Empty playback is a safe no-op.
    EXPECT_TRUE(entityManager.IsAlive(entity));
    EXPECT_EQ(entityManager.GetEntityCount(), 1U);
}

} // namespace
} // namespace UVE::Scene::Tests
