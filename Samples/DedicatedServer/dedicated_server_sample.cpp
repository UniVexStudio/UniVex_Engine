// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "dedicated_server_sample.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "character_controller_component.h"
#include "collider_component.h"
#include "predicted_character_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "gameplay_input.h"
#include "pawn.h"
#include "vector3.h"
#include "memory_manager.h"
#include "network_lobby.h"
#include "network_prediction.h"
#include "network_relay.h"
#include "network_replication.h"
#include "network_session.h"
#include "network_transport.h"
#include "character_3d.h"
#include "character_prediction.h"
#include "replicated_entity.h"
#include "character_world_query.h"
#include "collision_system.h"
#include "scene_graph.h"

namespace UVE::Samples {
namespace {

constexpr std::uint64_t kTickMs = 16U;
constexpr std::uint64_t kBaseMs = 1000U;
// The sim step IS the tick: 62.5 Hz wall to wall, prediction and the walker alike.
constexpr float kFixedDt = 0.016F;
constexpr float kGravityY = -9.81F;
constexpr Network::NetworkEntityId kPawnNetId = 7U;
constexpr Network::NetworkEntityId kNpcNetId = 1U;
// Sessions in feature order: lobby, relay, replication, prediction.
constexpr std::size_t kRelayFeature = 1U;
constexpr std::uint64_t kDefaultTimeoutMs = 5000U;
// Relay rooms can be legitimately one-sided: a bot alone in its room hears nothing by
// design, so relay sessions outlive the run instead of timing out on the silence. Every
// other session keeps the 5 s default - the story's traffic must prove their liveness.
constexpr std::uint64_t kRelayTimeoutMs = 30000U;

const Network::PredictionConfig kSamplePredictionConfig{0.016F, 0.001F, 0.01F};

[[nodiscard]] Network::PredictionStepper MakeClientStepper() {
    return Scene::MakeCharacterPredictionStepper(Scene::CharacterControllerComponent{},
                                                    kGravityY);
}

[[nodiscard]] Network::PredictionStepper MakeServerStepper() {
    Network::PredictionStepper base = MakeClientStepper();
    // The only world the clients cannot see: a floor at y = 0 that lands what falls.
    return [base = std::move(base)](const Network::PredictedState& state,
                                     const Network::PredictedInput& input, const float dt) {
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
}

[[nodiscard]] Network::PredictedState SpawnState(const Math::Vector3 spawn) {
    Network::PredictedState state;
    state.position = spawn;
    return state;
}

[[nodiscard]] std::vector<std::uint8_t> Bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

[[nodiscard]] std::string Vec(const Math::Vector3 value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "(%.2f, %.2f, %.2f)", static_cast<double>(value.x),
                  static_cast<double>(value.y), static_cast<double>(value.z));
    return std::string{buffer};
}

struct Bot final {
    std::string name;
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager;
    Scene::SceneGraph sceneGraph;
    Network::NetworkLobbyClient lobby;
    Network::NetworkSession relaySession;
    Network::NetworkReplicator replicator;
    Network::NetworkPrediction predictor;
    Scene::ReplicatedEntityMap map;
    Scene::Entity pawn = Scene::kInvalidEntity;
    Scene::Entity npcDoll = Scene::kInvalidEntity;
    Math::Vector3 push{};
    std::uint64_t jumpTick = 0U;
    std::vector<std::string> heard;
    std::vector<std::string> lastRoster;
    std::uint64_t rosterPushes = 0U;
    bool wired = false;

