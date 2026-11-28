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
#include "network_lobby.h"
#include "blueprint_compiler.h"
#include "blueprint_instance.h"
#include "blueprint_value.h"

namespace UVE::Scene::Tests {
namespace {

struct ScriptLobbyHarness final {
    std::vector<Network::LoopbackTransportPair> links;
    // Facade 0 is the scripted one; the rest are C++-driven sparring partners. Add them
    // all before the host takes facade 0's address - the vector must not move after that.
    std::vector<Network::NetworkLobbyClient> facades;
    std::vector<Network::LobbyClientId> ids;
    Network::NetworkLobby lobby;

    void AddFacade() {
        Network::LoopbackTransportPair pair =
            Network::CreateLoopbackTransportPair(256U);
        Network::NetworkSession client(
            Network::SessionRole::Client,
            std::make_unique<Network::LoopbackEndpoint>(pair.a));
        Network::NetworkSession server(
            Network::SessionRole::Server,
            std::make_unique<Network::LoopbackEndpoint>(pair.b));
        ids.push_back(lobby.AddClient(std::move(server)));
        facades.emplace_back(std::move(client));
        links.push_back(std::move(pair));
    }

    void UpdateAll(const std::uint64_t nowMs) {
        lobby.Update(nowMs);
        for (auto& facade : facades) {
            facade.Update(nowMs);
        }
    }

    void Settle(const std::uint64_t nowMs) {
        UpdateAll(nowMs);
        UpdateAll(nowMs);
        UpdateAll(nowMs);
    }

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        for (auto& facade : facades) {
            if (!facade.Session().Connect(nowMs)) {
                return false;
            }
        }
        UpdateAll(nowMs);
        UpdateAll(nowMs);
        for (auto& facade : facades) {
            if (facade.Session().State() != Network::SessionState::Established) {
                return false;
            }
        }
        for (const Network::LobbyClientId id : ids) {
            const Network::NetworkRpc* server = lobby.ClientRpc(id);
            if (server == nullptr ||
                server->Session().State() != Network::SessionState::Established) {
                return false;
            }
        }
        return true;
    }
};

// One script for every test below: the calls under test plus the readers, so each test
// compiles the same source and drives a different path through it.
inline constexpr const char* kLobbyScript = R"(on lobby_result(id, ok):
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

struct LobbySignature final {
    const char* name;
    std::vector<Blueprint::Type> params;
    Blueprint::Type result;
};

class BlueprintLobbyBindingsTest : public ::testing::Test {
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

    [[nodiscard]] std::int64_t CallInt2(Blueprint::BlueprintInstance& instance,
                                           const std::string& function, const std::int64_t first,
                                           const std::int64_t second) {
        const std::array<Blueprint::Value, 2> args{Blueprint::Value{first},
                                                     Blueprint::Value{second}};
        return std::get<std::int64_t>(*instance.Call(function, args));
    }

    [[nodiscard]] std::string CallStr(Blueprint::BlueprintInstance& instance,
                                         const std::string& function, const std::int64_t id) {
        const std::array<Blueprint::Value, 1> args{Blueprint::Value{id}};
        return std::get<std::string>(*instance.Call(function, args));
    }

    [[nodiscard]] std::string CallStr2(Blueprint::BlueprintInstance& instance,
                                          const std::string& function, const std::int64_t first,
                                          const std::int64_t second) {
        const std::array<Blueprint::Value, 2> args{Blueprint::Value{first},
                                                     Blueprint::Value{second}};
        return std::get<std::string>(*instance.Call(function, args));
    }

