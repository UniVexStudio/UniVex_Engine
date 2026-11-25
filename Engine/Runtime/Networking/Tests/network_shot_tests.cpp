// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_shot.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "network_inspector.h"
#include "network_session.h"
#include "network_transport.h"

namespace UVE::Network::Tests {
namespace {

struct ShotHarness final {
    LoopbackTransportPair link;
    NetworkSession client;
    NetworkSession server;

    void UpdateBoth(const std::uint64_t nowMs) {
        client.Update(nowMs);
        server.Update(nowMs);
    }

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

[[nodiscard]] ShotHarness MakeHarness(const bool multiplexed) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession client(SessionRole::Client,
                             std::make_unique<LoopbackEndpoint>(pair.a));
    NetworkSession server(SessionRole::Server,
                             std::make_unique<LoopbackEndpoint>(pair.b));
    if (multiplexed) {
        EXPECT_TRUE(client.EnableMultiplexing());
        EXPECT_TRUE(server.EnableMultiplexing());
    }
    return ShotHarness{std::move(pair), std::move(client), std::move(server)};
}

TEST(ShotIntentTest, CodecRoundTrip) {
    ShotIntent intent;
    intent.pawn = 3U;
    intent.seq = 12U;
    intent.viewTick = 45U;
    intent.fraction = 0.5F;
    const std::vector<std::uint8_t> bytes = EncodeShotIntent(intent);
    ASSERT_EQ(bytes.size(), kShotIntentBytes);
    EXPECT_EQ(bytes[0], kShotIntentVersion);
    const std::optional<ShotIntent> decoded = DecodeShotIntent(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, intent);

    // A zero view tick is meaningful (no view yet), not corruption.
    ShotIntent noView = intent;
    noView.viewTick = kInvalidPredictionTick;
    noView.fraction = 0.0F;
    EXPECT_EQ(DecodeShotIntent(EncodeShotIntent(noView)), noView);
}

TEST(ShotIntentTest, DecodeRejects) {
    ShotIntent intent;
    intent.pawn = 1U;
    intent.seq = 2U;
    intent.viewTick = 3U;
    intent.fraction = 0.25F;
    const std::vector<std::uint8_t> bytes = EncodeShotIntent(intent);

    EXPECT_FALSE(DecodeShotIntent({}).has_value());
    EXPECT_FALSE(
        DecodeShotIntent(std::vector<std::uint8_t>(bytes.begin(), bytes.end() - 1))
            .has_value());
    std::vector<std::uint8_t> longBytes = bytes;
    longBytes.push_back(0x00U);
    EXPECT_FALSE(DecodeShotIntent(longBytes).has_value());

    std::vector<std::uint8_t> badVersion = bytes;
    badVersion[0] = 0x7FU;
    EXPECT_FALSE(DecodeShotIntent(badVersion).has_value());

    for (const float fraction : {std::numeric_limits<float>::quiet_NaN(),
                                 std::numeric_limits<float>::infinity(),
                                 -std::numeric_limits<float>::infinity(), -0.5F, 1.0F, 2.0F}) {
        ShotIntent bad = intent;
        bad.fraction = fraction;
        EXPECT_FALSE(DecodeShotIntent(EncodeShotIntent(bad)).has_value())
            << "fraction " << fraction;
    }
    // The edges of the valid range survive.
    ShotIntent zero = intent;
    zero.fraction = 0.0F;
    EXPECT_TRUE(DecodeShotIntent(EncodeShotIntent(zero)).has_value());
    ShotIntent almost = intent;
    almost.fraction = std::nextafter(1.0F, 0.0F);
    EXPECT_TRUE(DecodeShotIntent(EncodeShotIntent(almost)).has_value());
}

TEST(ShotIntentTest, SenderReceiverOverLegacySession) {
    ShotHarness harness = MakeHarness(false);
    ASSERT_TRUE(harness.Establish(1000U));
    ShotSender sender(harness.client);
    ShotReceiver receiver(harness.server);

    ASSERT_TRUE(sender.Fire(3U, 45U, 0.5F));
    ASSERT_TRUE(sender.Fire(3U, 46U, 0.25F));
    ASSERT_TRUE(sender.Fire(5U, 1U, 0.0F));
    harness.UpdateBoth(1100U);

    const std::vector<ShotIntent> intents = receiver.Poll();
    ASSERT_EQ(intents.size(), 3U);
    EXPECT_EQ(intents[0].pawn, 3U);
    EXPECT_EQ(intents[0].seq, 1U);
    EXPECT_EQ(intents[0].viewTick, 45U);
    EXPECT_FLOAT_EQ(intents[0].fraction, 0.5F);
    EXPECT_EQ(intents[1].seq, 2U);
    EXPECT_EQ(intents[1].viewTick, 46U);
    EXPECT_EQ(intents[2].pawn, 5U);
    EXPECT_EQ(intents[2].seq, 3U);
    EXPECT_EQ(receiver.Stats(), (ShotReceiverStats{3U, 0U}));
    EXPECT_TRUE(receiver.Poll().empty());
}

TEST(ShotIntentTest, SenderReceiverOverChannel) {
    ShotHarness harness = MakeHarness(true);
    ASSERT_TRUE(harness.Establish(1000U));
    ShotSender sender(harness.client, 9U);
    ShotReceiver receiver(harness.server, 9U);

    ASSERT_TRUE(sender.Fire(0U, 7U, 0.75F));
    harness.UpdateBoth(1100U);
    const std::vector<ShotIntent> intents = receiver.Poll();
    ASSERT_EQ(intents.size(), 1U);
    EXPECT_EQ(intents[0].viewTick, 7U);
    EXPECT_FLOAT_EQ(intents[0].fraction, 0.75F);

    // The frame rode channel 9: no sibling queue and no legacy queue saw it.
    EXPECT_FALSE(harness.server.TryReceiveChannelData(8U).has_value());
    EXPECT_FALSE(harness.server.TryReceiveData().has_value());
}

TEST(ShotIntentTest, ReceiverDropsStaleAndMalformed) {
    ShotHarness harness = MakeHarness(false);
    ASSERT_TRUE(harness.Establish(1000U));
    ShotSender sender(harness.client);
    ShotReceiver receiver(harness.server);
    ASSERT_TRUE(sender.Fire(3U, 10U, 0.5F));
    harness.UpdateBoth(1100U);
    ASSERT_EQ(receiver.Poll().size(), 1U);

    // A replay of seq 1, an older seq 0, a short frame, and a bad fraction.
    ShotIntent replay;
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
        datagram.push_back(static_cast<std::uint8_t>(SessionMessageTag::Data));
        datagram.insert(datagram.end(), payload.begin(), payload.end());
        return harness.link.a.Send(datagram);
    };
    ASSERT_TRUE(inject(EncodeShotIntent(replay)));
    ShotIntent older = replay;
    older.seq = 0U;
    ASSERT_TRUE(inject(EncodeShotIntent(older)));
    ASSERT_TRUE(inject(std::vector<std::uint8_t>{kShotIntentVersion}));
    ShotIntent bad = replay;
    bad.seq = 2U;
    bad.fraction = 2.0F;
    ASSERT_TRUE(inject(EncodeShotIntent(bad)));
    // A sibling pawn's low seq is independent, not stale.
    ShotIntent sibling = replay;
    sibling.pawn = 4U;
    ASSERT_TRUE(inject(EncodeShotIntent(sibling)));
    harness.UpdateBoth(1200U);