    Bot(std::string botName, Network::NetworkSession lobbySession,
           Network::NetworkSession botRelaySession, Network::NetworkSession replSession,
           Network::NetworkSession predSession, const Math::Vector3 spawn,
           const Math::Vector3 pushInput, const std::uint64_t jumpAt)
        : name(std::move(botName)),
          entityManager(memoryManager.GetDefaultAllocator(), eventSystem),
          lobby(std::move(lobbySession)),
          relaySession(std::move(botRelaySession)),
          replicator(std::move(replSession)),
          predictor(std::move(predSession), Network::SessionRole::Client,
                    MakeClientStepper(), SpawnState(spawn), kSamplePredictionConfig),
          push(pushInput),
          jumpTick(jumpAt) {
        // The pawn: attached, defined, possessed, driven, tagged - the production shape.
        pawn = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = spawn;
        sceneGraph.AttachTransform(entityManager, pawn, transform);
        Scene::ApplyCharacter3DObjectDefinition(entityManager, pawn,
                                                   Scene::Character3DObjectDefinition::WithDefaultCollider());
        sceneGraph.Update(entityManager);
        const Scene::Entity driver = entityManager.CreateEntity();
        Scene::ControllerComponent controller;
        controller.kind = Scene::ControllerKind::Player;
        entityManager.AddComponent<Scene::ControllerComponent>(driver, controller);
        const bool possessed = Scene::PossessController(entityManager, driver, pawn);
        Gameplay::GameplayInput input;
        input.move = push;
        entityManager.GetComponent<Scene::PawnComponent>(pawn).input = input;
        entityManager.AddComponent<Scene::PredictedCharacterComponent>(
            pawn, Scene::PredictedCharacterComponent{});

        // The NPC doll: a transform triple, nothing simulated.
        npcDoll = entityManager.CreateEntity();
        Scene::TransformComponent dollTransform;
        dollTransform.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
        sceneGraph.AttachTransform(entityManager, npcDoll, dollTransform);
        sceneGraph.Update(entityManager);

        const bool pawnRegistered = Scene::RegisterReplicatedEntity(
            entityManager, sceneGraph, replicator, map, pawn, kPawnNetId,
            Network::ReplicationAuthority::Owner);
        const bool npcRegistered = Scene::RegisterReplicatedEntity(
            entityManager, sceneGraph, replicator, map, npcDoll, kNpcNetId,
            Network::ReplicationAuthority::Proxy);
        wired = possessed && pawnRegistered && npcRegistered;

        lobby.SetRosterHandler([this](const Network::LobbyRoomId,
                                         const std::vector<std::string>&) { ++rosterPushes; });
    }

    void DriveTick(const std::uint64_t tick) {
        Gameplay::GameplayInput input =
            entityManager.GetComponent<Scene::PawnComponent>(pawn).input;
        input.move = push;
        input.jumpPressed = (tick == jumpTick);
        entityManager.GetComponent<Scene::PawnComponent>(pawn).input = input;
    }

    [[nodiscard]] bool Shout(const std::uint64_t tick) {
        // The relay is opaque: no sender metadata crosses, so the name rides in the bytes.
        return relaySession.SendData(Bytes(name + "@" + std::to_string(tick)));
    }

    void DrainShouts() {
        while (const std::optional<std::vector<std::uint8_t>> datagram =
                   relaySession.TryReceiveData()) {
            heard.emplace_back(datagram->begin(), datagram->end());
        }
    }

    void RefreshRoster() {
        lobby.QueryRoster(lobby.CurrentRoom(),
                             [this](std::vector<std::string> roster, const std::string& error) {
                                 if (error.empty()) {
                                     lastRoster = std::move(roster);
                                 }
                             });
    }

    [[nodiscard]] Math::Vector3 PawnPose() {
        return entityManager.GetComponent<Scene::WorldTransformComponent>(pawn)
            .worldPosition;
    }
};

struct Server final {
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager;
    Scene::SceneGraph sceneGraph;
    Physics::CollisionSystem collision;
    Network::NetworkLobby lobby;
    Network::NetworkRelay relay;
    std::vector<Network::NetworkReplicator> replicators;
    std::vector<Network::NetworkPrediction> predictors;
    std::vector<Scene::ReplicatedEntityMap> maps;
    std::vector<Network::LobbyClientId> lobbyIds;
    std::vector<Network::RelayPeerId> peerIds;
    std::vector<Scene::Entity> dolls;
    Scene::Entity npc = Scene::kInvalidEntity;
    Scene::Entity walker = Scene::kInvalidEntity;
    float npcX = 0.0F;
    float npcDir = 1.0F;
    bool wired = true;

