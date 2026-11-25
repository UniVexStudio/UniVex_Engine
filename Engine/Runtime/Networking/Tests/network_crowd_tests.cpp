// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// The crowd soak: N clients storming the lobby and the relay under a seeded script,
// proving the spine holds together at scale. Determinism (same seed, same stats),
// conservation (every send arrives once, every room is born empty-removed or lives),
// no stranded callbacks, and exact mesh/avalanche/churn arithmetic.

#include "network_lobby.h"
#include "network_relay.h"
#include "network_session.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

[[nodiscard]] std::vector<std::uint8_t> Bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

struct CrowdHarness final {
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

    void AddClients(const std::size_t count) {
        for (std::size_t i = 0U; i < count; ++i) {
            AddClient();
        }
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
        return true;
    }

    [[nodiscard]] bool SyncRooms() {
        for (std::size_t i = 0U; i < facades.size(); ++i) {
            if (!relay.SetPeerRoom(relayPeers[i], lobby.ClientRoom(facadeIds[i]))) {
                return false;
            }
        }
        return true;
    }
};

struct StormDigest final {
    LobbyStats lobby;
    RelayStats relay;
    std::size_t rooms = 0U;
    std::size_t peers = 0U;
    std::vector<LobbyRoomId> serverRooms;
    std::vector<LobbyRoomId> cachedRooms;
    std::uint64_t initiated = 0U;
    std::uint64_t fired = 0U;
    std::uint64_t sent = 0U;
    std::uint64_t drained = 0U;

    [[nodiscard]] bool operator==(const StormDigest&) const noexcept = default;
};

// One storm: every client runs one seeded op per tick (relay sends, lobby calls, interest
// mutes), then the tick settles and the app re-syncs relay rooms from the lobby.
[[nodiscard]] bool RunStorm(CrowdHarness& harness, StormDigest& digest,
                               const unsigned seed, const int ticks) {
    if (!harness.Establish(1000U)) {
        return false;
    }
    std::mt19937 rng(seed);
    const std::size_t clients = harness.facades.size();
    std::uint64_t initiated = 0U;
    std::uint64_t fired = 0U;
    std::uint64_t sent = 0U;
    std::uint64_t nowMs = 1100U;
    for (int tick = 0; tick < ticks; ++tick) {
        for (std::size_t i = 0U; i < clients; ++i) {
            const int op = static_cast<int>(rng() % 10U);
            const std::string me = "p" + std::to_string(i);
            if (op <= 2) {
                const std::string text =
                    "s" + std::to_string(tick) + "-" + std::to_string(i);
                if (harness.relayClients[i].SendData(Bytes(text))) {
                    ++sent;
                }
            } else if (op == 3) {
                ++initiated;
                harness.facades[i].CreateRoom(
                    "r" + std::to_string(tick) + "c" + std::to_string(i), me, 8U,
                    [&](LobbyRoomId, const std::string&) { ++fired; });
            } else if (op == 4) {
                const auto room = static_cast<LobbyRoomId>(rng() % 12U + 1U);
                ++initiated;
                harness.facades[i].JoinRoom(
                    room, me,
                    [&](std::vector<std::string>, const std::string&) { ++fired; });
            } else if (op == 5) {
                ++initiated;
                harness.facades[i].LeaveRoom(
                    [&](bool, const std::string&) { ++fired; });
            } else if (op == 6) {
                ++initiated;
                harness.facades[i].ListRooms(
                    [&](std::vector<LobbyRoomInfo>, const std::string&) { ++fired; });
            } else if (op == 7) {
                const auto room = static_cast<LobbyRoomId>(rng() % 12U + 1U);
                ++initiated;
                harness.facades[i].QueryRoster(
                    room, [&](std::vector<std::string>, const std::string&) { ++fired; });
            } else if (op == 8) {
                const std::size_t a = rng() % clients;
                std::size_t b = rng() % clients;
                if (b == a) {
                    b = (b + 1U) % clients;
                }
                // Ids are valid by construction; the sync loop below is the asserted path.
                static_cast<void>(harness.relay.SetPeerInterest(
                    harness.relayPeers[a], harness.relayPeers[b], (rng() % 2U) == 0U));
            }
        }
        harness.Settle(nowMs);
        nowMs += 100U;
        static_cast<void>(harness.SyncRooms());
    }
    std::uint64_t drained = 0U;
    for (std::size_t i = 0U; i < clients; ++i) {
        while (harness.relayClients[i].TryReceiveData().has_value()) {
            ++drained;
        }
    }
    digest.lobby = harness.lobby.Stats();
    digest.relay = harness.relay.Stats();
    digest.rooms = harness.lobby.RoomCount();
    digest.peers = harness.lobby.PeerCount();
    for (std::size_t i = 0U; i < clients; ++i) {
        digest.serverRooms.push_back(harness.lobby.ClientRoom(harness.facadeIds[i]));
        digest.cachedRooms.push_back(harness.facades[i].CurrentRoom());
    }
    digest.initiated = initiated;
    digest.fired = fired;
    digest.sent = sent;
    digest.drained = drained;
    return true;
}

