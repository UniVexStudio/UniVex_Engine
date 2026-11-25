// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_packet_capture.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "vector3.h"
#include "network_replication.h"
#include "network_session.h"
#include "network_transport.h"

namespace UVE::Network::Tests {
namespace {

struct SessionTap final {
    LoopbackTransportPair link;
    NetworkSession a;
    NetworkSession b;
};

[[nodiscard]] SessionTap MakeSessionTap() {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession a(SessionRole::Client, std::make_unique<LoopbackEndpoint>(pair.a));
    NetworkSession b(SessionRole::Server, std::make_unique<LoopbackEndpoint>(pair.b));
    return SessionTap{std::move(pair), std::move(a), std::move(b)};
}

struct ReplicationTap final {
    LoopbackTransportPair link;
    NetworkReplicator a;
    NetworkReplicator b;

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        if (!a.Session().Connect(nowMs)) {
            return false;
        }
        a.Update(nowMs);
        b.Update(nowMs);
        a.Update(nowMs);
        b.Update(nowMs);
        return a.Session().State() == SessionState::Established &&
               b.Session().State() == SessionState::Established;
    }
};

[[nodiscard]] ReplicationTap MakeReplicationTap() {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession sessionA(SessionRole::Client,
                               std::make_unique<LoopbackEndpoint>(pair.a));
    NetworkSession sessionB(SessionRole::Server,
                               std::make_unique<LoopbackEndpoint>(pair.b));
    NetworkReplicator a(std::move(sessionA));
    NetworkReplicator b(std::move(sessionB));
    return ReplicationTap{std::move(pair), std::move(a), std::move(b)};
}

[[nodiscard]] std::vector<std::uint8_t> StealHello(SessionTap& tap) {
    EXPECT_TRUE(tap.a.Connect(1000U, 123U));
    const std::optional<std::vector<std::uint8_t>> hello = tap.link.b.TryReceive();
    EXPECT_TRUE(hello.has_value());
    return hello.value_or(std::vector<std::uint8_t>{});
}

[[nodiscard]] std::vector<std::uint8_t> StealPayload(ReplicationTap& tap) {
    EXPECT_TRUE(tap.Establish(1000U));
    // Drain the handshake traffic: the steal below must be the post-registration frame.
    while (tap.link.b.TryReceive().has_value()) {
    }
    EXPECT_TRUE(tap.a.RegisterEntity(7U, [] { return Math::Vector3{1.0F, 2.0F, 3.0F}; },
                                        [](const Math::Vector3&) {}));
    EXPECT_TRUE(tap.a.RegisterEntity(9U, [] { return Math::Vector3{4.0F, 5.0F, 6.0F}; },
                                        [](const Math::Vector3&) {}));
    tap.a.Update(1200U);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceive();
    EXPECT_TRUE(wire.has_value());
    return wire.value_or(std::vector<std::uint8_t>{});
}

TEST(PacketCaptureTest, PushDecodesSessionLayer) {
    SessionTap tap = MakeSessionTap();
    PacketCapture capture;
    capture.Push(StealHello(tap), PacketDirection::Outbound, 1000U);

    ASSERT_EQ(capture.Size(), 1U);
    EXPECT_EQ(capture.Captured(), 1U);
    EXPECT_EQ(capture.Drops(), 0U);
    const InspectedPacket* const packet = capture.TryAt(0U);
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->sessionKind, "hello");
    ASSERT_TRUE(packet->helloToken.has_value());
    EXPECT_EQ(*packet->helloToken, 123U);
    EXPECT_EQ(packet->summary, "hello v2 token 0x000000000000007b");
    EXPECT_FALSE(packet->malformed);
}

TEST(PacketCaptureTest, PushDecodesReplicationPayload) {
    ReplicationTap tap = MakeReplicationTap();
    PacketCapture capture;
    capture.Push(StealPayload(tap), PacketDirection::Outbound, 1200U,
                    InspectedChannel::Replication);

    ASSERT_EQ(capture.Size(), 1U);
    const InspectedPacket* const packet = capture.TryAt(0U);
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->dataKind, "delta");
    ASSERT_TRUE(std::holds_alternative<ReplicationSnapshotView>(packet->data));
    EXPECT_EQ(std::get<ReplicationSnapshotView>(packet->data).entryCount, 2U);
    EXPECT_EQ(packet->summary, "delta 2 entries");
    EXPECT_FALSE(packet->malformed);
}

