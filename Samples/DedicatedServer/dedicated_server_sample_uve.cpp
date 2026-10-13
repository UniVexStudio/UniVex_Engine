// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "dedicated_server_sample_uve.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/predicted_character_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/gameplay/gameplay_input_uve.h"
#include "uve/gameplay/pawn_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/network/network_lobby_uve.h"
#include "uve/network/network_prediction_uve.h"
#include "uve/network/network_relay_uve.h"
#include "uve/network/network_replication_uve.h"
#include "uve/network/network_session_uve.h"
#include "uve/network/network_transport_uve.h"
#include "uve/objects/3d/character_3d_uve.h"
#include "uve/objects/3d/character_prediction_uve.h"
#include "uve/objects/3d/replicated_entity_uve.h"
#include "uve/physics/character_world_query_uve.h"
#include "uve/physics/collision_system_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Samples {
namespace {

constexpr std::uint64_t kTickMsUVE = 16U;
constexpr std::uint64_t kBaseMsUVE = 1000U;
// The sim step IS the tick: 62.5 Hz wall to wall, prediction and the walker alike.
constexpr float kFixedDtUVE = 0.016F;
constexpr float kGravityYUVE = -9.81F;
constexpr Network::NetworkEntityIdUVE kPawnNetIdUVE = 7U;
constexpr Network::NetworkEntityIdUVE kNpcNetIdUVE = 1U;
// Sessions in feature order: lobby, relay, replication, prediction.
constexpr std::size_t kRelayFeatureUVE = 1U;
constexpr std::uint64_t kDefaultTimeoutMsUVE = 5000U;
// Relay rooms can be legitimately one-sided: a bot alone in its room hears nothing by
// design, so relay sessions outlive the run instead of timing out on the silence. Every
// other session keeps the 5 s default - the story's traffic must prove their liveness.
constexpr std::uint64_t kRelayTimeoutMsUVE = 30000U;

const Network::PredictionConfigUVE kSamplePredictionConfigUVE{0.016F, 0.001F, 0.01F};

[[nodiscard]] Network::PredictionStepperUVE MakeClientStepperUVE() {
    return Scene::MakeCharacterPredictionStepperUVE(Scene::CharacterControllerComponentUVE{},
                                                    kGravityYUVE);
}

[[nodiscard]] Network::PredictionStepperUVE MakeServerStepperUVE() {
    Network::PredictionStepperUVE base = MakeClientStepperUVE();
    // The only world the clients cannot see: a floor at y = 0 that lands what falls.
    return [base = std::move(base)](const Network::PredictedStateUVE& state,
                                     const Network::PredictedInputUVE& input, const float dt) {
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
}

[[nodiscard]] Network::PredictedStateUVE SpawnStateUVE(const Math::Vector3UVE spawn) {
    Network::PredictedStateUVE state;
    state.position = spawn;
    return state;
}

[[nodiscard]] std::vector<std::uint8_t> BytesUVE(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

[[nodiscard]] std::string VecUVE(const Math::Vector3UVE value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "(%.2f, %.2f, %.2f)", static_cast<double>(value.x),
                  static_cast<double>(value.y), static_cast<double>(value.z));
    return std::string{buffer};
}

struct BotUVE final {
    std::string name;
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager;
    Scene::SceneGraphUVE sceneGraph;
    Network::NetworkLobbyClientUVE lobby;
    Network::NetworkSessionUVE relaySession;
    Network::NetworkReplicatorUVE replicator;
    Network::NetworkPredictionUVE predictor;
    Scene::ReplicatedEntityMapUVE map;
    Scene::EntityUVE pawn = Scene::kInvalidEntityUVE;
    Scene::EntityUVE npcDoll = Scene::kInvalidEntityUVE;
    Math::Vector3UVE push{};
    std::uint64_t jumpTick = 0U;
    std::vector<std::string> heard;
    std::vector<std::string> lastRoster;
    std::uint64_t rosterPushes = 0U;
    bool wired = false;

    BotUVE(std::string botName, Network::NetworkSessionUVE lobbySession,
           Network::NetworkSessionUVE botRelaySession, Network::NetworkSessionUVE replSession,
           Network::NetworkSessionUVE predSession, const Math::Vector3UVE spawn,
           const Math::Vector3UVE pushInput, const std::uint64_t jumpAt)
        : name(std::move(botName)),
          entityManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem),
          lobby(std::move(lobbySession)),
          relaySession(std::move(botRelaySession)),
          replicator(std::move(replSession)),
          predictor(std::move(predSession), Network::SessionRoleUVE::Client,
                    MakeClientStepperUVE(), SpawnStateUVE(spawn), kSamplePredictionConfigUVE),
          push(pushInput),
          jumpTick(jumpAt) {
        // The pawn: attached, defined, possessed, driven, tagged - the production shape.
        pawn = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform;
        transform.localPosition = spawn;
        sceneGraph.AttachTransformUVE(entityManager, pawn, transform);
        Scene::ApplyCharacter3DObjectDefinitionUVE(entityManager, pawn,
                                                   Scene::Character3DObjectDefinitionUVE{});
        sceneGraph.UpdateUVE(entityManager);
        const Scene::EntityUVE driver = entityManager.CreateEntityUVE();
        Scene::ControllerComponentUVE controller;
        controller.kind = Scene::ControllerKindUVE::Player;
        entityManager.AddComponentUVE<Scene::ControllerComponentUVE>(driver, controller);
        const bool possessed = Scene::PossessControllerUVE(entityManager, driver, pawn);
        Gameplay::GameplayInputUVE input;
        input.move = push;
        entityManager.GetComponentUVE<Scene::PawnComponentUVE>(pawn).input = input;
        entityManager.AddComponentUVE<Scene::PredictedCharacterComponentUVE>(
            pawn, Scene::PredictedCharacterComponentUVE{});

        // The NPC doll: a transform triple, nothing simulated.
        npcDoll = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE dollTransform;
        dollTransform.localPosition = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        sceneGraph.AttachTransformUVE(entityManager, npcDoll, dollTransform);
        sceneGraph.UpdateUVE(entityManager);

        const bool pawnRegistered = Scene::RegisterReplicatedEntityUVE(
            entityManager, sceneGraph, replicator, map, pawn, kPawnNetIdUVE,
            Network::ReplicationAuthorityUVE::Owner);
        const bool npcRegistered = Scene::RegisterReplicatedEntityUVE(
            entityManager, sceneGraph, replicator, map, npcDoll, kNpcNetIdUVE,
            Network::ReplicationAuthorityUVE::Proxy);
        wired = possessed && pawnRegistered && npcRegistered;

        lobby.SetRosterHandlerUVE([this](const Network::LobbyRoomIdUVE,
                                         const std::vector<std::string>&) { ++rosterPushes; });
    }

    void DriveTickUVE(const std::uint64_t tick) {
        Gameplay::GameplayInputUVE input =
            entityManager.GetComponentUVE<Scene::PawnComponentUVE>(pawn).input;
        input.move = push;
        input.jumpPressed = (tick == jumpTick);
        entityManager.GetComponentUVE<Scene::PawnComponentUVE>(pawn).input = input;
    }

    [[nodiscard]] bool ShoutUVE(const std::uint64_t tick) {
        // The relay is opaque: no sender metadata crosses, so the name rides in the bytes.
        return relaySession.SendDataUVE(BytesUVE(name + "@" + std::to_string(tick)));
    }

    void DrainShoutsUVE() {
        while (const std::optional<std::vector<std::uint8_t>> datagram =
                   relaySession.TryReceiveDataUVE()) {
            heard.emplace_back(datagram->begin(), datagram->end());
        }
    }

    void RefreshRosterUVE() {
        lobby.QueryRosterUVE(lobby.CurrentRoomUVE(),
                             [this](std::vector<std::string> roster, const std::string& error) {
                                 if (error.empty()) {
                                     lastRoster = std::move(roster);
                                 }
                             });
    }

    [[nodiscard]] Math::Vector3UVE PawnPoseUVE() {
        return entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(pawn)
            .worldPosition;
    }
};

struct ServerUVE final {
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager;
    Scene::SceneGraphUVE sceneGraph;
    Physics::CollisionSystemUVE collision;
    Network::NetworkLobbyUVE lobby;
    Network::NetworkRelayUVE relay;
    std::vector<Network::NetworkReplicatorUVE> replicators;
    std::vector<Network::NetworkPredictionUVE> predictors;
    std::vector<Scene::ReplicatedEntityMapUVE> maps;
    std::vector<Network::LobbyClientIdUVE> lobbyIds;
    std::vector<Network::RelayPeerIdUVE> peerIds;
    std::vector<Scene::EntityUVE> dolls;
    Scene::EntityUVE npc = Scene::kInvalidEntityUVE;
    Scene::EntityUVE walker = Scene::kInvalidEntityUVE;
    float npcX = 0.0F;
    float npcDir = 1.0F;
    bool wired = true;

