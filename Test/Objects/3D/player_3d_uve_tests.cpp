// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/player_3d_uve.h"

#include "uve/gameplay/pawn_uve.h"

#include <limits>

#include <gtest/gtest.h>

#include "uve/component/camera_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/camera_3d_uve.h"
#include "uve/objects/3d/character_3d_uve.h"
#include "uve/objects/3d/spring_arm_3d_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

class Player3DUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;
};

TEST_F(Player3DUVETest, ResolvePossessedPlayerPrefersPossessOnPlay) {
    const EntityUVE npc = entityManager.CreateEntityUVE();
    ApplyPlayer3DObjectDefinitionUVE(entityManager, npc, Player3DObjectDefinitionUVE{});
    entityManager.GetComponentUVE<PlayerComponentUVE>(npc).possessOnPlay = false;

    const EntityUVE player = entityManager.CreateEntityUVE();
    ApplyPlayer3DObjectDefinitionUVE(entityManager, player, Player3DObjectDefinitionUVE{});

    // Resolution follows possession now: the engine's per-frame fill runs first, like in play.
    MaintainPlayerPossessionUVE(entityManager);
    EXPECT_EQ(ResolvePossessedPlayerUVE(entityManager), player);
}

TEST_F(Player3DUVETest, MaintainFillsSelfPossessionButNeverStealsADirectorsTakeover) {
    const EntityUVE player = entityManager.CreateEntityUVE();
    ApplyPlayer3DObjectDefinitionUVE(entityManager, player, Player3DObjectDefinitionUVE{});
    EXPECT_EQ(FindPawnControllerUVE(entityManager, player), kInvalidEntityUVE);
    EXPECT_EQ(ResolvePossessedPlayerUVE(entityManager), kInvalidEntityUVE);

    MaintainPlayerPossessionUVE(entityManager);
    EXPECT_EQ(FindPawnControllerUVE(entityManager, player), player);
    EXPECT_EQ(ResolvePossessedPlayerUVE(entityManager), player);

    // A Player-kind holder elsewhere still counts as player-driven - and is never stolen back.
    const EntityUVE director = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<ControllerComponentUVE>(director, ControllerComponentUVE{});
    ASSERT_TRUE(PossessControllerUVE(entityManager, director, player));
    MaintainPlayerPossessionUVE(entityManager);
    EXPECT_EQ(FindPawnControllerUVE(entityManager, player), director);
    EXPECT_EQ(ResolvePossessedPlayerUVE(entityManager), player);

    // An AI-kind takeover removes the body from player resolution until it is released.
    ASSERT_TRUE(UnpossessControllerUVE(entityManager, director));
    const EntityUVE aiDirector = entityManager.CreateEntityUVE();
    ControllerComponentUVE aiController;
    aiController.kind = ControllerKindUVE::AI;
    entityManager.AddComponentUVE<ControllerComponentUVE>(aiDirector, aiController);
    ASSERT_TRUE(PossessControllerUVE(entityManager, aiDirector, player));
    MaintainPlayerPossessionUVE(entityManager);
    EXPECT_EQ(FindPawnControllerUVE(entityManager, player), aiDirector);
    EXPECT_EQ(ResolvePossessedPlayerUVE(entityManager), kInvalidEntityUVE);

    ASSERT_TRUE(UnpossessControllerUVE(entityManager, aiDirector));
    MaintainPlayerPossessionUVE(entityManager);
    EXPECT_EQ(FindPawnControllerUVE(entityManager, player), player);
    EXPECT_EQ(ResolvePossessedPlayerUVE(entityManager), player);
}

TEST_F(Player3DUVETest, MaintainSkipsUnflaggedBodiesAndNonPlayerControllers) {
    const EntityUVE npc = entityManager.CreateEntityUVE();
    ApplyPlayer3DObjectDefinitionUVE(entityManager, npc, Player3DObjectDefinitionUVE{});
    entityManager.GetComponentUVE<PlayerComponentUVE>(npc).possessOnPlay = false;

    Player3DObjectDefinitionUVE aiDriven{};
    aiDriven.controllerKind = ControllerKindUVE::AI;
    const EntityUVE aiBody = entityManager.CreateEntityUVE();
    ApplyPlayer3DObjectDefinitionUVE(entityManager, aiBody, aiDriven);

    MaintainPlayerPossessionUVE(entityManager);
    EXPECT_EQ(FindPawnControllerUVE(entityManager, npc), kInvalidEntityUVE);
    EXPECT_EQ(FindPawnControllerUVE(entityManager, aiBody), kInvalidEntityUVE);
    EXPECT_EQ(ResolvePossessedPlayerUVE(entityManager), kInvalidEntityUVE);
}

