// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "entity.h"
#include "blueprint_object_host.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "network_relay.h"
#include "network_session.h"
#include "network_transport.h"
#include "blueprint_compiler.h"
#include "blueprint_instance.h"
#include "blueprint_value.h"

namespace UVE::Scene::Tests {
namespace {

struct ScriptRelayHarness final {
    Network::NetworkRelay relay;
    // Client 0 is the scripted one; the rest are C++-driven sparring partners. Add them
    // all before the host takes client 0's address - the vector must not move after that.
    std::vector<Network::NetworkSession> clients;
    std::vector<Network::LoopbackTransportPair> links;
    std::vector<Network::RelayPeerId> ids;

    void AddClient() {
        Network::LoopbackTransportPair pair =
            Network::CreateLoopbackTransportPair(256U);
        Network::NetworkSession client(
            Network::SessionRole::Client,
            std::make_unique<Network::LoopbackEndpoint>(pair.a));
        Network::NetworkSession server(
            Network::SessionRole::Server,
            std::make_unique<Network::LoopbackEndpoint>(pair.b));
        ids.push_back(relay.AddPeer(std::move(server)));
        clients.push_back(std::move(client));
        links.push_back(std::move(pair));
    }

    void UpdateAll(const std::uint64_t nowMs) {
        for (auto& client : clients) {
            client.Update(nowMs);
        }
        relay.Update(nowMs);
        for (auto& client : clients) {
            client.Update(nowMs);
        }
    }

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        for (auto& client : clients) {
            if (!client.Connect(nowMs)) {
                return false;
            }
        }
        UpdateAll(nowMs);
        UpdateAll(nowMs);
        for (auto& client : clients) {
            if (client.State() != Network::SessionState::Established) {
                return false;
            }
        }
        for (const Network::RelayPeerId id : ids) {
            const Network::NetworkSession* server = relay.PeerSession(id);
            if (server == nullptr ||
                server->State() != Network::SessionState::Established) {
                return false;
            }
        }
        return true;
    }
};

[[nodiscard]] std::vector<std::uint8_t> Bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

[[nodiscard]] std::string Text(const std::vector<std::uint8_t>& bytes) {
    return std::string{bytes.begin(), bytes.end()};
}

// One script for every test below: the calls under test, so each test compiles the same
// source and drives a different path through it. All synchronous - no events to raise.
inline constexpr const char* kRelayScript = R"(
fn peer_count() -> int:
    return relay.peer_count()

fn set_room(peer: int, room: int) -> bool:
    return relay.set_room(peer, room)

fn room(peer: int) -> int:
    return relay.room(peer)

fn set_interest(receiver: int, sender: int, should: bool) -> bool:
    return relay.set_interest(receiver, sender, should)

fn interested(receiver: int, sender: int) -> bool:
    return relay.interested(receiver, sender)

fn remove_peer(peer: int) -> bool:
    return relay.remove(peer)

fn stat_received() -> int:
    return relay.stat_received()

fn stat_forwarded() -> int:
    return relay.stat_forwarded()

fn stat_dropped() -> int:
    return relay.stat_dropped()

fn peer_state(peer: int) -> str:
    return relay.peer_state(peer)

fn send_text(text: str) -> bool:
    return relay.send(text)

fn poll() -> str:
    return relay.poll()
)";

struct RelaySignature final {
    const char* name;
    std::vector<Blueprint::Type> params;
    Blueprint::Type result;
};

class BlueprintRelayBindingsTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};

    [[nodiscard]] std::shared_ptr<const Blueprint::Program> CompileOrFail(
        const std::string& source, Core::BlueprintObjectHost& host) {
        const Blueprint::CompileResult result = Blueprint::CompileBlueprintNotation(source, host);
        for (const Blueprint::Diagnostic& d : result.diagnostics) {
            ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
        }
        return result.program;
    }

    [[nodiscard]] std::int64_t CallInt(Blueprint::BlueprintInstance& instance,
                                          const std::string& function) {
        return std::get<std::int64_t>(*instance.Call(function));
    }

    [[nodiscard]] std::int64_t CallInt(Blueprint::BlueprintInstance& instance,
                                          const std::string& function, const std::int64_t id) {
        const std::array<Blueprint::Value, 1> args{Blueprint::Value{id}};
        return std::get<std::int64_t>(*instance.Call(function, args));
    }

    [[nodiscard]] std::string CallStr(Blueprint::BlueprintInstance& instance,
                                         const std::string& function) {
        return std::get<std::string>(*instance.Call(function));
    }

    [[nodiscard]] std::string CallStr(Blueprint::BlueprintInstance& instance,
                                         const std::string& function, const std::int64_t id) {
        const std::array<Blueprint::Value, 1> args{Blueprint::Value{id}};
        return std::get<std::string>(*instance.Call(function, args));
    }

    [[nodiscard]] bool CallBool(Blueprint::BlueprintInstance& instance,
                                   const std::string& function, const std::int64_t id) {
        const std::array<Blueprint::Value, 1> args{Blueprint::Value{id}};
        return std::get<bool>(*instance.Call(function, args));
    }

    [[nodiscard]] bool CallBool2(Blueprint::BlueprintInstance& instance,
                                    const std::string& function, const std::int64_t first,
                                    const std::int64_t second) {
        const std::array<Blueprint::Value, 2> args{Blueprint::Value{first},
                                                     Blueprint::Value{second}};
        return std::get<bool>(*instance.Call(function, args));
    }

    [[nodiscard]] bool CallBool3(Blueprint::BlueprintInstance& instance,
                                    const std::string& function, const std::int64_t first,
                                    const std::int64_t second, const bool third) {
        const std::array<Blueprint::Value, 3> args{
            Blueprint::Value{first}, Blueprint::Value{second}, Blueprint::Value{third}};
        return std::get<bool>(*instance.Call(function, args));
    }

    [[nodiscard]] bool CallBoolStr(Blueprint::BlueprintInstance& instance,
                                      const std::string& function, const std::string& text) {
        const std::array<Blueprint::Value, 1> args{Blueprint::Value{text}};
        return std::get<bool>(*instance.Call(function, args));
    }
};

TEST_F(BlueprintRelayBindingsTest, Host_DescribesRelaySurfaceUngated) {
    // Ungated means ungated: a bare entity with neither attachment still describes every
    // name, and every call through them fails closed at run time instead.
    Core::BlueprintObjectHost host{entityManager, nullptr, entityManager.CreateEntity()};
    const RelaySignature cases[] = {
        {"relay.peer_count", {}, Blueprint::Type::Int()},
        {"relay.set_room",
         {Blueprint::Type::Int(), Blueprint::Type::Int()},
         Blueprint::Type::Bool()},
        {"relay.room", {Blueprint::Type::Int()}, Blueprint::Type::Int()},
        {"relay.set_interest",
         {Blueprint::Type::Int(), Blueprint::Type::Int(),
          Blueprint::Type::Bool()},
         Blueprint::Type::Bool()},
        {"relay.interested",
         {Blueprint::Type::Int(), Blueprint::Type::Int()},
         Blueprint::Type::Bool()},
        {"relay.remove", {Blueprint::Type::Int()}, Blueprint::Type::Bool()},
        {"relay.stat_received", {}, Blueprint::Type::Int()},
        {"relay.stat_forwarded", {}, Blueprint::Type::Int()},
        {"relay.stat_dropped", {}, Blueprint::Type::Int()},
        {"relay.peer_state", {Blueprint::Type::Int()}, Blueprint::Type::Str()},
        {"relay.send", {Blueprint::Type::Str()}, Blueprint::Type::Bool()},
        {"relay.poll", {}, Blueprint::Type::Str()},
    };
    for (const RelaySignature& expected : cases) {
        const std::optional<Blueprint::HostFunction> described =
            host.DescribeFunction(expected.name);
        ASSERT_TRUE(described.has_value()) << expected.name;
        EXPECT_EQ(described->params, expected.params) << expected.name;
        EXPECT_EQ(described->result, expected.result) << expected.name;
    }
}

