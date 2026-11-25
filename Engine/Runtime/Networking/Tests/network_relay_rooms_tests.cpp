// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_relay.h"

#include "network_lobby.h"
#include "network_session.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

[[nodiscard]] std::vector<std::uint8_t> Bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

struct RelayRoomsHarness final {
    std::vector<LoopbackTransportPair> links;
    std::vector<NetworkSession> clients;
    NetworkRelay relay;
    std::vector<RelayPeerId> peerIds;

    void UpdateAll(const std::uint64_t nowMs) {
        relay.Update(nowMs);
        for (auto& client : clients) {
            client.Update(nowMs);
        }
    }

    void Settle(const std::uint64_t nowMs) {
        UpdateAll(nowMs);
        UpdateAll(nowMs);
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
            if (client.State() != SessionState::Established) {
                return false;
            }
        }
        for (const RelayPeerId id : peerIds) {
            const NetworkSession* peer = relay.PeerSession(id);
            if (peer == nullptr || peer->State() != SessionState::Established) {
                return false;
            }
        }
        return true;
    }
};

[[nodiscard]] RelayRoomsHarness MakeRelayRooms(const std::size_t clientCount) {
    RelayRoomsHarness harness;
    for (std::size_t i = 0U; i < clientCount; ++i) {
        LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
        NetworkSession client(SessionRole::Client,
                                 std::make_unique<LoopbackEndpoint>(pair.a));
        NetworkSession server(SessionRole::Server,
                                 std::make_unique<LoopbackEndpoint>(pair.b));
        harness.peerIds.push_back(harness.relay.AddPeer(std::move(server)));
        harness.clients.push_back(std::move(client));
        harness.links.push_back(std::move(pair));
    }
    return harness;
}

struct BindingHarness final {
    std::vector<LoopbackTransportPair> lobbyLinks;
    std::vector<NetworkLobbyClient> facades;
    std::vector<LobbyClientId> facadeIds;
    NetworkLobby lobby;
    std::vector<LoopbackTransportPair> relayLinks;
    std::vector<NetworkSession> relayClients;
    std::vector<RelayPeerId> relayPeers;
    NetworkRelay relay;

    void AddClient() {
        LoopbackTransportPair lobbyPair = CreateLoopbackTransportPair(256U);
        NetworkSession lobbyClient(SessionRole::Client,
                                      std::make_unique<LoopbackEndpoint>(lobbyPair.a));
        NetworkSession lobbyServer(SessionRole::Server,
                                      std::make_unique<LoopbackEndpoint>(lobbyPair.b));
        facadeIds.push_back(lobby.AddClient(std::move(lobbyServer)));
        facades.emplace_back(std::move(lobbyClient));
        lobbyLinks.push_back(std::move(lobbyPair));

        LoopbackTransportPair relayPair = CreateLoopbackTransportPair(256U);
        NetworkSession relayClient(SessionRole::Client,
                                      std::make_unique<LoopbackEndpoint>(relayPair.a));
        NetworkSession relayServer(SessionRole::Server,
                                      std::make_unique<LoopbackEndpoint>(relayPair.b));
        relayPeers.push_back(relay.AddPeer(std::move(relayServer)));
        relayClients.push_back(std::move(relayClient));
        relayLinks.push_back(std::move(relayPair));
    }

