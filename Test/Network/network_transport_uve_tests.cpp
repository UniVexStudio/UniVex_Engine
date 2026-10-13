// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_transport_uve.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

[[nodiscard]] std::vector<std::uint8_t> BytesUVE(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

TEST(LoopbackTransportUVETest, PairDeliversBothDirectionsWithExactBytes) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(16U);

    ASSERT_TRUE(pair.a.SendUVE(BytesUVE("hello-b")));
    ASSERT_TRUE(pair.b.SendUVE(BytesUVE("hello-a")));

    const std::optional<std::vector<std::uint8_t>> toB = pair.b.TryReceiveUVE();
    ASSERT_TRUE(toB.has_value());
    EXPECT_EQ(*toB, BytesUVE("hello-b"));
    const std::optional<std::vector<std::uint8_t>> toA = pair.a.TryReceiveUVE();
    ASSERT_TRUE(toA.has_value());
    EXPECT_EQ(*toA, BytesUVE("hello-a"));
}

TEST(LoopbackTransportUVETest, DeliveryIsFifoPerDirection) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(16U);

    ASSERT_TRUE(pair.a.SendUVE(BytesUVE("first")));
    ASSERT_TRUE(pair.a.SendUVE(BytesUVE("second")));
    ASSERT_TRUE(pair.a.SendUVE(BytesUVE("third")));
    EXPECT_EQ(pair.b.PendingCountUVE(), 3U);
    EXPECT_TRUE(pair.b.HasPendingUVE());

    EXPECT_EQ(pair.b.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("first")));
    EXPECT_EQ(pair.b.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("second")));
    EXPECT_EQ(pair.b.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("third")));
    EXPECT_FALSE(pair.b.HasPendingUVE());
}

TEST(LoopbackTransportUVETest, EmptyReceive_ReturnsNulloptWithoutTouchingStats) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(16U);

    EXPECT_FALSE(pair.a.TryReceiveUVE().has_value());
    EXPECT_FALSE(pair.b.TryReceiveUVE().has_value());
    EXPECT_EQ(pair.a.StatsUVE(), LoopbackTransportStatsUVE{});
    EXPECT_EQ(pair.b.StatsUVE(), LoopbackTransportStatsUVE{});
}

TEST(LoopbackTransportUVETest, EndpointsNeverReceiveTheirOwnSends) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(16U);

    ASSERT_TRUE(pair.a.SendUVE(BytesUVE("ping")));
    EXPECT_FALSE(pair.a.TryReceiveUVE().has_value());
    EXPECT_TRUE(pair.b.HasPendingUVE());
    EXPECT_EQ(pair.b.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("ping")));
}

TEST(LoopbackTransportUVETest, MaxSizeDatagram_DeliversButOneByteMoreIsRejected) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(16U);
    const std::vector<std::uint8_t> maxed(kTransportMaximumDatagramBytesUVE, 0xABU);
    const std::vector<std::uint8_t> tooBig(kTransportMaximumDatagramBytesUVE + 1U, 0xABU);

    ASSERT_TRUE(pair.a.SendUVE(maxed));
    EXPECT_FALSE(pair.a.SendUVE(tooBig));

    const std::optional<std::vector<std::uint8_t>> received = pair.b.TryReceiveUVE();
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(*received, maxed);
    EXPECT_FALSE(pair.b.TryReceiveUVE().has_value());

    const LoopbackTransportStatsUVE stats = pair.a.StatsUVE();
    EXPECT_EQ(stats.datagramsSent, 1U);
    EXPECT_EQ(stats.datagramsDroppedOversize, 1U);
    EXPECT_EQ(stats.datagramsDroppedFull, 0U);
}

TEST(LoopbackTransportUVETest, FullQueue_DropsNewestAndCounts) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(2U);

    ASSERT_TRUE(pair.a.SendUVE(BytesUVE("one")));
    ASSERT_TRUE(pair.a.SendUVE(BytesUVE("two")));
    EXPECT_FALSE(pair.a.SendUVE(BytesUVE("three")));

    EXPECT_EQ(pair.b.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("one")));
    EXPECT_EQ(pair.b.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("two")));
    EXPECT_FALSE(pair.b.TryReceiveUVE().has_value());

    const LoopbackTransportStatsUVE stats = pair.a.StatsUVE();
    EXPECT_EQ(stats.datagramsSent, 2U);
    EXPECT_EQ(stats.datagramsDroppedFull, 1U);
    EXPECT_EQ(stats.datagramsDroppedOversize, 0U);
}

TEST(LoopbackTransportUVETest, ZeroCapacity_DeliversNothing) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(0U);

    EXPECT_FALSE(pair.a.SendUVE(BytesUVE("lost")));
    EXPECT_FALSE(pair.b.HasPendingUVE());

    const LoopbackTransportStatsUVE stats = pair.a.StatsUVE();
    EXPECT_EQ(stats.datagramsSent, 0U);
    EXPECT_EQ(stats.datagramsDroppedFull, 1U);
}

TEST(LoopbackTransportUVETest, StatsTrackSendsAndReceivesPerEndpoint) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(16U);

    ASSERT_TRUE(pair.a.SendUVE(BytesUVE("a1")));
    ASSERT_TRUE(pair.a.SendUVE(BytesUVE("a2")));
    ASSERT_TRUE(pair.b.SendUVE(BytesUVE("b1")));
    ASSERT_TRUE(pair.b.TryReceiveUVE().has_value());
    ASSERT_TRUE(pair.b.TryReceiveUVE().has_value());
    ASSERT_TRUE(pair.a.TryReceiveUVE().has_value());

    EXPECT_EQ(pair.a.StatsUVE(), (LoopbackTransportStatsUVE{2U, 1U, 0U, 0U}));
    EXPECT_EQ(pair.b.StatsUVE(), (LoopbackTransportStatsUVE{1U, 2U, 0U, 0U}));
}

TEST(LoopbackTransportUVETest, EndpointCopiesShareTheSameLink) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(16U);
    LoopbackEndpointUVE aliasOfA = pair.a;

    ASSERT_TRUE(aliasOfA.SendUVE(BytesUVE("via-alias")));
    EXPECT_EQ(pair.b.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("via-alias")));
    EXPECT_EQ(pair.a.StatsUVE().datagramsSent, 1U);
}

TEST(LoopbackTransportUVETest, MovedFromEndpointIsInert) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(16U);
    LoopbackEndpointUVE moved = std::move(pair.a);

    EXPECT_FALSE(pair.a.SendUVE(BytesUVE("nope")));
    EXPECT_FALSE(pair.a.TryReceiveUVE().has_value());
    EXPECT_FALSE(pair.a.HasPendingUVE());
    EXPECT_EQ(pair.a.PendingCountUVE(), 0U);
    EXPECT_EQ(pair.a.StatsUVE(), LoopbackTransportStatsUVE{});
    EXPECT_TRUE(moved.SendUVE(BytesUVE("still-works")));
    EXPECT_EQ(pair.b.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("still-works")));
}

} // namespace
} // namespace UVE::Network::Tests