TEST_F(BlueprintRelayBindingsTest, Host_FailsClosedWithoutAttachments) {
    Core::BlueprintObjectHost host{entityManager, nullptr, entityManager.CreateEntity()};
    const std::span<const Blueprint::Value> noArgs{};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("relay.peer_count", noArgs)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("relay.stat_received", noArgs)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("relay.stat_forwarded", noArgs)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("relay.stat_dropped", noArgs)), 0);
    EXPECT_EQ(std::get<std::string>(host.CallFunction("relay.poll", noArgs)), "");
    const std::array<Blueprint::Value, 1> id{Blueprint::Value{std::int64_t{1}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("relay.room", id)), 0);
    EXPECT_FALSE(std::get<bool>(host.CallFunction("relay.remove", id)));
    EXPECT_EQ(std::get<std::string>(host.CallFunction("relay.peer_state", id)), "unknown");
    const std::array<Blueprint::Value, 2> pair{Blueprint::Value{std::int64_t{1}},
                                                 Blueprint::Value{std::int64_t{0}}};
    EXPECT_FALSE(std::get<bool>(host.CallFunction("relay.set_room", pair)));
    EXPECT_FALSE(std::get<bool>(host.CallFunction("relay.interested", pair)));
    const std::array<Blueprint::Value, 3> triple{Blueprint::Value{std::int64_t{1}},
                                                   Blueprint::Value{std::int64_t{2}},
                                                   Blueprint::Value{true}};
    EXPECT_FALSE(std::get<bool>(host.CallFunction("relay.set_interest", triple)));
    const std::array<Blueprint::Value, 1> text{Blueprint::Value{std::string{"hi"}}};
    EXPECT_FALSE(std::get<bool>(host.CallFunction("relay.send", text)));
}

TEST_F(BlueprintRelayBindingsTest, Script_ManagesRoomsAndRoutes) {
    ScriptRelayHarness harness;
    harness.AddClient();
    harness.AddClient();
    harness.AddClient();
    ASSERT_TRUE(harness.Establish(1000U));
    Core::BlueprintObjectHost host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntity(),
                                     nullptr,
                                     nullptr,
                                     Core::BlueprintObjectHost::InstanceResolver{},
                                     nullptr,
                                     Core::RpcResponseSink{},
                                     nullptr,
                                     Core::LobbyResultSink{},
                                     Core::LobbyRosterSink{},
                                     &harness.relay,
                                     &harness.clients[0]};
    const auto program = CompileOrFail(kRelayScript, host);
    ASSERT_NE(program, nullptr);
    Blueprint::BlueprintInstance instance(program, host);

    EXPECT_EQ(CallInt(instance, "peer_count"), 3);
    const std::int64_t moved = harness.ids[2];
    EXPECT_EQ(CallInt(instance, "room", moved), 0);
    EXPECT_TRUE(CallBool2(instance, "set_room", moved, 7));
    EXPECT_EQ(CallInt(instance, "room", moved), 7);
    EXPECT_EQ(harness.relay.PeerRoom(harness.ids[2]), 7U);
    EXPECT_FALSE(CallBool2(instance, "set_room", 99, 7));

    EXPECT_TRUE(CallBoolStr(instance, "send_text", "hi"));
    harness.UpdateAll(1100U);
    const std::optional<std::vector<std::uint8_t>> roommate =
        harness.clients[1].TryReceiveData();
    ASSERT_TRUE(roommate.has_value());
    EXPECT_EQ(Text(*roommate), "hi");
    EXPECT_FALSE(harness.clients[2].TryReceiveData().has_value());
    EXPECT_FALSE(harness.clients[0].TryReceiveData().has_value());
}

