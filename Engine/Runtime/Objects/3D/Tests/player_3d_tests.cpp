// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "player_3d.h"

#include "pawn.h"

#include <limits>

#include <gtest/gtest.h>

#include "camera_component.h"
#include "hierarchy_component.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "quaternion.h"
#include "memory_manager.h"
#include "camera_3d.h"
#include "character_3d.h"
#include "spring_arm_3d.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

class Player3DTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;
};

TEST_F(Player3DTest, ResolvePossessedPlayerPrefersPossessOnPlay) {
    const Entity npc = entityManager.CreateEntity();
    ApplyPlayer3DObjectDefinition(entityManager, npc, Player3DObjectDefinition{});
    entityManager.GetComponent<PlayerComponent>(npc).possessOnPlay = false;

    const Entity player = entityManager.CreateEntity();
    ApplyPlayer3DObjectDefinition(entityManager, player, Player3DObjectDefinition{});

    // Resolution follows possession now: the engine's per-frame fill runs first, like in play.
    MaintainPlayerPossession(entityManager);
    EXPECT_EQ(ResolvePossessedPlayer(entityManager), player);
}

TEST_F(Player3DTest, MaintainFillsSelfPossessionButNeverStealsADirectorsTakeover) {
    const Entity player = entityManager.CreateEntity();
    ApplyPlayer3DObjectDefinition(entityManager, player, Player3DObjectDefinition{});
    EXPECT_EQ(FindPawnController(entityManager, player), kInvalidEntity);
    EXPECT_EQ(ResolvePossessedPlayer(entityManager), kInvalidEntity);

    MaintainPlayerPossession(entityManager);
    EXPECT_EQ(FindPawnController(entityManager, player), player);
    EXPECT_EQ(ResolvePossessedPlayer(entityManager), player);

    // A Player-kind holder elsewhere still counts as player-driven - and is never stolen back.
    const Entity director = entityManager.CreateEntity();
    entityManager.AddComponent<ControllerComponent>(director, ControllerComponent{});
    ASSERT_TRUE(PossessController(entityManager, director, player));
    MaintainPlayerPossession(entityManager);
    EXPECT_EQ(FindPawnController(entityManager, player), director);
    EXPECT_EQ(ResolvePossessedPlayer(entityManager), player);

    // An AI-kind takeover removes the body from player resolution until it is released.
    ASSERT_TRUE(UnpossessController(entityManager, director));
    const Entity aiDirector = entityManager.CreateEntity();
    ControllerComponent aiController;
    aiController.kind = ControllerKind::AI;
    entityManager.AddComponent<ControllerComponent>(aiDirector, aiController);
    ASSERT_TRUE(PossessController(entityManager, aiDirector, player));
    MaintainPlayerPossession(entityManager);
    EXPECT_EQ(FindPawnController(entityManager, player), aiDirector);
    EXPECT_EQ(ResolvePossessedPlayer(entityManager), kInvalidEntity);

    ASSERT_TRUE(UnpossessController(entityManager, aiDirector));
    MaintainPlayerPossession(entityManager);
    EXPECT_EQ(FindPawnController(entityManager, player), player);
    EXPECT_EQ(ResolvePossessedPlayer(entityManager), player);
}

TEST_F(Player3DTest, MaintainSkipsUnflaggedBodiesAndNonPlayerControllers) {
    const Entity npc = entityManager.CreateEntity();
    ApplyPlayer3DObjectDefinition(entityManager, npc, Player3DObjectDefinition{});
    entityManager.GetComponent<PlayerComponent>(npc).possessOnPlay = false;

    Player3DObjectDefinition aiDriven{};
    aiDriven.controllerKind = ControllerKind::AI;
    const Entity aiBody = entityManager.CreateEntity();
    ApplyPlayer3DObjectDefinition(entityManager, aiBody, aiDriven);

    MaintainPlayerPossession(entityManager);
    EXPECT_EQ(FindPawnController(entityManager, npc), kInvalidEntity);
    EXPECT_EQ(FindPawnController(entityManager, aiBody), kInvalidEntity);
    EXPECT_EQ(ResolvePossessedPlayer(entityManager), kInvalidEntity);
}

TEST_F(Player3DTest, DefinitionCarriesControllerKindAndRefusesUnknownKinds) {
    EXPECT_TRUE(IsPlayer3DObjectDefinitionValid(Player3DObjectDefinition{}));
    Player3DObjectDefinition bad{};
    bad.controllerKind = static_cast<ControllerKind>(7U);
    EXPECT_FALSE(IsPlayer3DObjectDefinitionValid(bad));

    Player3DObjectDefinition aiDriven{};
    aiDriven.controllerKind = ControllerKind::AI;
    const Entity entity = entityManager.CreateEntity();
    ApplyPlayer3DObjectDefinition(entityManager, entity, aiDriven);
    ASSERT_TRUE(entityManager.HasComponent<PawnComponent>(entity));
    ASSERT_TRUE(entityManager.HasComponent<ControllerComponent>(entity));
    EXPECT_EQ(entityManager.GetComponent<ControllerComponent>(entity).kind,
              ControllerKind::AI);
}

