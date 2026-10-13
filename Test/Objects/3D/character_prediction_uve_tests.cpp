// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/character_prediction_uve.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include <gtest/gtest.h>

#include "uve/component/character_controller_component_uve.h"
#include "uve/component/predicted_character_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/gameplay/gameplay_input_uve.h"
#include "uve/gameplay/pawn_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/network/network_prediction_uve.h"
#include "uve/network/network_session_uve.h"
#include "uve/network/network_transport_uve.h"
#include "uve/objects/3d/character_3d_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

class CharacterPredictionUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;

    static constexpr float kDeltaTimeUVE = 1.0F / 60.0F;
    static constexpr float kGravityYUVE = -9.81F;

    [[nodiscard]] EntityUVE MakePawnUVE(const Math::Vector3UVE position = {}) {
        // Attached first, defined after: the graph owns the transform triple and the
        // definition fills in around it (collider, controller, pawn) where missing.
        const EntityUVE entity = entityManager.CreateEntityUVE();
        TransformComponentUVE transform;
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        ApplyCharacter3DObjectDefinitionUVE(entityManager, entity,
                                            Character3DObjectDefinitionUVE{});
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    [[nodiscard]] EntityUVE MakeControllerUVE(const ControllerKindUVE kind) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        ControllerComponentUVE controller;
        controller.kind = kind;
        entityManager.AddComponentUVE<ControllerComponentUVE>(entity, controller);
        return entity;
    }

    void DrivePawnUVE(const EntityUVE pawn, const Gameplay::GameplayInputUVE& input) {
        entityManager.GetComponentUVE<PawnComponentUVE>(pawn).input = input;
    }

    [[nodiscard]] Gameplay::GameplayInputUVE InputUVE() {
        Gameplay::GameplayInputUVE input;
        input.move = {1.0F, 0.0F, -1.0F};
        input.rise = 0.5F;
        input.jumpPressed = true;
        return input;
    }

    [[nodiscard]] Math::Vector3UVE WorldPositionUVE(const EntityUVE entity) {
        return entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).worldPosition;
    }

    [[nodiscard]] CharacterControllerComponentUVE ControllerUVE(const EntityUVE entity) {
        return entityManager.GetComponentUVE<CharacterControllerComponentUVE>(entity);
    }
};

struct PredictedPairUVE final {
    Network::LoopbackTransportPairUVE link;
    Network::NetworkPredictionUVE client;
    Network::NetworkPredictionUVE server;

    void UpdateBothUVE(const std::uint64_t nowMs) {
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
    }

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        if (!client.SessionUVE().ConnectUVE(nowMs)) {
            return false;
        }
        UpdateBothUVE(nowMs);
        UpdateBothUVE(nowMs);
        return client.SessionUVE().StateUVE() == Network::SessionStateUVE::Established &&
               server.SessionUVE().StateUVE() == Network::SessionStateUVE::Established;
    }
};

[[nodiscard]] PredictedPairUVE MakePairUVE(Network::PredictionStepperUVE clientStepper,
                                           Network::PredictionStepperUVE serverStepper,
                                           const Network::PredictedStateUVE& initial) {
    Network::LoopbackTransportPairUVE pair = Network::CreateLoopbackTransportPairUVE(256U);
    Network::NetworkSessionUVE sessionA(Network::SessionRoleUVE::Client,
                                        std::make_unique<Network::LoopbackEndpointUVE>(pair.a));
    Network::NetworkSessionUVE sessionB(Network::SessionRoleUVE::Server,
                                        std::make_unique<Network::LoopbackEndpointUVE>(pair.b));
    Network::NetworkPredictionUVE client(std::move(sessionA), Network::SessionRoleUVE::Client,
                                         std::move(clientStepper), initial);
    Network::NetworkPredictionUVE server(std::move(sessionB), Network::SessionRoleUVE::Server,
                                         std::move(serverStepper), initial);
    return PredictedPairUVE{std::move(pair), std::move(client), std::move(server)};
}

