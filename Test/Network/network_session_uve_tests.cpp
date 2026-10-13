// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_session_uve.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

[[nodiscard]] std::vector<std::uint8_t> BytesUVE(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

[[nodiscard]] std::vector<std::uint8_t> TaggedUVE(const std::uint8_t tag, const std::string& text) {
    std::vector<std::uint8_t> datagram{tag};
    for (const std::uint8_t byte : BytesUVE(text)) {
        datagram.push_back(byte);
    }
    return datagram;
}

struct SessionHarnessUVE final {
    LoopbackTransportPairUVE link;
    NetworkSessionUVE client;
    NetworkSessionUVE server;

    void UpdateBothUVE(const std::uint64_t nowMs) {
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
    }

    // A handshake needs two pump rounds: the first carries Hello to the server and queues the
    // Welcome, the second delivers it to the client.
    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        if (!client.ConnectUVE(nowMs)) {
            return false;
        }
        UpdateBothUVE(nowMs);
        UpdateBothUVE(nowMs);
        return client.StateUVE() == SessionStateUVE::Established &&
               server.StateUVE() == SessionStateUVE::Established;
    }
};

[[nodiscard]] SessionHarnessUVE MakeSessionsUVE(
    const std::uint64_t timeoutMs = kDefaultSessionTimeoutMsUVE) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(64U);
    NetworkSessionUVE client(SessionRoleUVE::Client,
                             std::make_unique<LoopbackEndpointUVE>(pair.a), timeoutMs);
    NetworkSessionUVE server(SessionRoleUVE::Server,
                             std::make_unique<LoopbackEndpointUVE>(pair.b), timeoutMs);
    return SessionHarnessUVE{std::move(pair), std::move(client), std::move(server)};
}

TEST(NetworkSessionUVETest, Handshake_ClientAndServerReachEstablished) {
    SessionHarnessUVE harness = MakeSessionsUVE();

    ASSERT_TRUE(harness.client.ConnectUVE(1000U));
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Connecting);
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);

    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Established);
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Established);
    EXPECT_EQ(harness.client.CloseReasonUVE(), SessionCloseReasonUVE::None);
    EXPECT_EQ(harness.server.CloseReasonUVE(), SessionCloseReasonUVE::None);
}

TEST(NetworkSessionUVETest, RoleRules_RejectInvalidTransitions) {
    SessionHarnessUVE harness = MakeSessionsUVE();

    EXPECT_FALSE(harness.server.ConnectUVE(1000U));
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_FALSE(harness.client.DisconnectUVE());
    EXPECT_FALSE(harness.client.SendDataUVE(BytesUVE("early")));

    ASSERT_TRUE(harness.client.ConnectUVE(1000U));
    EXPECT_FALSE(harness.client.ConnectUVE(1000U));
    EXPECT_FALSE(harness.client.SendDataUVE(BytesUVE("connecting")));
}

TEST(NetworkSessionUVETest, DataFlowsBothWaysAfterHandshake) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_EQ(harness.client.StateUVE(), SessionStateUVE::Established);

    ASSERT_TRUE(harness.client.SendDataUVE(BytesUVE("c-to-s")));
    ASSERT_TRUE(harness.server.SendDataUVE(BytesUVE("s-to-c")));
    harness.UpdateBothUVE(1000U);

    EXPECT_EQ(harness.server.TryReceiveDataUVE(),
              std::optional<std::vector<std::uint8_t>>(BytesUVE("c-to-s")));
    EXPECT_EQ(harness.client.TryReceiveDataUVE(),
              std::optional<std::vector<std::uint8_t>>(BytesUVE("s-to-c")));
    EXPECT_FALSE(harness.client.TryReceiveDataUVE().has_value());
}

TEST(NetworkSessionUVETest, Disconnect_ByeClosesBothSidesAndClearsQueuedData) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.client.SendDataUVE(BytesUVE("unread")));

    EXPECT_TRUE(harness.client.DisconnectUVE());
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_EQ(harness.client.CloseReasonUVE(), SessionCloseReasonUVE::LocalDisconnect);

    harness.server.UpdateUVE(1000U);
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_EQ(harness.server.CloseReasonUVE(), SessionCloseReasonUVE::RemoteDisconnect);
    EXPECT_FALSE(harness.server.TryReceiveDataUVE().has_value());
}

