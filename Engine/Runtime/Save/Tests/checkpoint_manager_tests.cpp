// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "checkpoint_manager.h"

#include <filesystem>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "event_system.h"
#include "memory_manager.h"
#include "save_game_system.h"
#include "entity_manager.h"
#include "scene_serializer.h"

namespace UVE::Save::Tests {
namespace {

using Scene::EntityManager;
using Scene::Entity;
using Scene::SceneSerializer;

class CheckpointManagerTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneSerializer sceneSerializer;
    std::filesystem::path saveDirectory = "uve_checkpoint_manager_tests_saves";
    SaveGameSystem saveGameSystem{sceneSerializer, saveDirectory};
    CheckpointManager checkpointManager{saveGameSystem, 2.0};

    [[nodiscard]] std::vector<Entity> MakeRootEntities() {
        return {entityManager.CreateEntity()};
    }

    void TearDown() override {
        std::error_code errorCode;
        std::filesystem::remove_all(saveDirectory, errorCode);
    }
};

TEST_F(CheckpointManagerTest, Update_BeforeIntervalElapses_NoSaveWritten) {
    checkpointManager.Update(1.0, entityManager, MakeRootEntities());
    EXPECT_FALSE(saveGameSystem.HasSave(kAutoSaveSlotIndex));
}

TEST_F(CheckpointManagerTest, Checkpoint_FailedSavePreservesElapsedTime) {
    checkpointManager.Update(0.5, entityManager, MakeRootEntities());
    ASSERT_DOUBLE_EQ(checkpointManager.GetElapsedSinceLastSaveSeconds(), 0.5);

    EXPECT_FALSE(checkpointManager.Checkpoint(entityManager, {Scene::kInvalidEntity}));
    EXPECT_DOUBLE_EQ(checkpointManager.GetElapsedSinceLastSaveSeconds(), 0.5);
    EXPECT_FALSE(saveGameSystem.HasSave(kManualCheckpointSlotIndex));
}

TEST_F(CheckpointManagerTest, Update_AtOrAfterIntervalElapses_WritesAutoSaveAndResetsElapsed) {
    checkpointManager.Update(1.0, entityManager, MakeRootEntities());
    checkpointManager.Update(1.5, entityManager, MakeRootEntities());

    EXPECT_TRUE(saveGameSystem.HasSave(kAutoSaveSlotIndex));
    EXPECT_LT(checkpointManager.GetElapsedSinceLastSaveSeconds(), 2.0);
}

TEST_F(CheckpointManagerTest, Update_RejectsInvalidDeltaWithoutChangingCounters) {
    checkpointManager.Update(0.5, entityManager, MakeRootEntities());
    ASSERT_DOUBLE_EQ(checkpointManager.GetElapsedSinceLastSaveSeconds(), 0.5);
    ASSERT_DOUBLE_EQ(checkpointManager.GetTotalPlaytimeSeconds(), 0.5);

    checkpointManager.Update(-1.0, entityManager, MakeRootEntities());
    checkpointManager.Update(std::numeric_limits<double>::quiet_NaN(), entityManager,
                                MakeRootEntities());
    checkpointManager.Update(std::numeric_limits<double>::infinity(), entityManager,
                                MakeRootEntities());

    EXPECT_DOUBLE_EQ(checkpointManager.GetElapsedSinceLastSaveSeconds(), 0.5);
    EXPECT_DOUBLE_EQ(checkpointManager.GetTotalPlaytimeSeconds(), 0.5);
    EXPECT_FALSE(saveGameSystem.HasSave(kAutoSaveSlotIndex));

    checkpointManager.Update(1.5, entityManager, MakeRootEntities());
    EXPECT_TRUE(saveGameSystem.HasSave(kAutoSaveSlotIndex));
}

TEST_F(CheckpointManagerTest, Update_AccumulatesTotalPlaytimeAcrossCalls) {
    checkpointManager.Update(1.0, entityManager, MakeRootEntities());
    checkpointManager.Update(0.5, entityManager, MakeRootEntities());
    EXPECT_DOUBLE_EQ(checkpointManager.GetTotalPlaytimeSeconds(), 1.5);
}

TEST_F(CheckpointManagerTest, Checkpoint_WritesImmediately_RegardlessOfElapsedTime) {
    checkpointManager.Update(0.1, entityManager, MakeRootEntities());
    EXPECT_FALSE(saveGameSystem.HasSave(kAutoSaveSlotIndex));

    EXPECT_TRUE(checkpointManager.Checkpoint(entityManager, MakeRootEntities()));
    EXPECT_FALSE(saveGameSystem.HasSave(kAutoSaveSlotIndex));
    EXPECT_TRUE(saveGameSystem.HasSave(kManualCheckpointSlotIndex));
}

TEST_F(CheckpointManagerTest, Checkpoint_ResetsElapsedTimer_DefersNextAutoSave) {
    ASSERT_TRUE(checkpointManager.Checkpoint(entityManager, MakeRootEntities()));
    EXPECT_TRUE(saveGameSystem.HasSave(kManualCheckpointSlotIndex));
    EXPECT_DOUBLE_EQ(checkpointManager.GetElapsedSinceLastSaveSeconds(), 0.0);

    checkpointManager.Update(1.9, entityManager, MakeRootEntities());
    // Still under the 2.0s interval since the checkpoint reset the timer - no second auto-save.
    EXPECT_LT(checkpointManager.GetElapsedSinceLastSaveSeconds(), 2.0);
}

TEST_F(CheckpointManagerTest, AutoSaveNeverCollidesWithNumberedSlots) {
    const Entity numberedEntity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(7, entityManager, {numberedEntity}, GameStateMetadata{}));

