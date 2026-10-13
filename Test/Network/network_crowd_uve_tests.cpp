// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// The crowd soak: N clients storming the lobby and the relay under a seeded script,
// proving the spine holds together at scale. Determinism (same seed, same stats),
// conservation (every send arrives once, every room is born empty-removed or lives),
// no stranded callbacks, and exact mesh/avalanche/churn arithmetic.

#include "uve/network/network_lobby_uve.h"
#include "uve/network/network_relay_uve.h"
#include "uve/network/network_session_uve.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

[[nodiscard]] std::vector<std::uint8_t> BytesUVE(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

struct CrowdHarnessUVE final {
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

    void AddClientsUVE(const std::size_t count) {
        for (std::size_t i = 0U; i < count; ++i) {
            AddClientUVE();
        }
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
        return true;
    }

    [[nodiscard]] bool SyncRoomsUVE() {
        for (std::size_t i = 0U; i < facades.size(); ++i) {
            if (!relay.SetPeerRoomUVE(relayPeers[i], lobby.ClientRoomUVE(facadeIds[i]))) {
                return false;
            }
        }
        return true;
    }
};

struct StormDigestUVE final {
    LobbyStatsUVE lobby;
    RelayStatsUVE relay;
    std::size_t rooms = 0U;
    std::size_t peers = 0U;
    std::vector<LobbyRoomIdUVE> serverRooms;
    std::vector<LobbyRoomIdUVE> cachedRooms;
    std::uint64_t initiated = 0U;
    std::uint64_t fired = 0U;
    std::uint64_t sent = 0U;
    std::uint64_t drained = 0U;

    [[nodiscard]] bool operator==(const StormDigestUVE&) const noexcept = default;
};

// One storm: every client runs one seeded op per tick (relay sends, lobby calls, interest
// mutes), then the tick settles and the app re-syncs relay rooms from the lobby.
[[nodiscard]] bool RunStormUVE(CrowdHarnessUVE& harness, StormDigestUVE& digest,
                               const unsigned seed, const int ticks) {
    if (!harness.EstablishUVE(1000U)) {
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
                if (harness.relayClients[i].SendDataUVE(BytesUVE(text))) {
                    ++sent;
                }
            } else if (op == 3) {
                ++initiated;
                harness.facades[i].CreateRoomUVE(
                    "r" + std::to_string(tick) + "c" + std::to_string(i), me, 8U,
                    [&](LobbyRoomIdUVE, const std::string&) { ++fired; });
            } else if (op == 4) {
                const auto room = static_cast<LobbyRoomIdUVE>(rng() % 12U + 1U);
                ++initiated;
                harness.facades[i].JoinRoomUVE(
                    room, me,
                    [&](std::vector<std::string>, const std::string&) { ++fired; });
            } else if (op == 5) {
                ++initiated;
                harness.facades[i].LeaveRoomUVE(
                    [&](bool, const std::string&) { ++fired; });
            } else if (op == 6) {
                ++initiated;
                harness.facades[i].ListRoomsUVE(
                    [&](std::vector<LobbyRoomInfoUVE>, const std::string&) { ++fired; });
            } else if (op == 7) {
                const auto room = static_cast<LobbyRoomIdUVE>(rng() % 12U + 1U);
                ++initiated;
                harness.facades[i].QueryRosterUVE(
                    room, [&](std::vector<std::string>, const std::string&) { ++fired; });
            } else if (op == 8) {
                const std::size_t a = rng() % clients;
                std::size_t b = rng() % clients;
                if (b == a) {
                    b = (b + 1U) % clients;
                }
                // Ids are valid by construction; the sync loop below is the asserted path.
                static_cast<void>(harness.relay.SetPeerInterestUVE(
                    harness.relayPeers[a], harness.relayPeers[b], (rng() % 2U) == 0U));
            }
        }
        harness.SettleUVE(nowMs);
        nowMs += 100U;
        static_cast<void>(harness.SyncRoomsUVE());
    }
    std::uint64_t drained = 0U;
    for (std::size_t i = 0U; i < clients; ++i) {
        while (harness.relayClients[i].TryReceiveDataUVE().has_value()) {
            ++drained;
        }
    }
    digest.lobby = harness.lobby.StatsUVE();
    digest.relay = harness.relay.StatsUVE();
    digest.rooms = harness.lobby.RoomCountUVE();
    digest.peers = harness.lobby.PeerCountUVE();
    for (std::size_t i = 0U; i < clients; ++i) {
        digest.serverRooms.push_back(harness.lobby.ClientRoomUVE(harness.facadeIds[i]));
        digest.cachedRooms.push_back(harness.facades[i].CurrentRoomUVE());
    }
    digest.initiated = initiated;
    digest.fired = fired;
    digest.sent = sent;
    digest.drained = drained;
    return true;
}

