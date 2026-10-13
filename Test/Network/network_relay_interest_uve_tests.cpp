// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_relay_uve.h"

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

struct RelayInterestHarnessUVE final {
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

[[nodiscard]] RelayInterestHarnessUVE MakeRelayInterestUVE(const std::size_t clientCount) {
    RelayInterestHarnessUVE harness;
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

TEST(NetworkRelayInterestUVETest, Interest_SetAndQueryGuards) {
    RelayInterestHarnessUVE harness = MakeRelayInterestUVE(2U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    const RelayPeerIdUVE a = harness.peerIds[0];
    const RelayPeerIdUVE b = harness.peerIds[1];
    EXPECT_TRUE(harness.relay.IsPeerInterestedUVE(a, b));
    EXPECT_TRUE(harness.relay.IsPeerInterestedUVE(b, a));
    EXPECT_TRUE(harness.relay.IsPeerInterestedUVE(a, 999U));
    EXPECT_TRUE(harness.relay.IsPeerInterestedUVE(999U, a));
    EXPECT_FALSE(harness.relay.SetPeerInterestUVE(999U, a, false));
    EXPECT_FALSE(harness.relay.SetPeerInterestUVE(a, 999U, false));
    EXPECT_TRUE(harness.relay.SetPeerInterestUVE(b, a, false));
    EXPECT_FALSE(harness.relay.IsPeerInterestedUVE(b, a));
    EXPECT_TRUE(harness.relay.IsPeerInterestedUVE(a, b));
    EXPECT_TRUE(harness.relay.SetPeerInterestUVE(b, a, true));
    EXPECT_TRUE(harness.relay.IsPeerInterestedUVE(b, a));
    EXPECT_TRUE(harness.relay.SetPeerInterestUVE(a, a, false));
    EXPECT_FALSE(harness.relay.IsPeerInterestedUVE(a, a));
}

TEST(NetworkRelayInterestUVETest, Interest_MutesAreOneWay) {
    RelayInterestHarnessUVE harness = MakeRelayInterestUVE(3U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(
        harness.relay.SetPeerInterestUVE(harness.peerIds[1], harness.peerIds[0], false));

    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("a")));
    harness.SettleUVE(1100U);
    EXPECT_FALSE(harness.clients[1].TryReceiveDataUVE().has_value());
    EXPECT_EQ(harness.clients[2].TryReceiveDataUVE(), BytesUVE("a"));

    ASSERT_TRUE(harness.clients[1].SendDataUVE(BytesUVE("b")));
    harness.SettleUVE(1200U);
    EXPECT_EQ(harness.clients[0].TryReceiveDataUVE(), BytesUVE("b"));
    EXPECT_EQ(harness.clients[2].TryReceiveDataUVE(), BytesUVE("b"));

    const RelayStatsUVE stats = harness.relay.StatsUVE();
    EXPECT_EQ(stats.payloadsReceived, 2U);
    EXPECT_EQ(stats.payloadsForwarded, 3U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayInterestUVETest, Interest_ComposesWithRooms) {
    RelayInterestHarnessUVE harness = MakeRelayInterestUVE(4U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[0], 1U));
    ASSERT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[1], 1U));
    ASSERT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[2], 1U));
    ASSERT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[3], 2U));
    ASSERT_TRUE(
        harness.relay.SetPeerInterestUVE(harness.peerIds[2], harness.peerIds[0], false));

    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("a1")));
    harness.SettleUVE(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE("a1"));
    EXPECT_FALSE(harness.clients[2].TryReceiveDataUVE().has_value());
    EXPECT_FALSE(harness.clients[3].TryReceiveDataUVE().has_value());

    ASSERT_TRUE(harness.relay.SetPeerRoomUVE(harness.peerIds[3], 1U));
    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("a2")));
    harness.SettleUVE(1200U);
    EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE("a2"));
    EXPECT_FALSE(harness.clients[2].TryReceiveDataUVE().has_value());
    EXPECT_EQ(harness.clients[3].TryReceiveDataUVE(), BytesUVE("a2"));

    const RelayStatsUVE stats = harness.relay.StatsUVE();
    EXPECT_EQ(stats.payloadsReceived, 2U);
    EXPECT_EQ(stats.payloadsForwarded, 3U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayInterestUVETest, Interest_RemovalPrunesBothDirections) {
    RelayInterestHarnessUVE harness = MakeRelayInterestUVE(4U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    const RelayPeerIdUVE a = harness.peerIds[0];
    const RelayPeerIdUVE b = harness.peerIds[1];
    const RelayPeerIdUVE c = harness.peerIds[2];
    const RelayPeerIdUVE d = harness.peerIds[3];
    ASSERT_TRUE(harness.relay.SetPeerInterestUVE(b, a, false));
    ASSERT_TRUE(harness.relay.SetPeerInterestUVE(c, a, false));
    ASSERT_TRUE(harness.relay.SetPeerInterestUVE(d, b, false));

    EXPECT_TRUE(harness.relay.RemovePeerUVE(a));
    EXPECT_TRUE(harness.relay.IsPeerInterestedUVE(b, a));
    EXPECT_TRUE(harness.relay.IsPeerInterestedUVE(c, a));
    EXPECT_FALSE(harness.relay.IsPeerInterestedUVE(d, b));

    ASSERT_TRUE(harness.clients[1].SendDataUVE(BytesUVE("b")));
    harness.SettleUVE(1100U);
    EXPECT_EQ(harness.clients[2].TryReceiveDataUVE(), BytesUVE("b"));
    EXPECT_FALSE(harness.clients[3].TryReceiveDataUVE().has_value());

    const RelayStatsUVE stats = harness.relay.StatsUVE();
    EXPECT_EQ(stats.payloadsReceived, 1U);
    EXPECT_EQ(stats.payloadsForwarded, 1U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayInterestUVETest, Interest_MuteUnmuteMidStream) {
    RelayInterestHarnessUVE harness = MakeRelayInterestUVE(2U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("m1")));
    harness.SettleUVE(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE("m1"));

    ASSERT_TRUE(
        harness.relay.SetPeerInterestUVE(harness.peerIds[1], harness.peerIds[0], false));
    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("m2")));
    harness.SettleUVE(1200U);
    EXPECT_FALSE(harness.clients[1].TryReceiveDataUVE().has_value());

    ASSERT_TRUE(
        harness.relay.SetPeerInterestUVE(harness.peerIds[1], harness.peerIds[0], true));
    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("m3")));
    harness.SettleUVE(1300U);
    EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE("m3"));
}

