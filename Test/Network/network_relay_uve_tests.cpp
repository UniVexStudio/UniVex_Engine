// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_relay_uve.h"

#include "uve/network/network_replication_uve.h"
#include "uve/network/network_rpc_uve.h"

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

[[nodiscard]] std::vector<std::uint8_t> BytesUVE(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

struct RelayHarnessUVE final {
    std::vector<LoopbackTransportPairUVE> links;
    std::vector<NetworkSessionUVE> clients;
    NetworkRelayUVE relay;
    std::vector<RelayPeerIdUVE> peerIds;

    void UpdateClientsUVE(const std::uint64_t nowMs) {
        for (auto& client : clients) {
            client.UpdateUVE(nowMs);
        }
    }

    void UpdateAllUVE(const std::uint64_t nowMs) {
        relay.UpdateUVE(nowMs);
        UpdateClientsUVE(nowMs);
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

[[nodiscard]] RelayHarnessUVE MakeRelayUVE(const std::size_t clientCount,
                                           const bool multiplexed = false) {
    RelayHarnessUVE harness;
    for (std::size_t i = 0U; i < clientCount; ++i) {
        LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
        NetworkSessionUVE client(SessionRoleUVE::Client,
                                 std::make_unique<LoopbackEndpointUVE>(pair.a));
        NetworkSessionUVE server(SessionRoleUVE::Server,
                                 std::make_unique<LoopbackEndpointUVE>(pair.b));
        if (multiplexed) {
            static_cast<void>(client.EnableMultiplexingUVE());
            static_cast<void>(server.EnableMultiplexingUVE());
        }
        harness.peerIds.push_back(harness.relay.AddPeerUVE(std::move(server)));
        harness.clients.push_back(std::move(client));
        harness.links.push_back(std::move(pair));
    }
    return harness;
}

TEST(NetworkRelayUVETest, Peers_AddRemoveGuards) {
    RelayHarnessUVE harness = MakeRelayUVE(2U);
    EXPECT_NE(harness.peerIds[0], kInvalidRelayPeerIdUVE);
    EXPECT_NE(harness.peerIds[1], kInvalidRelayPeerIdUVE);
    EXPECT_NE(harness.peerIds[0], harness.peerIds[1]);
    EXPECT_NE(harness.relay.PeerSessionUVE(harness.peerIds[0]), nullptr);
    EXPECT_EQ(harness.relay.PeerSessionUVE(999U), nullptr);
    EXPECT_EQ(harness.relay.PeerCountUVE(), 2U);

    EXPECT_FALSE(harness.relay.RemovePeerUVE(999U));
    EXPECT_TRUE(harness.relay.RemovePeerUVE(harness.peerIds[0]));
    EXPECT_FALSE(harness.relay.RemovePeerUVE(harness.peerIds[0]));
    EXPECT_EQ(harness.relay.PeerSessionUVE(harness.peerIds[0]), nullptr);
    EXPECT_EQ(harness.relay.PeerCountUVE(), 1U);
}

TEST(NetworkRelayUVETest, Handshakes_CompleteThroughRelayPumps) {
    RelayHarnessUVE harness = MakeRelayUVE(3U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    for (const RelayPeerIdUVE id : harness.peerIds) {
        EXPECT_EQ(harness.relay.PeerSessionUVE(id)->StateUVE(),
                  SessionStateUVE::Established);
    }
}

TEST(NetworkRelayUVETest, Broadcast_FansOutWithoutEcho) {
    RelayHarnessUVE harness = MakeRelayUVE(3U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("a")));
    harness.UpdateAllUVE(1100U);

    EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE("a"));
    EXPECT_EQ(harness.clients[2].TryReceiveDataUVE(), BytesUVE("a"));
    EXPECT_FALSE(harness.clients[0].TryReceiveDataUVE().has_value());
    EXPECT_EQ(harness.relay.StatsUVE().payloadsReceived, 1U);
    EXPECT_EQ(harness.relay.StatsUVE().payloadsForwarded, 2U);
    EXPECT_EQ(harness.relay.StatsUVE().forwardsDropped, 0U);
}

TEST(NetworkRelayUVETest, Forwards_SkipNonEstablished) {
    RelayHarnessUVE harness = MakeRelayUVE(3U);
    ASSERT_TRUE(harness.clients[0].ConnectUVE(1000U));
    ASSERT_TRUE(harness.clients[1].ConnectUVE(1000U));
    harness.UpdateAllUVE(1000U);
    harness.UpdateAllUVE(1000U);
    ASSERT_EQ(harness.clients[2].StateUVE(), SessionStateUVE::Disconnected);
    ASSERT_EQ(harness.relay.PeerSessionUVE(harness.peerIds[2])->StateUVE(),
              SessionStateUVE::Disconnected);

    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("x")));
    harness.UpdateAllUVE(1100U);
    EXPECT_EQ(harness.clients[1].TryReceiveDataUVE(), BytesUVE("x"));
    EXPECT_FALSE(
        harness.relay.PeerSessionUVE(harness.peerIds[2])->TryReceiveDataUVE().has_value());
    EXPECT_EQ(harness.relay.StatsUVE().payloadsReceived, 1U);
    EXPECT_EQ(harness.relay.StatsUVE().payloadsForwarded, 1U);
    EXPECT_EQ(harness.relay.StatsUVE().forwardsDropped, 0U);

    ASSERT_TRUE(harness.clients[2].ConnectUVE(1100U));
    harness.UpdateAllUVE(1100U);
    harness.UpdateAllUVE(1100U);
    ASSERT_EQ(harness.relay.PeerSessionUVE(harness.peerIds[2])->StateUVE(),
              SessionStateUVE::Established);
    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("y")));
    harness.UpdateAllUVE(1200U);
    EXPECT_EQ(harness.clients[2].TryReceiveDataUVE(), BytesUVE("y"));
}

