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
#include "uve/network/network_lobby_uve.h"
#include "uve/uvscript/uvscript_compiler_uve.h"
#include "uve/uvscript/uvscript_instance_uve.h"
#include "uve/uvscript/uvscript_value_uve.h"

namespace UVE::Scene::Tests {
namespace {

struct ScriptLobbyHarnessUVE final {
    std::vector<Network::LoopbackTransportPairUVE> links;
    // Facade 0 is the scripted one; the rest are C++-driven sparring partners. Add them
    // all before the host takes facade 0's address - the vector must not move after that.
    std::vector<Network::NetworkLobbyClientUVE> facades;
    std::vector<Network::LobbyClientIdUVE> ids;
    Network::NetworkLobbyUVE lobby;

    void AddFacadeUVE() {
        Network::LoopbackTransportPairUVE pair =
            Network::CreateLoopbackTransportPairUVE(256U);
        Network::NetworkSessionUVE client(
            Network::SessionRoleUVE::Client,
            std::make_unique<Network::LoopbackEndpointUVE>(pair.a));
        Network::NetworkSessionUVE server(
            Network::SessionRoleUVE::Server,
            std::make_unique<Network::LoopbackEndpointUVE>(pair.b));
        ids.push_back(lobby.AddClientUVE(std::move(server)));
        facades.emplace_back(std::move(client));
        links.push_back(std::move(pair));
    }

    void UpdateAllUVE(const std::uint64_t nowMs) {
        lobby.UpdateUVE(nowMs);
        for (auto& facade : facades) {
            facade.UpdateUVE(nowMs);
        }
    }

    void SettleUVE(const std::uint64_t nowMs) {
        UpdateAllUVE(nowMs);
        UpdateAllUVE(nowMs);
        UpdateAllUVE(nowMs);
    }

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        for (auto& facade : facades) {
            if (!facade.SessionUVE().ConnectUVE(nowMs)) {
                return false;
            }
        }
        UpdateAllUVE(nowMs);
        UpdateAllUVE(nowMs);
        for (auto& facade : facades) {
            if (facade.SessionUVE().StateUVE() != Network::SessionStateUVE::Established) {
                return false;
            }
        }
        for (const Network::LobbyClientIdUVE id : ids) {
            const Network::NetworkRpcUVE* server = lobby.ClientRpcUVE(id);
            if (server == nullptr ||
                server->SessionUVE().StateUVE() != Network::SessionStateUVE::Established) {
                return false;
            }
        }
        return true;
    }
};

// One script for every test below: the calls under test plus the readers, so each test
// compiles the same source and drives a different path through it.
inline constexpr const char* kLobbyScriptUVE = R"(on lobby_result(id, ok):
    print(lobby.error(id))

on lobby_roster(room):
    print(lobby.roster_name(0))

fn do_list() -> int:
    return lobby.list()

fn do_create(room: str, player: str, max: int) -> int:
    return lobby.create(room, player, max)

fn do_join(room: int, player: str) -> int:
    return lobby.join(room, player)

fn do_leave() -> int:
    return lobby.leave()

fn do_roster(room: int) -> int:
    return lobby.roster(room)

fn kind(id: int) -> str:
    return lobby.result_kind(id)

fn err(id: int) -> str:
    return lobby.error(id)

fn room_count(id: int) -> int:
    return lobby.result_room_count(id)

fn room_id(id: int, index: int) -> int:
    return lobby.result_room_id(id, index)

fn room_name(id: int, index: int) -> str:
    return lobby.result_room_name(id, index)

fn room_players(id: int, index: int) -> int:
    return lobby.result_room_players(id, index)

fn room_max(id: int, index: int) -> int:
    return lobby.result_room_max(id, index)

fn created(id: int) -> int:
    return lobby.result_created_room(id)

fn roster_count(id: int) -> int:
    return lobby.result_roster_count(id)

fn roster_name(id: int, index: int) -> str:
    return lobby.result_roster_name(id, index)

fn left(id: int) -> bool:
    return lobby.result_left(id)

fn release(id: int) -> bool:
    return lobby.release(id)

fn cancel_all() -> int:
    return lobby.cancel_all()

fn my_room() -> int:
    return lobby.room()

fn my_roster_count() -> int:
    return lobby.roster_count()

fn my_roster_name(index: int) -> str:
    return lobby.roster_name(index)
)";

