// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "scene_serializer.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "entity_manager.h"
#include "event_system.h"
#include "cinematic.h"
#include "gameplay_attributes.h"
#include "gameplay_tags.h"
#include "status_effects.h"
#include "trigger_volume.h"
#include "pawn.h"
#include "camera_follow_component.h"
#include "memory_manager.h"

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {
namespace {

class GameplaySerializationTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneSerializer serializer;
};

} // namespace

TEST_F(GameplaySerializationTest, Attributes_RoundTripThroughCaptureRestore) {
    const Entity source = entityManager.CreateEntity();
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 100.0F, 70.0F, 2.0F));
    ASSERT_TRUE(AddGameplayAttribute(attributes, "mana", 50.0F, 50.0F));
    entityManager.AddComponent<GameplayAttributesComponent>(source, attributes);

    const std::optional<SceneSnapshot> snapshot =
        serializer.Capture(entityManager, {source}, SceneAssetType::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<Entity> restored = serializer.Restore(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    const GameplayAttributesComponent& revived =
        entityManager.GetComponent<GameplayAttributesComponent>(restored.front());
    EXPECT_EQ(revived, attributes);
}

TEST_F(GameplaySerializationTest, Tags_RoundTripThroughCaptureRestore) {
    const Entity source = entityManager.CreateEntity();
    GameplayTagComponent tags;
    ASSERT_TRUE(AddGameplayTag(tags, "undead"));
    ASSERT_TRUE(AddGameplayTag(tags, "boss"));
    entityManager.AddComponent<GameplayTagComponent>(source, tags);

    const std::optional<SceneSnapshot> snapshot =
        serializer.Capture(entityManager, {source}, SceneAssetType::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<Entity> restored = serializer.Restore(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    const GameplayTagComponent& revived =
        entityManager.GetComponent<GameplayTagComponent>(restored.front());
    EXPECT_EQ(revived, tags);
}

TEST_F(GameplaySerializationTest, StatusEffects_RoundTripThroughCaptureRestore) {
    const Entity source = entityManager.CreateEntity();
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 100.0F, 100.0F));
    StatusEffectsComponent effects;
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStacking::Stack, 3U));
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStacking::Stack, 3U));
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "regen-aura", "mana", -5.0F, 9.5F));
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "might", "stamina", 0.0F, 30.0F,
                                     StatusEffectStacking::Refresh, 1U, 25.0F));
    ASSERT_EQ(effects.effects.size(), 3U);
    ASSERT_EQ(effects.effects[0].stacks, 2U);
    ASSERT_TRUE(effects.effects[2].maxApplied);
    entityManager.AddComponent<StatusEffectsComponent>(source, effects);

    const std::optional<SceneSnapshot> snapshot =
        serializer.Capture(entityManager, {source}, SceneAssetType::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<Entity> restored = serializer.Restore(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    const StatusEffectsComponent& revived =
        entityManager.GetComponent<StatusEffectsComponent>(restored.front());
    EXPECT_EQ(revived, effects);
}


TEST_F(GameplaySerializationTest, PawnController_RoundTripThroughCaptureRestore) {
    const Entity controllerEntity = entityManager.CreateEntity();
    ControllerComponent controller;
    controller.kind = ControllerKind::AI;
    entityManager.AddComponent<ControllerComponent>(controllerEntity, controller);
    const Entity pawnEntity = entityManager.CreateEntity();
    entityManager.AddComponent<PawnComponent>(pawnEntity, PawnComponent{});
    ASSERT_TRUE(PossessController(entityManager, controllerEntity, pawnEntity));
    Gameplay::GameplayInput snapshot;
    snapshot.move = {1.0F, 2.0F, 3.0F};
    snapshot.jumpPressed = true;
    entityManager.GetComponent<PawnComponent>(pawnEntity).input = snapshot;

    const std::optional<SceneSnapshot> captured =
        serializer.Capture(entityManager, {controllerEntity, pawnEntity}, SceneAssetType::Scene);
    ASSERT_TRUE(captured.has_value());
    const std::vector<Entity> restored = serializer.Restore(entityManager, *captured);
    ASSERT_EQ(restored.size(), 2U);
    const Entity revivedController =
        entityManager.HasComponent<ControllerComponent>(restored[0]) ? restored[0] : restored[1];
    const Entity revivedPawn =
        revivedController == restored[0] ? restored[1] : restored[0];
    ASSERT_TRUE(entityManager.HasComponent<PawnComponent>(revivedPawn));
    const ControllerComponent& revivedControllerComp =
        entityManager.GetComponent<ControllerComponent>(revivedController);
    const PawnComponent& revivedPawnComp =
        entityManager.GetComponent<PawnComponent>(revivedPawn);
    EXPECT_EQ(revivedControllerComp.kind, ControllerKind::AI);
    EXPECT_EQ(revivedControllerComp.pawn, revivedPawn);
    EXPECT_EQ(revivedPawnComp.controller, revivedController);
    EXPECT_EQ(revivedPawnComp.input, snapshot);
}

TEST_F(GameplaySerializationTest, PawnController_DropsLinksOutsideTheSavedSet) {
    const Entity controllerEntity = entityManager.CreateEntity();
    entityManager.AddComponent<ControllerComponent>(controllerEntity, ControllerComponent{});
    const Entity pawnEntity = entityManager.CreateEntity();
    entityManager.AddComponent<PawnComponent>(pawnEntity, PawnComponent{});
    ASSERT_TRUE(PossessController(entityManager, controllerEntity, pawnEntity));

    const std::optional<SceneSnapshot> captured =
        serializer.Capture(entityManager, {pawnEntity}, SceneAssetType::Scene);
    ASSERT_TRUE(captured.has_value());
    const std::vector<Entity> restored = serializer.Restore(entityManager, *captured);
    ASSERT_EQ(restored.size(), 1U);
    const PawnComponent& revived =
        entityManager.GetComponent<PawnComponent>(restored.front());
    EXPECT_EQ(revived.controller, kInvalidEntity);
}

TEST_F(GameplaySerializationTest, TriggerVolume_RoundTripThroughCaptureRestore) {
    const Entity source = entityManager.CreateEntity();
    TriggerVolumeComponent volume;
    volume.policy = TriggerFirePolicy::WhileOccupied;
    volume.intervalSeconds = 2.5F;
    volume.cooldownSeconds = 1.0F;
    volume.armed = false;
    volume.firedCount = 3U;
    volume.cooldownRemaining = 0.5F;
    volume.intervalRemaining = 1.5F;
    ASSERT_TRUE(IsTriggerVolumeComponentValid(volume));
    entityManager.AddComponent<TriggerVolumeComponent>(source, volume);

    const std::optional<SceneSnapshot> snapshot =
        serializer.Capture(entityManager, {source}, SceneAssetType::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<Entity> restored = serializer.Restore(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    const TriggerVolumeComponent& revived =
        entityManager.GetComponent<TriggerVolumeComponent>(restored.front());
    EXPECT_EQ(revived, volume);
}

TEST_F(GameplaySerializationTest, Cinematic_RoundTripThroughCaptureRestore) {
    const Entity source = entityManager.CreateEntity();
    const Entity camA = entityManager.CreateEntity();
    const Entity camB = entityManager.CreateEntity();
    const Entity actor = entityManager.CreateEntity();
    CinematicComponent cinematic;
    cinematic.durationSeconds = 10.0;
    cinematic.autoplay = true;
    cinematic.speed = 2.0F;
    cinematic.loopMode = CinematicLoopMode::Loop;
    ASSERT_TRUE(AddCinematicEvent(cinematic, 0.0, "open"));
    ASSERT_TRUE(AddCinematicEvent(cinematic, 5.0, "mid"));
    ASSERT_TRUE(AddCinematicCut(cinematic, 0.0, camA));
    ASSERT_TRUE(AddCinematicCut(cinematic, 5.0, camB));
    ASSERT_TRUE(AddCinematicCameraKey(cinematic, 0.0, Math::Vector3{1.0F, 2.0F, 3.0F},
                                         Math::Quaternion{0.0F, 0.70710678F, 0.0F, 0.70710678F}));
    ASSERT_TRUE(AddCinematicCameraKey(cinematic, 10.0, Math::Vector3{4.0F, 5.0F, 6.0F},
                                         Math::Quaternion{}));
    ASSERT_TRUE(AddCinematicAnimationKey(cinematic, 2.0, actor, Asset::AssetGuid{12345U}));
    ASSERT_TRUE(AddCinematicAudioKey(cinematic, 3.0, "sfx/boom.uvaudio", 0.5F));
    ASSERT_TRUE(AddCinematicAudioKey(
        cinematic, CinematicAudioKey{4.0, "sfx/door.uvaudio", 0.8F, true,
                                        Math::Vector3{1.0F, 2.0F, 3.0F}, 2.0F, 40.0F}));
    entityManager.AddComponent<CinematicComponent>(source, cinematic);

    const std::optional<SceneSnapshot> snapshot =
        serializer.Capture(entityManager, {source, camA, camB, actor}, SceneAssetType::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<Entity> restored = serializer.Restore(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 4U);
    std::optional<Entity> revivedEntity;
    for (const Entity entity : restored) {
        if (entityManager.HasComponent<CinematicComponent>(entity)) {
            revivedEntity = entity;
        }
    }
    ASSERT_TRUE(revivedEntity.has_value());
    const CinematicComponent& revived =
        entityManager.GetComponent<CinematicComponent>(*revivedEntity);
    EXPECT_DOUBLE_EQ(revived.durationSeconds, 10.0);
    EXPECT_TRUE(revived.autoplay);
    EXPECT_FLOAT_EQ(revived.speed, 2.0F);
    EXPECT_EQ(revived.loopMode, CinematicLoopMode::Loop);
    EXPECT_EQ(revived.events, cinematic.events);
    EXPECT_EQ(revived.cameraKeys, cinematic.cameraKeys);
    EXPECT_EQ(revived.audioKeys, cinematic.audioKeys);
    ASSERT_EQ(revived.animationKeys.size(), 1U);
    EXPECT_DOUBLE_EQ(revived.animationKeys.front().timeSeconds, 2.0);
    EXPECT_EQ(revived.animationKeys.front().clip, Asset::AssetGuid{12345U});
    EXPECT_NE(std::find(restored.begin(), restored.end(), revived.animationKeys.front().target),
              restored.end());
    ASSERT_EQ(revived.cuts.size(), 2U);
    EXPECT_DOUBLE_EQ(revived.cuts[0].timeSeconds, 0.0);
    EXPECT_DOUBLE_EQ(revived.cuts[1].timeSeconds, 5.0);
    EXPECT_NE(revived.cuts[0].camera, kInvalidEntity);
    EXPECT_NE(revived.cuts[1].camera, kInvalidEntity);
    EXPECT_NE(revived.cuts[0].camera, revived.cuts[1].camera);
    EXPECT_NE(std::find(restored.begin(), restored.end(), revived.cuts[0].camera), restored.end());
    EXPECT_NE(std::find(restored.begin(), restored.end(), revived.cuts[1].camera), restored.end());
    EXPECT_FALSE(revived.isPlaying);
    EXPECT_FALSE(revived.finished);
    EXPECT_DOUBLE_EQ(revived.currentTimeSeconds, 0.0);
}

TEST_F(GameplaySerializationTest, Cinematic_DropsCutsOutsideTheSavedSet) {
    const Entity source = entityManager.CreateEntity();
    const Entity outsider = entityManager.CreateEntity();
    CinematicComponent cinematic;
    cinematic.durationSeconds = 10.0;
    ASSERT_TRUE(AddCinematicEvent(cinematic, 1.0, "kept"));
    ASSERT_TRUE(AddCinematicCut(cinematic, 1.0, outsider));
    ASSERT_TRUE(AddCinematicAnimationKey(cinematic, 1.0, outsider, Asset::AssetGuid{}));
    entityManager.AddComponent<CinematicComponent>(source, cinematic);

    const std::optional<SceneSnapshot> snapshot =
        serializer.Capture(entityManager, {source}, SceneAssetType::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<Entity> restored = serializer.Restore(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    const CinematicComponent& revived =
        entityManager.GetComponent<CinematicComponent>(restored.front());
    EXPECT_EQ(revived.events, cinematic.events);
    EXPECT_TRUE(revived.cuts.empty());
    EXPECT_TRUE(revived.animationKeys.empty());
    EXPECT_TRUE(IsCinematicComponentValid(revived));
}
TEST_F(GameplaySerializationTest, CameraFollow_RoundTripThroughCaptureRestore) {
    const Entity target = entityManager.CreateEntity();
    const Entity camera = entityManager.CreateEntity();
    CameraFollowComponent follow;
    follow.target = target;
    follow.offset = {0.0F, 5.0F, -8.0F};
    follow.followPossessedPawn = true;
    entityManager.AddComponent<CameraFollowComponent>(camera, follow);

    const std::optional<SceneSnapshot> captured =
        serializer.Capture(entityManager, {camera, target}, SceneAssetType::Scene);
    ASSERT_TRUE(captured.has_value());
    const std::vector<Entity> restored = serializer.Restore(entityManager, *captured);
    ASSERT_EQ(restored.size(), 2U);
    const Entity revivedCamera =
        entityManager.HasComponent<CameraFollowComponent>(restored[0]) ? restored[0] : restored[1];
    const Entity revivedTarget = revivedCamera == restored[0] ? restored[1] : restored[0];
    const CameraFollowComponent& revived =
        entityManager.GetComponent<CameraFollowComponent>(revivedCamera);
    EXPECT_EQ(revived.target, revivedTarget);
    EXPECT_EQ(revived.offset, (Math::Vector3{0.0F, 5.0F, -8.0F}));
    EXPECT_TRUE(revived.followPossessedPawn);
}

TEST_F(GameplaySerializationTest, CameraFollow_DropsTargetOutsideTheSavedSet) {
    const Entity target = entityManager.CreateEntity();
    const Entity camera = entityManager.CreateEntity();
    CameraFollowComponent follow;
    follow.target = target;
    follow.followPossessedPawn = true;
    entityManager.AddComponent<CameraFollowComponent>(camera, follow);

    const std::optional<SceneSnapshot> captured =
        serializer.Capture(entityManager, {camera}, SceneAssetType::Scene);
    ASSERT_TRUE(captured.has_value());
    const std::vector<Entity> restored = serializer.Restore(entityManager, *captured);
    ASSERT_EQ(restored.size(), 1U);
    const CameraFollowComponent& revived =
        entityManager.GetComponent<CameraFollowComponent>(restored.front());
    EXPECT_EQ(revived.target, kInvalidEntity);
    EXPECT_TRUE(revived.followPossessedPawn);
}

} // namespace UVE::Scene::Tests
