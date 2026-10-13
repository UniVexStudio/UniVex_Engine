// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_udp_uve.h"

#include "uve/network/network_session_uve.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

// Everything here binds ephemeral ports (the listener reports its own, clients learn it):
// no fixed-port squatting, no chicken-and-egg, nothing to clash with in parallel.
inline constexpr std::uint32_t kLoopbackHostUVE = 0x7F000001U;

[[nodiscard]] std::vector<std::uint8_t> BytesUVE(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

[[nodiscard]] std::optional<UdpTransportEndpointUVE> MakeClientUVE(
    const std::uint16_t listenerPort) {
    UdpTransportConfigUVE config;
    config.localPort = 0U;
    config.remoteHost = "127.0.0.1";
    config.remotePort = listenerPort;
    return CreateUdpTransportEndpointUVE(config);
}

[[nodiscard]] UdpEndpointAddressUVE AddressOfUVE(const UdpTransportEndpointUVE& client) {
    return UdpEndpointAddressUVE{kLoopbackHostUVE, client.LocalPortUVE()};
}

// Spins until the predicate holds or the budget (in 1ms naps) runs out. Localhost delivery
// is near-instant, but the kernel is not the test's to command - poll, don't assume.
template <typename PredicateUVE>
[[nodiscard]] bool PollUntilUVE(PredicateUVE&& predicate, const int budgetMs = 2000) {
    for (int i = 0; i < budgetMs; ++i) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

TEST(UdpListenerUVE, ListenerSeesStrangersAndLearnsTheirAddresses) {
    std::optional<UdpListenerUVE> listener = CreateUdpListenerUVE(UdpListenerConfigUVE{});
    ASSERT_TRUE(listener.has_value());
    EXPECT_NE(listener->LocalPortUVE(), 0U);
    EXPECT_FALSE(listener->HasStrangersUVE());
    EXPECT_EQ(listener->StrangerCountUVE(), 0U);
    EXPECT_FALSE(listener->NextStrangerUVE().has_value());
    EXPECT_EQ(listener->PeerCountUVE(), 0U);

    std::optional<UdpTransportEndpointUVE> client = MakeClientUVE(listener->LocalPortUVE());
    ASSERT_TRUE(client.has_value());
    ASSERT_TRUE(client->SendUVE(BytesUVE("hi")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return listener->HasStrangersUVE();
    }));

    const UdpEndpointAddressUVE expected = AddressOfUVE(*client);
    EXPECT_EQ(listener->StrangerCountUVE(), 1U);
    ASSERT_TRUE(listener->NextStrangerUVE().has_value());
    EXPECT_EQ(*listener->NextStrangerUVE(), expected);
    EXPECT_EQ(listener->NextStrangerUVE()->ToStringUVE(),
              "127.0.0.1:" + std::to_string(client->LocalPortUVE()));

    std::optional<UdpListenerPeerUVE> peer = listener->AcceptPeerUVE();
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(peer->RemoteUVE(), expected);
    EXPECT_EQ(listener->PeerCountUVE(), 1U);
    EXPECT_TRUE(listener->HasPeerUVE(expected));
    EXPECT_FALSE(listener->HasPeerUVE(UdpEndpointAddressUVE{kLoopbackHostUVE, 1U}));
    EXPECT_FALSE(listener->HasStrangersUVE());
    EXPECT_FALSE(listener->AcceptPeerUVE().has_value());
}

TEST(UdpListenerUVE, AcceptReplaysHeldDatagramsInOrder) {
    std::optional<UdpListenerUVE> listener = CreateUdpListenerUVE(UdpListenerConfigUVE{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpointUVE> client = MakeClientUVE(listener->LocalPortUVE());
    ASSERT_TRUE(client.has_value());

    ASSERT_TRUE(client->SendUVE(BytesUVE("first")));
    ASSERT_TRUE(client->SendUVE(BytesUVE("second")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return listener->StatsUVE().datagramsHeldForStrangers == 2U;
    }));

    std::optional<UdpListenerPeerUVE> peer = listener->AcceptPeerUVE();
    ASSERT_TRUE(peer.has_value());
    // Synchronous replay, arrival order, no pump in between.
    EXPECT_EQ(peer->TryReceiveUVE(), BytesUVE("first"));
    EXPECT_EQ(peer->TryReceiveUVE(), BytesUVE("second"));
    EXPECT_FALSE(peer->TryReceiveUVE().has_value());
    EXPECT_EQ(peer->StatsUVE().datagramsReceived, 2U);
}

TEST(UdpListenerUVE, PeersIsolateTrafficBothDirections) {
    std::optional<UdpListenerUVE> listener = CreateUdpListenerUVE(UdpListenerConfigUVE{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpointUVE> clientA = MakeClientUVE(listener->LocalPortUVE());
    std::optional<UdpTransportEndpointUVE> clientB = MakeClientUVE(listener->LocalPortUVE());
    ASSERT_TRUE(clientA.has_value());
    ASSERT_TRUE(clientB.has_value());

    ASSERT_TRUE(clientA->SendUVE(BytesUVE("knock-a")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return listener->StrangerCountUVE() == 1U;
    }));
    std::optional<UdpListenerPeerUVE> peerA = listener->AcceptPeerUVE();
    ASSERT_TRUE(peerA.has_value());
    EXPECT_EQ(peerA->TryReceiveUVE(), BytesUVE("knock-a"));
    ASSERT_TRUE(clientB->SendUVE(BytesUVE("knock-b")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return listener->StrangerCountUVE() == 1U;
    }));
    std::optional<UdpListenerPeerUVE> peerB = listener->AcceptPeerUVE();
    ASSERT_TRUE(peerB.has_value());
    EXPECT_EQ(peerB->TryReceiveUVE(), BytesUVE("knock-b"));

    ASSERT_TRUE(clientA->SendUVE(BytesUVE("up-a")));
    ASSERT_TRUE(clientB->SendUVE(BytesUVE("up-b")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return peerA->HasPendingUVE() && peerB->HasPendingUVE();
    }));
    EXPECT_EQ(peerA->TryReceiveUVE(), BytesUVE("up-a"));
    EXPECT_EQ(peerB->TryReceiveUVE(), BytesUVE("up-b"));

    ASSERT_TRUE(peerA->SendUVE(BytesUVE("down-a")));
    ASSERT_TRUE(peerB->SendUVE(BytesUVE("down-b")));
    ASSERT_TRUE(PollUntilUVE([&] { return clientA->HasPendingUVE(); }));
    ASSERT_TRUE(PollUntilUVE([&] { return clientB->HasPendingUVE(); }));
    EXPECT_EQ(clientA->TryReceiveUVE(), BytesUVE("down-a"));
    EXPECT_EQ(clientB->TryReceiveUVE(), BytesUVE("down-b"));
    EXPECT_FALSE(clientA->TryReceiveUVE().has_value());
    EXPECT_FALSE(clientB->TryReceiveUVE().has_value());

    // Copies share the socket, the queue, and the ledger; moved-from is inert.
    UdpListenerPeerUVE copy = *peerA;
    EXPECT_TRUE(copy.SendUVE(BytesUVE("via-copy")));
    EXPECT_EQ(peerA->StatsUVE().datagramsSent, copy.StatsUVE().datagramsSent);
    ASSERT_TRUE(PollUntilUVE([&] { return clientA->HasPendingUVE(); }));
    EXPECT_EQ(clientA->TryReceiveUVE(), BytesUVE("via-copy"));
    UdpListenerPeerUVE moved = std::move(copy);
    EXPECT_FALSE(copy.SendUVE(BytesUVE("nope")));
    EXPECT_FALSE(copy.TryReceiveUVE().has_value());
    EXPECT_FALSE(copy.HasPendingUVE());
    EXPECT_EQ(copy.PendingCountUVE(), 0U);
    EXPECT_EQ(copy.StatsUVE(), UdpTransportStatsUVE{});
    EXPECT_EQ(moved.RemoteUVE(), peerA->RemoteUVE());
}

TEST(UdpListenerUVE, TwoSessionsShareOneSocket) {
    std::optional<UdpListenerUVE> listener = CreateUdpListenerUVE(UdpListenerConfigUVE{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpointUVE> endpointA = MakeClientUVE(listener->LocalPortUVE());
    std::optional<UdpTransportEndpointUVE> endpointB = MakeClientUVE(listener->LocalPortUVE());
    ASSERT_TRUE(endpointA.has_value());
    ASSERT_TRUE(endpointB.has_value());
    NetworkSessionUVE clientA(SessionRoleUVE::Client,
                              std::make_unique<UdpTransportEndpointUVE>(std::move(*endpointA)));
    NetworkSessionUVE clientB(SessionRoleUVE::Client,
                              std::make_unique<UdpTransportEndpointUVE>(std::move(*endpointB)));
    ASSERT_TRUE(clientA.ConnectUVE(0U));
    ASSERT_TRUE(clientB.ConnectUVE(0U));

    // A real server's accept loop in miniature: pump, adopt every stranger into a session,
    // pump the sessions - the replayed Hellos establish with no redial.
    std::vector<NetworkSessionUVE> servers;
    servers.reserve(2U);
    std::uint64_t nowMs = 0U;
    const bool established = PollUntilUVE([&] {
        clientA.UpdateUVE(nowMs);
        clientB.UpdateUVE(nowMs);
        listener->UpdateUVE();
        while (servers.size() < 2U) {
            std::optional<UdpListenerPeerUVE> peer = listener->AcceptPeerUVE();
            if (!peer.has_value()) {
                break;
            }
            servers.push_back(NetworkSessionUVE(
                SessionRoleUVE::Server, std::make_unique<UdpListenerPeerUVE>(std::move(*peer))));
        }
        for (auto& server : servers) {
            server.UpdateUVE(nowMs);
        }
        ++nowMs;
        return servers.size() == 2U &&
               clientA.StateUVE() == SessionStateUVE::Established &&
               clientB.StateUVE() == SessionStateUVE::Established &&
               servers[0].StateUVE() == SessionStateUVE::Established &&
               servers[1].StateUVE() == SessionStateUVE::Established;
    });
    ASSERT_TRUE(established);

    ASSERT_TRUE(clientA.SendDataUVE(BytesUVE("a1")));
    ASSERT_TRUE(servers[0].SendDataUVE(BytesUVE("a2")));
    ASSERT_TRUE(clientB.SendDataUVE(BytesUVE("b1")));
    ASSERT_TRUE(servers[1].SendDataUVE(BytesUVE("b2")));
    std::optional<std::vector<std::uint8_t>> toServerA;
    std::optional<std::vector<std::uint8_t>> toClientA;
    std::optional<std::vector<std::uint8_t>> toServerB;
    std::optional<std::vector<std::uint8_t>> toClientB;
    const bool exchanged = PollUntilUVE([&] {
        clientA.UpdateUVE(nowMs);
        clientB.UpdateUVE(nowMs);
        listener->UpdateUVE();
        for (auto& server : servers) {
            server.UpdateUVE(nowMs);
        }
        ++nowMs;
        if (!toServerA.has_value()) {
            toServerA = servers[0].TryReceiveDataUVE();
        }
        if (!toClientA.has_value()) {
            toClientA = clientA.TryReceiveDataUVE();
        }
        if (!toServerB.has_value()) {
            toServerB = servers[1].TryReceiveDataUVE();
        }
        if (!toClientB.has_value()) {
            toClientB = clientB.TryReceiveDataUVE();
        }
        return toServerA.has_value() && toClientA.has_value() && toServerB.has_value() &&
               toClientB.has_value();
    });
    ASSERT_TRUE(exchanged);
    EXPECT_EQ(toServerA, BytesUVE("a1"));
    EXPECT_EQ(toClientA, BytesUVE("a2"));
    EXPECT_EQ(toServerB, BytesUVE("b1"));
    EXPECT_EQ(toClientB, BytesUVE("b2"));
}

TEST(UdpListenerUVE, StrangerBacklogBoundsAndRefusalsForget) {
    UdpListenerConfigUVE config;
    config.maxStrangers = 1U;
    config.maxStrangerDatagrams = 1U;
    std::optional<UdpListenerUVE> listener = CreateUdpListenerUVE(config);
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpointUVE> clientA = MakeClientUVE(listener->LocalPortUVE());
    std::optional<UdpTransportEndpointUVE> clientB = MakeClientUVE(listener->LocalPortUVE());
    ASSERT_TRUE(clientA.has_value());
    ASSERT_TRUE(clientB.has_value());

    // Staged so A lands first: loopback preserves per-sender order, not cross-sender order.
    ASSERT_TRUE(clientA->SendUVE(BytesUVE("a1")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return listener->StrangerCountUVE() == 1U;
    }));
    ASSERT_TRUE(clientA->SendUVE(BytesUVE("a2")));
    ASSERT_TRUE(clientB->SendUVE(BytesUVE("b1")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return listener->StatsUVE().datagramsReceived == 3U;
    }));

    EXPECT_EQ(listener->StatsUVE().datagramsHeldForStrangers, 1U);
    EXPECT_EQ(listener->StatsUVE().strangersDroppedFull, 2U);
    EXPECT_EQ(listener->NextStrangerUVE(), AddressOfUVE(*clientA));

    EXPECT_TRUE(listener->RefuseStrangerUVE(AddressOfUVE(*clientA)));
    EXPECT_FALSE(listener->RefuseStrangerUVE(AddressOfUVE(*clientA)));
    EXPECT_FALSE(listener->HasStrangersUVE());

    // Refusal is forgetting, not banning: the next datagram strangers it again.
    ASSERT_TRUE(clientA->SendUVE(BytesUVE("a3")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return listener->HasStrangersUVE();
    }));
    EXPECT_EQ(listener->NextStrangerUVE(), AddressOfUVE(*clientA));
}