TEST(NetworkCrowdTest, Storm_DeterministicTwice) {
    CrowdHarness first;
    first.AddClients(32U);
    StormDigest firstDigest;
    ASSERT_TRUE(RunStorm(first, firstDigest, 7U, 40));

    CrowdHarness second;
    second.AddClients(32U);
    StormDigest secondDigest;
    ASSERT_TRUE(RunStorm(second, secondDigest, 7U, 40));

    EXPECT_EQ(firstDigest.lobby, secondDigest.lobby);
    EXPECT_EQ(firstDigest.relay, secondDigest.relay);
    EXPECT_EQ(firstDigest.rooms, secondDigest.rooms);
    EXPECT_EQ(firstDigest.peers, secondDigest.peers);
    EXPECT_EQ(firstDigest.serverRooms, secondDigest.serverRooms);
    EXPECT_EQ(firstDigest.cachedRooms, secondDigest.cachedRooms);
    EXPECT_EQ(firstDigest.initiated, secondDigest.initiated);
    EXPECT_EQ(firstDigest.fired, secondDigest.fired);
    EXPECT_EQ(firstDigest.sent, secondDigest.sent);
    EXPECT_EQ(firstDigest.drained, secondDigest.drained);
    EXPECT_GT(firstDigest.initiated, 0U);
    EXPECT_GT(firstDigest.sent, 0U);
}

TEST(NetworkCrowdTest, Storm_InvariantsHold) {
    CrowdHarness harness;
    harness.AddClients(32U);
    StormDigest digest;
    ASSERT_TRUE(RunStorm(harness, digest, 11U, 40));

    for (std::size_t i = 0U; i < 32U; ++i) {
        EXPECT_EQ(harness.facades[i].Session().State(),
                  SessionState::Established);
        EXPECT_EQ(harness.relayClients[i].State(), SessionState::Established);
        EXPECT_EQ(harness.facades[i].CurrentRoom(),
                  harness.lobby.ClientRoom(harness.facadeIds[i]));
    }

    bool listed = false;
    std::vector<LobbyRoomInfo> rooms;
    harness.facades[0].ListRooms(
        [&](std::vector<LobbyRoomInfo> found, const std::string& error) {
            listed = true;
            rooms = std::move(found);
            EXPECT_TRUE(error.empty());
        });
    harness.Settle(5100U);
    ASSERT_TRUE(listed);
    EXPECT_EQ(rooms.size(), harness.lobby.RoomCount());
    std::uint32_t seated = 0U;
    for (const LobbyRoomInfo& room : rooms) {
        EXPECT_GT(room.players, 0U);
        seated += room.players;
    }
    std::uint32_t expected = 0U;
    for (const LobbyRoomId room : digest.serverRooms) {
        if (room != kInvalidLobbyRoomId) {
            ++expected;
        }
    }
    EXPECT_EQ(seated, expected);

    const RelayStats before = harness.relay.Stats();
    const LobbyStats lobbyBefore = harness.lobby.Stats();
    harness.Settle(5200U);
    harness.Settle(5300U);
    EXPECT_EQ(harness.relay.Stats(), before);
    EXPECT_EQ(harness.lobby.Stats(), lobbyBefore);
}