TEST(NetworkSessionUVETest, Timeout_ConnectingWithoutWelcome) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.client.ConnectUVE(1000U));

    harness.client.UpdateUVE(5999U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Connecting);

    harness.client.UpdateUVE(6000U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_EQ(harness.client.CloseReasonUVE(), SessionCloseReasonUVE::Timeout);
}

TEST(NetworkSessionUVETest, Timeout_EstablishedGoesQuiet) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    harness.UpdateBothUVE(5999U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Established);
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Established);

    harness.UpdateBothUVE(6000U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_EQ(harness.client.CloseReasonUVE(), SessionCloseReasonUVE::Timeout);
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_EQ(harness.server.CloseReasonUVE(), SessionCloseReasonUVE::Timeout);
}

TEST(NetworkSessionUVETest, TrafficResetsTheTimeoutPerDirection) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    // Only inbound datagrams refresh a session's clock: each side must hear the other.
    ASSERT_TRUE(harness.client.SendDataUVE(BytesUVE("c1")));
    ASSERT_TRUE(harness.server.SendDataUVE(BytesUVE("s1")));
    harness.UpdateBothUVE(5000U);
    harness.UpdateBothUVE(9999U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Established);
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Established);

    harness.UpdateBothUVE(10000U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Disconnected);
}

TEST(NetworkSessionUVETest, VersionMismatch_ServerAnswersByeAndStaysDisconnected) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    const std::vector<std::uint8_t> badHello{
        static_cast<std::uint8_t>(SessionMessageTagUVE::Hello), 0x09U, 0x00U};
    ASSERT_TRUE(harness.link.a.SendUVE(badHello));

    harness.server.UpdateUVE(1000U);
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_EQ(harness.server.CloseReasonUVE(), SessionCloseReasonUVE::None);

    const std::vector<std::uint8_t> expectedBye{
        static_cast<std::uint8_t>(SessionMessageTagUVE::Bye)};
    EXPECT_EQ(harness.link.a.TryReceiveUVE(),
              std::optional<std::vector<std::uint8_t>>(expectedBye));
}

TEST(NetworkSessionUVETest, GarbageAndEarlyData_AreIgnored) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.link.a.SendUVE(BytesUVE("no-tag-here")));
    ASSERT_TRUE(harness.link.a.SendUVE(std::vector<std::uint8_t>{}));
    ASSERT_TRUE(harness.link.a.SendUVE(
        TaggedUVE(static_cast<std::uint8_t>(SessionMessageTagUVE::Data), "early-data")));

    harness.server.UpdateUVE(1000U);
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_FALSE(harness.server.TryReceiveDataUVE().has_value());

    ASSERT_TRUE(harness.EstablishUVE(2000U));
    ASSERT_EQ(harness.server.StateUVE(), SessionStateUVE::Established);
    ASSERT_TRUE(harness.link.a.SendUVE(TaggedUVE(0xFFU, "unknown-tag")));
    harness.server.UpdateUVE(2000U);
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Established);
    EXPECT_FALSE(harness.server.TryReceiveDataUVE().has_value());
}

TEST(NetworkSessionUVETest, DataPayload_BoundedOneByteUnderTheDatagramCap) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    const std::vector<std::uint8_t> maxed(kSessionMaximumPayloadBytesUVE, 0xCDU);
    const std::vector<std::uint8_t> tooBig(kSessionMaximumPayloadBytesUVE + 1U, 0xCDU);
    ASSERT_TRUE(harness.client.SendDataUVE(maxed));
    EXPECT_FALSE(harness.client.SendDataUVE(tooBig));

    harness.server.UpdateUVE(1000U);
    const std::optional<std::vector<std::uint8_t>> received = harness.server.TryReceiveDataUVE();
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(*received, maxed);
    EXPECT_FALSE(harness.server.TryReceiveDataUVE().has_value());
}

// Byte-distinct across all eight token bytes, so a byte-order slip anywhere in the Hello
// framing cannot hide.
inline constexpr std::uint64_t kTestSessionTokenUVE = 0x0102030405060708U;

// Establishes with a token, the way EstablishUVE does anonymously: two pump rounds carry
// Hello to the server and the Welcome back.
[[nodiscard]] bool EstablishWithTokenUVE(SessionHarnessUVE& harness, const std::uint64_t nowMs,
                                         const std::uint64_t token) {
    if (!harness.client.ConnectUVE(nowMs, token)) {
        return false;
    }
    harness.UpdateBothUVE(nowMs);
    harness.UpdateBothUVE(nowMs);
    return harness.client.StateUVE() == SessionStateUVE::Established &&
           harness.server.StateUVE() == SessionStateUVE::Established;
}

