// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_udp.h"

#include "network_session.h"

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
inline constexpr std::uint32_t kLoopbackHost = 0x7F000001U;

[[nodiscard]] std::vector<std::uint8_t> Bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

[[nodiscard]] std::optional<UdpTransportEndpoint> MakeClient(
    const std::uint16_t listenerPort) {
    UdpTransportConfig config;
    config.localPort = 0U;
    config.remoteHost = "127.0.0.1";
    config.remotePort = listenerPort;
    return CreateUdpTransportEndpoint(config);
}

[[nodiscard]] UdpEndpointAddress AddressOf(const UdpTransportEndpoint& client) {
    return UdpEndpointAddress{kLoopbackHost, client.LocalPort()};
}

// Spins until the predicate holds or the budget (in 1ms naps) runs out. Localhost delivery
// is near-instant, but the kernel is not the test's to command - poll, don't assume.
template <typename Predicate>
[[nodiscard]] bool PollUntil(Predicate&& predicate, const int budgetMs = 2000) {
    for (int i = 0; i < budgetMs; ++i) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

TEST(UdpListener, ListenerSeesStrangersAndLearnsTheirAddresses) {
    std::optional<UdpListener> listener = CreateUdpListener(UdpListenerConfig{});
    ASSERT_TRUE(listener.has_value());
    EXPECT_NE(listener->LocalPort(), 0U);
    EXPECT_FALSE(listener->HasStrangers());
    EXPECT_EQ(listener->StrangerCount(), 0U);
    EXPECT_FALSE(listener->NextStranger().has_value());
    EXPECT_EQ(listener->PeerCount(), 0U);

    std::optional<UdpTransportEndpoint> client = MakeClient(listener->LocalPort());
    ASSERT_TRUE(client.has_value());
    ASSERT_TRUE(client->Send(Bytes("hi")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return listener->HasStrangers();
    }));

    const UdpEndpointAddress expected = AddressOf(*client);
    EXPECT_EQ(listener->StrangerCount(), 1U);
    ASSERT_TRUE(listener->NextStranger().has_value());
    EXPECT_EQ(*listener->NextStranger(), expected);
    EXPECT_EQ(listener->NextStranger()->ToString(),
              "127.0.0.1:" + std::to_string(client->LocalPort()));

    std::optional<UdpListenerPeer> peer = listener->AcceptPeer();
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(peer->Remote(), expected);
    EXPECT_EQ(listener->PeerCount(), 1U);
    EXPECT_TRUE(listener->HasPeer(expected));
    EXPECT_FALSE(listener->HasPeer(UdpEndpointAddress{kLoopbackHost, 1U}));
    EXPECT_FALSE(listener->HasStrangers());
    EXPECT_FALSE(listener->AcceptPeer().has_value());
}

