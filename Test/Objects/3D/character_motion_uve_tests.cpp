// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/character_3d_uve.h"

#include <limits>

#include <gtest/gtest.h>

#include "uve/component/transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/gameplay/pawn_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/player_3d_uve.h"

namespace UVE::Scene::Tests {
namespace {

class CharacterMotionUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    EntityUVE MakeCharacterUVE() {
        // The definition brings the transform, the pawn and the controller by itself.
        const EntityUVE entity = entityManager.CreateEntityUVE();
        ApplyCharacter3DObjectDefinitionUVE(entityManager, entity, Character3DObjectDefinitionUVE{});
        return entity;
    }

    EntityUVE MakeControllerUVE(const ControllerKindUVE kind) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        ControllerComponentUVE controller;
        controller.kind = kind;
        entityManager.AddComponentUVE<ControllerComponentUVE>(entity, controller);
        return entity;
    }

    void DrivePawnUVE(const EntityUVE pawn, const Gameplay::GameplayInputUVE& input) {
        entityManager.GetComponentUVE<PawnComponentUVE>(pawn).input = input;
    }

    Gameplay::GameplayInputUVE InputUVE() {
        Gameplay::GameplayInputUVE input;
        input.move = {1.0F, 0.0F, -1.0F};
        input.rise = 0.5F;
        input.jumpPressed = true;
        return input;
    }
};

TEST_F(CharacterMotionUVETest, PlayerPossessionSteersFacedByOwnYaw) {
    const EntityUVE body = MakeCharacterUVE();
    ASSERT_TRUE(
        PossessControllerUVE(entityManager, MakeControllerUVE(ControllerKindUVE::Player), body));
    DrivePawnUVE(body, InputUVE());

    // Identity yaw passes the intent through untouched.
    Physics::CharacterMotionInputUVE straight = ResolveCharacterMotionUVE(entityManager, body);
    EXPECT_EQ(straight.move, InputUVE().move);
    EXPECT_FLOAT_EQ(straight.rise, 0.5F);
    EXPECT_TRUE(straight.jumpPressed);

    // A yawed body steers look-relative, exactly like the player facing says.
    Math::QuaternionUVE yaw{};
    ASSERT_TRUE(
        Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 1.57079632679F, yaw));
    entityManager.GetComponentUVE<TransformComponentUVE>(body).localRotation = yaw;
    const Physics::CharacterMotionInputUVE faced = ResolveCharacterMotionUVE(entityManager, body);
    EXPECT_EQ(faced.move, FaceMoveFromLookUVE(yaw, InputUVE().move));
    EXPECT_NE(faced.move, InputUVE().move);
}

TEST_F(CharacterMotionUVETest, AiPossessionPassesWorldSpaceIntentThroughUnrotated) {
    const EntityUVE body = MakeCharacterUVE();
    ASSERT_TRUE(
        PossessControllerUVE(entityManager, MakeControllerUVE(ControllerKindUVE::AI), body));
    DrivePawnUVE(body, InputUVE());
    Math::QuaternionUVE yaw{};
    ASSERT_TRUE(
        Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 1.57079632679F, yaw));
    entityManager.GetComponentUVE<TransformComponentUVE>(body).localRotation = yaw;

    const Physics::CharacterMotionInputUVE motion = ResolveCharacterMotionUVE(entityManager, body);
    EXPECT_EQ(motion.move, InputUVE().move);
    EXPECT_FLOAT_EQ(motion.rise, 0.5F);
    EXPECT_TRUE(motion.jumpPressed);
}

TEST_F(CharacterMotionUVETest, UnpossessedBodiesStandStill) {
    const Physics::CharacterMotionInputUVE zero{};
    EXPECT_EQ(ResolveCharacterMotionUVE(entityManager, entityManager.CreateEntityUVE()), zero);

    const EntityUVE loner = MakeCharacterUVE();
    DrivePawnUVE(loner, InputUVE());
    EXPECT_EQ(ResolveCharacterMotionUVE(entityManager, loner), zero);

    // A one-sided link is no link.
    const EntityUVE stray = MakeCharacterUVE();
    const EntityUVE holder = MakeControllerUVE(ControllerKindUVE::Player);
    entityManager.GetComponentUVE<PawnComponentUVE>(stray).controller = holder;
    DrivePawnUVE(stray, InputUVE());
    EXPECT_EQ(ResolveCharacterMotionUVE(entityManager, stray), zero);

    // A destroyed driver orphans the body the same way.
    const EntityUVE orphan = MakeCharacterUVE();
    const EntityUVE driver = MakeControllerUVE(ControllerKindUVE::Player);
    ASSERT_TRUE(PossessControllerUVE(entityManager, driver, orphan));
    DrivePawnUVE(orphan, InputUVE());
    ASSERT_NE(ResolveCharacterMotionUVE(entityManager, orphan), zero);
    entityManager.DestroyEntityUVE(driver);
    EXPECT_EQ(ResolveCharacterMotionUVE(entityManager, orphan), zero);
}

TEST_F(CharacterMotionUVETest, NonFiniteInputStandsStill) {
    const EntityUVE body = MakeCharacterUVE();
    ASSERT_TRUE(
        PossessControllerUVE(entityManager, MakeControllerUVE(ControllerKindUVE::Player), body));
    Gameplay::GameplayInputUVE broken = InputUVE();
    broken.move.x = std::numeric_limits<float>::quiet_NaN();
    DrivePawnUVE(body, broken);
    EXPECT_EQ(ResolveCharacterMotionUVE(entityManager, body), Physics::CharacterMotionInputUVE{});

    broken = InputUVE();
    broken.rise = std::numeric_limits<float>::infinity();
    DrivePawnUVE(body, broken);
    EXPECT_EQ(ResolveCharacterMotionUVE(entityManager, body), Physics::CharacterMotionInputUVE{});
}

TEST_F(CharacterMotionUVETest, MissingTransformFallsBackToRawMove) {
    const EntityUVE body = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<PawnComponentUVE>(body, PawnComponentUVE{});
    ASSERT_TRUE(
        PossessControllerUVE(entityManager, MakeControllerUVE(ControllerKindUVE::Player), body));
    DrivePawnUVE(body, InputUVE());

    const Physics::CharacterMotionInputUVE motion = ResolveCharacterMotionUVE(entityManager, body);
    EXPECT_EQ(motion.move, InputUVE().move);
    EXPECT_TRUE(motion.jumpPressed);
}

TEST_F(CharacterMotionUVETest, DefinitionMakesBodiesPossessableWithAiKindByDefault) {
    EXPECT_TRUE(IsCharacter3DObjectDefinitionValidUVE(Character3DObjectDefinitionUVE{}));
    Character3DObjectDefinitionUVE bad{};
    bad.controllerKind = static_cast<ControllerKindUVE>(7U);
    EXPECT_FALSE(IsCharacter3DObjectDefinitionValidUVE(bad));

    const EntityUVE body = MakeCharacterUVE();
    ASSERT_TRUE(entityManager.HasComponentUVE<PawnComponentUVE>(body));
    ASSERT_TRUE(entityManager.HasComponentUVE<ControllerComponentUVE>(body));
    EXPECT_EQ(entityManager.GetComponentUVE<ControllerComponentUVE>(body).kind,
              ControllerKindUVE::AI);
}

} // namespace
} // namespace UVE::Scene::Tests