    Server() : entityManager(memoryManager.GetDefaultAllocator(), eventSystem) {
        replicators.reserve(3U);
        predictors.reserve(3U);
        maps.reserve(3U);
        lobbyIds.reserve(3U);
        peerIds.reserve(3U);
        dolls.reserve(3U);
    }

    void AddBot(Network::NetworkSession lobbySession,
                   Network::NetworkSession peerSession, Network::NetworkSession replSession,
                   Network::NetworkSession predSession, const Math::Vector3 spawn) {
        lobbyIds.push_back(lobby.AddClient(std::move(lobbySession)));
        peerIds.push_back(relay.AddPeer(std::move(peerSession)));
        replicators.emplace_back(std::move(replSession));
        predictors.emplace_back(std::move(predSession), Network::SessionRole::Server,
                                MakeServerStepper(), SpawnState(spawn),
                                kSamplePredictionConfig);
        const Scene::Entity doll = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = spawn;
        sceneGraph.AttachTransform(entityManager, doll, transform);
        sceneGraph.Update(entityManager);
        dolls.push_back(doll);
        maps.emplace_back();
        wired = wired && Scene::RegisterReplicatedEntity(
                             entityManager, sceneGraph, replicators.back(), maps.back(), doll,
                             kPawnNetId, Network::ReplicationAuthority::Proxy);
    }

    void BuildWorld() {
        // The patrol NPC, owned here and dollied on every bot.
        npc = entityManager.CreateEntity();
        Scene::TransformComponent npcTransform;
        npcTransform.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
        sceneGraph.AttachTransform(entityManager, npc, npcTransform);
        sceneGraph.Update(entityManager);
        for (std::size_t i = 0U; i < replicators.size(); ++i) {
            wired = wired && Scene::RegisterReplicatedEntity(
                                 entityManager, sceneGraph, replicators[i], maps[i], npc,
                                 kNpcNetId, Network::ReplicationAuthority::Owner);
        }

        // A floor and an untagged walker: the real mover steps what prediction owns not.
        const Scene::Entity floor = entityManager.CreateEntity();
        Scene::TransformComponent floorTransform;
        floorTransform.localPosition = Math::Vector3{0.0F, -0.5F, 0.0F};
        sceneGraph.AttachTransform(entityManager, floor, floorTransform);
        Scene::ColliderComponent slab{Math::Vector3{100.0F, 0.5F, 100.0F}};
        entityManager.AddComponent<Scene::ColliderComponent>(floor, slab);
        sceneGraph.Update(entityManager);

        walker = entityManager.CreateEntity();
        Scene::TransformComponent walkTransform;
        walkTransform.localPosition = Math::Vector3{10.0F, 3.0F, 10.0F};
        sceneGraph.AttachTransform(entityManager, walker, walkTransform);
        Scene::ApplyCharacter3DObjectDefinition(entityManager, walker,
                                                   Scene::Character3DObjectDefinition::WithDefaultCollider());
        sceneGraph.Update(entityManager);
    }

    void MoveNpc() {
        // A deterministic patrol: ping-pong between the walls at +-5, no sim needed.
        npcX += npcDir * 0.05F;
        if (npcX > 5.0F) {
            npcX = 5.0F;
            npcDir = -1.0F;
        } else if (npcX < -5.0F) {
            npcX = -5.0F;
            npcDir = 1.0F;
        }
        Scene::TransformComponent transform =
            entityManager.GetComponent<Scene::TransformComponent>(npc);
        transform.localPosition = Math::Vector3{npcX, 1.0F, 0.0F};
        sceneGraph.SetLocalTransform(entityManager, npc, transform);
        sceneGraph.Update(entityManager);
    }

    [[nodiscard]] Physics::Character3DStepResult StepWalker() {
        Physics::CharacterMotionInput input;
        input.move = Math::Vector3{1.0F, 0.0F, 0.0F};
        return Physics::StepCharacter3D(entityManager, sceneGraph, collision, walker, input,
                                           kGravityY, kFixedDt);
    }