TEST_F(BlueprintRelayBindingsTest, Script_ManagesInterestMutes) {
    ScriptRelayHarness harness;
    harness.AddClient();
    harness.AddClient();
    harness.AddClient();
    ASSERT_TRUE(harness.Establish(1000U));
    Core::BlueprintObjectHost host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntity(),
                                     nullptr,
                                     nullptr,
                                     Core::BlueprintObjectHost::InstanceResolver{},
                                     nullptr,
                                     Core::RpcResponseSink{},
                                     nullptr,
                                     Core::LobbyResultSink{},
                                     Core::LobbyRosterSink{},
                                     &harness.relay,
                                     &harness.clients[0]};
    const auto program = CompileOrFail(kRelayScript, host);
    ASSERT_NE(program, nullptr);
    Blueprint::BlueprintInstance instance(program, host);

    const std::int64_t muted = harness.ids[1];
    const std::int64_t speaker = harness.ids[0];
    EXPECT_TRUE(CallBool2(instance, "interested", muted, speaker));
    EXPECT_TRUE(CallBool3(instance, "set_interest", muted, speaker, false));
    EXPECT_FALSE(CallBool2(instance, "interested", muted, speaker));
    EXPECT_FALSE(harness.relay.IsPeerInterested(harness.ids[1], harness.ids[0]));
    EXPECT_FALSE(CallBool3(instance, "set_interest", 99, speaker, false));

    EXPECT_TRUE(CallBoolStr(instance, "send_text", "hi"));
    harness.UpdateAll(1100U);
    EXPECT_FALSE(harness.clients[1].TryReceiveData().has_value());
    const std::optional<std::vector<std::uint8_t>> overheard =
        harness.clients[2].TryReceiveData();
    ASSERT_TRUE(overheard.has_value());
    EXPECT_EQ(Text(*overheard), "hi");

    EXPECT_TRUE(CallBool3(instance, "set_interest", muted, speaker, true));
    EXPECT_TRUE(CallBool2(instance, "interested", muted, speaker));
}

TEST_F(BlueprintRelayBindingsTest, Script_RemovesPeersAndReadsStates) {
    ScriptRelayHarness harness;
    harness.AddClient();
    harness.AddClient();
    ASSERT_TRUE(harness.Establish(1000U));
    Core::BlueprintObjectHost host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntity(),
                                     nullptr,
                                     nullptr,
                                     Core::BlueprintObjectHost::InstanceResolver{},
                                     nullptr,
                                     Core::RpcResponseSink{},
                                     nullptr,
                                     Core::LobbyResultSink{},
                                     Core::LobbyRosterSink{},
                                     &harness.relay,
                                     &harness.clients[0]};
    const auto program = CompileOrFail(kRelayScript, host);
    ASSERT_NE(program, nullptr);
    Blueprint::BlueprintInstance instance(program, host);

    EXPECT_EQ(CallInt(instance, "peer_count"), 2);
    EXPECT_EQ(CallStr(instance, "peer_state", harness.ids[0]), "established");
    EXPECT_EQ(CallStr(instance, "peer_state", 99), "unknown");
    EXPECT_TRUE(CallBool(instance, "remove_peer", harness.ids[1]));
    EXPECT_EQ(CallInt(instance, "peer_count"), 1);
    EXPECT_EQ(harness.relay.PeerCount(), 2U - 1U);
    EXPECT_FALSE(CallBool(instance, "remove_peer", harness.ids[1]));
    EXPECT_EQ(CallStr(instance, "peer_state", harness.ids[1]), "unknown");
}

TEST_F(BlueprintRelayBindingsTest, Script_ReadsStats) {
    ScriptRelayHarness harness;
    harness.AddClient();
    harness.AddClient();
    harness.AddClient();
    ASSERT_TRUE(harness.Establish(1000U));
    Core::BlueprintObjectHost host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntity(),
                                     nullptr,
                                     nullptr,
                                     Core::BlueprintObjectHost::InstanceResolver{},
                                     nullptr,
                                     Core::RpcResponseSink{},
                                     nullptr,
                                     Core::LobbyResultSink{},
                                     Core::LobbyRosterSink{},
                                     &harness.relay,
                                     &harness.clients[0]};
    const auto program = CompileOrFail(kRelayScript, host);
    ASSERT_NE(program, nullptr);
    Blueprint::BlueprintInstance instance(program, host);

    EXPECT_TRUE(CallBoolStr(instance, "send_text", "a"));
    harness.UpdateAll(1100U);
    EXPECT_TRUE(CallBoolStr(instance, "send_text", "b"));
    harness.UpdateAll(1200U);
    EXPECT_EQ(CallInt(instance, "stat_received"), 2);
    EXPECT_EQ(CallInt(instance, "stat_forwarded"), 4);
    EXPECT_EQ(CallInt(instance, "stat_dropped"), 0);
    EXPECT_EQ(harness.relay.Stats().payloadsReceived, 2U);
    EXPECT_EQ(harness.relay.Stats().payloadsForwarded, 4U);
}

