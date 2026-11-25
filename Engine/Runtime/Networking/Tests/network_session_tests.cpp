// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_session.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

[[nodiscard]] std::vector<std::uint8_t> Bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

[[nodiscard]] std::vector<std::uint8_t> Tagged(const std::uint8_t tag, const std::string& text) {
    std::vector<std::uint8_t> datagram{tag};
    for (const std::uint8_t byte : Bytes(text)) {
        datagram.push_back(byte);
    }
    return datagram;
}

struct SessionHarness final {
    LoopbackTransportPair link;
    NetworkSession client;
    NetworkSession server;

    void UpdateBoth(const std::uint64_t nowMs) {
        client.Update(nowMs);
        server.Update(nowMs);
    }

    // A handshake needs two pump rounds: the first carries Hello to the server and queues the
    // Welcome, the second delivers it to the client.
    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        if (!client.Connect(nowMs)) {
            return false;
        }
        UpdateBoth(nowMs);
        UpdateBoth(nowMs);
        return client.State() == SessionState::Established &&
               server.State() == SessionState::Established;
    }
};

[[nodiscard]] SessionHarness MakeSessions(
    const std::uint64_t timeoutMs = kDefaultSessionTimeoutMs) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(64U);
    NetworkSession client(SessionRole::Client,
                             std::make_unique<LoopbackEndpoint>(pair.a), timeoutMs);
    NetworkSession server(SessionRole::Server,
                             std::make_unique<LoopbackEndpoint>(pair.b), timeoutMs);
    return SessionHarness{std::move(pair), std::move(client), std::move(server)};
}

TEST(NetworkSessionTest, Handshake_ClientAndServerReachEstablished) {
    SessionHarness harness = MakeSessions();

    ASSERT_TRUE(harness.client.Connect(1000U));
    EXPECT_EQ(harness.client.State(), SessionState::Connecting);
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);

    EXPECT_EQ(harness.client.State(), SessionState::Established);
    EXPECT_EQ(harness.server.State(), SessionState::Established);
    EXPECT_EQ(harness.client.CloseReason(), SessionCloseReason::None);
    EXPECT_EQ(harness.server.CloseReason(), SessionCloseReason::None);
}

TEST(NetworkSessionTest, RoleRules_RejectInvalidTransitions) {
    SessionHarness harness = MakeSessions();

    EXPECT_FALSE(harness.server.Connect(1000U));
    EXPECT_EQ(harness.server.State(), SessionState::Disconnected);
    EXPECT_FALSE(harness.client.Disconnect());
    EXPECT_FALSE(harness.client.SendData(Bytes("early")));

    ASSERT_TRUE(harness.client.Connect(1000U));
    EXPECT_FALSE(harness.client.Connect(1000U));
    EXPECT_FALSE(harness.client.SendData(Bytes("connecting")));
}

TEST(NetworkSessionTest, DataFlowsBothWaysAfterHandshake) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_EQ(harness.client.State(), SessionState::Established);

    ASSERT_TRUE(harness.client.SendData(Bytes("c-to-s")));
    ASSERT_TRUE(harness.server.SendData(Bytes("s-to-c")));
    harness.UpdateBoth(1000U);

    EXPECT_EQ(harness.server.TryReceiveData(),
              std::optional<std::vector<std::uint8_t>>(Bytes("c-to-s")));
    EXPECT_EQ(harness.client.TryReceiveData(),
              std::optional<std::vector<std::uint8_t>>(Bytes("s-to-c")));
    EXPECT_FALSE(harness.client.TryReceiveData().has_value());
}

TEST(NetworkSessionTest, Disconnect_ByeClosesBothSidesAndClearsQueuedData) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.client.SendData(Bytes("unread")));

    EXPECT_TRUE(harness.client.Disconnect());
    EXPECT_EQ(harness.client.State(), SessionState::Disconnected);
    EXPECT_EQ(harness.client.CloseReason(), SessionCloseReason::LocalDisconnect);

    harness.server.Update(1000U);
    EXPECT_EQ(harness.server.State(), SessionState::Disconnected);
    EXPECT_EQ(harness.server.CloseReason(), SessionCloseReason::RemoteDisconnect);
    EXPECT_FALSE(harness.server.TryReceiveData().has_value());
}