TEST(NetworkSessionUVETest, Token_ClientHelloBindsItOnBothSides) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(EstablishWithTokenUVE(harness, 1000U, kTestSessionTokenUVE));

    EXPECT_EQ(harness.client.SessionTokenUVE(), kTestSessionTokenUVE);
    EXPECT_EQ(harness.server.SessionTokenUVE(), kTestSessionTokenUVE);
    EXPECT_FALSE(harness.client.DidResumeUVE());
    EXPECT_FALSE(harness.server.DidResumeUVE());
    EXPECT_EQ(harness.client.ReconnectAttemptsUVE(), 0U);
}

TEST(NetworkSessionUVETest, Resume_ReturningTokenIsRecognized) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(EstablishWithTokenUVE(harness, 1000U, kTestSessionTokenUVE));
    harness.client.SetReconnectConfigUVE(SessionReconnectConfigUVE{100U, 10U});

    // Silence past the timeout drops both sides; the client starts redialing instead of
    // disconnecting, and reports no closure while it still tries.
    harness.UpdateBothUVE(6000U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Reconnecting);
    EXPECT_EQ(harness.client.CloseReasonUVE(), SessionCloseReasonUVE::None);
    EXPECT_EQ(harness.client.ReconnectAttemptsUVE(), 0U);
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_EQ(harness.server.CloseReasonUVE(), SessionCloseReasonUVE::Timeout);

    // The first due attempt redials; the server accepts in the same pump and recognizes
    // the returning token, while the Welcome is still in flight to the client.
    harness.UpdateBothUVE(6100U);
    EXPECT_EQ(harness.client.ReconnectAttemptsUVE(), 1U);
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Established);
    EXPECT_EQ(harness.server.SessionTokenUVE(), kTestSessionTokenUVE);
    EXPECT_TRUE(harness.server.DidResumeUVE());

    harness.UpdateBothUVE(6100U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Established);
    EXPECT_TRUE(harness.client.DidResumeUVE());
    // The landing attempt stays readable: the redial succeeded "1 of 10".
    EXPECT_EQ(harness.client.ReconnectAttemptsUVE(), 1U);
}

TEST(NetworkSessionUVETest, Resume_AnonymousTokenNeverMatches) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    harness.client.SetReconnectConfigUVE(SessionReconnectConfigUVE{100U, 10U});

    harness.UpdateBothUVE(6000U);
    ASSERT_EQ(harness.client.StateUVE(), SessionStateUVE::Reconnecting);

    harness.UpdateBothUVE(6100U);
    harness.UpdateBothUVE(6100U);
    ASSERT_EQ(harness.client.StateUVE(), SessionStateUVE::Established);
    // The redial itself succeeded, so the client reports a resume - but the server bound
    // an anonymous token, which never matches anything, not even itself.
    EXPECT_TRUE(harness.client.DidResumeUVE());
    EXPECT_FALSE(harness.server.DidResumeUVE());
    EXPECT_EQ(harness.server.SessionTokenUVE(), kAnonymousSessionTokenUVE);
}