TEST(NetworkRelayUVETest, Muxed_ChannelsRideThroughUntouched) {
    RelayHarnessUVE harness = MakeRelayUVE(3U, true);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.clients[0].SendChannelDataUVE(5U, BytesUVE("c5")));
    harness.UpdateAllUVE(1100U);

    EXPECT_EQ(harness.clients[1].TryReceiveChannelDataUVE(5U), BytesUVE("c5"));
    EXPECT_EQ(harness.clients[2].TryReceiveChannelDataUVE(5U), BytesUVE("c5"));
    EXPECT_FALSE(harness.clients[1].TryReceiveChannelDataUVE(6U).has_value());
    EXPECT_FALSE(harness.clients[0].TryReceiveChannelDataUVE(5U).has_value());
    EXPECT_EQ(harness.relay.StatsUVE().payloadsReceived, 1U);
    EXPECT_EQ(harness.relay.StatsUVE().payloadsForwarded, 2U);
}

TEST(NetworkRelayUVETest, FullQueue_DropsCounted) {
    RelayHarnessUVE harness = MakeRelayUVE(3U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    int queuedA = 0;
    int queuedC = 0;
    for (int i = 0; i < 256; ++i) {
        queuedA += harness.clients[0].SendDataUVE(BytesUVE("f")) ? 1 : 0;
        queuedC += harness.clients[2].SendDataUVE(BytesUVE("g")) ? 1 : 0;
    }
    ASSERT_EQ(queuedA, 256);
    ASSERT_EQ(queuedC, 256);

    // One relay tick, nobody else pumps: A's 256 fill B's and C's queues, then C's 256 find
    // B's queue full - 256 drops, counted, everything else delivered.
    harness.relay.UpdateUVE(1100U);
    EXPECT_EQ(harness.relay.StatsUVE().payloadsReceived, 512U);
    EXPECT_EQ(harness.relay.StatsUVE().payloadsForwarded, 768U);
    EXPECT_EQ(harness.relay.StatsUVE().forwardsDropped, 256U);
}

TEST(NetworkRelayUVETest, Remove_StopsTraffic) {
    RelayHarnessUVE harness = MakeRelayUVE(3U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.relay.RemovePeerUVE(harness.peerIds[1]));
    ASSERT_TRUE(harness.clients[0].SendDataUVE(BytesUVE("a")));
    harness.UpdateAllUVE(1100U);

    EXPECT_EQ(harness.clients[2].TryReceiveDataUVE(), BytesUVE("a"));
    EXPECT_EQ(harness.relay.PeerCountUVE(), 2U);
    EXPECT_EQ(harness.relay.StatsUVE().payloadsReceived, 1U);
    EXPECT_EQ(harness.relay.StatsUVE().payloadsForwarded, 1U);
}

TEST(NetworkRelayUVETest, Replication_ConvergesOverRelay) {
    RelayHarnessUVE harness = MakeRelayUVE(3U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    NetworkReplicatorUVE replA(std::move(harness.clients[0]));
    NetworkReplicatorUVE replB(std::move(harness.clients[1]));
    NetworkReplicatorUVE replC(std::move(harness.clients[2]));
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> positionsA;
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> positionsB;
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> positionsC;
    ASSERT_TRUE(replA.RegisterEntityUVE(
        7U, [&positionsA]() { return positionsA[7U]; },
        [&positionsA](const Math::Vector3UVE& position) { positionsA[7U] = position; },
        ReplicationAuthorityUVE::Owner));
    ASSERT_TRUE(replB.RegisterEntityUVE(
        7U, [&positionsB]() { return positionsB[7U]; },
        [&positionsB](const Math::Vector3UVE& position) { positionsB[7U] = position; },
        ReplicationAuthorityUVE::Proxy));
    ASSERT_TRUE(replC.RegisterEntityUVE(
        7U, [&positionsC]() { return positionsC[7U]; },
        [&positionsC](const Math::Vector3UVE& position) { positionsC[7U] = position; },
        ReplicationAuthorityUVE::Proxy));
    positionsA[7U] = Math::Vector3UVE{5.0F, 5.0F, 5.0F};

    replA.UpdateUVE(1100U);
    harness.relay.UpdateUVE(1100U);
    replB.UpdateUVE(1100U);
    replC.UpdateUVE(1100U);
    EXPECT_EQ(positionsB[7U], (Math::Vector3UVE{5.0F, 5.0F, 5.0F}));
    EXPECT_EQ(positionsC[7U], (Math::Vector3UVE{5.0F, 5.0F, 5.0F}));
    replA.UpdateUVE(1200U);
    harness.relay.UpdateUVE(1200U);
    replB.UpdateUVE(1200U);
    replC.UpdateUVE(1200U);
    harness.relay.UpdateUVE(1300U);
    replA.UpdateUVE(1300U);

    EXPECT_EQ(replA.StatsUVE().snapshotsSent, 3U);
    EXPECT_EQ(replA.StatsUVE().snapshotsReceived, 4U); // Both rounds of B/C empties.
    EXPECT_EQ(replA.StatsUVE().messagesIgnoredMalformed, 0U);
    EXPECT_EQ(replB.StatsUVE().messagesIgnoredMalformed, 0U);
    EXPECT_EQ(replC.StatsUVE().messagesIgnoredMalformed, 0U);
    EXPECT_EQ(harness.relay.StatsUVE().payloadsReceived, 6U);
    EXPECT_EQ(harness.relay.StatsUVE().payloadsForwarded, 12U);
    EXPECT_EQ(harness.relay.StatsUVE().forwardsDropped, 0U);
}

TEST(NetworkRelayUVETest, Rpc_BroadcastIsFirstAnswerWins) {
    RelayHarnessUVE harness = MakeRelayUVE(3U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    NetworkRpcUVE rpcA(std::move(harness.clients[0]));
    NetworkRpcUVE rpcB(std::move(harness.clients[1]));
    NetworkRpcUVE rpcC(std::move(harness.clients[2]));
    ASSERT_TRUE(rpcB.RegisterMethodUVE(
        "add", [](const std::span<const UVScript::ValueUVE> args) {
            return RpcOkUVE(UVScript::ValueUVE{std::get<std::int64_t>(args[0]) +
                                               std::get<std::int64_t>(args[1])});
        }));
    std::map<RpcCallIdUVE, RpcOutcomeUVE> doneA;
    const RpcCallIdUVE callId = rpcA.CallUVE(
        "add", {UVScript::ValueUVE{std::int64_t{1}}, UVScript::ValueUVE{std::int64_t{2}}},
        [&doneA](const RpcCallIdUVE id, const RpcOutcomeUVE& outcome) {
            doneA.emplace(id, outcome);
        });
    ASSERT_NE(callId, kInvalidRpcCallIdUVE);
    for (int round = 0; round < 3; ++round) {
        harness.relay.UpdateUVE(1100U);
        rpcA.UpdateUVE(1100U);
        rpcB.UpdateUVE(1100U);
        rpcC.UpdateUVE(1100U);
    }

    // Both B and C heard the call; B answered first (peer order), so A completed on B's
    // answer and C's error landed stale - broadcast RPC, honestly pinned.
    ASSERT_EQ(doneA.size(), 1U);
    EXPECT_TRUE(doneA[callId].ok);
    EXPECT_EQ(doneA[callId].value, UVScript::ValueUVE{std::int64_t{3}});
    EXPECT_EQ(rpcB.StatsUVE().callsReceived, 1U);
    EXPECT_EQ(rpcC.StatsUVE().callsReceived, 1U);
    EXPECT_EQ(rpcB.StatsUVE().responsesSent, 1U);
    EXPECT_EQ(rpcC.StatsUVE().responsesSent, 1U);
    EXPECT_EQ(rpcA.StatsUVE().responsesIgnoredStale, 1U);
    EXPECT_EQ(rpcB.StatsUVE().responsesIgnoredStale, 1U);
    EXPECT_EQ(rpcC.StatsUVE().responsesIgnoredStale, 1U);
    EXPECT_EQ(harness.relay.StatsUVE().payloadsReceived, 3U);
    EXPECT_EQ(harness.relay.StatsUVE().payloadsForwarded, 6U);
}

TEST(NetworkRelayUVETest, Timeout_IdlesPeerUntilRemoved) {
    RelayHarnessUVE harness = MakeRelayUVE(2U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.clients[1].SendDataUVE(BytesUVE("keep")));
    harness.relay.UpdateUVE(7000U);

    EXPECT_EQ(harness.relay.PeerSessionUVE(harness.peerIds[0])->StateUVE(),
              SessionStateUVE::Disconnected);
    EXPECT_EQ(harness.relay.PeerSessionUVE(harness.peerIds[1])->StateUVE(),
              SessionStateUVE::Established);
    EXPECT_EQ(harness.relay.StatsUVE().payloadsReceived, 1U);
    EXPECT_EQ(harness.relay.StatsUVE().payloadsForwarded, 0U);
    EXPECT_TRUE(harness.relay.RemovePeerUVE(harness.peerIds[0]));
    EXPECT_EQ(harness.relay.PeerCountUVE(), 1U);
}

} // namespace
} // namespace UVE::Network::Tests