    void UpdateAll(const std::uint64_t nowMs) {
        lobby.Update(nowMs);
        for (auto& facade : facades) {
            facade.Update(nowMs);
        }
        relay.Update(nowMs);
        for (auto& client : relayClients) {
            client.Update(nowMs);
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
        for (auto& client : relayClients) {
            if (!client.Connect(nowMs)) {
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
        for (auto& client : relayClients) {
            if (client.State() != SessionState::Established) {
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
        for (const RelayPeerId id : relayPeers) {
            const NetworkSession* peer = relay.PeerSession(id);
            if (peer == nullptr || peer->State() != SessionState::Established) {
                return false;
            }
        }
        return true;
    }

    // The app's sync loop: tag every relay peer with its lobby room, verbatim.
    [[nodiscard]] bool SyncRooms() {
        for (std::size_t i = 0U; i < facades.size(); ++i) {
            if (!relay.SetPeerRoom(relayPeers[i], lobby.ClientRoom(facadeIds[i]))) {
                return false;
            }
        }
        return true;
    }
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

TEST(NetworkRelayRoomsTest, Rooms_TagGuards) {
    RelayRoomsHarness harness = MakeRelayRooms(2U);
    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(harness.relay.PeerRoom(harness.peerIds[0]), kDefaultRelayRoom);
    EXPECT_EQ(harness.relay.PeerRoom(harness.peerIds[1]), kDefaultRelayRoom);
    EXPECT_EQ(harness.relay.PeerRoom(999U), kDefaultRelayRoom);
    EXPECT_FALSE(harness.relay.SetPeerRoom(999U, 7U));
    EXPECT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[0], 7U));
    EXPECT_EQ(harness.relay.PeerRoom(harness.peerIds[0]), 7U);
    EXPECT_EQ(harness.relay.PeerRoom(harness.peerIds[1]), kDefaultRelayRoom);
    EXPECT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[0], 3U));
    EXPECT_EQ(harness.relay.PeerRoom(harness.peerIds[0]), 3U);
}

TEST(NetworkRelayRoomsTest, Rooms_ScopeDelivery) {
    RelayRoomsHarness harness = MakeRelayRooms(4U);
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[0], 7U));
    ASSERT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[1], 7U));
    ASSERT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[2], 9U));

    ASSERT_TRUE(harness.clients[0].SendData(Bytes("a")));
    harness.Settle(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes("a"));
    EXPECT_FALSE(harness.clients[2].TryReceiveData().has_value());
    EXPECT_FALSE(harness.clients[3].TryReceiveData().has_value());

    ASSERT_TRUE(harness.clients[2].SendData(Bytes("c")));
    harness.Settle(1200U);
    EXPECT_FALSE(harness.clients[0].TryReceiveData().has_value());
    EXPECT_FALSE(harness.clients[1].TryReceiveData().has_value());
    EXPECT_FALSE(harness.clients[3].TryReceiveData().has_value());

    ASSERT_TRUE(harness.clients[3].SendData(Bytes("d")));
    harness.Settle(1300U);
    EXPECT_FALSE(harness.clients[0].TryReceiveData().has_value());
    EXPECT_FALSE(harness.clients[1].TryReceiveData().has_value());
    EXPECT_FALSE(harness.clients[2].TryReceiveData().has_value());

    const RelayStats stats = harness.relay.Stats();
    EXPECT_EQ(stats.payloadsReceived, 3U);
    EXPECT_EQ(stats.payloadsForwarded, 1U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayRoomsTest, Rooms_DefaultRoomIsBroadcast) {
    RelayRoomsHarness harness = MakeRelayRooms(3U);
    ASSERT_TRUE(harness.Establish(1000U));
    auto exchange = [&](const std::string& text, const std::uint64_t nowMs) {
        ASSERT_TRUE(harness.clients[0].SendData(Bytes(text)));
        harness.Settle(nowMs);
        EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes(text));
        EXPECT_EQ(harness.clients[2].TryReceiveData(), Bytes(text));
        EXPECT_FALSE(harness.clients[0].TryReceiveData().has_value());
    };
    exchange("open", 1100U);
    for (const RelayPeerId id : harness.peerIds) {
        ASSERT_TRUE(harness.relay.SetPeerRoom(id, 5U));
    }
    exchange("five", 1200U);
    for (const RelayPeerId id : harness.peerIds) {
        ASSERT_TRUE(harness.relay.SetPeerRoom(id, kDefaultRelayRoom));
    }
    exchange("open-again", 1300U);
}

TEST(NetworkRelayRoomsTest, Rooms_SenderAndUnestablishedExcluded) {
    RelayRoomsHarness harness = MakeRelayRooms(2U);
    ASSERT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[0], 7U));
    ASSERT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[1], 7U));
    ASSERT_TRUE(harness.clients[0].Connect(1000U));
    harness.UpdateAll(1000U);
    harness.UpdateAll(1000U);
    ASSERT_EQ(harness.clients[0].State(), SessionState::Established);
    ASSERT_TRUE(harness.clients[0].SendData(Bytes("a")));
    harness.Settle(1100U);
    EXPECT_FALSE(harness.clients[0].TryReceiveData().has_value());
    EXPECT_FALSE(harness.clients[1].TryReceiveData().has_value());
    const RelayStats stats = harness.relay.Stats();
    EXPECT_EQ(stats.payloadsReceived, 1U);
    EXPECT_EQ(stats.payloadsForwarded, 0U);
    EXPECT_EQ(stats.forwardsDropped, 0U);

    ASSERT_TRUE(harness.clients[1].Connect(1200U));
    harness.Settle(1200U);
    ASSERT_EQ(harness.clients[1].State(), SessionState::Established);
    ASSERT_TRUE(harness.clients[1].SendData(Bytes("b")));
    harness.Settle(1300U);
    EXPECT_EQ(harness.clients[0].TryReceiveData(), Bytes("b"));
}

