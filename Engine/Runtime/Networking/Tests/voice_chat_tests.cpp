// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "voice_chat.h"

namespace UVE::Networking {
namespace {

TEST(VoiceChatTest, ImaAdpcmRoundTripPreservesWaveformAndCompresses4To1) {
    ImaAdpcmVoiceCodec codec;
    std::vector<float> input(kDefaultVoiceFrameSamples);
    for (std::size_t i = 0U; i < input.size(); ++i) {
        input[i] = 0.45F * std::sin(2.0F * 3.14159265F * 440.0F * static_cast<float>(i) / 16000.0F);
    }

    std::vector<std::uint8_t> encoded;
    codec.EncodeFrame(input, encoded);
    EXPECT_LE(encoded.size(), input.size() / 2U + 6U);

    std::vector<float> decoded;
    codec.DecodeFrame(encoded, input.size(), decoded);
    ASSERT_EQ(decoded.size(), input.size());

    double errSq = 0.0;
    for (std::size_t i = 0U; i < input.size(); ++i) {
        const double d = static_cast<double>(decoded[i] - input[i]);
        errSq += d * d;
    }
    const double rmse = std::sqrt(errSq / static_cast<double>(input.size()));
    EXPECT_LT(rmse, 0.05);
}

TEST(VoiceChatTest, VadPttJitterBufferReorderingAndSessionRoundTrip) {
    VoiceChatSession sender(7U);
    VoiceChatSession receiver(2U);

    // Silence rejected by VAD.
    std::vector<float> silence(kDefaultVoiceFrameSamples, 0.0F);
    VoiceFramePacket pkt{};
    EXPECT_FALSE(sender.CaptureAndEncode(silence, kDefaultVoiceSampleRateHz, pkt));

    // Active speech accepted, serialized, reordered, and recovered in order by jitter buffer.
    std::vector<float> speech(kDefaultVoiceFrameSamples);
    for (std::size_t i = 0U; i < speech.size(); ++i) {
        speech[i] = 0.35F * std::sin(0.15F * static_cast<float>(i));
    }

    std::vector<VoiceFramePacket> txPackets;
    for (int f = 0; f < 4; ++f) {
        VoiceFramePacket tx{};
        ASSERT_TRUE(sender.CaptureAndEncode(speech, kDefaultVoiceSampleRateHz, tx));
        const auto wire = SerializeVoicePacket(tx);
        VoiceFramePacket rx{};
        ASSERT_TRUE(DeserializeVoicePacket(wire, rx));
        EXPECT_EQ(rx.peerId, 7U);
        EXPECT_EQ(rx.sequence, static_cast<std::uint32_t>(f));
        txPackets.push_back(std::move(rx));
    }

    // Deliver out of order: 2, 0, 1, 3.
    receiver.ReceivePacket(txPackets[2]);
    receiver.ReceivePacket(txPackets[0]);
    receiver.ReceivePacket(txPackets[1]);
    receiver.ReceivePacket(txPackets[3]);
    EXPECT_EQ(receiver.ActivePeerCount(), 1U);

    for (int f = 0; f < 4; ++f) {
        std::vector<float> pcm;
        ASSERT_TRUE(receiver.PopPeerPlaybackPcm(7U, pcm));
        EXPECT_EQ(pcm.size(), kDefaultVoiceFrameSamples);
    }
}

} // namespace
} // namespace UVE::Networking