TEST(NetworkSessionTest, Timeout_ConnectingWithoutWelcome) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.client.Connect(1000U));

    harness.client.Update(5999U);
    EXPECT_EQ(harness.client.State(), SessionState::Connecting);

    harness.client.Update(6000U);
    EXPECT_EQ(harness.client.State(), SessionState::Disconnected);
    EXPECT_EQ(harness.client.CloseReason(), SessionCloseReason::Timeout);
}

TEST(NetworkSessionTest, Timeout_EstablishedGoesQuiet) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.Establish(1000U));

    harness.UpdateBoth(5999U);
    EXPECT_EQ(harness.client.State(), SessionState::Established);
    EXPECT_EQ(harness.server.State(), SessionState::Established);

    harness.UpdateBoth(6000U);
    EXPECT_EQ(harness.client.State(), SessionState::Disconnected);
    EXPECT_EQ(harness.client.CloseReason(), SessionCloseReason::Timeout);
    EXPECT_EQ(harness.server.State(), SessionState::Disconnected);
    EXPECT_EQ(harness.server.CloseReason(), SessionCloseReason::Timeout);
}

TEST(NetworkSessionTest, TrafficResetsTheTimeoutPerDirection) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.Establish(1000U));

    // Only inbound datagrams refresh a session's clock: each side must hear the other.
    ASSERT_TRUE(harness.client.SendData(Bytes("c1")));
    ASSERT_TRUE(harness.server.SendData(Bytes("s1")));
    harness.UpdateBoth(5000U);
    harness.UpdateBoth(9999U);
    EXPECT_EQ(harness.client.State(), SessionState::Established);
    EXPECT_EQ(harness.server.State(), SessionState::Established);

    harness.UpdateBoth(10000U);
    EXPECT_EQ(harness.client.State(), SessionState::Disconnected);
    EXPECT_EQ(harness.server.State(), SessionState::Disconnected);
}

TEST(NetworkSessionTest, VersionMismatch_ServerAnswersByeAndStaysDisconnected) {
    SessionHarness harness = MakeSessions();
    const std::vector<std::uint8_t> badHello{
        static_cast<std::uint8_t>(SessionMessageTag::Hello), 0x09U, 0x00U};
    ASSERT_TRUE(harness.link.a.Send(badHello));

    harness.server.Update(1000U);
    EXPECT_EQ(harness.server.State(), SessionState::Disconnected);
    EXPECT_EQ(harness.server.CloseReason(), SessionCloseReason::None);

    const std::vector<std::uint8_t> expectedBye{
        static_cast<std::uint8_t>(SessionMessageTag::Bye)};
    EXPECT_EQ(harness.link.a.TryReceive(),
              std::optional<std::vector<std::uint8_t>>(expectedBye));
}

TEST(NetworkSessionTest, GarbageAndEarlyData_AreIgnored) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.link.a.Send(Bytes("no-tag-here")));
    ASSERT_TRUE(harness.link.a.Send(std::vector<std::uint8_t>{}));
    ASSERT_TRUE(harness.link.a.Send(
        Tagged(static_cast<std::uint8_t>(SessionMessageTag::Data), "early-data")));

    harness.server.Update(1000U);
    EXPECT_EQ(harness.server.State(), SessionState::Disconnected);
    EXPECT_FALSE(harness.server.TryReceiveData().has_value());

    ASSERT_TRUE(harness.Establish(2000U));
    ASSERT_EQ(harness.server.State(), SessionState::Established);
    ASSERT_TRUE(harness.link.a.Send(Tagged(0xFFU, "unknown-tag")));
    harness.server.Update(2000U);
    EXPECT_EQ(harness.server.State(), SessionState::Established);
    EXPECT_FALSE(harness.server.TryReceiveData().has_value());
}

TEST(NetworkSessionTest, DataPayload_BoundedOneByteUnderTheDatagramCap) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.Establish(1000U));

    const std::vector<std::uint8_t> maxed(kSessionMaximumPayloadBytes, 0xCDU);
    const std::vector<std::uint8_t> tooBig(kSessionMaximumPayloadBytes + 1U, 0xCDU);
    ASSERT_TRUE(harness.client.SendData(maxed));
    EXPECT_FALSE(harness.client.SendData(tooBig));

    harness.server.Update(1000U);
    const std::optional<std::vector<std::uint8_t>> received = harness.server.TryReceiveData();
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(*received, maxed);
    EXPECT_FALSE(harness.server.TryReceiveData().has_value());
}

