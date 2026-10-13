// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_packet_capture_uve.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "uve/math/vector3_uve.h"
#include "uve/network/network_replication_uve.h"
#include "uve/network/network_session_uve.h"
#include "uve/network/network_transport_uve.h"

namespace UVE::Network::Tests {
namespace {

struct SessionTapUVE final {
    LoopbackTransportPairUVE link;
    NetworkSessionUVE a;
    NetworkSessionUVE b;
};

[[nodiscard]] SessionTapUVE MakeSessionTapUVE() {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE a(SessionRoleUVE::Client, std::make_unique<LoopbackEndpointUVE>(pair.a));
    NetworkSessionUVE b(SessionRoleUVE::Server, std::make_unique<LoopbackEndpointUVE>(pair.b));
    return SessionTapUVE{std::move(pair), std::move(a), std::move(b)};
}

struct ReplicationTapUVE final {
    LoopbackTransportPairUVE link;
    NetworkReplicatorUVE a;
    NetworkReplicatorUVE b;

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        if (!a.SessionUVE().ConnectUVE(nowMs)) {
            return false;
        }
        a.UpdateUVE(nowMs);
        b.UpdateUVE(nowMs);
        a.UpdateUVE(nowMs);
        b.UpdateUVE(nowMs);
        return a.SessionUVE().StateUVE() == SessionStateUVE::Established &&
               b.SessionUVE().StateUVE() == SessionStateUVE::Established;
    }
};

[[nodiscard]] ReplicationTapUVE MakeReplicationTapUVE() {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE sessionA(SessionRoleUVE::Client,
                               std::make_unique<LoopbackEndpointUVE>(pair.a));
    NetworkSessionUVE sessionB(SessionRoleUVE::Server,
                               std::make_unique<LoopbackEndpointUVE>(pair.b));
    NetworkReplicatorUVE a(std::move(sessionA));
    NetworkReplicatorUVE b(std::move(sessionB));
    return ReplicationTapUVE{std::move(pair), std::move(a), std::move(b)};
}

[[nodiscard]] std::vector<std::uint8_t> StealHelloUVE(SessionTapUVE& tap) {
    EXPECT_TRUE(tap.a.ConnectUVE(1000U, 123U));
    const std::optional<std::vector<std::uint8_t>> hello = tap.link.b.TryReceiveUVE();
    EXPECT_TRUE(hello.has_value());
    return hello.value_or(std::vector<std::uint8_t>{});
}

[[nodiscard]] std::vector<std::uint8_t> StealPayloadUVE(ReplicationTapUVE& tap) {
    EXPECT_TRUE(tap.EstablishUVE(1000U));
    // Drain the handshake traffic: the steal below must be the post-registration frame.
    while (tap.link.b.TryReceiveUVE().has_value()) {
    }
    EXPECT_TRUE(tap.a.RegisterEntityUVE(7U, [] { return Math::Vector3UVE{1.0F, 2.0F, 3.0F}; },
                                        [](const Math::Vector3UVE&) {}));
    EXPECT_TRUE(tap.a.RegisterEntityUVE(9U, [] { return Math::Vector3UVE{4.0F, 5.0F, 6.0F}; },
                                        [](const Math::Vector3UVE&) {}));
    tap.a.UpdateUVE(1200U);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceiveUVE();
    EXPECT_TRUE(wire.has_value());
    return wire.value_or(std::vector<std::uint8_t>{});
}

TEST(PacketCaptureUVETest, PushDecodesSessionLayer) {
    SessionTapUVE tap = MakeSessionTapUVE();
    PacketCaptureUVE capture;
    capture.PushUVE(StealHelloUVE(tap), PacketDirectionUVE::Outbound, 1000U);

    ASSERT_EQ(capture.SizeUVE(), 1U);
    EXPECT_EQ(capture.CapturedUVE(), 1U);
    EXPECT_EQ(capture.DropsUVE(), 0U);
    const InspectedPacketUVE* const packet = capture.TryAtUVE(0U);
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->sessionKind, "hello");
    ASSERT_TRUE(packet->helloToken.has_value());
    EXPECT_EQ(*packet->helloToken, 123U);
    EXPECT_EQ(packet->summary, "hello v2 token 0x000000000000007b");
    EXPECT_FALSE(packet->malformed);
}

TEST(PacketCaptureUVETest, PushDecodesReplicationPayload) {
    ReplicationTapUVE tap = MakeReplicationTapUVE();
    PacketCaptureUVE capture;
    capture.PushUVE(StealPayloadUVE(tap), PacketDirectionUVE::Outbound, 1200U,
                    InspectedChannelUVE::Replication);

    ASSERT_EQ(capture.SizeUVE(), 1U);
    const InspectedPacketUVE* const packet = capture.TryAtUVE(0U);
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->dataKind, "delta");
    ASSERT_TRUE(std::holds_alternative<ReplicationSnapshotViewUVE>(packet->data));
    EXPECT_EQ(std::get<ReplicationSnapshotViewUVE>(packet->data).entryCount, 2U);
    EXPECT_EQ(packet->summary, "delta 2 entries");
    EXPECT_FALSE(packet->malformed);
}