struct LobbySignatureUVE final {
    const char* name;
    std::vector<UVScript::TypeUVE> params;
    UVScript::TypeUVE result;
};

class UVScriptLobbyBindingsUVETest : public ::testing::Test {
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

    [[nodiscard]] std::int64_t CallInt2UVE(UVScript::ScriptInstanceUVE& instance,
                                           const std::string& function, const std::int64_t first,
                                           const std::int64_t second) {
        const std::array<UVScript::ValueUVE, 2> args{UVScript::ValueUVE{first},
                                                     UVScript::ValueUVE{second}};
        return std::get<std::int64_t>(*instance.CallUVE(function, args));
    }

    [[nodiscard]] std::string CallStrUVE(UVScript::ScriptInstanceUVE& instance,
                                         const std::string& function, const std::int64_t id) {
        const std::array<UVScript::ValueUVE, 1> args{UVScript::ValueUVE{id}};
        return std::get<std::string>(*instance.CallUVE(function, args));
    }

    [[nodiscard]] std::string CallStr2UVE(UVScript::ScriptInstanceUVE& instance,
                                          const std::string& function, const std::int64_t first,
                                          const std::int64_t second) {
        const std::array<UVScript::ValueUVE, 2> args{UVScript::ValueUVE{first},
                                                     UVScript::ValueUVE{second}};
        return std::get<std::string>(*instance.CallUVE(function, args));
    }

    [[nodiscard]] bool CallBoolUVE(UVScript::ScriptInstanceUVE& instance,
                                   const std::string& function, const std::int64_t id) {
        const std::array<UVScript::ValueUVE, 1> args{UVScript::ValueUVE{id}};
        return std::get<bool>(*instance.CallUVE(function, args));
    }
};

TEST_F(UVScriptLobbyBindingsUVETest, Host_DescribesLobbySurfaceUngated) {
    // Ungated means ungated: a bare entity with no peer at all still describes every name,
    // and every call through them fails closed at run time instead.
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, entityManager.CreateEntityUVE()};
    const LobbySignatureUVE cases[] = {
        {"lobby.list", {}, UVScript::TypeUVE::IntUVE()},
        {"lobby.create",
         {UVScript::TypeUVE::StrUVE(), UVScript::TypeUVE::StrUVE(),
          UVScript::TypeUVE::IntUVE()},
         UVScript::TypeUVE::IntUVE()},
        {"lobby.join",
         {UVScript::TypeUVE::IntUVE(), UVScript::TypeUVE::StrUVE()},
         UVScript::TypeUVE::IntUVE()},
        {"lobby.leave", {}, UVScript::TypeUVE::IntUVE()},
        {"lobby.roster", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::IntUVE()},
        {"lobby.result_kind", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::StrUVE()},
        {"lobby.error", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::StrUVE()},
        {"lobby.result_room_count",
         {UVScript::TypeUVE::IntUVE()},
         UVScript::TypeUVE::IntUVE()},
        {"lobby.result_room_id",
         {UVScript::TypeUVE::IntUVE(), UVScript::TypeUVE::IntUVE()},
         UVScript::TypeUVE::IntUVE()},
        {"lobby.result_room_name",
         {UVScript::TypeUVE::IntUVE(), UVScript::TypeUVE::IntUVE()},
         UVScript::TypeUVE::StrUVE()},
        {"lobby.result_room_players",
         {UVScript::TypeUVE::IntUVE(), UVScript::TypeUVE::IntUVE()},
         UVScript::TypeUVE::IntUVE()},
        {"lobby.result_room_max",
         {UVScript::TypeUVE::IntUVE(), UVScript::TypeUVE::IntUVE()},
         UVScript::TypeUVE::IntUVE()},
        {"lobby.result_created_room",
         {UVScript::TypeUVE::IntUVE()},
         UVScript::TypeUVE::IntUVE()},
        {"lobby.result_roster_count",
         {UVScript::TypeUVE::IntUVE()},
         UVScript::TypeUVE::IntUVE()},
        {"lobby.result_roster_name",
         {UVScript::TypeUVE::IntUVE(), UVScript::TypeUVE::IntUVE()},
         UVScript::TypeUVE::StrUVE()},
        {"lobby.result_left", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::BoolUVE()},
        {"lobby.release", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::BoolUVE()},
        {"lobby.cancel_all", {}, UVScript::TypeUVE::IntUVE()},
        {"lobby.room", {}, UVScript::TypeUVE::IntUVE()},
        {"lobby.roster_count", {}, UVScript::TypeUVE::IntUVE()},
        {"lobby.roster_name", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::StrUVE()},
    };
    for (const LobbySignatureUVE& expected : cases) {
        const std::optional<UVScript::HostFunctionUVE> described =
            host.DescribeFunctionUVE(expected.name);
        ASSERT_TRUE(described.has_value()) << expected.name;
        EXPECT_EQ(described->params, expected.params) << expected.name;
        EXPECT_EQ(described->result, expected.result) << expected.name;
    }

    const std::optional<std::vector<UVScript::TypeUVE>> result =
        host.DescribeEventUVE("lobby_result");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, (std::vector<UVScript::TypeUVE>{UVScript::TypeUVE::IntUVE(),
                                                       UVScript::TypeUVE::BoolUVE()}));
    const std::optional<std::vector<UVScript::TypeUVE>> roster =
        host.DescribeEventUVE("lobby_roster");
    ASSERT_TRUE(roster.has_value());
    EXPECT_EQ(*roster, (std::vector<UVScript::TypeUVE>{UVScript::TypeUVE::IntUVE()}));
}

