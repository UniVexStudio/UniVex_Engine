// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/scene_serializer_uve.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/gameplay/cinematic_uve.h"
#include "uve/gameplay/gameplay_attributes_uve.h"
#include "uve/gameplay/gameplay_tags_uve.h"
#include "uve/gameplay/status_effects_uve.h"
#include "uve/gameplay/trigger_volume_uve.h"
#include "uve/gameplay/pawn_uve.h"
#include "uve/component/camera_follow_component_uve.h"
#include "uve/memory/memory_manager_uve.h"

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {
namespace {

class GameplaySerializationUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneSerializerUVE serializer;
};

} // namespace

TEST_F(GameplaySerializationUVETest, Attributes_RoundTripThroughCaptureRestore) {
    const EntityUVE source = entityManager.CreateEntityUVE();
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 100.0F, 70.0F, 2.0F));
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "mana", 50.0F, 50.0F));
    entityManager.AddComponentUVE<GameplayAttributesComponentUVE>(source, attributes);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {source}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<EntityUVE> restored = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    const GameplayAttributesComponentUVE& revived =
        entityManager.GetComponentUVE<GameplayAttributesComponentUVE>(restored.front());
    EXPECT_EQ(revived, attributes);
}

TEST_F(GameplaySerializationUVETest, Tags_RoundTripThroughCaptureRestore) {
    const EntityUVE source = entityManager.CreateEntityUVE();
    GameplayTagComponentUVE tags;
    ASSERT_TRUE(AddGameplayTagUVE(tags, "undead"));
    ASSERT_TRUE(AddGameplayTagUVE(tags, "boss"));
    entityManager.AddComponentUVE<GameplayTagComponentUVE>(source, tags);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {source}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<EntityUVE> restored = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    const GameplayTagComponentUVE& revived =
        entityManager.GetComponentUVE<GameplayTagComponentUVE>(restored.front());
    EXPECT_EQ(revived, tags);
}

TEST_F(GameplaySerializationUVETest, StatusEffects_RoundTripThroughCaptureRestore) {
    const EntityUVE source = entityManager.CreateEntityUVE();
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 100.0F, 100.0F));
    StatusEffectsComponentUVE effects;
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStackingUVE::Stack, 3U));
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStackingUVE::Stack, 3U));
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "regen-aura", "mana", -5.0F, 9.5F));
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "might", "stamina", 0.0F, 30.0F,
                                     StatusEffectStackingUVE::Refresh, 1U, 25.0F));
    ASSERT_EQ(effects.effects.size(), 3U);
    ASSERT_EQ(effects.effects[0].stacks, 2U);
    ASSERT_TRUE(effects.effects[2].maxApplied);
    entityManager.AddComponentUVE<StatusEffectsComponentUVE>(source, effects);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {source}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<EntityUVE> restored = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    const StatusEffectsComponentUVE& revived =
        entityManager.GetComponentUVE<StatusEffectsComponentUVE>(restored.front());
    EXPECT_EQ(revived, effects);
}