TEST(UdpListener, AcceptReplaysHeldDatagramsInOrder) {
    std::optional<UdpListener> listener = CreateUdpListener(UdpListenerConfig{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpoint> client = MakeClient(listener->LocalPort());
    ASSERT_TRUE(client.has_value());

    ASSERT_TRUE(client->Send(Bytes("first")));
    ASSERT_TRUE(client->Send(Bytes("second")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return listener->Stats().datagramsHeldForStrangers == 2U;
    }));

    std::optional<UdpListenerPeer> peer = listener->AcceptPeer();
    ASSERT_TRUE(peer.has_value());
    // Synchronous replay, arrival order, no pump in between.
    EXPECT_EQ(peer->TryReceive(), Bytes("first"));
    EXPECT_EQ(peer->TryReceive(), Bytes("second"));
    EXPECT_FALSE(peer->TryReceive().has_value());
    EXPECT_EQ(peer->Stats().datagramsReceived, 2U);
}

TEST(UdpListener, PeersIsolateTrafficBothDirections) {
    std::optional<UdpListener> listener = CreateUdpListener(UdpListenerConfig{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpoint> clientA = MakeClient(listener->LocalPort());
    std::optional<UdpTransportEndpoint> clientB = MakeClient(listener->LocalPort());
    ASSERT_TRUE(clientA.has_value());
    ASSERT_TRUE(clientB.has_value());

    ASSERT_TRUE(clientA->Send(Bytes("knock-a")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return listener->StrangerCount() == 1U;
    }));
    std::optional<UdpListenerPeer> peerA = listener->AcceptPeer();
    ASSERT_TRUE(peerA.has_value());
    EXPECT_EQ(peerA->TryReceive(), Bytes("knock-a"));
    ASSERT_TRUE(clientB->Send(Bytes("knock-b")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return listener->StrangerCount() == 1U;
    }));
    std::optional<UdpListenerPeer> peerB = listener->AcceptPeer();
    ASSERT_TRUE(peerB.has_value());
    EXPECT_EQ(peerB->TryReceive(), Bytes("knock-b"));

    ASSERT_TRUE(clientA->Send(Bytes("up-a")));
    ASSERT_TRUE(clientB->Send(Bytes("up-b")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return peerA->HasPending() && peerB->HasPending();
    }));
    EXPECT_EQ(peerA->TryReceive(), Bytes("up-a"));
    EXPECT_EQ(peerB->TryReceive(), Bytes("up-b"));

    ASSERT_TRUE(peerA->Send(Bytes("down-a")));
    ASSERT_TRUE(peerB->Send(Bytes("down-b")));
    ASSERT_TRUE(PollUntil([&] { return clientA->HasPending(); }));
    ASSERT_TRUE(PollUntil([&] { return clientB->HasPending(); }));
    EXPECT_EQ(clientA->TryReceive(), Bytes("down-a"));
    EXPECT_EQ(clientB->TryReceive(), Bytes("down-b"));
    EXPECT_FALSE(clientA->TryReceive().has_value());
    EXPECT_FALSE(clientB->TryReceive().has_value());

    // Copies share the socket, the queue, and the ledger; moved-from is inert.
    UdpListenerPeer copy = *peerA;
    EXPECT_TRUE(copy.Send(Bytes("via-copy")));
    EXPECT_EQ(peerA->Stats().datagramsSent, copy.Stats().datagramsSent);
    ASSERT_TRUE(PollUntil([&] { return clientA->HasPending(); }));
    EXPECT_EQ(clientA->TryReceive(), Bytes("via-copy"));
    UdpListenerPeer moved = std::move(copy);
    EXPECT_FALSE(copy.Send(Bytes("nope")));
    EXPECT_FALSE(copy.TryReceive().has_value());
    EXPECT_FALSE(copy.HasPending());
    EXPECT_EQ(copy.PendingCount(), 0U);
    EXPECT_EQ(copy.Stats(), UdpTransportStats{});
    EXPECT_EQ(moved.Remote(), peerA->Remote());
}

TEST(UdpListener, TwoSessionsShareOneSocket) {
    std::optional<UdpListener> listener = CreateUdpListener(UdpListenerConfig{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpoint> endpointA = MakeClient(listener->LocalPort());
    std::optional<UdpTransportEndpoint> endpointB = MakeClient(listener->LocalPort());
    ASSERT_TRUE(endpointA.has_value());
    ASSERT_TRUE(endpointB.has_value());
    NetworkSession clientA(SessionRole::Client,
                              std::make_unique<UdpTransportEndpoint>(std::move(*endpointA)));
    NetworkSession clientB(SessionRole::Client,
                              std::make_unique<UdpTransportEndpoint>(std::move(*endpointB)));
    ASSERT_TRUE(clientA.Connect(0U));
    ASSERT_TRUE(clientB.Connect(0U));

    // A real server's accept loop in miniature: pump, adopt every stranger into a session,
    // pump the sessions - the replayed Hellos establish with no redial.
    std::vector<NetworkSession> servers;
    servers.reserve(2U);
    std::uint64_t nowMs = 0U;
    const bool established = PollUntil([&] {
        clientA.Update(nowMs);
        clientB.Update(nowMs);
        listener->Update();
        while (servers.size() < 2U) {
            std::optional<UdpListenerPeer> peer = listener->AcceptPeer();
            if (!peer.has_value()) {
                break;
            }
            servers.push_back(NetworkSession(
                SessionRole::Server, std::make_unique<UdpListenerPeer>(std::move(*peer))));
        }
        for (auto& server : servers) {
            server.Update(nowMs);
        }
        ++nowMs;
        return servers.size() == 2U &&
               clientA.State() == SessionState::Established &&
               clientB.State() == SessionState::Established &&
               servers[0].State() == SessionState::Established &&
               servers[1].State() == SessionState::Established;
    });
    ASSERT_TRUE(established);

    ASSERT_TRUE(clientA.SendData(Bytes("a1")));
    ASSERT_TRUE(servers[0].SendData(Bytes("a2")));
    ASSERT_TRUE(clientB.SendData(Bytes("b1")));
    ASSERT_TRUE(servers[1].SendData(Bytes("b2")));
    std::optional<std::vector<std::uint8_t>> toServerA;
    std::optional<std::vector<std::uint8_t>> toClientA;
    std::optional<std::vector<std::uint8_t>> toServerB;
    std::optional<std::vector<std::uint8_t>> toClientB;
    const bool exchanged = PollUntil([&] {
        clientA.Update(nowMs);
        clientB.Update(nowMs);
        listener->Update();
        for (auto& server : servers) {
            server.Update(nowMs);
        }
        ++nowMs;
        if (!toServerA.has_value()) {
            toServerA = servers[0].TryReceiveData();
        }
        if (!toClientA.has_value()) {
            toClientA = clientA.TryReceiveData();
        }
        if (!toServerB.has_value()) {
            toServerB = servers[1].TryReceiveData();
        }
        if (!toClientB.has_value()) {
            toClientB = clientB.TryReceiveData();
        }
        return toServerA.has_value() && toClientA.has_value() && toServerB.has_value() &&
               toClientB.has_value();
    });
    ASSERT_TRUE(exchanged);
    EXPECT_EQ(toServerA, Bytes("a1"));
    EXPECT_EQ(toClientA, Bytes("a2"));
    EXPECT_EQ(toServerB, Bytes("b1"));
    EXPECT_EQ(toClientB, Bytes("b2"));
}

TEST(UdpListener, StrangerBacklogBoundsAndRefusalsForget) {
    UdpListenerConfig config;
    config.maxStrangers = 1U;
    config.maxStrangerDatagrams = 1U;
    std::optional<UdpListener> listener = CreateUdpListener(config);
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpoint> clientA = MakeClient(listener->LocalPort());
    std::optional<UdpTransportEndpoint> clientB = MakeClient(listener->LocalPort());
    ASSERT_TRUE(clientA.has_value());
    ASSERT_TRUE(clientB.has_value());

    // Staged so A lands first: loopback preserves per-sender order, not cross-sender order.
    ASSERT_TRUE(clientA->Send(Bytes("a1")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return listener->StrangerCount() == 1U;
    }));
    ASSERT_TRUE(clientA->Send(Bytes("a2")));
    ASSERT_TRUE(clientB->Send(Bytes("b1")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return listener->Stats().datagramsReceived == 3U;
    }));

    EXPECT_EQ(listener->Stats().datagramsHeldForStrangers, 1U);
    EXPECT_EQ(listener->Stats().strangersDroppedFull, 2U);
    EXPECT_EQ(listener->NextStranger(), AddressOf(*clientA));

    EXPECT_TRUE(listener->RefuseStranger(AddressOf(*clientA)));
    EXPECT_FALSE(listener->RefuseStranger(AddressOf(*clientA)));
    EXPECT_FALSE(listener->HasStrangers());

    // Refusal is forgetting, not banning: the next datagram strangers it again.
    ASSERT_TRUE(clientA->Send(Bytes("a3")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return listener->HasStrangers();
    }));
    EXPECT_EQ(listener->NextStranger(), AddressOf(*clientA));
}