TEST(NetworkCrowdTest, Storm_Conservation) {
    CrowdHarness harness;
    harness.AddClients(32U);
    StormDigest digest;
    ASSERT_TRUE(RunStorm(harness, digest, 13U, 40));

    EXPECT_EQ(digest.relay.payloadsReceived, digest.sent);
    EXPECT_EQ(digest.drained, digest.relay.payloadsForwarded);
    EXPECT_LE(digest.relay.payloadsForwarded, digest.relay.payloadsReceived * 31U);
    EXPECT_EQ(digest.relay.forwardsDropped, 0U);
    EXPECT_EQ(digest.lobby.roomsCreated,
              digest.lobby.roomsRemoved + harness.lobby.RoomCount());
}

TEST(NetworkCrowdTest, Storm_NoStrandedCallbacks) {
    CrowdHarness harness;
    harness.AddClients(24U);
    StormDigest digest;
    ASSERT_TRUE(RunStorm(harness, digest, 21U, 30));
    EXPECT_GT(digest.initiated, 0U);
    EXPECT_EQ(digest.fired, digest.initiated);
}

TEST(NetworkCrowdTest, Storm_SingleRoomJoinAvalanche) {
    CrowdHarness harness;
    harness.AddClients(32U);
    ASSERT_TRUE(harness.Establish(1000U));

    bool created = false;
    harness.facades[0].CreateRoom(
        "mega", "p0", 64U, [&](LobbyRoomId id, const std::string& error) {
            created = true;
            EXPECT_EQ(id, 1U);
            EXPECT_TRUE(error.empty());
        });
    harness.Settle(1100U);
    ASSERT_TRUE(created);

    std::uint32_t joined = 0U;
    for (std::size_t i = 1U; i < 32U; ++i) {
        harness.facades[i].JoinRoom(
            1U, "p" + std::to_string(i),
            [&](std::vector<std::string>, const std::string& error) {
                ++joined;
                EXPECT_TRUE(error.empty());
            });
    }
    std::uint64_t nowMs = 1200U;
    for (int round = 0; round < 15; ++round) {
        harness.Settle(nowMs);
        nowMs += 100U;
    }
    EXPECT_EQ(joined, 31U);

    std::vector<std::string> full;
    for (std::size_t i = 0U; i < 32U; ++i) {
        full.push_back("p" + std::to_string(i));
    }
    for (std::size_t i = 0U; i < 32U; ++i) {
        EXPECT_EQ(harness.facades[i].CachedRoster(), full);
    }
    const LobbyStats stats = harness.lobby.Stats();
    EXPECT_EQ(stats.joinsAccepted, 31U);
    EXPECT_EQ(stats.joinsRejected, 0U);
    EXPECT_EQ(stats.notificationsSent, 496U);
    EXPECT_EQ(stats.notificationsDropped, 0U);
}