TEST_F(CharacterPredictionUVETest, ClocksPackIntoMillisecondHalves) {
    EXPECT_EQ(PackPredictionClocksUVE(0.0F, 0.0F), 0U);
    EXPECT_EQ(PackPredictionClocksUVE(1.5F, 0.5F), (1500U << 16U) | 500U);
    // Truncation, not rounding: 1.9 ms keeps 1.
    EXPECT_EQ(PackPredictionClocksUVE(0.0019F, 0.0F), (1U << 16U));
    // Saturation past 65 seconds, and poison in means zero out.
    EXPECT_EQ(PackPredictionClocksUVE(100.0F, 100.0F), 0xFFFFFFFFU);
    EXPECT_EQ(PackPredictionClocksUVE(-1.0F, -2.0F), 0U);
    EXPECT_EQ(PackPredictionClocksUVE(std::numeric_limits<float>::quiet_NaN(), 0.0F), 0U);
    EXPECT_EQ(PackPredictionClocksUVE(0.0F, std::numeric_limits<float>::infinity()), 0xFFFFU);

    const PredictionClocksUVE clocks = UnpackPredictionClocksUVE((1500U << 16U) | 500U);
    EXPECT_FLOAT_EQ(clocks.coyoteSeconds, 1.5F);
    EXPECT_FLOAT_EQ(clocks.bufferSeconds, 0.5F);
    // Exact-millisecond words round-trip exactly.
    EXPECT_EQ(PackPredictionClocksUVE(clocks.coyoteSeconds, clocks.bufferSeconds),
              (1500U << 16U) | 500U);
}