    [[nodiscard]] bool CallBool(Blueprint::BlueprintInstance& instance,
                                   const std::string& function, const std::int64_t id) {
        const std::array<Blueprint::Value, 1> args{Blueprint::Value{id}};
        return std::get<bool>(*instance.Call(function, args));
    }
};

TEST_F(BlueprintLobbyBindingsTest, Host_DescribesLobbySurfaceUngated) {
    // Ungated means ungated: a bare entity with no peer at all still describes every name,
    // and every call through them fails closed at run time instead.
    Core::BlueprintObjectHost host{entityManager, nullptr, entityManager.CreateEntity()};
    const LobbySignature cases[] = {
        {"lobby.list", {}, Blueprint::Type::Int()},
        {"lobby.create",
         {Blueprint::Type::Str(), Blueprint::Type::Str(),
          Blueprint::Type::Int()},
         Blueprint::Type::Int()},
        {"lobby.join",
         {Blueprint::Type::Int(), Blueprint::Type::Str()},
         Blueprint::Type::Int()},
        {"lobby.leave", {}, Blueprint::Type::Int()},
        {"lobby.roster", {Blueprint::Type::Int()}, Blueprint::Type::Int()},
        {"lobby.result_kind", {Blueprint::Type::Int()}, Blueprint::Type::Str()},
        {"lobby.error", {Blueprint::Type::Int()}, Blueprint::Type::Str()},
        {"lobby.result_room_count",
         {Blueprint::Type::Int()},
         Blueprint::Type::Int()},
        {"lobby.result_room_id",
         {Blueprint::Type::Int(), Blueprint::Type::Int()},
         Blueprint::Type::Int()},
        {"lobby.result_room_name",
         {Blueprint::Type::Int(), Blueprint::Type::Int()},
         Blueprint::Type::Str()},
        {"lobby.result_room_players",
         {Blueprint::Type::Int(), Blueprint::Type::Int()},
         Blueprint::Type::Int()},
        {"lobby.result_room_max",
         {Blueprint::Type::Int(), Blueprint::Type::Int()},
         Blueprint::Type::Int()},
        {"lobby.result_created_room",
         {Blueprint::Type::Int()},
         Blueprint::Type::Int()},
        {"lobby.result_roster_count",
         {Blueprint::Type::Int()},
         Blueprint::Type::Int()},
        {"lobby.result_roster_name",
         {Blueprint::Type::Int(), Blueprint::Type::Int()},
         Blueprint::Type::Str()},
        {"lobby.result_left", {Blueprint::Type::Int()}, Blueprint::Type::Bool()},
        {"lobby.release", {Blueprint::Type::Int()}, Blueprint::Type::Bool()},
        {"lobby.cancel_all", {}, Blueprint::Type::Int()},
        {"lobby.room", {}, Blueprint::Type::Int()},
        {"lobby.roster_count", {}, Blueprint::Type::Int()},
        {"lobby.roster_name", {Blueprint::Type::Int()}, Blueprint::Type::Str()},
    };
    for (const LobbySignature& expected : cases) {
        const std::optional<Blueprint::HostFunction> described =
            host.DescribeFunction(expected.name);
        ASSERT_TRUE(described.has_value()) << expected.name;
        EXPECT_EQ(described->params, expected.params) << expected.name;
        EXPECT_EQ(described->result, expected.result) << expected.name;
    }

    const std::optional<std::vector<Blueprint::Type>> result =
        host.DescribeEvent("lobby_result");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, (std::vector<Blueprint::Type>{Blueprint::Type::Int(),
                                                       Blueprint::Type::Bool()}));
    const std::optional<std::vector<Blueprint::Type>> roster =
        host.DescribeEvent("lobby_roster");
    ASSERT_TRUE(roster.has_value());
    EXPECT_EQ(*roster, (std::vector<Blueprint::Type>{Blueprint::Type::Int()}));
}