TEST(UdpListenerUVE, RemovePeerOrphansItsEndpoint) {
    std::optional<UdpListenerUVE> listener = CreateUdpListenerUVE(UdpListenerConfigUVE{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpointUVE> client = MakeClientUVE(listener->LocalPortUVE());
    ASSERT_TRUE(client.has_value());
    ASSERT_TRUE(client->SendUVE(BytesUVE("knock")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return listener->HasStrangersUVE();
    }));
    const UdpEndpointAddressUVE address = AddressOfUVE(*client);
    std::optional<UdpListenerPeerUVE> peer = listener->AcceptPeerUVE();
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(peer->TryReceiveUVE(), BytesUVE("knock"));

    EXPECT_TRUE(listener->RemovePeerUVE(address));
    EXPECT_FALSE(listener->RemovePeerUVE(address));
    EXPECT_EQ(listener->PeerCountUVE(), 0U);
    EXPECT_FALSE(peer->SendUVE(BytesUVE("nope")));
    EXPECT_FALSE(peer->TryReceiveUVE().has_value());
    EXPECT_EQ(peer->StatsUVE(), UdpTransportStatsUVE{});

    // Its next datagram strangers it again, and the named accept re-adopts it.
    ASSERT_TRUE(client->SendUVE(BytesUVE("again")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return listener->HasStrangersUVE();
    }));
    EXPECT_FALSE(listener->AcceptPeerUVE(UdpEndpointAddressUVE{kLoopbackHostUVE, 1U}, 256U)
                     .has_value());
    std::optional<UdpListenerPeerUVE> readopted = listener->AcceptPeerUVE(address, 256U);
    ASSERT_TRUE(readopted.has_value());
    EXPECT_EQ(readopted->TryReceiveUVE(), BytesUVE("again"));
    EXPECT_EQ(listener->PeerCountUVE(), 1U);
}