TEST_F(GameplaySerializationUVETest, PawnController_RoundTripThroughCaptureRestore) {
    const EntityUVE controllerEntity = entityManager.CreateEntityUVE();
    ControllerComponentUVE controller;
    controller.kind = ControllerKindUVE::AI;
    entityManager.AddComponentUVE<ControllerComponentUVE>(controllerEntity, controller);
    const EntityUVE pawnEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<PawnComponentUVE>(pawnEntity, PawnComponentUVE{});
    ASSERT_TRUE(PossessControllerUVE(entityManager, controllerEntity, pawnEntity));
    Gameplay::GameplayInputUVE snapshot;
    snapshot.move = {1.0F, 2.0F, 3.0F};
    snapshot.jumpPressed = true;
    entityManager.GetComponentUVE<PawnComponentUVE>(pawnEntity).input = snapshot;

    const std::optional<SceneSnapshotUVE> captured =
        serializer.CaptureUVE(entityManager, {controllerEntity, pawnEntity}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(captured.has_value());
    const std::vector<EntityUVE> restored = serializer.RestoreUVE(entityManager, *captured);
    ASSERT_EQ(restored.size(), 2U);
    const EntityUVE revivedController =
        entityManager.HasComponentUVE<ControllerComponentUVE>(restored[0]) ? restored[0] : restored[1];
    const EntityUVE revivedPawn =
        revivedController == restored[0] ? restored[1] : restored[0];
    ASSERT_TRUE(entityManager.HasComponentUVE<PawnComponentUVE>(revivedPawn));
    const ControllerComponentUVE& revivedControllerComp =
        entityManager.GetComponentUVE<ControllerComponentUVE>(revivedController);
    const PawnComponentUVE& revivedPawnComp =
        entityManager.GetComponentUVE<PawnComponentUVE>(revivedPawn);
    EXPECT_EQ(revivedControllerComp.kind, ControllerKindUVE::AI);
    EXPECT_EQ(revivedControllerComp.pawn, revivedPawn);
    EXPECT_EQ(revivedPawnComp.controller, revivedController);
    EXPECT_EQ(revivedPawnComp.input, snapshot);
}

TEST_F(GameplaySerializationUVETest, PawnController_DropsLinksOutsideTheSavedSet) {
    const EntityUVE controllerEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<ControllerComponentUVE>(controllerEntity, ControllerComponentUVE{});
    const EntityUVE pawnEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<PawnComponentUVE>(pawnEntity, PawnComponentUVE{});
    ASSERT_TRUE(PossessControllerUVE(entityManager, controllerEntity, pawnEntity));

    const std::optional<SceneSnapshotUVE> captured =
        serializer.CaptureUVE(entityManager, {pawnEntity}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(captured.has_value());
    const std::vector<EntityUVE> restored = serializer.RestoreUVE(entityManager, *captured);
    ASSERT_EQ(restored.size(), 1U);
    const PawnComponentUVE& revived =
        entityManager.GetComponentUVE<PawnComponentUVE>(restored.front());
    EXPECT_EQ(revived.controller, kInvalidEntityUVE);
}

TEST_F(GameplaySerializationUVETest, TriggerVolume_RoundTripThroughCaptureRestore) {
    const EntityUVE source = entityManager.CreateEntityUVE();
    TriggerVolumeComponentUVE volume;
    volume.policy = TriggerFirePolicyUVE::WhileOccupied;
    volume.intervalSeconds = 2.5F;
    volume.cooldownSeconds = 1.0F;
    volume.armed = false;
    volume.firedCount = 3U;
    volume.cooldownRemaining = 0.5F;
    volume.intervalRemaining = 1.5F;
    ASSERT_TRUE(IsTriggerVolumeComponentValidUVE(volume));
    entityManager.AddComponentUVE<TriggerVolumeComponentUVE>(source, volume);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {source}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<EntityUVE> restored = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    const TriggerVolumeComponentUVE& revived =
        entityManager.GetComponentUVE<TriggerVolumeComponentUVE>(restored.front());
    EXPECT_EQ(revived, volume);
}

TEST_F(GameplaySerializationUVETest, Cinematic_RoundTripThroughCaptureRestore) {
    const EntityUVE source = entityManager.CreateEntityUVE();
    const EntityUVE camA = entityManager.CreateEntityUVE();
    const EntityUVE camB = entityManager.CreateEntityUVE();
    const EntityUVE actor = entityManager.CreateEntityUVE();
    CinematicComponentUVE cinematic;
    cinematic.durationSeconds = 10.0;
    cinematic.autoplay = true;
    cinematic.speed = 2.0F;
    cinematic.loopMode = CinematicLoopModeUVE::Loop;
    ASSERT_TRUE(AddCinematicEventUVE(cinematic, 0.0, "open"));
    ASSERT_TRUE(AddCinematicEventUVE(cinematic, 5.0, "mid"));
    ASSERT_TRUE(AddCinematicCutUVE(cinematic, 0.0, camA));
    ASSERT_TRUE(AddCinematicCutUVE(cinematic, 5.0, camB));
    ASSERT_TRUE(AddCinematicCameraKeyUVE(cinematic, 0.0, Math::Vector3UVE{1.0F, 2.0F, 3.0F},
                                         Math::QuaternionUVE{0.0F, 0.70710678F, 0.0F, 0.70710678F}));
    ASSERT_TRUE(AddCinematicCameraKeyUVE(cinematic, 10.0, Math::Vector3UVE{4.0F, 5.0F, 6.0F},
                                         Math::QuaternionUVE{}));
    ASSERT_TRUE(AddCinematicAnimationKeyUVE(cinematic, 2.0, actor, Asset::AssetGuidUVE{12345U}));
    ASSERT_TRUE(AddCinematicAudioKeyUVE(cinematic, 3.0, "sfx/boom.uvaudio", 0.5F));
    ASSERT_TRUE(AddCinematicAudioKeyUVE(
        cinematic, CinematicAudioKeyUVE{4.0, "sfx/door.uvaudio", 0.8F, true,
                                        Math::Vector3UVE{1.0F, 2.0F, 3.0F}, 2.0F, 40.0F}));
    entityManager.AddComponentUVE<CinematicComponentUVE>(source, cinematic);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {source, camA, camB, actor}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<EntityUVE> restored = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 4U);
    std::optional<EntityUVE> revivedEntity;
    for (const EntityUVE entity : restored) {
        if (entityManager.HasComponentUVE<CinematicComponentUVE>(entity)) {
            revivedEntity = entity;
        }
    }
    ASSERT_TRUE(revivedEntity.has_value());
    const CinematicComponentUVE& revived =
        entityManager.GetComponentUVE<CinematicComponentUVE>(*revivedEntity);
    EXPECT_DOUBLE_EQ(revived.durationSeconds, 10.0);
    EXPECT_TRUE(revived.autoplay);
    EXPECT_FLOAT_EQ(revived.speed, 2.0F);
    EXPECT_EQ(revived.loopMode, CinematicLoopModeUVE::Loop);
    EXPECT_EQ(revived.events, cinematic.events);
    EXPECT_EQ(revived.cameraKeys, cinematic.cameraKeys);
    EXPECT_EQ(revived.audioKeys, cinematic.audioKeys);
    ASSERT_EQ(revived.animationKeys.size(), 1U);
    EXPECT_DOUBLE_EQ(revived.animationKeys.front().timeSeconds, 2.0);
    EXPECT_EQ(revived.animationKeys.front().clip, Asset::AssetGuidUVE{12345U});
    EXPECT_NE(std::find(restored.begin(), restored.end(), revived.animationKeys.front().target),
              restored.end());
    ASSERT_EQ(revived.cuts.size(), 2U);
    EXPECT_DOUBLE_EQ(revived.cuts[0].timeSeconds, 0.0);
    EXPECT_DOUBLE_EQ(revived.cuts[1].timeSeconds, 5.0);
    EXPECT_NE(revived.cuts[0].camera, kInvalidEntityUVE);
    EXPECT_NE(revived.cuts[1].camera, kInvalidEntityUVE);
    EXPECT_NE(revived.cuts[0].camera, revived.cuts[1].camera);
    EXPECT_NE(std::find(restored.begin(), restored.end(), revived.cuts[0].camera), restored.end());
    EXPECT_NE(std::find(restored.begin(), restored.end(), revived.cuts[1].camera), restored.end());
    EXPECT_FALSE(revived.isPlaying);
    EXPECT_FALSE(revived.finished);
    EXPECT_DOUBLE_EQ(revived.currentTimeSeconds, 0.0);
}

TEST_F(GameplaySerializationUVETest, Cinematic_DropsCutsOutsideTheSavedSet) {
    const EntityUVE source = entityManager.CreateEntityUVE();
    const EntityUVE outsider = entityManager.CreateEntityUVE();
    CinematicComponentUVE cinematic;
    cinematic.durationSeconds = 10.0;
    ASSERT_TRUE(AddCinematicEventUVE(cinematic, 1.0, "kept"));
    ASSERT_TRUE(AddCinematicCutUVE(cinematic, 1.0, outsider));
    ASSERT_TRUE(AddCinematicAnimationKeyUVE(cinematic, 1.0, outsider, Asset::AssetGuidUVE{}));
    entityManager.AddComponentUVE<CinematicComponentUVE>(source, cinematic);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {source}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<EntityUVE> restored = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    const CinematicComponentUVE& revived =
        entityManager.GetComponentUVE<CinematicComponentUVE>(restored.front());
    EXPECT_EQ(revived.events, cinematic.events);
    EXPECT_TRUE(revived.cuts.empty());
    EXPECT_TRUE(revived.animationKeys.empty());
    EXPECT_TRUE(IsCinematicComponentValidUVE(revived));
}
TEST_F(GameplaySerializationUVETest, CameraFollow_RoundTripThroughCaptureRestore) {
    const EntityUVE target = entityManager.CreateEntityUVE();
    const EntityUVE camera = entityManager.CreateEntityUVE();
    CameraFollowComponentUVE follow;
    follow.target = target;
    follow.offset = {0.0F, 5.0F, -8.0F};
    follow.followPossessedPawn = true;
    entityManager.AddComponentUVE<CameraFollowComponentUVE>(camera, follow);

    const std::optional<SceneSnapshotUVE> captured =
        serializer.CaptureUVE(entityManager, {camera, target}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(captured.has_value());
    const std::vector<EntityUVE> restored = serializer.RestoreUVE(entityManager, *captured);
    ASSERT_EQ(restored.size(), 2U);
    const EntityUVE revivedCamera =
        entityManager.HasComponentUVE<CameraFollowComponentUVE>(restored[0]) ? restored[0] : restored[1];
    const EntityUVE revivedTarget = revivedCamera == restored[0] ? restored[1] : restored[0];
    const CameraFollowComponentUVE& revived =
        entityManager.GetComponentUVE<CameraFollowComponentUVE>(revivedCamera);
    EXPECT_EQ(revived.target, revivedTarget);
    EXPECT_EQ(revived.offset, (Math::Vector3UVE{0.0F, 5.0F, -8.0F}));
    EXPECT_TRUE(revived.followPossessedPawn);
}

TEST_F(GameplaySerializationUVETest, CameraFollow_DropsTargetOutsideTheSavedSet) {
    const EntityUVE target = entityManager.CreateEntityUVE();
    const EntityUVE camera = entityManager.CreateEntityUVE();
    CameraFollowComponentUVE follow;
    follow.target = target;
    follow.followPossessedPawn = true;
    entityManager.AddComponentUVE<CameraFollowComponentUVE>(camera, follow);

    const std::optional<SceneSnapshotUVE> captured =
        serializer.CaptureUVE(entityManager, {camera}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(captured.has_value());
    const std::vector<EntityUVE> restored = serializer.RestoreUVE(entityManager, *captured);
    ASSERT_EQ(restored.size(), 1U);
    const CameraFollowComponentUVE& revived =
        entityManager.GetComponentUVE<CameraFollowComponentUVE>(restored.front());
    EXPECT_EQ(revived.target, kInvalidEntityUVE);
    EXPECT_TRUE(revived.followPossessedPawn);
}

} // namespace UVE::Scene::Tests
