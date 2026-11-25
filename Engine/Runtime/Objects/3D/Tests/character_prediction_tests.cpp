// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "character_prediction.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include <gtest/gtest.h>

#include "character_controller_component.h"
#include "predicted_character_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "gameplay_input.h"
#include "pawn.h"
#include "quaternion.h"
#include "vector3.h"
#include "memory_manager.h"
#include "network_prediction.h"
#include "network_session.h"
#include "network_transport.h"
#include "character_3d.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

class CharacterPredictionTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;

    static constexpr float kDeltaTime = 1.0F / 60.0F;
    static constexpr float kGravityY = -9.81F;

    [[nodiscard]] Entity MakePawn(const Math::Vector3 position = {}) {
        // Attached first, defined after: the graph owns the transform triple and the
        // definition fills in around it (collider, controller, pawn) where missing.
        const Entity entity = entityManager.CreateEntity();
        TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        ApplyCharacter3DObjectDefinition(entityManager, entity,
                                            Character3DObjectDefinition::WithDefaultCollider());
        sceneGraph.Update(entityManager);
        return entity;
    }

    [[nodiscard]] Entity MakeController(const ControllerKind kind) {
        const Entity entity = entityManager.CreateEntity();
        ControllerComponent controller;
        controller.kind = kind;
        entityManager.AddComponent<ControllerComponent>(entity, controller);
        return entity;
    }

    void DrivePawn(const Entity pawn, const Gameplay::GameplayInput& input) {
        entityManager.GetComponent<PawnComponent>(pawn).input = input;
    }

    [[nodiscard]] Gameplay::GameplayInput Input() {
        Gameplay::GameplayInput input;
        input.move = {1.0F, 0.0F, -1.0F};
        input.rise = 0.5F;
        input.jumpPressed = true;
        return input;
    }

    [[nodiscard]] Math::Vector3 WorldPosition(const Entity entity) {
        return entityManager.GetComponent<WorldTransformComponent>(entity).worldPosition;
    }

    [[nodiscard]] CharacterControllerComponent Controller(const Entity entity) {
        return entityManager.GetComponent<CharacterControllerComponent>(entity);
    }
};

struct PredictedPair final {
    Network::LoopbackTransportPair link;
    Network::NetworkPrediction client;
    Network::NetworkPrediction server;

    void UpdateBoth(const std::uint64_t nowMs) {
        client.Update(nowMs);
        server.Update(nowMs);
    }

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        if (!client.Session().Connect(nowMs)) {
            return false;
        }
        UpdateBoth(nowMs);
        UpdateBoth(nowMs);
        return client.Session().State() == Network::SessionState::Established &&
               server.Session().State() == Network::SessionState::Established;
    }
};

[[nodiscard]] PredictedPair MakePair(Network::PredictionStepper clientStepper,
                                           Network::PredictionStepper serverStepper,
                                           const Network::PredictedState& initial) {
    Network::LoopbackTransportPair pair = Network::CreateLoopbackTransportPair(256U);
    Network::NetworkSession sessionA(Network::SessionRole::Client,
                                        std::make_unique<Network::LoopbackEndpoint>(pair.a));
    Network::NetworkSession sessionB(Network::SessionRole::Server,
                                        std::make_unique<Network::LoopbackEndpoint>(pair.b));
    Network::NetworkPrediction client(std::move(sessionA), Network::SessionRole::Client,
                                         std::move(clientStepper), initial);
    Network::NetworkPrediction server(std::move(sessionB), Network::SessionRole::Server,
                                         std::move(serverStepper), initial);
    return PredictedPair{std::move(pair), std::move(client), std::move(server)};
}

