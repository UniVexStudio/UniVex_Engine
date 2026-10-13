// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_relay_uve.h"

#include "uve/network/network_lobby_uve.h"
#include "uve/network/network_session_uve.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

[[nodiscard]] std::vector<std::uint8_t> BytesUVE(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

struct RelayRoomsHarnessUVE final {
    std::vector<LoopbackTransportPairUVE> links;
    std::vector<NetworkSessionUVE> clients;
    NetworkRelayUVE relay;
    std::vector<RelayPeerIdUVE> peerIds;

    void UpdateAllUVE(const std::uint64_t nowMs) {
        relay.UpdateUVE(nowMs);
        for (auto& client : clients) {
            client.UpdateUVE(nowMs);
        }
    }

    void SettleUVE(const std::uint64_t nowMs) {
        UpdateAllUVE(nowMs);
        UpdateAllUVE(nowMs);
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
            if (client.StateUVE() != SessionStateUVE::Established) {
                return false;
            }
        }
        for (const RelayPeerIdUVE id : peerIds) {
            const NetworkSessionUVE* peer = relay.PeerSessionUVE(id);
            if (peer == nullptr || peer->StateUVE() != SessionStateUVE::Established) {
                return false;
            }
        }
        return true;
    }
};

[[nodiscard]] RelayRoomsHarnessUVE MakeRelayRoomsUVE(const std::size_t clientCount) {
    RelayRoomsHarnessUVE harness;
    for (std::size_t i = 0U; i < clientCount; ++i) {
        LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
        NetworkSessionUVE client(SessionRoleUVE::Client,
                                 std::make_unique<LoopbackEndpointUVE>(pair.a));
        NetworkSessionUVE server(SessionRoleUVE::Server,
                                 std::make_unique<LoopbackEndpointUVE>(pair.b));
        harness.peerIds.push_back(harness.relay.AddPeerUVE(std::move(server)));
        harness.clients.push_back(std::move(client));
        harness.links.push_back(std::move(pair));
    }
    return harness;
}

struct BindingHarnessUVE final {
    std::vector<LoopbackTransportPairUVE> lobbyLinks;
    std::vector<NetworkLobbyClientUVE> facades;
    std::vector<LobbyClientIdUVE> facadeIds;
    NetworkLobbyUVE lobby;
    std::vector<LoopbackTransportPairUVE> relayLinks;
    std::vector<NetworkSessionUVE> relayClients;
    std::vector<RelayPeerIdUVE> relayPeers;
    NetworkRelayUVE relay;

    void AddClientUVE() {
        LoopbackTransportPairUVE lobbyPair = CreateLoopbackTransportPairUVE(256U);
        NetworkSessionUVE lobbyClient(SessionRoleUVE::Client,
                                      std::make_unique<LoopbackEndpointUVE>(lobbyPair.a));
        NetworkSessionUVE lobbyServer(SessionRoleUVE::Server,
                                      std::make_unique<LoopbackEndpointUVE>(lobbyPair.b));
        facadeIds.push_back(lobby.AddClientUVE(std::move(lobbyServer)));
        facades.emplace_back(std::move(lobbyClient));
        lobbyLinks.push_back(std::move(lobbyPair));

        LoopbackTransportPairUVE relayPair = CreateLoopbackTransportPairUVE(256U);
        NetworkSessionUVE relayClient(SessionRoleUVE::Client,
                                      std::make_unique<LoopbackEndpointUVE>(relayPair.a));
        NetworkSessionUVE relayServer(SessionRoleUVE::Server,
                                      std::make_unique<LoopbackEndpointUVE>(relayPair.b));
        relayPeers.push_back(relay.AddPeerUVE(std::move(relayServer)));
        relayClients.push_back(std::move(relayClient));
        relayLinks.push_back(std::move(relayPair));
    }