TEST(NetworkRelayInterestUVETest, Interest_TeamsMuteAcross) {
    RelayInterestHarnessUVE harness = MakeRelayInterestUVE(4U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    const RelayPeerIdUVE red[2] = {harness.peerIds[0], harness.peerIds[1]};
    const RelayPeerIdUVE blue[2] = {harness.peerIds[2], harness.peerIds[3]};
    for (const RelayPeerIdUVE r : red) {
        for (const RelayPeerIdUVE b : blue) {
            ASSERT_TRUE(harness.relay.SetPeerInterestUVE(r, b, false));
            ASSERT_TRUE(harness.relay.SetPeerInterestUVE(b, r, false));
        }
    }

    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("red")));
    harness.SettleUVE(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE("red"));
    EXPECT_FALSE(harness.clients[2].TryReceiveDataUVE().has_value());
    EXPECT_FALSE(harness.clients[3].TryReceiveDataUVE().has_value());

    ASSERT_TRUE(harness.clients[2].SendDataUVE(BytesUVE("blue")));
    harness.SettleUVE(1200U);
    EXPECT_FALSE(harness.clients[0].TryReceiveDataUVE().has_value());
    EXPECT_FALSE(harness.clients[1].TryReceiveDataUVE().has_value());
    EXPECT_EQ(harness.clients[3].TryReceiveDataUVE(), BytesUVE("blue"));

    const RelayStatsUVE stats = harness.relay.StatsUVE();
    EXPECT_EQ(stats.payloadsReceived, 2U);
    EXPECT_EQ(stats.payloadsForwarded, 2U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayInterestUVETest, Binding_ProximityMutesFarPairs) {
    RelayInterestHarnessUVE harness = MakeRelayInterestUVE(3U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    double positions[3] = {0.0, 5.0, 50.0};
    auto syncProximity = [&]() {
        for (std::size_t r = 0U; r < 3U; ++r) {
            for (std::size_t s = 0U; s < 3U; ++s) {
                if (r == s) {
                    continue;
                }
                double distance = positions[r] - positions[s];
                if (distance < 0.0) {
                    distance = -distance;
                }
                EXPECT_TRUE(harness.relay.SetPeerInterestUVE(
                    harness.peerIds[r], harness.peerIds[s], distance <= 10.0));
            }
        }
    };
    syncProximity();

    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("hi")));
    harness.SettleUVE(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE("hi"));
    EXPECT_FALSE(harness.clients[2].TryReceiveDataUVE().has_value());

    ASSERT_TRUE(harness.clients[2].SendDataUVE(BytesUVE("far")));
    harness.SettleUVE(1200U);
    EXPECT_FALSE(harness.clients[0].TryReceiveDataUVE().has_value());
    EXPECT_FALSE(harness.clients[1].TryReceiveDataUVE().has_value());

    positions[2] = 7.0;
    syncProximity();
    ASSERT_TRUE(harness.clients[2].SendDataUVE(BytesUVE("near")));
    harness.SettleUVE(1300U);
    EXPECT_EQ(harness.clients[0].TryReceiveDataUVE(), BytesUVE("near"));
    EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE("near"));

    const RelayStatsUVE stats = harness.relay.StatsUVE();
    EXPECT_EQ(stats.payloadsReceived, 3U);
    EXPECT_EQ(stats.payloadsForwarded, 3U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayInterestUVETest, Interest_SelfMuteIsNeverConsulted) {
    RelayInterestHarnessUVE harness = MakeRelayInterestUVE(2U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(
        harness.relay.SetPeerInterestUVE(harness.peerIds[0], harness.peerIds[0], false));

    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("a")));
    harness.SettleUVE(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE("a"));

    ASSERT_TRUE(harness.clients[1].SendDataUVE(BytesUVE("b")));
    harness.SettleUVE(1200U);
    EXPECT_EQ(harness.clients[0].TryReceiveDataUVE(), BytesUVE("b"));

    const RelayStatsUVE stats = harness.relay.StatsUVE();
    EXPECT_EQ(stats.payloadsReceived, 2U);
    EXPECT_EQ(stats.payloadsForwarded, 2U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

} // namespace
} // namespace UVE::Network::Tests