TEST_F(Player3DTest, FaceMoveTurnsForwardWithYaw) {
    Math::Quaternion yaw{};
    ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F}, 1.57079632679F, yaw));
    const Math::Vector3 faced = FaceMoveFromLook(yaw, Math::Vector3{0.0F, 0.0F, -1.0F});
    EXPECT_NEAR(faced.x, -1.0F, 1.0e-5F);
    EXPECT_NEAR(faced.z, 0.0F, 1.0e-5F);
}

TEST_F(Player3DTest, LookYawsTheBodyAndPitchesAChildCamera) {
    const Entity player = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, player, TransformComponent{});
    ApplyPlayer3DObjectDefinition(entityManager, player, Player3DObjectDefinition{});

    const Entity camera = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, camera, TransformComponent{});
    entityManager.AddComponent<CameraComponent>(camera, CameraComponent{});
    sceneGraph.SetParent(entityManager, camera, player);

    EXPECT_EQ(FindPlayerLookTarget(entityManager, player), camera);

    PlayerComponent& state = entityManager.GetComponent<PlayerComponent>(player);
    TransformComponent body = entityManager.GetComponent<TransformComponent>(player);
    TransformComponent look = entityManager.GetComponent<TransformComponent>(camera);
    ApplyPlayerLook(state, body, &look, Math::Vector2{10.0F, 5.0F}, Math::Vector2{}, 0.0F);
    EXPECT_LT(state.pitchDegrees, 0.0F);
    EXPECT_NE(body.localRotation, Math::Quaternion{});
    EXPECT_NE(look.localRotation, Math::Quaternion{});
}

TEST_F(Player3DTest, ResolvePlayCharacterFallsBackToTheFirstController) {
    const Entity body = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, body, TransformComponent{});
    ApplyCharacter3DObjectDefinition(entityManager, body, Character3DObjectDefinition{});
    EXPECT_EQ(ResolvePossessedPlayer(entityManager), kInvalidEntity);
    EXPECT_EQ(ResolvePlayCharacter(entityManager), body);
}

TEST_F(Player3DTest, ResolvePlayCharacterPrefersABodyMeantForAPlayerOverAnAICrowd) {
    // Earlier in entity order, so it would have won before: an NPC must not steal the play view.
    const Entity npc = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, npc, TransformComponent{});
    ApplyCharacter3DObjectDefinition(entityManager, npc, Character3DObjectDefinition{});
    ASSERT_EQ(entityManager.GetComponent<ControllerComponent>(npc).kind, ControllerKind::AI);

    const Entity hero = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, hero, TransformComponent{});
    Character3DObjectDefinition heroDefinition{};
    heroDefinition.controllerKind = ControllerKind::Player;
    ApplyCharacter3DObjectDefinition(entityManager, hero, heroDefinition);

    EXPECT_EQ(ResolvePlayCharacter(entityManager), hero);
}

TEST_F(Player3DTest, FindPlayerCameraWalksPastTheSpringArm) {
    const Entity player = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, player, TransformComponent{});
    ApplyPlayer3DObjectDefinition(entityManager, player, Player3DObjectDefinition{});

    const Entity springArm = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, springArm, TransformComponent{});
    ApplySpringArm3DObjectDefinition(entityManager, springArm, SpringArm3DObjectDefinition{});
    sceneGraph.SetParent(entityManager, springArm, player);

    const Entity camera = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, camera, TransformComponent{});
    entityManager.AddComponent<CameraComponent>(camera, CameraComponent{});
    sceneGraph.SetParent(entityManager, camera, springArm);

    EXPECT_EQ(FindPlayerLookTarget(entityManager, player), springArm);
    EXPECT_EQ(FindPlayerCamera(entityManager, player), camera);

    const Entity other = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, other, TransformComponent{});
    CameraComponent otherCamera{};
    otherCamera.current = true;
    entityManager.AddComponent<CameraComponent>(other, otherCamera);
    MakePlayerCameraCurrent(entityManager, player);
    EXPECT_TRUE(entityManager.GetComponent<CameraComponent>(camera).current);
    EXPECT_FALSE(entityManager.GetComponent<CameraComponent>(other).current);
}

TEST_F(Player3DTest, InvalidLookSettingsAreRefused) {
    PlayerComponent bad{};
    bad.minPitchDegrees = 10.0F;
    bad.maxPitchDegrees = -10.0F;
    EXPECT_FALSE(IsPlayer3DObjectComponentValid(bad));
    bad = {};
    bad.lookSensitivity = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsPlayer3DObjectComponentValid(bad));
}

} // namespace
} // namespace UVE::Scene::Tests