    ServerUVE() : entityManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem) {
        replicators.reserve(3U);
        predictors.reserve(3U);
        maps.reserve(3U);
        lobbyIds.reserve(3U);
        peerIds.reserve(3U);
        dolls.reserve(3U);
    }

    void AddBotUVE(Network::NetworkSessionUVE lobbySession,
                   Network::NetworkSessionUVE peerSession, Network::NetworkSessionUVE replSession,
                   Network::NetworkSessionUVE predSession, const Math::Vector3UVE spawn) {
        lobbyIds.push_back(lobby.AddClientUVE(std::move(lobbySession)));
        peerIds.push_back(relay.AddPeerUVE(std::move(peerSession)));
        replicators.emplace_back(std::move(replSession));
        predictors.emplace_back(std::move(predSession), Network::SessionRoleUVE::Server,
                                MakeServerStepperUVE(), SpawnStateUVE(spawn),
                                kSamplePredictionConfigUVE);
        const Scene::EntityUVE doll = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform;
        transform.localPosition = spawn;
        sceneGraph.AttachTransformUVE(entityManager, doll, transform);
        sceneGraph.UpdateUVE(entityManager);
        dolls.push_back(doll);
        maps.emplace_back();
        wired = wired && Scene::RegisterReplicatedEntityUVE(
                             entityManager, sceneGraph, replicators.back(), maps.back(), doll,
                             kPawnNetIdUVE, Network::ReplicationAuthorityUVE::Proxy);
    }