    void UpdateAllUVE(const std::uint64_t nowMs) {
        lobby.UpdateUVE(nowMs);
        for (auto& facade : facades) {
            facade.UpdateUVE(nowMs);
        }
        relay.UpdateUVE(nowMs);
        for (auto& client : relayClients) {
            client.UpdateUVE(nowMs);
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
        for (auto& client : relayClients) {
            if (!client.ConnectUVE(nowMs)) {
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
        for (auto& client : relayClients) {
            if (client.StateUVE() != SessionStateUVE::Established) {
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
        for (const RelayPeerIdUVE id : relayPeers) {
            const NetworkSessionUVE* peer = relay.PeerSessionUVE(id);
            if (peer == nullptr || peer->StateUVE() != SessionStateUVE::Established) {
                return false;
            }
        }
        return true;
    }

    // The app's sync loop: tag every relay peer with its lobby room, verbatim.
    [[nodiscard]] bool SyncRoomsUVE() {
        for (std::size_t i = 0U; i < facades.size(); ++i) {
            if (!relay.SetPeerRoomUVE(relayPeers[i], lobby.ClientRoomUVE(facadeIds[i]))) {
                return false;
            }
        }
        return true;
    }
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

TEST(NetworkRelayRoomsUVETest, Rooms_TagGuards) {
    RelayRoomsHarnessUVE harness = MakeRelayRoomsUVE(2U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(harness.relay.PeerRoomUVE(harness.peerIds[0]), kDefaultRelayRoomUVE);
    EXPECT_EQ(harness.relay.PeerRoomUVE(harness.peerIds[1]), kDefaultRelayRoomUVE);
    EXPECT_EQ(harness.relay.PeerRoomUVE(999U), kDefaultRelayRoomUVE);
    EXPECT_FALSE(harness.relay.SetPeerRoomUVE(999U, 7U));
    EXPECT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[0], 7U));
    EXPECT_EQ(harness.relay.PeerRoomUVE(harness.peerIds[0]), 7U);
    EXPECT_EQ(harness.relay.PeerRoomUVE(harness.peerIds[1]), kDefaultRelayRoomUVE);
    EXPECT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[0], 3U));
    EXPECT_EQ(harness.relay.PeerRoomUVE(harness.peerIds[0]), 3U);
}

TEST(NetworkRelayRoomsUVETest, Rooms_ScopeDelivery) {
    RelayRoomsHarnessUVE harness = MakeRelayRoomsUVE(4U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[0], 7U));
    ASSERT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[1], 7U));
    ASSERT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[2], 9U));

    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("a")));
    harness.SettleUVE(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE("a"));
    EXPECT_FALSE(harness.clients[2].TryReceiveDataUVE().has_value());
    EXPECT_FALSE(harness.clients[3].TryReceiveDataUVE().has_value());

    ASSERT_TRUE(harness.clients[2].SendDataUVE(BytesUVE("c")));
    harness.SettleUVE(1200U);
    EXPECT_FALSE(harness.clients[0].TryReceiveDataUVE().has_value());
    EXPECT_FALSE(harness.clients[1].TryReceiveDataUVE().has_value());
    EXPECT_FALSE(harness.clients[3].TryReceiveDataUVE().has_value());

    ASSERT_TRUE(harness.clients[3].SendDataUVE(BytesUVE("d")));
    harness.SettleUVE(1300U);
    EXPECT_FALSE(harness.clients[0].TryReceiveDataUVE().has_value());
    EXPECT_FALSE(harness.clients[1].TryReceiveDataUVE().has_value());
    EXPECT_FALSE(harness.clients[2].TryReceiveDataUVE().has_value());

    const RelayStatsUVE stats = harness.relay.StatsUVE();
    EXPECT_EQ(stats.payloadsReceived, 3U);
    EXPECT_EQ(stats.payloadsForwarded, 1U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayRoomsUVETest, Rooms_DefaultRoomIsBroadcast) {
    RelayRoomsHarnessUVE harness = MakeRelayRoomsUVE(3U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    auto exchange = [&](const std::string& text, const std::uint64_t nowMs) {
        ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE(text)));
        harness.SettleUVE(nowMs);
        EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE(text));
        EXPECT_EQ(harness.clients[2].TryReceiveDataUVE(), BytesUVE(text));
        EXPECT_FALSE(harness.clients[0].TryReceiveDataUVE().has_value());
    };
    exchange("open", 1100U);
    for (const RelayPeerIdUVE id : harness.peerIds) {
        ASSERT_TRUE(harness.relay.SetPeerRoomUVE(id, 5U));
    }
    exchange("five", 1200U);
    for (const RelayPeerIdUVE id : harness.peerIds) {
        ASSERT_TRUE(harness.relay.SetPeerRoomUVE(id, kDefaultRelayRoomUVE));
    }
    exchange("open-again", 1300U);
}

TEST(NetworkRelayRoomsUVETest, Rooms_SenderAndUnestablishedExcluded) {
    RelayRoomsHarnessUVE harness = MakeRelayRoomsUVE(2U);
    ASSERT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[0], 7U));
    ASSERT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[1], 7U));
    ASSERT_TRUE(harness.clients[0].ConnectUVE(1000U));
    harness.UpdateAllUVE(1000U);
    harness.UpdateAllUVE(1000U);
    ASSERT_EQ(harness.clients[0].StateUVE(), SessionStateUVE::Established);
    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("a")));
    harness.SettleUVE(1100U);
    EXPECT_FALSE(harness.clients[0].TryReceiveDataUVE().has_value());
    EXPECT_FALSE(harness.clients[1].TryReceiveDataUVE().has_value());
    const RelayStatsUVE stats = harness.relay.StatsUVE();
    EXPECT_EQ(stats.payloadsReceived, 1U);
    EXPECT_EQ(stats.payloadsForwarded, 0U);
    EXPECT_EQ(stats.forwardsDropped, 0U);

    ASSERT_TRUE(harness.clients[1].ConnectUVE(1200U));
    harness.SettleUVE(1200U);
    ASSERT_EQ(harness.clients[1].StateUVE(), SessionStateUVE::Established);
    ASSERT_TRUE(harness.clients[1].SendDataUVE(BytesUVE("b")));
    harness.SettleUVE(1300U);
    EXPECT_EQ(harness.clients[0].TryReceiveDataUVE(), BytesUVE("b"));
}