TEST(NetworkRelayRoomsTest, Rooms_RemovalCleansRouting) {
    RelayRoomsHarness harness = MakeRelayRooms(3U);
    ASSERT_TRUE(harness.Establish(1000U));
    for (const RelayPeerId id : harness.peerIds) {
        ASSERT_TRUE(harness.relay.SetPeerRoom(id, 7U));
    }
    ASSERT_TRUE(harness.clients[0].SendData(Bytes("before")));
    harness.Settle(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes("before"));
    EXPECT_EQ(harness.clients[2].TryReceiveData(), Bytes("before"));
    EXPECT_TRUE(harness.relay.RemovePeer(harness.peerIds[1]));
    EXPECT_EQ(harness.relay.PeerRoom(harness.peerIds[1]), kDefaultRelayRoom);
    EXPECT_EQ(harness.relay.PeerCount(), 2U);
    ASSERT_TRUE(harness.clients[0].SendData(Bytes("after")));
    harness.Settle(1200U);
    EXPECT_EQ(harness.clients[2].TryReceiveData(), Bytes("after"));
    EXPECT_FALSE(harness.clients[0].TryReceiveData().has_value());
}

TEST(NetworkRelayRoomsTest, Rooms_RetagMidStream) {
    RelayRoomsHarness harness = MakeRelayRooms(2U);
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[0], 1U));
    ASSERT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[1], 1U));
    ASSERT_TRUE(harness.clients[0].SendData(Bytes("m1")));
    harness.Settle(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes("m1"));
    ASSERT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[1], 2U));
    ASSERT_TRUE(harness.clients[0].SendData(Bytes("m2")));
    harness.Settle(1200U);
    EXPECT_FALSE(harness.clients[1].TryReceiveData().has_value());
    ASSERT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[1], 1U));
    ASSERT_TRUE(harness.clients[0].SendData(Bytes("m3")));
    harness.Settle(1300U);
    EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes("m3"));
}

