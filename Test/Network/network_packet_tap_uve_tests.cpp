// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_packet_tap_uve.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "uve/math/vector3_uve.h"
#include "uve/network/network_packet_capture_uve.h"
#include "uve/network/network_replication_uve.h"
#include "uve/network/network_session_uve.h"
#include "uve/network/network_transport_uve.h"

namespace UVE::Network::Tests {
namespace {

constexpr NetworkChannelIdUVE kReplicationChannelUVE = 3U;

struct SessionTapUVE final {
    LoopbackTransportPairUVE link;
    NetworkSessionUVE a;
    NetworkSessionUVE b;
    std::vector<PacketTapUVE> tappedA;
    std::vector<PacketTapUVE> tappedB;
};

[[nodiscard]] SessionTapUVE MakeSessionTapUVE() {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE a(SessionRoleUVE::Client, std::make_unique<LoopbackEndpointUVE>(pair.a));
    NetworkSessionUVE b(SessionRoleUVE::Server, std::make_unique<LoopbackEndpointUVE>(pair.b));
    return SessionTapUVE{std::move(pair), std::move(a), std::move(b), {}, {}};
}

[[nodiscard]] SessionTapUVE MakeMuxedTapUVE() {
    SessionTapUVE tap = MakeSessionTapUVE();
    EXPECT_TRUE(tap.a.EnableMultiplexingUVE());
    EXPECT_TRUE(tap.b.EnableMultiplexingUVE());
    return tap;
}

// Binds after the harness is in place: the lambdas capture the harness by reference, so
// binding before a move would dangle.
void WatchUVE(SessionTapUVE& tap) {
    tap.a.SetTapUVE([&tap](const PacketTapUVE& record) { tap.tappedA.push_back(record); });
    tap.b.SetTapUVE([&tap](const PacketTapUVE& record) { tap.tappedB.push_back(record); });
}

void EstablishUVE(SessionTapUVE& tap, const std::uint64_t nowMs) {
    EXPECT_TRUE(tap.a.ConnectUVE(nowMs, 123U));
    tap.b.UpdateUVE(nowMs);
    tap.a.UpdateUVE(nowMs);
    EXPECT_EQ(tap.a.StateUVE(), SessionStateUVE::Established);
    EXPECT_EQ(tap.b.StateUVE(), SessionStateUVE::Established);
}

struct MuxedReplicationTapUVE final {
    LoopbackTransportPairUVE link;
    std::unique_ptr<NetworkSessionUVE> a;
    std::unique_ptr<NetworkSessionUVE> b;
    std::unique_ptr<NetworkReplicatorUVE> replicator;
    std::vector<PacketTapUVE> tappedA;
    std::vector<PacketTapUVE> tappedB;
};

[[nodiscard]] MuxedReplicationTapUVE MakeMuxedReplicationTapUVE() {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    auto a = std::make_unique<NetworkSessionUVE>(SessionRoleUVE::Client,
                                                 std::make_unique<LoopbackEndpointUVE>(pair.a));
    auto b = std::make_unique<NetworkSessionUVE>(SessionRoleUVE::Server,
                                                 std::make_unique<LoopbackEndpointUVE>(pair.b));
    EXPECT_TRUE(a->EnableMultiplexingUVE());
    EXPECT_TRUE(b->EnableMultiplexingUVE());
    auto replicator = std::make_unique<NetworkReplicatorUVE>(*a, kReplicationChannelUVE);
    return MuxedReplicationTapUVE{std::move(pair), std::move(a), std::move(b),
                                  std::move(replicator), {}, {}};
}

void WatchUVE(MuxedReplicationTapUVE& tap) {
    tap.a->SetTapUVE(
        [&tap](const PacketTapUVE& record) { tap.tappedA.push_back(record); });
    tap.b->SetTapUVE(
        [&tap](const PacketTapUVE& record) { tap.tappedB.push_back(record); });
}

TEST(PacketTapUVETest, HandshakeTappedBothDirections) {
    SessionTapUVE tap = MakeSessionTapUVE();
    EXPECT_FALSE(tap.a.HasTapUVE());
    WatchUVE(tap);
    EXPECT_TRUE(tap.a.HasTapUVE());

    EstablishUVE(tap, 1000U);

    ASSERT_EQ(tap.tappedA.size(), 2U);
    EXPECT_EQ(tap.tappedA[0].direction, PacketDirectionUVE::Outbound);
    EXPECT_EQ(tap.tappedA[0].timestampMs, 1000U);
    EXPECT_FALSE(tap.tappedA[0].multiplexed);
    EXPECT_FALSE(tap.tappedA[0].channel.has_value());
    EXPECT_EQ(tap.tappedA[1].direction, PacketDirectionUVE::Inbound);
    ASSERT_EQ(tap.tappedB.size(), 2U);
    EXPECT_EQ(tap.tappedB[0].direction, PacketDirectionUVE::Inbound);
    EXPECT_EQ(tap.tappedB[1].direction, PacketDirectionUVE::Outbound);

    // Tap straight into the capture ring: the records decode like stolen datagrams.
    PacketCaptureUVE capture;
    for (const PacketTapUVE& record : tap.tappedB) {
        capture.PushUVE(record.datagram, record.direction, record.timestampMs);
    }
    ASSERT_EQ(capture.SizeUVE(), 2U);
    ASSERT_NE(capture.TryAtUVE(0U), nullptr);
    EXPECT_EQ(capture.TryAtUVE(0U)->sessionKind, "hello");
    EXPECT_EQ(capture.TryAtUVE(0U)->summary, "hello v2 token 0x000000000000007b");
    ASSERT_NE(capture.TryAtUVE(1U), nullptr);
    EXPECT_EQ(capture.TryAtUVE(1U)->sessionKind, "welcome");
    EXPECT_FALSE(capture.TryAtUVE(1U)->malformed);
}

TEST(PacketTapUVETest, DataFramesTapBothDirections) {
    SessionTapUVE tap = MakeSessionTapUVE();
    EstablishUVE(tap, 1000U);
    WatchUVE(tap);

    ASSERT_TRUE(tap.a.SendDataUVE(std::vector<std::uint8_t>{1U, 2U, 3U}));
    ASSERT_EQ(tap.tappedA.size(), 1U);
    EXPECT_EQ(tap.tappedA[0].direction, PacketDirectionUVE::Outbound);
    EXPECT_EQ(tap.tappedA[0].timestampMs, 1000U);
    EXPECT_FALSE(tap.tappedA[0].channel.has_value());

    tap.b.UpdateUVE(1100U);
    ASSERT_EQ(tap.tappedB.size(), 1U);
    EXPECT_EQ(tap.tappedB[0].direction, PacketDirectionUVE::Inbound);
    EXPECT_EQ(tap.tappedB[0].timestampMs, 1100U);
    EXPECT_EQ(tap.tappedB[0].datagram,
              (std::vector<std::uint8_t>{static_cast<std::uint8_t>(SessionMessageTagUVE::Data),
                                         1U, 2U, 3U}));
    EXPECT_EQ(tap.b.TryReceiveDataUVE(),
              (std::optional<std::vector<std::uint8_t>>{std::vector<std::uint8_t>{1U, 2U, 3U}}));
}

TEST(PacketTapUVETest, MultiplexedChannelTapped) {
    SessionTapUVE tap = MakeMuxedTapUVE();
    WatchUVE(tap);
    EstablishUVE(tap, 1000U);
    const std::size_t sentBase = tap.tappedA.size();
    const std::size_t receivedBase = tap.tappedB.size();

    ASSERT_TRUE(tap.a.SendChannelDataUVE(7U, std::vector<std::uint8_t>{9U}));
    ASSERT_EQ(tap.tappedA.size(), sentBase + 1U);
    EXPECT_TRUE(tap.tappedA.back().multiplexed);
    ASSERT_TRUE(tap.tappedA.back().channel.has_value());
    EXPECT_EQ(*tap.tappedA.back().channel, 7U);

    tap.b.UpdateUVE(1100U);
    ASSERT_EQ(tap.tappedB.size(), receivedBase + 1U);
    EXPECT_EQ(tap.tappedB.back().direction, PacketDirectionUVE::Inbound);
    EXPECT_TRUE(tap.tappedB.back().multiplexed);
    ASSERT_TRUE(tap.tappedB.back().channel.has_value());
    EXPECT_EQ(*tap.tappedB.back().channel, 7U);
    EXPECT_EQ(tap.tappedB.back().datagram,
              (std::vector<std::uint8_t>{static_cast<std::uint8_t>(SessionMessageTagUVE::Data),
                                         7U, 9U}));
    EXPECT_EQ(tap.b.TryReceiveChannelDataUVE(7U),
              (std::optional<std::vector<std::uint8_t>>{std::vector<std::uint8_t>{9U}}));
}

TEST(PacketTapUVETest, ReplicatedPayloadTapsWithChannel) {
    MuxedReplicationTapUVE tap = MakeMuxedReplicationTapUVE();
    WatchUVE(tap);
    ASSERT_TRUE(tap.a->ConnectUVE(1000U, 123U));
    tap.b->UpdateUVE(1000U);
    tap.a->UpdateUVE(1000U);
    ASSERT_EQ(tap.a->StateUVE(), SessionStateUVE::Established);

    ASSERT_TRUE(tap.replicator->RegisterEntityUVE(
        7U, [] { return Math::Vector3UVE{1.0F, 2.0F, 3.0F}; },
        [](const Math::Vector3UVE&) {}));
    ASSERT_TRUE(tap.replicator->RegisterEntityUVE(
        9U, [] { return Math::Vector3UVE{4.0F, 5.0F, 6.0F}; },
        [](const Math::Vector3UVE&) {}));
    tap.replicator->UpdateUVE(1200U);

    std::vector<PacketTapUVE> payloads;
    for (const PacketTapUVE& record : tap.tappedA) {
        if (record.channel.has_value() && *record.channel == kReplicationChannelUVE) {
            payloads.push_back(record);
        }
    }
    ASSERT_FALSE(payloads.empty());
    const PacketTapUVE& payload = payloads.back();
    EXPECT_EQ(payload.direction, PacketDirectionUVE::Outbound);
    EXPECT_TRUE(payload.multiplexed);

    // The host maps its own channel byte to the inspector channel; the ring decodes.
    PacketCaptureUVE capture;
    capture.PushUVE(payload.datagram, payload.direction, payload.timestampMs,
                    InspectedChannelUVE::Replication, payload.multiplexed);
    ASSERT_EQ(capture.SizeUVE(), 1U);
    ASSERT_NE(capture.TryAtUVE(0U), nullptr);
    EXPECT_EQ(capture.TryAtUVE(0U)->dataKind, "snapshot");
    EXPECT_EQ(capture.TryAtUVE(0U)->summary, "snapshot 2 entries");

    tap.b->UpdateUVE(1200U);
    bool sawInbound = false;
    for (const PacketTapUVE& record : tap.tappedB) {
        if (record.direction == PacketDirectionUVE::Inbound && record.channel.has_value() &&
            *record.channel == kReplicationChannelUVE) {
            sawInbound = true;
        }
    }
    EXPECT_TRUE(sawInbound);
}

TEST(PacketTapUVETest, FailedSendsNotTapped) {
    SessionTapUVE tap = MakeSessionTapUVE();
    WatchUVE(tap);
    EXPECT_FALSE(tap.a.SendDataUVE(std::vector<std::uint8_t>{1U}));
    EXPECT_TRUE(tap.tappedA.empty());

    // Disconnected, so the establish below taps a hello: size-gate past that baseline.
    EstablishUVE(tap, 1000U);
    const std::size_t base = tap.tappedA.size();
    EXPECT_FALSE(tap.a.SendDataUVE(
        std::vector<std::uint8_t>(kSessionMaximumPayloadBytesUVE + 1U, 0U)));
    EXPECT_EQ(tap.tappedA.size(), base);
}

TEST(PacketTapUVETest, IgnoredGarbageStillTapped) {
    SessionTapUVE tap = MakeSessionTapUVE();
    EstablishUVE(tap, 1000U);
    WatchUVE(tap);

    ASSERT_TRUE(tap.link.a.SendUVE(std::vector<std::uint8_t>{0xFFU}));
    tap.b.UpdateUVE(1100U);
    ASSERT_EQ(tap.tappedB.size(), 1U);
    EXPECT_EQ(tap.tappedB[0].direction, PacketDirectionUVE::Inbound);
    EXPECT_EQ(tap.tappedB[0].datagram, (std::vector<std::uint8_t>{0xFFU}));
    EXPECT_FALSE(tap.tappedB[0].channel.has_value());
    // The session ignored it; the tap kept it.
    EXPECT_EQ(tap.b.StateUVE(), SessionStateUVE::Established);
    EXPECT_FALSE(tap.b.TryReceiveDataUVE().has_value());

    PacketCaptureUVE capture;
    capture.PushUVE(tap.tappedB[0].datagram, tap.tappedB[0].direction,
                    tap.tappedB[0].timestampMs);
    ASSERT_EQ(capture.SizeUVE(), 1U);
    ASSERT_NE(capture.TryAtUVE(0U), nullptr);
    EXPECT_TRUE(capture.TryAtUVE(0U)->malformed);
}

TEST(PacketTapUVETest, ClearTapStops) {
    SessionTapUVE tap = MakeSessionTapUVE();
    WatchUVE(tap);
    EstablishUVE(tap, 1000U);
    ASSERT_FALSE(tap.tappedA.empty());
    tap.a.ClearTapUVE();
    tap.b.ClearTapUVE();
    EXPECT_FALSE(tap.a.HasTapUVE());

    const std::size_t sentBase = tap.tappedA.size();
    const std::size_t receivedBase = tap.tappedB.size();
    ASSERT_TRUE(tap.a.SendDataUVE(std::vector<std::uint8_t>{5U}));
    tap.b.UpdateUVE(1100U);
    tap.a.UpdateUVE(1100U);
    EXPECT_EQ(tap.tappedA.size(), sentBase);
    EXPECT_EQ(tap.tappedB.size(), receivedBase);
}

TEST(PacketTapUVETest, TapTimestampsFollowThePump) {
    SessionTapUVE tap = MakeSessionTapUVE();
    WatchUVE(tap);
    ASSERT_TRUE(tap.a.ConnectUVE(1000U));
    ASSERT_EQ(tap.tappedA.size(), 1U);
    EXPECT_EQ(tap.tappedA[0].timestampMs, 1000U);
    tap.b.UpdateUVE(1000U);
    tap.a.UpdateUVE(1000U);

    tap.a.UpdateUVE(1500U);
    tap.b.UpdateUVE(1500U);
    ASSERT_TRUE(tap.a.SendDataUVE(std::vector<std::uint8_t>{5U}));
    EXPECT_EQ(tap.tappedA.back().timestampMs, 1500U);

    tap.b.UpdateUVE(1600U);
    EXPECT_EQ(tap.tappedB.back().timestampMs, 1600U);
}

} // namespace
} // namespace UVE::Network::Tests