TEST(NetworkRelayRoomsUVETest, Rooms_RemovalCleansRouting) {
    RelayRoomsHarnessUVE harness = MakeRelayRoomsUVE(3U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    for (const RelayPeerIdUVE id : harness.peerIds) {
        ASSERT_TRUE(harness.relay.SetPeerRoomUVE(id, 7U));
    }
    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("before")));
    harness.SettleUVE(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE("before"));
    EXPECT_EQ(harness.clients[2].TryReceiveDataUVE(), BytesUVE("before"));
    EXPECT_TRUE(harness.relay.RemovePeerUVE(harness.peerIds[1]));
    EXPECT_EQ(harness.relay.PeerRoomUVE(harness.peerIds[1]), kDefaultRelayRoomUVE);
    EXPECT_EQ(harness.relay.PeerCountUVE(), 2U);
    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("after")));
    harness.SettleUVE(1200U);
    EXPECT_EQ(harness.clients[2].TryReceiveDataUVE(), BytesUVE("after"));
    EXPECT_FALSE(harness.clients[0].TryReceiveDataUVE().has_value());
}

TEST(NetworkRelayRoomsUVETest, Rooms_RetagMidStream) {
    RelayRoomsHarnessUVE harness = MakeRelayRoomsUVE(2U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[0], 1U));
    ASSERT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[1], 1U));
    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("m1")));
    harness.SettleUVE(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE("m1"));
    ASSERT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[1], 2U));
    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("m2")));
    harness.SettleUVE(1200U);
    EXPECT_FALSE(harness.clients[1].TryReceiveDataUVE().has_value());
    ASSERT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[1], 1U));
    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("m3")));
    harness.SettleUVE(1300U);
    EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE("m3"));
}

