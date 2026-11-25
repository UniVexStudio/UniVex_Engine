// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_relay.h"

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

struct RelayInterestHarness final {
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

[[nodiscard]] RelayInterestHarness MakeRelayInterest(const std::size_t clientCount) {
    RelayInterestHarness harness;
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

TEST(NetworkRelayInterestTest, Interest_SetAndQueryGuards) {
    RelayInterestHarness harness = MakeRelayInterest(2U);
    ASSERT_TRUE(harness.Establish(1000U));
    const RelayPeerId a = harness.peerIds[0];
    const RelayPeerId b = harness.peerIds[1];
    EXPECT_TRUE(harness.relay.IsPeerInterested(a, b));
    EXPECT_TRUE(harness.relay.IsPeerInterested(b, a));
    EXPECT_TRUE(harness.relay.IsPeerInterested(a, 999U));
    EXPECT_TRUE(harness.relay.IsPeerInterested(999U, a));
    EXPECT_FALSE(harness.relay.SetPeerInterest(999U, a, false));
    EXPECT_FALSE(harness.relay.SetPeerInterest(a, 999U, false));
    EXPECT_TRUE(harness.relay.SetPeerInterest(b, a, false));
    EXPECT_FALSE(harness.relay.IsPeerInterested(b, a));
    EXPECT_TRUE(harness.relay.IsPeerInterested(a, b));
    EXPECT_TRUE(harness.relay.SetPeerInterest(b, a, true));
    EXPECT_TRUE(harness.relay.IsPeerInterested(b, a));
    EXPECT_TRUE(harness.relay.SetPeerInterest(a, a, false));
    EXPECT_FALSE(harness.relay.IsPeerInterested(a, a));
}

TEST(NetworkRelayInterestTest, Interest_MutesAreOneWay) {
    RelayInterestHarness harness = MakeRelayInterest(3U);
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(
        harness.relay.SetPeerInterest(harness.peerIds[1], harness.peerIds[0], false));

    ASSERT_TRUE(harness.clients[0].SendData(Bytes("a")));
    harness.Settle(1100U);
    EXPECT_FALSE(harness.clients[1].TryReceiveData().has_value());
    EXPECT_EQ(harness.clients[2].TryReceiveData(), Bytes("a"));

    ASSERT_TRUE(harness.clients[1].SendData(Bytes("b")));
    harness.Settle(1200U);
    EXPECT_EQ(harness.clients[0].TryReceiveData(), Bytes("b"));
    EXPECT_EQ(harness.clients[2].TryReceiveData(), Bytes("b"));

    const RelayStats stats = harness.relay.Stats();
    EXPECT_EQ(stats.payloadsReceived, 2U);
    EXPECT_EQ(stats.payloadsForwarded, 3U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayInterestTest, Interest_ComposesWithRooms) {
    RelayInterestHarness harness = MakeRelayInterest(4U);
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[0], 1U));
    ASSERT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[1], 1U));
    ASSERT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[2], 1U));
    ASSERT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[3], 2U));
    ASSERT_TRUE(
        harness.relay.SetPeerInterest(harness.peerIds[2], harness.peerIds[0], false));

    ASSERT_TRUE(harness.clients[0].SendData(Bytes("a1")));
    harness.Settle(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes("a1"));
    EXPECT_FALSE(harness.clients[2].TryReceiveData().has_value());
    EXPECT_FALSE(harness.clients[3].TryReceiveData().has_value());

    ASSERT_TRUE(harness.relay.SetPeerRoom(harness.peerIds[3], 1U));
    ASSERT_TRUE(harness.clients[0].SendData(Bytes("a2")));
    harness.Settle(1200U);
    EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes("a2"));
    EXPECT_FALSE(harness.clients[2].TryReceiveData().has_value());
    EXPECT_EQ(harness.clients[3].TryReceiveData(), Bytes("a2"));

    const RelayStats stats = harness.relay.Stats();
    EXPECT_EQ(stats.payloadsReceived, 2U);
    EXPECT_EQ(stats.payloadsForwarded, 3U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayInterestTest, Interest_RemovalPrunesBothDirections) {
    RelayInterestHarness harness = MakeRelayInterest(4U);
    ASSERT_TRUE(harness.Establish(1000U));
    const RelayPeerId a = harness.peerIds[0];
    const RelayPeerId b = harness.peerIds[1];
    const RelayPeerId c = harness.peerIds[2];
    const RelayPeerId d = harness.peerIds[3];
    ASSERT_TRUE(harness.relay.SetPeerInterest(b, a, false));
    ASSERT_TRUE(harness.relay.SetPeerInterest(c, a, false));
    ASSERT_TRUE(harness.relay.SetPeerInterest(d, b, false));

    EXPECT_TRUE(harness.relay.RemovePeer(a));
    EXPECT_TRUE(harness.relay.IsPeerInterested(b, a));
    EXPECT_TRUE(harness.relay.IsPeerInterested(c, a));
    EXPECT_FALSE(harness.relay.IsPeerInterested(d, b));

    ASSERT_TRUE(harness.clients[1].SendData(Bytes("b")));
    harness.Settle(1100U);
    EXPECT_EQ(harness.clients[2].TryReceiveData(), Bytes("b"));
    EXPECT_FALSE(harness.clients[3].TryReceiveData().has_value());

    const RelayStats stats = harness.relay.Stats();
    EXPECT_EQ(stats.payloadsReceived, 1U);
    EXPECT_EQ(stats.payloadsForwarded, 1U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayInterestTest, Interest_MuteUnmuteMidStream) {
    RelayInterestHarness harness = MakeRelayInterest(2U);
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.clients[0].SendData(Bytes("m1")));
    harness.Settle(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes("m1"));

    ASSERT_TRUE(
        harness.relay.SetPeerInterest(harness.peerIds[1], harness.peerIds[0], false));
    ASSERT_TRUE(harness.clients[0].SendData(Bytes("m2")));
    harness.Settle(1200U);
    EXPECT_FALSE(harness.clients[1].TryReceiveData().has_value());

    ASSERT_TRUE(
        harness.relay.SetPeerInterest(harness.peerIds[1], harness.peerIds[0], true));
    ASSERT_TRUE(harness.clients[0].SendData(Bytes("m3")));
    harness.Settle(1300U);
    EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes("m3"));
}