TEST_F(BlueprintLobbyBindingsTest, Host_FailsClosedWithoutPeer) {
    Core::BlueprintObjectHost host{entityManager, nullptr, entityManager.CreateEntity()};
    const std::span<const Blueprint::Value> noArgs{};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("lobby.list", noArgs)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("lobby.leave", noArgs)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("lobby.cancel_all", noArgs)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("lobby.room", noArgs)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("lobby.roster_count", noArgs)), 0);
    const std::array<Blueprint::Value, 3> create{Blueprint::Value{std::string{"r"}},
                                                   Blueprint::Value{std::string{"p"}},
                                                   Blueprint::Value{std::int64_t{4}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("lobby.create", create)), 0);
    const std::array<Blueprint::Value, 2> join{Blueprint::Value{std::int64_t{1}},
                                                 Blueprint::Value{std::string{"p"}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("lobby.join", join)), 0);
    const std::array<Blueprint::Value, 1> id{Blueprint::Value{std::int64_t{1}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("lobby.roster", id)), 0);
    EXPECT_EQ(std::get<std::string>(host.CallFunction("lobby.result_kind", id)), "none");
    EXPECT_EQ(std::get<std::string>(host.CallFunction("lobby.error", id)), "");
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("lobby.result_room_count", id)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("lobby.result_created_room", id)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("lobby.result_roster_count", id)), 0);
    EXPECT_FALSE(std::get<bool>(host.CallFunction("lobby.result_left", id)));
    EXPECT_FALSE(std::get<bool>(host.CallFunction("lobby.release", id)));
    EXPECT_EQ(std::get<std::string>(host.CallFunction("lobby.roster_name", id)), "");
    const std::array<Blueprint::Value, 2> indexed{Blueprint::Value{std::int64_t{1}},
                                                    Blueprint::Value{std::int64_t{0}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("lobby.result_room_id", indexed)), 0);
    EXPECT_EQ(std::get<std::string>(host.CallFunction("lobby.result_room_name", indexed)),
              "");
    EXPECT_EQ(
        std::get<std::int64_t>(host.CallFunction("lobby.result_room_players", indexed)), 0);
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("lobby.result_room_max", indexed)), 0);
    EXPECT_EQ(std::get<std::string>(host.CallFunction("lobby.result_roster_name", indexed)),
              "");
}

TEST_F(BlueprintLobbyBindingsTest, Script_ListsCreatesAndReadsRooms) {
    ScriptLobbyHarness harness;
    harness.AddFacade();
    ASSERT_TRUE(harness.Establish(1000U));

    // The game loop in miniature: the sinks record and raise the events on the instance,
    // exactly as a running game would from its own response sinks.
    std::vector<std::pair<std::uint32_t, bool>> results;
    std::vector<std::uint32_t> pushes;
    Blueprint::BlueprintInstance* live = nullptr;
    Core::LobbyResultSink resultSink = [&](const std::uint32_t id, const bool ok) {
        results.emplace_back(id, ok);
        if (live != nullptr) {
            const std::array<Blueprint::Value, 2> args{
                Blueprint::Value{static_cast<std::int64_t>(id)}, Blueprint::Value{ok}};
            static_cast<void>(live->RaiseEvent("lobby_result", args));
        }
    };
    Core::LobbyRosterSink rosterSink = [&](const std::uint32_t room) {
        pushes.push_back(room);
        if (live != nullptr) {
            const std::array<Blueprint::Value, 1> args{
                Blueprint::Value{static_cast<std::int64_t>(room)}};
            static_cast<void>(live->RaiseEvent("lobby_roster", args));
        }
    };
    Core::BlueprintObjectHost host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntity(),
                                     nullptr,
                                     nullptr,
                                     Core::BlueprintObjectHost::InstanceResolver{},
                                     nullptr,
                                     Core::RpcResponseSink{},
                                     &harness.facades[0],
                                     std::move(resultSink),
                                     std::move(rosterSink)};
    const auto program = CompileOrFail(kLobbyScript, host);
    ASSERT_NE(program, nullptr);
    Blueprint::BlueprintInstance instance(program, host);
    live = &instance;

    const std::int64_t listed = CallInt(instance, "do_list");
    EXPECT_NE(listed, 0);
    EXPECT_EQ(CallStr(instance, "kind", listed), "pending");
    harness.Settle(1100U);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].first, static_cast<std::uint32_t>(listed));
    EXPECT_TRUE(results[0].second);
    EXPECT_EQ(CallStr(instance, "kind", listed), "rooms");
    EXPECT_EQ(CallInt(instance, "room_count", listed), 0);

    const std::array<Blueprint::Value, 3> create{Blueprint::Value{std::string{"arena"}},
                                                   Blueprint::Value{std::string{"amy"}},
                                                   Blueprint::Value{std::int64_t{4}}};
    const std::int64_t made = std::get<std::int64_t>(*instance.Call("do_create", create));
    EXPECT_NE(made, 0);
    harness.Settle(1200U);
    ASSERT_EQ(results.size(), 2U);
    EXPECT_TRUE(results[1].second);
    EXPECT_EQ(CallStr(instance, "kind", made), "room");
    EXPECT_EQ(CallInt(instance, "created", made), 1);
    EXPECT_EQ(CallInt(instance, "my_room"), 1);
    EXPECT_EQ(CallStr(instance, "err", made), "");

    const std::int64_t relisted = CallInt(instance, "do_list");
    harness.Settle(1300U);
    EXPECT_EQ(CallStr(instance, "kind", relisted), "rooms");
    EXPECT_EQ(CallInt(instance, "room_count", relisted), 1);
    EXPECT_EQ(CallInt2(instance, "room_id", relisted, 0), 1);
    EXPECT_EQ(CallStr2(instance, "room_name", relisted, 0), "arena");
    EXPECT_EQ(CallInt2(instance, "room_players", relisted, 0), 1);
    EXPECT_EQ(CallInt2(instance, "room_max", relisted, 0), 4);
    EXPECT_EQ(CallInt2(instance, "room_id", relisted, 1), 0);
    EXPECT_EQ(CallStr2(instance, "room_name", relisted, 9), "");

    // Wrong-kind readers stay defaulted instead of leaking across result shapes.
    EXPECT_EQ(CallInt(instance, "room_count", made), 0);
    EXPECT_EQ(CallInt(instance, "created", relisted), 0);
    EXPECT_EQ(CallInt(instance, "roster_count", relisted), 0);
    EXPECT_FALSE(CallBool(instance, "left", relisted));

    // The handler from the shared script ran on the sink's raise (a print, so the log owns
    // the proof); raising again directly still finds it.
    const std::array<Blueprint::Value, 2> eventArgs{Blueprint::Value{relisted},
                                                      Blueprint::Value{true}};
    EXPECT_TRUE(instance.RaiseEvent("lobby_result", eventArgs));

    EXPECT_TRUE(CallBool(instance, "release", listed));
    EXPECT_EQ(CallStr(instance, "kind", listed), "none");
    EXPECT_TRUE(pushes.empty());
}