TEST_F(CharacterPredictionTest, ClocksPackIntoMillisecondHalves) {
    EXPECT_EQ(PackPredictionClocks(0.0F, 0.0F), 0U);
    EXPECT_EQ(PackPredictionClocks(1.5F, 0.5F), (1500U << 16U) | 500U);
    // Truncation, not rounding: 1.9 ms keeps 1.
    EXPECT_EQ(PackPredictionClocks(0.0019F, 0.0F), (1U << 16U));
    // Saturation past 65 seconds, and poison in means zero out.
    EXPECT_EQ(PackPredictionClocks(100.0F, 100.0F), 0xFFFFFFFFU);
    EXPECT_EQ(PackPredictionClocks(-1.0F, -2.0F), 0U);
    EXPECT_EQ(PackPredictionClocks(std::numeric_limits<float>::quiet_NaN(), 0.0F), 0U);
    EXPECT_EQ(PackPredictionClocks(0.0F, std::numeric_limits<float>::infinity()), 0xFFFFU);

    const PredictionClocks clocks = UnpackPredictionClocks((1500U << 16U) | 500U);
    EXPECT_FLOAT_EQ(clocks.coyoteSeconds, 1.5F);
    EXPECT_FLOAT_EQ(clocks.bufferSeconds, 0.5F);
    // Exact-millisecond words round-trip exactly.
    EXPECT_EQ(PackPredictionClocks(clocks.coyoteSeconds, clocks.bufferSeconds),
              (1500U << 16U) | 500U);
}

