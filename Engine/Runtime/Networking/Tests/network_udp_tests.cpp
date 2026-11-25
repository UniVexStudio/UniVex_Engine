// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_udp.h"

#include "network_rpc.h"
#include "network_session.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#if defined(_WIN32)
#    include <process.h>
#else
#    include <unistd.h>
#endif

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

// Fixed loopback ports are retained for the explicit bind/conflict tests below. The pair helper
// derives its ports from the test process instead: CTest launches each case as a separate process,
// so fixed ports make otherwise independent UDP tests race when the suite runs with -j.
inline constexpr std::uint16_t kUdpTestPortA = 41741U;
inline constexpr std::uint16_t kUdpTestPortB = 41742U;
inline constexpr std::uint16_t kUdpDynamicPortBase = 49152U;

[[nodiscard]] std::uint32_t CurrentProcessId() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(_getpid());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

[[nodiscard]] std::pair<std::uint16_t, std::uint16_t> DynamicPairPorts(
    const std::uint32_t attempt) noexcept {
    constexpr std::uint32_t kPortPairCount = 2500U;
    const std::uint32_t pairIndex =
        (CurrentProcessId() % kPortPairCount + attempt) % kPortPairCount;
    const std::uint16_t first = static_cast<std::uint16_t>(
        kUdpDynamicPortBase + pairIndex * 2U);
    return {first, static_cast<std::uint16_t>(first + 1U)};
}

[[nodiscard]] std::vector<std::uint8_t> Bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

[[nodiscard]] UdpTransportConfig Config(const std::uint16_t local,
                                              const std::uint16_t remote) {
    UdpTransportConfig config;
    config.localPort = local;
    config.remoteHost = "127.0.0.1";
    config.remotePort = remote;
    return config;
}

struct UdpPair final {
    UdpTransportEndpoint a;
    UdpTransportEndpoint b;
};