    void BuildWorldUVE() {
        // The patrol NPC, owned here and dollied on every bot.
        npc = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE npcTransform;
        npcTransform.localPosition = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        sceneGraph.AttachTransformUVE(entityManager, npc, npcTransform);
        sceneGraph.UpdateUVE(entityManager);
        for (std::size_t i = 0U; i < replicators.size(); ++i) {
            wired = wired && Scene::RegisterReplicatedEntityUVE(
                                 entityManager, sceneGraph, replicators[i], maps[i], npc,
                                 kNpcNetIdUVE, Network::ReplicationAuthorityUVE::Owner);
        }

        // A floor and an untagged walker: the real mover steps what prediction owns not.
        const Scene::EntityUVE floor = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE floorTransform;
        floorTransform.localPosition = Math::Vector3UVE{0.0F, -0.5F, 0.0F};
        sceneGraph.AttachTransformUVE(entityManager, floor, floorTransform);
        Scene::ColliderComponentUVE slab{Math::Vector3UVE{100.0F, 0.5F, 100.0F}};
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(floor, slab);
        sceneGraph.UpdateUVE(entityManager);

        walker = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE walkTransform;
        walkTransform.localPosition = Math::Vector3UVE{10.0F, 3.0F, 10.0F};
        sceneGraph.AttachTransformUVE(entityManager, walker, walkTransform);
        Scene::ApplyCharacter3DObjectDefinitionUVE(entityManager, walker,
                                                   Scene::Character3DObjectDefinitionUVE{});
        sceneGraph.UpdateUVE(entityManager);
    }

    void MoveNpcUVE() {
        // A deterministic patrol: ping-pong between the walls at +-5, no sim needed.
        npcX += npcDir * 0.05F;
        if (npcX > 5.0F) {
            npcX = 5.0F;
            npcDir = -1.0F;
        } else if (npcX < -5.0F) {
            npcX = -5.0F;
            npcDir = 1.0F;
        }
        Scene::TransformComponentUVE transform =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(npc);
        transform.localPosition = Math::Vector3UVE{npcX, 1.0F, 0.0F};
        sceneGraph.SetLocalTransformUVE(entityManager, npc, transform);
        sceneGraph.UpdateUVE(entityManager);
    }

    [[nodiscard]] Physics::Character3DStepResultUVE StepWalkerUVE() {
        Physics::CharacterMotionInputUVE input;
        input.move = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
        return Physics::StepCharacter3DUVE(entityManager, sceneGraph, collision, walker, input,
                                           kGravityYUVE, kFixedDtUVE);
    }

    void BindRelayUVE() {
        // The documented relay-rooms loop: lobby rooms bind onto relay rooms verbatim, so a
        // shout only ever reaches roommates.
        for (std::size_t i = 0U; i < lobbyIds.size(); ++i) {
            static_cast<void>(
                relay.SetPeerRoomUVE(peerIds[i], lobby.ClientRoomUVE(lobbyIds[i])));
        }
    }
};