TEST_F(CharacterPredictionUVETest, PackApplyRoundTripsThroughComponents) {
    const EntityUVE pawn = MakePawnUVE({1.0F, 2.0F, 3.0F});
    CharacterControllerComponentUVE& c =
        entityManager.GetComponentUVE<CharacterControllerComponentUVE>(pawn);
    c.velocity = {4.0F, 5.0F, 6.0F};
    c.grounded = true;
    c.timeSinceOnFloor = 0.15F;
    c.jumpBufferRemaining = 0.05F;

    const std::optional<Network::PredictedStateUVE> packed =
        PackPredictedStateUVE(entityManager, pawn);
    ASSERT_TRUE(packed.has_value());
    EXPECT_EQ(packed->position, (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(packed->velocity, (Math::Vector3UVE{4.0F, 5.0F, 6.0F}));
    EXPECT_TRUE(packed->grounded);
    EXPECT_EQ(packed->appState, PackPredictionClocksUVE(0.15F, 0.05F));

    // Scramble the components, then apply the packed state back over them.
    c.velocity = {};
    c.grounded = false;
    EXPECT_TRUE(ApplyPredictedStateUVE(entityManager, sceneGraph, pawn, *packed));
    EXPECT_EQ(WorldPositionUVE(pawn), (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    const CharacterControllerComponentUVE& back =
        entityManager.GetComponentUVE<CharacterControllerComponentUVE>(pawn);
    EXPECT_EQ(back.velocity, (Math::Vector3UVE{4.0F, 5.0F, 6.0F}));
    EXPECT_TRUE(back.grounded);
    EXPECT_FLOAT_EQ(back.timeSinceOnFloor, 0.15F);
    EXPECT_FLOAT_EQ(back.jumpBufferRemaining, 0.05F);
}

TEST_F(CharacterPredictionUVETest, MissingComponentsFailClosed) {
    Network::PredictedStateUVE state;
    state.position = {1.0F, 2.0F, 3.0F};

    // A bare entity: nothing to pack, nowhere to apply.
    const EntityUVE bare = entityManager.CreateEntityUVE();
    EXPECT_FALSE(PackPredictedStateUVE(entityManager, bare).has_value());
    EXPECT_FALSE(ApplyPredictedStateUVE(entityManager, sceneGraph, bare, state));
    entityManager.DestroyEntityUVE(bare);
    EXPECT_FALSE(PackPredictedStateUVE(entityManager, bare).has_value());
    EXPECT_FALSE(ApplyPredictedStateUVE(entityManager, sceneGraph, bare, state));

    // Defined but never attached: no hierarchy component counts as root, so
    // definition-built pawns pack and apply without ever touching the graph.
    const EntityUVE defined = entityManager.CreateEntityUVE();
    ApplyCharacter3DObjectDefinitionUVE(entityManager, defined,
                                        Character3DObjectDefinitionUVE{});
    EXPECT_TRUE(PackPredictedStateUVE(entityManager, defined).has_value());
    EXPECT_TRUE(ApplyPredictedStateUVE(entityManager, sceneGraph, defined, state));

    // A component removed mid-life refuses precisely: no world, no pack and no apply...
    const EntityUVE noWorld = MakePawnUVE();
    entityManager.RemoveComponentUVE<WorldTransformComponentUVE>(noWorld);
    EXPECT_FALSE(PackPredictedStateUVE(entityManager, noWorld).has_value());
    EXPECT_FALSE(ApplyPredictedStateUVE(entityManager, sceneGraph, noWorld, state));
    // ...no local transform, no apply (but the pack still reads the world)...
    const EntityUVE noLocal = MakePawnUVE();
    entityManager.RemoveComponentUVE<TransformComponentUVE>(noLocal);
    EXPECT_TRUE(PackPredictedStateUVE(entityManager, noLocal).has_value());
    EXPECT_FALSE(ApplyPredictedStateUVE(entityManager, sceneGraph, noLocal, state));
    // ...no controller, neither.
    const EntityUVE noController = MakePawnUVE();
    entityManager.RemoveComponentUVE<CharacterControllerComponentUVE>(noController);
    EXPECT_FALSE(PackPredictedStateUVE(entityManager, noController).has_value());
    EXPECT_FALSE(ApplyPredictedStateUVE(entityManager, sceneGraph, noController, state));

    // Non-finite components never pack; non-finite states never apply, leaving the pose.
    const EntityUVE pawn = MakePawnUVE();
    entityManager.GetComponentUVE<WorldTransformComponentUVE>(pawn).worldPosition.x =
        std::numeric_limits<float>::infinity();
    EXPECT_FALSE(PackPredictedStateUVE(entityManager, pawn).has_value());

    const EntityUVE sound = MakePawnUVE({7.0F, 8.0F, 9.0F});
    Network::PredictedStateUVE poison;
    poison.position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ApplyPredictedStateUVE(entityManager, sceneGraph, sound, poison));
    EXPECT_EQ(WorldPositionUVE(sound), (Math::Vector3UVE{7.0F, 8.0F, 9.0F}));
}

TEST_F(CharacterPredictionUVETest, ParentedBodiesPackButRefuseApply) {
    const EntityUVE parent = MakePawnUVE({10.0F, 0.0F, 0.0F});
    const EntityUVE child = MakePawnUVE({1.0F, 2.0F, 3.0F});
    sceneGraph.SetParentUVE(entityManager, child, parent);
    sceneGraph.UpdateUVE(entityManager);

    // Pack reads the world pose and works parented...
    const std::optional<Network::PredictedStateUVE> packed =
        PackPredictedStateUVE(entityManager, child);
    ASSERT_TRUE(packed.has_value());
    EXPECT_EQ(packed->position, WorldPositionUVE(child));

    // ...but the apply would land a world pose in a local slot, so it refuses instead.
    const Math::Vector3UVE localBefore =
        entityManager.GetComponentUVE<TransformComponentUVE>(child).localPosition;
    EXPECT_FALSE(ApplyPredictedStateUVE(entityManager, sceneGraph, child, *packed));
    EXPECT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(child).localPosition,
              localBefore);
}

TEST_F(CharacterPredictionUVETest, CaptureConvertsResolvedIntent) {
    const Network::PredictedInputUVE zero{};
    // Unpossessed stands still, through the resolver's own fail-closed rules.
    const EntityUVE loner = MakePawnUVE();
    DrivePawnUVE(loner, InputUVE());
    EXPECT_EQ(CapturePredictedInputUVE(entityManager, loner), zero);

    // Possessed, both kinds: the capture is the resolved intent, field for field.
    for (const ControllerKindUVE kind : {ControllerKindUVE::Player, ControllerKindUVE::AI}) {
        const EntityUVE body = MakePawnUVE();
        ASSERT_TRUE(PossessControllerUVE(entityManager, MakeControllerUVE(kind), body));
        DrivePawnUVE(body, InputUVE());
        // A yaw, so the Player leg proves the facing survives the conversion.
        Math::QuaternionUVE yaw{};
        ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F},
                                              1.57079632679F, yaw));
        entityManager.GetComponentUVE<TransformComponentUVE>(body).localRotation = yaw;
        const Physics::CharacterMotionInputUVE resolved =
            ResolveCharacterMotionUVE(entityManager, body);
        const Network::PredictedInputUVE captured = CapturePredictedInputUVE(entityManager, body);
        EXPECT_EQ(captured.move, resolved.move);
        EXPECT_FLOAT_EQ(captured.rise, resolved.rise);
        EXPECT_EQ(captured.jumpPressed, resolved.jumpPressed);
        if (kind == ControllerKindUVE::Player) {
            EXPECT_NE(captured.move, InputUVE().move);
        } else {
            EXPECT_EQ(captured.move, InputUVE().move);
        }
    }

    // Non-finite input stands still.
    const EntityUVE body = MakePawnUVE();
    ASSERT_TRUE(
        PossessControllerUVE(entityManager, MakeControllerUVE(ControllerKindUVE::Player), body));
    Gameplay::GameplayInputUVE broken = InputUVE();
    broken.move.x = std::numeric_limits<float>::quiet_NaN();
    DrivePawnUVE(body, broken);
    EXPECT_EQ(CapturePredictedInputUVE(entityManager, body), zero);
}