TEST(NetworkRelayRoomsTest, Rooms_ManyRoomsIsolation) {
    RelayRoomsHarness harness = MakeRelayRooms(6U);
    ASSERT_TRUE(harness.Establish(1000U));
    const RelayRoomId rooms[6] = {1U, 1U, 2U, 2U, 3U, 3U};
    for (std::size_t i = 0U; i < 6U; ++i) {
        ASSERT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[i], rooms[i]));
    }
    ASSERT_TRUE(harness.clients[0].SendData(Bytes("r1")));
    ASSERT_TRUE(harness.clients[2].SendData(Bytes("r2")));
    ASSERT_TRUE(harness.clients[4].SendData(Bytes("r3")));
    harness.Settle(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes("r1"));
    EXPECT_EQ(harness.clients[3].TryReceiveData(), Bytes("r2"));
    EXPECT_EQ(harness.clients[5].TryReceiveData(), Bytes("r3"));
    EXPECT_FALSE(harness.clients[0].TryReceiveData().has_value());
    EXPECT_FALSE(harness.clients[2].TryReceiveData().has_value());
    EXPECT_FALSE(harness.clients[4].TryReceiveData().has_value());
    const RelayStats stats = harness.relay.Stats();
    EXPECT_EQ(stats.payloadsReceived, 3U);
    EXPECT_EQ(stats.payloadsForwarded, 3U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayRoomsTest, Binding_LobbyRoomsDriveRelayRoutes) {
    BindingHarness harness;
    harness.AddClient();
    harness.AddClient();
    harness.AddClient();
    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(harness.lobby.ClientRoom(999U), kInvalidLobbyRoomId);

    RoomResult red;
    harness.facades[0].CreateRoom(
        "red", "amy", 4U, [&](LobbyRoomId id, const std::string& error) {
            red.fired = true;
            red.id = id;
            red.error = error;
        });
    harness.Settle(1100U);
    ASSERT_TRUE(red.fired);
    EXPECT_EQ(red.id, 1U);

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

    RoomResult blue;
    harness.facades[2].CreateRoom(
        "blue", "cat", 4U, [&](LobbyRoomId id, const std::string& error) {
            blue.fired = true;
            blue.id = id;
            blue.error = error;
        });
    harness.Settle(1300U);
    ASSERT_TRUE(blue.fired);
    EXPECT_EQ(blue.id, 2U);

    EXPECT_EQ(harness.lobby.ClientRoom(harness.facadeIds[0]), 1U);
    EXPECT_EQ(harness.lobby.ClientRoom(harness.facadeIds[1]), 1U);
    EXPECT_EQ(harness.lobby.ClientRoom(harness.facadeIds[2]), 2U);
    ASSERT_TRUE(harness.SyncRooms());
    EXPECT_EQ(harness.relay.PeerRoom(harness.relayPeers[0]), 1U);
    EXPECT_EQ(harness.relay.PeerRoom(harness.relayPeers[1]), 1U);
    EXPECT_EQ(harness.relay.PeerRoom(harness.relayPeers[2]), 2U);

    auto sendGame = [&](const std::size_t from, const std::string& text,
                        const std::uint64_t nowMs) {
        ASSERT_TRUE(harness.relayClients[from].SendData(Bytes(text)));
        harness.Settle(nowMs);
    };
    auto inbox = [&](const std::size_t who) {
        return harness.relayClients[who].TryReceiveData();
    };

    sendGame(0U, "a1", 1400U);
    EXPECT_EQ(inbox(1U), Bytes("a1"));
    EXPECT_FALSE(inbox(2U).has_value());
    EXPECT_FALSE(inbox(0U).has_value());

    sendGame(2U, "c1", 1500U);
    EXPECT_FALSE(inbox(0U).has_value());
    EXPECT_FALSE(inbox(1U).has_value());
    EXPECT_FALSE(inbox(2U).has_value());

    LeaveResult bobLeft;
    harness.facades[1].LeaveRoom([&](bool left, const std::string& error) {
        bobLeft.fired = true;
        bobLeft.left = left;
        bobLeft.error = error;
    });
    harness.Settle(1600U);
    ASSERT_TRUE(bobLeft.fired);
    EXPECT_TRUE(bobLeft.left);
    EXPECT_EQ(harness.lobby.ClientRoom(harness.facadeIds[1]), kInvalidLobbyRoomId);
    ASSERT_TRUE(harness.SyncRooms());

    sendGame(0U, "a2", 1700U);
    EXPECT_FALSE(inbox(1U).has_value());
    EXPECT_FALSE(inbox(2U).has_value());

    sendGame(1U, "b1", 1800U);
    EXPECT_FALSE(inbox(0U).has_value());
    EXPECT_FALSE(inbox(2U).has_value());

    RosterResult moved;
    harness.facades[1].JoinRoom(
        2U, "bob", [&](std::vector<std::string> roster, const std::string& error) {
            moved.fired = true;
            moved.roster = std::move(roster);
            moved.error = error;
        });
    harness.Settle(1900U);
    ASSERT_TRUE(moved.fired);
    EXPECT_TRUE(moved.error.empty());
    ASSERT_TRUE(harness.SyncRooms());

    sendGame(2U, "c2", 2000U);
    EXPECT_EQ(inbox(1U), Bytes("c2"));
    EXPECT_FALSE(inbox(0U).has_value());

    const RelayStats stats = harness.relay.Stats();
    EXPECT_EQ(stats.payloadsReceived, 5U);
    EXPECT_EQ(stats.payloadsForwarded, 2U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayRoomsTest, Binding_ClientRoomReads) {
    BindingHarness harness;
    harness.AddClient();
    ASSERT_TRUE(harness.Establish(1000U));
    const LobbyClientId id = harness.facadeIds[0];
    EXPECT_EQ(harness.lobby.ClientRoom(999U), kInvalidLobbyRoomId);
    EXPECT_EQ(harness.lobby.ClientRoom(id), kInvalidLobbyRoomId);

    RoomResult created;
    harness.facades[0].CreateRoom(
        "red", "amy", 4U, [&](LobbyRoomId createdId, const std::string& error) {
            created.fired = true;
            created.id = createdId;
            created.error = error;
        });
    harness.Settle(1100U);
    ASSERT_TRUE(created.fired);
    EXPECT_EQ(created.id, 1U);
    EXPECT_EQ(harness.lobby.ClientRoom(id), 1U);

    LeaveResult left;
    harness.facades[0].LeaveRoom([&](bool wasLeft, const std::string& error) {
        left.fired = true;
        left.left = wasLeft;
        left.error = error;
    });
    harness.Settle(1200U);
    ASSERT_TRUE(left.fired);
    EXPECT_TRUE(left.left);
    EXPECT_EQ(harness.lobby.ClientRoom(id), kInvalidLobbyRoomId);
}

} // namespace
} // namespace UVE::Network::Tests