struct StoryUVE final {
    DedicatedServerSampleConfigUVE config;
    std::vector<std::string> failures;
    std::vector<Network::LoopbackTransportPairUVE> links;
    ServerUVE server;
    std::vector<std::unique_ptr<BotUVE>> bots;
    std::uint64_t nowMs = kBaseMsUVE;

    void SayUVE(const std::string& line) {
        if (config.log) {
            config.log(line);
        }
    }

    void CheckUVE(const bool condition, const std::string& message) {
        if (!condition) {
            failures.push_back(message);
            SayUVE("CHECK FAIL: " + message);
        }
    }

    void PumpLobbyUVE(const int rounds) {
        for (int i = 0; i < rounds; ++i) {
            server.lobby.UpdateUVE(nowMs);
            for (auto& botPtr : bots) {
                BotUVE& bot = *botPtr;
                bot.lobby.UpdateUVE(nowMs);
            }
        }
    }

    void PumpPredictionUVE() {
        for (std::size_t i = 0U; i < bots.size(); ++i) {
            bots[i]->predictor.UpdateUVE(nowMs);
            server.predictors[i].UpdateUVE(nowMs);
        }
    }

    void PumpReplicationUVE() {
        for (std::size_t i = 0U; i < bots.size(); ++i) {
            bots[i]->replicator.UpdateUVE(nowMs);
            server.replicators[i].UpdateUVE(nowMs);
        }
    }

    void PumpRelayUVE() {
        // Flush, forward, receive: the client sessions push to the wire, the relay fans out
        // to roommates, and the second client pump pulls the arrivals in.
        for (auto& botPtr : bots) {
            BotUVE& bot = *botPtr;
            bot.relaySession.UpdateUVE(nowMs);
        }
        server.relay.UpdateUVE(nowMs);
        for (auto& botPtr : bots) {
            BotUVE& bot = *botPtr;
            bot.relaySession.UpdateUVE(nowMs);
            bot.DrainShoutsUVE();
        }
    }

    void BuildUVE() {
        links.reserve(12U);
        // Heap-owned: the bots' callbacks capture their own `this`, and neither the event
        // system nor the entity manager can move - so the bots never move either.
        bots.reserve(3U);
        const char* names[3] = {"amy", "bob", "cat"};
        const Math::Vector3UVE spawns[3] = {Math::Vector3UVE{0.0F, 3.0F, 0.0F},
                                            Math::Vector3UVE{-4.0F, 3.0F, 2.0F},
                                            Math::Vector3UVE{4.0F, 3.0F, -2.0F}};
        const Math::Vector3UVE pushes[3] = {Math::Vector3UVE{1.0F, 0.0F, 0.0F},
                                            Math::Vector3UVE{-1.0F, 0.0F, 0.0F},
                                            Math::Vector3UVE{0.0F, 0.0F, 1.0F}};
        const std::uint64_t jumps[3] = {
            100U, std::numeric_limits<std::uint64_t>::max(), 300U};
        for (std::size_t i = 0U; i < 3U; ++i) {
            // One loopback pair per feature per bot: lobby, relay, replication, prediction.
            // Endpoints share the pair's queues, so the pair is stored only after both ends
            // are built from it - exactly like every test harness.
            std::vector<Network::NetworkSessionUVE> clientSessions;
            std::vector<Network::NetworkSessionUVE> serverSessions;
            clientSessions.reserve(4U);
            serverSessions.reserve(4U);
            for (std::size_t f = 0U; f < 4U; ++f) {
                Network::LoopbackTransportPairUVE pair =
                    Network::CreateLoopbackTransportPairUVE(256U);
                const std::uint64_t timeout =
                    (f == kRelayFeatureUVE) ? kRelayTimeoutMsUVE : kDefaultTimeoutMsUVE;
                clientSessions.emplace_back(
                    Network::SessionRoleUVE::Client,
                    std::make_unique<Network::LoopbackEndpointUVE>(pair.a), timeout);
                serverSessions.emplace_back(
                    Network::SessionRoleUVE::Server,
                    std::make_unique<Network::LoopbackEndpointUVE>(pair.b), timeout);
                links.push_back(std::move(pair));
            }
            bots.emplace_back(std::make_unique<BotUVE>(names[i], std::move(clientSessions[0]), std::move(clientSessions[1]),
                              std::move(clientSessions[2]), std::move(clientSessions[3]),
                              spawns[i], pushes[i], jumps[i]));
            server.AddBotUVE(std::move(serverSessions[0]), std::move(serverSessions[1]),
                             std::move(serverSessions[2]), std::move(serverSessions[3]),
                             spawns[i]);
        }
        server.BuildWorldUVE();
        for (const auto& botPtr : bots) {
            const BotUVE& bot = *botPtr;
            CheckUVE(bot.wired, bot.name + " wired its entities");
        }
        CheckUVE(server.wired, "server wired its entities");
    }