TEST_F(Player3DUVETest, DefinitionCarriesControllerKindAndRefusesUnknownKinds) {
    EXPECT_TRUE(IsPlayer3DObjectDefinitionValidUVE(Player3DObjectDefinitionUVE{}));
    Player3DObjectDefinitionUVE bad{};
    bad.controllerKind = static_cast<ControllerKindUVE>(7U);
    EXPECT_FALSE(IsPlayer3DObjectDefinitionValidUVE(bad));

    Player3DObjectDefinitionUVE aiDriven{};
    aiDriven.controllerKind = ControllerKindUVE::AI;
    const EntityUVE entity = entityManager.CreateEntityUVE();
    ApplyPlayer3DObjectDefinitionUVE(entityManager, entity, aiDriven);
    ASSERT_TRUE(entityManager.HasComponentUVE<PawnComponentUVE>(entity));
    ASSERT_TRUE(entityManager.HasComponentUVE<ControllerComponentUVE>(entity));
    EXPECT_EQ(entityManager.GetComponentUVE<ControllerComponentUVE>(entity).kind,
              ControllerKindUVE::AI);
}

TEST_F(Player3DUVETest, FaceMoveTurnsForwardWithYaw) {
    Math::QuaternionUVE yaw{};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 1.57079632679F, yaw));
    const Math::Vector3UVE faced = FaceMoveFromLookUVE(yaw, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
    EXPECT_NEAR(faced.x, -1.0F, 1.0e-5F);
    EXPECT_NEAR(faced.z, 0.0F, 1.0e-5F);
}

TEST_F(Player3DUVETest, LookYawsTheBodyAndPitchesAChildCamera) {
    const EntityUVE player = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, player, TransformComponentUVE{});
    ApplyPlayer3DObjectDefinitionUVE(entityManager, player, Player3DObjectDefinitionUVE{});

    const EntityUVE camera = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, camera, TransformComponentUVE{});
    entityManager.AddComponentUVE<CameraComponentUVE>(camera, CameraComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, camera, player);

    EXPECT_EQ(FindPlayerLookTargetUVE(entityManager, player), camera);

    PlayerComponentUVE& state = entityManager.GetComponentUVE<PlayerComponentUVE>(player);
    TransformComponentUVE body = entityManager.GetComponentUVE<TransformComponentUVE>(player);
    TransformComponentUVE look = entityManager.GetComponentUVE<TransformComponentUVE>(camera);
    ApplyPlayerLookUVE(state, body, &look, Math::Vector2UVE{10.0F, 5.0F}, Math::Vector2UVE{}, 0.0F);
    EXPECT_LT(state.pitchDegrees, 0.0F);
    EXPECT_NE(body.localRotation, Math::QuaternionUVE{});
    EXPECT_NE(look.localRotation, Math::QuaternionUVE{});
}

TEST_F(Player3DUVETest, ResolvePlayCharacterFallsBackToTheFirstController) {
    const EntityUVE body = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, body, TransformComponentUVE{});
    ApplyCharacter3DObjectDefinitionUVE(entityManager, body, Character3DObjectDefinitionUVE{});
    EXPECT_EQ(ResolvePossessedPlayerUVE(entityManager), kInvalidEntityUVE);
    EXPECT_EQ(ResolvePlayCharacterUVE(entityManager), body);
}

TEST_F(Player3DUVETest, FindPlayerCameraWalksPastTheSpringArm) {
    const EntityUVE player = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, player, TransformComponentUVE{});
    ApplyPlayer3DObjectDefinitionUVE(entityManager, player, Player3DObjectDefinitionUVE{});

    const EntityUVE springArm = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, springArm, TransformComponentUVE{});
    ApplySpringArm3DObjectDefinitionUVE(entityManager, springArm, SpringArm3DObjectDefinitionUVE{});
    sceneGraph.SetParentUVE(entityManager, springArm, player);

    const EntityUVE camera = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, camera, TransformComponentUVE{});
    entityManager.AddComponentUVE<CameraComponentUVE>(camera, CameraComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, camera, springArm);

    EXPECT_EQ(FindPlayerLookTargetUVE(entityManager, player), springArm);
    EXPECT_EQ(FindPlayerCameraUVE(entityManager, player), camera);

    const EntityUVE other = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, other, TransformComponentUVE{});
    CameraComponentUVE otherCamera{};
    otherCamera.current = true;
    entityManager.AddComponentUVE<CameraComponentUVE>(other, otherCamera);
    MakePlayerCameraCurrentUVE(entityManager, player);
    EXPECT_TRUE(entityManager.GetComponentUVE<CameraComponentUVE>(camera).current);
    EXPECT_FALSE(entityManager.GetComponentUVE<CameraComponentUVE>(other).current);
}

TEST_F(Player3DUVETest, InvalidLookSettingsAreRefused) {
    PlayerComponentUVE bad{};
    bad.minPitchDegrees = 10.0F;
    bad.maxPitchDegrees = -10.0F;
    EXPECT_FALSE(IsPlayer3DObjectComponentValidUVE(bad));
    bad = {};
    bad.lookSensitivity = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsPlayer3DObjectComponentValidUVE(bad));
}

} // namespace
} // namespace UVE::Scene::Tests