    ASSERT_TRUE(checkpointManager.Checkpoint(entityManager, MakeRootEntities()));

    EXPECT_TRUE(saveGameSystem.HasSave(7));
    EXPECT_FALSE(saveGameSystem.HasSave(kAutoSaveSlotIndex));
    EXPECT_TRUE(saveGameSystem.HasSave(kManualCheckpointSlotIndex));
    EXPECT_EQ(saveGameSystem.ListUsedSlots(), (std::vector<int>{7}));
}

TEST_F(CheckpointManagerTest, InvalidAutoSaveIntervalsPreserveLastValidConfiguration) {
    CheckpointManager invalidConstructionManager{
        saveGameSystem, std::numeric_limits<double>::quiet_NaN()};
    EXPECT_DOUBLE_EQ(invalidConstructionManager.GetAutoSaveIntervalSeconds(), 300.0);

    checkpointManager.Update(0.5, entityManager, MakeRootEntities());
    checkpointManager.SetAutoSaveIntervalSeconds(0.0);
    checkpointManager.SetAutoSaveIntervalSeconds(-1.0);
    checkpointManager.SetAutoSaveIntervalSeconds(std::numeric_limits<double>::quiet_NaN());
    checkpointManager.SetAutoSaveIntervalSeconds(std::numeric_limits<double>::infinity());

    EXPECT_DOUBLE_EQ(checkpointManager.GetAutoSaveIntervalSeconds(), 2.0);
    EXPECT_DOUBLE_EQ(checkpointManager.GetElapsedSinceLastSaveSeconds(), 0.5);

    checkpointManager.SetAutoSaveIntervalSeconds(1.0);
    EXPECT_DOUBLE_EQ(checkpointManager.GetAutoSaveIntervalSeconds(), 1.0);
}

TEST_F(CheckpointManagerTest, SetAutoSaveIntervalSeconds_TakesEffectWithoutResettingElapsed) {
    checkpointManager.Update(1.5, entityManager, MakeRootEntities());
    ASSERT_FALSE(saveGameSystem.HasSave(kAutoSaveSlotIndex));

    checkpointManager.SetAutoSaveIntervalSeconds(1.0);
    EXPECT_DOUBLE_EQ(checkpointManager.GetElapsedSinceLastSaveSeconds(), 1.5);

    // The next Update() call should fire immediately since 1.5s already exceeds the new 1.0s
    // interval, even with a tiny additional delta.
    checkpointManager.Update(0.01, entityManager, MakeRootEntities());
    EXPECT_TRUE(saveGameSystem.HasSave(kAutoSaveSlotIndex));
}

TEST_F(CheckpointManagerTest, AutoSaveAndManualCheckpoint_UseIndependentReservedSlots) {
    checkpointManager.Update(2.0, entityManager, MakeRootEntities());
    ASSERT_TRUE(saveGameSystem.HasSave(kAutoSaveSlotIndex));
    ASSERT_TRUE(checkpointManager.Checkpoint(entityManager, MakeRootEntities()));
    EXPECT_TRUE(saveGameSystem.HasSave(kAutoSaveSlotIndex));
    EXPECT_TRUE(saveGameSystem.HasSave(kManualCheckpointSlotIndex));
    EXPECT_EQ(saveGameSystem.GetSaveMetadata(kAutoSaveSlotIndex)->slotIndex, kAutoSaveSlotIndex);
    EXPECT_EQ(saveGameSystem.GetSaveMetadata(kManualCheckpointSlotIndex)->slotIndex,
              kManualCheckpointSlotIndex);
}

TEST_F(CheckpointManagerTest, GetTotalPlaytimeSeconds_FeedsIntoSavedMetadata) {
    checkpointManager.Update(1.0, entityManager, MakeRootEntities());
    ASSERT_TRUE(checkpointManager.Checkpoint(entityManager, MakeRootEntities()));

    const std::optional<GameStateMetadata> metadata = saveGameSystem.GetSaveMetadata(kManualCheckpointSlotIndex);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_DOUBLE_EQ(metadata->playtimeSeconds, checkpointManager.GetTotalPlaytimeSeconds());
}

TEST_F(CheckpointManagerTest, Checkpoint_PreservesMetadataTemplateFields) {
    GameStateMetadata metadataTemplate;
    metadataTemplate.engineVersionMajor = 4;
    metadataTemplate.engineVersionMinor = 2;
    metadataTemplate.engineVersionPatch = 7;
    metadataTemplate.engineVersionBuild = 19;
    metadataTemplate.saveName = "checkpoint-template";
    CheckpointManager templatedManager{saveGameSystem, 2.0, metadataTemplate};

    ASSERT_TRUE(templatedManager.Checkpoint(entityManager, MakeRootEntities()));
    const std::optional<GameStateMetadata> metadata =
        saveGameSystem.GetSaveMetadata(kManualCheckpointSlotIndex);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->engineVersionMajor, metadataTemplate.engineVersionMajor);
    EXPECT_EQ(metadata->engineVersionMinor, metadataTemplate.engineVersionMinor);
    EXPECT_EQ(metadata->engineVersionPatch, metadataTemplate.engineVersionPatch);
    EXPECT_EQ(metadata->engineVersionBuild, metadataTemplate.engineVersionBuild);
    EXPECT_EQ(metadata->saveName, metadataTemplate.saveName);
}

} // namespace
} // namespace UVE::Save::Tests
