// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "character_3d.h"

#include <limits>

#include <gtest/gtest.h>

#include "transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "pawn.h"
#include "quaternion.h"
#include "memory_manager.h"
#include "player_3d.h"

namespace UVE::Scene::Tests {
namespace {

class CharacterMotionTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};

    Entity MakeCharacter() {
        // The definition brings the transform, the pawn and the controller by itself.
        const Entity entity = entityManager.CreateEntity();
        // A moving body needs the shape its controller sweeps; the definition no longer carries
        // one by default, because the editor leaves that choice to the author.
        ApplyCharacter3DObjectDefinition(entityManager, entity,
                                            Character3DObjectDefinition::WithDefaultCollider());
        return entity;
    }

    Entity MakeController(const ControllerKind kind) {
        const Entity entity = entityManager.CreateEntity();
        ControllerComponent controller;
        controller.kind = kind;
        entityManager.AddComponent<ControllerComponent>(entity, controller);
        return entity;
    }

    void DrivePawn(const Entity pawn, const Gameplay::GameplayInput& input) {
        entityManager.GetComponent<PawnComponent>(pawn).input = input;
    }

    Gameplay::GameplayInput Input() {
        Gameplay::GameplayInput input;
        input.move = {1.0F, 0.0F, -1.0F};
        input.rise = 0.5F;
        input.jumpPressed = true;
        return input;
    }
};

TEST_F(CharacterMotionTest, PlayerPossessionSteersFacedByOwnYaw) {
    const Entity body = MakeCharacter();
    ASSERT_TRUE(
        PossessController(entityManager, MakeController(ControllerKind::Player), body));
    DrivePawn(body, Input());

    // Identity yaw passes the intent through untouched.
    Physics::CharacterMotionInput straight = ResolveCharacterMotion(entityManager, body);
    EXPECT_EQ(straight.move, Input().move);
    EXPECT_FLOAT_EQ(straight.rise, 0.5F);
    EXPECT_TRUE(straight.jumpPressed);

    // A yawed body steers look-relative, exactly like the player facing says.
    Math::Quaternion yaw{};
    ASSERT_TRUE(
        Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F}, 1.57079632679F, yaw));
    entityManager.GetComponent<TransformComponent>(body).localRotation = yaw;
    const Physics::CharacterMotionInput faced = ResolveCharacterMotion(entityManager, body);
    EXPECT_EQ(faced.move, FaceMoveFromLook(yaw, Input().move));
    EXPECT_NE(faced.move, Input().move);
}

TEST_F(CharacterMotionTest, AiPossessionPassesWorldSpaceIntentThroughUnrotated) {
    const Entity body = MakeCharacter();
    ASSERT_TRUE(
        PossessController(entityManager, MakeController(ControllerKind::AI), body));
    DrivePawn(body, Input());
    Math::Quaternion yaw{};
    ASSERT_TRUE(
        Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F}, 1.57079632679F, yaw));
    entityManager.GetComponent<TransformComponent>(body).localRotation = yaw;

    const Physics::CharacterMotionInput motion = ResolveCharacterMotion(entityManager, body);
    EXPECT_EQ(motion.move, Input().move);
    EXPECT_FLOAT_EQ(motion.rise, 0.5F);
    EXPECT_TRUE(motion.jumpPressed);
}

TEST_F(CharacterMotionTest, UnpossessedBodiesStandStill) {
    const Physics::CharacterMotionInput zero{};
    EXPECT_EQ(ResolveCharacterMotion(entityManager, entityManager.CreateEntity()), zero);

    const Entity loner = MakeCharacter();
    DrivePawn(loner, Input());
    EXPECT_EQ(ResolveCharacterMotion(entityManager, loner), zero);

    // A one-sided link is no link.
    const Entity stray = MakeCharacter();
    const Entity holder = MakeController(ControllerKind::Player);
    entityManager.GetComponent<PawnComponent>(stray).controller = holder;
    DrivePawn(stray, Input());
    EXPECT_EQ(ResolveCharacterMotion(entityManager, stray), zero);

    // A destroyed driver orphans the body the same way.
    const Entity orphan = MakeCharacter();
    const Entity driver = MakeController(ControllerKind::Player);
    ASSERT_TRUE(PossessController(entityManager, driver, orphan));
    DrivePawn(orphan, Input());
    ASSERT_NE(ResolveCharacterMotion(entityManager, orphan), zero);
    entityManager.DestroyEntity(driver);
    EXPECT_EQ(ResolveCharacterMotion(entityManager, orphan), zero);
}

TEST_F(CharacterMotionTest, NonFiniteInputStandsStill) {
    const Entity body = MakeCharacter();
    ASSERT_TRUE(
        PossessController(entityManager, MakeController(ControllerKind::Player), body));
    Gameplay::GameplayInput broken = Input();
    broken.move.x = std::numeric_limits<float>::quiet_NaN();
    DrivePawn(body, broken);
    EXPECT_EQ(ResolveCharacterMotion(entityManager, body), Physics::CharacterMotionInput{});

    broken = Input();
    broken.rise = std::numeric_limits<float>::infinity();
    DrivePawn(body, broken);
    EXPECT_EQ(ResolveCharacterMotion(entityManager, body), Physics::CharacterMotionInput{});
}

TEST_F(CharacterMotionTest, MissingTransformFallsBackToRawMove) {
    const Entity body = entityManager.CreateEntity();
    entityManager.AddComponent<PawnComponent>(body, PawnComponent{});
    ASSERT_TRUE(
        PossessController(entityManager, MakeController(ControllerKind::Player), body));
    DrivePawn(body, Input());

    const Physics::CharacterMotionInput motion = ResolveCharacterMotion(entityManager, body);
    EXPECT_EQ(motion.move, Input().move);
    EXPECT_TRUE(motion.jumpPressed);
}

TEST_F(CharacterMotionTest, DefinitionMakesBodiesPossessableWithAiKindByDefault) {
    EXPECT_TRUE(IsCharacter3DObjectDefinitionValid(Character3DObjectDefinition{}));
    Character3DObjectDefinition bad{};
    bad.controllerKind = static_cast<ControllerKind>(7U);
    EXPECT_FALSE(IsCharacter3DObjectDefinitionValid(bad));

    const Entity body = MakeCharacter();
    ASSERT_TRUE(entityManager.HasComponent<PawnComponent>(body));
    ASSERT_TRUE(entityManager.HasComponent<ControllerComponent>(body));
    EXPECT_EQ(entityManager.GetComponent<ControllerComponent>(body).kind,
              ControllerKind::AI);
}

} // namespace
} // namespace UVE::Scene::Tests
