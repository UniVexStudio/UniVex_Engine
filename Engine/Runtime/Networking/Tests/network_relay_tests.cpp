// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_relay.h"

#include "network_replication.h"
#include "network_rpc.h"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

[[nodiscard]] std::vector<std::uint8_t> Bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

struct RelayHarness final {
    std::vector<LoopbackTransportPair> links;
    std::vector<NetworkSession> clients;
    NetworkRelay relay;
    std::vector<RelayPeerId> peerIds;

    void UpdateClients(const std::uint64_t nowMs) {
        for (auto& client : clients) {
            client.Update(nowMs);
        }
    }

    void UpdateAll(const std::uint64_t nowMs) {
        relay.Update(nowMs);
        UpdateClients(nowMs);
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

[[nodiscard]] RelayHarness MakeRelay(const std::size_t clientCount,
                                           const bool multiplexed = false) {
    RelayHarness harness;
    for (std::size_t i = 0U; i < clientCount; ++i) {
        LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
        NetworkSession client(SessionRole::Client,
                                 std::make_unique<LoopbackEndpoint>(pair.a));
        NetworkSession server(SessionRole::Server,
                                 std::make_unique<LoopbackEndpoint>(pair.b));
        if (multiplexed) {
            static_cast<void>(client.EnableMultiplexing());
            static_cast<void>(server.EnableMultiplexing());
        }
        harness.peerIds.push_back(harness.relay.AddPeer(std::move(server)));
        harness.clients.push_back(std::move(client));
        harness.links.push_back(std::move(pair));
    }
    return harness;
}

TEST(NetworkRelayTest, Peers_AddRemoveGuards) {
    RelayHarness harness = MakeRelay(2U);
    EXPECT_NE(harness.peerIds[0], kInvalidRelayPeerId);
    EXPECT_NE(harness.peerIds[1], kInvalidRelayPeerId);
    EXPECT_NE(harness.peerIds[0], harness.peerIds[1]);
    EXPECT_NE(harness.relay.PeerSession(harness.peerIds[0]), nullptr);
    EXPECT_EQ(harness.relay.PeerSession(999U), nullptr);
    EXPECT_EQ(harness.relay.PeerCount(), 2U);

    EXPECT_FALSE(harness.relay.RemovePeer(999U));
    EXPECT_TRUE(harness.relay.RemovePeer(harness.peerIds[0]));
    EXPECT_FALSE(harness.relay.RemovePeer(harness.peerIds[0]));
    EXPECT_EQ(harness.relay.PeerSession(harness.peerIds[0]), nullptr);
    EXPECT_EQ(harness.relay.PeerCount(), 1U);
}

TEST(NetworkRelayTest, Handshakes_CompleteThroughRelayPumps) {
    RelayHarness harness = MakeRelay(3U);
    ASSERT_TRUE(harness.Establish(1000U));
    for (const RelayPeerId id : harness.peerIds) {
        EXPECT_EQ(harness.relay.PeerSession(id)->State(),
                  SessionState::Established);
    }
}

TEST(NetworkRelayTest, Broadcast_FansOutWithoutEcho) {
    RelayHarness harness = MakeRelay(3U);
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.clients[0].SendData(Bytes("a")));
    harness.UpdateAll(1100U);

    EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes("a"));
    EXPECT_EQ(harness.clients[2].TryReceiveData(), Bytes("a"));
    EXPECT_FALSE(harness.clients[0].TryReceiveData().has_value());
    EXPECT_EQ(harness.relay.Stats().payloadsReceived, 1U);
    EXPECT_EQ(harness.relay.Stats().payloadsForwarded, 2U);
    EXPECT_EQ(harness.relay.Stats().forwardsDropped, 0U);
}

TEST(NetworkRelayTest, Forwards_SkipNonEstablished) {
    RelayHarness harness = MakeRelay(3U);
    ASSERT_TRUE(harness.clients[0].Connect(1000U));
    ASSERT_TRUE(harness.clients[1].Connect(1000U));
    harness.UpdateAll(1000U);
    harness.UpdateAll(1000U);
    ASSERT_EQ(harness.clients[2].State(), SessionState::Disconnected);
    ASSERT_EQ(harness.relay.PeerSession(harness.peerIds[2])->State(),
              SessionState::Disconnected);

    ASSERT_TRUE(harness.clients[0].SendData(Bytes("x")));
    harness.UpdateAll(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveData(), Bytes("x"));
    EXPECT_FALSE(
        harness.relay.PeerSession(harness.peerIds[2])->TryReceiveData().has_value());
    EXPECT_EQ(harness.relay.Stats().payloadsReceived, 1U);
    EXPECT_EQ(harness.relay.Stats().payloadsForwarded, 1U);
    EXPECT_EQ(harness.relay.Stats().forwardsDropped, 0U);

    ASSERT_TRUE(harness.clients[2].Connect(1100U));
    harness.UpdateAll(1100U);
    harness.UpdateAll(1100U);
    ASSERT_EQ(harness.relay.PeerSession(harness.peerIds[2])->State(),
              SessionState::Established);
    ASSERT_TRUE(harness.clients[0].SendData(Bytes("y")));
    harness.UpdateAll(1200U);
    EXPECT_EQ(harness.clients[2].TryReceiveData(), Bytes("y"));
}