// Byte-distinct across all eight token bytes, so a byte-order slip anywhere in the Hello
// framing cannot hide.
inline constexpr std::uint64_t kTestSessionToken = 0x0102030405060708U;

// Establishes with a token, the way Establish does anonymously: two pump rounds carry
// Hello to the server and the Welcome back.
[[nodiscard]] bool EstablishWithToken(SessionHarness& harness, const std::uint64_t nowMs,
                                         const std::uint64_t token) {
    if (!harness.client.Connect(nowMs, token)) {
        return false;
    }
    harness.UpdateBoth(nowMs);
    harness.UpdateBoth(nowMs);
    return harness.client.State() == SessionState::Established &&
           harness.server.State() == SessionState::Established;
}

TEST(NetworkSessionTest, Token_ClientHelloBindsItOnBothSides) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(EstablishWithToken(harness, 1000U, kTestSessionToken));

    EXPECT_EQ(harness.client.SessionToken(), kTestSessionToken);
    EXPECT_EQ(harness.server.SessionToken(), kTestSessionToken);
    EXPECT_FALSE(harness.client.DidResume());
    EXPECT_FALSE(harness.server.DidResume());
    EXPECT_EQ(harness.client.ReconnectAttempts(), 0U);
}

TEST(NetworkSessionTest, Resume_ReturningTokenIsRecognized) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(EstablishWithToken(harness, 1000U, kTestSessionToken));
    harness.client.SetReconnectConfig(SessionReconnectConfig{100U, 10U});

    // Silence past the timeout drops both sides; the client starts redialing instead of
    // disconnecting, and reports no closure while it still tries.
    harness.UpdateBoth(6000U);
    EXPECT_EQ(harness.client.State(), SessionState::Reconnecting);
    EXPECT_EQ(harness.client.CloseReason(), SessionCloseReason::None);
    EXPECT_EQ(harness.client.ReconnectAttempts(), 0U);
    EXPECT_EQ(harness.server.State(), SessionState::Disconnected);
    EXPECT_EQ(harness.server.CloseReason(), SessionCloseReason::Timeout);

    // The first due attempt redials; the server accepts in the same pump and recognizes
    // the returning token, while the Welcome is still in flight to the client.
    harness.UpdateBoth(6100U);
    EXPECT_EQ(harness.client.ReconnectAttempts(), 1U);
    EXPECT_EQ(harness.server.State(), SessionState::Established);
    EXPECT_EQ(harness.server.SessionToken(), kTestSessionToken);
    EXPECT_TRUE(harness.server.DidResume());

    harness.UpdateBoth(6100U);
    EXPECT_EQ(harness.client.State(), SessionState::Established);
    EXPECT_TRUE(harness.client.DidResume());
    // The landing attempt stays readable: the redial succeeded "1 of 10".
    EXPECT_EQ(harness.client.ReconnectAttempts(), 1U);
}

TEST(NetworkSessionTest, Resume_AnonymousTokenNeverMatches) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.Establish(1000U));
    harness.client.SetReconnectConfig(SessionReconnectConfig{100U, 10U});

    harness.UpdateBoth(6000U);
    ASSERT_EQ(harness.client.State(), SessionState::Reconnecting);

    harness.UpdateBoth(6100U);
    harness.UpdateBoth(6100U);
    ASSERT_EQ(harness.client.State(), SessionState::Established);
    // The redial itself succeeded, so the client reports a resume - but the server bound
    // an anonymous token, which never matches anything, not even itself.
    EXPECT_TRUE(harness.client.DidResume());
    EXPECT_FALSE(harness.server.DidResume());
    EXPECT_EQ(harness.server.SessionToken(), kAnonymousSessionToken);
}

