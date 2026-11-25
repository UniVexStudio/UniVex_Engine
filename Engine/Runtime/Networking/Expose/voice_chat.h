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

inline constexpr std::uint32_t kDefaultVoiceSampleRateHz = 16000U;
inline constexpr std::size_t kDefaultVoiceFrameSamples = 320U; // 20 ms @ 16 kHz

struct VoiceFramePacket final {
    std::uint32_t peerId = 0U;
    std::uint32_t sequence = 0U;
    std::uint32_t sampleRateHz = kDefaultVoiceSampleRateHz;
    std::uint16_t sampleCount = 0U;
    std::uint8_t codecId = 1U; // 1 = IMA-ADPCM
    std::vector<std::uint8_t> payload;
};

[[nodiscard]] std::vector<std::uint8_t> SerializeVoicePacket(const VoiceFramePacket& packet);
[[nodiscard]] bool DeserializeVoicePacket(std::span<const std::uint8_t> bytes,
                                             VoiceFramePacket& outPacket);

/// Pluggable voice frame codec interface (default: IMA-ADPCM 4:1; Opus can drop in cleanly).
class IVoiceCodec {
public:
    virtual ~IVoiceCodec() = default;
    [[nodiscard]] virtual std::uint8_t CodecId() const noexcept = 0;
    virtual void EncodeFrame(std::span<const float> monoPcm,
                                std::vector<std::uint8_t>& outEncoded) = 0;
    virtual void DecodeFrame(std::span<const std::uint8_t> encoded, std::size_t sampleCount,
                                std::vector<float>& outMonoPcm) = 0;
};

/// 4:1 IMA-ADPCM voice codec (4-byte predictor header + 4-bit nibbles per sample).
class ImaAdpcmVoiceCodec final : public IVoiceCodec {
public:
    [[nodiscard]] std::uint8_t CodecId() const noexcept override { return 1U; }
    void EncodeFrame(std::span<const float> monoPcm,
                        std::vector<std::uint8_t>& outEncoded) override;
    void DecodeFrame(std::span<const std::uint8_t> encoded, std::size_t sampleCount,
                        std::vector<float>& outMonoPcm) override;
};

/// Energy-based Voice Activity Detector with holdover smoothing and Push-To-Talk override.
struct VoiceActivityDetector final {
    float rmsThreshold = 0.015F;
    std::uint32_t holdoverFrames = 8U;
    bool pushToTalkMode = false;
    bool pushToTalkActive = false;
    std::uint32_t remainingHoldover = 0U;

    [[nodiscard]] bool EvaluateFrame(std::span<const float> monoPcm) noexcept;
};

/// Adaptive sequence-ordered jitter buffer smoothing packet reordering and loss.
class VoiceJitterBuffer final {
public:
    explicit VoiceJitterBuffer(std::size_t targetDepthFrames = 3U,
                                  std::size_t maxDepthFrames = 16U) noexcept
        : m_targetDepth(targetDepthFrames), m_maxDepth(maxDepthFrames) {}

    void PushPacket(VoiceFramePacket packet);
    [[nodiscard]] bool TryPopNext(VoiceFramePacket& outPacket);
    [[nodiscard]] std::size_t BufferedCount() const noexcept { return m_frames.size(); }
    [[nodiscard]] std::uint64_t DroppedPackets() const noexcept { return m_dropped; }
    void Reset() noexcept;

private:
    std::size_t m_targetDepth = 3U;
    std::size_t m_maxDepth = 16U;
    bool m_primed = false;
    std::uint32_t m_nextSequence = 0U;
    std::uint64_t m_dropped = 0U;
    std::map<std::uint32_t, VoiceFramePacket> m_frames;
};

/// End-to-end peer voice chat pipeline: VAD/PTT + codec + per-peer jitter buffers.
class VoiceChatSession final {
public:
    explicit VoiceChatSession(std::uint32_t localPeerId = 1U,
                                 std::unique_ptr<IVoiceCodec> codec = nullptr);

    VoiceActivityDetector& Vad() noexcept { return m_vad; }
    const VoiceActivityDetector& Vad() const noexcept { return m_vad; }

    /// Evaluates VAD/PTT and encodes a captured mono frame into `outPacket` if active.
    [[nodiscard]] bool CaptureAndEncode(std::span<const float> monoPcm,
                                           std::uint32_t sampleRateHz,
                                           VoiceFramePacket& outPacket);

    /// Inserts a received network voice packet into its peer's jitter buffer.
    void ReceivePacket(VoiceFramePacket packet);

    /// Decodes the next ready audio frame for `peerId` into `outMonoPcm`. Returns false if not primed.
    [[nodiscard]] bool PopPeerPlaybackPcm(std::uint32_t peerId, std::vector<float>& outMonoPcm);

    [[nodiscard]] std::size_t ActivePeerCount() const noexcept { return m_peerBuffers.size(); }

private:
    std::uint32_t m_localPeerId = 1U;
    std::uint32_t m_txSequence = 0U;
    std::unique_ptr<IVoiceCodec> m_codec;
    VoiceActivityDetector m_vad{};
    std::unordered_map<std::uint32_t, VoiceJitterBuffer> m_peerBuffers;
};

} // namespace UVE::Networking