TEST_F(CharacterPredictionUVETest, StepperStepsRealIntentDeterministically) {
    const Network::PredictionStepperUVE stepper =
        MakeCharacterPredictionStepperUVE(CharacterControllerComponentUVE{}, kGravityYUVE);

    // Standing still: grounded, no input, nowhere to go, clocks at rest.
    Network::PredictedStateUVE spawn;
    spawn.grounded = true;
    const Network::PredictedStateUVE rest =
        stepper(spawn, Network::PredictedInputUVE{}, kDeltaTimeUVE);
    EXPECT_EQ(rest.position, spawn.position);
    EXPECT_EQ(rest.velocity, Math::Vector3UVE{});
    EXPECT_TRUE(rest.grounded);
    EXPECT_EQ(rest.appState, 0U);

    // A jump: the intent's velocity, the liftoff in the flag, integrated forward.
    Network::PredictedInputUVE jump;
    jump.jumpPressed = true;
    const Network::PredictedStateUVE liftoff = stepper(spawn, jump, kDeltaTimeUVE);
    const float jumpSpeed = std::sqrt(2.0F * 9.81F * 1.5F);
    EXPECT_FLOAT_EQ(liftoff.velocity.y, jumpSpeed);
    EXPECT_FALSE(liftoff.grounded);
    EXPECT_FLOAT_EQ(liftoff.position.y, jumpSpeed * kDeltaTimeUVE);

    // Same arguments, same answer: replay is bitwise.
    EXPECT_EQ(stepper(spawn, jump, kDeltaTimeUVE), liftoff);
    EXPECT_EQ(stepper(spawn, Network::PredictedInputUVE{}, kDeltaTimeUVE), rest);
}