TEST_F(UVScriptLobbyBindingsUVETest, Host_FailsClosedWithoutPeer) {
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, entityManager.CreateEntityUVE()};
    const std::span<const UVScript::ValueUVE> noArgs{};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("lobby.list", noArgs)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("lobby.leave", noArgs)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("lobby.cancel_all", noArgs)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("lobby.room", noArgs)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("lobby.roster_count", noArgs)), 0);
    const std::array<UVScript::ValueUVE, 3> create{UVScript::ValueUVE{std::string{"r"}},
                                                   UVScript::ValueUVE{std::string{"p"}},
                                                   UVScript::ValueUVE{std::int64_t{4}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("lobby.create", create)), 0);
    const std::array<UVScript::ValueUVE, 2> join{UVScript::ValueUVE{std::int64_t{1}},
                                                 UVScript::ValueUVE{std::string{"p"}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("lobby.join", join)), 0);
    const std::array<UVScript::ValueUVE, 1> id{UVScript::ValueUVE{std::int64_t{1}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("lobby.roster", id)), 0);
    EXPECT_EQ(std::get<std::string>(host.CallFunctionUVE("lobby.result_kind", id)), "none");
    EXPECT_EQ(std::get<std::string>(host.CallFunctionUVE("lobby.error", id)), "");
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("lobby.result_room_count", id)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("lobby.result_created_room", id)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("lobby.result_roster_count", id)), 0);
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("lobby.result_left", id)));
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("lobby.release", id)));
    EXPECT_EQ(std::get<std::string>(host.CallFunctionUVE("lobby.roster_name", id)), "");
    const std::array<UVScript::ValueUVE, 2> indexed{UVScript::ValueUVE{std::int64_t{1}},
                                                    UVScript::ValueUVE{std::int64_t{0}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("lobby.result_room_id", indexed)), 0);
    EXPECT_EQ(std::get<std::string>(host.CallFunctionUVE("lobby.result_room_name", indexed)),
              "");
    EXPECT_EQ(
        std::get<std::int64_t>(host.CallFunctionUVE("lobby.result_room_players", indexed)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("lobby.result_room_max", indexed)), 0);
    EXPECT_EQ(std::get<std::string>(host.CallFunctionUVE("lobby.result_roster_name", indexed)),
              "");
}

