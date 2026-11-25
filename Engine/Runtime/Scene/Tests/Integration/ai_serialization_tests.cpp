// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "scene_serializer.h"

#include <optional>
#include <vector>

#include "perception.h"
#include "utility_ai.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {
namespace {

class AiSerializationTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneSerializer serializer;
};

} // namespace

TEST_F(AiSerializationTest, Brain_RoundTripResetsSelectionState) {
    const Entity source = entityManager.CreateEntity();
    AiBrainComponent brain;
    brain.hysteresis = 0.2F;
    AiAction flee;
    flee.actionId = "flee";
    flee.baseScore = 0.9F;
    flee.considerations.push_back(
        AiConsideration{"health", AiResponseCurve::InverseQuadratic, 2.0F});
    brain.actions.push_back(std::move(flee));
    brain.actions.push_back(AiAction{"patrol", 0.4F, {}});
    brain.currentAction = "flee";
    brain.currentScore = 0.8F;
    entityManager.AddComponent<AiBrainComponent>(source, brain);

    const std::optional<SceneSnapshot> snapshot =
        serializer.Capture(entityManager, {source}, SceneAssetType::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<Entity> restored = serializer.Restore(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    const AiBrainComponent& revived =
        entityManager.GetComponent<AiBrainComponent>(restored.front());
    EXPECT_FLOAT_EQ(revived.hysteresis, 0.2F);
    ASSERT_EQ(revived.actions.size(), 2U);
    EXPECT_EQ(revived.actions, brain.actions);
    EXPECT_TRUE(revived.currentAction.empty());
    EXPECT_FLOAT_EQ(revived.currentScore, 0.0F);
}

TEST_F(AiSerializationTest, Perception_RoundTripThroughCaptureRestore) {
    const Entity source = entityManager.CreateEntity();
    PerceptionComponent sensor;
    sensor.watchedTag = "enemy";
    sensor.sightRangeMetres = 30.0F;
    sensor.sightFieldOfViewDegrees = 120.0F;
    sensor.hearingRadiusMetres = 12.0F;
    sensor.requiresLineOfSight = false;
    entityManager.AddComponent<PerceptionComponent>(source, sensor);

    const std::optional<SceneSnapshot> snapshot =
        serializer.Capture(entityManager, {source}, SceneAssetType::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<Entity> restored = serializer.Restore(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    const PerceptionComponent& revived =
        entityManager.GetComponent<PerceptionComponent>(restored.front());
    EXPECT_EQ(revived, sensor);
}

TEST_F(AiSerializationTest, NoiseEmitter_RoundTripThroughCaptureRestore) {
    const Entity source = entityManager.CreateEntity();
    NoiseEmitterComponent emitter;
    emitter.loudness = 0.6F;
    emitter.decayPerSecond = 0.2F;
    entityManager.AddComponent<NoiseEmitterComponent>(source, emitter);

    const std::optional<SceneSnapshot> snapshot =
        serializer.Capture(entityManager, {source}, SceneAssetType::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<Entity> restored = serializer.Restore(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    const NoiseEmitterComponent& revived =
        entityManager.GetComponent<NoiseEmitterComponent>(restored.front());
    EXPECT_EQ(revived, emitter);
}

TEST_F(AiSerializationTest, BlackboardWithVectors_RoundTripThroughCaptureRestore) {
    const Entity source = entityManager.CreateEntity();
    BlackboardComponent board;
    ASSERT_TRUE(SetBlackboardValue(board, "sight.enemy.visible", 1.0F));
    ASSERT_TRUE(
        SetBlackboardVector(board, "sight.enemy.position", Math::Vector3{1.0F, 2.0F, 3.0F}));
    entityManager.AddComponent<BlackboardComponent>(source, board);

    const std::optional<SceneSnapshot> snapshot =
        serializer.Capture(entityManager, {source}, SceneAssetType::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<Entity> restored = serializer.Restore(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    const BlackboardComponent& revived =
        entityManager.GetComponent<BlackboardComponent>(restored.front());
    EXPECT_EQ(revived, board);
}

TEST_F(AiSerializationTest, Blackboard_RoundTripThroughCaptureRestore) {
    const Entity source = entityManager.CreateEntity();
    BlackboardComponent board;
    ASSERT_TRUE(SetBlackboardValue(board, "health", 0.25F));
    ASSERT_TRUE(SetBlackboardValue(board, "enemyNear", 1.0F));
    entityManager.AddComponent<BlackboardComponent>(source, board);

    const std::optional<SceneSnapshot> snapshot =
        serializer.Capture(entityManager, {source}, SceneAssetType::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<Entity> restored = serializer.Restore(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    const BlackboardComponent& revived =
        entityManager.GetComponent<BlackboardComponent>(restored.front());
    EXPECT_EQ(revived, board);
}

} // namespace UVE::Scene::Tests