TEST(NetworkCrowdUVETest, Storm_DeterministicTwice) {
    CrowdHarnessUVE first;
    first.AddClientsUVE(32U);
    StormDigestUVE firstDigest;
    ASSERT_TRUE(RunStormUVE(first, firstDigest, 7U, 40));

    CrowdHarnessUVE second;
    second.AddClientsUVE(32U);
    StormDigestUVE secondDigest;
    ASSERT_TRUE(RunStormUVE(second, secondDigest, 7U, 40));

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

TEST(NetworkCrowdUVETest, Storm_InvariantsHold) {
    CrowdHarnessUVE harness;
    harness.AddClientsUVE(32U);
    StormDigestUVE digest;
    ASSERT_TRUE(RunStormUVE(harness, digest, 11U, 40));

    for (std::size_t i = 0U; i < 32U; ++i) {
        EXPECT_EQ(harness.facades[i].SessionUVE().StateUVE(),
                  SessionStateUVE::Established);
        EXPECT_EQ(harness.relayClients[i].StateUVE(), SessionStateUVE::Established);
        EXPECT_EQ(harness.facades[i].CurrentRoomUVE(),
                  harness.lobby.ClientRoomUVE(harness.facadeIds[i]));
    }

    bool listed = false;
    std::vector<LobbyRoomInfoUVE> rooms;
    harness.facades[0].ListRoomsUVE(
        [&](std::vector<LobbyRoomInfoUVE> found, const std::string& error) {
            listed = true;
            rooms = std::move(found);
            EXPECT_TRUE(error.empty());
        });
    harness.SettleUVE(5100U);
    ASSERT_TRUE(listed);
    EXPECT_EQ(rooms.size(), harness.lobby.RoomCountUVE());
    std::uint32_t seated = 0U;
    for (const LobbyRoomInfoUVE& room : rooms) {
        EXPECT_GT(room.players, 0U);
        seated += room.players;
    }
    std::uint32_t expected = 0U;
    for (const LobbyRoomIdUVE room : digest.serverRooms) {
        if (room != kInvalidLobbyRoomIdUVE) {
            ++expected;
        }
    }
    EXPECT_EQ(seated, expected);

    const RelayStatsUVE before = harness.relay.StatsUVE();
    const LobbyStatsUVE lobbyBefore = harness.lobby.StatsUVE();
    harness.SettleUVE(5200U);
    harness.SettleUVE(5300U);
    EXPECT_EQ(harness.relay.StatsUVE(), before);
    EXPECT_EQ(harness.lobby.StatsUVE(), lobbyBefore);
}

TEST(NetworkCrowdUVETest, Storm_Conservation) {
    CrowdHarnessUVE harness;
    harness.AddClientsUVE(32U);
    StormDigestUVE digest;
    ASSERT_TRUE(RunStormUVE(harness, digest, 13U, 40));

    EXPECT_EQ(digest.relay.payloadsReceived, digest.sent);
    EXPECT_EQ(digest.drained, digest.relay.payloadsForwarded);
    EXPECT_LE(digest.relay.payloadsForwarded, digest.relay.payloadsReceived * 31U);
    EXPECT_EQ(digest.relay.forwardsDropped, 0U);
    EXPECT_EQ(digest.lobby.roomsCreated,
              digest.lobby.roomsRemoved + harness.lobby.RoomCountUVE());
}

TEST(NetworkCrowdUVETest, Storm_NoStrandedCallbacks) {
    CrowdHarnessUVE harness;
    harness.AddClientsUVE(24U);
    StormDigestUVE digest;
    ASSERT_TRUE(RunStormUVE(harness, digest, 21U, 30));
    EXPECT_GT(digest.initiated, 0U);
    EXPECT_EQ(digest.fired, digest.initiated);
}

TEST(NetworkCrowdUVETest, Storm_SingleRoomJoinAvalanche) {
    CrowdHarnessUVE harness;
    harness.AddClientsUVE(32U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    bool created = false;
    harness.facades[0].CreateRoomUVE(
        "mega", "p0", 64U, [&](LobbyRoomIdUVE id, const std::string& error) {
            created = true;
            EXPECT_EQ(id, 1U);
            EXPECT_TRUE(error.empty());
        });
    harness.SettleUVE(1100U);
    ASSERT_TRUE(created);

    std::uint32_t joined = 0U;
    for (std::size_t i = 1U; i < 32U; ++i) {
        harness.facades[i].JoinRoomUVE(
            1U, "p" + std::to_string(i),
            [&](std::vector<std::string>, const std::string& error) {
                ++joined;
                EXPECT_TRUE(error.empty());
            });
    }
    std::uint64_t nowMs = 1200U;
    for (int round = 0; round < 15; ++round) {
        harness.SettleUVE(nowMs);
        nowMs += 100U;
    }
    EXPECT_EQ(joined, 31U);

    std::vector<std::string> full;
    for (std::size_t i = 0U; i < 32U; ++i) {
        full.push_back("p" + std::to_string(i));
    }
    for (std::size_t i = 0U; i < 32U; ++i) {
        EXPECT_EQ(harness.facades[i].CachedRosterUVE(), full);
    }
    const LobbyStatsUVE stats = harness.lobby.StatsUVE();
    EXPECT_EQ(stats.joinsAccepted, 31U);
    EXPECT_EQ(stats.joinsRejected, 0U);
    EXPECT_EQ(stats.notificationsSent, 496U);
    EXPECT_EQ(stats.notificationsDropped, 0U);
}

TEST(NetworkCrowdUVETest, Storm_RelayMeshFlood) {
    CrowdHarnessUVE harness;
    harness.AddClientsUVE(16U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    std::uint64_t nowMs = 1100U;
    for (int round = 0; round < 10; ++round) {
        for (std::size_t i = 0U; i < 16U; ++i) {
            ASSERT_TRUE(harness.relayClients[i].SendDataUVE(
                BytesUVE("r" + std::to_string(round) + "c" + std::to_string(i))));
        }
        harness.SettleUVE(nowMs);
        nowMs += 100U;
    }
    for (std::size_t i = 0U; i < 16U; ++i) {
        std::uint32_t inbox = 0U;
        while (harness.relayClients[i].TryReceiveDataUVE().has_value()) {
            ++inbox;
        }
        EXPECT_EQ(inbox, 150U);
    }
    const RelayStatsUVE stats = harness.relay.StatsUVE();
    EXPECT_EQ(stats.payloadsReceived, 160U);
    EXPECT_EQ(stats.payloadsForwarded, 2400U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkCrowdUVETest, Storm_ChurnRooms) {
    CrowdHarnessUVE harness;
    harness.AddClientsUVE(12U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    std::uint64_t nowMs = 1100U;
    for (int round = 0; round < 5; ++round) {
        std::vector<LobbyRoomIdUVE> ids(12U, kInvalidLobbyRoomIdUVE);
        std::uint32_t created = 0U;
        for (std::size_t i = 0U; i < 12U; ++i) {
            harness.facades[i].CreateRoomUVE(
                "c" + std::to_string(i) + "r" + std::to_string(round),
                "p" + std::to_string(i), 8U,
                [&, i](LobbyRoomIdUVE id, const std::string& error) {
                    ++created;
                    EXPECT_TRUE(error.empty());
                    ids[i] = id;
                });
        }
        harness.SettleUVE(nowMs);
        nowMs += 100U;
        ASSERT_EQ(created, 12U);

        std::uint32_t joined = 0U;
        std::vector<std::pair<std::size_t, std::string>> joinFailures;
        for (std::size_t i = 0U; i < 12U; ++i) {
            ASSERT_NE(ids[i], kInvalidLobbyRoomIdUVE);
            harness.facades[i].JoinRoomUVE(
                ids[(i + 1U) % 12U], "p" + std::to_string(i),
                [&, i](std::vector<std::string>, const std::string& error) {
                    if (error.empty()) {
                        ++joined;
                    } else {
                        joinFailures.emplace_back(i, error);
                    }
                });
        }
        harness.SettleUVE(nowMs);
        nowMs += 100U;
        // Joins process in peer-id order: client 0 leaves R0 first, so client 11 always
        // arrives at a removed room. One race rejection per round, deterministically.
        ASSERT_EQ(joined, 11U);
        ASSERT_EQ(joinFailures.size(), 1U);
        EXPECT_EQ(joinFailures[0].first, 11U);
        EXPECT_EQ(joinFailures[0].second.rfind("unknown room", 0U), 0U);

        std::uint32_t left = 0U;
        for (std::size_t i = 0U; i < 12U; ++i) {
            harness.facades[i].LeaveRoomUVE([&](bool wasLeft, const std::string& error) {
                ++left;
                EXPECT_TRUE(wasLeft);
                EXPECT_TRUE(error.empty());
            });
        }
        harness.SettleUVE(nowMs);
        nowMs += 100U;
        ASSERT_EQ(left, 12U);
    }

    const LobbyStatsUVE stats = harness.lobby.StatsUVE();
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
    EXPECT_EQ(harness.lobby.RoomCountUVE(), 0U);
}

TEST(NetworkCrowdUVETest, Storm_BindingSurvivesLoad) {
    CrowdHarnessUVE harness;
    harness.AddClientsUVE(32U);
    StormDigestUVE digest;
    ASSERT_TRUE(RunStormUVE(harness, digest, 29U, 25));

    for (std::size_t i = 0U; i < 32U; ++i) {
        EXPECT_EQ(harness.relay.PeerRoomUVE(harness.relayPeers[i]),
                  harness.lobby.ClientRoomUVE(harness.facadeIds[i]));
    }

    bool created = false;
    harness.facades[0].CreateRoomUVE(
        "probe", "p0", 8U, [&](LobbyRoomIdUVE, const std::string& error) {
            created = true;
            EXPECT_TRUE(error.empty());
        });
    harness.SettleUVE(3600U);
    ASSERT_TRUE(created);
    const LobbyRoomIdUVE probe = harness.facades[0].CurrentRoomUVE();
    ASSERT_NE(probe, kInvalidLobbyRoomIdUVE);

    bool joined = false;
    harness.facades[1].JoinRoomUVE(
        probe, "p1", [&](std::vector<std::string>, const std::string& error) {
            joined = true;
            EXPECT_TRUE(error.empty());
        });
    harness.SettleUVE(3700U);
    ASSERT_TRUE(joined);
    ASSERT_TRUE(harness.SyncRoomsUVE());

    ASSERT_TRUE(harness.relayClients[0].SendDataUVE(BytesUVE("probe")));
    harness.SettleUVE(3800U);
    EXPECT_EQ(harness.relayClients[1].TryReceiveDataUVE(), BytesUVE("probe"));
    for (std::size_t i = 2U; i < 32U; ++i) {
        EXPECT_FALSE(harness.relayClients[i].TryReceiveDataUVE().has_value());
    }
}

} // namespace
} // namespace UVE::Network::Tests