TEST(NetworkRelayRoomsUVETest, Rooms_ManyRoomsIsolation) {
    RelayRoomsHarnessUVE harness = MakeRelayRoomsUVE(6U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    const RelayRoomIdUVE rooms[6] = {1U, 1U, 2U, 2U, 3U, 3U};
    for (std::size_t i = 0U; i < 6U; ++i) {
        ASSERT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[i], rooms[i]));
    }
    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("r1")));
    ASSERT_TRUE(harness.clients[2].SendDataUVE(BytesUVE("r2")));
    ASSERT_TRUE(harness.clients[4].SendDataUVE(BytesUVE("r3")));
    harness.SettleUVE(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE("r1"));
    EXPECT_EQ(harness.clients[3].TryReceiveDataUVE(), BytesUVE("r2"));
    EXPECT_EQ(harness.clients[5].TryReceiveDataUVE(), BytesUVE("r3"));
    EXPECT_FALSE(harness.clients[0].TryReceiveDataUVE().has_value());
    EXPECT_FALSE(harness.clients[2].TryReceiveDataUVE().has_value());
    EXPECT_FALSE(harness.clients[4].TryReceiveDataUVE().has_value());
    const RelayStatsUVE stats = harness.relay.StatsUVE();
    EXPECT_EQ(stats.payloadsReceived, 3U);
    EXPECT_EQ(stats.payloadsForwarded, 3U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayRoomsUVETest, Binding_LobbyRoomsDriveRelayRoutes) {
    BindingHarnessUVE harness;
    harness.AddClientUVE();
    harness.AddClientUVE();
    harness.AddClientUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(harness.lobby.ClientRoomUVE(999U), kInvalidLobbyRoomIdUVE);

    RoomResultUVE red;
    harness.facades[0].CreateRoomUVE(
        "red", "amy", 4U, [&](LobbyRoomIdUVE id, const std::string& error) {
            red.fired = true;
            red.id = id;
            red.error = error;
        });
    harness.SettleUVE(1100U);
    ASSERT_TRUE(red.fired);
    EXPECT_EQ(red.id, 1U);

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

    RoomResultUVE blue;
    harness.facades[2].CreateRoomUVE(
        "blue", "cat", 4U, [&](LobbyRoomIdUVE id, const std::string& error) {
            blue.fired = true;
            blue.id = id;
            blue.error = error;
        });
    harness.SettleUVE(1300U);
    ASSERT_TRUE(blue.fired);
    EXPECT_EQ(blue.id, 2U);

    EXPECT_EQ(harness.lobby.ClientRoomUVE(harness.facadeIds[0]), 1U);
    EXPECT_EQ(harness.lobby.ClientRoomUVE(harness.facadeIds[1]), 1U);
    EXPECT_EQ(harness.lobby.ClientRoomUVE(harness.facadeIds[2]), 2U);
    ASSERT_TRUE(harness.SyncRoomsUVE());
    EXPECT_EQ(harness.relay.PeerRoomUVE(harness.relayPeers[0]), 1U);
    EXPECT_EQ(harness.relay.PeerRoomUVE(harness.relayPeers[1]), 1U);
    EXPECT_EQ(harness.relay.PeerRoomUVE(harness.relayPeers[2]), 2U);

    auto sendGame = [&](const std::size_t from, const std::string& text,
                        const std::uint64_t nowMs) {
        ASSERT_TRUE(harness.relayClients[from].SendDataUVE(BytesUVE(text)));
        harness.SettleUVE(nowMs);
    };
    auto inbox = [&](const std::size_t who) {
        return harness.relayClients[who].TryReceiveDataUVE();
    };

    sendGame(0U, "a1", 1400U);
    EXPECT_EQ(inbox(1U), BytesUVE("a1"));
    EXPECT_FALSE(inbox(2U).has_value());
    EXPECT_FALSE(inbox(0U).has_value());

    sendGame(2U, "c1", 1500U);
    EXPECT_FALSE(inbox(0U).has_value());
    EXPECT_FALSE(inbox(1U).has_value());
    EXPECT_FALSE(inbox(2U).has_value());

    LeaveResultUVE bobLeft;
    harness.facades[1].LeaveRoomUVE([&](bool left, const std::string& error) {
        bobLeft.fired = true;
        bobLeft.left = left;
        bobLeft.error = error;
    });
    harness.SettleUVE(1600U);
    ASSERT_TRUE(bobLeft.fired);
    EXPECT_TRUE(bobLeft.left);
    EXPECT_EQ(harness.lobby.ClientRoomUVE(harness.facadeIds[1]), kInvalidLobbyRoomIdUVE);
    ASSERT_TRUE(harness.SyncRoomsUVE());

    sendGame(0U, "a2", 1700U);
    EXPECT_FALSE(inbox(1U).has_value());
    EXPECT_FALSE(inbox(2U).has_value());

    sendGame(1U, "b1", 1800U);
    EXPECT_FALSE(inbox(0U).has_value());
    EXPECT_FALSE(inbox(2U).has_value());

    RosterResultUVE moved;
    harness.facades[1].JoinRoomUVE(
        2U, "bob", [&](std::vector<std::string> roster, const std::string& error) {
            moved.fired = true;
            moved.roster = std::move(roster);
            moved.error = error;
        });
    harness.SettleUVE(1900U);
    ASSERT_TRUE(moved.fired);
    EXPECT_TRUE(moved.error.empty());
    ASSERT_TRUE(harness.SyncRoomsUVE());

    sendGame(2U, "c2", 2000U);
    EXPECT_EQ(inbox(1U), BytesUVE("c2"));
    EXPECT_FALSE(inbox(0U).has_value());

    const RelayStatsUVE stats = harness.relay.StatsUVE();
    EXPECT_EQ(stats.payloadsReceived, 5U);
    EXPECT_EQ(stats.payloadsForwarded, 2U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayRoomsUVETest, Binding_ClientRoomReads) {
    BindingHarnessUVE harness;
    harness.AddClientUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    const LobbyClientIdUVE id = harness.facadeIds[0];
    EXPECT_EQ(harness.lobby.ClientRoomUVE(999U), kInvalidLobbyRoomIdUVE);
    EXPECT_EQ(harness.lobby.ClientRoomUVE(id), kInvalidLobbyRoomIdUVE);

    RoomResultUVE created;
    harness.facades[0].CreateRoomUVE(
        "red", "amy", 4U, [&](LobbyRoomIdUVE createdId, const std::string& error) {
            created.fired = true;
            created.id = createdId;
            created.error = error;
        });
    harness.SettleUVE(1100U);
    ASSERT_TRUE(created.fired);
    EXPECT_EQ(created.id, 1U);
    EXPECT_EQ(harness.lobby.ClientRoomUVE(id), 1U);

    LeaveResultUVE left;
    harness.facades[0].LeaveRoomUVE([&](bool wasLeft, const std::string& error) {
        left.fired = true;
        left.left = wasLeft;
        left.error = error;
    });
    harness.SettleUVE(1200U);
    ASSERT_TRUE(left.fired);
    EXPECT_TRUE(left.left);
    EXPECT_EQ(harness.lobby.ClientRoomUVE(id), kInvalidLobbyRoomIdUVE);
}

} // namespace
} // namespace UVE::Network::Tests
