// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/core/uvscript_object_host_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/network/network_relay_uve.h"
#include "uve/network/network_session_uve.h"
#include "uve/network/network_transport_uve.h"
#include "uve/uvscript/uvscript_compiler_uve.h"
#include "uve/uvscript/uvscript_instance_uve.h"
#include "uve/uvscript/uvscript_value_uve.h"

namespace UVE::Scene::Tests {
namespace {

struct ScriptRelayHarnessUVE final {
    Network::NetworkRelayUVE relay;
    // Client 0 is the scripted one; the rest are C++-driven sparring partners. Add them
    // all before the host takes client 0's address - the vector must not move after that.
    std::vector<Network::NetworkSessionUVE> clients;
    std::vector<Network::LoopbackTransportPairUVE> links;
    std::vector<Network::RelayPeerIdUVE> ids;

    void AddClientUVE() {
        Network::LoopbackTransportPairUVE pair =
            Network::CreateLoopbackTransportPairUVE(256U);
        Network::NetworkSessionUVE client(
            Network::SessionRoleUVE::Client,
            std::make_unique<Network::LoopbackEndpointUVE>(pair.a));
        Network::NetworkSessionUVE server(
            Network::SessionRoleUVE::Server,
            std::make_unique<Network::LoopbackEndpointUVE>(pair.b));
        ids.push_back(relay.AddPeerUVE(std::move(server)));
        clients.push_back(std::move(client));
        links.push_back(std::move(pair));
    }