TEST_F(CharacterPredictionUVETest, ClientPredictsThroughComponentsWithZeroReconciles) {
    const EntityUVE pawn = MakePawnUVE();
    ASSERT_TRUE(PossessControllerUVE(entityManager, MakeControllerUVE(ControllerKindUVE::Player),
                                     pawn));
    Gameplay::GameplayInputUVE push;
    push.move = {0.5F, 0.0F, 0.0F};
    DrivePawnUVE(pawn, push);
    entityManager.AddComponentUVE<PredictedCharacterComponentUVE>(pawn,
                                                                 PredictedCharacterComponentUVE{});

    const std::optional<Network::PredictedStateUVE> spawn =
        PackPredictedStateUVE(entityManager, pawn);
    ASSERT_TRUE(spawn.has_value());
    PredictedPairUVE pair = MakePairUVE(
        MakeCharacterPredictionStepperUVE(CharacterControllerComponentUVE{}, kGravityYUVE),
        MakeCharacterPredictionStepperUVE(CharacterControllerComponentUVE{}, kGravityYUVE),
        *spawn);
    ASSERT_TRUE(pair.EstablishUVE(1000U));

    for (std::uint64_t n = 1U; n <= 10U; ++n) {
        EXPECT_EQ(TickPredictedPawnUVE(entityManager, sceneGraph, pawn, pair.client), n);
        // The apply already shows the prediction; the pump and sync only confirm it.
        EXPECT_EQ(WorldPositionUVE(pawn), pair.client.LocalStateUVE().position);
        pair.UpdateBothUVE(1000U);
        pair.UpdateBothUVE(1000U);
        EXPECT_TRUE(SyncPredictedPawnUVE(entityManager, sceneGraph, pawn, pair.client));
        EXPECT_EQ(WorldPositionUVE(pawn), pair.client.LocalStateUVE().position);
        EXPECT_EQ(ControllerUVE(pawn).velocity, pair.client.LocalStateUVE().velocity);
    }
    EXPECT_EQ(pair.client.LastAckedTickUVE(), 10U);
    EXPECT_EQ(pair.server.LastAppliedTickUVE(), 10U);
    EXPECT_EQ(pair.client.StatsUVE().reconciles, 0U);
    EXPECT_EQ(pair.client.LocalStateUVE(), pair.server.LocalStateUVE());
    EXPECT_GT(WorldPositionUVE(pawn).x, 0.0F);
}

TEST_F(CharacterPredictionUVETest, ReconcileSnapsComponentsToServerTruth) {
    const EntityUVE pawn = MakePawnUVE();
    entityManager.GetComponentUVE<CharacterControllerComponentUVE>(pawn).grounded = true;
    ASSERT_TRUE(PossessControllerUVE(entityManager, MakeControllerUVE(ControllerKindUVE::Player),
                                     pawn));
    entityManager.AddComponentUVE<PredictedCharacterComponentUVE>(pawn,
                                                                 PredictedCharacterComponentUVE{});

    const std::optional<Network::PredictedStateUVE> spawn =
        PackPredictedStateUVE(entityManager, pawn);
    ASSERT_TRUE(spawn.has_value());
    ASSERT_TRUE(spawn->grounded);
    Network::PredictionStepperUVE floored =
        [base = MakeCharacterPredictionStepperUVE(CharacterControllerComponentUVE{},
                                                  kGravityYUVE)](const Network::PredictedStateUVE& state,
                                                                 const Network::PredictedInputUVE& input,
                                                                 const float dt) {
            Network::PredictedStateUVE next = base(state, input, dt);
            if (next.position.y <= 0.0F) {
                next.position.y = 0.0F;
                if (next.velocity.y < 0.0F) {
                    next.velocity.y = 0.0F;
                }
                next.grounded = true;
            } else {
                next.grounded = false;
            }
            return next;
        };
    PredictedPairUVE pair = MakePairUVE(
        MakeCharacterPredictionStepperUVE(CharacterControllerComponentUVE{}, kGravityYUVE),
        std::move(floored), *spawn);
    ASSERT_TRUE(pair.EstablishUVE(1000U));

    auto pump = [&]() {
        pair.UpdateBothUVE(1000U);
        pair.UpdateBothUVE(1000U);
        EXPECT_TRUE(SyncPredictedPawnUVE(entityManager, sceneGraph, pawn, pair.client));
    };

    // Four standing ticks: both sides rest grounded at y = 0, nothing to correct.
    const Gameplay::GameplayInputUVE still;
    DrivePawnUVE(pawn, still);
    for (std::uint64_t n = 1U; n <= 4U; ++n) {
        EXPECT_EQ(TickPredictedPawnUVE(entityManager, sceneGraph, pawn, pair.client), n);
        pump();
    }
    EXPECT_EQ(pair.client.StatsUVE().reconciles, 0U);
    EXPECT_EQ(pair.client.LocalStateUVE(), pair.server.LocalStateUVE());

    // The jump: the pawn leaves the floor on the components the same tick it ticks, and the
    // liftoff agrees - the flight only diverges where the floor is.
    Gameplay::GameplayInputUVE jump;
    jump.jumpPressed = true;
    DrivePawnUVE(pawn, jump);
    EXPECT_EQ(TickPredictedPawnUVE(entityManager, sceneGraph, pawn, pair.client), 5U);
    EXPECT_GT(WorldPositionUVE(pawn).y, 0.0F);
    pump();
    EXPECT_EQ(pair.client.StatsUVE().reconciles, 0U);
    EXPECT_EQ(pair.client.LocalStateUVE(), pair.server.LocalStateUVE());

    // The arc and the landing: the client flies through the floor it cannot see, and every
    // server truth past the crossing snaps the components back until both rest together.
    DrivePawnUVE(pawn, still);
    for (std::uint64_t n = 6U; n <= 80U; ++n) {
        EXPECT_EQ(TickPredictedPawnUVE(entityManager, sceneGraph, pawn, pair.client), n);
        pump();
    }
    EXPECT_GT(pair.client.StatsUVE().reconciles, 0U);
    EXPECT_EQ(pair.server.LocalStateUVE().position.y, 0.0F);
    EXPECT_TRUE(pair.server.LocalStateUVE().grounded);
    EXPECT_EQ(pair.client.LocalStateUVE(), pair.server.LocalStateUVE());
    EXPECT_EQ(WorldPositionUVE(pawn).y, 0.0F);
    EXPECT_TRUE(ControllerUVE(pawn).grounded);
}