TEST_F(BlueprintRelayBindingsTest, Script_SendsAndPolls) {
    ScriptRelayHarness harness;
    harness.AddClient();
    harness.AddClient();
    ASSERT_TRUE(harness.Establish(1000U));
    Core::BlueprintObjectHost host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntity(),
                                     nullptr,
                                     nullptr,
                                     Core::BlueprintObjectHost::InstanceResolver{},
                                     nullptr,
                                     Core::RpcResponseSink{},
                                     nullptr,
                                     Core::LobbyResultSink{},
                                     Core::LobbyRosterSink{},
                                     &harness.relay,
                                     &harness.clients[0]};
    const auto program = CompileOrFail(kRelayScript, host);
    ASSERT_NE(program, nullptr);
    Blueprint::BlueprintInstance instance(program, host);

    EXPECT_EQ(CallStr(instance, "poll"), "");
    EXPECT_TRUE(CallBoolStr(instance, "send_text", "hi"));
    harness.UpdateAll(1100U);
    const std::optional<std::vector<std::uint8_t>> heard =
        harness.clients[1].TryReceiveData();
    ASSERT_TRUE(heard.has_value());
    EXPECT_EQ(Text(*heard), "hi");

    ASSERT_TRUE(harness.clients[1].SendData(Bytes("yo")));
    harness.UpdateAll(1200U);
    EXPECT_EQ(CallStr(instance, "poll"), "yo");
    EXPECT_EQ(CallStr(instance, "poll"), "");
}

TEST_F(BlueprintRelayBindingsTest, Script_RefusesOutOfRangeIds) {
    ScriptRelayHarness harness;
    harness.AddClient();
    ASSERT_TRUE(harness.Establish(1000U));
    Core::BlueprintObjectHost host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntity(),
                                     nullptr,
                                     nullptr,
                                     Core::BlueprintObjectHost::InstanceResolver{},
                                     nullptr,
                                     Core::RpcResponseSink{},
                                     nullptr,
                                     Core::LobbyResultSink{},
                                     Core::LobbyRosterSink{},
                                     &harness.relay,
                                     &harness.clients[0]};
    const auto program = CompileOrFail(kRelayScript, host);
    ASSERT_NE(program, nullptr);
    Blueprint::BlueprintInstance instance(program, host);

    constexpr std::int64_t kPastU32 = 4294967296LL;
    EXPECT_FALSE(CallBool2(instance, "set_room", -1, 0));
    EXPECT_FALSE(CallBool2(instance, "set_room", 1, -1));
    EXPECT_FALSE(CallBool2(instance, "set_room", 1, kPastU32));
    EXPECT_EQ(CallInt(instance, "room", -1), 0);
    EXPECT_FALSE(CallBool2(instance, "interested", -1, 1));
    EXPECT_FALSE(CallBool2(instance, "interested", 1, kPastU32));
    EXPECT_FALSE(CallBool3(instance, "set_interest", 1, -1, true));
    EXPECT_FALSE(CallBool(instance, "remove_peer", kPastU32));
    EXPECT_FALSE(CallBool(instance, "remove_peer", -1));
    EXPECT_EQ(CallStr(instance, "peer_state", -5), "unknown");

    // In-range but unknown is not a refusal: the core's own defaults show through.
    EXPECT_EQ(CallInt(instance, "room", 99), 0);
    EXPECT_TRUE(CallBool2(instance, "interested", 99, 98));
    EXPECT_FALSE(CallBool(instance, "remove_peer", 99));
    EXPECT_EQ(harness.relay.PeerCount(), 1U);
}

} // namespace
} // namespace UVE::Scene::Tests