    void EstablishUVE() {
        for (auto& botPtr : bots) {
            BotUVE& bot = *botPtr;
            CheckUVE(bot.lobby.SessionUVE().ConnectUVE(nowMs), bot.name + " dials lobby");
            CheckUVE(bot.relaySession.ConnectUVE(nowMs), bot.name + " dials relay");
            CheckUVE(bot.replicator.SessionUVE().ConnectUVE(nowMs),
                     bot.name + " dials replication");
            CheckUVE(bot.predictor.SessionUVE().ConnectUVE(nowMs),
                     bot.name + " dials prediction");
        }
        for (int i = 0; i < 4; ++i) {
            server.lobby.UpdateUVE(nowMs);
            server.relay.UpdateUVE(nowMs);
            for (auto& botPtr : bots) {
                BotUVE& bot = *botPtr;
                bot.lobby.UpdateUVE(nowMs);
                bot.relaySession.UpdateUVE(nowMs);
                bot.replicator.UpdateUVE(nowMs);
                bot.predictor.UpdateUVE(nowMs);
            }
            PumpPredictionUVE();
            PumpReplicationUVE();
        }
        for (std::size_t i = 0U; i < bots.size(); ++i) {
            BotUVE& bot = *bots[i];
            CheckUVE(bot.lobby.SessionUVE().StateUVE() ==
                         Network::SessionStateUVE::Established,
                     bot.name + " lobby established");
            CheckUVE(bot.relaySession.StateUVE() == Network::SessionStateUVE::Established,
                     bot.name + " relay established");
            CheckUVE(bot.replicator.SessionUVE().StateUVE() ==
                         Network::SessionStateUVE::Established,
                     bot.name + " replication established");
            CheckUVE(bot.predictor.SessionUVE().StateUVE() ==
                         Network::SessionStateUVE::Established,
                     bot.name + " prediction established");
            const Network::NetworkRpcUVE* rpc = server.lobby.ClientRpcUVE(server.lobbyIds[i]);
            CheckUVE(rpc != nullptr && rpc->SessionUVE().StateUVE() ==
                                          Network::SessionStateUVE::Established,
                     "server lobby peer " + std::to_string(i) + " established");
            const Network::NetworkSessionUVE* peer =
                server.relay.PeerSessionUVE(server.peerIds[i]);
            CheckUVE(peer != nullptr &&
                         peer->StateUVE() == Network::SessionStateUVE::Established,
                     "server relay peer " + std::to_string(i) + " established");
            CheckUVE(server.replicators[i].SessionUVE().StateUVE() ==
                         Network::SessionStateUVE::Established,
                     "server replicator " + std::to_string(i) + " established");
            CheckUVE(server.predictors[i].SessionUVE().StateUVE() ==
                         Network::SessionStateUVE::Established,
                     "server predictor " + std::to_string(i) + " established");
        }
    }