TEST_F(CharacterPredictionTest, PackApplyRoundTripsThroughComponents) {
    const Entity pawn = MakePawn({1.0F, 2.0F, 3.0F});
    CharacterControllerComponent& c =
        entityManager.GetComponent<CharacterControllerComponent>(pawn);
    c.velocity = {4.0F, 5.0F, 6.0F};
    c.grounded = true;
    c.timeSinceOnFloor = 0.15F;
    c.jumpBufferRemaining = 0.05F;

    const std::optional<Network::PredictedState> packed =
        PackPredictedState(entityManager, pawn);
    ASSERT_TRUE(packed.has_value());
    EXPECT_EQ(packed->position, (Math::Vector3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(packed->velocity, (Math::Vector3{4.0F, 5.0F, 6.0F}));
    EXPECT_TRUE(packed->grounded);
    EXPECT_EQ(packed->appState, PackPredictionClocks(0.15F, 0.05F));

    // Scramble the components, then apply the packed state back over them.
    c.velocity = {};
    c.grounded = false;
    EXPECT_TRUE(ApplyPredictedState(entityManager, sceneGraph, pawn, *packed));
    EXPECT_EQ(WorldPosition(pawn), (Math::Vector3{1.0F, 2.0F, 3.0F}));
    const CharacterControllerComponent& back =
        entityManager.GetComponent<CharacterControllerComponent>(pawn);
    EXPECT_EQ(back.velocity, (Math::Vector3{4.0F, 5.0F, 6.0F}));
    EXPECT_TRUE(back.grounded);
    EXPECT_FLOAT_EQ(back.timeSinceOnFloor, 0.15F);
    EXPECT_FLOAT_EQ(back.jumpBufferRemaining, 0.05F);
}

TEST_F(CharacterPredictionTest, MissingComponentsFailClosed) {
    Network::PredictedState state;
    state.position = {1.0F, 2.0F, 3.0F};

    // A bare entity: nothing to pack, nowhere to apply.
    const Entity bare = entityManager.CreateEntity();
    EXPECT_FALSE(PackPredictedState(entityManager, bare).has_value());
    EXPECT_FALSE(ApplyPredictedState(entityManager, sceneGraph, bare, state));
    entityManager.DestroyEntity(bare);
    EXPECT_FALSE(PackPredictedState(entityManager, bare).has_value());
    EXPECT_FALSE(ApplyPredictedState(entityManager, sceneGraph, bare, state));

    // Defined but never attached: no hierarchy component counts as root, so
    // definition-built pawns pack and apply without ever touching the graph.
    const Entity defined = entityManager.CreateEntity();
    ApplyCharacter3DObjectDefinition(entityManager, defined,
                                        Character3DObjectDefinition::WithDefaultCollider());
    EXPECT_TRUE(PackPredictedState(entityManager, defined).has_value());
    EXPECT_TRUE(ApplyPredictedState(entityManager, sceneGraph, defined, state));

    // A component removed mid-life refuses precisely: no world, no pack and no apply...
    const Entity noWorld = MakePawn();
    entityManager.RemoveComponent<WorldTransformComponent>(noWorld);
    EXPECT_FALSE(PackPredictedState(entityManager, noWorld).has_value());
    EXPECT_FALSE(ApplyPredictedState(entityManager, sceneGraph, noWorld, state));
    // ...no local transform, no apply (but the pack still reads the world)...
    const Entity noLocal = MakePawn();
    entityManager.RemoveComponent<TransformComponent>(noLocal);
    EXPECT_TRUE(PackPredictedState(entityManager, noLocal).has_value());
    EXPECT_FALSE(ApplyPredictedState(entityManager, sceneGraph, noLocal, state));
    // ...no controller, neither.
    const Entity noController = MakePawn();
    entityManager.RemoveComponent<CharacterControllerComponent>(noController);
    EXPECT_FALSE(PackPredictedState(entityManager, noController).has_value());
    EXPECT_FALSE(ApplyPredictedState(entityManager, sceneGraph, noController, state));

    // Non-finite components never pack; non-finite states never apply, leaving the pose.
    const Entity pawn = MakePawn();
    // Construct a malformed cache fixture; ordinary live cache access is read-only.
    auto invalidWorld = entityManager.GetComponent<WorldTransformComponent>(pawn);
    invalidWorld.worldPosition.x = std::numeric_limits<float>::infinity();
    entityManager.RemoveComponent<WorldTransformComponent>(pawn);
    entityManager.AddComponent<WorldTransformComponent>(pawn, invalidWorld);
    EXPECT_FALSE(PackPredictedState(entityManager, pawn).has_value());

    const Entity sound = MakePawn({7.0F, 8.0F, 9.0F});
    Network::PredictedState poison;
    poison.position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ApplyPredictedState(entityManager, sceneGraph, sound, poison));
    EXPECT_EQ(WorldPosition(sound), (Math::Vector3{7.0F, 8.0F, 9.0F}));
}

TEST_F(CharacterPredictionTest, ParentedBodiesPackButRefuseApply) {
    const Entity parent = MakePawn({10.0F, 0.0F, 0.0F});
    const Entity child = MakePawn({1.0F, 2.0F, 3.0F});
    sceneGraph.SetParent(entityManager, child, parent);
    sceneGraph.Update(entityManager);

    // Pack reads the world pose and works parented...
    const std::optional<Network::PredictedState> packed =
        PackPredictedState(entityManager, child);
    ASSERT_TRUE(packed.has_value());
    EXPECT_EQ(packed->position, WorldPosition(child));

    // ...but the apply would land a world pose in a local slot, so it refuses instead.
    const Math::Vector3 localBefore =
        entityManager.GetComponent<TransformComponent>(child).localPosition;
    EXPECT_FALSE(ApplyPredictedState(entityManager, sceneGraph, child, *packed));
    EXPECT_EQ(entityManager.GetComponent<TransformComponent>(child).localPosition,
              localBefore);
}

TEST_F(CharacterPredictionTest, CaptureConvertsResolvedIntent) {
    const Network::PredictedInput zero{};
    // Unpossessed stands still, through the resolver's own fail-closed rules.
    const Entity loner = MakePawn();
    DrivePawn(loner, Input());
    EXPECT_EQ(CapturePredictedInput(entityManager, loner), zero);

    // Possessed, both kinds: the capture is the resolved intent, field for field.
    for (const ControllerKind kind : {ControllerKind::Player, ControllerKind::AI}) {
        const Entity body = MakePawn();
        ASSERT_TRUE(PossessController(entityManager, MakeController(kind), body));
        DrivePawn(body, Input());
        // A yaw, so the Player leg proves the facing survives the conversion.
        Math::Quaternion yaw{};
        ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F},
                                              1.57079632679F, yaw));
        entityManager.GetComponent<TransformComponent>(body).localRotation = yaw;
        const Physics::CharacterMotionInput resolved =
            ResolveCharacterMotion(entityManager, body);
        const Network::PredictedInput captured = CapturePredictedInput(entityManager, body);
        EXPECT_EQ(captured.move, resolved.move);
        EXPECT_FLOAT_EQ(captured.rise, resolved.rise);
        EXPECT_EQ(captured.jumpPressed, resolved.jumpPressed);
        if (kind == ControllerKind::Player) {
            EXPECT_NE(captured.move, Input().move);
        } else {
            EXPECT_EQ(captured.move, Input().move);
        }
    }

    // Non-finite input stands still.
    const Entity body = MakePawn();
    ASSERT_TRUE(
        PossessController(entityManager, MakeController(ControllerKind::Player), body));
    Gameplay::GameplayInput broken = Input();
    broken.move.x = std::numeric_limits<float>::quiet_NaN();
    DrivePawn(body, broken);
    EXPECT_EQ(CapturePredictedInput(entityManager, body), zero);
}