TEST_F(UVScriptLobbyBindingsUVETest, Script_ListsCreatesAndReadsRooms) {
    ScriptLobbyHarnessUVE harness;
    harness.AddFacadeUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    // The game loop in miniature: the sinks record and raise the events on the instance,
    // exactly as a running game would from its own response sinks.
    std::vector<std::pair<std::uint32_t, bool>> results;
    std::vector<std::uint32_t> pushes;
    UVScript::ScriptInstanceUVE* live = nullptr;
    Core::LobbyResultSinkUVE resultSink = [&](const std::uint32_t id, const bool ok) {
        results.emplace_back(id, ok);
        if (live != nullptr) {
            const std::array<UVScript::ValueUVE, 2> args{
                UVScript::ValueUVE{static_cast<std::int64_t>(id)}, UVScript::ValueUVE{ok}};
            static_cast<void>(live->RaiseEventUVE("lobby_result", args));
        }
    };
    Core::LobbyRosterSinkUVE rosterSink = [&](const std::uint32_t room) {
        pushes.push_back(room);
        if (live != nullptr) {
            const std::array<UVScript::ValueUVE, 1> args{
                UVScript::ValueUVE{static_cast<std::int64_t>(room)}};
            static_cast<void>(live->RaiseEventUVE("lobby_roster", args));
        }
    };
    Core::UVScriptObjectHostUVE host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntityUVE(),
                                     nullptr,
                                     nullptr,
                                     Core::UVScriptObjectHostUVE::InstanceResolverUVE{},
                                     nullptr,
                                     Core::RpcResponseSinkUVE{},
                                     &harness.facades[0],
                                     std::move(resultSink),
                                     std::move(rosterSink)};
    const auto program = CompileOrFailUVE(kLobbyScriptUVE, host);
    ASSERT_NE(program, nullptr);
    UVScript::ScriptInstanceUVE instance(program, host);
    live = &instance;

    const std::int64_t listed = CallIntUVE(instance, "do_list");
    EXPECT_NE(listed, 0);
    EXPECT_EQ(CallStrUVE(instance, "kind", listed), "pending");
    harness.SettleUVE(1100U);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].first, static_cast<std::uint32_t>(listed));
    EXPECT_TRUE(results[0].second);
    EXPECT_EQ(CallStrUVE(instance, "kind", listed), "rooms");
    EXPECT_EQ(CallIntUVE(instance, "room_count", listed), 0);

    const std::array<UVScript::ValueUVE, 3> create{UVScript::ValueUVE{std::string{"arena"}},
                                                   UVScript::ValueUVE{std::string{"amy"}},
                                                   UVScript::ValueUVE{std::int64_t{4}}};
    const std::int64_t made = std::get<std::int64_t>(*instance.CallUVE("do_create", create));
    EXPECT_NE(made, 0);
    harness.SettleUVE(1200U);
    ASSERT_EQ(results.size(), 2U);
    EXPECT_TRUE(results[1].second);
    EXPECT_EQ(CallStrUVE(instance, "kind", made), "room");
    EXPECT_EQ(CallIntUVE(instance, "created", made), 1);
    EXPECT_EQ(CallIntUVE(instance, "my_room"), 1);
    EXPECT_EQ(CallStrUVE(instance, "err", made), "");

    const std::int64_t relisted = CallIntUVE(instance, "do_list");
    harness.SettleUVE(1300U);
    EXPECT_EQ(CallStrUVE(instance, "kind", relisted), "rooms");
    EXPECT_EQ(CallIntUVE(instance, "room_count", relisted), 1);
    EXPECT_EQ(CallInt2UVE(instance, "room_id", relisted, 0), 1);
    EXPECT_EQ(CallStr2UVE(instance, "room_name", relisted, 0), "arena");
    EXPECT_EQ(CallInt2UVE(instance, "room_players", relisted, 0), 1);
    EXPECT_EQ(CallInt2UVE(instance, "room_max", relisted, 0), 4);
    EXPECT_EQ(CallInt2UVE(instance, "room_id", relisted, 1), 0);
    EXPECT_EQ(CallStr2UVE(instance, "room_name", relisted, 9), "");

    // Wrong-kind readers stay defaulted instead of leaking across result shapes.
    EXPECT_EQ(CallIntUVE(instance, "room_count", made), 0);
    EXPECT_EQ(CallIntUVE(instance, "created", relisted), 0);
    EXPECT_EQ(CallIntUVE(instance, "roster_count", relisted), 0);
    EXPECT_FALSE(CallBoolUVE(instance, "left", relisted));

    // The handler from the shared script ran on the sink's raise (a print, so the log owns
    // the proof); raising again directly still finds it.
    const std::array<UVScript::ValueUVE, 2> eventArgs{UVScript::ValueUVE{relisted},
                                                      UVScript::ValueUVE{true}};
    EXPECT_TRUE(instance.RaiseEventUVE("lobby_result", eventArgs));

    EXPECT_TRUE(CallBoolUVE(instance, "release", listed));
    EXPECT_EQ(CallStrUVE(instance, "kind", listed), "none");
    EXPECT_TRUE(pushes.empty());
}