TEST(NetworkSessionUVETest, Redial_GivesUpAfterMaxAttempts) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(EstablishWithTokenUVE(harness, 1000U, kTestSessionTokenUVE));
    harness.client.SetReconnectConfigUVE(SessionReconnectConfigUVE{100U, 3U});

    // The server is never pumped again: every Hello lands in an unread queue.
    harness.client.UpdateUVE(6000U);
    ASSERT_EQ(harness.client.StateUVE(), SessionStateUVE::Reconnecting);

    harness.client.UpdateUVE(6099U);
    EXPECT_EQ(harness.client.ReconnectAttemptsUVE(), 0U);
    harness.client.UpdateUVE(6100U);
    EXPECT_EQ(harness.client.ReconnectAttemptsUVE(), 1U);
    harness.client.UpdateUVE(6200U);
    EXPECT_EQ(harness.client.ReconnectAttemptsUVE(), 2U);
    harness.client.UpdateUVE(6300U);
    EXPECT_EQ(harness.client.ReconnectAttemptsUVE(), 3U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Reconnecting);

    // Three is the budget: the next pump gives up without firing a fourth, and the frozen
    // count proves no further Hello ever goes out.
    harness.client.UpdateUVE(6400U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_EQ(harness.client.CloseReasonUVE(), SessionCloseReasonUVE::ReconnectFailed);
    harness.client.UpdateUVE(99999U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_EQ(harness.client.ReconnectAttemptsUVE(), 3U);
}

TEST(NetworkSessionUVETest, Redial_LocalDisconnectNeverRetries) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(EstablishWithTokenUVE(harness, 1000U, kTestSessionTokenUVE));
    harness.client.SetReconnectConfigUVE(SessionReconnectConfigUVE{100U, 10U});

    ASSERT_TRUE(harness.client.DisconnectUVE());
    harness.client.UpdateUVE(99999U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_EQ(harness.client.CloseReasonUVE(), SessionCloseReasonUVE::LocalDisconnect);
    EXPECT_EQ(harness.client.ReconnectAttemptsUVE(), 0U);
}

TEST(NetworkSessionUVETest, Redial_ByeDuringRedialDisconnectsImmediately) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(EstablishWithTokenUVE(harness, 1000U, kTestSessionTokenUVE));
    harness.client.SetReconnectConfigUVE(SessionReconnectConfigUVE{1000U, 10U});

    harness.client.UpdateUVE(6000U);
    ASSERT_EQ(harness.client.StateUVE(), SessionStateUVE::Reconnecting);
    // Reconnecting counts as live: dialing over a redial is refused, hang up first.
    EXPECT_FALSE(harness.client.ConnectUVE(6000U));

    // A refusal mid-redial (the version-mismatch Bye takes this path) ends it: retrying
    // against a server that says no is futile.
    ASSERT_TRUE(harness.link.b.SendUVE(
        std::vector<std::uint8_t>{static_cast<std::uint8_t>(SessionMessageTagUVE::Bye)}));
    harness.client.UpdateUVE(6000U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_EQ(harness.client.CloseReasonUVE(), SessionCloseReasonUVE::RemoteDisconnect);
    EXPECT_EQ(harness.client.ReconnectAttemptsUVE(), 0U);
}

TEST(NetworkSessionUVETest, Redial_DisablingMidFlightCancelsToOriginalReason) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(EstablishWithTokenUVE(harness, 1000U, kTestSessionTokenUVE));
    harness.client.SetReconnectConfigUVE(SessionReconnectConfigUVE{100U, 10U});

    harness.UpdateBothUVE(6000U);
    ASSERT_EQ(harness.client.StateUVE(), SessionStateUVE::Reconnecting);

    harness.client.SetReconnectConfigUVE(SessionReconnectConfigUVE{});
    harness.client.UpdateUVE(6100U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_EQ(harness.client.CloseReasonUVE(), SessionCloseReasonUVE::Timeout);
}

TEST(NetworkSessionUVETest, Server_ForgetsTokenOnLocalDisconnect) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(EstablishWithTokenUVE(harness, 1000U, kTestSessionTokenUVE));
    harness.client.SetReconnectConfigUVE(SessionReconnectConfigUVE{100U, 10U});

    ASSERT_TRUE(harness.server.DisconnectUVE());
    // The client's Bye becomes a redial, and the server - having hung up - greets the
    // same token as a stranger.
    harness.client.UpdateUVE(1000U);
    ASSERT_EQ(harness.client.StateUVE(), SessionStateUVE::Reconnecting);
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Established);
    EXPECT_FALSE(harness.server.DidResumeUVE());
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Established);
    EXPECT_TRUE(harness.client.DidResumeUVE());
}

TEST(NetworkSessionUVETest, Hello_RightVersionWithoutTokenIsIgnored) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    const std::vector<std::uint8_t> shortHello{
        static_cast<std::uint8_t>(SessionMessageTagUVE::Hello),
        static_cast<std::uint8_t>(kSessionProtocolVersionUVE & 0xFFU),
        static_cast<std::uint8_t>((kSessionProtocolVersionUVE >> 8) & 0xFFU)};
    ASSERT_TRUE(harness.link.a.SendUVE(shortHello));

    harness.server.UpdateUVE(1000U);
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_EQ(harness.server.CloseReasonUVE(), SessionCloseReasonUVE::None);
    // Neither welcomed nor refused: a version-correct Hello with no token is malformed,
    // and malformed mail gets silence.
    EXPECT_FALSE(harness.link.a.TryReceiveUVE().has_value());
}

