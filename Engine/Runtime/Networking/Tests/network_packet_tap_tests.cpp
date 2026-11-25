// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_packet_tap.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "vector3.h"
#include "network_packet_capture.h"
#include "network_replication.h"
#include "network_session.h"
#include "network_transport.h"

namespace UVE::Network::Tests {
namespace {

constexpr NetworkChannelId kReplicationChannel = 3U;

struct SessionTap final {
    LoopbackTransportPair link;
    NetworkSession a;
    NetworkSession b;
    std::vector<PacketTap> tappedA;
    std::vector<PacketTap> tappedB;
};

[[nodiscard]] SessionTap MakeSessionTap() {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession a(SessionRole::Client, std::make_unique<LoopbackEndpoint>(pair.a));
    NetworkSession b(SessionRole::Server, std::make_unique<LoopbackEndpoint>(pair.b));
    return SessionTap{std::move(pair), std::move(a), std::move(b), {}, {}};
}

[[nodiscard]] SessionTap MakeMuxedTap() {
    SessionTap tap = MakeSessionTap();
    EXPECT_TRUE(tap.a.EnableMultiplexing());
    EXPECT_TRUE(tap.b.EnableMultiplexing());
    return tap;
}

// Binds after the harness is in place: the lambdas capture the harness by reference, so
// binding before a move would dangle.
void Watch(SessionTap& tap) {
    tap.a.SetTap([&tap](const PacketTap& record) { tap.tappedA.push_back(record); });
    tap.b.SetTap([&tap](const PacketTap& record) { tap.tappedB.push_back(record); });
}

void Establish(SessionTap& tap, const std::uint64_t nowMs) {
    EXPECT_TRUE(tap.a.Connect(nowMs, 123U));
    tap.b.Update(nowMs);
    tap.a.Update(nowMs);
    EXPECT_EQ(tap.a.State(), SessionState::Established);
    EXPECT_EQ(tap.b.State(), SessionState::Established);
}

struct MuxedReplicationTap final {
    LoopbackTransportPair link;
    std::unique_ptr<NetworkSession> a;
    std::unique_ptr<NetworkSession> b;
    std::unique_ptr<NetworkReplicator> replicator;
    std::vector<PacketTap> tappedA;
    std::vector<PacketTap> tappedB;
};

[[nodiscard]] MuxedReplicationTap MakeMuxedReplicationTap() {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    auto a = std::make_unique<NetworkSession>(SessionRole::Client,
                                                 std::make_unique<LoopbackEndpoint>(pair.a));
    auto b = std::make_unique<NetworkSession>(SessionRole::Server,
                                                 std::make_unique<LoopbackEndpoint>(pair.b));
    EXPECT_TRUE(a->EnableMultiplexing());
    EXPECT_TRUE(b->EnableMultiplexing());
    auto replicator = std::make_unique<NetworkReplicator>(*a, kReplicationChannel);
    return MuxedReplicationTap{std::move(pair), std::move(a), std::move(b),
                                  std::move(replicator), {}, {}};
}

void Watch(MuxedReplicationTap& tap) {
    tap.a->SetTap(
        [&tap](const PacketTap& record) { tap.tappedA.push_back(record); });
    tap.b->SetTap(
        [&tap](const PacketTap& record) { tap.tappedB.push_back(record); });
}

TEST(PacketTapTest, HandshakeTappedBothDirections) {
    SessionTap tap = MakeSessionTap();
    EXPECT_FALSE(tap.a.HasTap());
    Watch(tap);
    EXPECT_TRUE(tap.a.HasTap());

    Establish(tap, 1000U);

    ASSERT_EQ(tap.tappedA.size(), 2U);
    EXPECT_EQ(tap.tappedA[0].direction, PacketDirection::Outbound);
    EXPECT_EQ(tap.tappedA[0].timestampMs, 1000U);
    EXPECT_FALSE(tap.tappedA[0].multiplexed);
    EXPECT_FALSE(tap.tappedA[0].channel.has_value());
    EXPECT_EQ(tap.tappedA[1].direction, PacketDirection::Inbound);
    ASSERT_EQ(tap.tappedB.size(), 2U);
    EXPECT_EQ(tap.tappedB[0].direction, PacketDirection::Inbound);
    EXPECT_EQ(tap.tappedB[1].direction, PacketDirection::Outbound);

    // Tap straight into the capture ring: the records decode like stolen datagrams.
    PacketCapture capture;
    for (const PacketTap& record : tap.tappedB) {
        capture.Push(record.datagram, record.direction, record.timestampMs);
    }
    ASSERT_EQ(capture.Size(), 2U);
    ASSERT_NE(capture.TryAt(0U), nullptr);
    EXPECT_EQ(capture.TryAt(0U)->sessionKind, "hello");
    EXPECT_EQ(capture.TryAt(0U)->summary, "hello v2 token 0x000000000000007b");
    ASSERT_NE(capture.TryAt(1U), nullptr);
    EXPECT_EQ(capture.TryAt(1U)->sessionKind, "welcome");
    EXPECT_FALSE(capture.TryAt(1U)->malformed);
}

TEST(PacketTapTest, DataFramesTapBothDirections) {
    SessionTap tap = MakeSessionTap();
    Establish(tap, 1000U);
    Watch(tap);

    ASSERT_TRUE(tap.a.SendData(std::vector<std::uint8_t>{1U, 2U, 3U}));
    ASSERT_EQ(tap.tappedA.size(), 1U);
    EXPECT_EQ(tap.tappedA[0].direction, PacketDirection::Outbound);
    EXPECT_EQ(tap.tappedA[0].timestampMs, 1000U);
    EXPECT_FALSE(tap.tappedA[0].channel.has_value());

    tap.b.Update(1100U);
    ASSERT_EQ(tap.tappedB.size(), 1U);
    EXPECT_EQ(tap.tappedB[0].direction, PacketDirection::Inbound);
    EXPECT_EQ(tap.tappedB[0].timestampMs, 1100U);
    EXPECT_EQ(tap.tappedB[0].datagram,
              (std::vector<std::uint8_t>{static_cast<std::uint8_t>(SessionMessageTag::Data),
                                         1U, 2U, 3U}));
    EXPECT_EQ(tap.b.TryReceiveData(),
              (std::optional<std::vector<std::uint8_t>>{std::vector<std::uint8_t>{1U, 2U, 3U}}));
}

TEST(PacketTapTest, MultiplexedChannelTapped) {
    SessionTap tap = MakeMuxedTap();
    Watch(tap);
    Establish(tap, 1000U);
    const std::size_t sentBase = tap.tappedA.size();
    const std::size_t receivedBase = tap.tappedB.size();

    ASSERT_TRUE(tap.a.SendChannelData(7U, std::vector<std::uint8_t>{9U}));
    ASSERT_EQ(tap.tappedA.size(), sentBase + 1U);
    EXPECT_TRUE(tap.tappedA.back().multiplexed);
    ASSERT_TRUE(tap.tappedA.back().channel.has_value());
    EXPECT_EQ(*tap.tappedA.back().channel, 7U);

    tap.b.Update(1100U);
    ASSERT_EQ(tap.tappedB.size(), receivedBase + 1U);
    EXPECT_EQ(tap.tappedB.back().direction, PacketDirection::Inbound);
    EXPECT_TRUE(tap.tappedB.back().multiplexed);
    ASSERT_TRUE(tap.tappedB.back().channel.has_value());
    EXPECT_EQ(*tap.tappedB.back().channel, 7U);
    EXPECT_EQ(tap.tappedB.back().datagram,
              (std::vector<std::uint8_t>{static_cast<std::uint8_t>(SessionMessageTag::Data),
                                         7U, 9U}));
    EXPECT_EQ(tap.b.TryReceiveChannelData(7U),
              (std::optional<std::vector<std::uint8_t>>{std::vector<std::uint8_t>{9U}}));
}

TEST(PacketTapTest, ReplicatedPayloadTapsWithChannel) {
    MuxedReplicationTap tap = MakeMuxedReplicationTap();
    Watch(tap);
    ASSERT_TRUE(tap.a->Connect(1000U, 123U));
    tap.b->Update(1000U);
    tap.a->Update(1000U);
    ASSERT_EQ(tap.a->State(), SessionState::Established);

    ASSERT_TRUE(tap.replicator->RegisterEntity(
        7U, [] { return Math::Vector3{1.0F, 2.0F, 3.0F}; },
        [](const Math::Vector3&) {}));
    ASSERT_TRUE(tap.replicator->RegisterEntity(
        9U, [] { return Math::Vector3{4.0F, 5.0F, 6.0F}; },
        [](const Math::Vector3&) {}));
    tap.replicator->Update(1200U);

    std::vector<PacketTap> payloads;
    for (const PacketTap& record : tap.tappedA) {
        if (record.channel.has_value() && *record.channel == kReplicationChannel) {
            payloads.push_back(record);
        }
    }
    ASSERT_FALSE(payloads.empty());
    const PacketTap& payload = payloads.back();
    EXPECT_EQ(payload.direction, PacketDirection::Outbound);
    EXPECT_TRUE(payload.multiplexed);

    // The host maps its own channel byte to the inspector channel; the ring decodes.
    PacketCapture capture;
    capture.Push(payload.datagram, payload.direction, payload.timestampMs,
                    InspectedChannel::Replication, payload.multiplexed);
    ASSERT_EQ(capture.Size(), 1U);
    ASSERT_NE(capture.TryAt(0U), nullptr);
    EXPECT_EQ(capture.TryAt(0U)->dataKind, "snapshot");
    EXPECT_EQ(capture.TryAt(0U)->summary, "snapshot 2 entries");

    tap.b->Update(1200U);
    bool sawInbound = false;
    for (const PacketTap& record : tap.tappedB) {
        if (record.direction == PacketDirection::Inbound && record.channel.has_value() &&
            *record.channel == kReplicationChannel) {
            sawInbound = true;
        }
    }
    EXPECT_TRUE(sawInbound);
}

TEST(PacketTapTest, FailedSendsNotTapped) {
    SessionTap tap = MakeSessionTap();
    Watch(tap);
    EXPECT_FALSE(tap.a.SendData(std::vector<std::uint8_t>{1U}));
    EXPECT_TRUE(tap.tappedA.empty());

    // Disconnected, so the establish below taps a hello: size-gate past that baseline.
    Establish(tap, 1000U);
    const std::size_t base = tap.tappedA.size();
    EXPECT_FALSE(tap.a.SendData(
        std::vector<std::uint8_t>(kSessionMaximumPayloadBytes + 1U, 0U)));
    EXPECT_EQ(tap.tappedA.size(), base);
}

TEST(PacketTapTest, IgnoredGarbageStillTapped) {
    SessionTap tap = MakeSessionTap();
    Establish(tap, 1000U);
    Watch(tap);

    ASSERT_TRUE(tap.link.a.Send(std::vector<std::uint8_t>{0xFFU}));
    tap.b.Update(1100U);
    ASSERT_EQ(tap.tappedB.size(), 1U);
    EXPECT_EQ(tap.tappedB[0].direction, PacketDirection::Inbound);
    EXPECT_EQ(tap.tappedB[0].datagram, (std::vector<std::uint8_t>{0xFFU}));
    EXPECT_FALSE(tap.tappedB[0].channel.has_value());
    // The session ignored it; the tap kept it.
    EXPECT_EQ(tap.b.State(), SessionState::Established);
    EXPECT_FALSE(tap.b.TryReceiveData().has_value());

    PacketCapture capture;
    capture.Push(tap.tappedB[0].datagram, tap.tappedB[0].direction,
                    tap.tappedB[0].timestampMs);
    ASSERT_EQ(capture.Size(), 1U);
    ASSERT_NE(capture.TryAt(0U), nullptr);
    EXPECT_TRUE(capture.TryAt(0U)->malformed);
}

TEST(PacketTapTest, ClearTapStops) {
    SessionTap tap = MakeSessionTap();
    Watch(tap);
    Establish(tap, 1000U);
    ASSERT_FALSE(tap.tappedA.empty());
    tap.a.ClearTap();
    tap.b.ClearTap();
    EXPECT_FALSE(tap.a.HasTap());

    const std::size_t sentBase = tap.tappedA.size();
    const std::size_t receivedBase = tap.tappedB.size();
    ASSERT_TRUE(tap.a.SendData(std::vector<std::uint8_t>{5U}));
    tap.b.Update(1100U);
    tap.a.Update(1100U);
    EXPECT_EQ(tap.tappedA.size(), sentBase);
    EXPECT_EQ(tap.tappedB.size(), receivedBase);
}

TEST(PacketTapTest, TapTimestampsFollowThePump) {
    SessionTap tap = MakeSessionTap();
    Watch(tap);
    ASSERT_TRUE(tap.a.Connect(1000U));
    ASSERT_EQ(tap.tappedA.size(), 1U);
    EXPECT_EQ(tap.tappedA[0].timestampMs, 1000U);
    tap.b.Update(1000U);
    tap.a.Update(1000U);

    tap.a.Update(1500U);
    tap.b.Update(1500U);
    ASSERT_TRUE(tap.a.SendData(std::vector<std::uint8_t>{5U}));
    EXPECT_EQ(tap.tappedA.back().timestampMs, 1500U);

    tap.b.Update(1600U);
    EXPECT_EQ(tap.tappedB.back().timestampMs, 1600U);
}

} // namespace
} // namespace UVE::Network::Tests
