// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_shot_uve.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "uve/network/network_inspector_uve.h"
#include "uve/network/network_session_uve.h"
#include "uve/network/network_transport_uve.h"

namespace UVE::Network::Tests {
namespace {

struct ShotHarnessUVE final {
    LoopbackTransportPairUVE link;
    NetworkSessionUVE client;
    NetworkSessionUVE server;

    void UpdateBothUVE(const std::uint64_t nowMs) {
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
    }

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

[[nodiscard]] ShotHarnessUVE MakeHarnessUVE(const bool multiplexed) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE client(SessionRoleUVE::Client,
                             std::make_unique<LoopbackEndpointUVE>(pair.a));
    NetworkSessionUVE server(SessionRoleUVE::Server,
                             std::make_unique<LoopbackEndpointUVE>(pair.b));
    if (multiplexed) {
        EXPECT_TRUE(client.EnableMultiplexingUVE());
        EXPECT_TRUE(server.EnableMultiplexingUVE());
    }
    return ShotHarnessUVE{std::move(pair), std::move(client), std::move(server)};
}

TEST(ShotIntentUVETest, CodecRoundTrip) {
    ShotIntentUVE intent;
    intent.pawn = 3U;
    intent.seq = 12U;
    intent.viewTick = 45U;
    intent.fraction = 0.5F;
    const std::vector<std::uint8_t> bytes = EncodeShotIntentUVE(intent);
    ASSERT_EQ(bytes.size(), kShotIntentBytesUVE);
    EXPECT_EQ(bytes[0], kShotIntentVersionUVE);
    const std::optional<ShotIntentUVE> decoded = DecodeShotIntentUVE(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, intent);

    // A zero view tick is meaningful (no view yet), not corruption.
    ShotIntentUVE noView = intent;
    noView.viewTick = kInvalidPredictionTickUVE;
    noView.fraction = 0.0F;
    EXPECT_EQ(DecodeShotIntentUVE(EncodeShotIntentUVE(noView)), noView);
}

TEST(ShotIntentUVETest, DecodeRejects) {
    ShotIntentUVE intent;
    intent.pawn = 1U;
    intent.seq = 2U;
    intent.viewTick = 3U;
    intent.fraction = 0.25F;
    const std::vector<std::uint8_t> bytes = EncodeShotIntentUVE(intent);

    EXPECT_FALSE(DecodeShotIntentUVE({}).has_value());
    EXPECT_FALSE(
        DecodeShotIntentUVE(std::vector<std::uint8_t>(bytes.begin(), bytes.end() - 1))
            .has_value());
    std::vector<std::uint8_t> longBytes = bytes;
    longBytes.push_back(0x00U);
    EXPECT_FALSE(DecodeShotIntentUVE(longBytes).has_value());

    std::vector<std::uint8_t> badVersion = bytes;
    badVersion[0] = 0x7FU;
    EXPECT_FALSE(DecodeShotIntentUVE(badVersion).has_value());

    for (const float fraction : {std::numeric_limits<float>::quiet_NaN(),
                                 std::numeric_limits<float>::infinity(),
                                 -std::numeric_limits<float>::infinity(), -0.5F, 1.0F, 2.0F}) {
        ShotIntentUVE bad = intent;
        bad.fraction = fraction;
        EXPECT_FALSE(DecodeShotIntentUVE(EncodeShotIntentUVE(bad)).has_value())
            << "fraction " << fraction;
    }
    // The edges of the valid range survive.
    ShotIntentUVE zero = intent;
    zero.fraction = 0.0F;
    EXPECT_TRUE(DecodeShotIntentUVE(EncodeShotIntentUVE(zero)).has_value());
    ShotIntentUVE almost = intent;
    almost.fraction = std::nextafter(1.0F, 0.0F);
    EXPECT_TRUE(DecodeShotIntentUVE(EncodeShotIntentUVE(almost)).has_value());
}

TEST(ShotIntentUVETest, SenderReceiverOverLegacySession) {
    ShotHarnessUVE harness = MakeHarnessUVE(false);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ShotSenderUVE sender(harness.client);
    ShotReceiverUVE receiver(harness.server);

    ASSERT_TRUE(sender.FireUVE(3U, 45U, 0.5F));
    ASSERT_TRUE(sender.FireUVE(3U, 46U, 0.25F));
    ASSERT_TRUE(sender.FireUVE(5U, 1U, 0.0F));
    harness.UpdateBothUVE(1100U);

    const std::vector<ShotIntentUVE> intents = receiver.PollUVE();
    ASSERT_EQ(intents.size(), 3U);
    EXPECT_EQ(intents[0].pawn, 3U);
    EXPECT_EQ(intents[0].seq, 1U);
    EXPECT_EQ(intents[0].viewTick, 45U);
    EXPECT_FLOAT_EQ(intents[0].fraction, 0.5F);
    EXPECT_EQ(intents[1].seq, 2U);
    EXPECT_EQ(intents[1].viewTick, 46U);
    EXPECT_EQ(intents[2].pawn, 5U);
    EXPECT_EQ(intents[2].seq, 3U);
    EXPECT_EQ(receiver.StatsUVE(), (ShotReceiverStatsUVE{3U, 0U}));
    EXPECT_TRUE(receiver.PollUVE().empty());
}

TEST(ShotIntentUVETest, SenderReceiverOverChannel) {
    ShotHarnessUVE harness = MakeHarnessUVE(true);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ShotSenderUVE sender(harness.client, 9U);
    ShotReceiverUVE receiver(harness.server, 9U);

    ASSERT_TRUE(sender.FireUVE(0U, 7U, 0.75F));
    harness.UpdateBothUVE(1100U);
    const std::vector<ShotIntentUVE> intents = receiver.PollUVE();
    ASSERT_EQ(intents.size(), 1U);
    EXPECT_EQ(intents[0].viewTick, 7U);
    EXPECT_FLOAT_EQ(intents[0].fraction, 0.75F);

    // The frame rode channel 9: no sibling queue and no legacy queue saw it.
    EXPECT_FALSE(harness.server.TryReceiveChannelDataUVE(8U).has_value());
    EXPECT_FALSE(harness.server.TryReceiveDataUVE().has_value());
}

TEST(ShotIntentUVETest, ReceiverDropsStaleAndMalformed) {
    ShotHarnessUVE harness = MakeHarnessUVE(false);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ShotSenderUVE sender(harness.client);
    ShotReceiverUVE receiver(harness.server);
    ASSERT_TRUE(sender.FireUVE(3U, 10U, 0.5F));
    harness.UpdateBothUVE(1100U);
    ASSERT_EQ(receiver.PollUVE().size(), 1U);

    // A replay of seq 1, an older seq 0, a short frame, and a bad fraction.
    ShotIntentUVE replay;
    replay.pawn = 3U;
    replay.seq = 1U;
    replay.viewTick = 10U;
    replay.fraction = 0.5F;
    const auto inject = [&](const std::vector<std::uint8_t>& payload) {
        // Reserved up front instead of constructed with the tag byte already in it: GCC 12 at -O3
        // otherwise reports the one-byte payload copy as an out-of-bounds memcpy, because the
        // branch that grows the buffer from capacity 1 to 2 loses its bound after inlining.
        std::vector<std::uint8_t> datagram;
        datagram.reserve(payload.size() + 1U);
        datagram.push_back(static_cast<std::uint8_t>(SessionMessageTagUVE::Data));
        datagram.insert(datagram.end(), payload.begin(), payload.end());
        return harness.link.a.SendUVE(datagram);
    };
    ASSERT_TRUE(inject(EncodeShotIntentUVE(replay)));
    ShotIntentUVE older = replay;
    older.seq = 0U;
    ASSERT_TRUE(inject(EncodeShotIntentUVE(older)));
    ASSERT_TRUE(inject(std::vector<std::uint8_t>{kShotIntentVersionUVE}));
    ShotIntentUVE bad = replay;
    bad.seq = 2U;
    bad.fraction = 2.0F;
    ASSERT_TRUE(inject(EncodeShotIntentUVE(bad)));
    // A sibling pawn's low seq is independent, not stale.
    ShotIntentUVE sibling = replay;
    sibling.pawn = 4U;
    ASSERT_TRUE(inject(EncodeShotIntentUVE(sibling)));
    harness.UpdateBothUVE(1200U);

    const std::vector<ShotIntentUVE> intents = receiver.PollUVE();
    ASSERT_EQ(intents.size(), 1U);
    EXPECT_EQ(intents[0].pawn, 4U);
    EXPECT_EQ(receiver.StatsUVE(), (ShotReceiverStatsUVE{2U, 4U}));
}

TEST(ShotIntentUVETest, FireRefusesInvalid) {
    ShotHarnessUVE harness = MakeHarnessUVE(false);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ShotSenderUVE sender(harness.client);
    ShotReceiverUVE receiver(harness.server);

    EXPECT_FALSE(sender.FireUVE(0U, 1U, 1.5F));
    EXPECT_FALSE(
        sender.FireUVE(0U, 1U, std::numeric_limits<float>::quiet_NaN()));
    harness.UpdateBothUVE(1100U);
    EXPECT_TRUE(receiver.PollUVE().empty());

    // No seq burned by the refusals: the next flight is seq 1.
    ASSERT_TRUE(sender.FireUVE(0U, 1U, 0.0F));
    harness.UpdateBothUVE(1200U);
    const std::vector<ShotIntentUVE> intents = receiver.PollUVE();
    ASSERT_EQ(intents.size(), 1U);
    EXPECT_EQ(intents[0].seq, 1U);
}

TEST(ShotIntentUVETest, FireFailsWhenSessionRefuses) {
    ShotHarnessUVE harness = MakeHarnessUVE(false);
    ShotSenderUVE sender(harness.client);
    EXPECT_FALSE(sender.FireUVE(0U, 1U, 0.5F));
}

TEST(ShotIntentUVETest, InspectorMirror) {
    ShotIntentUVE intent;
    intent.pawn = 3U;
    intent.seq = 12U;
    intent.viewTick = 45U;
    intent.fraction = 0.5F;
    std::vector<std::uint8_t> datagram{static_cast<std::uint8_t>(SessionMessageTagUVE::Data),
                                       12U};
    const std::vector<std::uint8_t> payload = EncodeShotIntentUVE(intent);
    datagram.insert(datagram.end(), payload.begin(), payload.end());

    const InspectedPacketUVE packet =
        InspectPacketUVE(datagram, PacketDirectionUVE::Outbound, 1000U,
                         InspectedChannelUVE::Shot, true);
    EXPECT_EQ(packet.dataKind, "shot");
    EXPECT_EQ(packet.summary, "fire pawn 3 view 45 +0.5 seq 12");
    EXPECT_FALSE(packet.malformed);
    ASSERT_TRUE(std::holds_alternative<ShotIntentViewUVE>(packet.data));
    EXPECT_EQ(std::get<ShotIntentViewUVE>(packet.data).viewTick, 45U);
    EXPECT_EQ(DescribePacketUVE(packet), "> [shot ch12] fire pawn 3 view 45 +0.5 seq 12");

    // Truncated past the tag and channel byte: the channel rejects it, malformed.
    const std::vector<std::uint8_t> shortDatagram(datagram.begin(), datagram.end() - 1);
    const InspectedPacketUVE broken =
        InspectPacketUVE(shortDatagram, PacketDirectionUVE::Outbound, 1000U,
                         InspectedChannelUVE::Shot, true);
    EXPECT_TRUE(broken.malformed);
    EXPECT_EQ(broken.dataKind, "malformed");
}

} // namespace
} // namespace UVE::Network::Tests