    void UpdateAllUVE(const std::uint64_t nowMs) {
        for (auto& client : clients) {
            client.UpdateUVE(nowMs);
        }
        relay.UpdateUVE(nowMs);
        for (auto& client : clients) {
            client.UpdateUVE(nowMs);
        }
    }

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        for (auto& client : clients) {
            if (!client.ConnectUVE(nowMs)) {
                return false;
            }
        }
        UpdateAllUVE(nowMs);
        UpdateAllUVE(nowMs);
        for (auto& client : clients) {
            if (client.StateUVE() != Network::SessionStateUVE::Established) {
                return false;
            }
        }
        for (const Network::RelayPeerIdUVE id : ids) {
            const Network::NetworkSessionUVE* server = relay.PeerSessionUVE(id);
            if (server == nullptr ||
                server->StateUVE() != Network::SessionStateUVE::Established) {
                return false;
            }
        }
        return true;
    }
};

[[nodiscard]] std::vector<std::uint8_t> BytesUVE(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

[[nodiscard]] std::string TextUVE(const std::vector<std::uint8_t>& bytes) {
    return std::string{bytes.begin(), bytes.end()};
}

// One script for every test below: the calls under test, so each test compiles the same
// source and drives a different path through it. All synchronous - no events to raise.
inline constexpr const char* kRelayScriptUVE = R"(
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

struct RelaySignatureUVE final {
    const char* name;
    std::vector<UVScript::TypeUVE> params;
    UVScript::TypeUVE result;
};

class UVScriptRelayBindingsUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    [[nodiscard]] std::shared_ptr<const UVScript::ProgramUVE> CompileOrFailUVE(
        const std::string& source, Core::UVScriptObjectHostUVE& host) {
        const UVScript::CompileResultUVE result = UVScript::CompileUVScriptSourceUVE(source, host);
        for (const UVScript::DiagnosticUVE& d : result.diagnostics) {
            ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
        }
        return result.program;
    }

    [[nodiscard]] std::int64_t CallIntUVE(UVScript::ScriptInstanceUVE& instance,
                                          const std::string& function) {
        return std::get<std::int64_t>(*instance.CallUVE(function));
    }

    [[nodiscard]] std::int64_t CallIntUVE(UVScript::ScriptInstanceUVE& instance,
                                          const std::string& function, const std::int64_t id) {
        const std::array<UVScript::ValueUVE, 1> args{UVScript::ValueUVE{id}};
        return std::get<std::int64_t>(*instance.CallUVE(function, args));
    }

    [[nodiscard]] std::string CallStrUVE(UVScript::ScriptInstanceUVE& instance,
                                         const std::string& function) {
        return std::get<std::string>(*instance.CallUVE(function));
    }

    [[nodiscard]] std::string CallStrUVE(UVScript::ScriptInstanceUVE& instance,
                                         const std::string& function, const std::int64_t id) {
        const std::array<UVScript::ValueUVE, 1> args{UVScript::ValueUVE{id}};
        return std::get<std::string>(*instance.CallUVE(function, args));
    }

    [[nodiscard]] bool CallBoolUVE(UVScript::ScriptInstanceUVE& instance,
                                   const std::string& function, const std::int64_t id) {
        const std::array<UVScript::ValueUVE, 1> args{UVScript::ValueUVE{id}};
        return std::get<bool>(*instance.CallUVE(function, args));
    }

    [[nodiscard]] bool CallBool2UVE(UVScript::ScriptInstanceUVE& instance,
                                    const std::string& function, const std::int64_t first,
                                    const std::int64_t second) {
        const std::array<UVScript::ValueUVE, 2> args{UVScript::ValueUVE{first},
                                                     UVScript::ValueUVE{second}};
        return std::get<bool>(*instance.CallUVE(function, args));
    }

    [[nodiscard]] bool CallBool3UVE(UVScript::ScriptInstanceUVE& instance,
                                    const std::string& function, const std::int64_t first,
                                    const std::int64_t second, const bool third) {
        const std::array<UVScript::ValueUVE, 3> args{
            UVScript::ValueUVE{first}, UVScript::ValueUVE{second}, UVScript::ValueUVE{third}};
        return std::get<bool>(*instance.CallUVE(function, args));
    }

    [[nodiscard]] bool CallBoolStrUVE(UVScript::ScriptInstanceUVE& instance,
                                      const std::string& function, const std::string& text) {
        const std::array<UVScript::ValueUVE, 1> args{UVScript::ValueUVE{text}};
        return std::get<bool>(*instance.CallUVE(function, args));
    }
};

TEST_F(UVScriptRelayBindingsUVETest, Host_DescribesRelaySurfaceUngated) {
    // Ungated means ungated: a bare entity with neither attachment still describes every
    // name, and every call through them fails closed at run time instead.
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, entityManager.CreateEntityUVE()};
    const RelaySignatureUVE cases[] = {
        {"relay.peer_count", {}, UVScript::TypeUVE::IntUVE()},
        {"relay.set_room",
         {UVScript::TypeUVE::IntUVE(), UVScript::TypeUVE::IntUVE()},
         UVScript::TypeUVE::BoolUVE()},
        {"relay.room", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::IntUVE()},
        {"relay.set_interest",
         {UVScript::TypeUVE::IntUVE(), UVScript::TypeUVE::IntUVE(),
          UVScript::TypeUVE::BoolUVE()},
         UVScript::TypeUVE::BoolUVE()},
        {"relay.interested",
         {UVScript::TypeUVE::IntUVE(), UVScript::TypeUVE::IntUVE()},
         UVScript::TypeUVE::BoolUVE()},
        {"relay.remove", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::BoolUVE()},
        {"relay.stat_received", {}, UVScript::TypeUVE::IntUVE()},
        {"relay.stat_forwarded", {}, UVScript::TypeUVE::IntUVE()},
        {"relay.stat_dropped", {}, UVScript::TypeUVE::IntUVE()},
        {"relay.peer_state", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::StrUVE()},
        {"relay.send", {UVScript::TypeUVE::StrUVE()}, UVScript::TypeUVE::BoolUVE()},
        {"relay.poll", {}, UVScript::TypeUVE::StrUVE()},
    };
    for (const RelaySignatureUVE& expected : cases) {
        const std::optional<UVScript::HostFunctionUVE> described =
            host.DescribeFunctionUVE(expected.name);
        ASSERT_TRUE(described.has_value()) << expected.name;
        EXPECT_EQ(described->params, expected.params) << expected.name;
        EXPECT_EQ(described->result, expected.result) << expected.name;
    }
}