    const std::vector<ShotIntent> intents = receiver.Poll();
    ASSERT_EQ(intents.size(), 1U);
    EXPECT_EQ(intents[0].pawn, 4U);
    EXPECT_EQ(receiver.Stats(), (ShotReceiverStats{2U, 4U}));
}

TEST(ShotIntentTest, FireRefusesInvalid) {
    ShotHarness harness = MakeHarness(false);
    ASSERT_TRUE(harness.Establish(1000U));
    ShotSender sender(harness.client);
    ShotReceiver receiver(harness.server);

    EXPECT_FALSE(sender.Fire(0U, 1U, 1.5F));
    EXPECT_FALSE(
        sender.Fire(0U, 1U, std::numeric_limits<float>::quiet_NaN()));
    harness.UpdateBoth(1100U);
    EXPECT_TRUE(receiver.Poll().empty());

    // No seq burned by the refusals: the next flight is seq 1.
    ASSERT_TRUE(sender.Fire(0U, 1U, 0.0F));
    harness.UpdateBoth(1200U);
    const std::vector<ShotIntent> intents = receiver.Poll();
    ASSERT_EQ(intents.size(), 1U);
    EXPECT_EQ(intents[0].seq, 1U);
}

TEST(ShotIntentTest, FireFailsWhenSessionRefuses) {
    ShotHarness harness = MakeHarness(false);
    ShotSender sender(harness.client);
    EXPECT_FALSE(sender.Fire(0U, 1U, 0.5F));
}

TEST(ShotIntentTest, InspectorMirror) {
    ShotIntent intent;
    intent.pawn = 3U;
    intent.seq = 12U;
    intent.viewTick = 45U;
    intent.fraction = 0.5F;
    std::vector<std::uint8_t> datagram{static_cast<std::uint8_t>(SessionMessageTag::Data),
                                       12U};
    const std::vector<std::uint8_t> payload = EncodeShotIntent(intent);
    datagram.insert(datagram.end(), payload.begin(), payload.end());

    const InspectedPacket packet =
        InspectPacket(datagram, PacketDirection::Outbound, 1000U,
                         InspectedChannel::Shot, true);
    EXPECT_EQ(packet.dataKind, "shot");
    EXPECT_EQ(packet.summary, "fire pawn 3 view 45 +0.5 seq 12");
    EXPECT_FALSE(packet.malformed);
    ASSERT_TRUE(std::holds_alternative<ShotIntentView>(packet.data));
    EXPECT_EQ(std::get<ShotIntentView>(packet.data).viewTick, 45U);
    EXPECT_EQ(DescribePacket(packet), "> [shot ch12] fire pawn 3 view 45 +0.5 seq 12");

    // Truncated past the tag and channel byte: the channel rejects it, malformed.
    const std::vector<std::uint8_t> shortDatagram(datagram.begin(), datagram.end() - 1);
    const InspectedPacket broken =
        InspectPacket(shortDatagram, PacketDirection::Outbound, 1000U,
                         InspectedChannel::Shot, true);
    EXPECT_TRUE(broken.malformed);
    EXPECT_EQ(broken.dataKind, "malformed");
}

} // namespace
} // namespace UVE::Network::Tests