TEST(NetworkSessionTest, Redial_GivesUpAfterMaxAttempts) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(EstablishWithToken(harness, 1000U, kTestSessionToken));
    harness.client.SetReconnectConfig(SessionReconnectConfig{100U, 3U});

    // The server is never pumped again: every Hello lands in an unread queue.
    harness.client.Update(6000U);
    ASSERT_EQ(harness.client.State(), SessionState::Reconnecting);

    harness.client.Update(6099U);
    EXPECT_EQ(harness.client.ReconnectAttempts(), 0U);
    harness.client.Update(6100U);
    EXPECT_EQ(harness.client.ReconnectAttempts(), 1U);
    harness.client.Update(6200U);
    EXPECT_EQ(harness.client.ReconnectAttempts(), 2U);
    harness.client.Update(6300U);
    EXPECT_EQ(harness.client.ReconnectAttempts(), 3U);
    EXPECT_EQ(harness.client.State(), SessionState::Reconnecting);

    // Three is the budget: the next pump gives up without firing a fourth, and the frozen
    // count proves no further Hello ever goes out.
    harness.client.Update(6400U);
    EXPECT_EQ(harness.client.State(), SessionState::Disconnected);
    EXPECT_EQ(harness.client.CloseReason(), SessionCloseReason::ReconnectFailed);
    harness.client.Update(99999U);
    EXPECT_EQ(harness.client.State(), SessionState::Disconnected);
    EXPECT_EQ(harness.client.ReconnectAttempts(), 3U);
}

TEST(NetworkSessionTest, Redial_LocalDisconnectNeverRetries) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(EstablishWithToken(harness, 1000U, kTestSessionToken));
    harness.client.SetReconnectConfig(SessionReconnectConfig{100U, 10U});

    ASSERT_TRUE(harness.client.Disconnect());
    harness.client.Update(99999U);
    EXPECT_EQ(harness.client.State(), SessionState::Disconnected);
    EXPECT_EQ(harness.client.CloseReason(), SessionCloseReason::LocalDisconnect);
    EXPECT_EQ(harness.client.ReconnectAttempts(), 0U);
}

TEST(NetworkSessionTest, Redial_ByeDuringRedialDisconnectsImmediately) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(EstablishWithToken(harness, 1000U, kTestSessionToken));
    harness.client.SetReconnectConfig(SessionReconnectConfig{1000U, 10U});

    harness.client.Update(6000U);
    ASSERT_EQ(harness.client.State(), SessionState::Reconnecting);
    // Reconnecting counts as live: dialing over a redial is refused, hang up first.
    EXPECT_FALSE(harness.client.Connect(6000U));

    // A refusal mid-redial (the version-mismatch Bye takes this path) ends it: retrying
    // against a server that says no is futile.
    ASSERT_TRUE(harness.link.b.Send(
        std::vector<std::uint8_t>{static_cast<std::uint8_t>(SessionMessageTag::Bye)}));
    harness.client.Update(6000U);
    EXPECT_EQ(harness.client.State(), SessionState::Disconnected);
    EXPECT_EQ(harness.client.CloseReason(), SessionCloseReason::RemoteDisconnect);
    EXPECT_EQ(harness.client.ReconnectAttempts(), 0U);
}

TEST(NetworkSessionTest, Redial_DisablingMidFlightCancelsToOriginalReason) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(EstablishWithToken(harness, 1000U, kTestSessionToken));
    harness.client.SetReconnectConfig(SessionReconnectConfig{100U, 10U});

    harness.UpdateBoth(6000U);
    ASSERT_EQ(harness.client.State(), SessionState::Reconnecting);

    harness.client.SetReconnectConfig(SessionReconnectConfig{});
    harness.client.Update(6100U);
    EXPECT_EQ(harness.client.State(), SessionState::Disconnected);
    EXPECT_EQ(harness.client.CloseReason(), SessionCloseReason::Timeout);
}

TEST(NetworkSessionTest, Server_ForgetsTokenOnLocalDisconnect) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(EstablishWithToken(harness, 1000U, kTestSessionToken));
    harness.client.SetReconnectConfig(SessionReconnectConfig{100U, 10U});

    ASSERT_TRUE(harness.server.Disconnect());
    // The client's Bye becomes a redial, and the server - having hung up - greets the
    // same token as a stranger.
    harness.client.Update(1000U);
    ASSERT_EQ(harness.client.State(), SessionState::Reconnecting);
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.server.State(), SessionState::Established);
    EXPECT_FALSE(harness.server.DidResume());
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.client.State(), SessionState::Established);
    EXPECT_TRUE(harness.client.DidResume());
}