    void BindRelay() {
        // The documented relay-rooms loop: lobby rooms bind onto relay rooms verbatim, so a
        // shout only ever reaches roommates.
        for (std::size_t i = 0U; i < lobbyIds.size(); ++i) {
            static_cast<void>(
                relay.SetPeerRoom(peerIds[i], lobby.ClientRoom(lobbyIds[i])));
        }
    }
};

struct Story final {
    DedicatedServerSampleConfig config;
    std::vector<std::string> failures;
    std::vector<Network::LoopbackTransportPair> links;
    Server server;
    std::vector<std::unique_ptr<Bot>> bots;
    std::uint64_t nowMs = kBaseMs;

    void Say(const std::string& line) {
        if (config.log) {
            config.log(line);
        }
    }

    void Check(const bool condition, const std::string& message) {
        if (!condition) {
            failures.push_back(message);
            Say("CHECK FAIL: " + message);
        }
    }

    void PumpLobby(const int rounds) {
        for (int i = 0; i < rounds; ++i) {
            server.lobby.Update(nowMs);
            for (auto& botPtr : bots) {
                Bot& bot = *botPtr;
                bot.lobby.Update(nowMs);
            }
        }
    }

    void PumpPrediction() {
        for (std::size_t i = 0U; i < bots.size(); ++i) {
            bots[i]->predictor.Update(nowMs);
            server.predictors[i].Update(nowMs);
        }
    }

    void PumpReplication() {
        for (std::size_t i = 0U; i < bots.size(); ++i) {
            bots[i]->replicator.Update(nowMs);
            server.replicators[i].Update(nowMs);
        }
    }

    void PumpRelay() {
        // Flush, forward, receive: the client sessions push to the wire, the relay fans out
        // to roommates, and the second client pump pulls the arrivals in.
        for (auto& botPtr : bots) {
            Bot& bot = *botPtr;
            bot.relaySession.Update(nowMs);
        }
        server.relay.Update(nowMs);
        for (auto& botPtr : bots) {
            Bot& bot = *botPtr;
            bot.relaySession.Update(nowMs);
            bot.DrainShouts();
        }
    }

    void Build() {
        links.reserve(12U);
        // Heap-owned: the bots' callbacks capture their own `this`, and neither the event
        // system nor the entity manager can move - so the bots never move either.
        bots.reserve(3U);
        const char* names[3] = {"amy", "bob", "cat"};
        const Math::Vector3 spawns[3] = {Math::Vector3{0.0F, 3.0F, 0.0F},
                                            Math::Vector3{-4.0F, 3.0F, 2.0F},
                                            Math::Vector3{4.0F, 3.0F, -2.0F}};
        const Math::Vector3 pushes[3] = {Math::Vector3{1.0F, 0.0F, 0.0F},
                                            Math::Vector3{-1.0F, 0.0F, 0.0F},
                                            Math::Vector3{0.0F, 0.0F, 1.0F}};
        const std::uint64_t jumps[3] = {
            100U, std::numeric_limits<std::uint64_t>::max(), 300U};
        for (std::size_t i = 0U; i < 3U; ++i) {
            // One loopback pair per feature per bot: lobby, relay, replication, prediction.
            // Endpoints share the pair's queues, so the pair is stored only after both ends
            // are built from it - exactly like every test harness.
            std::vector<Network::NetworkSession> clientSessions;
            std::vector<Network::NetworkSession> serverSessions;
            clientSessions.reserve(4U);
            serverSessions.reserve(4U);
            for (std::size_t f = 0U; f < 4U; ++f) {
                Network::LoopbackTransportPair pair =
                    Network::CreateLoopbackTransportPair(256U);
                const std::uint64_t timeout =
                    (f == kRelayFeature) ? kRelayTimeoutMs : kDefaultTimeoutMs;
                clientSessions.emplace_back(
                    Network::SessionRole::Client,
                    std::make_unique<Network::LoopbackEndpoint>(pair.a), timeout);
                serverSessions.emplace_back(
                    Network::SessionRole::Server,
                    std::make_unique<Network::LoopbackEndpoint>(pair.b), timeout);
                links.push_back(std::move(pair));
            }
            bots.emplace_back(std::make_unique<Bot>(names[i], std::move(clientSessions[0]), std::move(clientSessions[1]),
                              std::move(clientSessions[2]), std::move(clientSessions[3]),
                              spawns[i], pushes[i], jumps[i]));
            server.AddBot(std::move(serverSessions[0]), std::move(serverSessions[1]),
                             std::move(serverSessions[2]), std::move(serverSessions[3]),
                             spawns[i]);
        }
        server.BuildWorld();
        for (const auto& botPtr : bots) {
            const Bot& bot = *botPtr;
            Check(bot.wired, bot.name + " wired its entities");
        }
        Check(server.wired, "server wired its entities");
    }