TEST(UdpListener, RemovePeerOrphansItsEndpoint) {
    std::optional<UdpListener> listener = CreateUdpListener(UdpListenerConfig{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpoint> client = MakeClient(listener->LocalPort());
    ASSERT_TRUE(client.has_value());
    ASSERT_TRUE(client->Send(Bytes("knock")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return listener->HasStrangers();
    }));
    const UdpEndpointAddress address = AddressOf(*client);
    std::optional<UdpListenerPeer> peer = listener->AcceptPeer();
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(peer->TryReceive(), Bytes("knock"));

    EXPECT_TRUE(listener->RemovePeer(address));
    EXPECT_FALSE(listener->RemovePeer(address));
    EXPECT_EQ(listener->PeerCount(), 0U);
    EXPECT_FALSE(peer->Send(Bytes("nope")));
    EXPECT_FALSE(peer->TryReceive().has_value());
    EXPECT_EQ(peer->Stats(), UdpTransportStats{});

    // Its next datagram strangers it again, and the named accept re-adopts it.
    ASSERT_TRUE(client->Send(Bytes("again")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return listener->HasStrangers();
    }));
    EXPECT_FALSE(listener->AcceptPeer(UdpEndpointAddress{kLoopbackHost, 1U}, 256U)
                     .has_value());
    std::optional<UdpListenerPeer> readopted = listener->AcceptPeer(address, 256U);
    ASSERT_TRUE(readopted.has_value());
    EXPECT_EQ(readopted->TryReceive(), Bytes("again"));
    EXPECT_EQ(listener->PeerCount(), 1U);
}

TEST(UdpListener, AcceptSpecificStrangerLeavesTheRest) {
    std::optional<UdpListener> listener = CreateUdpListener(UdpListenerConfig{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpoint> clientA = MakeClient(listener->LocalPort());
    std::optional<UdpTransportEndpoint> clientB = MakeClient(listener->LocalPort());
    ASSERT_TRUE(clientA.has_value());
    ASSERT_TRUE(clientB.has_value());

    ASSERT_TRUE(clientA->Send(Bytes("a")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return listener->StrangerCount() == 1U;
    }));
    ASSERT_TRUE(clientB->Send(Bytes("b")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return listener->StrangerCount() == 2U;
    }));

    std::optional<UdpListenerPeer> peerB =
        listener->AcceptPeer(AddressOf(*clientB), 256U);
    ASSERT_TRUE(peerB.has_value());
    EXPECT_EQ(peerB->TryReceive(), Bytes("b"));
    EXPECT_EQ(listener->StrangerCount(), 1U);
    EXPECT_EQ(listener->NextStranger(), AddressOf(*clientA));
    std::optional<UdpListenerPeer> peerA = listener->AcceptPeer();
    ASSERT_TRUE(peerA.has_value());
    EXPECT_EQ(peerA->Remote(), AddressOf(*clientA));
}