TEST_F(CharacterPredictionTest, StepperStepsRealIntentDeterministically) {
    const Network::PredictionStepper stepper =
        MakeCharacterPredictionStepper(CharacterControllerComponent{}, kGravityY);

    // Standing still: grounded, no input, nowhere to go, clocks at rest.
    Network::PredictedState spawn;
    spawn.grounded = true;
    const Network::PredictedState rest =
        stepper(spawn, Network::PredictedInput{}, kDeltaTime);
    EXPECT_EQ(rest.position, spawn.position);
    EXPECT_EQ(rest.velocity, Math::Vector3{});
    EXPECT_TRUE(rest.grounded);
    EXPECT_EQ(rest.appState, 0U);

    // A jump: the intent's velocity, the liftoff in the flag, integrated forward.
    Network::PredictedInput jump;
    jump.jumpPressed = true;
    const Network::PredictedState liftoff = stepper(spawn, jump, kDeltaTime);
    const float jumpSpeed = std::sqrt(2.0F * 9.81F * 1.5F);
    EXPECT_FLOAT_EQ(liftoff.velocity.y, jumpSpeed);
    EXPECT_FALSE(liftoff.grounded);
    EXPECT_FLOAT_EQ(liftoff.position.y, jumpSpeed * kDeltaTime);

    // Same arguments, same answer: replay is bitwise.
    EXPECT_EQ(stepper(spawn, jump, kDeltaTime), liftoff);
    EXPECT_EQ(stepper(spawn, Network::PredictedInput{}, kDeltaTime), rest);
}

TEST_F(CharacterPredictionTest, ClientPredictsThroughComponentsWithZeroReconciles) {
    const Entity pawn = MakePawn();
    ASSERT_TRUE(PossessController(entityManager, MakeController(ControllerKind::Player),
                                     pawn));
    Gameplay::GameplayInput push;
    push.move = {0.5F, 0.0F, 0.0F};
    DrivePawn(pawn, push);
    entityManager.AddComponent<PredictedCharacterComponent>(pawn,
                                                                 PredictedCharacterComponent{});

    const std::optional<Network::PredictedState> spawn =
        PackPredictedState(entityManager, pawn);
    ASSERT_TRUE(spawn.has_value());
    PredictedPair pair = MakePair(
        MakeCharacterPredictionStepper(CharacterControllerComponent{}, kGravityY),
        MakeCharacterPredictionStepper(CharacterControllerComponent{}, kGravityY),
        *spawn);
    ASSERT_TRUE(pair.Establish(1000U));

    for (std::uint64_t n = 1U; n <= 10U; ++n) {
        EXPECT_EQ(TickPredictedPawn(entityManager, sceneGraph, pawn, pair.client), n);
        // The apply already shows the prediction; the pump and sync only confirm it.
        EXPECT_EQ(WorldPosition(pawn), pair.client.LocalState().position);
        pair.UpdateBoth(1000U);
        pair.UpdateBoth(1000U);
        EXPECT_TRUE(SyncPredictedPawn(entityManager, sceneGraph, pawn, pair.client));
        EXPECT_EQ(WorldPosition(pawn), pair.client.LocalState().position);
        EXPECT_EQ(Controller(pawn).velocity, pair.client.LocalState().velocity);
    }
    EXPECT_EQ(pair.client.LastAckedTick(), 10U);
    EXPECT_EQ(pair.server.LastAppliedTick(), 10U);
    EXPECT_EQ(pair.client.Stats().reconciles, 0U);
    EXPECT_EQ(pair.client.LocalState(), pair.server.LocalState());
    EXPECT_GT(WorldPosition(pawn).x, 0.0F);
}