    void LobbyPhaseUVE() {
        // Amy opens the arena; Bob finds it on the list and joins; Cat opens the void.
        Network::LobbyRoomIdUVE roomA = 0U;
        bool doneA = false;
        std::string errorA = "pending";
        bots[0]->lobby.CreateRoomUVE(
            "arena", "amy", 4U,
            [&](const Network::LobbyRoomIdUVE room, const std::string& error) {
                roomA = room;
                errorA = error;
                doneA = true;
            });
        PumpLobbyUVE(6);
        CheckUVE(doneA && errorA.empty() && roomA == 1U, "amy creates arena as room 1");
        CheckUVE(bots[0]->lobby.CurrentRoomUVE() == 1U, "amy sits in room 1");

        std::vector<Network::LobbyRoomInfoUVE> rooms;
        bool listed = false;
        bots[1]->lobby.ListRoomsUVE(
            [&](std::vector<Network::LobbyRoomInfoUVE> found, const std::string& error) {
                rooms = std::move(found);
                listed = error.empty();
            });
        PumpLobbyUVE(6);
        Network::LobbyRoomIdUVE arena = 0U;
        CheckUVE(listed, "bob's room list arrives");
        if (listed) {
            for (const Network::LobbyRoomInfoUVE& room : rooms) {
                if (room.name == "arena") {
                    arena = room.id;
                }
            }
        }
        CheckUVE(arena == 1U, "bob lists the arena as room 1");
        std::vector<std::string> joinRoster;
        bool joined = false;
        bots[1]->lobby.JoinRoomUVE(
            arena, "bob",
            [&](std::vector<std::string> roster, const std::string& error) {
                joinRoster = std::move(roster);
                joined = error.empty();
            });
        PumpLobbyUVE(6);
        CheckUVE(joined && bots[1]->lobby.CurrentRoomUVE() == 1U, "bob joins room 1");
        CheckUVE(joinRoster == std::vector<std::string>{"amy", "bob"},
                 "bob's join roster reads amy, bob in member order");

        Network::LobbyRoomIdUVE roomC = 0U;
        bool doneC = false;
        bots[2]->lobby.CreateRoomUVE(
            "void", "cat", 4U,
            [&](const Network::LobbyRoomIdUVE room, const std::string& error) {
                roomC = room;
                doneC = error.empty();
            });
        PumpLobbyUVE(6);
        CheckUVE(doneC && roomC == 2U, "cat creates the void as room 2");

        server.BindRelayUVE();
        const Network::RelayRoomIdUVE expected[3] = {1U, 1U, 2U};
        for (std::size_t i = 0U; i < 3U; ++i) {
            CheckUVE(server.relay.PeerRoomUVE(server.peerIds[i]) == expected[i],
                     "relay peer " + std::to_string(i) + " bound to its lobby room");
        }
        // Bob's join pushed to the one survivor: Amy heard it, no one else heard anything.
        CheckUVE(bots[0]->rosterPushes == 1U, "amy heard bob's join push");
        CheckUVE(bots[1]->rosterPushes == 0U, "bob heard no pushes");
        CheckUVE(bots[2]->rosterPushes == 0U, "cat heard no pushes");
    }

    void TickUVE(const std::uint64_t tick) {
        nowMs = kBaseMsUVE + (tick + 1U) * kTickMsUVE;
        for (auto& botPtr : bots) {
            BotUVE& bot = *botPtr;
            bot.DriveTickUVE(tick);
            const Network::PredictionTickUVE tickNumber = Scene::TickPredictedPawnUVE(
                bot.entityManager, bot.sceneGraph, bot.pawn, bot.predictor);
            CheckUVE(tickNumber == tick + 1U,
                     bot.name + " ticks in order at " + std::to_string(tick));
        }
        PumpPredictionUVE();
        PumpPredictionUVE();
        for (auto& botPtr : bots) {
            BotUVE& bot = *botPtr;
            CheckUVE(Scene::SyncPredictedPawnUVE(bot.entityManager, bot.sceneGraph, bot.pawn,
                                                 bot.predictor),
                     bot.name + " syncs its pawn at tick " + std::to_string(tick));
        }

        server.MoveNpcUVE();
        const Physics::Character3DStepResultUVE walked = server.StepWalkerUVE();
        if (tick % 100U == 0U) {
            CheckUVE(walked.stepped, "walker steps at tick " + std::to_string(tick));
        }

        PumpReplicationUVE();
        PumpReplicationUVE();

        if (tick % 30U == 0U) {
            for (auto& botPtr : bots) {
                BotUVE& bot = *botPtr;
                CheckUVE(bot.ShoutUVE(tick),
                         bot.name + " shouts at tick " + std::to_string(tick));
            }
        }
        PumpRelayUVE();
        server.BindRelayUVE();
        PumpLobbyUVE(1);

        if (tick % 60U == 0U) {
            for (auto& botPtr : bots) {
                BotUVE& bot = *botPtr;
                bot.RefreshRosterUVE();
            }
        }
        if (tick == 300U) {
            // Mid-run health: nothing dead, nothing stripped, nothing to prune anywhere.
            for (std::size_t i = 0U; i < bots.size(); ++i) {
                CheckUVE(Scene::PruneReplicatedEntitiesUVE(bots[i]->entityManager,
                                                           bots[i]->replicator, bots[i]->map) == 0U,
                         bots[i]->name + " prunes nothing mid-run");
                CheckUVE(Scene::PruneReplicatedEntitiesUVE(server.entityManager,
                                                           server.replicators[i],
                                                           server.maps[i]) == 0U,
                         "server prunes nothing mid-run");
            }
        }
        if (tick % 100U == 0U) {
            for (auto& botPtr : bots) {
                BotUVE& bot = *botPtr;
                RefusalProbeUVE(bot, tick);
            }
            SayUVE("tick " + std::to_string(tick) + ": amy " + VecUVE(bots[0]->PawnPoseUVE()) +
                   " r=" + std::to_string(bots[0]->predictor.StatsUVE().reconciles) + ", bob " +
                   VecUVE(bots[1]->PawnPoseUVE()) + " r=" +
                   std::to_string(bots[1]->predictor.StatsUVE().reconciles) + ", cat " +
                   VecUVE(bots[2]->PawnPoseUVE()) + " r=" +
                   std::to_string(bots[2]->predictor.StatsUVE().reconciles) + ", npc x=" +
                   std::to_string(server.npcX));
        }
    }