TEST(NetworkRelayTest, Muxed_ChannelsRideThroughUntouched) {
    RelayHarness harness = MakeRelay(3U, true);
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.clients[0].SendChannelData(5U, Bytes("c5")));
    harness.UpdateAll(1100U);

    EXPECT_EQ(harness.clients[1].TryReceiveChannelData(5U), Bytes("c5"));
    EXPECT_EQ(harness.clients[2].TryReceiveChannelData(5U), Bytes("c5"));
    EXPECT_FALSE(harness.clients[1].TryReceiveChannelData(6U).has_value());
    EXPECT_FALSE(harness.clients[0].TryReceiveChannelData(5U).has_value());
    EXPECT_EQ(harness.relay.Stats().payloadsReceived, 1U);
    EXPECT_EQ(harness.relay.Stats().payloadsForwarded, 2U);
}

TEST(NetworkRelayTest, FullQueue_DropsCounted) {
    RelayHarness harness = MakeRelay(3U);
    ASSERT_TRUE(harness.Establish(1000U));
    int queuedA = 0;
    int queuedC = 0;
    for (int i = 0; i < 256; ++i) {
        queuedA += harness.clients[0].SendData(Bytes("f")) ? 1 : 0;
        queuedC += harness.clients[2].SendData(Bytes("g")) ? 1 : 0;
    }
    ASSERT_EQ(queuedA, 256);
    ASSERT_EQ(queuedC, 256);

    // One relay tick, nobody else pumps: A's 256 fill B's and C's queues, then C's 256 find
    // B's queue full - 256 drops, counted, everything else delivered.
    harness.relay.Update(1100U);
    EXPECT_EQ(harness.relay.Stats().payloadsReceived, 512U);
    EXPECT_EQ(harness.relay.Stats().payloadsForwarded, 768U);
    EXPECT_EQ(harness.relay.Stats().forwardsDropped, 256U);
}

TEST(NetworkRelayTest, Remove_StopsTraffic) {
    RelayHarness harness = MakeRelay(3U);
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.relay.RemovePeer(harness.peerIds[1]));
    ASSERT_TRUE(harness.clients[0].SendData(Bytes("a")));
    harness.UpdateAll(1100U);

    EXPECT_EQ(harness.clients[2].TryReceiveData(), Bytes("a"));
    EXPECT_EQ(harness.relay.PeerCount(), 2U);
    EXPECT_EQ(harness.relay.Stats().payloadsReceived, 1U);
    EXPECT_EQ(harness.relay.Stats().payloadsForwarded, 1U);
}

TEST(NetworkRelayTest, Replication_ConvergesOverRelay) {
    RelayHarness harness = MakeRelay(3U);
    ASSERT_TRUE(harness.Establish(1000U));
    NetworkReplicator replA(std::move(harness.clients[0]));
    NetworkReplicator replB(std::move(harness.clients[1]));
    NetworkReplicator replC(std::move(harness.clients[2]));
    std::map<NetworkEntityId, Math::Vector3> positionsA;
    std::map<NetworkEntityId, Math::Vector3> positionsB;
    std::map<NetworkEntityId, Math::Vector3> positionsC;
    ASSERT_TRUE(replA.RegisterEntity(
        7U, [&positionsA]() { return positionsA[7U]; },
        [&positionsA](const Math::Vector3& position) { positionsA[7U] = position; },
        ReplicationAuthority::Owner));
    ASSERT_TRUE(replB.RegisterEntity(
        7U, [&positionsB]() { return positionsB[7U]; },
        [&positionsB](const Math::Vector3& position) { positionsB[7U] = position; },
        ReplicationAuthority::Proxy));
    ASSERT_TRUE(replC.RegisterEntity(
        7U, [&positionsC]() { return positionsC[7U]; },
        [&positionsC](const Math::Vector3& position) { positionsC[7U] = position; },
        ReplicationAuthority::Proxy));
    positionsA[7U] = Math::Vector3{5.0F, 5.0F, 5.0F};

    replA.Update(1100U);
    harness.relay.Update(1100U);
    replB.Update(1100U);
    replC.Update(1100U);
    EXPECT_EQ(positionsB[7U], (Math::Vector3{5.0F, 5.0F, 5.0F}));
    EXPECT_EQ(positionsC[7U], (Math::Vector3{5.0F, 5.0F, 5.0F}));
    replA.Update(1200U);
    harness.relay.Update(1200U);
    replB.Update(1200U);
    replC.Update(1200U);
    harness.relay.Update(1300U);
    replA.Update(1300U);

    EXPECT_EQ(replA.Stats().snapshotsSent, 3U);
    EXPECT_EQ(replA.Stats().snapshotsReceived, 4U); // Both rounds of B/C empties.
    EXPECT_EQ(replA.Stats().messagesIgnoredMalformed, 0U);
    EXPECT_EQ(replB.Stats().messagesIgnoredMalformed, 0U);
    EXPECT_EQ(replC.Stats().messagesIgnoredMalformed, 0U);
    EXPECT_EQ(harness.relay.Stats().payloadsReceived, 6U);
    EXPECT_EQ(harness.relay.Stats().payloadsForwarded, 12U);
    EXPECT_EQ(harness.relay.Stats().forwardsDropped, 0U);
}