[[nodiscard]] std::optional<UdpPair> MakePair() {
    // A runner may already have a socket on the process-derived candidate, so retry a bounded
    // sequence of disjoint pairs instead of turning harmless port contention into a flaky test.
    for (std::uint32_t attempt = 0U; attempt < 64U; ++attempt) {
        const auto [portA, portB] = DynamicPairPorts(attempt);
        std::optional<UdpTransportEndpoint> a =
            CreateUdpTransportEndpoint(Config(portA, portB));
        if (!a.has_value()) {
            continue;
        }
        std::optional<UdpTransportEndpoint> b =
            CreateUdpTransportEndpoint(Config(portB, portA));
        if (b.has_value()) {
            return UdpPair{std::move(*a), std::move(*b)};
        }
    }
    return std::nullopt;
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

TEST(UdpTransport, PairExchangesDatagramsBothDirectionsInOrder) {
    std::optional<UdpPair> pair = MakePair();
    ASSERT_TRUE(pair.has_value());
    EXPECT_NE(pair->a.LocalPort(), 0U);
    EXPECT_NE(pair->b.LocalPort(), 0U);
    EXPECT_NE(pair->a.LocalPort(), pair->b.LocalPort());

    EXPECT_TRUE(pair->a.Send(Bytes("one")));
    EXPECT_TRUE(pair->a.Send(Bytes("two")));
    EXPECT_TRUE(pair->a.Send(Bytes("three")));
    EXPECT_TRUE(pair->b.Send(Bytes("back")));

    ASSERT_TRUE(PollUntil([&] { return pair->b.HasPending(); }));
    EXPECT_EQ(pair->b.TryReceive(), Bytes("one"));
    EXPECT_EQ(pair->b.TryReceive(), Bytes("two"));
    EXPECT_EQ(pair->b.TryReceive(), Bytes("three"));
    EXPECT_EQ(pair->a.TryReceive(), Bytes("back"));
    EXPECT_FALSE(pair->a.TryReceive().has_value());
    EXPECT_FALSE(pair->b.TryReceive().has_value());
}

TEST(UdpTransport, PendingFlagsTrackQueuedMail) {
    std::optional<UdpPair> pair = MakePair();
    ASSERT_TRUE(pair.has_value());
    EXPECT_FALSE(pair->a.HasPending());
    EXPECT_FALSE(pair->b.HasPending());
    EXPECT_EQ(pair->a.PendingCount(), 0U);
    EXPECT_EQ(pair->b.PendingCount(), 0U);

    EXPECT_TRUE(pair->a.Send(Bytes("first")));
    EXPECT_TRUE(pair->a.Send(Bytes("second")));
    ASSERT_TRUE(PollUntil([&] { return pair->b.HasPending(); }));
    // Nothing drained yet: an empty queue plus a visibly-full socket reads exactly one,
    // the documented lower bound - two wait, one is provable without receiving.
    EXPECT_EQ(pair->b.PendingCount(), 1U);

    EXPECT_EQ(pair->b.TryReceive(), Bytes("first"));
    // The first receive drained both into the queue and popped one: one queued, the
    // socket dry, so the count is exact from here on.
    EXPECT_TRUE(pair->b.HasPending());
    EXPECT_EQ(pair->b.PendingCount(), 1U);
    EXPECT_EQ(pair->b.TryReceive(), Bytes("second"));
    EXPECT_FALSE(pair->b.HasPending());
    EXPECT_EQ(pair->b.PendingCount(), 0U);
}

TEST(UdpTransport, OversizeSendFailsAndCounts) {
    std::optional<UdpPair> pair = MakePair();
    ASSERT_TRUE(pair.has_value());
    const std::vector<std::uint8_t> maximum(kTransportMaximumDatagramBytes, 0xAB);
    EXPECT_TRUE(pair->a.Send(maximum));
    const std::vector<std::uint8_t> over(kTransportMaximumDatagramBytes + 1U, 0xAB);
    EXPECT_FALSE(pair->a.Send(over));

    UdpTransportStats expected;
    expected.datagramsSent = 1U;
    expected.datagramsDroppedOversize = 1U;
    EXPECT_EQ(pair->a.Stats(), expected);

    ASSERT_TRUE(PollUntil([&] { return pair->b.HasPending(); }));
    EXPECT_EQ(pair->b.TryReceive(), maximum);
    EXPECT_EQ(pair->b.Stats().datagramsReceived, 1U);
}

TEST(UdpTransport, EmptyEndpointStaysSilent) {
    std::optional<UdpPair> pair = MakePair();
    ASSERT_TRUE(pair.has_value());
    EXPECT_FALSE(pair->a.TryReceive().has_value());
    EXPECT_FALSE(pair->b.TryReceive().has_value());
    EXPECT_FALSE(pair->a.HasPending());
    EXPECT_EQ(pair->b.PendingCount(), 0U);
    EXPECT_EQ(pair->a.Stats(), UdpTransportStats{});
}

TEST(UdpTransport, CopiesShareOneSocketAndMovedFromIsInert) {
    std::optional<UdpPair> pair = MakePair();
    ASSERT_TRUE(pair.has_value());
    UdpTransportEndpoint copy = pair->a;
    EXPECT_TRUE(copy.Send(Bytes("via-copy")));
    EXPECT_EQ(pair->a.Stats().datagramsSent, 1U);
    ASSERT_TRUE(PollUntil([&] { return pair->b.HasPending(); }));
    EXPECT_EQ(pair->b.TryReceive(), Bytes("via-copy"));

    UdpTransportEndpoint moved = std::move(copy);
    EXPECT_FALSE(copy.Send(Bytes("nope")));
    EXPECT_FALSE(copy.TryReceive().has_value());
    EXPECT_FALSE(copy.HasPending());
    EXPECT_EQ(copy.PendingCount(), 0U);
    EXPECT_EQ(copy.Stats(), UdpTransportStats{});
    EXPECT_EQ(copy.LocalPort(), 0U);
    EXPECT_NE(moved.LocalPort(), 0U);
}

TEST(UdpTransport, InvalidConfigsReturnNullopt) {
    UdpTransportConfig badHost = Config(kUdpTestPortA, kUdpTestPortB);
    badHost.remoteHost = "not-a-host";
    EXPECT_FALSE(CreateUdpTransportEndpoint(badHost).has_value());
    badHost.remoteHost = "999.1.1.1";
    EXPECT_FALSE(CreateUdpTransportEndpoint(badHost).has_value());
    badHost.remoteHost.clear();
    EXPECT_FALSE(CreateUdpTransportEndpoint(badHost).has_value());
    // Unresolvable names fail the same way invalid quads do (DNS, then nothing).
    badHost.remoteHost = "nonexistent.invalid";
    EXPECT_FALSE(CreateUdpTransportEndpoint(badHost).has_value());

    const UdpTransportConfig inUse = Config(kUdpTestPortA, kUdpTestPortB);
    std::optional<UdpTransportEndpoint> first = CreateUdpTransportEndpoint(inUse);
    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(CreateUdpTransportEndpoint(inUse).has_value());
}

TEST(UdpTransport, ResolveNumericQuadsWithoutTheNameService) {
    EXPECT_EQ(ResolveUdpHost("127.0.0.1"), 0x7F000001U);
    EXPECT_EQ(ResolveUdpHost("192.168.1.10"), 0xC0A8010AU);
    EXPECT_EQ(ResolveUdpHost("0.0.0.0"), 0U);
    EXPECT_EQ(ResolveUdpHost("255.255.255.255"), 0xFFFFFFFFU);
}

TEST(UdpTransport, ResolveLocalhostHitsLoopback) {
    // The one name every resolver answers without leaving the machine.
    EXPECT_EQ(ResolveUdpHost("localhost"), 0x7F000001U);
}

TEST(UdpTransport, ResolveUnresolvableFailsClosed) {
    EXPECT_FALSE(ResolveUdpHost("").has_value());
    EXPECT_FALSE(ResolveUdpHost("not a host!").has_value());
    EXPECT_FALSE(ResolveUdpHost("nonexistent.invalid").has_value());
    // IPv6-only answers are failures to an IPv4 transport, not fallbacks.
    EXPECT_FALSE(ResolveUdpHost("::1").has_value());
    EXPECT_FALSE(ResolveUdpHost("999.999.999.999").has_value());
}

TEST(UdpTransport, FactoryAcceptsHostnames) {
    // Port 9 (discard) has nobody listening, like the ephemeral test: UDP connects without
    // a handshake, so the factory proves resolve plus bind - "localhost" now builds.
    UdpTransportConfig config;
    config.remoteHost = "localhost";
    config.remotePort = 9U;
    const std::optional<UdpTransportEndpoint> endpoint =
        CreateUdpTransportEndpoint(config);
    ASSERT_TRUE(endpoint.has_value());
    EXPECT_NE(endpoint->LocalPort(), 0U);
}

TEST(UdpTransport, EphemeralPortBindsAndReports) {
    UdpTransportConfig config;
    config.remoteHost = "127.0.0.1";
    // Port 9 (discard) has nobody listening, which is fine: UDP connects without a
    // handshake, so the factory only proves the local bind plus the remote parses.
    config.remotePort = 9U;
    std::optional<UdpTransportEndpoint> endpoint = CreateUdpTransportEndpoint(config);
    ASSERT_TRUE(endpoint.has_value());
    EXPECT_NE(endpoint->LocalPort(), 0U);
}

TEST(UdpTransport, SessionHandshakesOverRealUdp) {
    std::optional<UdpPair> pair = MakePair();
    ASSERT_TRUE(pair.has_value());
    NetworkSession client(SessionRole::Client,
                             std::make_unique<UdpTransportEndpoint>(pair->a));
    NetworkSession server(SessionRole::Server,
                             std::make_unique<UdpTransportEndpoint>(pair->b));
    ASSERT_TRUE(client.Connect(0U));

    // The session core is unchanged - same pump, same handshake, now over the wire. The
    // clock crawls at 1ms a round with a 5s timeout, so the poll budget cannot trip it.
    std::uint64_t nowMs = 0U;
    const bool established = PollUntil([&] {
        client.Update(nowMs);
        server.Update(nowMs);
        ++nowMs;
        return client.State() == SessionState::Established &&
               server.State() == SessionState::Established;
    });
    ASSERT_TRUE(established);

    ASSERT_TRUE(client.SendData(Bytes("ping")));
    ASSERT_TRUE(server.SendData(Bytes("pong")));
    std::optional<std::vector<std::uint8_t>> toServer;
    std::optional<std::vector<std::uint8_t>> toClient;
    const bool exchanged = PollUntil([&] {
        client.Update(nowMs);
        server.Update(nowMs);
        ++nowMs;
        if (!toServer.has_value()) {
            toServer = server.TryReceiveData();
        }
        if (!toClient.has_value()) {
            toClient = client.TryReceiveData();
        }
        return toServer.has_value() && toClient.has_value();
    });
    ASSERT_TRUE(exchanged);
    EXPECT_EQ(toServer, Bytes("ping"));
    EXPECT_EQ(toClient, Bytes("pong"));
}

TEST(UdpTransport, SessionRedialsOverRealUdp) {
    std::optional<UdpPair> pair = MakePair();
    ASSERT_TRUE(pair.has_value());
    NetworkSession client(SessionRole::Client,
                             std::make_unique<UdpTransportEndpoint>(pair->a));
    NetworkSession server(SessionRole::Server,
                             std::make_unique<UdpTransportEndpoint>(pair->b));
    constexpr std::uint64_t kToken = 0x0102030405060708U;
    client.SetReconnectConfig(SessionReconnectConfig{50U, 20U});
    ASSERT_TRUE(client.Connect(0U, kToken));

    // Same crawl-the-clock poll as the handshake test: real sockets, fake session time.
    std::uint64_t nowMs = 0U;
    const bool established = PollUntil([&] {
        client.Update(nowMs);
        server.Update(nowMs);
        ++nowMs;
        return client.State() == SessionState::Established &&
               server.State() == SessionState::Established;
    });
    ASSERT_TRUE(established);

    // Jump the clock past the silence timeout with the wire idle: both sides drop, and
    // the client starts redialing on its own.
    nowMs += kDefaultSessionTimeoutMs;
    client.Update(nowMs);
    server.Update(nowMs);
    ASSERT_EQ(client.State(), SessionState::Reconnecting);
    ASSERT_EQ(server.State(), SessionState::Disconnected);

    const bool resumed = PollUntil([&] {
        client.Update(nowMs);
        server.Update(nowMs);
        ++nowMs;
        return client.State() == SessionState::Established;
    });
    ASSERT_TRUE(resumed);
    EXPECT_TRUE(server.DidResume());
    EXPECT_TRUE(client.DidResume());
    EXPECT_EQ(server.SessionToken(), kToken);
    EXPECT_EQ(client.ReconnectAttempts(), 1U);
}

TEST(UdpTransport, RpcRoundTripsOverRealUdp) {
    std::optional<UdpPair> pair = MakePair();
    ASSERT_TRUE(pair.has_value());
    NetworkSession clientSession(SessionRole::Client,
                                    std::make_unique<UdpTransportEndpoint>(pair->a));
    NetworkSession serverSession(SessionRole::Server,
                                    std::make_unique<UdpTransportEndpoint>(pair->b));
    NetworkRpc client(std::move(clientSession));
    NetworkRpc server(std::move(serverSession));
    ASSERT_TRUE(server.RegisterMethod("add", [](const std::span<const Blueprint::Value> args) {
        const std::int64_t lhs = std::get<std::int64_t>(args[0]);
        const std::int64_t rhs = std::get<std::int64_t>(args[1]);
        return RpcOk(Blueprint::Value{lhs + rhs});
    }));
    ASSERT_TRUE(client.Session().Connect(0U));

    // Same crawl-the-clock poll as the handshake test: real sockets, fake session time.
    std::uint64_t nowMs = 0U;
    const bool established = PollUntil([&] {
        client.Update(nowMs);
        server.Update(nowMs);
        ++nowMs;
        return client.Session().State() == SessionState::Established &&
               server.Session().State() == SessionState::Established;
    });
    ASSERT_TRUE(established);

    // The RPC layer is unchanged - same calls, same framing, now over the wire.
    std::optional<RpcOutcome> result;
    const RpcCallId call =
        client.Call("add",
                       {Blueprint::Value{std::int64_t{20}}, Blueprint::Value{std::int64_t{22}}},
                       [&](const RpcCallId /*id*/, const RpcOutcome& outcome) {
                           result = outcome;
                       });
    ASSERT_NE(call, kInvalidRpcCallId);
    const bool answered = PollUntil([&] {
        client.Update(nowMs);
        server.Update(nowMs);
        ++nowMs;
        return result.has_value();
    });
    ASSERT_TRUE(answered);
    EXPECT_TRUE(result->ok);
    EXPECT_EQ(result->value, Blueprint::Value{std::int64_t{42}});
}

} // namespace
} // namespace UVE::Network::Tests