TEST_F(UVScriptLobbyBindingsUVETest, Script_JoinsLeavesAndSeesErrors) {
    ScriptLobbyHarnessUVE harness;
    harness.AddFacadeUVE();
    harness.AddFacadeUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    bool made = false;
    harness.facades[1].CreateRoomUVE(
        "arena", "bob", 4U, [&](Network::LobbyRoomIdUVE id, const std::string& error) {
            made = true;
            EXPECT_EQ(id, 1U);
            EXPECT_TRUE(error.empty());
        });
    harness.SettleUVE(1100U);
    ASSERT_TRUE(made);

    std::vector<std::pair<std::uint32_t, bool>> results;
    Core::UVScriptObjectHostUVE host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntityUVE(),
                                     nullptr,
                                     nullptr,
                                     Core::UVScriptObjectHostUVE::InstanceResolverUVE{},
                                     nullptr,
                                     Core::RpcResponseSinkUVE{},
                                     &harness.facades[0],
                                     [&](const std::uint32_t id, const bool ok) {
                                         results.emplace_back(id, ok);
                                     },
                                     Core::LobbyRosterSinkUVE{}};
    const auto program = CompileOrFailUVE(kLobbyScriptUVE, host);
    ASSERT_NE(program, nullptr);
    UVScript::ScriptInstanceUVE instance(program, host);

    auto joinAs = [&](const std::int64_t room, const std::string& player) {
        const std::array<UVScript::ValueUVE, 2> args{UVScript::ValueUVE{room},
                                                     UVScript::ValueUVE{player}};
        return std::get<std::int64_t>(*instance.CallUVE("do_join", args));
    };
    const std::int64_t joined = joinAs(1, "amy");
    EXPECT_NE(joined, 0);
    EXPECT_EQ(CallStrUVE(instance, "kind", joined), "pending");
    harness.SettleUVE(1200U);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_TRUE(results[0].second);
    EXPECT_EQ(CallStrUVE(instance, "kind", joined), "roster");
    EXPECT_EQ(CallIntUVE(instance, "roster_count", joined), 2);
    // Member-id order: the script facade is client 1, so "amy" sorts first.
    EXPECT_EQ(CallStr2UVE(instance, "roster_name", joined, 0), "amy");
    EXPECT_EQ(CallStr2UVE(instance, "roster_name", joined, 1), "bob");
    EXPECT_EQ(CallStr2UVE(instance, "roster_name", joined, 2), "");
    EXPECT_EQ(CallIntUVE(instance, "my_room"), 1);
    EXPECT_EQ(CallIntUVE(instance, "my_roster_count"), 2);
    EXPECT_EQ(CallStrUVE(instance, "my_roster_name", 0), "amy");

    const std::int64_t left = CallIntUVE(instance, "do_leave");
    harness.SettleUVE(1300U);
    EXPECT_EQ(CallStrUVE(instance, "kind", left), "left");
    EXPECT_TRUE(CallBoolUVE(instance, "left", left));
    EXPECT_EQ(CallIntUVE(instance, "my_room"), 0);
    EXPECT_EQ(CallIntUVE(instance, "my_roster_count"), 0);
    EXPECT_EQ(CallStrUVE(instance, "my_roster_name", 0), "");

    const std::int64_t ghost = joinAs(999, "amy");
    harness.SettleUVE(1400U);
    ASSERT_EQ(results.size(), 3U);
    EXPECT_FALSE(results[2].second);
    EXPECT_EQ(CallStrUVE(instance, "kind", ghost), "error");
    EXPECT_EQ(CallStrUVE(instance, "err", ghost), "unknown room '999'");
    EXPECT_EQ(CallIntUVE(instance, "roster_count", ghost), 0);

    // A pending call refuses release; abandoning it drops the id without a sink fire.
    const std::size_t settled = results.size();
    const std::int64_t dangling = CallIntUVE(instance, "do_list");
    EXPECT_FALSE(CallBoolUVE(instance, "release", dangling));
    EXPECT_EQ(CallStrUVE(instance, "kind", dangling), "pending");
    EXPECT_EQ(CallIntUVE(instance, "cancel_all"), 1);
    EXPECT_EQ(CallStrUVE(instance, "kind", dangling), "none");
    harness.SettleUVE(1500U);
    EXPECT_EQ(results.size(), settled);
    EXPECT_EQ(CallIntUVE(instance, "cancel_all"), 0);

    EXPECT_TRUE(CallBoolUVE(instance, "release", ghost));
    EXPECT_EQ(CallStrUVE(instance, "kind", ghost), "none");
    EXPECT_FALSE(CallBoolUVE(instance, "release", ghost));
}