    void RefusalProbeUVE(BotUVE& bot, const std::uint64_t tick) {
        // The live no-double-step proof: the real mover refuses the tagged pawn and leaves
        // its pose exactly where prediction put it.
        const float xBefore = bot.PawnPoseUVE().x;
        Physics::CharacterMotionInputUVE input;
        input.move = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
        const Physics::Character3DStepResultUVE report = Physics::StepCharacter3DUVE(
            bot.entityManager, bot.sceneGraph, server.collision, bot.pawn, input, kGravityYUVE,
            kFixedDtUVE);
        CheckUVE(report.code == Physics::Character3DStepCodeUVE::PredictedElsewhere &&
                     !report.stepped,
                 bot.name + " still refuses the mover at tick " + std::to_string(tick));
        CheckUVE(bot.PawnPoseUVE().x == xBefore,
                 bot.name + " unmoved by the refusal at tick " + std::to_string(tick));
    }

    void SettleUVE() {
        // Flush the cadenced tails (replication sends every 100 ms of sim): a few long
        // rounds so the last poses land exactly.
        for (int i = 0; i < 4; ++i) {
            nowMs += 100U;
            PumpReplicationUVE();
            PumpReplicationUVE();
            PumpPredictionUVE();
            PumpRelayUVE();
            PumpLobbyUVE(1);
        }
    }