TEST_F(BlueprintLobbyBindingsTest, Script_JoinsLeavesAndSeesErrors) {
    ScriptLobbyHarness harness;
    harness.AddFacade();
    harness.AddFacade();
    ASSERT_TRUE(harness.Establish(1000U));

    bool made = false;
    harness.facades[1].CreateRoom(
        "arena", "bob", 4U, [&](Network::LobbyRoomId id, const std::string& error) {
            made = true;
            EXPECT_EQ(id, 1U);
            EXPECT_TRUE(error.empty());
        });
    harness.Settle(1100U);
    ASSERT_TRUE(made);

    std::vector<std::pair<std::uint32_t, bool>> results;
    Core::BlueprintObjectHost host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntity(),
                                     nullptr,
                                     nullptr,
                                     Core::BlueprintObjectHost::InstanceResolver{},
                                     nullptr,
                                     Core::RpcResponseSink{},
                                     &harness.facades[0],
                                     [&](const std::uint32_t id, const bool ok) {
                                         results.emplace_back(id, ok);
                                     },
                                     Core::LobbyRosterSink{}};
    const auto program = CompileOrFail(kLobbyScript, host);
    ASSERT_NE(program, nullptr);
    Blueprint::BlueprintInstance instance(program, host);

    auto joinAs = [&](const std::int64_t room, const std::string& player) {
        const std::array<Blueprint::Value, 2> args{Blueprint::Value{room},
                                                     Blueprint::Value{player}};
        return std::get<std::int64_t>(*instance.Call("do_join", args));
    };
    const std::int64_t joined = joinAs(1, "amy");
    EXPECT_NE(joined, 0);
    EXPECT_EQ(CallStr(instance, "kind", joined), "pending");
    harness.Settle(1200U);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_TRUE(results[0].second);
    EXPECT_EQ(CallStr(instance, "kind", joined), "roster");
    EXPECT_EQ(CallInt(instance, "roster_count", joined), 2);
    // Member-id order: the script facade is client 1, so "amy" sorts first.
    EXPECT_EQ(CallStr2(instance, "roster_name", joined, 0), "amy");
    EXPECT_EQ(CallStr2(instance, "roster_name", joined, 1), "bob");
    EXPECT_EQ(CallStr2(instance, "roster_name", joined, 2), "");
    EXPECT_EQ(CallInt(instance, "my_room"), 1);
    EXPECT_EQ(CallInt(instance, "my_roster_count"), 2);
    EXPECT_EQ(CallStr(instance, "my_roster_name", 0), "amy");

    const std::int64_t left = CallInt(instance, "do_leave");
    harness.Settle(1300U);
    EXPECT_EQ(CallStr(instance, "kind", left), "left");
    EXPECT_TRUE(CallBool(instance, "left", left));
    EXPECT_EQ(CallInt(instance, "my_room"), 0);
    EXPECT_EQ(CallInt(instance, "my_roster_count"), 0);
    EXPECT_EQ(CallStr(instance, "my_roster_name", 0), "");

    const std::int64_t ghost = joinAs(999, "amy");
    harness.Settle(1400U);
    ASSERT_EQ(results.size(), 3U);
    EXPECT_FALSE(results[2].second);
    EXPECT_EQ(CallStr(instance, "kind", ghost), "error");
    EXPECT_EQ(CallStr(instance, "err", ghost), "unknown room '999'");
    EXPECT_EQ(CallInt(instance, "roster_count", ghost), 0);

    // A pending call refuses release; abandoning it drops the id without a sink fire.
    const std::size_t settled = results.size();
    const std::int64_t dangling = CallInt(instance, "do_list");
    EXPECT_FALSE(CallBool(instance, "release", dangling));
    EXPECT_EQ(CallStr(instance, "kind", dangling), "pending");
    EXPECT_EQ(CallInt(instance, "cancel_all"), 1);
    EXPECT_EQ(CallStr(instance, "kind", dangling), "none");
    harness.Settle(1500U);
    EXPECT_EQ(results.size(), settled);
    EXPECT_EQ(CallInt(instance, "cancel_all"), 0);

    EXPECT_TRUE(CallBool(instance, "release", ghost));
    EXPECT_EQ(CallStr(instance, "kind", ghost), "none");
    EXPECT_FALSE(CallBool(instance, "release", ghost));
}