    void Establish() {
        for (auto& botPtr : bots) {
            Bot& bot = *botPtr;
            Check(bot.lobby.Session().Connect(nowMs), bot.name + " dials lobby");
            Check(bot.relaySession.Connect(nowMs), bot.name + " dials relay");
            Check(bot.replicator.Session().Connect(nowMs),
                     bot.name + " dials replication");
            Check(bot.predictor.Session().Connect(nowMs),
                     bot.name + " dials prediction");
        }
        for (int i = 0; i < 4; ++i) {
            server.lobby.Update(nowMs);
            server.relay.Update(nowMs);
            for (auto& botPtr : bots) {
                Bot& bot = *botPtr;
                bot.lobby.Update(nowMs);
                bot.relaySession.Update(nowMs);
                bot.replicator.Update(nowMs);
                bot.predictor.Update(nowMs);
            }
            PumpPrediction();
            PumpReplication();
        }
        for (std::size_t i = 0U; i < bots.size(); ++i) {
            Bot& bot = *bots[i];
            Check(bot.lobby.Session().State() ==
                         Network::SessionState::Established,
                     bot.name + " lobby established");
            Check(bot.relaySession.State() == Network::SessionState::Established,
                     bot.name + " relay established");
            Check(bot.replicator.Session().State() ==
                         Network::SessionState::Established,
                     bot.name + " replication established");
            Check(bot.predictor.Session().State() ==
                         Network::SessionState::Established,
                     bot.name + " prediction established");
            const Network::NetworkRpc* rpc = server.lobby.ClientRpc(server.lobbyIds[i]);
            Check(rpc != nullptr && rpc->Session().State() ==
                                          Network::SessionState::Established,
                     "server lobby peer " + std::to_string(i) + " established");
            const Network::NetworkSession* peer =
                server.relay.PeerSession(server.peerIds[i]);
            Check(peer != nullptr &&
                         peer->State() == Network::SessionState::Established,
                     "server relay peer " + std::to_string(i) + " established");
            Check(server.replicators[i].Session().State() ==
                         Network::SessionState::Established,
                     "server replicator " + std::to_string(i) + " established");
            Check(server.predictors[i].Session().State() ==
                         Network::SessionState::Established,
                     "server predictor " + std::to_string(i) + " established");
        }
    }

