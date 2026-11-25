// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_lobby.h"

#include "network_rpc.h"
#include "network_session.h"
#include "blueprint_value.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

struct LobbyHarness final {
    std::vector<LoopbackTransportPair> links;
    std::vector<NetworkLobbyClient> facades;
    std::vector<LobbyClientId> facadeIds;
    std::vector<NetworkRpc> raw;
    std::vector<LobbyClientId> rawIds;
    NetworkLobby lobby;

    // Note: growing these vectors moves live facades around, which is exactly the
    // relocation the roster pushes must survive - multi-facade tests cover it.
    void AddFacade() {
        LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
        NetworkSession client(SessionRole::Client,
                                 std::make_unique<LoopbackEndpoint>(pair.a));
        NetworkSession server(SessionRole::Server,
                                 std::make_unique<LoopbackEndpoint>(pair.b));
        facadeIds.push_back(lobby.AddClient(std::move(server)));
        facades.emplace_back(std::move(client));
        links.push_back(std::move(pair));
    }

    void AddRaw() {
        LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
        NetworkSession client(SessionRole::Client,
                                 std::make_unique<LoopbackEndpoint>(pair.a));
        NetworkSession server(SessionRole::Server,
                                 std::make_unique<LoopbackEndpoint>(pair.b));
        rawIds.push_back(lobby.AddClient(std::move(server)));
        raw.emplace_back(std::move(client));
        links.push_back(std::move(pair));
    }