TEST_F(BlueprintLobbyBindingsTest, Script_ReceivesRosterPushes) {
    ScriptLobbyHarness harness;
    harness.AddFacade();
    harness.AddFacade();
    ASSERT_TRUE(harness.Establish(1000U));

    std::vector<std::uint32_t> pushes;
    Blueprint::BlueprintInstance* live = nullptr;
    Core::BlueprintObjectHost host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntity(),
                                     nullptr,
                                     nullptr,
                                     Core::BlueprintObjectHost::InstanceResolver{},
                                     nullptr,
                                     Core::RpcResponseSink{},
                                     &harness.facades[0],
                                     Core::LobbyResultSink{},
                                     [&](const std::uint32_t room) {
                                         pushes.push_back(room);
                                         if (live != nullptr) {
                                             const std::array<Blueprint::Value, 1> args{
                                                 Blueprint::Value{
                                                     static_cast<std::int64_t>(room)}};
                                             static_cast<void>(
                                                 live->RaiseEvent("lobby_roster", args));
                                         }
                                     }};
    const auto program = CompileOrFail(kLobbyScript, host);
    ASSERT_NE(program, nullptr);
    Blueprint::BlueprintInstance instance(program, host);
    live = &instance;

    const std::array<Blueprint::Value, 3> create{Blueprint::Value{std::string{"arena"}},
                                                   Blueprint::Value{std::string{"amy"}},
                                                   Blueprint::Value{std::int64_t{4}}};
    const std::int64_t made = std::get<std::int64_t>(*instance.Call("do_create", create));
    EXPECT_NE(made, 0);
    harness.Settle(1100U);
    EXPECT_EQ(CallInt(instance, "my_room"), 1);
    EXPECT_TRUE(pushes.empty());

    bool joined = false;
    harness.facades[1].JoinRoom(
        1U, "cat",
        [&](std::vector<std::string> roster, const std::string& error) {
            joined = true;
            EXPECT_TRUE(error.empty());
            EXPECT_EQ(roster, (std::vector<std::string>{"amy", "cat"}));
        });
    harness.Settle(1200U);
    ASSERT_TRUE(joined);
    ASSERT_EQ(pushes.size(), 1U);
    EXPECT_EQ(pushes[0], 1U);
    EXPECT_EQ(CallInt(instance, "my_roster_count"), 2);
    EXPECT_EQ(CallStr(instance, "my_roster_name", 1), "cat");

    const std::array<Blueprint::Value, 1> eventArgs{Blueprint::Value{std::int64_t{1}}};
    EXPECT_TRUE(instance.RaiseEvent("lobby_roster", eventArgs));

    bool left = false;
    harness.facades[1].LeaveRoom([&](bool wasLeft, const std::string& error) {
        left = true;
        EXPECT_TRUE(wasLeft);
        EXPECT_TRUE(error.empty());
    });
    harness.Settle(1300U);
    ASSERT_TRUE(left);
    ASSERT_EQ(pushes.size(), 2U);
    EXPECT_EQ(pushes[1], 1U);
    EXPECT_EQ(CallInt(instance, "my_roster_count"), 1);
}