TEST_F(UVScriptLobbyBindingsUVETest, Script_ReceivesRosterPushes) {
    ScriptLobbyHarnessUVE harness;
    harness.AddFacadeUVE();
    harness.AddFacadeUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    std::vector<std::uint32_t> pushes;
    UVScript::ScriptInstanceUVE* live = nullptr;
    Core::UVScriptObjectHostUVE host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntityUVE(),
                                     nullptr,
                                     nullptr,
                                     Core::UVScriptObjectHostUVE::InstanceResolverUVE{},
                                     nullptr,
                                     Core::RpcResponseSinkUVE{},
                                     &harness.facades[0],
                                     Core::LobbyResultSinkUVE{},
                                     [&](const std::uint32_t room) {
                                         pushes.push_back(room);
                                         if (live != nullptr) {
                                             const std::array<UVScript::ValueUVE, 1> args{
                                                 UVScript::ValueUVE{
                                                     static_cast<std::int64_t>(room)}};
                                             static_cast<void>(
                                                 live->RaiseEventUVE("lobby_roster", args));
                                         }
                                     }};
    const auto program = CompileOrFailUVE(kLobbyScriptUVE, host);
    ASSERT_NE(program, nullptr);
    UVScript::ScriptInstanceUVE instance(program, host);
    live = &instance;

    const std::array<UVScript::ValueUVE, 3> create{UVScript::ValueUVE{std::string{"arena"}},
                                                   UVScript::ValueUVE{std::string{"amy"}},
                                                   UVScript::ValueUVE{std::int64_t{4}}};
    const std::int64_t made = std::get<std::int64_t>(*instance.CallUVE("do_create", create));
    EXPECT_NE(made, 0);
    harness.SettleUVE(1100U);
    EXPECT_EQ(CallIntUVE(instance, "my_room"), 1);
    EXPECT_TRUE(pushes.empty());

    bool joined = false;
    harness.facades[1].JoinRoomUVE(
        1U, "cat",
        [&](std::vector<std::string> roster, const std::string& error) {
            joined = true;
            EXPECT_TRUE(error.empty());
            EXPECT_EQ(roster, (std::vector<std::string>{"amy", "cat"}));
        });
    harness.SettleUVE(1200U);
    ASSERT_TRUE(joined);
    ASSERT_EQ(pushes.size(), 1U);
    EXPECT_EQ(pushes[0], 1U);
    EXPECT_EQ(CallIntUVE(instance, "my_roster_count"), 2);
    EXPECT_EQ(CallStrUVE(instance, "my_roster_name", 1), "cat");

    const std::array<UVScript::ValueUVE, 1> eventArgs{UVScript::ValueUVE{std::int64_t{1}}};
    EXPECT_TRUE(instance.RaiseEventUVE("lobby_roster", eventArgs));

    bool left = false;
    harness.facades[1].LeaveRoomUVE([&](bool wasLeft, const std::string& error) {
        left = true;
        EXPECT_TRUE(wasLeft);
        EXPECT_TRUE(error.empty());
    });
    harness.SettleUVE(1300U);
    ASSERT_TRUE(left);
    ASSERT_EQ(pushes.size(), 2U);
    EXPECT_EQ(pushes[1], 1U);
    EXPECT_EQ(CallIntUVE(instance, "my_roster_count"), 1);
}