TEST(UdpListenerUVE, AcceptSpecificStrangerLeavesTheRest) {
    std::optional<UdpListenerUVE> listener = CreateUdpListenerUVE(UdpListenerConfigUVE{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpointUVE> clientA = MakeClientUVE(listener->LocalPortUVE());
    std::optional<UdpTransportEndpointUVE> clientB = MakeClientUVE(listener->LocalPortUVE());
    ASSERT_TRUE(clientA.has_value());
    ASSERT_TRUE(clientB.has_value());

    ASSERT_TRUE(clientA->SendUVE(BytesUVE("a")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return listener->StrangerCountUVE() == 1U;
    }));
    ASSERT_TRUE(clientB->SendUVE(BytesUVE("b")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return listener->StrangerCountUVE() == 2U;
    }));

    std::optional<UdpListenerPeerUVE> peerB =
        listener->AcceptPeerUVE(AddressOfUVE(*clientB), 256U);
    ASSERT_TRUE(peerB.has_value());
    EXPECT_EQ(peerB->TryReceiveUVE(), BytesUVE("b"));
    EXPECT_EQ(listener->StrangerCountUVE(), 1U);
    EXPECT_EQ(listener->NextStrangerUVE(), AddressOfUVE(*clientA));
    std::optional<UdpListenerPeerUVE> peerA = listener->AcceptPeerUVE();
    ASSERT_TRUE(peerA.has_value());
    EXPECT_EQ(peerA->RemoteUVE(), AddressOfUVE(*clientA));
}