TEST_F(CharacterPredictionUVETest, ServerSyncMirrorsAuthorityOntoItsOwnPawn) {
    const EntityUVE clientPawn = MakePawnUVE();
    ASSERT_TRUE(PossessControllerUVE(
        entityManager, MakeControllerUVE(ControllerKindUVE::Player), clientPawn));
    Gameplay::GameplayInputUVE push;
    push.move = {1.0F, 0.0F, 0.0F};
    DrivePawnUVE(clientPawn, push);

    const EntityUVE serverPawn = MakePawnUVE();
    entityManager.AddComponentUVE<PredictedCharacterComponentUVE>(
        serverPawn, PredictedCharacterComponentUVE{});

    const std::optional<Network::PredictedStateUVE> spawn =
        PackPredictedStateUVE(entityManager, clientPawn);
    ASSERT_TRUE(spawn.has_value());
    PredictedPairUVE pair = MakePairUVE(
        MakeCharacterPredictionStepperUVE(CharacterControllerComponentUVE{}, kGravityYUVE),
        MakeCharacterPredictionStepperUVE(CharacterControllerComponentUVE{}, kGravityYUVE),
        *spawn);
    ASSERT_TRUE(pair.EstablishUVE(1000U));

    for (std::uint64_t n = 1U; n <= 5U; ++n) {
        EXPECT_EQ(TickPredictedPawnUVE(entityManager, sceneGraph, clientPawn, pair.client), n);
        pair.UpdateBothUVE(1000U);
        pair.UpdateBothUVE(1000U);
        EXPECT_TRUE(SyncPredictedPawnUVE(entityManager, sceneGraph, serverPawn, pair.server));
        EXPECT_EQ(WorldPositionUVE(serverPawn), pair.server.LocalStateUVE().position);
        EXPECT_EQ(ControllerUVE(serverPawn).velocity, pair.server.LocalStateUVE().velocity);
    }
    EXPECT_EQ(pair.server.LastAppliedTickUVE(), 5U);
    EXPECT_EQ(WorldPositionUVE(serverPawn), WorldPositionUVE(clientPawn));
}

