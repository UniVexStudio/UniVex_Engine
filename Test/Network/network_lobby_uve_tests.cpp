// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_lobby_uve.h"

#include "uve/network/network_rpc_uve.h"
#include "uve/network/network_session_uve.h"
#include "uve/uvscript/uvscript_value_uve.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

struct LobbyHarnessUVE final {
    std::vector<LoopbackTransportPairUVE> links;
    std::vector<NetworkLobbyClientUVE> facades;
    std::vector<LobbyClientIdUVE> facadeIds;
    std::vector<NetworkRpcUVE> raw;
    std::vector<LobbyClientIdUVE> rawIds;
    NetworkLobbyUVE lobby;

    // Note: growing these vectors moves live facades around, which is exactly the
    // relocation the roster pushes must survive - multi-facade tests cover it.
    void AddFacadeUVE() {
        LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
        NetworkSessionUVE client(SessionRoleUVE::Client,
                                 std::make_unique<LoopbackEndpointUVE>(pair.a));
        NetworkSessionUVE server(SessionRoleUVE::Server,
                                 std::make_unique<LoopbackEndpointUVE>(pair.b));
        facadeIds.push_back(lobby.AddClientUVE(std::move(server)));
        facades.emplace_back(std::move(client));
        links.push_back(std::move(pair));
    }

    void AddRawUVE() {
        LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
        NetworkSessionUVE client(SessionRoleUVE::Client,
                                 std::make_unique<LoopbackEndpointUVE>(pair.a));
        NetworkSessionUVE server(SessionRoleUVE::Server,
                                 std::make_unique<LoopbackEndpointUVE>(pair.b));
        rawIds.push_back(lobby.AddClientUVE(std::move(server)));
        raw.emplace_back(std::move(client));
        links.push_back(std::move(pair));
    }

