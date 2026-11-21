// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace UVE::Networking {

inline constexpr std::uint32_t kDefaultVoiceSampleRateHzUVE = 16000U;
inline constexpr std::size_t kDefaultVoiceFrameSamplesUVE = 320U; // 20 ms @ 16 kHz

struct VoiceFramePacketUVE final {
    std::uint32_t peerId = 0U;
    std::uint32_t sequence = 0U;
    std::uint32_t sampleRateHz = kDefaultVoiceSampleRateHzUVE;
    std::uint16_t sampleCount = 0U;
    std::uint8_t codecId = 1U; // 1 = IMA-ADPCM
    std::vector<std::uint8_t> payload;
};

[[nodiscard]] std::vector<std::uint8_t> SerializeVoicePacketUVE(const VoiceFramePacketUVE& packet);
[[nodiscard]] bool DeserializeVoicePacketUVE(std::span<const std::uint8_t> bytes,
                                             VoiceFramePacketUVE& outPacket);

/// Pluggable voice frame codec interface (default: IMA-ADPCM 4:1; Opus can drop in cleanly).
class IVoiceCodecUVE {
public:
    virtual ~IVoiceCodecUVE() = default;
    [[nodiscard]] virtual std::uint8_t CodecIdUVE() const noexcept = 0;
    virtual void EncodeFrameUVE(std::span<const float> monoPcm,
                                std::vector<std::uint8_t>& outEncoded) = 0;
    virtual void DecodeFrameUVE(std::span<const std::uint8_t> encoded, std::size_t sampleCount,
                                std::vector<float>& outMonoPcm) = 0;
};

/// 4:1 IMA-ADPCM voice codec (4-byte predictor header + 4-bit nibbles per sample).
class ImaAdpcmVoiceCodecUVE final : public IVoiceCodecUVE {
public:
    [[nodiscard]] std::uint8_t CodecIdUVE() const noexcept override { return 1U; }
    void EncodeFrameUVE(std::span<const float> monoPcm,
                        std::vector<std::uint8_t>& outEncoded) override;
    void DecodeFrameUVE(std::span<const std::uint8_t> encoded, std::size_t sampleCount,
                        std::vector<float>& outMonoPcm) override;
};

/// Energy-based Voice Activity Detector with holdover smoothing and Push-To-Talk override.
struct VoiceActivityDetectorUVE final {
    float rmsThreshold = 0.015F;
    std::uint32_t holdoverFrames = 8U;
    bool pushToTalkMode = false;
    bool pushToTalkActive = false;
    std::uint32_t remainingHoldover = 0U;

    [[nodiscard]] bool EvaluateFrameUVE(std::span<const float> monoPcm) noexcept;
};

/// Adaptive sequence-ordered jitter buffer smoothing packet reordering and loss.
class VoiceJitterBufferUVE final {
public:
    explicit VoiceJitterBufferUVE(std::size_t targetDepthFrames = 3U,
                                  std::size_t maxDepthFrames = 16U) noexcept
        : m_targetDepth(targetDepthFrames), m_maxDepth(maxDepthFrames) {}

    void PushPacketUVE(VoiceFramePacketUVE packet);
    [[nodiscard]] bool TryPopNextUVE(VoiceFramePacketUVE& outPacket);
    [[nodiscard]] std::size_t BufferedCountUVE() const noexcept { return m_frames.size(); }
    [[nodiscard]] std::uint64_t DroppedPacketsUVE() const noexcept { return m_dropped; }
    void ResetUVE() noexcept;

private:
    std::size_t m_targetDepth = 3U;
    std::size_t m_maxDepth = 16U;
    bool m_primed = false;
    std::uint32_t m_nextSequence = 0U;
    std::uint64_t m_dropped = 0U;
    std::map<std::uint32_t, VoiceFramePacketUVE> m_frames;
};

/// End-to-end peer voice chat pipeline: VAD/PTT + codec + per-peer jitter buffers.
class VoiceChatSessionUVE final {
public:
    explicit VoiceChatSessionUVE(std::uint32_t localPeerId = 1U,
                                 std::unique_ptr<IVoiceCodecUVE> codec = nullptr);

    VoiceActivityDetectorUVE& VadUVE() noexcept { return m_vad; }
    const VoiceActivityDetectorUVE& VadUVE() const noexcept { return m_vad; }

    /// Evaluates VAD/PTT and encodes a captured mono frame into `outPacket` if active.
    [[nodiscard]] bool CaptureAndEncodeUVE(std::span<const float> monoPcm,
                                           std::uint32_t sampleRateHz,
                                           VoiceFramePacketUVE& outPacket);

    /// Inserts a received network voice packet into its peer's jitter buffer.
    void ReceivePacketUVE(VoiceFramePacketUVE packet);

    /// Decodes the next ready audio frame for `peerId` into `outMonoPcm`. Returns false if not primed.
    [[nodiscard]] bool PopPeerPlaybackPcmUVE(std::uint32_t peerId, std::vector<float>& outMonoPcm);

    [[nodiscard]] std::size_t ActivePeerCountUVE() const noexcept { return m_peerBuffers.size(); }

private:
    std::uint32_t m_localPeerId = 1U;
    std::uint32_t m_txSequence = 0U;
    std::unique_ptr<IVoiceCodecUVE> m_codec;
    VoiceActivityDetectorUVE m_vad{};
    std::unordered_map<std::uint32_t, VoiceJitterBufferUVE> m_peerBuffers;
};

} // namespace UVE::Networking