TEST_F(UVScriptLobbyBindingsUVETest, Host_AbandonsAndUnhooksOnDestruction) {
    ScriptLobbyHarnessUVE harness;
    harness.AddFacadeUVE();
    harness.AddFacadeUVE();
    harness.AddFacadeUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    std::vector<std::pair<std::uint32_t, bool>> results;
    std::vector<std::uint32_t> pushes;
    {
        Core::UVScriptObjectHostUVE host{entityManager,
                                         nullptr,
                                         entityManager.CreateEntityUVE(),
                                         nullptr,
                                         nullptr,
                                         Core::UVScriptObjectHostUVE::InstanceResolverUVE{},
                                         nullptr,
                                         Core::RpcResponseSinkUVE{},
                                         &harness.facades[0],
                                         [&](const std::uint32_t id, const bool ok) {
                                             results.emplace_back(id, ok);
                                         },
                                         [&](const std::uint32_t room) {
                                             pushes.push_back(room);
                                         }};
        const std::span<const UVScript::ValueUVE> noArgs{};
        const std::int64_t listed =
            std::get<std::int64_t>(host.CallFunctionUVE("lobby.list", noArgs));
        EXPECT_NE(listed, 0);
    }

    // The host is gone but the peer lives on: the exchange still runs, the completion has
    // nowhere to go, and the late response reads as stale.
    harness.SettleUVE(1100U);
    harness.SettleUVE(1200U);
    EXPECT_TRUE(results.empty());
    EXPECT_TRUE(pushes.empty());
    EXPECT_EQ(harness.facades[0].StatsUVE().responsesIgnoredStale, 1U);

    // The roster push unhooked with it: joins still update the peer's cache, silently.
    bool made = false;
    harness.facades[1].CreateRoomUVE(
        "arena", "m1", 4U, [&](Network::LobbyRoomIdUVE id, const std::string& error) {
            made = true;
            EXPECT_EQ(id, 1U);
            EXPECT_TRUE(error.empty());
        });
    harness.SettleUVE(1300U);
    ASSERT_TRUE(made);
    bool joined = false;
    harness.facades[0].JoinRoomUVE(
        1U, "ghost", [&](std::vector<std::string>, const std::string& error) {
            joined = true;
            EXPECT_TRUE(error.empty());
        });
    harness.SettleUVE(1400U);
    ASSERT_TRUE(joined);
    bool third = false;
    harness.facades[2].JoinRoomUVE(
        1U, "m2", [&](std::vector<std::string>, const std::string& error) {
            third = true;
            EXPECT_TRUE(error.empty());
        });
    harness.SettleUVE(1500U);
    ASSERT_TRUE(third);
    EXPECT_TRUE(pushes.empty());
    EXPECT_EQ(harness.facades[0].CachedRosterUVE(),
              (std::vector<std::string>{"ghost", "m1", "m2"}));
}