TEST(UdpListenerUVE, PeerSendOversizeFailsAndCounts) {
    std::optional<UdpListenerUVE> listener = CreateUdpListenerUVE(UdpListenerConfigUVE{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpointUVE> client = MakeClientUVE(listener->LocalPortUVE());
    ASSERT_TRUE(client.has_value());
    ASSERT_TRUE(client->SendUVE(BytesUVE("knock")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return listener->HasStrangersUVE();
    }));
    std::optional<UdpListenerPeerUVE> peer = listener->AcceptPeerUVE();
    ASSERT_TRUE(peer.has_value());

    const std::vector<std::uint8_t> over(kTransportMaximumDatagramBytesUVE + 1U, 0xAB);
    EXPECT_FALSE(peer->SendUVE(over));
    EXPECT_EQ(peer->StatsUVE().datagramsDroppedOversize, 1U);
    EXPECT_EQ(peer->StatsUVE().datagramsSent, 0U);
}

TEST(UdpListenerUVE, ListenerStatsPartitionExactly) {
    std::optional<UdpListenerUVE> listener = CreateUdpListenerUVE(UdpListenerConfigUVE{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpointUVE> clientA = MakeClientUVE(listener->LocalPortUVE());
    std::optional<UdpTransportEndpointUVE> clientB = MakeClientUVE(listener->LocalPortUVE());
    ASSERT_TRUE(clientA.has_value());
    ASSERT_TRUE(clientB.has_value());

    ASSERT_TRUE(clientA->SendUVE(BytesUVE("knock")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return listener->HasStrangersUVE();
    }));
    std::optional<UdpListenerPeerUVE> peer =
        listener->AcceptPeerUVE(AddressOfUVE(*clientA), 1U);
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(peer->TryReceiveUVE(), BytesUVE("knock"));

    ASSERT_TRUE(clientA->SendUVE(BytesUVE("a1")));
    ASSERT_TRUE(clientA->SendUVE(BytesUVE("a2")));
    ASSERT_TRUE(clientB->SendUVE(BytesUVE("b1")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return listener->StatsUVE().datagramsReceived == 4U;
    }));

    // knock held, a1 routed, a2 over the bound-1 queue, b1 held: every arrival in exactly
    // one bucket, summing to received.
    const UdpListenerStatsUVE stats = listener->StatsUVE();
    EXPECT_EQ(stats.datagramsReceived, 4U);
    EXPECT_EQ(stats.datagramsRouted, 1U);
    EXPECT_EQ(stats.datagramsHeldForStrangers, 2U);
    EXPECT_EQ(stats.datagramsDroppedFull, 1U);
    EXPECT_EQ(stats.datagramsDroppedOversize, 0U);
    EXPECT_EQ(stats.strangersDroppedFull, 0U);
    EXPECT_EQ(stats.datagramsReceived, stats.datagramsRouted +
                                           stats.datagramsHeldForStrangers +
                                           stats.datagramsDroppedFull +
                                           stats.datagramsDroppedOversize +
                                           stats.strangersDroppedFull);
    EXPECT_EQ(peer->TryReceiveUVE(), BytesUVE("a1"));
    EXPECT_FALSE(peer->TryReceiveUVE().has_value());
}

TEST(UdpListenerUVE, PeerPendingCountsAreExact) {
    std::optional<UdpListenerUVE> listener = CreateUdpListenerUVE(UdpListenerConfigUVE{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpointUVE> client = MakeClientUVE(listener->LocalPortUVE());
    ASSERT_TRUE(client.has_value());
    ASSERT_TRUE(client->SendUVE(BytesUVE("knock")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return listener->HasStrangersUVE();
    }));
    std::optional<UdpListenerPeerUVE> peer = listener->AcceptPeerUVE();
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(peer->TryReceiveUVE(), BytesUVE("knock"));
    EXPECT_FALSE(peer->HasPendingUVE());
    EXPECT_EQ(peer->PendingCountUVE(), 0U);

    ASSERT_TRUE(client->SendUVE(BytesUVE("one")));
    ASSERT_TRUE(client->SendUVE(BytesUVE("two")));
    ASSERT_TRUE(PollUntilUVE([&] {
        listener->UpdateUVE();
        return peer->PendingCountUVE() == 2U;
    }));
    // Exact, where a connected endpoint would read the lower bound 1: peers hold no socket
    // backlog of their own, so the queue is all there is.
    EXPECT_TRUE(peer->HasPendingUVE());
    EXPECT_EQ(peer->PendingCountUVE(), 2U);
    EXPECT_EQ(peer->TryReceiveUVE(), BytesUVE("one"));
    EXPECT_EQ(peer->PendingCountUVE(), 1U);
    EXPECT_EQ(peer->TryReceiveUVE(), BytesUVE("two"));
    EXPECT_FALSE(peer->HasPendingUVE());
    EXPECT_EQ(peer->PendingCountUVE(), 0U);
}

TEST(UdpListenerUVE, FactoryRefusesTakenPort) {
    std::optional<UdpListenerUVE> first = CreateUdpListenerUVE(UdpListenerConfigUVE{});
    ASSERT_TRUE(first.has_value());
    EXPECT_NE(first->LocalPortUVE(), 0U);
    UdpListenerConfigUVE taken;
    taken.localPort = first->LocalPortUVE();
    EXPECT_FALSE(CreateUdpListenerUVE(taken).has_value());
}

} // namespace
} // namespace UVE::Network::Tests