TEST_F(CharacterPredictionUVETest, TickStaysDenseWhenComponentsAreGone) {
    const EntityUVE pawn = MakePawnUVE();
    ASSERT_TRUE(PossessControllerUVE(entityManager, MakeControllerUVE(ControllerKindUVE::Player),
                                     pawn));
    DrivePawnUVE(pawn, InputUVE());

    const std::optional<Network::PredictedStateUVE> spawn =
        PackPredictedStateUVE(entityManager, pawn);
    ASSERT_TRUE(spawn.has_value());
    PredictedPairUVE pair = MakePairUVE(
        MakeCharacterPredictionStepperUVE(CharacterControllerComponentUVE{}, kGravityYUVE),
        MakeCharacterPredictionStepperUVE(CharacterControllerComponentUVE{}, kGravityYUVE),
        *spawn);
    ASSERT_TRUE(pair.EstablishUVE(1000U));

    EXPECT_EQ(TickPredictedPawnUVE(entityManager, sceneGraph, pawn, pair.client), 1U);

    // The controller goes missing mid-stream: the tick still issues while the sync refuses,
    // so the server never waits on a gap for an entity problem.
    entityManager.RemoveComponentUVE<CharacterControllerComponentUVE>(pawn);
    EXPECT_EQ(TickPredictedPawnUVE(entityManager, sceneGraph, pawn, pair.client), 2U);
    EXPECT_FALSE(SyncPredictedPawnUVE(entityManager, sceneGraph, pawn, pair.client));

    entityManager.AddComponentUVE<CharacterControllerComponentUVE>(
        pawn, CharacterControllerComponentUVE{});
    EXPECT_TRUE(SyncPredictedPawnUVE(entityManager, sceneGraph, pawn, pair.client));
    EXPECT_EQ(WorldPositionUVE(pawn), pair.client.LocalStateUVE().position);

    pair.UpdateBothUVE(1000U);
    pair.UpdateBothUVE(1000U);
    EXPECT_EQ(pair.server.LastAppliedTickUVE(), 2U);
    EXPECT_EQ(pair.client.StatsUVE().reconciles, 0U);
}


TEST_F(CharacterPredictionUVETest, TwoPawns_TickAndSyncPerPawnThroughOnePredictor) {
    const EntityUVE pawnA = MakePawnUVE();
    const EntityUVE pawnB = MakePawnUVE(Math::Vector3UVE{10.0F, 0.0F, 0.0F});
    ASSERT_TRUE(PossessControllerUVE(entityManager, MakeControllerUVE(ControllerKindUVE::Player),
                                     pawnA));
    ASSERT_TRUE(PossessControllerUVE(entityManager, MakeControllerUVE(ControllerKindUVE::Player),
                                     pawnB));
    Gameplay::GameplayInputUVE pushA;
    pushA.move = {0.5F, 0.0F, 0.0F};
    DrivePawnUVE(pawnA, pushA);
    Gameplay::GameplayInputUVE pushB;
    pushB.move = {-0.5F, 0.0F, 0.0F};
    DrivePawnUVE(pawnB, pushB);
    entityManager.AddComponentUVE<PredictedCharacterComponentUVE>(pawnA,
                                                                 PredictedCharacterComponentUVE{});
    entityManager.AddComponentUVE<PredictedCharacterComponentUVE>(pawnB,
                                                                 PredictedCharacterComponentUVE{});

    const std::optional<Network::PredictedStateUVE> spawnA =
        PackPredictedStateUVE(entityManager, pawnA);
    const std::optional<Network::PredictedStateUVE> spawnB =
        PackPredictedStateUVE(entityManager, pawnB);
    ASSERT_TRUE(spawnA.has_value());
    ASSERT_TRUE(spawnB.has_value());
    PredictedPairUVE pair = MakePairUVE(
        MakeCharacterPredictionStepperUVE(CharacterControllerComponentUVE{}, kGravityYUVE),
        MakeCharacterPredictionStepperUVE(CharacterControllerComponentUVE{}, kGravityYUVE),
        *spawnA);
    ASSERT_TRUE(pair.client.SeedPawnUVE(1U, *spawnB));
    ASSERT_TRUE(pair.server.SeedPawnUVE(1U, *spawnB));
    ASSERT_TRUE(pair.EstablishUVE(1000U));

    for (std::uint64_t n = 1U; n <= 5U; ++n) {
        EXPECT_EQ(TickPredictedPawnUVE(entityManager, sceneGraph, pawnA, pair.client), n);
        EXPECT_EQ(TickPredictedPawnUVE(entityManager, sceneGraph, pawnB, pair.client, 1U), n);
        pair.UpdateBothUVE(1000U);
        pair.UpdateBothUVE(1000U);
        EXPECT_TRUE(SyncPredictedPawnUVE(entityManager, sceneGraph, pawnA, pair.client));
        EXPECT_TRUE(SyncPredictedPawnUVE(entityManager, sceneGraph, pawnB, pair.client, 1U));
        EXPECT_EQ(WorldPositionUVE(pawnA), pair.client.LocalStateUVE(0U).position);
        EXPECT_EQ(WorldPositionUVE(pawnB), pair.client.LocalStateUVE(1U).position);
    }
    EXPECT_EQ(pair.client.LastAckedTickUVE(0U), 5U);
    EXPECT_EQ(pair.client.LastAckedTickUVE(1U), 5U);
    EXPECT_EQ(pair.server.LastAppliedTickUVE(0U), 5U);
    EXPECT_EQ(pair.server.LastAppliedTickUVE(1U), 5U);
    EXPECT_EQ(pair.client.StatsUVE().reconciles, 0U);
    EXPECT_GT(WorldPositionUVE(pawnA).x, 0.0F);
    EXPECT_LT(WorldPositionUVE(pawnB).x, 10.0F);
}