TEST_F(BlueprintLobbyBindingsTest, Host_AbandonsAndUnhooksOnDestruction) {
    ScriptLobbyHarness harness;
    harness.AddFacade();
    harness.AddFacade();
    harness.AddFacade();
    ASSERT_TRUE(harness.Establish(1000U));

    std::vector<std::pair<std::uint32_t, bool>> results;
    std::vector<std::uint32_t> pushes;
    {
        Core::BlueprintObjectHost host{entityManager,
                                         nullptr,
                                         entityManager.CreateEntity(),
                                         nullptr,
                                         nullptr,
                                         Core::BlueprintObjectHost::InstanceResolver{},
                                         nullptr,
                                         Core::RpcResponseSink{},
                                         &harness.facades[0],
                                         [&](const std::uint32_t id, const bool ok) {
                                             results.emplace_back(id, ok);
                                         },
                                         [&](const std::uint32_t room) {
                                             pushes.push_back(room);
                                         }};
        const std::span<const Blueprint::Value> noArgs{};
        const std::int64_t listed =
            std::get<std::int64_t>(host.CallFunction("lobby.list", noArgs));
        EXPECT_NE(listed, 0);
    }

    // The host is gone but the peer lives on: the exchange still runs, the completion has
    // nowhere to go, and the late response reads as stale.
    harness.Settle(1100U);
    harness.Settle(1200U);
    EXPECT_TRUE(results.empty());
    EXPECT_TRUE(pushes.empty());
    EXPECT_EQ(harness.facades[0].Stats().responsesIgnoredStale, 1U);

    // The roster push unhooked with it: joins still update the peer's cache, silently.
    bool made = false;
    harness.facades[1].CreateRoom(
        "arena", "m1", 4U, [&](Network::LobbyRoomId id, const std::string& error) {
            made = true;
            EXPECT_EQ(id, 1U);
            EXPECT_TRUE(error.empty());
        });
    harness.Settle(1300U);
    ASSERT_TRUE(made);
    bool joined = false;
    harness.facades[0].JoinRoom(
        1U, "ghost", [&](std::vector<std::string>, const std::string& error) {
            joined = true;
            EXPECT_TRUE(error.empty());
        });
    harness.Settle(1400U);
    ASSERT_TRUE(joined);
    bool third = false;
    harness.facades[2].JoinRoom(
        1U, "m2", [&](std::vector<std::string>, const std::string& error) {
            third = true;
            EXPECT_TRUE(error.empty());
        });
    harness.Settle(1500U);
    ASSERT_TRUE(third);
    EXPECT_TRUE(pushes.empty());
    EXPECT_EQ(harness.facades[0].CachedRoster(),
              (std::vector<std::string>{"ghost", "m1", "m2"}));
}