    void UpdateAllUVE(const std::uint64_t nowMs) {
        lobby.UpdateUVE(nowMs);
        for (auto& facade : facades) {
            facade.UpdateUVE(nowMs);
        }
        for (auto& peer : raw) {
            peer.UpdateUVE(nowMs);
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
        for (auto& peer : raw) {
            if (!peer.SessionUVE().ConnectUVE(nowMs)) {
                return false;
            }
        }
        UpdateAllUVE(nowMs);
        UpdateAllUVE(nowMs);
        for (auto& facade : facades) {
            if (facade.SessionUVE().StateUVE() != SessionStateUVE::Established) {
                return false;
            }
        }
        for (auto& peer : raw) {
            if (peer.SessionUVE().StateUVE() != SessionStateUVE::Established) {
                return false;
            }
        }
        for (const LobbyClientIdUVE id : facadeIds) {
            const NetworkRpcUVE* server = lobby.ClientRpcUVE(id);
            if (server == nullptr ||
                server->SessionUVE().StateUVE() != SessionStateUVE::Established) {
                return false;
            }
        }
        for (const LobbyClientIdUVE id : rawIds) {
            const NetworkRpcUVE* server = lobby.ClientRpcUVE(id);
            if (server == nullptr ||
                server->SessionUVE().StateUVE() != SessionStateUVE::Established) {
                return false;
            }
        }
        return true;
    }
};

[[nodiscard]] LobbyHarnessUVE MakeLobbyUVE(const std::size_t facadeCount) {
    LobbyHarnessUVE harness;
    for (std::size_t i = 0U; i < facadeCount; ++i) {
        harness.AddFacadeUVE();
    }
    return harness;
}

[[nodiscard]] UVScript::ValueUVE IntUVE(const std::int64_t number) {
    return UVScript::ValueUVE{number};
}

[[nodiscard]] UVScript::ValueUVE TextUVE(const std::string& text) {
    return UVScript::ValueUVE{text};
}

struct RoomsResultUVE final {
    bool fired = false;
    std::vector<LobbyRoomInfoUVE> rooms;
    std::string error;
};

struct RoomResultUVE final {
    bool fired = false;
    LobbyRoomIdUVE id = kInvalidLobbyRoomIdUVE;
    std::string error;
};

struct RosterResultUVE final {
    bool fired = false;
    std::vector<std::string> roster;
    std::string error;
};

struct LeaveResultUVE final {
    bool fired = false;
    bool left = false;
    std::string error;
};

struct RawResultUVE final {
    bool fired = false;
    bool ok = false;
    UVScript::ValueUVE value;
    std::string error;
};

struct PushLogUVE final {
    std::vector<std::pair<LobbyRoomIdUVE, std::vector<std::string>>> pushes;
};

TEST(NetworkLobbyUVETest, Lifecycle_AddRemoveGuards) {
    LobbyHarnessUVE harness = MakeLobbyUVE(2U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(harness.facadeIds[0], 1U);
    EXPECT_EQ(harness.facadeIds[1], 2U);
    EXPECT_NE(harness.lobby.ClientRpcUVE(harness.facadeIds[0]), nullptr);
    EXPECT_EQ(harness.lobby.ClientRpcUVE(999U), nullptr);
    EXPECT_EQ(harness.lobby.PeerCountUVE(), 2U);
    EXPECT_EQ(harness.lobby.RoomCountUVE(), 0U);
    EXPECT_EQ(harness.lobby.StatsUVE(), LobbyStatsUVE{});

    EXPECT_FALSE(harness.lobby.RemoveClientUVE(999U));
    EXPECT_TRUE(harness.lobby.RemoveClientUVE(harness.facadeIds[0]));
    EXPECT_FALSE(harness.lobby.RemoveClientUVE(harness.facadeIds[0]));
    EXPECT_EQ(harness.lobby.ClientRpcUVE(harness.facadeIds[0]), nullptr);
    EXPECT_EQ(harness.lobby.PeerCountUVE(), 1U);
}

TEST(NetworkLobbyUVETest, Rooms_CreateListAndQuery) {
    LobbyHarnessUVE harness = MakeLobbyUVE(2U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    RoomResultUVE created;
    harness.facades[0].CreateRoomUVE(
        "arena", "amy", 4U, [&](LobbyRoomIdUVE id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    harness.SettleUVE(1100U);
    ASSERT_TRUE(created.fired);
    EXPECT_TRUE(created.error.empty());
    EXPECT_EQ(created.id, 1U);
    EXPECT_EQ(harness.facades[0].CurrentRoomUVE(), 1U);
    EXPECT_EQ(harness.facades[0].CachedRosterUVE(), std::vector<std::string>{"amy"});

    RoomsResultUVE listed;
    harness.facades[1].ListRoomsUVE(
        [&](std::vector<LobbyRoomInfoUVE> rooms, const std::string& error) {
            listed.fired = true;
            listed.rooms = std::move(rooms);
            listed.error = error;
        });
    harness.SettleUVE(1200U);
    ASSERT_TRUE(listed.fired);
    EXPECT_TRUE(listed.error.empty());
    ASSERT_EQ(listed.rooms.size(), 1U);
    EXPECT_EQ(listed.rooms[0], (LobbyRoomInfoUVE{1U, "arena", 1U, 4U}));

    RoomResultUVE created2;
    harness.facades[1].CreateRoomUVE(
        "dome", "bob", 2U, [&](LobbyRoomIdUVE id, const std::string& error) {
            created2.fired = true;
            created2.id = id;
            created2.error = error;
        });
    harness.SettleUVE(1300U);
    ASSERT_TRUE(created2.fired);
    EXPECT_EQ(created2.id, 2U);

    listed = RoomsResultUVE{};
    harness.facades[0].ListRoomsUVE(
        [&](std::vector<LobbyRoomInfoUVE> rooms, const std::string& error) {
            listed.fired = true;
            listed.rooms = std::move(rooms);
            listed.error = error;
        });
    harness.SettleUVE(1400U);
    ASSERT_TRUE(listed.fired);
    ASSERT_EQ(listed.rooms.size(), 2U);
    EXPECT_EQ(listed.rooms[0], (LobbyRoomInfoUVE{1U, "arena", 1U, 4U}));
    EXPECT_EQ(listed.rooms[1], (LobbyRoomInfoUVE{2U, "dome", 1U, 2U}));

    RosterResultUVE roster;
    harness.facades[1].QueryRosterUVE(
        1U, [&](std::vector<std::string> names, const std::string& error) {
            roster.fired = true;
            roster.roster = std::move(names);
            roster.error = error;
        });
    harness.SettleUVE(1500U);
    ASSERT_TRUE(roster.fired);
    EXPECT_TRUE(roster.error.empty());
    EXPECT_EQ(roster.roster, std::vector<std::string>{"amy"});
    EXPECT_EQ(harness.facades[1].CurrentRoomUVE(), 2U);

    const LobbyStatsUVE stats = harness.lobby.StatsUVE();
    EXPECT_EQ(stats.roomsCreated, 2U);
    EXPECT_EQ(stats.roomsRemoved, 0U);
    EXPECT_EQ(stats.joinsAccepted, 0U);
    EXPECT_EQ(stats.leavesAccepted, 0U);
    EXPECT_EQ(stats.notificationsSent, 0U);
}

TEST(NetworkLobbyUVETest, Join_RulesRejectAndAccept) {
    LobbyHarnessUVE harness = MakeLobbyUVE(3U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    PushLogUVE amyPushes;
    harness.facades[0].SetRosterHandlerUVE(
        [&](LobbyRoomIdUVE room, const std::vector<std::string>& roster) {
            amyPushes.pushes.emplace_back(room, roster);
        });

    RoomResultUVE created;
    harness.facades[0].CreateRoomUVE(
        "arena", "amy", 2U, [&](LobbyRoomIdUVE id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    harness.SettleUVE(1100U);
    ASSERT_TRUE(created.fired);
    EXPECT_EQ(created.id, 1U);

    auto joinAs = [&](const std::size_t facade, const LobbyRoomIdUVE room,
                      const std::string& name, const std::uint64_t nowMs) {
        RosterResultUVE result;
        harness.facades[facade].JoinRoomUVE(
            room, name, [&](std::vector<std::string> roster, const std::string& error) {
                result.fired = true;
                result.roster = std::move(roster);
                result.error = error;
            });
        harness.SettleUVE(nowMs);
        return result;
    };

    const RosterResultUVE ghost = joinAs(1U, 999U, "bob", 1200U);
    ASSERT_TRUE(ghost.fired);
    EXPECT_TRUE(ghost.roster.empty());
    EXPECT_EQ(ghost.error, "unknown room '999'");
    EXPECT_EQ(harness.facades[1].CurrentRoomUVE(), kInvalidLobbyRoomIdUVE);

    const RosterResultUVE clash = joinAs(1U, 1U, "amy", 1300U);
    ASSERT_TRUE(clash.fired);
    EXPECT_EQ(clash.error, "name 'amy' is taken in room '1'");

    const RosterResultUVE joined = joinAs(1U, 1U, "bob", 1400U);
    ASSERT_TRUE(joined.fired);
    EXPECT_TRUE(joined.error.empty());
    EXPECT_EQ(joined.roster, (std::vector<std::string>{"amy", "bob"}));
    EXPECT_EQ(harness.facades[1].CurrentRoomUVE(), 1U);
    EXPECT_EQ(harness.facades[1].CachedRosterUVE(), joined.roster);

    ASSERT_EQ(amyPushes.pushes.size(), 1U);
    EXPECT_EQ(amyPushes.pushes[0].first, 1U);
    EXPECT_EQ(amyPushes.pushes[0].second, (std::vector<std::string>{"amy", "bob"}));
    EXPECT_EQ(harness.facades[0].CachedRosterUVE(), amyPushes.pushes[0].second);

    const RosterResultUVE full = joinAs(2U, 1U, "cat", 1500U);
    ASSERT_TRUE(full.fired);
    EXPECT_EQ(full.error, "room '1' is full");

    const RosterResultUVE quiet = joinAs(1U, 1U, "bob", 1600U);
    ASSERT_TRUE(quiet.fired);
    EXPECT_TRUE(quiet.error.empty());
    EXPECT_EQ(quiet.roster, (std::vector<std::string>{"amy", "bob"}));
    EXPECT_EQ(amyPushes.pushes.size(), 1U);

    const RosterResultUVE clashRename = joinAs(1U, 1U, "amy", 1700U);
    ASSERT_TRUE(clashRename.fired);
    EXPECT_EQ(clashRename.error, "name 'amy' is taken in room '1'");

    const RosterResultUVE renamed = joinAs(1U, 1U, "bobby", 1800U);
    ASSERT_TRUE(renamed.fired);
    EXPECT_TRUE(renamed.error.empty());
    EXPECT_EQ(renamed.roster, (std::vector<std::string>{"amy", "bobby"}));
    ASSERT_EQ(amyPushes.pushes.size(), 2U);
    EXPECT_EQ(amyPushes.pushes[1].second, (std::vector<std::string>{"amy", "bobby"}));

    const LobbyStatsUVE stats = harness.lobby.StatsUVE();
    EXPECT_EQ(stats.joinsAccepted, 3U);
    EXPECT_EQ(stats.joinsRejected, 4U);
    EXPECT_EQ(stats.notificationsSent, 2U);
    EXPECT_EQ(stats.notificationsDropped, 0U);
    EXPECT_EQ(stats.leavesAccepted, 0U);
}

TEST(NetworkLobbyUVETest, Leave_EmptiesAndRemovesRooms) {
    LobbyHarnessUVE harness = MakeLobbyUVE(2U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    PushLogUVE amyPushes;
    harness.facades[0].SetRosterHandlerUVE(
        [&](LobbyRoomIdUVE room, const std::vector<std::string>& roster) {
            amyPushes.pushes.emplace_back(room, roster);
        });

    RoomResultUVE created;
    harness.facades[0].CreateRoomUVE(
        "arena", "amy", 4U, [&](LobbyRoomIdUVE id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    harness.SettleUVE(1100U);
    ASSERT_TRUE(created.fired);
    EXPECT_EQ(created.id, 1U);

    RosterResultUVE joined;
    harness.facades[1].JoinRoomUVE(
        1U, "bob", [&](std::vector<std::string> roster, const std::string& error) {
            joined.fired = true;
            joined.roster = std::move(roster);
            joined.error = error;
        });
    harness.SettleUVE(1200U);
    ASSERT_TRUE(joined.fired);
    EXPECT_TRUE(joined.error.empty());
    ASSERT_EQ(amyPushes.pushes.size(), 1U);

    auto leaveAs = [&](const std::size_t facade, const std::uint64_t nowMs) {
        LeaveResultUVE result;
        harness.facades[facade].LeaveRoomUVE([&](bool left, const std::string& error) {
            result.fired = true;
            result.left = left;
            result.error = error;
        });
        harness.SettleUVE(nowMs);
        return result;
    };

    const LeaveResultUVE bobLeft = leaveAs(1U, 1300U);
    ASSERT_TRUE(bobLeft.fired);
    EXPECT_TRUE(bobLeft.left);
    EXPECT_TRUE(bobLeft.error.empty());
    EXPECT_EQ(harness.facades[1].CurrentRoomUVE(), kInvalidLobbyRoomIdUVE);
    EXPECT_TRUE(harness.facades[1].CachedRosterUVE().empty());
    ASSERT_EQ(amyPushes.pushes.size(), 2U);
    EXPECT_EQ(amyPushes.pushes[1].second, std::vector<std::string>{"amy"});

    const LeaveResultUVE bobAgain = leaveAs(1U, 1400U);
    ASSERT_TRUE(bobAgain.fired);
    EXPECT_FALSE(bobAgain.left);
    EXPECT_EQ(bobAgain.error, "not in a room");

    const LeaveResultUVE amyLeft = leaveAs(0U, 1500U);
    ASSERT_TRUE(amyLeft.fired);
    EXPECT_TRUE(amyLeft.left);
    EXPECT_EQ(harness.lobby.RoomCountUVE(), 0U);
    EXPECT_EQ(amyPushes.pushes.size(), 2U);

    RoomsResultUVE listed;
    harness.facades[0].ListRoomsUVE(
        [&](std::vector<LobbyRoomInfoUVE> rooms, const std::string& error) {
            listed.fired = true;
            listed.rooms = std::move(rooms);
            listed.error = error;
        });
    harness.SettleUVE(1600U);
    ASSERT_TRUE(listed.fired);
    EXPECT_TRUE(listed.rooms.empty());

    RosterResultUVE gone;
    harness.facades[1].QueryRosterUVE(
        1U, [&](std::vector<std::string> roster, const std::string& error) {
            gone.fired = true;
            gone.roster = std::move(roster);
            gone.error = error;
        });
    harness.SettleUVE(1700U);
    ASSERT_TRUE(gone.fired);
    EXPECT_EQ(gone.error, "unknown room '1'");

    const LobbyStatsUVE stats = harness.lobby.StatsUVE();
    EXPECT_EQ(stats.roomsCreated, 1U);
    EXPECT_EQ(stats.roomsRemoved, 1U);
    EXPECT_EQ(stats.leavesAccepted, 2U);
    EXPECT_EQ(stats.leavesRejected, 1U);
}

TEST(NetworkLobbyUVETest, Move_CreateAndJoinLeaveTheOldRoom) {
    LobbyHarnessUVE harness = MakeLobbyUVE(2U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    RoomResultUVE first;
    harness.facades[0].CreateRoomUVE(
        "arena", "amy", 4U, [&](LobbyRoomIdUVE id, const std::string& error) {
            first.fired = true;
            first.id = id;
            first.error = error;
        });
    harness.SettleUVE(1100U);
    ASSERT_TRUE(first.fired);
    EXPECT_EQ(first.id, 1U);

    RoomResultUVE second;
    harness.facades[0].CreateRoomUVE(
        "dome", "amy2", 4U, [&](LobbyRoomIdUVE id, const std::string& error) {
            second.fired = true;
            second.id = id;
            second.error = error;
        });
    harness.SettleUVE(1200U);
    ASSERT_TRUE(second.fired);
    EXPECT_EQ(second.id, 2U);
    EXPECT_EQ(harness.facades[0].CurrentRoomUVE(), 2U);
    EXPECT_EQ(harness.facades[0].CachedRosterUVE(), std::vector<std::string>{"amy2"});
    EXPECT_EQ(harness.lobby.RoomCountUVE(), 1U);

    RosterResultUVE ghost;
    harness.facades[1].JoinRoomUVE(
        1U, "bob", [&](std::vector<std::string> roster, const std::string& error) {
            ghost.fired = true;
            ghost.roster = std::move(roster);
            ghost.error = error;
        });
    harness.SettleUVE(1300U);
    ASSERT_TRUE(ghost.fired);
    EXPECT_EQ(ghost.error, "unknown room '1'");

    RoomResultUVE side;
    harness.facades[1].CreateRoomUVE(
        "side", "bob", 4U, [&](LobbyRoomIdUVE id, const std::string& error) {
            side.fired = true;
            side.id = id;
            side.error = error;
        });
    harness.SettleUVE(1400U);
    ASSERT_TRUE(side.fired);
    EXPECT_EQ(side.id, 3U);

    PushLogUVE amyPushes;
    harness.facades[0].SetRosterHandlerUVE(
        [&](LobbyRoomIdUVE room, const std::vector<std::string>& roster) {
            amyPushes.pushes.emplace_back(room, roster);
        });
    RosterResultUVE moved;
    harness.facades[1].JoinRoomUVE(
        2U, "bob", [&](std::vector<std::string> roster, const std::string& error) {
            moved.fired = true;
            moved.roster = std::move(roster);
            moved.error = error;
        });
    harness.SettleUVE(1500U);
    ASSERT_TRUE(moved.fired);
    EXPECT_TRUE(moved.error.empty());
    EXPECT_EQ(moved.roster, (std::vector<std::string>{"amy2", "bob"}));
    EXPECT_EQ(harness.facades[1].CurrentRoomUVE(), 2U);
    EXPECT_EQ(harness.lobby.RoomCountUVE(), 1U);
    ASSERT_EQ(amyPushes.pushes.size(), 1U);
    EXPECT_EQ(amyPushes.pushes[0].second, (std::vector<std::string>{"amy2", "bob"}));

    const LobbyStatsUVE stats = harness.lobby.StatsUVE();
    EXPECT_EQ(stats.roomsCreated, 3U);
    EXPECT_EQ(stats.roomsRemoved, 2U);
    EXPECT_EQ(stats.leavesAccepted, 2U);
    EXPECT_EQ(stats.joinsAccepted, 1U);
    EXPECT_EQ(stats.joinsRejected, 1U);
}

TEST(NetworkLobbyUVETest, Notify_MembersWithoutTheHandlerStillPlay) {
    LobbyHarnessUVE harness = MakeLobbyUVE(1U);
    harness.AddRawUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    PushLogUVE amyPushes;
    harness.facades[0].SetRosterHandlerUVE(
        [&](LobbyRoomIdUVE room, const std::vector<std::string>& roster) {
            amyPushes.pushes.emplace_back(room, roster);
        });

    RoomResultUVE created;
    harness.facades[0].CreateRoomUVE(
        "arena", "amy", 4U, [&](LobbyRoomIdUVE id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    harness.SettleUVE(1100U);
    ASSERT_TRUE(created.fired);
    EXPECT_EQ(created.id, 1U);

    auto callRaw = [&](const std::string& method,
                       const std::vector<UVScript::ValueUVE>& args,
                       const std::uint64_t nowMs) {
        RawResultUVE result;
        const RpcCallIdUVE call = harness.raw[0].CallUVE(
            method, args, [&](RpcCallIdUVE, const RpcOutcomeUVE& outcome) {
                result.fired = true;
                result.ok = outcome.ok;
                result.value = outcome.value;
                result.error = outcome.error;
            });
        EXPECT_NE(call, kInvalidRpcCallIdUVE);
        harness.SettleUVE(nowMs);
        return result;
    };

    const RawResultUVE joined =
        callRaw(kLobbyJoinMethodUVE, {IntUVE(1LL), TextUVE("cat")}, 1200U);
    ASSERT_TRUE(joined.fired);
    EXPECT_TRUE(joined.ok) << joined.error;
    ASSERT_EQ(amyPushes.pushes.size(), 1U);
    EXPECT_EQ(amyPushes.pushes[0].second, (std::vector<std::string>{"amy", "cat"}));

    LeaveResultUVE amyLeft;
    harness.facades[0].LeaveRoomUVE([&](bool left, const std::string& error) {
        amyLeft.fired = true;
        amyLeft.left = left;
        amyLeft.error = error;
    });
    harness.SettleUVE(1300U);
    ASSERT_TRUE(amyLeft.fired);
    EXPECT_TRUE(amyLeft.left);
    EXPECT_EQ(harness.raw[0].StatsUVE().callsReceived, 1U);
    EXPECT_EQ(harness.raw[0].StatsUVE().responsesSent, 1U);

    const RawResultUVE roster = callRaw(kLobbyRosterMethodUVE, {IntUVE(1LL)}, 1400U);
    ASSERT_TRUE(roster.fired);
    EXPECT_TRUE(roster.ok) << roster.error;

    const LobbyStatsUVE stats = harness.lobby.StatsUVE();
    EXPECT_EQ(stats.notificationsSent, 2U);
    EXPECT_EQ(stats.notificationsDropped, 0U);
}

TEST(NetworkLobbyUVETest, Removal_KicksCleanUpAndDisconnectsAreFound) {
    LobbyHarnessUVE harness = MakeLobbyUVE(2U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    PushLogUVE amyPushes;
    harness.facades[0].SetRosterHandlerUVE(
        [&](LobbyRoomIdUVE room, const std::vector<std::string>& roster) {
            amyPushes.pushes.emplace_back(room, roster);
        });

    RoomResultUVE created;
    harness.facades[0].CreateRoomUVE(
        "arena", "amy", 4U, [&](LobbyRoomIdUVE id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    harness.SettleUVE(1100U);
    ASSERT_TRUE(created.fired);

    RosterResultUVE joined;
    harness.facades[1].JoinRoomUVE(
        1U, "bob", [&](std::vector<std::string> roster, const std::string& error) {
            joined.fired = true;
            joined.roster = std::move(roster);
            joined.error = error;
        });
    harness.SettleUVE(1200U);
    ASSERT_TRUE(joined.fired);
    EXPECT_TRUE(joined.error.empty());
    ASSERT_EQ(amyPushes.pushes.size(), 1U);

    EXPECT_TRUE(harness.lobby.RemoveClientUVE(harness.facadeIds[1]));
    harness.SettleUVE(1300U);
    ASSERT_EQ(amyPushes.pushes.size(), 2U);
    EXPECT_EQ(amyPushes.pushes[1].second, std::vector<std::string>{"amy"});
    EXPECT_EQ(harness.lobby.PeerCountUVE(), 1U);
    EXPECT_EQ(harness.lobby.RoomCountUVE(), 1U);

    EXPECT_TRUE(harness.lobby.RemoveClientUVE(harness.facadeIds[0]));
    EXPECT_EQ(harness.lobby.RoomCountUVE(), 0U);
    EXPECT_EQ(harness.lobby.PeerCountUVE(), 0U);

    const LobbyStatsUVE stats = harness.lobby.StatsUVE();
    EXPECT_EQ(stats.roomsRemoved, 1U);
    EXPECT_EQ(stats.leavesAccepted, 2U);

    LobbyHarnessUVE hungUp = MakeLobbyUVE(1U);
    ASSERT_TRUE(hungUp.EstablishUVE(2000U));
    RoomResultUVE solo;
    hungUp.facades[0].CreateRoomUVE(
        "solo", "sam", 4U, [&](LobbyRoomIdUVE id, const std::string& error) {
            solo.fired = true;
            solo.id = id;
            solo.error = error;
        });
    hungUp.SettleUVE(2100U);
    ASSERT_TRUE(solo.fired);
    EXPECT_EQ(solo.id, 1U);
    ASSERT_TRUE(hungUp.facades[0].SessionUVE().DisconnectUVE());
    hungUp.UpdateAllUVE(2200U);
    const NetworkRpcUVE* peer = hungUp.lobby.ClientRpcUVE(hungUp.facadeIds[0]);
    ASSERT_NE(peer, nullptr);
    EXPECT_EQ(peer->SessionUVE().StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_EQ(peer->SessionUVE().CloseReasonUVE(), SessionCloseReasonUVE::RemoteDisconnect);
    EXPECT_TRUE(hungUp.lobby.RemoveClientUVE(hungUp.facadeIds[0]));
    EXPECT_EQ(hungUp.lobby.RoomCountUVE(), 0U);
    EXPECT_EQ(hungUp.lobby.StatsUVE().roomsRemoved, 1U);
}

TEST(NetworkLobbyUVETest, Protocol_RawShapesAreRejected) {
    LobbyHarnessUVE harness = MakeLobbyUVE(0U);
    harness.AddRawUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    auto callRaw = [&](const std::string& method,
                       const std::vector<UVScript::ValueUVE>& args,
                       const std::uint64_t nowMs) {
        RawResultUVE result;
        const RpcCallIdUVE call = harness.raw[0].CallUVE(
            method, args, [&](RpcCallIdUVE, const RpcOutcomeUVE& outcome) {
                result.fired = true;
                result.ok = outcome.ok;
                result.value = outcome.value;
                result.error = outcome.error;
            });
        EXPECT_NE(call, kInvalidRpcCallIdUVE);
        harness.SettleUVE(nowMs);
        return result;
    };

    std::uint64_t nowMs = 1100U;
    auto attempt = [&](const std::string& method,
                       const std::vector<UVScript::ValueUVE>& args) {
        RawResultUVE result = callRaw(method, args, nowMs);
        nowMs += 100U;
        return result;
    };

    const RawResultUVE listArgs = attempt(kLobbyListMethodUVE, {IntUVE(1LL)});
    ASSERT_TRUE(listArgs.fired);
    EXPECT_FALSE(listArgs.ok);
    EXPECT_EQ(listArgs.error, "lobby.list takes no arguments");

    const RawResultUVE shortCreate =
        attempt(kLobbyCreateMethodUVE, {TextUVE("r"), TextUVE("p")});
    ASSERT_TRUE(shortCreate.fired);
    EXPECT_FALSE(shortCreate.ok);
    EXPECT_EQ(shortCreate.error, "lobby.create takes (name, player, max)");

    const RawResultUVE emptyRoom =
        attempt(kLobbyCreateMethodUVE, {TextUVE(""), TextUVE("p"), IntUVE(4LL)});
    ASSERT_TRUE(emptyRoom.fired);
    EXPECT_FALSE(emptyRoom.ok);
    EXPECT_EQ(emptyRoom.error, "room name is empty");

    const RawResultUVE emptyPlayer =
        attempt(kLobbyCreateMethodUVE, {TextUVE("r"), TextUVE(""), IntUVE(4LL)});
    ASSERT_TRUE(emptyPlayer.fired);
    EXPECT_FALSE(emptyPlayer.ok);
    EXPECT_EQ(emptyPlayer.error, "player name is empty");

    const RawResultUVE zeroMax =
        attempt(kLobbyCreateMethodUVE, {TextUVE("r"), TextUVE("p"), IntUVE(0LL)});
    ASSERT_TRUE(zeroMax.fired);
    EXPECT_FALSE(zeroMax.ok);
    EXPECT_EQ(zeroMax.error, "max players must be at least 1");

    const RawResultUVE shortJoin = attempt(kLobbyJoinMethodUVE, {IntUVE(1LL)});
    ASSERT_TRUE(shortJoin.fired);
    EXPECT_FALSE(shortJoin.ok);
    EXPECT_EQ(shortJoin.error, "lobby.join takes (room id, player)");

    const RawResultUVE stringJoin =
        attempt(kLobbyJoinMethodUVE, {TextUVE("1"), TextUVE("p")});
    ASSERT_TRUE(stringJoin.fired);
    EXPECT_FALSE(stringJoin.ok);
    EXPECT_EQ(stringJoin.error, "lobby.join takes (room id, player)");

    const RawResultUVE leaveArgs = attempt(kLobbyLeaveMethodUVE, {IntUVE(1LL)});
    ASSERT_TRUE(leaveArgs.fired);
    EXPECT_FALSE(leaveArgs.ok);
    EXPECT_EQ(leaveArgs.error, "lobby.leave takes no arguments");

    const RawResultUVE shortRoster = attempt(kLobbyRosterMethodUVE, {});
    ASSERT_TRUE(shortRoster.fired);
    EXPECT_FALSE(shortRoster.ok);
    EXPECT_EQ(shortRoster.error, "lobby.roster takes (room id)");

    const RawResultUVE stringRoster = attempt(kLobbyRosterMethodUVE, {TextUVE("1")});
    ASSERT_TRUE(stringRoster.fired);
    EXPECT_FALSE(stringRoster.ok);
    EXPECT_EQ(stringRoster.error, "lobby.roster takes (room id)");

    LobbyStatsUVE expected;
    expected.createsRejected = 4U;
    expected.joinsRejected = 2U;
    expected.leavesRejected = 1U;
    EXPECT_EQ(harness.lobby.StatsUVE(), expected);
    EXPECT_EQ(harness.lobby.RoomCountUVE(), 0U);
}

TEST(NetworkLobbyUVETest, Facade_ServerErrorsArriveTyped) {
    LobbyHarnessUVE harness = MakeLobbyUVE(1U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    RosterResultUVE ghost;
    harness.facades[0].JoinRoomUVE(
        999U, "amy", [&](std::vector<std::string> roster, const std::string& error) {
            ghost.fired = true;
            ghost.roster = std::move(roster);
            ghost.error = error;
        });
    harness.SettleUVE(1100U);
    ASSERT_TRUE(ghost.fired);
    EXPECT_TRUE(ghost.roster.empty());
    EXPECT_EQ(ghost.error, "unknown room '999'");
    EXPECT_EQ(harness.facades[0].CurrentRoomUVE(), kInvalidLobbyRoomIdUVE);

    RosterResultUVE missing;
    harness.facades[0].QueryRosterUVE(
        999U, [&](std::vector<std::string> roster, const std::string& error) {
            missing.fired = true;
            missing.roster = std::move(roster);
            missing.error = error;
        });
    harness.SettleUVE(1200U);
    ASSERT_TRUE(missing.fired);
    EXPECT_TRUE(missing.roster.empty());
    EXPECT_EQ(missing.error, "unknown room '999'");

    LeaveResultUVE nowhere;
    harness.facades[0].LeaveRoomUVE([&](bool left, const std::string& error) {
        nowhere.fired = true;
        nowhere.left = left;
        nowhere.error = error;
    });
    harness.SettleUVE(1300U);
    ASSERT_TRUE(nowhere.fired);
    EXPECT_FALSE(nowhere.left);
    EXPECT_EQ(nowhere.error, "not in a room");

    RoomResultUVE nameless;
    harness.facades[0].CreateRoomUVE(
        "", "amy", 4U, [&](LobbyRoomIdUVE id, const std::string& error) {
            nameless.fired = true;
            nameless.id = id;
            nameless.error = error;
        });
    harness.SettleUVE(1400U);
    ASSERT_TRUE(nameless.fired);
    EXPECT_EQ(nameless.id, kInvalidLobbyRoomIdUVE);
    EXPECT_EQ(nameless.error, "room name is empty");
    EXPECT_EQ(harness.facades[0].CurrentRoomUVE(), kInvalidLobbyRoomIdUVE);

    EXPECT_EQ(harness.lobby.RoomCountUVE(), 0U);
}

TEST(NetworkLobbyUVETest, Facade_CallsFailFastWhenDown) {
    LobbyHarnessUVE harness = MakeLobbyUVE(1U);

    RoomsResultUVE rooms;
    harness.facades[0].ListRoomsUVE(
        [&](std::vector<LobbyRoomInfoUVE> listed, const std::string& error) {
            rooms.fired = true;
            rooms.rooms = std::move(listed);
            rooms.error = error;
        });
    EXPECT_TRUE(rooms.fired);
    EXPECT_TRUE(rooms.rooms.empty());
    EXPECT_EQ(rooms.error, "lobby.list failed to send");

    RoomResultUVE created;
    harness.facades[0].CreateRoomUVE(
        "arena", "amy", 4U, [&](LobbyRoomIdUVE id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    EXPECT_TRUE(created.fired);
    EXPECT_EQ(created.error, "lobby.create failed to send");

    RosterResultUVE joined;
    harness.facades[0].JoinRoomUVE(
        1U, "amy", [&](std::vector<std::string> roster, const std::string& error) {
            joined.fired = true;
            joined.roster = std::move(roster);
            joined.error = error;
        });
    EXPECT_TRUE(joined.fired);
    EXPECT_EQ(joined.error, "lobby.join failed to send");

    LeaveResultUVE left;
    harness.facades[0].LeaveRoomUVE([&](bool wasLeft, const std::string& error) {
        left.fired = true;
        left.left = wasLeft;
        left.error = error;
    });
    EXPECT_TRUE(left.fired);
    EXPECT_EQ(left.error, "lobby.leave failed to send");

    RosterResultUVE queried;
    harness.facades[0].QueryRosterUVE(
        1U, [&](std::vector<std::string> roster, const std::string& error) {
            queried.fired = true;
            queried.roster = std::move(roster);
            queried.error = error;
        });
    EXPECT_TRUE(queried.fired);
    EXPECT_EQ(queried.error, "lobby.roster failed to send");

    harness.facades[0].ListRoomsUVE(nullptr);
    harness.facades[0].CreateRoomUVE("arena", "amy", 4U, nullptr);
    harness.facades[0].JoinRoomUVE(1U, "amy", nullptr);
    harness.facades[0].LeaveRoomUVE(nullptr);
    harness.facades[0].QueryRosterUVE(1U, nullptr);
    harness.SettleUVE(1100U);
}

TEST(NetworkLobbyUVETest, Notify_StalePushesAreSwallowed) {
    LobbyHarnessUVE harness = MakeLobbyUVE(2U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    PushLogUVE amyPushes;
    harness.facades[0].SetRosterHandlerUVE(
        [&](LobbyRoomIdUVE room, const std::vector<std::string>& roster) {
            amyPushes.pushes.emplace_back(room, roster);
        });
    PushLogUVE bobPushes;
    harness.facades[1].SetRosterHandlerUVE(
        [&](LobbyRoomIdUVE room, const std::vector<std::string>& roster) {
            bobPushes.pushes.emplace_back(room, roster);
        });

    RoomResultUVE created;
    harness.facades[0].CreateRoomUVE(
        "arena", "amy", 2U, [&](LobbyRoomIdUVE id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    harness.SettleUVE(1100U);
    ASSERT_TRUE(created.fired);
    EXPECT_EQ(created.id, 1U);

    RosterResultUVE joined;
    harness.facades[1].JoinRoomUVE(
        1U, "bob", [&](std::vector<std::string> roster, const std::string& error) {
            joined.fired = true;
            joined.roster = std::move(roster);
            joined.error = error;
        });
    harness.SettleUVE(1200U);
    ASSERT_TRUE(joined.fired);
    EXPECT_TRUE(joined.error.empty());
    ASSERT_EQ(amyPushes.pushes.size(), 1U);

    LeaveResultUVE amyLeft;
    harness.facades[0].LeaveRoomUVE([&](bool left, const std::string& error) {
        amyLeft.fired = true;
        amyLeft.left = left;
        amyLeft.error = error;
    });
    harness.SettleUVE(1300U);
    ASSERT_TRUE(amyLeft.fired);
    EXPECT_TRUE(amyLeft.left);
    ASSERT_EQ(bobPushes.pushes.size(), 1U);
    ASSERT_TRUE(harness.facades[0].CachedRosterUVE().empty());

    auto pushRaw = [&](const LobbyClientIdUVE target,
                       const std::vector<UVScript::ValueUVE>& args,
                       const std::uint64_t nowMs) {
        RawResultUVE result;
        NetworkRpcUVE* server = harness.lobby.ClientRpcUVE(target);
        EXPECT_NE(server, nullptr);
        const RpcCallIdUVE call = server->CallUVE(
            kLobbyRosterChangedMethodUVE, args,
            [&](RpcCallIdUVE, const RpcOutcomeUVE& outcome) {
                result.fired = true;
                result.ok = outcome.ok;
                result.error = outcome.error;
            });
        EXPECT_NE(call, kInvalidRpcCallIdUVE);
        harness.SettleUVE(nowMs);
        return result;
    };
    auto rosterArg = [](const std::vector<std::string>& names) {
        std::vector<UVScript::ValueUVE> items;
        for (const std::string& name : names) {
            items.push_back(UVScript::ValueUVE{name});
        }
        return UVScript::MakeListValueUVE(std::move(items));
    };

    const RawResultUVE staleAmy =
        pushRaw(harness.facadeIds[0], {IntUVE(1LL), rosterArg({"amy", "mallory"})}, 1400U);
    ASSERT_TRUE(staleAmy.fired);
    EXPECT_TRUE(staleAmy.ok);
    EXPECT_EQ(amyPushes.pushes.size(), 1U);
    EXPECT_TRUE(harness.facades[0].CachedRosterUVE().empty());

    const RawResultUVE staleBob =
        pushRaw(harness.facadeIds[1], {IntUVE(999LL), rosterArg({"mallory"})}, 1500U);
    ASSERT_TRUE(staleBob.fired);
    EXPECT_TRUE(staleBob.ok);
    EXPECT_EQ(bobPushes.pushes.size(), 1U);
    EXPECT_EQ(harness.facades[1].CachedRosterUVE(), std::vector<std::string>{"bob"});

    const RawResultUVE malformed = pushRaw(harness.facadeIds[1], {IntUVE(1LL)}, 1600U);
    ASSERT_TRUE(malformed.fired);
    EXPECT_FALSE(malformed.ok);
    EXPECT_EQ(malformed.error, "lobby.rosterChanged takes (room id, roster)");
    EXPECT_EQ(bobPushes.pushes.size(), 1U);
    EXPECT_EQ(harness.facades[1].CachedRosterUVE(), std::vector<std::string>{"bob"});
}

TEST(NetworkLobbyUVETest, CancelPending_AbandonsInFlight) {
    LobbyHarnessUVE harness = MakeLobbyUVE(1U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    RoomResultUVE created;
    harness.facades[0].CreateRoomUVE(
        "arena", "amy", 4U, [&](LobbyRoomIdUVE id, const std::string& error) {
            created.fired = true;
            created.id = id;
            created.error = error;
        });
    harness.SettleUVE(1100U);
    ASSERT_TRUE(created.fired);
    EXPECT_TRUE(created.error.empty());

    RoomsResultUVE listed;
    harness.facades[0].ListRoomsUVE(
        [&](std::vector<LobbyRoomInfoUVE> rooms, const std::string& error) {
            listed.fired = true;
            listed.rooms = std::move(rooms);
            listed.error = error;
        });
    RosterResultUVE queried;
    harness.facades[0].QueryRosterUVE(
        1U, [&](std::vector<std::string> roster, const std::string& error) {
            queried.fired = true;
            queried.roster = std::move(roster);
            queried.error = error;
        });
    EXPECT_EQ(harness.facades[0].CancelPendingUVE(), 2U);
    EXPECT_EQ(harness.facades[0].CancelPendingUVE(), 0U);
    harness.SettleUVE(1200U);
    harness.SettleUVE(1300U);
    EXPECT_FALSE(listed.fired);
    EXPECT_FALSE(queried.fired);
    EXPECT_EQ(harness.facades[0].StatsUVE().responsesIgnoredStale, 2U);
}

} // namespace
} // namespace UVE::Network::Tests