TEST(NetworkCrowdTest, Storm_RelayMeshFlood) {
    CrowdHarness harness;
    harness.AddClients(16U);
    ASSERT_TRUE(harness.Establish(1000U));

    std::uint64_t nowMs = 1100U;
    for (int round = 0; round < 10; ++round) {
        for (std::size_t i = 0U; i < 16U; ++i) {
            ASSERT_TRUE(harness.relayClients[i].SendData(
                Bytes("r" + std::to_string(round) + "c" + std::to_string(i))));
        }
        harness.Settle(nowMs);
        nowMs += 100U;
    }
    for (std::size_t i = 0U; i < 16U; ++i) {
        std::uint32_t inbox = 0U;
        while (harness.relayClients[i].TryReceiveData().has_value()) {
            ++inbox;
        }
        EXPECT_EQ(inbox, 150U);
    }
    const RelayStats stats = harness.relay.Stats();
    EXPECT_EQ(stats.payloadsReceived, 160U);
    EXPECT_EQ(stats.payloadsForwarded, 2400U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkCrowdTest, Storm_ChurnRooms) {
    CrowdHarness harness;
    harness.AddClients(12U);
    ASSERT_TRUE(harness.Establish(1000U));

    std::uint64_t nowMs = 1100U;
    for (int round = 0; round < 5; ++round) {
        std::vector<LobbyRoomId> ids(12U, kInvalidLobbyRoomId);
        std::uint32_t created = 0U;
        for (std::size_t i = 0U; i < 12U; ++i) {
            harness.facades[i].CreateRoom(
                "c" + std::to_string(i) + "r" + std::to_string(round),
                "p" + std::to_string(i), 8U,
                [&, i](LobbyRoomId id, const std::string& error) {
                    ++created;
                    EXPECT_TRUE(error.empty());
                    ids[i] = id;
                });
        }
        harness.Settle(nowMs);
        nowMs += 100U;
        ASSERT_EQ(created, 12U);

        std::uint32_t joined = 0U;
        std::vector<std::pair<std::size_t, std::string>> joinFailures;
        for (std::size_t i = 0U; i < 12U; ++i) {
            ASSERT_NE(ids[i], kInvalidLobbyRoomId);
            harness.facades[i].JoinRoom(
                ids[(i + 1U) % 12U], "p" + std::to_string(i),
                [&, i](std::vector<std::string>, const std::string& error) {
                    if (error.empty()) {
                        ++joined;
                    } else {
                        joinFailures.emplace_back(i, error);
                    }
                });
        }
        harness.Settle(nowMs);
        nowMs += 100U;
        // Joins process in peer-id order: client 0 leaves R0 first, so client 11 always
        // arrives at a removed room. One race rejection per round, deterministically.
        ASSERT_EQ(joined, 11U);
        ASSERT_EQ(joinFailures.size(), 1U);
        EXPECT_EQ(joinFailures[0].first, 11U);
        EXPECT_EQ(joinFailures[0].second.rfind("unknown room", 0U), 0U);

        std::uint32_t left = 0U;
        for (std::size_t i = 0U; i < 12U; ++i) {
            harness.facades[i].LeaveRoom([&](bool wasLeft, const std::string& error) {
                ++left;
                EXPECT_TRUE(wasLeft);
                EXPECT_TRUE(error.empty());
            });
        }
        harness.Settle(nowMs);
        nowMs += 100U;
        ASSERT_EQ(left, 12U);
    }

    const LobbyStats stats = harness.lobby.Stats();
    EXPECT_EQ(stats.roomsCreated, 60U);
    EXPECT_EQ(stats.roomsRemoved, 60U);
    EXPECT_EQ(stats.joinsAccepted, 55U);
    EXPECT_EQ(stats.joinsRejected, 5U);
    EXPECT_EQ(stats.leavesAccepted, 115U);
    EXPECT_EQ(stats.leavesRejected, 0U);
    EXPECT_EQ(stats.createsRejected, 0U);
    // 22 per round: 11 join-notifies + 10 auto-leave-notifies + 1 two-member-leave-notify.
    EXPECT_EQ(stats.notificationsSent, 110U);
    EXPECT_EQ(stats.notificationsDropped, 0U);
    EXPECT_EQ(harness.lobby.RoomCount(), 0U);
}

TEST(NetworkCrowdTest, Storm_BindingSurvivesLoad) {
    CrowdHarness harness;
    harness.AddClients(32U);
    StormDigest digest;
    ASSERT_TRUE(RunStorm(harness, digest, 29U, 25));

    for (std::size_t i = 0U; i < 32U; ++i) {
        EXPECT_EQ(harness.relay.PeerRoom(harness.relayPeers[i]),
                  harness.lobby.ClientRoom(harness.facadeIds[i]));
    }

    bool created = false;
    harness.facades[0].CreateRoom(
        "probe", "p0", 8U, [&](LobbyRoomId, const std::string& error) {
            created = true;
            EXPECT_TRUE(error.empty());
        });
    harness.Settle(3600U);
    ASSERT_TRUE(created);
    const LobbyRoomId probe = harness.facades[0].CurrentRoom();
    ASSERT_NE(probe, kInvalidLobbyRoomId);

    bool joined = false;
    harness.facades[1].JoinRoom(
        probe, "p1", [&](std::vector<std::string>, const std::string& error) {
            joined = true;
            EXPECT_TRUE(error.empty());
        });
    harness.Settle(3700U);
    ASSERT_TRUE(joined);
    ASSERT_TRUE(harness.SyncRooms());

    ASSERT_TRUE(harness.relayClients[0].SendData(Bytes("probe")));
    harness.Settle(3800U);
    EXPECT_EQ(harness.relayClients[1].TryReceiveData(), Bytes("probe"));
    for (std::size_t i = 2U; i < 32U; ++i) {
        EXPECT_FALSE(harness.relayClients[i].TryReceiveData().has_value());
    }
}

} // namespace
} // namespace UVE::Network::Tests