TEST(NetworkSessionUVETest, Multiplex_EnableGuards) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    EXPECT_TRUE(harness.client.EnableMultiplexingUVE());
    EXPECT_TRUE(harness.server.EnableMultiplexingUVE());
    EXPECT_TRUE(harness.client.IsMultiplexedUVE());
    EXPECT_TRUE(harness.server.IsMultiplexedUVE());
    EXPECT_TRUE(harness.client.EnableMultiplexingUVE()); // Idempotent while disconnected.
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_FALSE(harness.client.EnableMultiplexingUVE());
    EXPECT_FALSE(harness.server.EnableMultiplexingUVE());
}

TEST(NetworkSessionUVETest, Multiplex_HandshakeStaysSingleAndUntagged) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.client.EnableMultiplexingUVE());
    ASSERT_TRUE(harness.server.EnableMultiplexingUVE());
    ASSERT_TRUE(harness.client.ConnectUVE(1000U, 0x1234U));
    const std::optional<std::vector<std::uint8_t>> hello = harness.link.b.TryReceiveUVE();
    ASSERT_TRUE(hello.has_value());
    ASSERT_EQ(hello->size(), kSessionHelloBytesUVE);
    EXPECT_EQ(hello->front(), static_cast<std::uint8_t>(SessionMessageTagUVE::Hello));
    ASSERT_TRUE(harness.link.a.SendUVE(*hello));
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);
    EXPECT_EQ(harness.client.StateUVE(), SessionStateUVE::Established);
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Established);
}

TEST(NetworkSessionUVETest, Multiplex_ChannelsRoundTripIsolated) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.client.EnableMultiplexingUVE());
    ASSERT_TRUE(harness.server.EnableMultiplexingUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.client.SendChannelDataUVE(1U, BytesUVE("a1")));
    ASSERT_TRUE(harness.client.SendChannelDataUVE(2U, BytesUVE("b1")));
    ASSERT_TRUE(harness.client.SendChannelDataUVE(1U, BytesUVE("a2")));
    ASSERT_TRUE(harness.server.SendChannelDataUVE(9U, BytesUVE("s9")));
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.server.TryReceiveChannelDataUVE(1U), BytesUVE("a1"));
    EXPECT_EQ(harness.server.TryReceiveChannelDataUVE(2U), BytesUVE("b1"));
    EXPECT_EQ(harness.server.TryReceiveChannelDataUVE(1U), BytesUVE("a2"));
    EXPECT_FALSE(harness.server.TryReceiveChannelDataUVE(1U).has_value());
    EXPECT_FALSE(harness.server.TryReceiveChannelDataUVE(3U).has_value());
    EXPECT_FALSE(harness.server.TryReceiveDataUVE().has_value());
    EXPECT_EQ(harness.client.TryReceiveChannelDataUVE(9U), BytesUVE("s9"));
}

TEST(NetworkSessionUVETest, Multiplex_UntaggedSendsRefuse) {
    SessionHarnessUVE muxed = MakeSessionsUVE();
    ASSERT_TRUE(muxed.client.EnableMultiplexingUVE());
    ASSERT_TRUE(muxed.server.EnableMultiplexingUVE());
    ASSERT_TRUE(muxed.EstablishUVE(1000U));
    EXPECT_FALSE(muxed.client.SendDataUVE(BytesUVE("x")));
    EXPECT_FALSE(muxed.server.SendDataUVE(BytesUVE("x")));

    SessionHarnessUVE legacy = MakeSessionsUVE();
    ASSERT_TRUE(legacy.EstablishUVE(1000U));
    EXPECT_FALSE(legacy.client.SendChannelDataUVE(1U, BytesUVE("x")));
    EXPECT_FALSE(legacy.client.TryReceiveChannelDataUVE(1U).has_value());
}

TEST(NetworkSessionUVETest, Multiplex_BareTagHasNoChannel) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.client.EnableMultiplexingUVE());
    ASSERT_TRUE(harness.server.EnableMultiplexingUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    const std::uint8_t dataTag = static_cast<std::uint8_t>(SessionMessageTagUVE::Data);
    ASSERT_TRUE(harness.link.a.SendUVE(std::vector<std::uint8_t>{dataTag}));
    harness.server.UpdateUVE(1100U);
    EXPECT_FALSE(harness.server.TryReceiveChannelDataUVE(0U).has_value());
    EXPECT_FALSE(harness.server.TryReceiveDataUVE().has_value());
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Established);
}