TEST(PacketCaptureUVETest, RingDropsOldestPastCap) {
    SessionTapUVE tap = MakeSessionTapUVE();
    ReplicationTapUVE replication = MakeReplicationTapUVE();
    PacketCaptureConfigUVE config;
    config.maxPackets = 2U;
    PacketCaptureUVE capture(config);

    capture.PushUVE(StealHelloUVE(tap), PacketDirectionUVE::Outbound, 1000U);
    capture.PushUVE(StealPayloadUVE(replication), PacketDirectionUVE::Outbound, 1200U,
                    InspectedChannelUVE::Replication);
    capture.PushUVE(std::vector<std::uint8_t>{0xFFU}, PacketDirectionUVE::Inbound, 1300U,
                    InspectedChannelUVE::Replication);

    EXPECT_EQ(capture.SizeUVE(), 2U);
    EXPECT_EQ(capture.CapturedUVE(), 3U);
    EXPECT_EQ(capture.DropsUVE(), 1U);
    // The hello fell off: oldest-first now starts at the snapshot.
    ASSERT_NE(capture.TryAtUVE(0U), nullptr);
    EXPECT_EQ(capture.TryAtUVE(0U)->dataKind, "delta");
    ASSERT_NE(capture.TryAtUVE(1U), nullptr);
    EXPECT_TRUE(capture.TryAtUVE(1U)->malformed);
}

TEST(PacketCaptureUVETest, ZeroCapacityDisables) {
    SessionTapUVE tap = MakeSessionTapUVE();
    PacketCaptureConfigUVE config;
    config.maxPackets = 0U;
    PacketCaptureUVE capture(config);

    const std::vector<std::uint8_t> hello = StealHelloUVE(tap);
    capture.PushUVE(hello, PacketDirectionUVE::Outbound, 1000U);
    capture.PushUVE(hello, PacketDirectionUVE::Outbound, 1000U);
    EXPECT_EQ(capture.SizeUVE(), 0U);
    EXPECT_TRUE(capture.EmptyUVE());
    EXPECT_EQ(capture.CapturedUVE(), 2U);
    EXPECT_EQ(capture.DropsUVE(), 2U);
    EXPECT_EQ(capture.TryAtUVE(0U), nullptr);
}

TEST(PacketCaptureUVETest, ClearResetsAll) {
    SessionTapUVE tap = MakeSessionTapUVE();
    PacketCaptureConfigUVE config;
    config.maxPackets = 1U;
    PacketCaptureUVE capture(config);
    capture.PushUVE(StealHelloUVE(tap), PacketDirectionUVE::Outbound, 1000U);
    capture.PushUVE(std::vector<std::uint8_t>{0xFFU}, PacketDirectionUVE::Inbound, 1100U);
    ASSERT_EQ(capture.DropsUVE(), 1U);

    capture.ClearUVE();
    EXPECT_EQ(capture.SizeUVE(), 0U);
    EXPECT_TRUE(capture.EmptyUVE());
    EXPECT_EQ(capture.CapturedUVE(), 0U);
    EXPECT_EQ(capture.DropsUVE(), 0U);
    EXPECT_EQ(capture.MalformedUVE(), 0U);
}

TEST(PacketCaptureUVETest, TryAtBounds) {
    SessionTapUVE tap = MakeSessionTapUVE();
    PacketCaptureUVE capture;
    EXPECT_EQ(capture.TryAtUVE(0U), nullptr);
    capture.PushUVE(StealHelloUVE(tap), PacketDirectionUVE::Outbound, 1000U);
    EXPECT_NE(capture.TryAtUVE(0U), nullptr);
    EXPECT_EQ(capture.TryAtUVE(1U), nullptr);
    EXPECT_EQ(capture.TryAtUVE(99U), nullptr);
}

TEST(PacketCaptureUVETest, FilterDirection) {
    SessionTapUVE tap = MakeSessionTapUVE();
    ASSERT_TRUE(tap.a.ConnectUVE(1000U, 123U));
    const std::optional<std::vector<std::uint8_t>> hello = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(hello.has_value());
    ASSERT_TRUE(tap.link.a.SendUVE(*hello));
    tap.b.UpdateUVE(1000U);
    const std::optional<std::vector<std::uint8_t>> welcome = tap.link.a.TryReceiveUVE();
    ASSERT_TRUE(welcome.has_value());

    PacketCaptureUVE capture;
    capture.PushUVE(*hello, PacketDirectionUVE::Outbound, 1000U);
    capture.PushUVE(*welcome, PacketDirectionUVE::Inbound, 1000U);

    PacketCaptureFilterUVE outbound;
    outbound.direction = PacketCaptureDirectionUVE::Outbound;
    EXPECT_EQ(capture.FilteredUVE(outbound), (std::vector<std::size_t>{0U}));
    PacketCaptureFilterUVE inbound;
    inbound.direction = PacketCaptureDirectionUVE::Inbound;
    EXPECT_EQ(capture.FilteredUVE(inbound), (std::vector<std::size_t>{1U}));
    EXPECT_EQ(capture.FilteredUVE(PacketCaptureFilterUVE{}),
              (std::vector<std::size_t>{0U, 1U}));
}