TEST(UdpListener, PeerSendOversizeFailsAndCounts) {
    std::optional<UdpListener> listener = CreateUdpListener(UdpListenerConfig{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpoint> client = MakeClient(listener->LocalPort());
    ASSERT_TRUE(client.has_value());
    ASSERT_TRUE(client->Send(Bytes("knock")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return listener->HasStrangers();
    }));
    std::optional<UdpListenerPeer> peer = listener->AcceptPeer();
    ASSERT_TRUE(peer.has_value());

    const std::vector<std::uint8_t> over(kTransportMaximumDatagramBytes + 1U, 0xAB);
    EXPECT_FALSE(peer->Send(over));
    EXPECT_EQ(peer->Stats().datagramsDroppedOversize, 1U);
    EXPECT_EQ(peer->Stats().datagramsSent, 0U);
}

TEST(UdpListener, ListenerStatsPartitionExactly) {
    std::optional<UdpListener> listener = CreateUdpListener(UdpListenerConfig{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpoint> clientA = MakeClient(listener->LocalPort());
    std::optional<UdpTransportEndpoint> clientB = MakeClient(listener->LocalPort());
    ASSERT_TRUE(clientA.has_value());
    ASSERT_TRUE(clientB.has_value());

    ASSERT_TRUE(clientA->Send(Bytes("knock")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return listener->HasStrangers();
    }));
    std::optional<UdpListenerPeer> peer =
        listener->AcceptPeer(AddressOf(*clientA), 1U);
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(peer->TryReceive(), Bytes("knock"));

    ASSERT_TRUE(clientA->Send(Bytes("a1")));
    ASSERT_TRUE(clientA->Send(Bytes("a2")));
    ASSERT_TRUE(clientB->Send(Bytes("b1")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return listener->Stats().datagramsReceived == 4U;
    }));

    // knock held, a1 routed, a2 over the bound-1 queue, b1 held: every arrival in exactly
    // one bucket, summing to received.
    const UdpListenerStats stats = listener->Stats();
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
    EXPECT_EQ(peer->TryReceive(), Bytes("a1"));
    EXPECT_FALSE(peer->TryReceive().has_value());
}

TEST(UdpListener, PeerPendingCountsAreExact) {
    std::optional<UdpListener> listener = CreateUdpListener(UdpListenerConfig{});
    ASSERT_TRUE(listener.has_value());
    std::optional<UdpTransportEndpoint> client = MakeClient(listener->LocalPort());
    ASSERT_TRUE(client.has_value());
    ASSERT_TRUE(client->Send(Bytes("knock")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return listener->HasStrangers();
    }));
    std::optional<UdpListenerPeer> peer = listener->AcceptPeer();
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(peer->TryReceive(), Bytes("knock"));
    EXPECT_FALSE(peer->HasPending());
    EXPECT_EQ(peer->PendingCount(), 0U);

    ASSERT_TRUE(client->Send(Bytes("one")));
    ASSERT_TRUE(client->Send(Bytes("two")));
    ASSERT_TRUE(PollUntil([&] {
        listener->Update();
        return peer->PendingCount() == 2U;
    }));
    // Exact, where a connected endpoint would read the lower bound 1: peers hold no socket
    // backlog of their own, so the queue is all there is.
    EXPECT_TRUE(peer->HasPending());
    EXPECT_EQ(peer->PendingCount(), 2U);
    EXPECT_EQ(peer->TryReceive(), Bytes("one"));
    EXPECT_EQ(peer->PendingCount(), 1U);
    EXPECT_EQ(peer->TryReceive(), Bytes("two"));
    EXPECT_FALSE(peer->HasPending());
    EXPECT_EQ(peer->PendingCount(), 0U);
}

TEST(UdpListener, FactoryRefusesTakenPort) {
    std::optional<UdpListener> first = CreateUdpListener(UdpListenerConfig{});
    ASSERT_TRUE(first.has_value());
    EXPECT_NE(first->LocalPort(), 0U);
    UdpListenerConfig taken;
    taken.localPort = first->LocalPort();
    EXPECT_FALSE(CreateUdpListener(taken).has_value());
}

} // namespace
} // namespace UVE::Network::Tests
