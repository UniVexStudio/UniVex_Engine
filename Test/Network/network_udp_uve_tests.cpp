// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_udp_uve.h"

#include "uve/network/network_rpc_uve.h"
#include "uve/network/network_session_uve.h"

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

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

// Fixed loopback ports: no ephemeral chicken-and-egg (each side names the other up front),
// and nothing here runs in parallel with itself, so nothing else can be squatting on them.
inline constexpr std::uint16_t kUdpTestPortAUVE = 41741U;
inline constexpr std::uint16_t kUdpTestPortBUVE = 41742U;

[[nodiscard]] std::vector<std::uint8_t> BytesUVE(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

[[nodiscard]] UdpTransportConfigUVE ConfigUVE(const std::uint16_t local,
                                              const std::uint16_t remote) {
    UdpTransportConfigUVE config;
    config.localPort = local;
    config.remoteHost = "127.0.0.1";
    config.remotePort = remote;
    return config;
}

struct UdpPairUVE final {
    UdpTransportEndpointUVE a;
    UdpTransportEndpointUVE b;
};

[[nodiscard]] std::optional<UdpPairUVE> MakePairUVE() {
    std::optional<UdpTransportEndpointUVE> a =
        CreateUdpTransportEndpointUVE(ConfigUVE(kUdpTestPortAUVE, kUdpTestPortBUVE));
    std::optional<UdpTransportEndpointUVE> b =
        CreateUdpTransportEndpointUVE(ConfigUVE(kUdpTestPortBUVE, kUdpTestPortAUVE));
    if (!a.has_value() || !b.has_value()) {
        return std::nullopt;
    }
    return UdpPairUVE{std::move(*a), std::move(*b)};
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

TEST(UdpTransportUVE, PairExchangesDatagramsBothDirectionsInOrder) {
    std::optional<UdpPairUVE> pair = MakePairUVE();
    ASSERT_TRUE(pair.has_value());
    EXPECT_EQ(pair->a.LocalPortUVE(), kUdpTestPortAUVE);
    EXPECT_EQ(pair->b.LocalPortUVE(), kUdpTestPortBUVE);

    EXPECT_TRUE(pair->a.SendUVE(BytesUVE("one")));
    EXPECT_TRUE(pair->a.SendUVE(BytesUVE("two")));
    EXPECT_TRUE(pair->a.SendUVE(BytesUVE("three")));
    EXPECT_TRUE(pair->b.SendUVE(BytesUVE("back")));

    ASSERT_TRUE(PollUntilUVE([&] { return pair->b.HasPendingUVE(); }));
    EXPECT_EQ(pair->b.TryReceiveUVE(), BytesUVE("one"));
    EXPECT_EQ(pair->b.TryReceiveUVE(), BytesUVE("two"));
    EXPECT_EQ(pair->b.TryReceiveUVE(), BytesUVE("three"));
    EXPECT_EQ(pair->a.TryReceiveUVE(), BytesUVE("back"));
    EXPECT_FALSE(pair->a.TryReceiveUVE().has_value());
    EXPECT_FALSE(pair->b.TryReceiveUVE().has_value());
}

TEST(UdpTransportUVE, PendingFlagsTrackQueuedMail) {
    std::optional<UdpPairUVE> pair = MakePairUVE();
    ASSERT_TRUE(pair.has_value());
    EXPECT_FALSE(pair->a.HasPendingUVE());
    EXPECT_FALSE(pair->b.HasPendingUVE());
    EXPECT_EQ(pair->a.PendingCountUVE(), 0U);
    EXPECT_EQ(pair->b.PendingCountUVE(), 0U);

    EXPECT_TRUE(pair->a.SendUVE(BytesUVE("first")));
    EXPECT_TRUE(pair->a.SendUVE(BytesUVE("second")));
    ASSERT_TRUE(PollUntilUVE([&] { return pair->b.HasPendingUVE(); }));
    // Nothing drained yet: an empty queue plus a visibly-full socket reads exactly one,
    // the documented lower bound - two wait, one is provable without receiving.
    EXPECT_EQ(pair->b.PendingCountUVE(), 1U);

    EXPECT_EQ(pair->b.TryReceiveUVE(), BytesUVE("first"));
    // The first receive drained both into the queue and popped one: one queued, the
    // socket dry, so the count is exact from here on.
    EXPECT_TRUE(pair->b.HasPendingUVE());
    EXPECT_EQ(pair->b.PendingCountUVE(), 1U);
    EXPECT_EQ(pair->b.TryReceiveUVE(), BytesUVE("second"));
    EXPECT_FALSE(pair->b.HasPendingUVE());
    EXPECT_EQ(pair->b.PendingCountUVE(), 0U);
}

TEST(UdpTransportUVE, OversizeSendFailsAndCounts) {
    std::optional<UdpPairUVE> pair = MakePairUVE();
    ASSERT_TRUE(pair.has_value());
    const std::vector<std::uint8_t> maximum(kTransportMaximumDatagramBytesUVE, 0xAB);
    EXPECT_TRUE(pair->a.SendUVE(maximum));
    const std::vector<std::uint8_t> over(kTransportMaximumDatagramBytesUVE + 1U, 0xAB);
    EXPECT_FALSE(pair->a.SendUVE(over));

    UdpTransportStatsUVE expected;
    expected.datagramsSent = 1U;
    expected.datagramsDroppedOversize = 1U;
    EXPECT_EQ(pair->a.StatsUVE(), expected);

    ASSERT_TRUE(PollUntilUVE([&] { return pair->b.HasPendingUVE(); }));
    EXPECT_EQ(pair->b.TryReceiveUVE(), maximum);
    EXPECT_EQ(pair->b.StatsUVE().datagramsReceived, 1U);
}

TEST(UdpTransportUVE, EmptyEndpointStaysSilent) {
    std::optional<UdpPairUVE> pair = MakePairUVE();
    ASSERT_TRUE(pair.has_value());
    EXPECT_FALSE(pair->a.TryReceiveUVE().has_value());
    EXPECT_FALSE(pair->b.TryReceiveUVE().has_value());
    EXPECT_FALSE(pair->a.HasPendingUVE());
    EXPECT_EQ(pair->b.PendingCountUVE(), 0U);
    EXPECT_EQ(pair->a.StatsUVE(), UdpTransportStatsUVE{});
}

TEST(UdpTransportUVE, CopiesShareOneSocketAndMovedFromIsInert) {
    std::optional<UdpPairUVE> pair = MakePairUVE();
    ASSERT_TRUE(pair.has_value());
    UdpTransportEndpointUVE copy = pair->a;
    EXPECT_TRUE(copy.SendUVE(BytesUVE("via-copy")));
    EXPECT_EQ(pair->a.StatsUVE().datagramsSent, 1U);
    ASSERT_TRUE(PollUntilUVE([&] { return pair->b.HasPendingUVE(); }));
    EXPECT_EQ(pair->b.TryReceiveUVE(), BytesUVE("via-copy"));

    UdpTransportEndpointUVE moved = std::move(copy);
    EXPECT_FALSE(copy.SendUVE(BytesUVE("nope")));
    EXPECT_FALSE(copy.TryReceiveUVE().has_value());
    EXPECT_FALSE(copy.HasPendingUVE());
    EXPECT_EQ(copy.PendingCountUVE(), 0U);
    EXPECT_EQ(copy.StatsUVE(), UdpTransportStatsUVE{});
    EXPECT_EQ(copy.LocalPortUVE(), 0U);
    EXPECT_EQ(moved.LocalPortUVE(), kUdpTestPortAUVE);
}

TEST(UdpTransportUVE, InvalidConfigsReturnNullopt) {
    UdpTransportConfigUVE badHost = ConfigUVE(kUdpTestPortAUVE, kUdpTestPortBUVE);
    badHost.remoteHost = "not-a-host";
    EXPECT_FALSE(CreateUdpTransportEndpointUVE(badHost).has_value());
    badHost.remoteHost = "999.1.1.1";
    EXPECT_FALSE(CreateUdpTransportEndpointUVE(badHost).has_value());
    badHost.remoteHost.clear();
    EXPECT_FALSE(CreateUdpTransportEndpointUVE(badHost).has_value());
    // Unresolvable names fail the same way invalid quads do (DNS, then nothing).
    badHost.remoteHost = "nonexistent.invalid";
    EXPECT_FALSE(CreateUdpTransportEndpointUVE(badHost).has_value());

    const UdpTransportConfigUVE inUse = ConfigUVE(kUdpTestPortAUVE, kUdpTestPortBUVE);
    std::optional<UdpTransportEndpointUVE> first = CreateUdpTransportEndpointUVE(inUse);
    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(CreateUdpTransportEndpointUVE(inUse).has_value());
}

TEST(UdpTransportUVE, ResolveNumericQuadsWithoutTheNameService) {
    EXPECT_EQ(ResolveUdpHostUVE("127.0.0.1"), 0x7F000001U);
    EXPECT_EQ(ResolveUdpHostUVE("192.168.1.10"), 0xC0A8010AU);
    EXPECT_EQ(ResolveUdpHostUVE("0.0.0.0"), 0U);
    EXPECT_EQ(ResolveUdpHostUVE("255.255.255.255"), 0xFFFFFFFFU);
}

TEST(UdpTransportUVE, ResolveLocalhostHitsLoopback) {
    // The one name every resolver answers without leaving the machine.
    EXPECT_EQ(ResolveUdpHostUVE("localhost"), 0x7F000001U);
}

TEST(UdpTransportUVE, ResolveUnresolvableFailsClosed) {
    EXPECT_FALSE(ResolveUdpHostUVE("").has_value());
    EXPECT_FALSE(ResolveUdpHostUVE("not a host!").has_value());
    EXPECT_FALSE(ResolveUdpHostUVE("nonexistent.invalid").has_value());
    // IPv6-only answers are failures to an IPv4 transport, not fallbacks.
    EXPECT_FALSE(ResolveUdpHostUVE("::1").has_value());
    EXPECT_FALSE(ResolveUdpHostUVE("999.999.999.999").has_value());
}

TEST(UdpTransportUVE, FactoryAcceptsHostnames) {
    // Port 9 (discard) has nobody listening, like the ephemeral test: UDP connects without
    // a handshake, so the factory proves resolve plus bind - "localhost" now builds.
    UdpTransportConfigUVE config;
    config.remoteHost = "localhost";
    config.remotePort = 9U;
    const std::optional<UdpTransportEndpointUVE> endpoint =
        CreateUdpTransportEndpointUVE(config);
    ASSERT_TRUE(endpoint.has_value());
    EXPECT_NE(endpoint->LocalPortUVE(), 0U);
}

TEST(UdpTransportUVE, EphemeralPortBindsAndReports) {
    UdpTransportConfigUVE config;
    config.remoteHost = "127.0.0.1";
    // Port 9 (discard) has nobody listening, which is fine: UDP connects without a
    // handshake, so the factory only proves the local bind plus the remote parses.
    config.remotePort = 9U;
    std::optional<UdpTransportEndpointUVE> endpoint = CreateUdpTransportEndpointUVE(config);
    ASSERT_TRUE(endpoint.has_value());
    EXPECT_NE(endpoint->LocalPortUVE(), 0U);
}

TEST(UdpTransportUVE, SessionHandshakesOverRealUdp) {
    std::optional<UdpPairUVE> pair = MakePairUVE();
    ASSERT_TRUE(pair.has_value());
    NetworkSessionUVE client(SessionRoleUVE::Client,
                             std::make_unique<UdpTransportEndpointUVE>(pair->a));
    NetworkSessionUVE server(SessionRoleUVE::Server,
                             std::make_unique<UdpTransportEndpointUVE>(pair->b));
    ASSERT_TRUE(client.ConnectUVE(0U));

    // The session core is unchanged - same pump, same handshake, now over the wire. The
    // clock crawls at 1ms a round with a 5s timeout, so the poll budget cannot trip it.
    std::uint64_t nowMs = 0U;
    const bool established = PollUntilUVE([&] {
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
        ++nowMs;
        return client.StateUVE() == SessionStateUVE::Established &&
               server.StateUVE() == SessionStateUVE::Established;
    });
    ASSERT_TRUE(established);

    ASSERT_TRUE(client.SendDataUVE(BytesUVE("ping")));
    ASSERT_TRUE(server.SendDataUVE(BytesUVE("pong")));
    std::optional<std::vector<std::uint8_t>> toServer;
    std::optional<std::vector<std::uint8_t>> toClient;
    const bool exchanged = PollUntilUVE([&] {
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
        ++nowMs;
        if (!toServer.has_value()) {
            toServer = server.TryReceiveDataUVE();
        }
        if (!toClient.has_value()) {
            toClient = client.TryReceiveDataUVE();
        }
        return toServer.has_value() && toClient.has_value();
    });
    ASSERT_TRUE(exchanged);
    EXPECT_EQ(toServer, BytesUVE("ping"));
    EXPECT_EQ(toClient, BytesUVE("pong"));
}

TEST(UdpTransportUVE, SessionRedialsOverRealUdp) {
    std::optional<UdpPairUVE> pair = MakePairUVE();
    ASSERT_TRUE(pair.has_value());
    NetworkSessionUVE client(SessionRoleUVE::Client,
                             std::make_unique<UdpTransportEndpointUVE>(pair->a));
    NetworkSessionUVE server(SessionRoleUVE::Server,
                             std::make_unique<UdpTransportEndpointUVE>(pair->b));
    constexpr std::uint64_t kToken = 0x0102030405060708U;
    client.SetReconnectConfigUVE(SessionReconnectConfigUVE{50U, 20U});
    ASSERT_TRUE(client.ConnectUVE(0U, kToken));

    // Same crawl-the-clock poll as the handshake test: real sockets, fake session time.
    std::uint64_t nowMs = 0U;
    const bool established = PollUntilUVE([&] {
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
        ++nowMs;
        return client.StateUVE() == SessionStateUVE::Established &&
               server.StateUVE() == SessionStateUVE::Established;
    });
    ASSERT_TRUE(established);

    // Jump the clock past the silence timeout with the wire idle: both sides drop, and
    // the client starts redialing on its own.
    nowMs += kDefaultSessionTimeoutMsUVE;
    client.UpdateUVE(nowMs);
    server.UpdateUVE(nowMs);
    ASSERT_EQ(client.StateUVE(), SessionStateUVE::Reconnecting);
    ASSERT_EQ(server.StateUVE(), SessionStateUVE::Disconnected);

    const bool resumed = PollUntilUVE([&] {
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
        ++nowMs;
        return client.StateUVE() == SessionStateUVE::Established;
    });
    ASSERT_TRUE(resumed);
    EXPECT_TRUE(server.DidResumeUVE());
    EXPECT_TRUE(client.DidResumeUVE());
    EXPECT_EQ(server.SessionTokenUVE(), kToken);
    EXPECT_EQ(client.ReconnectAttemptsUVE(), 1U);
}

TEST(UdpTransportUVE, RpcRoundTripsOverRealUdp) {
    std::optional<UdpPairUVE> pair = MakePairUVE();
    ASSERT_TRUE(pair.has_value());
    NetworkSessionUVE clientSession(SessionRoleUVE::Client,
                                    std::make_unique<UdpTransportEndpointUVE>(pair->a));
    NetworkSessionUVE serverSession(SessionRoleUVE::Server,
                                    std::make_unique<UdpTransportEndpointUVE>(pair->b));
    NetworkRpcUVE client(std::move(clientSession));
    NetworkRpcUVE server(std::move(serverSession));
    ASSERT_TRUE(server.RegisterMethodUVE("add", [](const std::span<const UVScript::ValueUVE> args) {
        const std::int64_t lhs = std::get<std::int64_t>(args[0]);
        const std::int64_t rhs = std::get<std::int64_t>(args[1]);
        return RpcOkUVE(UVScript::ValueUVE{lhs + rhs});
    }));
    ASSERT_TRUE(client.SessionUVE().ConnectUVE(0U));

    // Same crawl-the-clock poll as the handshake test: real sockets, fake session time.
    std::uint64_t nowMs = 0U;
    const bool established = PollUntilUVE([&] {
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
        ++nowMs;
        return client.SessionUVE().StateUVE() == SessionStateUVE::Established &&
               server.SessionUVE().StateUVE() == SessionStateUVE::Established;
    });
    ASSERT_TRUE(established);

    // The RPC layer is unchanged - same calls, same framing, now over the wire.
    std::optional<RpcOutcomeUVE> result;
    const RpcCallIdUVE call =
        client.CallUVE("add",
                       {UVScript::ValueUVE{std::int64_t{20}}, UVScript::ValueUVE{std::int64_t{22}}},
                       [&](const RpcCallIdUVE /*id*/, const RpcOutcomeUVE& outcome) {
                           result = outcome;
                       });
    ASSERT_NE(call, kInvalidRpcCallIdUVE);
    const bool answered = PollUntilUVE([&] {
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
        ++nowMs;
        return result.has_value();
    });
    ASSERT_TRUE(answered);
    EXPECT_TRUE(result->ok);
    EXPECT_EQ(result->value, UVScript::ValueUVE{std::int64_t{42}});
}

} // namespace
} // namespace UVE::Network::Tests