    void UpdateAll(const std::uint64_t nowMs) {
        lobby.Update(nowMs);
        for (auto& facade : facades) {
            facade.Update(nowMs);
        }
        for (auto& peer : raw) {
            peer.Update(nowMs);
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
        for (auto& peer : raw) {
            if (!peer.Session().Connect(nowMs)) {
                return false;
            }
        }
        UpdateAll(nowMs);
        UpdateAll(nowMs);
        for (auto& facade : facades) {
            if (facade.Session().State() != SessionState::Established) {
                return false;
            }
        }
        for (auto& peer : raw) {
            if (peer.Session().State() != SessionState::Established) {
                return false;
            }
        }
        for (const LobbyClientId id : facadeIds) {
            const NetworkRpc* server = lobby.ClientRpc(id);
            if (server == nullptr ||
                server->Session().State() != SessionState::Established) {
                return false;
            }
        }
        for (const LobbyClientId id : rawIds) {
            const NetworkRpc* server = lobby.ClientRpc(id);
            if (server == nullptr ||
                server->Session().State() != SessionState::Established) {
                return false;
            }
        }
        return true;
    }
};

[[nodiscard]] LobbyHarness MakeLobby(const std::size_t facadeCount) {
    LobbyHarness harness;
    for (std::size_t i = 0U; i < facadeCount; ++i) {
        harness.AddFacade();
    }
    return harness;
}

[[nodiscard]] Blueprint::Value Int(const std::int64_t number) {
    return Blueprint::Value{number};
}

[[nodiscard]] Blueprint::Value Text(const std::string& text) {
    return Blueprint::Value{text};
}

struct RoomsResult final {
    bool fired = false;
    std::vector<LobbyRoomInfo> rooms;
    std::string error;
};

struct RoomResult final {
    bool fired = false;
    LobbyRoomId id = kInvalidLobbyRoomId;
    std::string error;
};

struct RosterResult final {
    bool fired = false;
    std::vector<std::string> roster;
    std::string error;
};

struct LeaveResult final {
    bool fired = false;
    bool left = false;
    std::string error;
};

struct RawResult final {
    bool fired = false;
    bool ok = false;
    Blueprint::Value value;
    std::string error;
};

struct PushLog final {
    std::vector<std::pair<LobbyRoomId, std::vector<std::string>>> pushes;
};

TEST(NetworkLobbyTest, Lifecycle_AddRemoveGuards) {
    LobbyHarness harness = MakeLobby(2U);
    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(harness.facadeIds[0], 1U);
    EXPECT_EQ(harness.facadeIds[1], 2U);
    EXPECT_NE(harness.lobby.ClientRpc(harness.facadeIds[0]), nullptr);
    EXPECT_EQ(harness.lobby.ClientRpc(999U), nullptr);
    EXPECT_EQ(harness.lobby.PeerCount(), 2U);
    EXPECT_EQ(harness.lobby.RoomCount(), 0U);
    EXPECT_EQ(harness.lobby.Stats(), LobbyStats{});

    EXPECT_FALSE(harness.lobby.RemoveClient(999U));
    EXPECT_TRUE(harness.lobby.RemoveClient(harness.facadeIds[0]));
    EXPECT_FALSE(harness.lobby.RemoveClient(harness.facadeIds[0]));
    EXPECT_EQ(harness.lobby.ClientRpc(harness.facadeIds[0]), nullptr);
    EXPECT_EQ(harness.lobby.PeerCount(), 1U);
}

TEST(NetworkLobbyTest, Rooms_CreateListAndQuery) {
    LobbyHarness harness = MakeLobby(2U);
    ASSERT_TRUE(harness.Establish(1000U));

    RoomResult created;
    harness.facades[0].CreateRoom(
        "arena", "amy", 4U, [&](LobbyRoomId id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    harness.Settle(1100U);
    ASSERT_TRUE(created.fired);
    EXPECT_TRUE(created.error.empty());
    EXPECT_EQ(created.id, 1U);
    EXPECT_EQ(harness.facades[0].CurrentRoom(), 1U);
    EXPECT_EQ(harness.facades[0].CachedRoster(), std::vector<std::string>{"amy"});

    RoomsResult listed;
    harness.facades[1].ListRooms(
        [&](std::vector<LobbyRoomInfo> rooms, const std::string& error) {
            listed.fired = true;
            listed.rooms = std::move(rooms);
            listed.error = error;
        });
    harness.Settle(1200U);
    ASSERT_TRUE(listed.fired);
    EXPECT_TRUE(listed.error.empty());
    ASSERT_EQ(listed.rooms.size(), 1U);
    EXPECT_EQ(listed.rooms[0], (LobbyRoomInfo{1U, "arena", 1U, 4U}));

    RoomResult created2;
    harness.facades[1].CreateRoom(
        "dome", "bob", 2U, [&](LobbyRoomId id, const std::string& error) {
            created2.fired = true;
            created2.id = id;
            created2.error = error;
        });
    harness.Settle(1300U);
    ASSERT_TRUE(created2.fired);
    EXPECT_EQ(created2.id, 2U);

    listed = RoomsResult{};
    harness.facades[0].ListRooms(
        [&](std::vector<LobbyRoomInfo> rooms, const std::string& error) {
            listed.fired = true;
            listed.rooms = std::move(rooms);
            listed.error = error;
        });
    harness.Settle(1400U);
    ASSERT_TRUE(listed.fired);
    ASSERT_EQ(listed.rooms.size(), 2U);
    EXPECT_EQ(listed.rooms[0], (LobbyRoomInfo{1U, "arena", 1U, 4U}));
    EXPECT_EQ(listed.rooms[1], (LobbyRoomInfo{2U, "dome", 1U, 2U}));

    RosterResult roster;
    harness.facades[1].QueryRoster(
        1U, [&](std::vector<std::string> names, const std::string& error) {
            roster.fired = true;
            roster.roster = std::move(names);
            roster.error = error;
        });
    harness.Settle(1500U);
    ASSERT_TRUE(roster.fired);
    EXPECT_TRUE(roster.error.empty());
    EXPECT_EQ(roster.roster, std::vector<std::string>{"amy"});
    EXPECT_EQ(harness.facades[1].CurrentRoom(), 2U);

    const LobbyStats stats = harness.lobby.Stats();
    EXPECT_EQ(stats.roomsCreated, 2U);
    EXPECT_EQ(stats.roomsRemoved, 0U);
    EXPECT_EQ(stats.joinsAccepted, 0U);
    EXPECT_EQ(stats.leavesAccepted, 0U);
    EXPECT_EQ(stats.notificationsSent, 0U);
}

TEST(NetworkLobbyTest, Join_RulesRejectAndAccept) {
    LobbyHarness harness = MakeLobby(3U);
    ASSERT_TRUE(harness.Establish(1000U));
    PushLog amyPushes;
    harness.facades[0].SetRosterHandler(
        [&](LobbyRoomId room, const std::vector<std::string>& roster) {
            amyPushes.pushes.emplace_back(room, roster);
        });

    RoomResult created;
    harness.facades[0].CreateRoom(
        "arena", "amy", 2U, [&](LobbyRoomId id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    harness.Settle(1100U);
    ASSERT_TRUE(created.fired);
    EXPECT_EQ(created.id, 1U);

    auto joinAs = [&](const std::size_t facade, const LobbyRoomId room,
                      const std::string& name, const std::uint64_t nowMs) {
        RosterResult result;
        harness.facades[facade].JoinRoom(
            room, name, [&](std::vector<std::string> roster, const std::string& error) {
                result.fired = true;
                result.roster = std::move(roster);
                result.error = error;
            });
        harness.Settle(nowMs);
        return result;
    };

    const RosterResult ghost = joinAs(1U, 999U, "bob", 1200U);
    ASSERT_TRUE(ghost.fired);
    EXPECT_TRUE(ghost.roster.empty());
    EXPECT_EQ(ghost.error, "unknown room '999'");
    EXPECT_EQ(harness.facades[1].CurrentRoom(), kInvalidLobbyRoomId);

    const RosterResult clash = joinAs(1U, 1U, "amy", 1300U);
    ASSERT_TRUE(clash.fired);
    EXPECT_EQ(clash.error, "name 'amy' is taken in room '1'");

    const RosterResult joined = joinAs(1U, 1U, "bob", 1400U);
    ASSERT_TRUE(joined.fired);
    EXPECT_TRUE(joined.error.empty());
    EXPECT_EQ(joined.roster, (std::vector<std::string>{"amy", "bob"}));
    EXPECT_EQ(harness.facades[1].CurrentRoom(), 1U);
    EXPECT_EQ(harness.facades[1].CachedRoster(), joined.roster);

    ASSERT_EQ(amyPushes.pushes.size(), 1U);
    EXPECT_EQ(amyPushes.pushes[0].first, 1U);
    EXPECT_EQ(amyPushes.pushes[0].second, (std::vector<std::string>{"amy", "bob"}));
    EXPECT_EQ(harness.facades[0].CachedRoster(), amyPushes.pushes[0].second);

    const RosterResult full = joinAs(2U, 1U, "cat", 1500U);
    ASSERT_TRUE(full.fired);
    EXPECT_EQ(full.error, "room '1' is full");

    const RosterResult quiet = joinAs(1U, 1U, "bob", 1600U);
    ASSERT_TRUE(quiet.fired);
    EXPECT_TRUE(quiet.error.empty());
    EXPECT_EQ(quiet.roster, (std::vector<std::string>{"amy", "bob"}));
    EXPECT_EQ(amyPushes.pushes.size(), 1U);

    const RosterResult clashRename = joinAs(1U, 1U, "amy", 1700U);
    ASSERT_TRUE(clashRename.fired);
    EXPECT_EQ(clashRename.error, "name 'amy' is taken in room '1'");

    const RosterResult renamed = joinAs(1U, 1U, "bobby", 1800U);
    ASSERT_TRUE(renamed.fired);
    EXPECT_TRUE(renamed.error.empty());
    EXPECT_EQ(renamed.roster, (std::vector<std::string>{"amy", "bobby"}));
    ASSERT_EQ(amyPushes.pushes.size(), 2U);
    EXPECT_EQ(amyPushes.pushes[1].second, (std::vector<std::string>{"amy", "bobby"}));

    const LobbyStats stats = harness.lobby.Stats();
    EXPECT_EQ(stats.joinsAccepted, 3U);
    EXPECT_EQ(stats.joinsRejected, 4U);
    EXPECT_EQ(stats.notificationsSent, 2U);
    EXPECT_EQ(stats.notificationsDropped, 0U);
    EXPECT_EQ(stats.leavesAccepted, 0U);
}

TEST(NetworkLobbyTest, Leave_EmptiesAndRemovesRooms) {
    LobbyHarness harness = MakeLobby(2U);
    ASSERT_TRUE(harness.Establish(1000U));
    PushLog amyPushes;
    harness.facades[0].SetRosterHandler(
        [&](LobbyRoomId room, const std::vector<std::string>& roster) {
            amyPushes.pushes.emplace_back(room, roster);
        });

    RoomResult created;
    harness.facades[0].CreateRoom(
        "arena", "amy", 4U, [&](LobbyRoomId id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    harness.Settle(1100U);
    ASSERT_TRUE(created.fired);
    EXPECT_EQ(created.id, 1U);

    RosterResult joined;
    harness.facades[1].JoinRoom(
        1U, "bob", [&](std::vector<std::string> roster, const std::string& error) {
            joined.fired = true;
            joined.roster = std::move(roster);
            joined.error = error;
        });
    harness.Settle(1200U);
    ASSERT_TRUE(joined.fired);
    EXPECT_TRUE(joined.error.empty());
    ASSERT_EQ(amyPushes.pushes.size(), 1U);

    auto leaveAs = [&](const std::size_t facade, const std::uint64_t nowMs) {
        LeaveResult result;
        harness.facades[facade].LeaveRoom([&](bool left, const std::string& error) {
            result.fired = true;
            result.left = left;
            result.error = error;
        });
        harness.Settle(nowMs);
        return result;
    };

    const LeaveResult bobLeft = leaveAs(1U, 1300U);
    ASSERT_TRUE(bobLeft.fired);
    EXPECT_TRUE(bobLeft.left);
    EXPECT_TRUE(bobLeft.error.empty());
    EXPECT_EQ(harness.facades[1].CurrentRoom(), kInvalidLobbyRoomId);
    EXPECT_TRUE(harness.facades[1].CachedRoster().empty());
    ASSERT_EQ(amyPushes.pushes.size(), 2U);
    EXPECT_EQ(amyPushes.pushes[1].second, std::vector<std::string>{"amy"});

    const LeaveResult bobAgain = leaveAs(1U, 1400U);
    ASSERT_TRUE(bobAgain.fired);
    EXPECT_FALSE(bobAgain.left);
    EXPECT_EQ(bobAgain.error, "not in a room");

    const LeaveResult amyLeft = leaveAs(0U, 1500U);
    ASSERT_TRUE(amyLeft.fired);
    EXPECT_TRUE(amyLeft.left);
    EXPECT_EQ(harness.lobby.RoomCount(), 0U);
    EXPECT_EQ(amyPushes.pushes.size(), 2U);

    RoomsResult listed;
    harness.facades[0].ListRooms(
        [&](std::vector<LobbyRoomInfo> rooms, const std::string& error) {
            listed.fired = true;
            listed.rooms = std::move(rooms);
            listed.error = error;
        });
    harness.Settle(1600U);
    ASSERT_TRUE(listed.fired);
    EXPECT_TRUE(listed.rooms.empty());

    RosterResult gone;
    harness.facades[1].QueryRoster(
        1U, [&](std::vector<std::string> roster, const std::string& error) {
            gone.fired = true;
            gone.roster = std::move(roster);
            gone.error = error;
        });
    harness.Settle(1700U);
    ASSERT_TRUE(gone.fired);
    EXPECT_EQ(gone.error, "unknown room '1'");

    const LobbyStats stats = harness.lobby.Stats();
    EXPECT_EQ(stats.roomsCreated, 1U);
    EXPECT_EQ(stats.roomsRemoved, 1U);
    EXPECT_EQ(stats.leavesAccepted, 2U);
    EXPECT_EQ(stats.leavesRejected, 1U);
}

TEST(NetworkLobbyTest, Move_CreateAndJoinLeaveTheOldRoom) {
    LobbyHarness harness = MakeLobby(2U);
    ASSERT_TRUE(harness.Establish(1000U));

    RoomResult first;
    harness.facades[0].CreateRoom(
        "arena", "amy", 4U, [&](LobbyRoomId id, const std::string& error) {
            first.fired = true;
            first.id = id;
            first.error = error;
        });
    harness.Settle(1100U);
    ASSERT_TRUE(first.fired);
    EXPECT_EQ(first.id, 1U);

    RoomResult second;
    harness.facades[0].CreateRoom(
        "dome", "amy2", 4U, [&](LobbyRoomId id, const std::string& error) {
            second.fired = true;
            second.id = id;
            second.error = error;
        });
    harness.Settle(1200U);
    ASSERT_TRUE(second.fired);
    EXPECT_EQ(second.id, 2U);
    EXPECT_EQ(harness.facades[0].CurrentRoom(), 2U);
    EXPECT_EQ(harness.facades[0].CachedRoster(), std::vector<std::string>{"amy2"});
    EXPECT_EQ(harness.lobby.RoomCount(), 1U);

    RosterResult ghost;
    harness.facades[1].JoinRoom(
        1U, "bob", [&](std::vector<std::string> roster, const std::string& error) {
            ghost.fired = true;
            ghost.roster = std::move(roster);
            ghost.error = error;
        });
    harness.Settle(1300U);
    ASSERT_TRUE(ghost.fired);
    EXPECT_EQ(ghost.error, "unknown room '1'");

    RoomResult side;
    harness.facades[1].CreateRoom(
        "side", "bob", 4U, [&](LobbyRoomId id, const std::string& error) {
            side.fired = true;
            side.id = id;
            side.error = error;
        });
    harness.Settle(1400U);
    ASSERT_TRUE(side.fired);
    EXPECT_EQ(side.id, 3U);

    PushLog amyPushes;
    harness.facades[0].SetRosterHandler(
        [&](LobbyRoomId room, const std::vector<std::string>& roster) {
            amyPushes.pushes.emplace_back(room, roster);
        });
    RosterResult moved;
    harness.facades[1].JoinRoom(
        2U, "bob", [&](std::vector<std::string> roster, const std::string& error) {
            moved.fired = true;
            moved.roster = std::move(roster);
            moved.error = error;
        });
    harness.Settle(1500U);
    ASSERT_TRUE(moved.fired);
    EXPECT_TRUE(moved.error.empty());
    EXPECT_EQ(moved.roster, (std::vector<std::string>{"amy2", "bob"}));
    EXPECT_EQ(harness.facades[1].CurrentRoom(), 2U);
    EXPECT_EQ(harness.lobby.RoomCount(), 1U);
    ASSERT_EQ(amyPushes.pushes.size(), 1U);
    EXPECT_EQ(amyPushes.pushes[0].second, (std::vector<std::string>{"amy2", "bob"}));

    const LobbyStats stats = harness.lobby.Stats();
    EXPECT_EQ(stats.roomsCreated, 3U);
    EXPECT_EQ(stats.roomsRemoved, 2U);
    EXPECT_EQ(stats.leavesAccepted, 2U);
    EXPECT_EQ(stats.joinsAccepted, 1U);
    EXPECT_EQ(stats.joinsRejected, 1U);
}

TEST(NetworkLobbyTest, Notify_MembersWithoutTheHandlerStillPlay) {
    LobbyHarness harness = MakeLobby(1U);
    harness.AddRaw();
    ASSERT_TRUE(harness.Establish(1000U));
    PushLog amyPushes;
    harness.facades[0].SetRosterHandler(
        [&](LobbyRoomId room, const std::vector<std::string>& roster) {
            amyPushes.pushes.emplace_back(room, roster);
        });

    RoomResult created;
    harness.facades[0].CreateRoom(
        "arena", "amy", 4U, [&](LobbyRoomId id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    harness.Settle(1100U);
    ASSERT_TRUE(created.fired);
    EXPECT_EQ(created.id, 1U);

    auto callRaw = [&](const std::string& method,
                       const std::vector<Blueprint::Value>& args,
                       const std::uint64_t nowMs) {
        RawResult result;
        const RpcCallId call = harness.raw[0].Call(
            method, args, [&](RpcCallId, const RpcOutcome& outcome) {
                result.fired = true;
                result.ok = outcome.ok;
                result.value = outcome.value;
                result.error = outcome.error;
            });
        EXPECT_NE(call, kInvalidRpcCallId);
        harness.Settle(nowMs);
        return result;
    };

    const RawResult joined =
        callRaw(kLobbyJoinMethod, {Int(1LL), Text("cat")}, 1200U);
    ASSERT_TRUE(joined.fired);
    EXPECT_TRUE(joined.ok) << joined.error;
    ASSERT_EQ(amyPushes.pushes.size(), 1U);
    EXPECT_EQ(amyPushes.pushes[0].second, (std::vector<std::string>{"amy", "cat"}));

    LeaveResult amyLeft;
    harness.facades[0].LeaveRoom([&](bool left, const std::string& error) {
        amyLeft.fired = true;
        amyLeft.left = left;
        amyLeft.error = error;
    });
    harness.Settle(1300U);
    ASSERT_TRUE(amyLeft.fired);
    EXPECT_TRUE(amyLeft.left);
    EXPECT_EQ(harness.raw[0].Stats().callsReceived, 1U);
    EXPECT_EQ(harness.raw[0].Stats().responsesSent, 1U);

    const RawResult roster = callRaw(kLobbyRosterMethod, {Int(1LL)}, 1400U);
    ASSERT_TRUE(roster.fired);
    EXPECT_TRUE(roster.ok) << roster.error;

    const LobbyStats stats = harness.lobby.Stats();
    EXPECT_EQ(stats.notificationsSent, 2U);
    EXPECT_EQ(stats.notificationsDropped, 0U);
}

TEST(NetworkLobbyTest, Removal_KicksCleanUpAndDisconnectsAreFound) {
    LobbyHarness harness = MakeLobby(2U);
    ASSERT_TRUE(harness.Establish(1000U));
    PushLog amyPushes;
    harness.facades[0].SetRosterHandler(
        [&](LobbyRoomId room, const std::vector<std::string>& roster) {
            amyPushes.pushes.emplace_back(room, roster);
        });

    RoomResult created;
    harness.facades[0].CreateRoom(
        "arena", "amy", 4U, [&](LobbyRoomId id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    harness.Settle(1100U);
    ASSERT_TRUE(created.fired);

    RosterResult joined;
    harness.facades[1].JoinRoom(
        1U, "bob", [&](std::vector<std::string> roster, const std::string& error) {
            joined.fired = true;
            joined.roster = std::move(roster);
            joined.error = error;
        });
    harness.Settle(1200U);
    ASSERT_TRUE(joined.fired);
    EXPECT_TRUE(joined.error.empty());
    ASSERT_EQ(amyPushes.pushes.size(), 1U);

    EXPECT_TRUE(harness.lobby.RemoveClient(harness.facadeIds[1]));
    harness.Settle(1300U);
    ASSERT_EQ(amyPushes.pushes.size(), 2U);
    EXPECT_EQ(amyPushes.pushes[1].second, std::vector<std::string>{"amy"});
    EXPECT_EQ(harness.lobby.PeerCount(), 1U);
    EXPECT_EQ(harness.lobby.RoomCount(), 1U);

    EXPECT_TRUE(harness.lobby.RemoveClient(harness.facadeIds[0]));
    EXPECT_EQ(harness.lobby.RoomCount(), 0U);
    EXPECT_EQ(harness.lobby.PeerCount(), 0U);

    const LobbyStats stats = harness.lobby.Stats();
    EXPECT_EQ(stats.roomsRemoved, 1U);
    EXPECT_EQ(stats.leavesAccepted, 2U);

    LobbyHarness hungUp = MakeLobby(1U);
    ASSERT_TRUE(hungUp.Establish(2000U));
    RoomResult solo;
    hungUp.facades[0].CreateRoom(
        "solo", "sam", 4U, [&](LobbyRoomId id, const std::string& error) {
            solo.fired = true;
            solo.id = id;
            solo.error = error;
        });
    hungUp.Settle(2100U);
    ASSERT_TRUE(solo.fired);
    EXPECT_EQ(solo.id, 1U);
    ASSERT_TRUE(hungUp.facades[0].Session().Disconnect());
    hungUp.UpdateAll(2200U);
    const NetworkRpc* peer = hungUp.lobby.ClientRpc(hungUp.facadeIds[0]);
    ASSERT_NE(peer, nullptr);
    EXPECT_EQ(peer->Session().State(), SessionState::Disconnected);
    EXPECT_EQ(peer->Session().CloseReason(), SessionCloseReason::RemoteDisconnect);
    EXPECT_TRUE(hungUp.lobby.RemoveClient(hungUp.facadeIds[0]));
    EXPECT_EQ(hungUp.lobby.RoomCount(), 0U);
    EXPECT_EQ(hungUp.lobby.Stats().roomsRemoved, 1U);
}

TEST(NetworkLobbyTest, Protocol_RawShapesAreRejected) {
    LobbyHarness harness = MakeLobby(0U);
    harness.AddRaw();
    ASSERT_TRUE(harness.Establish(1000U));

    auto callRaw = [&](const std::string& method,
                       const std::vector<Blueprint::Value>& args,
                       const std::uint64_t nowMs) {
        RawResult result;
        const RpcCallId call = harness.raw[0].Call(
            method, args, [&](RpcCallId, const RpcOutcome& outcome) {
                result.fired = true;
                result.ok = outcome.ok;
                result.value = outcome.value;
                result.error = outcome.error;
            });
        EXPECT_NE(call, kInvalidRpcCallId);
        harness.Settle(nowMs);
        return result;
    };

    std::uint64_t nowMs = 1100U;
    auto attempt = [&](const std::string& method,
                       const std::vector<Blueprint::Value>& args) {
        RawResult result = callRaw(method, args, nowMs);
        nowMs += 100U;
        return result;
    };

    const RawResult listArgs = attempt(kLobbyListMethod, {Int(1LL)});
    ASSERT_TRUE(listArgs.fired);
    EXPECT_FALSE(listArgs.ok);
    EXPECT_EQ(listArgs.error, "lobby.list takes no arguments");

    const RawResult shortCreate =
        attempt(kLobbyCreateMethod, {Text("r"), Text("p")});
    ASSERT_TRUE(shortCreate.fired);
    EXPECT_FALSE(shortCreate.ok);
    EXPECT_EQ(shortCreate.error, "lobby.create takes (name, player, max)");

    const RawResult emptyRoom =
        attempt(kLobbyCreateMethod, {Text(""), Text("p"), Int(4LL)});
    ASSERT_TRUE(emptyRoom.fired);
    EXPECT_FALSE(emptyRoom.ok);
    EXPECT_EQ(emptyRoom.error, "room name is empty");

    const RawResult emptyPlayer =
        attempt(kLobbyCreateMethod, {Text("r"), Text(""), Int(4LL)});
    ASSERT_TRUE(emptyPlayer.fired);
    EXPECT_FALSE(emptyPlayer.ok);
    EXPECT_EQ(emptyPlayer.error, "player name is empty");

    const RawResult zeroMax =
        attempt(kLobbyCreateMethod, {Text("r"), Text("p"), Int(0LL)});
    ASSERT_TRUE(zeroMax.fired);
    EXPECT_FALSE(zeroMax.ok);
    EXPECT_EQ(zeroMax.error, "max players must be at least 1");

    const RawResult shortJoin = attempt(kLobbyJoinMethod, {Int(1LL)});
    ASSERT_TRUE(shortJoin.fired);
    EXPECT_FALSE(shortJoin.ok);
    EXPECT_EQ(shortJoin.error, "lobby.join takes (room id, player)");

    const RawResult stringJoin =
        attempt(kLobbyJoinMethod, {Text("1"), Text("p")});
    ASSERT_TRUE(stringJoin.fired);
    EXPECT_FALSE(stringJoin.ok);
    EXPECT_EQ(stringJoin.error, "lobby.join takes (room id, player)");

    const RawResult leaveArgs = attempt(kLobbyLeaveMethod, {Int(1LL)});
    ASSERT_TRUE(leaveArgs.fired);
    EXPECT_FALSE(leaveArgs.ok);
    EXPECT_EQ(leaveArgs.error, "lobby.leave takes no arguments");

    const RawResult shortRoster = attempt(kLobbyRosterMethod, {});
    ASSERT_TRUE(shortRoster.fired);
    EXPECT_FALSE(shortRoster.ok);
    EXPECT_EQ(shortRoster.error, "lobby.roster takes (room id)");

    const RawResult stringRoster = attempt(kLobbyRosterMethod, {Text("1")});
    ASSERT_TRUE(stringRoster.fired);
    EXPECT_FALSE(stringRoster.ok);
    EXPECT_EQ(stringRoster.error, "lobby.roster takes (room id)");

    LobbyStats expected;
    expected.createsRejected = 4U;
    expected.joinsRejected = 2U;
    expected.leavesRejected = 1U;
    EXPECT_EQ(harness.lobby.Stats(), expected);
    EXPECT_EQ(harness.lobby.RoomCount(), 0U);
}

TEST(NetworkLobbyTest, Facade_ServerErrorsArriveTyped) {
    LobbyHarness harness = MakeLobby(1U);
    ASSERT_TRUE(harness.Establish(1000U));

    RosterResult ghost;
    harness.facades[0].JoinRoom(
        999U, "amy", [&](std::vector<std::string> roster, const std::string& error) {
            ghost.fired = true;
            ghost.roster = std::move(roster);
            ghost.error = error;
        });
    harness.Settle(1100U);
    ASSERT_TRUE(ghost.fired);
    EXPECT_TRUE(ghost.roster.empty());
    EXPECT_EQ(ghost.error, "unknown room '999'");
    EXPECT_EQ(harness.facades[0].CurrentRoom(), kInvalidLobbyRoomId);

    RosterResult missing;
    harness.facades[0].QueryRoster(
        999U, [&](std::vector<std::string> roster, const std::string& error) {
            missing.fired = true;
            missing.roster = std::move(roster);
            missing.error = error;
        });
    harness.Settle(1200U);
    ASSERT_TRUE(missing.fired);
    EXPECT_TRUE(missing.roster.empty());
    EXPECT_EQ(missing.error, "unknown room '999'");

    LeaveResult nowhere;
    harness.facades[0].LeaveRoom([&](bool left, const std::string& error) {
        nowhere.fired = true;
        nowhere.left = left;
        nowhere.error = error;
    });
    harness.Settle(1300U);
    ASSERT_TRUE(nowhere.fired);
    EXPECT_FALSE(nowhere.left);
    EXPECT_EQ(nowhere.error, "not in a room");

    RoomResult nameless;
    harness.facades[0].CreateRoom(
        "", "amy", 4U, [&](LobbyRoomId id, const std::string& error) {
            nameless.fired = true;
            nameless.id = id;
            nameless.error = error;
        });
    harness.Settle(1400U);
    ASSERT_TRUE(nameless.fired);
    EXPECT_EQ(nameless.id, kInvalidLobbyRoomId);
    EXPECT_EQ(nameless.error, "room name is empty");
    EXPECT_EQ(harness.facades[0].CurrentRoom(), kInvalidLobbyRoomId);

    EXPECT_EQ(harness.lobby.RoomCount(), 0U);
}

TEST(NetworkLobbyTest, Facade_CallsFailFastWhenDown) {
    LobbyHarness harness = MakeLobby(1U);

    RoomsResult rooms;
    harness.facades[0].ListRooms(
        [&](std::vector<LobbyRoomInfo> listed, const std::string& error) {
            rooms.fired = true;
            rooms.rooms = std::move(listed);
            rooms.error = error;
        });
    EXPECT_TRUE(rooms.fired);
    EXPECT_TRUE(rooms.rooms.empty());
    EXPECT_EQ(rooms.error, "lobby.list failed to send");

    RoomResult created;
    harness.facades[0].CreateRoom(
        "arena", "amy", 4U, [&](LobbyRoomId id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    EXPECT_TRUE(created.fired);
    EXPECT_EQ(created.error, "lobby.create failed to send");

    RosterResult joined;
    harness.facades[0].JoinRoom(
        1U, "amy", [&](std::vector<std::string> roster, const std::string& error) {
            joined.fired = true;
            joined.roster = std::move(roster);
            joined.error = error;
        });
    EXPECT_TRUE(joined.fired);
    EXPECT_EQ(joined.error, "lobby.join failed to send");

    LeaveResult left;
    harness.facades[0].LeaveRoom([&](bool wasLeft, const std::string& error) {
        left.fired = true;
        left.left = wasLeft;
        left.error = error;
    });
    EXPECT_TRUE(left.fired);
    EXPECT_EQ(left.error, "lobby.leave failed to send");

    RosterResult queried;
    harness.facades[0].QueryRoster(
        1U, [&](std::vector<std::string> roster, const std::string& error) {
            queried.fired = true;
            queried.roster = std::move(roster);
            queried.error = error;
        });
    EXPECT_TRUE(queried.fired);
    EXPECT_EQ(queried.error, "lobby.roster failed to send");

    harness.facades[0].ListRooms(nullptr);
    harness.facades[0].CreateRoom("arena", "amy", 4U, nullptr);
    harness.facades[0].JoinRoom(1U, "amy", nullptr);
    harness.facades[0].LeaveRoom(nullptr);
    harness.facades[0].QueryRoster(1U, nullptr);
    harness.Settle(1100U);
}

TEST(NetworkLobbyTest, Notify_StalePushesAreSwallowed) {
    LobbyHarness harness = MakeLobby(2U);
    ASSERT_TRUE(harness.Establish(1000U));
    PushLog amyPushes;
    harness.facades[0].SetRosterHandler(
        [&](LobbyRoomId room, const std::vector<std::string>& roster) {
            amyPushes.pushes.emplace_back(room, roster);
        });
    PushLog bobPushes;
    harness.facades[1].SetRosterHandler(
        [&](LobbyRoomId room, const std::vector<std::string>& roster) {
            bobPushes.pushes.emplace_back(room, roster);
        });

    RoomResult created;
    harness.facades[0].CreateRoom(
        "arena", "amy", 2U, [&](LobbyRoomId id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    harness.Settle(1100U);
    ASSERT_TRUE(created.fired);
    EXPECT_EQ(created.id, 1U);

    RosterResult joined;
    harness.facades[1].JoinRoom(
        1U, "bob", [&](std::vector<std::string> roster, const std::string& error) {
            joined.fired = true;
            joined.roster = std::move(roster);
            joined.error = error;
        });
    harness.Settle(1200U);
    ASSERT_TRUE(joined.fired);
    EXPECT_TRUE(joined.error.empty());
    ASSERT_EQ(amyPushes.pushes.size(), 1U);

    LeaveResult amyLeft;
    harness.facades[0].LeaveRoom([&](bool left, const std::string& error) {
        amyLeft.fired = true;
        amyLeft.left = left;
        amyLeft.error = error;
    });
    harness.Settle(1300U);
    ASSERT_TRUE(amyLeft.fired);
    EXPECT_TRUE(amyLeft.left);
    ASSERT_EQ(bobPushes.pushes.size(), 1U);
    ASSERT_TRUE(harness.facades[0].CachedRoster().empty());

    auto pushRaw = [&](const LobbyClientId target,
                       const std::vector<Blueprint::Value>& args,
                       const std::uint64_t nowMs) {
        RawResult result;
        NetworkRpc* server = harness.lobby.ClientRpc(target);
        EXPECT_NE(server, nullptr);
        const RpcCallId call = server->Call(
            kLobbyRosterChangedMethod, args,
            [&](RpcCallId, const RpcOutcome& outcome) {
                result.fired = true;
                result.ok = outcome.ok;
                result.error = outcome.error;
            });
        EXPECT_NE(call, kInvalidRpcCallId);
        harness.Settle(nowMs);
        return result;
    };
    auto rosterArg = [](const std::vector<std::string>& names) {
        std::vector<Blueprint::Value> items;
        for (const std::string& name : names) {
            items.push_back(Blueprint::Value{name});
        }
        return Blueprint::MakeListValue(std::move(items));
    };

    const RawResult staleAmy =
        pushRaw(harness.facadeIds[0], {Int(1LL), rosterArg({"amy", "mallory"})}, 1400U);
    ASSERT_TRUE(staleAmy.fired);
    EXPECT_TRUE(staleAmy.ok);
    EXPECT_EQ(amyPushes.pushes.size(), 1U);
    EXPECT_TRUE(harness.facades[0].CachedRoster().empty());

    const RawResult staleBob =
        pushRaw(harness.facadeIds[1], {Int(999LL), rosterArg({"mallory"})}, 1500U);
    ASSERT_TRUE(staleBob.fired);
    EXPECT_TRUE(staleBob.ok);
    EXPECT_EQ(bobPushes.pushes.size(), 1U);
    EXPECT_EQ(harness.facades[1].CachedRoster(), std::vector<std::string>{"bob"});

    const RawResult malformed = pushRaw(harness.facadeIds[1], {Int(1LL)}, 1600U);
    ASSERT_TRUE(malformed.fired);
    EXPECT_FALSE(malformed.ok);
    EXPECT_EQ(malformed.error, "lobby.rosterChanged takes (room id, roster)");
    EXPECT_EQ(bobPushes.pushes.size(), 1U);
    EXPECT_EQ(harness.facades[1].CachedRoster(), std::vector<std::string>{"bob"});
}

TEST(NetworkLobbyTest, CancelPending_AbandonsInFlight) {
    LobbyHarness harness = MakeLobby(1U);
    ASSERT_TRUE(harness.Establish(1000U));

    RoomResult created;
    harness.facades[0].CreateRoom(
        "arena", "amy", 4U, [&](LobbyRoomId id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    harness.Settle(1100U);
    ASSERT_TRUE(created.fired);
    EXPECT_TRUE(created.error.empty());

    RoomsResult listed;
    harness.facades[0].ListRooms(
        [&](std::vector<LobbyRoomInfo> rooms, const std::string& error) {
            listed.fired = true;
            listed.rooms = std::move(rooms);
            listed.error = error;
        });
    RosterResult queried;
    harness.facades[0].QueryRoster(
        1U, [&](std::vector<std::string> roster, const std::string& error) {
            queried.fired = true;
            queried.roster = std::move(roster);
            queried.error = error;
        });
    EXPECT_EQ(harness.facades[0].CancelPending(), 2U);
    EXPECT_EQ(harness.facades[0].CancelPending(), 0U);
    harness.Settle(1200U);
    harness.Settle(1300U);
    EXPECT_FALSE(listed.fired);
    EXPECT_FALSE(queried.fired);
    EXPECT_EQ(harness.facades[0].Stats().responsesIgnoredStale, 2U);
}

} // namespace
} // namespace UVE::Network::Tests