TEST(NetworkSessionUVETest, Multiplex_ChannelPayloadBound) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.client.EnableMultiplexingUVE());
    ASSERT_TRUE(harness.server.EnableMultiplexingUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_TRUE(harness.client.SendChannelDataUVE(
        1U, std::vector<std::uint8_t>(kSessionMaximumChannelPayloadBytesUVE, 0xAAU)));
    EXPECT_FALSE(harness.client.SendChannelDataUVE(
        1U, std::vector<std::uint8_t>(kSessionMaximumChannelPayloadBytesUVE + 1U, 0xAAU)));
    harness.UpdateBothUVE(1100U);
    const std::optional<std::vector<std::uint8_t>> got =
        harness.server.TryReceiveChannelDataUVE(1U);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->size(), kSessionMaximumChannelPayloadBytesUVE);
}

TEST(NetworkSessionUVETest, Multiplex_DisconnectClearsChannels) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.client.EnableMultiplexingUVE());
    ASSERT_TRUE(harness.server.EnableMultiplexingUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.client.SendChannelDataUVE(1U, BytesUVE("first")));
    ASSERT_TRUE(harness.client.SendChannelDataUVE(1U, BytesUVE("second")));
    harness.server.UpdateUVE(1100U);
    EXPECT_EQ(harness.server.TryReceiveChannelDataUVE(1U), BytesUVE("first"));
    ASSERT_TRUE(harness.server.DisconnectUVE());
    EXPECT_FALSE(harness.server.TryReceiveChannelDataUVE(1U).has_value());
}

TEST(NetworkSessionUVETest, Multiplex_TimeoutClearsChannels) {
    SessionHarnessUVE harness = MakeSessionsUVE(100U);
    ASSERT_TRUE(harness.client.EnableMultiplexingUVE());
    ASSERT_TRUE(harness.server.EnableMultiplexingUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.client.SendChannelDataUVE(1U, BytesUVE("doomed")));
    harness.server.UpdateUVE(1000U); // Queues it.
    harness.server.UpdateUVE(1200U); // Silence past the timeout.
    EXPECT_EQ(harness.server.StateUVE(), SessionStateUVE::Disconnected);
    EXPECT_FALSE(harness.server.TryReceiveChannelDataUVE(1U).has_value());
}

TEST(NetworkSessionUVETest, Raw_RoundTripsLegacy) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.client.SendRawDataUVE(std::vector<std::uint8_t>{0x01U, 0x02U, 0x03U}));
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.server.TryReceiveRawDataUVE(),
              std::vector<std::uint8_t>({0x01U, 0x02U, 0x03U}));
    EXPECT_FALSE(harness.server.TryReceiveRawDataUVE().has_value());
}

TEST(NetworkSessionUVETest, Raw_PreservesChannelByteMuxed) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.client.EnableMultiplexingUVE());
    ASSERT_TRUE(harness.server.EnableMultiplexingUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.client.SendRawDataUVE(std::vector<std::uint8_t>{0x05U, 0x09U, 0x09U}));
    harness.UpdateBothUVE(1100U);
    // Re-tagged with the channel it arrived on: byte for byte what the sender framed.
    EXPECT_EQ(harness.server.TryReceiveRawDataUVE(),
              std::vector<std::uint8_t>({0x05U, 0x09U, 0x09U}));
    // Raw and channel receives share the queues: the raw pop already drained channel 5.
    EXPECT_FALSE(harness.server.TryReceiveChannelDataUVE(5U).has_value());
}

TEST(NetworkSessionUVETest, Raw_CrossApiInteropAndGuards) {
    SessionHarnessUVE harness = MakeSessionsUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.client.SendDataUVE(BytesUVE("hi")));
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.server.TryReceiveRawDataUVE(), BytesUVE("hi"));
    ASSERT_TRUE(harness.client.SendRawDataUVE(BytesUVE("yo")));
    harness.UpdateBothUVE(1200U);
    EXPECT_EQ(harness.server.TryReceiveDataUVE(), BytesUVE("yo"));

    SessionHarnessUVE fresh = MakeSessionsUVE();
    EXPECT_FALSE(fresh.client.SendRawDataUVE(BytesUVE("x")));
    EXPECT_FALSE(fresh.client.TryReceiveRawDataUVE().has_value());
    EXPECT_FALSE(harness.client.SendRawDataUVE(
        std::vector<std::uint8_t>(kSessionMaximumPayloadBytesUVE + 1U, 0xAAU)));
}

} // namespace
} // namespace UVE::Network::Tests