    void LobbyPhase() {
        // Amy opens the arena; Bob finds it on the list and joins; Cat opens the void.
        Network::LobbyRoomId roomA = 0U;
        bool doneA = false;
        std::string errorA = "pending";
        bots[0]->lobby.CreateRoom(
            "arena", "amy", 4U,
            [&](const Network::LobbyRoomId room, const std::string& error) {
                roomA = room;
                errorA = error;
                doneA = true;
            });
        PumpLobby(6);
        Check(doneA && errorA.empty() && roomA == 1U, "amy creates arena as room 1");
        Check(bots[0]->lobby.CurrentRoom() == 1U, "amy sits in room 1");

        std::vector<Network::LobbyRoomInfo> rooms;
        bool listed = false;
        bots[1]->lobby.ListRooms(
            [&](std::vector<Network::LobbyRoomInfo> found, const std::string& error) {
                rooms = std::move(found);
                listed = error.empty();
            });
        PumpLobby(6);
        Network::LobbyRoomId arena = 0U;
        Check(listed, "bob's room list arrives");
        if (listed) {
            for (const Network::LobbyRoomInfo& room : rooms) {
                if (room.name == "arena") {
                    arena = room.id;
                }
            }
        }
        Check(arena == 1U, "bob lists the arena as room 1");
        std::vector<std::string> joinRoster;
        bool joined = false;
        bots[1]->lobby.JoinRoom(
            arena, "bob",
            [&](std::vector<std::string> roster, const std::string& error) {
                joinRoster = std::move(roster);
                joined = error.empty();
            });
        PumpLobby(6);
        Check(joined && bots[1]->lobby.CurrentRoom() == 1U, "bob joins room 1");
        Check(joinRoster == std::vector<std::string>{"amy", "bob"},
                 "bob's join roster reads amy, bob in member order");

        Network::LobbyRoomId roomC = 0U;
        bool doneC = false;
        bots[2]->lobby.CreateRoom(
            "void", "cat", 4U,
            [&](const Network::LobbyRoomId room, const std::string& error) {
                roomC = room;
                doneC = error.empty();
            });
        PumpLobby(6);
        Check(doneC && roomC == 2U, "cat creates the void as room 2");

        server.BindRelay();
        const Network::RelayRoomId expected[3] = {1U, 1U, 2U};
        for (std::size_t i = 0U; i < 3U; ++i) {
            Check(server.relay.PeerRoom(server.peerIds[i]) == expected[i],
                     "relay peer " + std::to_string(i) + " bound to its lobby room");
        }
        // Bob's join pushed to the one survivor: Amy heard it, no one else heard anything.
        Check(bots[0]->rosterPushes == 1U, "amy heard bob's join push");
        Check(bots[1]->rosterPushes == 0U, "bob heard no pushes");
        Check(bots[2]->rosterPushes == 0U, "cat heard no pushes");
    }

    void Tick(const std::uint64_t tick) {
        nowMs = kBaseMs + (tick + 1U) * kTickMs;
        for (auto& botPtr : bots) {
            Bot& bot = *botPtr;
            bot.DriveTick(tick);
            const Network::PredictionTick tickNumber = Scene::TickPredictedPawn(
                bot.entityManager, bot.sceneGraph, bot.pawn, bot.predictor);
            Check(tickNumber == tick + 1U,
                     bot.name + " ticks in order at " + std::to_string(tick));
        }
        PumpPrediction();
        PumpPrediction();
        for (auto& botPtr : bots) {
            Bot& bot = *botPtr;
            Check(Scene::SyncPredictedPawn(bot.entityManager, bot.sceneGraph, bot.pawn,
                                                 bot.predictor),
                     bot.name + " syncs its pawn at tick " + std::to_string(tick));
        }

        server.MoveNpc();
        const Physics::Character3DStepResult walked = server.StepWalker();
        if (tick % 100U == 0U) {
            Check(walked.stepped, "walker steps at tick " + std::to_string(tick));
        }

        PumpReplication();
        PumpReplication();

        if (tick % 30U == 0U) {
            for (auto& botPtr : bots) {
                Bot& bot = *botPtr;
                Check(bot.Shout(tick),
                         bot.name + " shouts at tick " + std::to_string(tick));
            }
        }
        PumpRelay();
        server.BindRelay();
        PumpLobby(1);

        if (tick % 60U == 0U) {
            for (auto& botPtr : bots) {
                Bot& bot = *botPtr;
                bot.RefreshRoster();
            }
        }
        if (tick == 300U) {
            // Mid-run health: nothing dead, nothing stripped, nothing to prune anywhere.
            for (std::size_t i = 0U; i < bots.size(); ++i) {
                Check(Scene::PruneReplicatedEntities(bots[i]->entityManager,
                                                           bots[i]->replicator, bots[i]->map) == 0U,
                         bots[i]->name + " prunes nothing mid-run");
                Check(Scene::PruneReplicatedEntities(server.entityManager,
                                                           server.replicators[i],
                                                           server.maps[i]) == 0U,
                         "server prunes nothing mid-run");
            }
        }
        if (tick % 100U == 0U) {
            for (auto& botPtr : bots) {
                Bot& bot = *botPtr;
                RefusalProbe(bot, tick);
            }
            Say("tick " + std::to_string(tick) + ": amy " + Vec(bots[0]->PawnPose()) +
                   " r=" + std::to_string(bots[0]->predictor.Stats().reconciles) + ", bob " +
                   Vec(bots[1]->PawnPose()) + " r=" +
                   std::to_string(bots[1]->predictor.Stats().reconciles) + ", cat " +
                   Vec(bots[2]->PawnPose()) + " r=" +
                   std::to_string(bots[2]->predictor.Stats().reconciles) + ", npc x=" +
                   std::to_string(server.npcX));
        }
    }

