// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/entity/entity_command_buffer_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"

namespace UVE::Scene::Tests {
namespace {

struct HealthUVE final {
    int hitPoints = 0;
};

struct LabelUVE final {
    std::string text;
};

class EntityCommandBufferUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
};

TEST_F(EntityCommandBufferUVETest, DeferredCreate_PlaysBackWithPayloadsAttached) {
    EntityCommandBufferUVE commands;
    EXPECT_TRUE(commands.IsEmptyUVE());
    const DeferredEntityUVE deferred = commands.CreateEntityUVE();
    commands.AddComponentUVE<HealthUVE>(deferred, 42);
    commands.AddComponentUVE<LabelUVE>(deferred, LabelUVE{"spawned"});
    EXPECT_EQ(commands.GetCommandCountUVE(), 3U);
    // Nothing applied yet: playback is what creates.
    EXPECT_EQ(entityManager.GetEntityCountUVE(), 0U);
    commands.PlaybackUVE(entityManager);
    EXPECT_TRUE(commands.IsEmptyUVE());
    const EntityUVE entity = commands.ResolveUVE(deferred);
    EXPECT_TRUE(entityManager.IsAliveUVE(entity));
    EXPECT_EQ(entityManager.GetComponentUVE<HealthUVE>(entity).hitPoints, 42);
    EXPECT_EQ(entityManager.GetComponentUVE<LabelUVE>(entity).text, "spawned");
}

TEST_F(EntityCommandBufferUVETest, RecordDuringForEach_StructuralChangesApplyAfterTheLoop) {
    for (int i = 0; i < 4; ++i) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<HealthUVE>(entity, i % 2 == 0 ? 0 : 10);
    }
    EntityCommandBufferUVE commands;
    entityManager.ForEachUVE<HealthUVE>([&commands](const EntityUVE entity, const HealthUVE& health) {
        if (health.hitPoints == 0) {
            commands.DestroyEntityUVE(entity); // Deferred: the loop keeps iterating safely.
        } else {
            commands.RemoveComponentUVE<HealthUVE>(entity);
        }
    });
    EXPECT_EQ(commands.GetCommandCountUVE(), 4U);
    commands.PlaybackUVE(entityManager);
    EXPECT_EQ(entityManager.GetEntityCountUVE(), 2U);
    entityManager.ForEachUVE<HealthUVE>(
        [](const EntityUVE, const HealthUVE&) { FAIL() << "All HealthUVE components were removed."; });
}

TEST_F(EntityCommandBufferUVETest, AddToLiveEntity_StagesPayloadByValue) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    EntityCommandBufferUVE commands;
    LabelUVE staged{"original"};
    commands.AddComponentUVE<LabelUVE>(entity, staged);
    staged.text = "mutated-after-record"; // Must not leak into the staged payload.
    commands.PlaybackUVE(entityManager);
    EXPECT_EQ(entityManager.GetComponentUVE<LabelUVE>(entity).text, "original");
}

TEST_F(EntityCommandBufferUVETest, CreateThenDestroy_NetsNothingAlive) {
    EntityCommandBufferUVE commands;
    const DeferredEntityUVE deferred = commands.CreateEntityUVE();
    commands.DestroyEntityUVE(deferred);
    commands.PlaybackUVE(entityManager);
    EXPECT_EQ(entityManager.GetEntityCountUVE(), 0U);
    // The mapping still resolves (to a now-dead handle): resolve-after-playback is unconditional.
    EXPECT_FALSE(entityManager.IsAliveUVE(commands.ResolveUVE(deferred)));
}

TEST_F(EntityCommandBufferUVETest, Clear_DiscardsWithoutRunning) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    EntityCommandBufferUVE commands;
    commands.DestroyEntityUVE(entity);
    commands.CreateEntityUVE();
    commands.ClearUVE();
    EXPECT_TRUE(commands.IsEmptyUVE());
    commands.PlaybackUVE(entityManager); // Empty playback is a safe no-op.
    EXPECT_TRUE(entityManager.IsAliveUVE(entity));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), 1U);
}

} // namespace
} // namespace UVE::Scene::Tests