TEST(NetworkRelayInterestTest, Interest_TeamsMuteAcross) {
    RelayInterestHarness harness = MakeRelayInterest(4U);
    ASSERT_TRUE(harness.Establish(1000U));
    const RelayPeerId red[2] = {harness.peerIds[0], harness.peerIds[1]};
    const RelayPeerId blue[2] = {harness.peerIds[2], harness.peerIds[3]};
    for (const RelayPeerId r : red) {
        for (const RelayPeerId b : blue) {
            ASSERT_TRUE(harness.relay.SetPeerInterest(r, b, false));
            ASSERT_TRUE(harness.relay.SetPeerInterest(b, r, false));
        }
    }

    ASSERT_TRUE(harness.clients[0].SendData(Bytes("red")));
    harness.Settle(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes("red"));
    EXPECT_FALSE(harness.clients[2].TryReceiveData().has_value());
    EXPECT_FALSE(harness.clients[3].TryReceiveData().has_value());

    ASSERT_TRUE(harness.clients[2].SendData(Bytes("blue")));
    harness.Settle(1200U);
    EXPECT_FALSE(harness.clients[0].TryReceiveData().has_value());
    EXPECT_FALSE(harness.clients[1].TryReceiveData().has_value());
    EXPECT_EQ(harness.clients[3].TryReceiveData(), Bytes("blue"));

    const RelayStats stats = harness.relay.Stats();
    EXPECT_EQ(stats.payloadsReceived, 2U);
    EXPECT_EQ(stats.payloadsForwarded, 2U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayInterestTest, Binding_ProximityMutesFarPairs) {
    RelayInterestHarness harness = MakeRelayInterest(3U);
    ASSERT_TRUE(harness.Establish(1000U));
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
                EXPECT_TRUE(harness.relay.SetPeerInterest(
                    harness.peerIds[r], harness.peerIds[s], distance <= 10.0));
            }
        }
    };
    syncProximity();

    ASSERT_TRUE(harness.clients[0].SendData(Bytes("hi")));
    harness.Settle(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes("hi"));
    EXPECT_FALSE(harness.clients[2].TryReceiveData().has_value());

    ASSERT_TRUE(harness.clients[2].SendData(Bytes("far")));
    harness.Settle(1200U);
    EXPECT_FALSE(harness.clients[0].TryReceiveData().has_value());
    EXPECT_FALSE(harness.clients[1].TryReceiveData().has_value());

    positions[2] = 7.0;
    syncProximity();
    ASSERT_TRUE(harness.clients[2].SendData(Bytes("near")));
    harness.Settle(1300U);
    EXPECT_EQ(harness.clients[0].TryReceiveData(), Bytes("near"));
    EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes("near"));

    const RelayStats stats = harness.relay.Stats();
    EXPECT_EQ(stats.payloadsReceived, 3U);
    EXPECT_EQ(stats.payloadsForwarded, 3U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

TEST(NetworkRelayInterestTest, Interest_SelfMuteIsNeverConsulted) {
    RelayInterestHarness harness = MakeRelayInterest(2U);
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(
        harness.relay.SetPeerInterest(harness.peerIds[0], harness.peerIds[0], false));

    ASSERT_TRUE(harness.clients[0].SendData(Bytes("a")));
    harness.Settle(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes("a"));

    ASSERT_TRUE(harness.clients[1].SendData(Bytes("b")));
    harness.Settle(1200U);
    EXPECT_EQ(harness.clients[0].TryReceiveData(), Bytes("b"));

    const RelayStats stats = harness.relay.Stats();
    EXPECT_EQ(stats.payloadsReceived, 2U);
    EXPECT_EQ(stats.payloadsForwarded, 2U);
    EXPECT_EQ(stats.forwardsDropped, 0U);
}

} // namespace
} // namespace UVE::Network::Tests