    void RefusalProbe(Bot& bot, const std::uint64_t tick) {
        // The live no-double-step proof: the real mover refuses the tagged pawn and leaves
        // its pose exactly where prediction put it.
        const float xBefore = bot.PawnPose().x;
        Physics::CharacterMotionInput input;
        input.move = Math::Vector3{1.0F, 0.0F, 0.0F};
        const Physics::Character3DStepResult report = Physics::StepCharacter3D(
            bot.entityManager, bot.sceneGraph, server.collision, bot.pawn, input, kGravityY,
            kFixedDt);
        Check(report.code == Physics::Character3DStepCode::PredictedElsewhere &&
                     !report.stepped,
                 bot.name + " still refuses the mover at tick " + std::to_string(tick));
        Check(bot.PawnPose().x == xBefore,
                 bot.name + " unmoved by the refusal at tick " + std::to_string(tick));
    }

    void Settle() {
        // Flush the cadenced tails (replication sends every 100 ms of sim): a few long
        // rounds so the last poses land exactly.
        for (int i = 0; i < 4; ++i) {
            nowMs += 100U;
            PumpReplication();
            PumpReplication();
            PumpPrediction();
            PumpRelay();
            PumpLobby(1);
        }
    }

    void Verify(DedicatedServerSampleStats& stats) {
        for (std::size_t i = 0U; i < bots.size(); ++i) {
            Bot& bot = *bots[i];
            Check(bot.lobby.Session().State() ==
                         Network::SessionState::Established,
                     bot.name + " lobby survives the run");
            Check(bot.relaySession.State() == Network::SessionState::Established,
                     bot.name + " relay survives the run");
            Check(bot.replicator.Session().State() ==
                         Network::SessionState::Established,
                     bot.name + " replication survives the run");
            Check(bot.predictor.Session().State() ==
                         Network::SessionState::Established,
                     bot.name + " prediction survives the run");
        }

        Check(bots[0]->lobby.CurrentRoom() == 1U, "amy ends in room 1");
        Check(bots[1]->lobby.CurrentRoom() == 1U, "bob ends in room 1");
        Check(bots[2]->lobby.CurrentRoom() == 2U, "cat ends in room 2");
        Check(bots[0]->lastRoster == std::vector<std::string>{"amy", "bob"},
                 "arena roster refreshes to amy, bob");
        Check(bots[1]->lastRoster == std::vector<std::string>{"amy", "bob"},
                 "bob's arena roster refreshes too");
        Check(bots[2]->lastRoster == std::vector<std::string>{"cat"},
                 "void roster refreshes to cat");

        const Network::RelayRoomId expected[3] = {1U, 1U, 2U};
        for (std::size_t i = 0U; i < 3U; ++i) {
            Check(server.relay.PeerRoom(server.peerIds[i]) == expected[i],
                     "relay peer " + std::to_string(i) + " stays bound");
        }

        const std::size_t expectedShouts = (config.ticks + 29U) / 30U;
        CheckShouts(*bots[0], "bob@", expectedShouts);
        CheckShouts(*bots[1], "amy@", expectedShouts);
        Check(bots[2]->heard.empty(), "cat hears nothing in the void");

        for (std::size_t i = 0U; i < bots.size(); ++i) {
            const Math::Vector3 pawn =
                bots[i]->entityManager
                    .GetComponent<Scene::WorldTransformComponent>(bots[i]->pawn)
                    .worldPosition;
            const Math::Vector3 doll =
                server.entityManager
                    .GetComponent<Scene::WorldTransformComponent>(server.dolls[i])
                    .worldPosition;
            Check(doll == pawn, bots[i]->name + "'s server doll tracks exactly");
            const Math::Vector3 npcBot =
                bots[i]->entityManager
                    .GetComponent<Scene::WorldTransformComponent>(bots[i]->npcDoll)
                    .worldPosition;
            const Math::Vector3 npcServer =
                server.entityManager
                    .GetComponent<Scene::WorldTransformComponent>(server.npc)
                    .worldPosition;
            Check(npcBot == npcServer, bots[i]->name + "'s npc doll tracks exactly");

            const Network::PredictionStats prediction = bots[i]->predictor.Stats();
            Check(prediction.reconciles > 0U,
                     bots[i]->name + " reconciled its landings");
            Check(bots[i]->predictor.LastAckedTick() == config.ticks,
                     bots[i]->name + " acked every tick");
            const Scene::CharacterControllerComponent& controller =
                bots[i]->entityManager
                    .GetComponent<Scene::CharacterControllerComponent>(bots[i]->pawn);
            Check(pawn.y == 0.0F && controller.grounded,
                     bots[i]->name + " rests on the server floor");
        }

        const Math::Vector3 walker =
            server.entityManager
                .GetComponent<Scene::WorldTransformComponent>(server.walker)
                .worldPosition;
        const Scene::CharacterControllerComponent& walkerController =
            server.entityManager.GetComponent<Scene::CharacterControllerComponent>(
                server.walker);
        Check(walker.x > 12.0F && walkerController.grounded,
                 "walker crossed the floor and stays grounded");

        stats.ticks = config.ticks;
        stats.reconcilesAmy = bots[0]->predictor.Stats().reconciles;
        stats.reconcilesBob = bots[1]->predictor.Stats().reconciles;
        stats.reconcilesCat = bots[2]->predictor.Stats().reconciles;
        stats.shoutsHeardAmy = bots[0]->heard.size();
        stats.shoutsHeardBob = bots[1]->heard.size();
        stats.shoutsHeardCat = bots[2]->heard.size();
        stats.snapshotsReceived = 0U;
        for (const Network::NetworkReplicator& replicator : server.replicators) {
            stats.snapshotsReceived += replicator.Stats().snapshotsReceived;
        }
        Check(stats.snapshotsReceived > 0U, "the server received snapshots");
        stats.restY = bots[0]->PawnPose().y;
        stats.walkerX = walker.x;
    }