TEST_F(UVScriptRelayBindingsUVETest, Host_FailsClosedWithoutAttachments) {
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, entityManager.CreateEntityUVE()};
    const std::span<const UVScript::ValueUVE> noArgs{};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("relay.peer_count", noArgs)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("relay.stat_received", noArgs)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("relay.stat_forwarded", noArgs)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("relay.stat_dropped", noArgs)), 0);
    EXPECT_EQ(std::get<std::string>(host.CallFunctionUVE("relay.poll", noArgs)), "");
    const std::array<UVScript::ValueUVE, 1> id{UVScript::ValueUVE{std::int64_t{1}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("relay.room", id)), 0);
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("relay.remove", id)));
    EXPECT_EQ(std::get<std::string>(host.CallFunctionUVE("relay.peer_state", id)), "unknown");
    const std::array<UVScript::ValueUVE, 2> pair{UVScript::ValueUVE{std::int64_t{1}},
                                                 UVScript::ValueUVE{std::int64_t{0}}};
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("relay.set_room", pair)));
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("relay.interested", pair)));
    const std::array<UVScript::ValueUVE, 3> triple{UVScript::ValueUVE{std::int64_t{1}},
                                                   UVScript::ValueUVE{std::int64_t{2}},
                                                   UVScript::ValueUVE{true}};
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("relay.set_interest", triple)));
    const std::array<UVScript::ValueUVE, 1> text{UVScript::ValueUVE{std::string{"hi"}}};
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("relay.send", text)));
}

TEST_F(UVScriptRelayBindingsUVETest, Script_ManagesRoomsAndRoutes) {
    ScriptRelayHarnessUVE harness;
    harness.AddClientUVE();
    harness.AddClientUVE();
    harness.AddClientUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    Core::UVScriptObjectHostUVE host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntityUVE(),
                                     nullptr,
                                     nullptr,
                                     Core::UVScriptObjectHostUVE::InstanceResolverUVE{},
                                     nullptr,
                                     Core::RpcResponseSinkUVE{},
                                     nullptr,
                                     Core::LobbyResultSinkUVE{},
                                     Core::LobbyRosterSinkUVE{},
                                     &harness.relay,
                                     &harness.clients[0]};
    const auto program = CompileOrFailUVE(kRelayScriptUVE, host);
    ASSERT_NE(program, nullptr);
    UVScript::ScriptInstanceUVE instance(program, host);

    EXPECT_EQ(CallIntUVE(instance, "peer_count"), 3);
    const std::int64_t moved = harness.ids[2];
    EXPECT_EQ(CallIntUVE(instance, "room", moved), 0);
    EXPECT_TRUE(CallBool2UVE(instance, "set_room", moved, 7));
    EXPECT_EQ(CallIntUVE(instance, "room", moved), 7);
    EXPECT_EQ(harness.relay.PeerRoomUVE(harness.ids[2]), 7U);
    EXPECT_FALSE(CallBool2UVE(instance, "set_room", 99, 7));

    EXPECT_TRUE(CallBoolStrUVE(instance, "send_text", "hi"));
    harness.UpdateAllUVE(1100U);
    const std::optional<std::vector<std::uint8_t>> roommate =
        harness.clients[1].TryReceiveDataUVE();
    ASSERT_TRUE(roommate.has_value());
    EXPECT_EQ(TextUVE(*roommate), "hi");
    EXPECT_FALSE(harness.clients[2].TryReceiveDataUVE().has_value());
    EXPECT_FALSE(harness.clients[0].TryReceiveDataUVE().has_value());
}

