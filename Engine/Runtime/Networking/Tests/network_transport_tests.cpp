// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_transport.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

[[nodiscard]] std::vector<std::uint8_t> Bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

TEST(LoopbackTransportTest, PairDeliversBothDirectionsWithExactBytes) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(16U);

    ASSERT_TRUE(pair.a.Send(Bytes("hello-b")));
    ASSERT_TRUE(pair.b.Send(Bytes("hello-a")));

    const std::optional<std::vector<std::uint8_t>> toB = pair.b.TryReceive();
    ASSERT_TRUE(toB.has_value());
    EXPECT_EQ(*toB, Bytes("hello-b"));
    const std::optional<std::vector<std::uint8_t>> toA = pair.a.TryReceive();
    ASSERT_TRUE(toA.has_value());
    EXPECT_EQ(*toA, Bytes("hello-a"));
}

TEST(LoopbackTransportTest, DeliveryIsFifoPerDirection) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(16U);

    ASSERT_TRUE(pair.a.Send(Bytes("first")));
    ASSERT_TRUE(pair.a.Send(Bytes("second")));
    ASSERT_TRUE(pair.a.Send(Bytes("third")));
    EXPECT_EQ(pair.b.PendingCount(), 3U);
    EXPECT_TRUE(pair.b.HasPending());

    EXPECT_EQ(pair.b.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("first")));
    EXPECT_EQ(pair.b.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("second")));
    EXPECT_EQ(pair.b.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("third")));
    EXPECT_FALSE(pair.b.HasPending());
}

TEST(LoopbackTransportTest, EmptyReceive_ReturnsNulloptWithoutTouchingStats) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(16U);

    EXPECT_FALSE(pair.a.TryReceive().has_value());
    EXPECT_FALSE(pair.b.TryReceive().has_value());
    EXPECT_EQ(pair.a.Stats(), LoopbackTransportStats{});
    EXPECT_EQ(pair.b.Stats(), LoopbackTransportStats{});
}

TEST(LoopbackTransportTest, EndpointsNeverReceiveTheirOwnSends) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(16U);

    ASSERT_TRUE(pair.a.Send(Bytes("ping")));
    EXPECT_FALSE(pair.a.TryReceive().has_value());
    EXPECT_TRUE(pair.b.HasPending());
    EXPECT_EQ(pair.b.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("ping")));
}

TEST(LoopbackTransportTest, MaxSizeDatagram_DeliversButOneByteMoreIsRejected) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(16U);
    const std::vector<std::uint8_t> maxed(kTransportMaximumDatagramBytes, 0xABU);
    const std::vector<std::uint8_t> tooBig(kTransportMaximumDatagramBytes + 1U, 0xABU);

    ASSERT_TRUE(pair.a.Send(maxed));
    EXPECT_FALSE(pair.a.Send(tooBig));

    const std::optional<std::vector<std::uint8_t>> received = pair.b.TryReceive();
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(*received, maxed);
    EXPECT_FALSE(pair.b.TryReceive().has_value());

    const LoopbackTransportStats stats = pair.a.Stats();
    EXPECT_EQ(stats.datagramsSent, 1U);
    EXPECT_EQ(stats.datagramsDroppedOversize, 1U);
    EXPECT_EQ(stats.datagramsDroppedFull, 0U);
}

TEST(LoopbackTransportTest, FullQueue_DropsNewestAndCounts) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(2U);

    ASSERT_TRUE(pair.a.Send(Bytes("one")));
    ASSERT_TRUE(pair.a.Send(Bytes("two")));
    EXPECT_FALSE(pair.a.Send(Bytes("three")));

    EXPECT_EQ(pair.b.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("one")));
    EXPECT_EQ(pair.b.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("two")));
    EXPECT_FALSE(pair.b.TryReceive().has_value());

    const LoopbackTransportStats stats = pair.a.Stats();
    EXPECT_EQ(stats.datagramsSent, 2U);
    EXPECT_EQ(stats.datagramsDroppedFull, 1U);
    EXPECT_EQ(stats.datagramsDroppedOversize, 0U);
}

TEST(LoopbackTransportTest, ZeroCapacity_DeliversNothing) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(0U);

    EXPECT_FALSE(pair.a.Send(Bytes("lost")));
    EXPECT_FALSE(pair.b.HasPending());

    const LoopbackTransportStats stats = pair.a.Stats();
    EXPECT_EQ(stats.datagramsSent, 0U);
    EXPECT_EQ(stats.datagramsDroppedFull, 1U);
}

TEST(LoopbackTransportTest, StatsTrackSendsAndReceivesPerEndpoint) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(16U);

    ASSERT_TRUE(pair.a.Send(Bytes("a1")));
    ASSERT_TRUE(pair.a.Send(Bytes("a2")));
    ASSERT_TRUE(pair.b.Send(Bytes("b1")));
    ASSERT_TRUE(pair.b.TryReceive().has_value());
    ASSERT_TRUE(pair.b.TryReceive().has_value());
    ASSERT_TRUE(pair.a.TryReceive().has_value());

    EXPECT_EQ(pair.a.Stats(), (LoopbackTransportStats{2U, 1U, 0U, 0U}));
    EXPECT_EQ(pair.b.Stats(), (LoopbackTransportStats{1U, 2U, 0U, 0U}));
}

TEST(LoopbackTransportTest, EndpointCopiesShareTheSameLink) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(16U);
    LoopbackEndpoint aliasOfA = pair.a;

    ASSERT_TRUE(aliasOfA.Send(Bytes("via-alias")));
    EXPECT_EQ(pair.b.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("via-alias")));
    EXPECT_EQ(pair.a.Stats().datagramsSent, 1U);
}

TEST(LoopbackTransportTest, MovedFromEndpointIsInert) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(16U);
    LoopbackEndpoint moved = std::move(pair.a);

    EXPECT_FALSE(pair.a.Send(Bytes("nope")));
    EXPECT_FALSE(pair.a.TryReceive().has_value());
    EXPECT_FALSE(pair.a.HasPending());
    EXPECT_EQ(pair.a.PendingCount(), 0U);
    EXPECT_EQ(pair.a.Stats(), LoopbackTransportStats{});
    EXPECT_TRUE(moved.Send(Bytes("still-works")));
    EXPECT_EQ(pair.b.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("still-works")));
}

} // namespace
} // namespace UVE::Network::Tests