TEST_F(UVScriptLobbyBindingsUVETest, Script_QueriesRostersWithoutJoining) {
    ScriptLobbyHarnessUVE harness;
    harness.AddFacadeUVE();
    harness.AddFacadeUVE();
    harness.AddFacadeUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    bool made = false;
    harness.facades[1].CreateRoomUVE(
        "arena", "bob", 4U, [&](Network::LobbyRoomIdUVE id, const std::string& error) {
            made = true;
            EXPECT_EQ(id, 1U);
            EXPECT_TRUE(error.empty());
        });
    harness.SettleUVE(1100U);
    ASSERT_TRUE(made);
    bool joined = false;
    harness.facades[2].JoinRoomUVE(
        1U, "cat", [&](std::vector<std::string>, const std::string& error) {
            joined = true;
            EXPECT_TRUE(error.empty());
        });
    harness.SettleUVE(1200U);
    ASSERT_TRUE(joined);

    std::vector<std::uint32_t> pushes;
    Core::UVScriptObjectHostUVE host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntityUVE(),
                                     nullptr,
                                     nullptr,
                                     Core::UVScriptObjectHostUVE::InstanceResolverUVE{},
                                     nullptr,
                                     Core::RpcResponseSinkUVE{},
                                     &harness.facades[0],
                                     Core::LobbyResultSinkUVE{},
                                     [&](const std::uint32_t room) { pushes.push_back(room); }};
    const auto program = CompileOrFailUVE(kLobbyScriptUVE, host);
    ASSERT_NE(program, nullptr);
    UVScript::ScriptInstanceUVE instance(program, host);

    const std::int64_t queried = CallIntUVE(instance, "do_roster", 1);
    EXPECT_NE(queried, 0);
    harness.SettleUVE(1300U);
    EXPECT_EQ(CallStrUVE(instance, "kind", queried), "roster");
    EXPECT_EQ(CallIntUVE(instance, "roster_count", queried), 2);
    EXPECT_EQ(CallStr2UVE(instance, "roster_name", queried, 0), "bob");
    EXPECT_EQ(CallStr2UVE(instance, "roster_name", queried, 1), "cat");
    EXPECT_EQ(CallIntUVE(instance, "my_room"), 0);
    EXPECT_TRUE(pushes.empty());

    const std::int64_t ghost = CallIntUVE(instance, "do_roster", 999);
    harness.SettleUVE(1400U);
    EXPECT_EQ(CallStrUVE(instance, "kind", ghost), "error");
    EXPECT_EQ(CallStrUVE(instance, "err", ghost), "unknown room '999'");
}

TEST_F(UVScriptLobbyBindingsUVETest, Script_SendFailuresAnswerZero) {
    ScriptLobbyHarnessUVE harness;
    harness.AddFacadeUVE();
    // Never established: every call fails its send synchronously, before any pump.

    std::vector<std::pair<std::uint32_t, bool>> results;
    std::vector<std::uint32_t> pushes;
    Core::UVScriptObjectHostUVE host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntityUVE(),
                                     nullptr,
                                     nullptr,
                                     Core::UVScriptObjectHostUVE::InstanceResolverUVE{},
                                     nullptr,
                                     Core::RpcResponseSinkUVE{},
                                     &harness.facades[0],
                                     [&](const std::uint32_t id, const bool ok) {
                                         results.emplace_back(id, ok);
                                     },
                                     [&](const std::uint32_t room) { pushes.push_back(room); }};
    const auto program = CompileOrFailUVE(kLobbyScriptUVE, host);
    ASSERT_NE(program, nullptr);
    UVScript::ScriptInstanceUVE instance(program, host);

    EXPECT_EQ(CallIntUVE(instance, "do_list"), 0);
    const std::array<UVScript::ValueUVE, 3> create{UVScript::ValueUVE{std::string{"arena"}},
                                                   UVScript::ValueUVE{std::string{"amy"}},
                                                   UVScript::ValueUVE{std::int64_t{4}}};
    EXPECT_EQ(std::get<std::int64_t>(*instance.CallUVE("do_create", create)), 0);
    const std::array<UVScript::ValueUVE, 2> join{UVScript::ValueUVE{std::int64_t{1}},
                                                 UVScript::ValueUVE{std::string{"amy"}}};
    EXPECT_EQ(std::get<std::int64_t>(*instance.CallUVE("do_join", join)), 0);
    EXPECT_EQ(CallIntUVE(instance, "do_leave"), 0);
    EXPECT_EQ(CallIntUVE(instance, "do_roster", 1), 0);
    EXPECT_TRUE(results.empty());
    EXPECT_TRUE(pushes.empty());
    EXPECT_EQ(CallStrUVE(instance, "kind", 0), "none");
    EXPECT_EQ(CallStrUVE(instance, "err", 0), "");
}

} // namespace
} // namespace UVE::Scene::Tests