TEST_F(UVScriptRelayBindingsUVETest, Script_ManagesInterestMutes) {
    ScriptRelayHarnessUVE harness;
    harness.AddClientUVE();
    harness.AddClientUVE();
    harness.AddClientUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    Core::UVScriptObjectHostUVE host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntityUVE(),
                                     nullptr,
                                     nullptr,
                                     Core::UVScriptObjectHostUVE::InstanceResolverUVE{},
                                     nullptr,
                                     Core::RpcResponseSinkUVE{},
                                     nullptr,
                                     Core::LobbyResultSinkUVE{},
                                     Core::LobbyRosterSinkUVE{},
                                     &harness.relay,
                                     &harness.clients[0]};
    const auto program = CompileOrFailUVE(kRelayScriptUVE, host);
    ASSERT_NE(program, nullptr);
    UVScript::ScriptInstanceUVE instance(program, host);

    const std::int64_t muted = harness.ids[1];
    const std::int64_t speaker = harness.ids[0];
    EXPECT_TRUE(CallBool2UVE(instance, "interested", muted, speaker));
    EXPECT_TRUE(CallBool3UVE(instance, "set_interest", muted, speaker, false));
    EXPECT_FALSE(CallBool2UVE(instance, "interested", muted, speaker));
    EXPECT_FALSE(harness.relay.IsPeerInterestedUVE(harness.ids[1], harness.ids[0]));
    EXPECT_FALSE(CallBool3UVE(instance, "set_interest", 99, speaker, false));

    EXPECT_TRUE(CallBoolStrUVE(instance, "send_text", "hi"));
    harness.UpdateAllUVE(1100U);
    EXPECT_FALSE(harness.clients[1].TryReceiveDataUVE().has_value());
    const std::optional<std::vector<std::uint8_t>> overheard =
        harness.clients[2].TryReceiveDataUVE();
    ASSERT_TRUE(overheard.has_value());
    EXPECT_EQ(TextUVE(*overheard), "hi");

    EXPECT_TRUE(CallBool3UVE(instance, "set_interest", muted, speaker, true));
    EXPECT_TRUE(CallBool2UVE(instance, "interested", muted, speaker));
}

TEST_F(UVScriptRelayBindingsUVETest, Script_RemovesPeersAndReadsStates) {
    ScriptRelayHarnessUVE harness;
    harness.AddClientUVE();
    harness.AddClientUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    Core::UVScriptObjectHostUVE host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntityUVE(),
                                     nullptr,
                                     nullptr,
                                     Core::UVScriptObjectHostUVE::InstanceResolverUVE{},
                                     nullptr,
                                     Core::RpcResponseSinkUVE{},
                                     nullptr,
                                     Core::LobbyResultSinkUVE{},
                                     Core::LobbyRosterSinkUVE{},
                                     &harness.relay,
                                     &harness.clients[0]};
    const auto program = CompileOrFailUVE(kRelayScriptUVE, host);
    ASSERT_NE(program, nullptr);
    UVScript::ScriptInstanceUVE instance(program, host);

    EXPECT_EQ(CallIntUVE(instance, "peer_count"), 2);
    EXPECT_EQ(CallStrUVE(instance, "peer_state", harness.ids[0]), "established");
    EXPECT_EQ(CallStrUVE(instance, "peer_state", 99), "unknown");
    EXPECT_TRUE(CallBoolUVE(instance, "remove_peer", harness.ids[1]));
    EXPECT_EQ(CallIntUVE(instance, "peer_count"), 1);
    EXPECT_EQ(harness.relay.PeerCountUVE(), 2U - 1U);
    EXPECT_FALSE(CallBoolUVE(instance, "remove_peer", harness.ids[1]));
    EXPECT_EQ(CallStrUVE(instance, "peer_state", harness.ids[1]), "unknown");
}

TEST_F(UVScriptRelayBindingsUVETest, Script_ReadsStats) {
    ScriptRelayHarnessUVE harness;
    harness.AddClientUVE();
    harness.AddClientUVE();
    harness.AddClientUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    Core::UVScriptObjectHostUVE host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntityUVE(),
                                     nullptr,
                                     nullptr,
                                     Core::UVScriptObjectHostUVE::InstanceResolverUVE{},
                                     nullptr,
                                     Core::RpcResponseSinkUVE{},
                                     nullptr,
                                     Core::LobbyResultSinkUVE{},
                                     Core::LobbyRosterSinkUVE{},
                                     &harness.relay,
                                     &harness.clients[0]};
    const auto program = CompileOrFailUVE(kRelayScriptUVE, host);
    ASSERT_NE(program, nullptr);
    UVScript::ScriptInstanceUVE instance(program, host);

    EXPECT_TRUE(CallBoolStrUVE(instance, "send_text", "a"));
    harness.UpdateAllUVE(1100U);
    EXPECT_TRUE(CallBoolStrUVE(instance, "send_text", "b"));
    harness.UpdateAllUVE(1200U);
    EXPECT_EQ(CallIntUVE(instance, "stat_received"), 2);
    EXPECT_EQ(CallIntUVE(instance, "stat_forwarded"), 4);
    EXPECT_EQ(CallIntUVE(instance, "stat_dropped"), 0);
    EXPECT_EQ(harness.relay.StatsUVE().payloadsReceived, 2U);
    EXPECT_EQ(harness.relay.StatsUVE().payloadsForwarded, 4U);
}