TEST_F(CharacterPredictionTest, ReconcileSnapsComponentsToServerTruth) {
    const Entity pawn = MakePawn();
    entityManager.GetComponent<CharacterControllerComponent>(pawn).grounded = true;
    ASSERT_TRUE(PossessController(entityManager, MakeController(ControllerKind::Player),
                                     pawn));
    entityManager.AddComponent<PredictedCharacterComponent>(pawn,
                                                                 PredictedCharacterComponent{});

    const std::optional<Network::PredictedState> spawn =
        PackPredictedState(entityManager, pawn);
    ASSERT_TRUE(spawn.has_value());
    ASSERT_TRUE(spawn->grounded);
    Network::PredictionStepper floored =
        [base = MakeCharacterPredictionStepper(CharacterControllerComponent{},
                                                  kGravityY)](const Network::PredictedState& state,
                                                                 const Network::PredictedInput& input,
                                                                 const float dt) {
            Network::PredictedState next = base(state, input, dt);
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
    PredictedPair pair = MakePair(
        MakeCharacterPredictionStepper(CharacterControllerComponent{}, kGravityY),
        std::move(floored), *spawn);
    ASSERT_TRUE(pair.Establish(1000U));

    auto pump = [&]() {
        pair.UpdateBoth(1000U);
        pair.UpdateBoth(1000U);
        EXPECT_TRUE(SyncPredictedPawn(entityManager, sceneGraph, pawn, pair.client));
    };

    // Four standing ticks: both sides rest grounded at y = 0, nothing to correct.
    const Gameplay::GameplayInput still;
    DrivePawn(pawn, still);
    for (std::uint64_t n = 1U; n <= 4U; ++n) {
        EXPECT_EQ(TickPredictedPawn(entityManager, sceneGraph, pawn, pair.client), n);
        pump();
    }
    EXPECT_EQ(pair.client.Stats().reconciles, 0U);
    EXPECT_EQ(pair.client.LocalState(), pair.server.LocalState());

    // The jump: the pawn leaves the floor on the components the same tick it ticks, and the
    // liftoff agrees - the flight only diverges where the floor is.
    Gameplay::GameplayInput jump;
    jump.jumpPressed = true;
    DrivePawn(pawn, jump);
    EXPECT_EQ(TickPredictedPawn(entityManager, sceneGraph, pawn, pair.client), 5U);
    EXPECT_GT(WorldPosition(pawn).y, 0.0F);
    pump();
    EXPECT_EQ(pair.client.Stats().reconciles, 0U);
    EXPECT_EQ(pair.client.LocalState(), pair.server.LocalState());

    // The arc and the landing: the client flies through the floor it cannot see, and every
    // server truth past the crossing snaps the components back until both rest together.
    DrivePawn(pawn, still);
    for (std::uint64_t n = 6U; n <= 80U; ++n) {
        EXPECT_EQ(TickPredictedPawn(entityManager, sceneGraph, pawn, pair.client), n);
        pump();
    }
    EXPECT_GT(pair.client.Stats().reconciles, 0U);
    EXPECT_EQ(pair.server.LocalState().position.y, 0.0F);
    EXPECT_TRUE(pair.server.LocalState().grounded);
    EXPECT_EQ(pair.client.LocalState(), pair.server.LocalState());
    EXPECT_EQ(WorldPosition(pawn).y, 0.0F);
    EXPECT_TRUE(Controller(pawn).grounded);
}

TEST_F(CharacterPredictionTest, ServerSyncMirrorsAuthorityOntoItsOwnPawn) {
    const Entity clientPawn = MakePawn();
    ASSERT_TRUE(PossessController(
        entityManager, MakeController(ControllerKind::Player), clientPawn));
    Gameplay::GameplayInput push;
    push.move = {1.0F, 0.0F, 0.0F};
    DrivePawn(clientPawn, push);

    const Entity serverPawn = MakePawn();
    entityManager.AddComponent<PredictedCharacterComponent>(
        serverPawn, PredictedCharacterComponent{});

    const std::optional<Network::PredictedState> spawn =
        PackPredictedState(entityManager, clientPawn);
    ASSERT_TRUE(spawn.has_value());
    PredictedPair pair = MakePair(
        MakeCharacterPredictionStepper(CharacterControllerComponent{}, kGravityY),
        MakeCharacterPredictionStepper(CharacterControllerComponent{}, kGravityY),
        *spawn);
    ASSERT_TRUE(pair.Establish(1000U));

    for (std::uint64_t n = 1U; n <= 5U; ++n) {
        EXPECT_EQ(TickPredictedPawn(entityManager, sceneGraph, clientPawn, pair.client), n);
        pair.UpdateBoth(1000U);
        pair.UpdateBoth(1000U);
        EXPECT_TRUE(SyncPredictedPawn(entityManager, sceneGraph, serverPawn, pair.server));
        EXPECT_EQ(WorldPosition(serverPawn), pair.server.LocalState().position);
        EXPECT_EQ(Controller(serverPawn).velocity, pair.server.LocalState().velocity);
    }
    EXPECT_EQ(pair.server.LastAppliedTick(), 5U);
    EXPECT_EQ(WorldPosition(serverPawn), WorldPosition(clientPawn));
}

TEST_F(CharacterPredictionTest, TickStaysDenseWhenComponentsAreGone) {
    const Entity pawn = MakePawn();
    ASSERT_TRUE(PossessController(entityManager, MakeController(ControllerKind::Player),
                                     pawn));
    DrivePawn(pawn, Input());

    const std::optional<Network::PredictedState> spawn =
        PackPredictedState(entityManager, pawn);
    ASSERT_TRUE(spawn.has_value());
    PredictedPair pair = MakePair(
        MakeCharacterPredictionStepper(CharacterControllerComponent{}, kGravityY),
        MakeCharacterPredictionStepper(CharacterControllerComponent{}, kGravityY),
        *spawn);
    ASSERT_TRUE(pair.Establish(1000U));

    EXPECT_EQ(TickPredictedPawn(entityManager, sceneGraph, pawn, pair.client), 1U);

    // The controller goes missing mid-stream: the tick still issues while the sync refuses,
    // so the server never waits on a gap for an entity problem.
    entityManager.RemoveComponent<CharacterControllerComponent>(pawn);
    EXPECT_EQ(TickPredictedPawn(entityManager, sceneGraph, pawn, pair.client), 2U);
    EXPECT_FALSE(SyncPredictedPawn(entityManager, sceneGraph, pawn, pair.client));

    entityManager.AddComponent<CharacterControllerComponent>(
        pawn, CharacterControllerComponent{});
    EXPECT_TRUE(SyncPredictedPawn(entityManager, sceneGraph, pawn, pair.client));
    EXPECT_EQ(WorldPosition(pawn), pair.client.LocalState().position);

    pair.UpdateBoth(1000U);
    pair.UpdateBoth(1000U);
    EXPECT_EQ(pair.server.LastAppliedTick(), 2U);
    EXPECT_EQ(pair.client.Stats().reconciles, 0U);
}


TEST_F(CharacterPredictionTest, TwoPawns_TickAndSyncPerPawnThroughOnePredictor) {
    const Entity pawnA = MakePawn();
    const Entity pawnB = MakePawn(Math::Vector3{10.0F, 0.0F, 0.0F});
    ASSERT_TRUE(PossessController(entityManager, MakeController(ControllerKind::Player),
                                     pawnA));
    ASSERT_TRUE(PossessController(entityManager, MakeController(ControllerKind::Player),
                                     pawnB));
    Gameplay::GameplayInput pushA;
    pushA.move = {0.5F, 0.0F, 0.0F};
    DrivePawn(pawnA, pushA);
    Gameplay::GameplayInput pushB;
    pushB.move = {-0.5F, 0.0F, 0.0F};
    DrivePawn(pawnB, pushB);
    entityManager.AddComponent<PredictedCharacterComponent>(pawnA,
                                                                 PredictedCharacterComponent{});
    entityManager.AddComponent<PredictedCharacterComponent>(pawnB,
                                                                 PredictedCharacterComponent{});

    const std::optional<Network::PredictedState> spawnA =
        PackPredictedState(entityManager, pawnA);
    const std::optional<Network::PredictedState> spawnB =
        PackPredictedState(entityManager, pawnB);
    ASSERT_TRUE(spawnA.has_value());
    ASSERT_TRUE(spawnB.has_value());
    PredictedPair pair = MakePair(
        MakeCharacterPredictionStepper(CharacterControllerComponent{}, kGravityY),
        MakeCharacterPredictionStepper(CharacterControllerComponent{}, kGravityY),
        *spawnA);
    ASSERT_TRUE(pair.client.SeedPawn(1U, *spawnB));
    ASSERT_TRUE(pair.server.SeedPawn(1U, *spawnB));
    ASSERT_TRUE(pair.Establish(1000U));

    for (std::uint64_t n = 1U; n <= 5U; ++n) {
        EXPECT_EQ(TickPredictedPawn(entityManager, sceneGraph, pawnA, pair.client), n);
        EXPECT_EQ(TickPredictedPawn(entityManager, sceneGraph, pawnB, pair.client, 1U), n);
        pair.UpdateBoth(1000U);
        pair.UpdateBoth(1000U);
        EXPECT_TRUE(SyncPredictedPawn(entityManager, sceneGraph, pawnA, pair.client));
        EXPECT_TRUE(SyncPredictedPawn(entityManager, sceneGraph, pawnB, pair.client, 1U));
        EXPECT_EQ(WorldPosition(pawnA), pair.client.LocalState(0U).position);
        EXPECT_EQ(WorldPosition(pawnB), pair.client.LocalState(1U).position);
    }
    EXPECT_EQ(pair.client.LastAckedTick(0U), 5U);
    EXPECT_EQ(pair.client.LastAckedTick(1U), 5U);
    EXPECT_EQ(pair.server.LastAppliedTick(0U), 5U);
    EXPECT_EQ(pair.server.LastAppliedTick(1U), 5U);
    EXPECT_EQ(pair.client.Stats().reconciles, 0U);
    EXPECT_GT(WorldPosition(pawnA).x, 0.0F);
    EXPECT_LT(WorldPosition(pawnB).x, 10.0F);
}

TEST_F(CharacterPredictionTest, ServerSyncMirrorsPerPawnAuthorityOntoItsOwnPawns) {
    const Entity pawnA = MakePawn();
    const Entity pawnB = MakePawn(Math::Vector3{10.0F, 0.0F, 0.0F});
    ASSERT_TRUE(PossessController(entityManager, MakeController(ControllerKind::Player),
                                     pawnA));
    ASSERT_TRUE(PossessController(entityManager, MakeController(ControllerKind::Player),
                                     pawnB));
    Gameplay::GameplayInput pushA;
    pushA.move = {0.5F, 0.0F, 0.0F};
    DrivePawn(pawnA, pushA);
    Gameplay::GameplayInput pushB;
    pushB.move = {-0.5F, 0.0F, 0.0F};
    DrivePawn(pawnB, pushB);
    entityManager.AddComponent<PredictedCharacterComponent>(pawnA,
                                                                 PredictedCharacterComponent{});
    entityManager.AddComponent<PredictedCharacterComponent>(pawnB,
                                                                 PredictedCharacterComponent{});

    const std::optional<Network::PredictedState> spawnA =
        PackPredictedState(entityManager, pawnA);
    const std::optional<Network::PredictedState> spawnB =
        PackPredictedState(entityManager, pawnB);
    ASSERT_TRUE(spawnA.has_value());
    ASSERT_TRUE(spawnB.has_value());
    PredictedPair pair = MakePair(
        MakeCharacterPredictionStepper(CharacterControllerComponent{}, kGravityY),
        MakeCharacterPredictionStepper(CharacterControllerComponent{}, kGravityY),
        *spawnA);
    ASSERT_TRUE(pair.client.SeedPawn(1U, *spawnB));
    ASSERT_TRUE(pair.server.SeedPawn(1U, *spawnB));
    ASSERT_TRUE(pair.Establish(1000U));

    for (std::uint64_t n = 1U; n <= 5U; ++n) {
        EXPECT_EQ(TickPredictedPawn(entityManager, sceneGraph, pawnA, pair.client), n);
        EXPECT_EQ(TickPredictedPawn(entityManager, sceneGraph, pawnB, pair.client, 1U), n);
        pair.UpdateBoth(1000U);
        pair.UpdateBoth(1000U);
    }
    EXPECT_EQ(pair.client.Stats().reconciles, 0U);

    // The server's own two pawns mirror whatever authority holds, one sync per pawn.
    const Entity serverA = MakePawn();
    const Entity serverB = MakePawn();
    EXPECT_TRUE(SyncPredictedPawn(entityManager, sceneGraph, serverA, pair.server));
    EXPECT_TRUE(SyncPredictedPawn(entityManager, sceneGraph, serverB, pair.server, 1U));
    EXPECT_EQ(WorldPosition(serverA), pair.server.LocalState().position);
    EXPECT_EQ(WorldPosition(serverB), pair.server.LocalState(1U).position);
    EXPECT_EQ(WorldPosition(serverB), WorldPosition(pawnB));
}

} // namespace
} // namespace UVE::Scene::Tests