TEST_F(BlueprintLobbyBindingsTest, Script_QueriesRostersWithoutJoining) {
    ScriptLobbyHarness harness;
    harness.AddFacade();
    harness.AddFacade();
    harness.AddFacade();
    ASSERT_TRUE(harness.Establish(1000U));

    bool made = false;
    harness.facades[1].CreateRoom(
        "arena", "bob", 4U, [&](Network::LobbyRoomId id, const std::string& error) {
            made = true;
            EXPECT_EQ(id, 1U);
            EXPECT_TRUE(error.empty());
        });
    harness.Settle(1100U);
    ASSERT_TRUE(made);
    bool joined = false;
    harness.facades[2].JoinRoom(
        1U, "cat", [&](std::vector<std::string>, const std::string& error) {
            joined = true;
            EXPECT_TRUE(error.empty());
        });
    harness.Settle(1200U);
    ASSERT_TRUE(joined);

    std::vector<std::uint32_t> pushes;
    Core::BlueprintObjectHost host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntity(),
                                     nullptr,
                                     nullptr,
                                     Core::BlueprintObjectHost::InstanceResolver{},
                                     nullptr,
                                     Core::RpcResponseSink{},
                                     &harness.facades[0],
                                     Core::LobbyResultSink{},
                                     [&](const std::uint32_t room) { pushes.push_back(room); }};
    const auto program = CompileOrFail(kLobbyScript, host);
    ASSERT_NE(program, nullptr);
    Blueprint::BlueprintInstance instance(program, host);

    const std::int64_t queried = CallInt(instance, "do_roster", 1);
    EXPECT_NE(queried, 0);
    harness.Settle(1300U);
    EXPECT_EQ(CallStr(instance, "kind", queried), "roster");
    EXPECT_EQ(CallInt(instance, "roster_count", queried), 2);
    EXPECT_EQ(CallStr2(instance, "roster_name", queried, 0), "bob");
    EXPECT_EQ(CallStr2(instance, "roster_name", queried, 1), "cat");
    EXPECT_EQ(CallInt(instance, "my_room"), 0);
    EXPECT_TRUE(pushes.empty());

    const std::int64_t ghost = CallInt(instance, "do_roster", 999);
    harness.Settle(1400U);
    EXPECT_EQ(CallStr(instance, "kind", ghost), "error");
    EXPECT_EQ(CallStr(instance, "err", ghost), "unknown room '999'");
}

TEST_F(BlueprintLobbyBindingsTest, Script_SendFailuresAnswerZero) {
    ScriptLobbyHarness harness;
    harness.AddFacade();
    // Never established: every call fails its send synchronously, before any pump.

    std::vector<std::pair<std::uint32_t, bool>> results;
    std::vector<std::uint32_t> pushes;
    Core::BlueprintObjectHost host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntity(),
                                     nullptr,
                                     nullptr,
                                     Core::BlueprintObjectHost::InstanceResolver{},
                                     nullptr,
                                     Core::RpcResponseSink{},
                                     &harness.facades[0],
                                     [&](const std::uint32_t id, const bool ok) {
                                         results.emplace_back(id, ok);
                                     },
                                     [&](const std::uint32_t room) { pushes.push_back(room); }};
    const auto program = CompileOrFail(kLobbyScript, host);
    ASSERT_NE(program, nullptr);
    Blueprint::BlueprintInstance instance(program, host);

    EXPECT_EQ(CallInt(instance, "do_list"), 0);
    const std::array<Blueprint::Value, 3> create{Blueprint::Value{std::string{"arena"}},
                                                   Blueprint::Value{std::string{"amy"}},
                                                   Blueprint::Value{std::int64_t{4}}};
    EXPECT_EQ(std::get<std::int64_t>(*instance.Call("do_create", create)), 0);
    const std::array<Blueprint::Value, 2> join{Blueprint::Value{std::int64_t{1}},
                                                 Blueprint::Value{std::string{"amy"}}};
    EXPECT_EQ(std::get<std::int64_t>(*instance.Call("do_join", join)), 0);
    EXPECT_EQ(CallInt(instance, "do_leave"), 0);
    EXPECT_EQ(CallInt(instance, "do_roster", 1), 0);
    EXPECT_TRUE(results.empty());
    EXPECT_TRUE(pushes.empty());
    EXPECT_EQ(CallStr(instance, "kind", 0), "none");
    EXPECT_EQ(CallStr(instance, "err", 0), "");
}

} // namespace
} // namespace UVE::Scene::Tests