TEST_F(UVScriptRelayBindingsUVETest, Script_SendsAndPolls) {
    ScriptRelayHarnessUVE harness;
    harness.AddClientUVE();
    harness.AddClientUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    Core::UVScriptObjectHostUVE host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntityUVE(),
                                     nullptr,
                                     nullptr,
                                     Core::UVScriptObjectHostUVE::InstanceResolverUVE{},
                                     nullptr,
                                     Core::RpcResponseSinkUVE{},
                                     nullptr,
                                     Core::LobbyResultSinkUVE{},
                                     Core::LobbyRosterSinkUVE{},
                                     &harness.relay,
                                     &harness.clients[0]};
    const auto program = CompileOrFailUVE(kRelayScriptUVE, host);
    ASSERT_NE(program, nullptr);
    UVScript::ScriptInstanceUVE instance(program, host);

    EXPECT_EQ(CallStrUVE(instance, "poll"), "");
    EXPECT_TRUE(CallBoolStrUVE(instance, "send_text", "hi"));
    harness.UpdateAllUVE(1100U);
    const std::optional<std::vector<std::uint8_t>> heard =
        harness.clients[1].TryReceiveDataUVE();
    ASSERT_TRUE(heard.has_value());
    EXPECT_EQ(TextUVE(*heard), "hi");

    ASSERT_TRUE(harness.clients[1].SendDataUVE(BytesUVE("yo")));
    harness.UpdateAllUVE(1200U);
    EXPECT_EQ(CallStrUVE(instance, "poll"), "yo");
    EXPECT_EQ(CallStrUVE(instance, "poll"), "");
}

TEST_F(UVScriptRelayBindingsUVETest, Script_RefusesOutOfRangeIds) {
    ScriptRelayHarnessUVE harness;
    harness.AddClientUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    Core::UVScriptObjectHostUVE host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntityUVE(),
                                     nullptr,
                                     nullptr,
                                     Core::UVScriptObjectHostUVE::InstanceResolverUVE{},
                                     nullptr,
                                     Core::RpcResponseSinkUVE{},
                                     nullptr,
                                     Core::LobbyResultSinkUVE{},
                                     Core::LobbyRosterSinkUVE{},
                                     &harness.relay,
                                     &harness.clients[0]};
    const auto program = CompileOrFailUVE(kRelayScriptUVE, host);
    ASSERT_NE(program, nullptr);
    UVScript::ScriptInstanceUVE instance(program, host);

    constexpr std::int64_t kPastU32 = 4294967296LL;
    EXPECT_FALSE(CallBool2UVE(instance, "set_room", -1, 0));
    EXPECT_FALSE(CallBool2UVE(instance, "set_room", 1, -1));
    EXPECT_FALSE(CallBool2UVE(instance, "set_room", 1, kPastU32));
    EXPECT_EQ(CallIntUVE(instance, "room", -1), 0);
    EXPECT_FALSE(CallBool2UVE(instance, "interested", -1, 1));
    EXPECT_FALSE(CallBool2UVE(instance, "interested", 1, kPastU32));
    EXPECT_FALSE(CallBool3UVE(instance, "set_interest", 1, -1, true));
    EXPECT_FALSE(CallBoolUVE(instance, "remove_peer", kPastU32));
    EXPECT_FALSE(CallBoolUVE(instance, "remove_peer", -1));
    EXPECT_EQ(CallStrUVE(instance, "peer_state", -5), "unknown");

    // In-range but unknown is not a refusal: the core's own defaults show through.
    EXPECT_EQ(CallIntUVE(instance, "room", 99), 0);
    EXPECT_TRUE(CallBool2UVE(instance, "interested", 99, 98));
    EXPECT_FALSE(CallBoolUVE(instance, "remove_peer", 99));
    EXPECT_EQ(harness.relay.PeerCountUVE(), 1U);
}

} // namespace
} // namespace UVE::Scene::Tests