TEST(NetworkRelayTest, Rpc_BroadcastIsFirstAnswerWins) {
    RelayHarness harness = MakeRelay(3U);
    ASSERT_TRUE(harness.Establish(1000U));
    NetworkRpc rpcA(std::move(harness.clients[0]));
    NetworkRpc rpcB(std::move(harness.clients[1]));
    NetworkRpc rpcC(std::move(harness.clients[2]));
    ASSERT_TRUE(rpcB.RegisterMethod(
        "add", [](const std::span<const Blueprint::Value> args) {
            return RpcOk(Blueprint::Value{std::get<std::int64_t>(args[0]) +
                                               std::get<std::int64_t>(args[1])});
        }));
    std::map<RpcCallId, RpcOutcome> doneA;
    const RpcCallId callId = rpcA.Call(
        "add", {Blueprint::Value{std::int64_t{1}}, Blueprint::Value{std::int64_t{2}}},
        [&doneA](const RpcCallId id, const RpcOutcome& outcome) {
            doneA.emplace(id, outcome);
        });
    ASSERT_NE(callId, kInvalidRpcCallId);
    for (int round = 0; round < 3; ++round) {
        harness.relay.Update(1100U);
        rpcA.Update(1100U);
        rpcB.Update(1100U);
        rpcC.Update(1100U);
    }

    // Both B and C heard the call; B answered first (peer order), so A completed on B's
    // answer and C's error landed stale - broadcast RPC, honestly pinned.
    ASSERT_EQ(doneA.size(), 1U);
    EXPECT_TRUE(doneA[callId].ok);
    EXPECT_EQ(doneA[callId].value, Blueprint::Value{std::int64_t{3}});
    EXPECT_EQ(rpcB.Stats().callsReceived, 1U);
    EXPECT_EQ(rpcC.Stats().callsReceived, 1U);
    EXPECT_EQ(rpcB.Stats().responsesSent, 1U);
    EXPECT_EQ(rpcC.Stats().responsesSent, 1U);
    EXPECT_EQ(rpcA.Stats().responsesIgnoredStale, 1U);
    EXPECT_EQ(rpcB.Stats().responsesIgnoredStale, 1U);
    EXPECT_EQ(rpcC.Stats().responsesIgnoredStale, 1U);
    EXPECT_EQ(harness.relay.Stats().payloadsReceived, 3U);
    EXPECT_EQ(harness.relay.Stats().payloadsForwarded, 6U);
}

TEST(NetworkRelayTest, Timeout_IdlesPeerUntilRemoved) {
    RelayHarness harness = MakeRelay(2U);
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.clients[1].SendData(Bytes("keep")));
    harness.relay.Update(7000U);

    EXPECT_EQ(harness.relay.PeerSession(harness.peerIds[0])->State(),
              SessionState::Disconnected);
    EXPECT_EQ(harness.relay.PeerSession(harness.peerIds[1])->State(),
              SessionState::Established);
    EXPECT_EQ(harness.relay.Stats().payloadsReceived, 1U);
    EXPECT_EQ(harness.relay.Stats().payloadsForwarded, 0U);
    EXPECT_TRUE(harness.relay.RemovePeer(harness.peerIds[0]));
    EXPECT_EQ(harness.relay.PeerCount(), 1U);
}

} // namespace
} // namespace UVE::Network::Tests