TEST(PacketCaptureTest, RingDropsOldestPastCap) {
    SessionTap tap = MakeSessionTap();
    ReplicationTap replication = MakeReplicationTap();
    PacketCaptureConfig config;
    config.maxPackets = 2U;
    PacketCapture capture(config);

    capture.Push(StealHello(tap), PacketDirection::Outbound, 1000U);
    capture.Push(StealPayload(replication), PacketDirection::Outbound, 1200U,
                    InspectedChannel::Replication);
    capture.Push(std::vector<std::uint8_t>{0xFFU}, PacketDirection::Inbound, 1300U,
                    InspectedChannel::Replication);

    EXPECT_EQ(capture.Size(), 2U);
    EXPECT_EQ(capture.Captured(), 3U);
    EXPECT_EQ(capture.Drops(), 1U);
    // The hello fell off: oldest-first now starts at the snapshot.
    ASSERT_NE(capture.TryAt(0U), nullptr);
    EXPECT_EQ(capture.TryAt(0U)->dataKind, "delta");
    ASSERT_NE(capture.TryAt(1U), nullptr);
    EXPECT_TRUE(capture.TryAt(1U)->malformed);
}

TEST(PacketCaptureTest, ZeroCapacityDisables) {
    SessionTap tap = MakeSessionTap();
    PacketCaptureConfig config;
    config.maxPackets = 0U;
    PacketCapture capture(config);

    const std::vector<std::uint8_t> hello = StealHello(tap);
    capture.Push(hello, PacketDirection::Outbound, 1000U);
    capture.Push(hello, PacketDirection::Outbound, 1000U);
    EXPECT_EQ(capture.Size(), 0U);
    EXPECT_TRUE(capture.Empty());
    EXPECT_EQ(capture.Captured(), 2U);
    EXPECT_EQ(capture.Drops(), 2U);
    EXPECT_EQ(capture.TryAt(0U), nullptr);
}

TEST(PacketCaptureTest, ClearResetsAll) {
    SessionTap tap = MakeSessionTap();
    PacketCaptureConfig config;
    config.maxPackets = 1U;
    PacketCapture capture(config);
    capture.Push(StealHello(tap), PacketDirection::Outbound, 1000U);
    capture.Push(std::vector<std::uint8_t>{0xFFU}, PacketDirection::Inbound, 1100U);
    ASSERT_EQ(capture.Drops(), 1U);

    capture.Clear();
    EXPECT_EQ(capture.Size(), 0U);
    EXPECT_TRUE(capture.Empty());
    EXPECT_EQ(capture.Captured(), 0U);
    EXPECT_EQ(capture.Drops(), 0U);
    EXPECT_EQ(capture.Malformed(), 0U);
}

TEST(PacketCaptureTest, TryAtBounds) {
    SessionTap tap = MakeSessionTap();
    PacketCapture capture;
    EXPECT_EQ(capture.TryAt(0U), nullptr);
    capture.Push(StealHello(tap), PacketDirection::Outbound, 1000U);
    EXPECT_NE(capture.TryAt(0U), nullptr);
    EXPECT_EQ(capture.TryAt(1U), nullptr);
    EXPECT_EQ(capture.TryAt(99U), nullptr);
}

TEST(PacketCaptureTest, FilterDirection) {
    SessionTap tap = MakeSessionTap();
    ASSERT_TRUE(tap.a.Connect(1000U, 123U));
    const std::optional<std::vector<std::uint8_t>> hello = tap.link.b.TryReceive();
    ASSERT_TRUE(hello.has_value());
    ASSERT_TRUE(tap.link.a.Send(*hello));
    tap.b.Update(1000U);
    const std::optional<std::vector<std::uint8_t>> welcome = tap.link.a.TryReceive();
    ASSERT_TRUE(welcome.has_value());

    PacketCapture capture;
    capture.Push(*hello, PacketDirection::Outbound, 1000U);
    capture.Push(*welcome, PacketDirection::Inbound, 1000U);

    PacketCaptureFilter outbound;
    outbound.direction = PacketCaptureDirection::Outbound;
    EXPECT_EQ(capture.Filtered(outbound), (std::vector<std::size_t>{0U}));
    PacketCaptureFilter inbound;
    inbound.direction = PacketCaptureDirection::Inbound;
    EXPECT_EQ(capture.Filtered(inbound), (std::vector<std::size_t>{1U}));
    EXPECT_EQ(capture.Filtered(PacketCaptureFilter{}),
              (std::vector<std::size_t>{0U, 1U}));
}