TEST(NetworkSessionTest, Hello_RightVersionWithoutTokenIsIgnored) {
    SessionHarness harness = MakeSessions();
    const std::vector<std::uint8_t> shortHello{
        static_cast<std::uint8_t>(SessionMessageTag::Hello),
        static_cast<std::uint8_t>(kSessionProtocolVersion & 0xFFU),
        static_cast<std::uint8_t>((kSessionProtocolVersion >> 8) & 0xFFU)};
    ASSERT_TRUE(harness.link.a.Send(shortHello));

    harness.server.Update(1000U);
    EXPECT_EQ(harness.server.State(), SessionState::Disconnected);
    EXPECT_EQ(harness.server.CloseReason(), SessionCloseReason::None);
    // Neither welcomed nor refused: a version-correct Hello with no token is malformed,
    // and malformed mail gets silence.
    EXPECT_FALSE(harness.link.a.TryReceive().has_value());
}

TEST(NetworkSessionTest, Multiplex_EnableGuards) {
    SessionHarness harness = MakeSessions();
    EXPECT_TRUE(harness.client.EnableMultiplexing());
    EXPECT_TRUE(harness.server.EnableMultiplexing());
    EXPECT_TRUE(harness.client.IsMultiplexed());
    EXPECT_TRUE(harness.server.IsMultiplexed());
    EXPECT_TRUE(harness.client.EnableMultiplexing()); // Idempotent while disconnected.
    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_FALSE(harness.client.EnableMultiplexing());
    EXPECT_FALSE(harness.server.EnableMultiplexing());
}

TEST(NetworkSessionTest, Multiplex_HandshakeStaysSingleAndUntagged) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.client.EnableMultiplexing());
    ASSERT_TRUE(harness.server.EnableMultiplexing());
    ASSERT_TRUE(harness.client.Connect(1000U, 0x1234U));
    const std::optional<std::vector<std::uint8_t>> hello = harness.link.b.TryReceive();
    ASSERT_TRUE(hello.has_value());
    ASSERT_EQ(hello->size(), kSessionHelloBytes);
    EXPECT_EQ(hello->front(), static_cast<std::uint8_t>(SessionMessageTag::Hello));
    ASSERT_TRUE(harness.link.a.Send(*hello));
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);
    EXPECT_EQ(harness.client.State(), SessionState::Established);
    EXPECT_EQ(harness.server.State(), SessionState::Established);
}

TEST(NetworkSessionTest, Multiplex_ChannelsRoundTripIsolated) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.client.EnableMultiplexing());
    ASSERT_TRUE(harness.server.EnableMultiplexing());
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.client.SendChannelData(1U, Bytes("a1")));
    ASSERT_TRUE(harness.client.SendChannelData(2U, Bytes("b1")));
    ASSERT_TRUE(harness.client.SendChannelData(1U, Bytes("a2")));
    ASSERT_TRUE(harness.server.SendChannelData(9U, Bytes("s9")));
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.server.TryReceiveChannelData(1U), Bytes("a1"));
    EXPECT_EQ(harness.server.TryReceiveChannelData(2U), Bytes("b1"));
    EXPECT_EQ(harness.server.TryReceiveChannelData(1U), Bytes("a2"));
    EXPECT_FALSE(harness.server.TryReceiveChannelData(1U).has_value());
    EXPECT_FALSE(harness.server.TryReceiveChannelData(3U).has_value());
    EXPECT_FALSE(harness.server.TryReceiveData().has_value());
    EXPECT_EQ(harness.client.TryReceiveChannelData(9U), Bytes("s9"));
}

TEST(NetworkSessionTest, Multiplex_UntaggedSendsRefuse) {
    SessionHarness muxed = MakeSessions();
    ASSERT_TRUE(muxed.client.EnableMultiplexing());
    ASSERT_TRUE(muxed.server.EnableMultiplexing());
    ASSERT_TRUE(muxed.Establish(1000U));
    EXPECT_FALSE(muxed.client.SendData(Bytes("x")));
    EXPECT_FALSE(muxed.server.SendData(Bytes("x")));

    SessionHarness legacy = MakeSessions();
    ASSERT_TRUE(legacy.Establish(1000U));
    EXPECT_FALSE(legacy.client.SendChannelData(1U, Bytes("x")));
    EXPECT_FALSE(legacy.client.TryReceiveChannelData(1U).has_value());
}

TEST(NetworkSessionTest, Multiplex_BareTagHasNoChannel) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.client.EnableMultiplexing());
    ASSERT_TRUE(harness.server.EnableMultiplexing());
    ASSERT_TRUE(harness.Establish(1000U));
    const std::uint8_t dataTag = static_cast<std::uint8_t>(SessionMessageTag::Data);
    ASSERT_TRUE(harness.link.a.Send(std::vector<std::uint8_t>{dataTag}));
    harness.server.Update(1100U);
    EXPECT_FALSE(harness.server.TryReceiveChannelData(0U).has_value());
    EXPECT_FALSE(harness.server.TryReceiveData().has_value());
    EXPECT_EQ(harness.server.State(), SessionState::Established);
}