    void VerifyUVE(DedicatedServerSampleStatsUVE& stats) {
        for (std::size_t i = 0U; i < bots.size(); ++i) {
            BotUVE& bot = *bots[i];
            CheckUVE(bot.lobby.SessionUVE().StateUVE() ==
                         Network::SessionStateUVE::Established,
                     bot.name + " lobby survives the run");
            CheckUVE(bot.relaySession.StateUVE() == Network::SessionStateUVE::Established,
                     bot.name + " relay survives the run");
            CheckUVE(bot.replicator.SessionUVE().StateUVE() ==
                         Network::SessionStateUVE::Established,
                     bot.name + " replication survives the run");
            CheckUVE(bot.predictor.SessionUVE().StateUVE() ==
                         Network::SessionStateUVE::Established,
                     bot.name + " prediction survives the run");
        }

        CheckUVE(bots[0]->lobby.CurrentRoomUVE() == 1U, "amy ends in room 1");
        CheckUVE(bots[1]->lobby.CurrentRoomUVE() == 1U, "bob ends in room 1");
        CheckUVE(bots[2]->lobby.CurrentRoomUVE() == 2U, "cat ends in room 2");
        CheckUVE(bots[0]->lastRoster == std::vector<std::string>{"amy", "bob"},
                 "arena roster refreshes to amy, bob");
        CheckUVE(bots[1]->lastRoster == std::vector<std::string>{"amy", "bob"},
                 "bob's arena roster refreshes too");
        CheckUVE(bots[2]->lastRoster == std::vector<std::string>{"cat"},
                 "void roster refreshes to cat");

        const Network::RelayRoomIdUVE expected[3] = {1U, 1U, 2U};
        for (std::size_t i = 0U; i < 3U; ++i) {
            CheckUVE(server.relay.PeerRoomUVE(server.peerIds[i]) == expected[i],
                     "relay peer " + std::to_string(i) + " stays bound");
        }

        const std::size_t expectedShouts = (config.ticks + 29U) / 30U;
        CheckShoutsUVE(*bots[0], "bob@", expectedShouts);
        CheckShoutsUVE(*bots[1], "amy@", expectedShouts);
        CheckUVE(bots[2]->heard.empty(), "cat hears nothing in the void");

        for (std::size_t i = 0U; i < bots.size(); ++i) {
            const Math::Vector3UVE pawn =
                bots[i]->entityManager
                    .GetComponentUVE<Scene::WorldTransformComponentUVE>(bots[i]->pawn)
                    .worldPosition;
            const Math::Vector3UVE doll =
                server.entityManager
                    .GetComponentUVE<Scene::WorldTransformComponentUVE>(server.dolls[i])
                    .worldPosition;
            CheckUVE(doll == pawn, bots[i]->name + "'s server doll tracks exactly");
            const Math::Vector3UVE npcBot =
                bots[i]->entityManager
                    .GetComponentUVE<Scene::WorldTransformComponentUVE>(bots[i]->npcDoll)
                    .worldPosition;
            const Math::Vector3UVE npcServer =
                server.entityManager
                    .GetComponentUVE<Scene::WorldTransformComponentUVE>(server.npc)
                    .worldPosition;
            CheckUVE(npcBot == npcServer, bots[i]->name + "'s npc doll tracks exactly");

            const Network::PredictionStatsUVE prediction = bots[i]->predictor.StatsUVE();
            CheckUVE(prediction.reconciles > 0U,
                     bots[i]->name + " reconciled its landings");
            CheckUVE(bots[i]->predictor.LastAckedTickUVE() == config.ticks,
                     bots[i]->name + " acked every tick");
            const Scene::CharacterControllerComponentUVE& controller =
                bots[i]->entityManager
                    .GetComponentUVE<Scene::CharacterControllerComponentUVE>(bots[i]->pawn);
            CheckUVE(pawn.y == 0.0F && controller.grounded,
                     bots[i]->name + " rests on the server floor");
        }

        const Math::Vector3UVE walker =
            server.entityManager
                .GetComponentUVE<Scene::WorldTransformComponentUVE>(server.walker)
                .worldPosition;
        const Scene::CharacterControllerComponentUVE& walkerController =
            server.entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(
                server.walker);
        CheckUVE(walker.x > 12.0F && walkerController.grounded,
                 "walker crossed the floor and stays grounded");

        stats.ticks = config.ticks;
        stats.reconcilesAmy = bots[0]->predictor.StatsUVE().reconciles;
        stats.reconcilesBob = bots[1]->predictor.StatsUVE().reconciles;
        stats.reconcilesCat = bots[2]->predictor.StatsUVE().reconciles;
        stats.shoutsHeardAmy = bots[0]->heard.size();
        stats.shoutsHeardBob = bots[1]->heard.size();
        stats.shoutsHeardCat = bots[2]->heard.size();
        stats.snapshotsReceived = 0U;
        for (const Network::NetworkReplicatorUVE& replicator : server.replicators) {
            stats.snapshotsReceived += replicator.StatsUVE().snapshotsReceived;
        }
        CheckUVE(stats.snapshotsReceived > 0U, "the server received snapshots");
        stats.restY = bots[0]->PawnPoseUVE().y;
        stats.walkerX = walker.x;
    }

    void CheckShoutsUVE(const BotUVE& bot, const std::string& prefix,
                        const std::size_t expected) {
        CheckUVE(bot.heard.size() == expected,
                 bot.name + " heard " + std::to_string(bot.heard.size()) + " shouts");
        for (const std::string& shout : bot.heard) {
            CheckUVE(shout.rfind(prefix, 0U) == 0U,
                     bot.name + " heard only roommates (" + shout + ")");
        }
    }

    [[nodiscard]] DedicatedServerSampleResultUVE RunUVE() {
        SayUVE("dedicated server sample: 1 server, 3 bots (amy+bob in arena, cat in void)");
        BuildUVE();
        SayUVE("built: 12 loopback pairs (lobby, relay, replication, prediction per bot)");
        EstablishUVE();
        SayUVE("established: every session up on both ends");
        LobbyPhaseUVE();
        SayUVE("lobby: arena {amy, bob}, void {cat}; relay bound to match");
        for (std::uint64_t tick = 0U; tick < config.ticks; ++tick) {
            TickUVE(tick);
        }
        SayUVE("ran " + std::to_string(config.ticks) + " fixed steps");
        SettleUVE();
        DedicatedServerSampleStatsUVE stats;
        VerifyUVE(stats);
        SayUVE("verified: " + std::to_string(failures.size()) + " violation(s)");
        DedicatedServerSampleResultUVE result;
        result.success = failures.empty();
        result.failures = failures;
        result.stats = stats;
        return result;
    }
};

} // namespace

DedicatedServerSampleResultUVE RunDedicatedServerSampleUVE(
    const DedicatedServerSampleConfigUVE& config) {
    StoryUVE story;
    story.config = config;
    return story.RunUVE();
}

} // namespace UVE::Samples