TEST(PacketCaptureUVETest, FilterChannelAndMalformed) {
    SessionTapUVE tap = MakeSessionTapUVE();
    ReplicationTapUVE replication = MakeReplicationTapUVE();
    PacketCaptureUVE capture;
    capture.PushUVE(StealPayloadUVE(replication), PacketDirectionUVE::Outbound, 1200U,
                    InspectedChannelUVE::Replication);
    capture.PushUVE(StealHelloUVE(tap), PacketDirectionUVE::Outbound, 1000U);
    capture.PushUVE(std::vector<std::uint8_t>{0xFFU}, PacketDirectionUVE::Inbound, 1300U,
                    InspectedChannelUVE::Replication);

    PacketCaptureFilterUVE channel;
    channel.channel = InspectedChannelUVE::Replication;
    EXPECT_EQ(capture.FilteredUVE(channel), (std::vector<std::size_t>{0U, 2U}));
    PacketCaptureFilterUVE broken;
    broken.malformedOnly = true;
    EXPECT_EQ(capture.FilteredUVE(broken), (std::vector<std::size_t>{2U}));
    PacketCaptureFilterUVE rpc;
    rpc.channel = InspectedChannelUVE::Rpc;
    EXPECT_TRUE(capture.FilteredUVE(rpc).empty());
}

TEST(PacketCaptureUVETest, FilterText) {
    SessionTapUVE tap = MakeSessionTapUVE();
    ReplicationTapUVE replication = MakeReplicationTapUVE();
    PacketCaptureUVE capture;
    capture.PushUVE(StealPayloadUVE(replication), PacketDirectionUVE::Outbound, 1200U,
                    InspectedChannelUVE::Replication);
    capture.PushUVE(StealHelloUVE(tap), PacketDirectionUVE::Outbound, 1000U);

    PacketCaptureFilterUVE delta;
    delta.text = "delta";
    EXPECT_EQ(capture.FilteredUVE(delta), (std::vector<std::size_t>{0U}));
    PacketCaptureFilterUVE hello;
    hello.text = "hello";
    EXPECT_EQ(capture.FilteredUVE(hello), (std::vector<std::size_t>{1U}));
    PacketCaptureFilterUVE missing;
    missing.text = "zzz-no-such-packet";
    EXPECT_TRUE(capture.FilteredUVE(missing).empty());
}

TEST(PacketCaptureUVETest, MalformedCountingFollowsStored) {
    PacketCaptureConfigUVE config;
    config.maxPackets = 2U;
    PacketCaptureUVE capture(config);
    const std::vector<std::uint8_t> garbage{0xFFU};
    capture.PushUVE(garbage, PacketDirectionUVE::Inbound, 1000U);
    capture.PushUVE(garbage, PacketDirectionUVE::Inbound, 1100U);
    ASSERT_EQ(capture.MalformedUVE(), 2U);

    // The oldest (malformed) drops off: dropped packets stop counting.
    SessionTapUVE tap = MakeSessionTapUVE();
    capture.PushUVE(StealHelloUVE(tap), PacketDirectionUVE::Outbound, 1200U);
    EXPECT_EQ(capture.SizeUVE(), 2U);
    EXPECT_EQ(capture.MalformedUVE(), 1U);
}

TEST(PacketCaptureUVETest, HexdumpFormatsRows) {
    // 5 bytes: 15 hex chars used of 48, so 33 spaces of padding before the ASCII bar.
    const std::vector<std::uint8_t> hello{0x48U, 0x65U, 0x6CU, 0x6CU, 0x6FU};
    EXPECT_EQ(HexdumpPacketUVE(hello),
              "00000000: 48 65 6C 6C 6F " + std::string(33, ' ') + "|Hello|");

    // Non-printables render as dots.
    const std::vector<std::uint8_t> mixed{0x00U, 0x41U, 0xFFU};
    EXPECT_EQ(HexdumpPacketUVE(mixed),
              "00000000: 00 41 FF " + std::string(39, ' ') + "|.A.|");

    // 17 bytes split 16 + 1 with the second row's offset past the first.
    std::vector<std::uint8_t> seventeen;
    for (std::uint8_t i = 0U; i < 17U; ++i) {
        seventeen.push_back(i);
    }
    const std::string dump = HexdumpPacketUVE(seventeen);
    ASSERT_EQ(dump.find('\n'), 76U);
    EXPECT_EQ(dump.substr(0U, 76U),
              "00000000: 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F "
              "|................|");
    EXPECT_EQ(dump.substr(77U), "00000010: 10 " + std::string(45, ' ') + "|.|");

    EXPECT_TRUE(HexdumpPacketUVE({}).empty());
    EXPECT_EQ(HexdumpPacketUVE(hello, 0U), HexdumpPacketUVE(hello, 16U));
}

} // namespace
} // namespace UVE::Network::Tests