TEST(PacketCaptureTest, FilterChannelAndMalformed) {
    SessionTap tap = MakeSessionTap();
    ReplicationTap replication = MakeReplicationTap();
    PacketCapture capture;
    capture.Push(StealPayload(replication), PacketDirection::Outbound, 1200U,
                    InspectedChannel::Replication);
    capture.Push(StealHello(tap), PacketDirection::Outbound, 1000U);
    capture.Push(std::vector<std::uint8_t>{0xFFU}, PacketDirection::Inbound, 1300U,
                    InspectedChannel::Replication);

    PacketCaptureFilter channel;
    channel.channel = InspectedChannel::Replication;
    EXPECT_EQ(capture.Filtered(channel), (std::vector<std::size_t>{0U, 2U}));
    PacketCaptureFilter broken;
    broken.malformedOnly = true;
    EXPECT_EQ(capture.Filtered(broken), (std::vector<std::size_t>{2U}));
    PacketCaptureFilter rpc;
    rpc.channel = InspectedChannel::Rpc;
    EXPECT_TRUE(capture.Filtered(rpc).empty());
}

TEST(PacketCaptureTest, FilterText) {
    SessionTap tap = MakeSessionTap();
    ReplicationTap replication = MakeReplicationTap();
    PacketCapture capture;
    capture.Push(StealPayload(replication), PacketDirection::Outbound, 1200U,
                    InspectedChannel::Replication);
    capture.Push(StealHello(tap), PacketDirection::Outbound, 1000U);

    PacketCaptureFilter delta;
    delta.text = "delta";
    EXPECT_EQ(capture.Filtered(delta), (std::vector<std::size_t>{0U}));
    PacketCaptureFilter hello;
    hello.text = "hello";
    EXPECT_EQ(capture.Filtered(hello), (std::vector<std::size_t>{1U}));
    PacketCaptureFilter missing;
    missing.text = "zzz-no-such-packet";
    EXPECT_TRUE(capture.Filtered(missing).empty());
}

TEST(PacketCaptureTest, MalformedCountingFollowsStored) {
    PacketCaptureConfig config;
    config.maxPackets = 2U;
    PacketCapture capture(config);
    const std::vector<std::uint8_t> garbage{0xFFU};
    capture.Push(garbage, PacketDirection::Inbound, 1000U);
    capture.Push(garbage, PacketDirection::Inbound, 1100U);
    ASSERT_EQ(capture.Malformed(), 2U);

    // The oldest (malformed) drops off: dropped packets stop counting.
    SessionTap tap = MakeSessionTap();
    capture.Push(StealHello(tap), PacketDirection::Outbound, 1200U);
    EXPECT_EQ(capture.Size(), 2U);
    EXPECT_EQ(capture.Malformed(), 1U);
}

TEST(PacketCaptureTest, HexdumpFormatsRows) {
    // 5 bytes: 15 hex chars used of 48, so 33 spaces of padding before the ASCII bar.
    const std::vector<std::uint8_t> hello{0x48U, 0x65U, 0x6CU, 0x6CU, 0x6FU};
    EXPECT_EQ(HexdumpPacket(hello),
              "00000000: 48 65 6C 6C 6F " + std::string(33, ' ') + "|Hello|");

    // Non-printables render as dots.
    const std::vector<std::uint8_t> mixed{0x00U, 0x41U, 0xFFU};
    EXPECT_EQ(HexdumpPacket(mixed),
              "00000000: 00 41 FF " + std::string(39, ' ') + "|.A.|");

    // 17 bytes split 16 + 1 with the second row's offset past the first.
    std::vector<std::uint8_t> seventeen;
    for (std::uint8_t i = 0U; i < 17U; ++i) {
        seventeen.push_back(i);
    }
    const std::string dump = HexdumpPacket(seventeen);
    ASSERT_EQ(dump.find('\n'), 76U);
    EXPECT_EQ(dump.substr(0U, 76U),
              "00000000: 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F "
              "|................|");
    EXPECT_EQ(dump.substr(77U), "00000010: 10 " + std::string(45, ' ') + "|.|");

    EXPECT_TRUE(HexdumpPacket({}).empty());
    EXPECT_EQ(HexdumpPacket(hello, 0U), HexdumpPacket(hello, 16U));
}

} // namespace
} // namespace UVE::Network::Tests