TEST(NetworkSessionTest, Multiplex_ChannelPayloadBound) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.client.EnableMultiplexing());
    ASSERT_TRUE(harness.server.EnableMultiplexing());
    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_TRUE(harness.client.SendChannelData(
        1U, std::vector<std::uint8_t>(kSessionMaximumChannelPayloadBytes, 0xAAU)));
    EXPECT_FALSE(harness.client.SendChannelData(
        1U, std::vector<std::uint8_t>(kSessionMaximumChannelPayloadBytes + 1U, 0xAAU)));
    harness.UpdateBoth(1100U);
    const std::optional<std::vector<std::uint8_t>> got =
        harness.server.TryReceiveChannelData(1U);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->size(), kSessionMaximumChannelPayloadBytes);
}

TEST(NetworkSessionTest, Multiplex_DisconnectClearsChannels) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.client.EnableMultiplexing());
    ASSERT_TRUE(harness.server.EnableMultiplexing());
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.client.SendChannelData(1U, Bytes("first")));
    ASSERT_TRUE(harness.client.SendChannelData(1U, Bytes("second")));
    harness.server.Update(1100U);
    EXPECT_EQ(harness.server.TryReceiveChannelData(1U), Bytes("first"));
    ASSERT_TRUE(harness.server.Disconnect());
    EXPECT_FALSE(harness.server.TryReceiveChannelData(1U).has_value());
}

TEST(NetworkSessionTest, Multiplex_TimeoutClearsChannels) {
    SessionHarness harness = MakeSessions(100U);
    ASSERT_TRUE(harness.client.EnableMultiplexing());
    ASSERT_TRUE(harness.server.EnableMultiplexing());
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.client.SendChannelData(1U, Bytes("doomed")));
    harness.server.Update(1000U); // Queues it.
    harness.server.Update(1200U); // Silence past the timeout.
    EXPECT_EQ(harness.server.State(), SessionState::Disconnected);
    EXPECT_FALSE(harness.server.TryReceiveChannelData(1U).has_value());
}

TEST(NetworkSessionTest, Raw_RoundTripsLegacy) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.client.SendRawData(std::vector<std::uint8_t>{0x01U, 0x02U, 0x03U}));
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.server.TryReceiveRawData(),
              std::vector<std::uint8_t>({0x01U, 0x02U, 0x03U}));
    EXPECT_FALSE(harness.server.TryReceiveRawData().has_value());
}

TEST(NetworkSessionTest, Raw_PreservesChannelByteMuxed) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.client.EnableMultiplexing());
    ASSERT_TRUE(harness.server.EnableMultiplexing());
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.client.SendRawData(std::vector<std::uint8_t>{0x05U, 0x09U, 0x09U}));
    harness.UpdateBoth(1100U);
    // Re-tagged with the channel it arrived on: byte for byte what the sender framed.
    EXPECT_EQ(harness.server.TryReceiveRawData(),
              std::vector<std::uint8_t>({0x05U, 0x09U, 0x09U}));
    // Raw and channel receives share the queues: the raw pop already drained channel 5.
    EXPECT_FALSE(harness.server.TryReceiveChannelData(5U).has_value());
}

TEST(NetworkSessionTest, Raw_CrossApiInteropAndGuards) {
    SessionHarness harness = MakeSessions();
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.client.SendData(Bytes("hi")));
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.server.TryReceiveRawData(), Bytes("hi"));
    ASSERT_TRUE(harness.client.SendRawData(Bytes("yo")));
    harness.UpdateBoth(1200U);
    EXPECT_EQ(harness.server.TryReceiveData(), Bytes("yo"));

    SessionHarness fresh = MakeSessions();
    EXPECT_FALSE(fresh.client.SendRawData(Bytes("x")));
    EXPECT_FALSE(fresh.client.TryReceiveRawData().has_value());
    EXPECT_FALSE(harness.client.SendRawData(
        std::vector<std::uint8_t>(kSessionMaximumPayloadBytes + 1U, 0xAAU)));
}

} // namespace
} // namespace UVE::Network::Tests
