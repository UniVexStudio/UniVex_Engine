// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/player_3d_uve.h"

#include <limits>

#include <gtest/gtest.h>

#include "uve/component/camera_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/memory/memory_manager_uve.h"
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

    EXPECT_EQ(ResolvePossessedPlayerUVE(entityManager), player);
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