TEST_F(CharacterPredictionUVETest, ServerSyncMirrorsPerPawnAuthorityOntoItsOwnPawns) {
    const EntityUVE pawnA = MakePawnUVE();
    const EntityUVE pawnB = MakePawnUVE(Math::Vector3UVE{10.0F, 0.0F, 0.0F});
    ASSERT_TRUE(PossessControllerUVE(entityManager, MakeControllerUVE(ControllerKindUVE::Player),
                                     pawnA));
    ASSERT_TRUE(PossessControllerUVE(entityManager, MakeControllerUVE(ControllerKindUVE::Player),
                                     pawnB));
    Gameplay::GameplayInputUVE pushA;
    pushA.move = {0.5F, 0.0F, 0.0F};
    DrivePawnUVE(pawnA, pushA);
    Gameplay::GameplayInputUVE pushB;
    pushB.move = {-0.5F, 0.0F, 0.0F};
    DrivePawnUVE(pawnB, pushB);
    entityManager.AddComponentUVE<PredictedCharacterComponentUVE>(pawnA,
                                                                 PredictedCharacterComponentUVE{});
    entityManager.AddComponentUVE<PredictedCharacterComponentUVE>(pawnB,
                                                                 PredictedCharacterComponentUVE{});

    const std::optional<Network::PredictedStateUVE> spawnA =
        PackPredictedStateUVE(entityManager, pawnA);
    const std::optional<Network::PredictedStateUVE> spawnB =
        PackPredictedStateUVE(entityManager, pawnB);
    ASSERT_TRUE(spawnA.has_value());
    ASSERT_TRUE(spawnB.has_value());
    PredictedPairUVE pair = MakePairUVE(
        MakeCharacterPredictionStepperUVE(CharacterControllerComponentUVE{}, kGravityYUVE),
        MakeCharacterPredictionStepperUVE(CharacterControllerComponentUVE{}, kGravityYUVE),
        *spawnA);
    ASSERT_TRUE(pair.client.SeedPawnUVE(1U, *spawnB));
    ASSERT_TRUE(pair.server.SeedPawnUVE(1U, *spawnB));
    ASSERT_TRUE(pair.EstablishUVE(1000U));

    for (std::uint64_t n = 1U; n <= 5U; ++n) {
        EXPECT_EQ(TickPredictedPawnUVE(entityManager, sceneGraph, pawnA, pair.client), n);
        EXPECT_EQ(TickPredictedPawnUVE(entityManager, sceneGraph, pawnB, pair.client, 1U), n);
        pair.UpdateBothUVE(1000U);
        pair.UpdateBothUVE(1000U);
    }
    EXPECT_EQ(pair.client.StatsUVE().reconciles, 0U);

    // The server's own two pawns mirror whatever authority holds, one sync per pawn.
    const EntityUVE serverA = MakePawnUVE();
    const EntityUVE serverB = MakePawnUVE();
    EXPECT_TRUE(SyncPredictedPawnUVE(entityManager, sceneGraph, serverA, pair.server));
    EXPECT_TRUE(SyncPredictedPawnUVE(entityManager, sceneGraph, serverB, pair.server, 1U));
    EXPECT_EQ(WorldPositionUVE(serverA), pair.server.LocalStateUVE().position);
    EXPECT_EQ(WorldPositionUVE(serverB), pair.server.LocalStateUVE(1U).position);
    EXPECT_EQ(WorldPositionUVE(serverB), WorldPositionUVE(pawnB));
}

} // namespace
} // namespace UVE::Scene::Tests