    void CheckShouts(const Bot& bot, const std::string& prefix,
                        const std::size_t expected) {
        Check(bot.heard.size() == expected,
                 bot.name + " heard " + std::to_string(bot.heard.size()) + " shouts");
        for (const std::string& shout : bot.heard) {
            Check(shout.rfind(prefix, 0U) == 0U,
                     bot.name + " heard only roommates (" + shout + ")");
        }
    }

    [[nodiscard]] DedicatedServerSampleResult Run() {
        Say("dedicated server sample: 1 server, 3 bots (amy+bob in arena, cat in void)");
        Build();
        Say("built: 12 loopback pairs (lobby, relay, replication, prediction per bot)");
        Establish();
        Say("established: every session up on both ends");
        LobbyPhase();
        Say("lobby: arena {amy, bob}, void {cat}; relay bound to match");
        for (std::uint64_t tick = 0U; tick < config.ticks; ++tick) {
            Tick(tick);
        }
        Say("ran " + std::to_string(config.ticks) + " fixed steps");
        Settle();
        DedicatedServerSampleStats stats;
        Verify(stats);
        Say("verified: " + std::to_string(failures.size()) + " violation(s)");
        DedicatedServerSampleResult result;
        result.success = failures.empty();
        result.failures = failures;
        result.stats = stats;
        return result;
    }
};

} // namespace

DedicatedServerSampleResult RunDedicatedServerSample(
    const DedicatedServerSampleConfig& config) {
    Story story;
    story.config = config;
    return story.Run();
}

} // namespace UVE::Samples
