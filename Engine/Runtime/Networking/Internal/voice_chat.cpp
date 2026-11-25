// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "voice_chat.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace UVE::Networking {
namespace {

constexpr std::array<int, 16> kImaIndexTable{
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

constexpr std::array<int, 89> kImaStepTable{
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

void AppendU32Le(std::vector<std::uint8_t>& out, const std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 24U) & 0xFFU));
}

[[nodiscard]] std::uint32_t ReadU32Le(const std::span<const std::uint8_t> b, const std::size_t off) noexcept {
    return static_cast<std::uint32_t>(b[off]) | (static_cast<std::uint32_t>(b[off + 1U]) << 8U) |
           (static_cast<std::uint32_t>(b[off + 2U]) << 16U) |
           (static_cast<std::uint32_t>(b[off + 3U]) << 24U);
}

[[nodiscard]] std::int16_t FloatToPcm16(const float s) noexcept {
    const float clamped = std::clamp(s, -1.0F, 1.0F);
    return static_cast<std::int16_t>(std::lrintf(clamped * 32767.0F));
}

[[nodiscard]] float Pcm16ToFloat(const int s) noexcept {
    return static_cast<float>(std::clamp(s, -32768, 32767)) / 32768.0F;
}

} // namespace

std::vector<std::uint8_t> SerializeVoicePacket(const VoiceFramePacket& packet) {
    std::vector<std::uint8_t> out;
    out.reserve(19U + packet.payload.size());
    out.push_back('U');
    out.push_back('V');
    out.push_back('C');
    out.push_back(packet.codecId);
    AppendU32Le(out, packet.peerId);
    AppendU32Le(out, packet.sequence);
    AppendU32Le(out, packet.sampleRateHz);
    out.push_back(static_cast<std::uint8_t>(packet.sampleCount & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((packet.sampleCount >> 8U) & 0xFFU));
    out.insert(out.end(), packet.payload.begin(), packet.payload.end());
    return out;
}

bool DeserializeVoicePacket(const std::span<const std::uint8_t> bytes,
                               VoiceFramePacket& outPacket) {
    if (bytes.size() < 18U || bytes[0] != 'U' || bytes[1] != 'V' || bytes[2] != 'C') {
        return false;
    }
    VoiceFramePacket pkt{};
    pkt.codecId = bytes[3];
    pkt.peerId = ReadU32Le(bytes, 4U);
    pkt.sequence = ReadU32Le(bytes, 8U);
    pkt.sampleRateHz = ReadU32Le(bytes, 12U);
    pkt.sampleCount = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[16]) | (static_cast<std::uint16_t>(bytes[17]) << 8U));
    pkt.payload.assign(bytes.begin() + 18, bytes.end());
    outPacket = std::move(pkt);
    return true;
}

void ImaAdpcmVoiceCodec::EncodeFrame(const std::span<const float> monoPcm,
                                           std::vector<std::uint8_t>& outEncoded) {
    outEncoded.clear();
    if (monoPcm.empty()) {
        return;
    }
    int predictor = static_cast<int>(FloatToPcm16(monoPcm[0]));
    int stepIndex = 0;

    // 4-byte header: int16 predictor, uint8 stepIndex, uint8 reserved
    outEncoded.reserve(4U + (monoPcm.size() + 1U) / 2U);
    outEncoded.push_back(static_cast<std::uint8_t>(predictor & 0xFF));
    outEncoded.push_back(static_cast<std::uint8_t>((predictor >> 8) & 0xFF));
    outEncoded.push_back(static_cast<std::uint8_t>(stepIndex));
    outEncoded.push_back(0U);

    std::uint8_t packed = 0U;
    bool highNibble = false;
    for (std::size_t i = 1U; i < monoPcm.size(); ++i) {
        const int sample = static_cast<int>(FloatToPcm16(monoPcm[i]));
        int diff = sample - predictor;
        std::uint8_t nibble = 0U;
        if (diff < 0) {
            nibble = 8U;
            diff = -diff;
        }
        const int step = kImaStepTable[static_cast<std::size_t>(stepIndex)];
        int delta = step >> 3;
        if (diff >= step) {
            nibble |= 4U;
            diff -= step;
            delta += step;
        }
        if (diff >= (step >> 1)) {
            nibble |= 2U;
            diff -= (step >> 1);
            delta += (step >> 1);
        }
        if (diff >= (step >> 2)) {
            nibble |= 1U;
            delta += (step >> 2);
        }
        predictor = std::clamp((nibble & 8U) ? (predictor - delta) : (predictor + delta), -32768, 32767);
        stepIndex = std::clamp(stepIndex + kImaIndexTable[nibble], 0, 88);

        if (!highNibble) {
            packed = static_cast<std::uint8_t>(nibble & 0x0FU);
            highNibble = true;
        } else {
            packed |= static_cast<std::uint8_t>((nibble & 0x0FU) << 4U);
            outEncoded.push_back(packed);
            packed = 0U;
            highNibble = false;
        }
    }
    if (highNibble) {
        outEncoded.push_back(packed);
    }
}

void ImaAdpcmVoiceCodec::DecodeFrame(const std::span<const std::uint8_t> encoded,
                                           const std::size_t sampleCount,
                                           std::vector<float>& outMonoPcm) {
    outMonoPcm.clear();
    if (encoded.size() < 4U || sampleCount == 0U) {
        return;
    }
    outMonoPcm.reserve(sampleCount);
    int predictor = static_cast<int>(static_cast<std::int16_t>(
        static_cast<std::uint16_t>(encoded[0]) | (static_cast<std::uint16_t>(encoded[1]) << 8U)));
    int stepIndex = std::clamp(static_cast<int>(encoded[2]), 0, 88);
    outMonoPcm.push_back(Pcm16ToFloat(predictor));

    std::size_t bytePos = 4U;
    bool highNibble = false;
    while (outMonoPcm.size() < sampleCount && bytePos < encoded.size()) {
        const std::uint8_t byte = encoded[bytePos];
        const std::uint8_t nibble = highNibble ? static_cast<std::uint8_t>((byte >> 4U) & 0x0FU)
                                               : static_cast<std::uint8_t>(byte & 0x0FU);
        if (highNibble) {
            ++bytePos;
        }
        highNibble = !highNibble;

        const int step = kImaStepTable[static_cast<std::size_t>(stepIndex)];
        int delta = step >> 3;
        if (nibble & 4U) delta += step;
        if (nibble & 2U) delta += (step >> 1);
        if (nibble & 1U) delta += (step >> 2);
        predictor = std::clamp((nibble & 8U) ? (predictor - delta) : (predictor + delta), -32768, 32767);
        stepIndex = std::clamp(stepIndex + kImaIndexTable[nibble], 0, 88);
        outMonoPcm.push_back(Pcm16ToFloat(predictor));
    }
    while (outMonoPcm.size() < sampleCount) {
        outMonoPcm.push_back(0.0F);
    }
}

bool VoiceActivityDetector::EvaluateFrame(const std::span<const float> monoPcm) noexcept {
    if (pushToTalkMode) {
        return pushToTalkActive;
    }
    if (monoPcm.empty()) {
        return false;
    }
    double sumSq = 0.0;
    for (const float s : monoPcm) {
        sumSq += static_cast<double>(s) * static_cast<double>(s);
    }
    const float rms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(monoPcm.size())));
    if (rms >= rmsThreshold) {
        remainingHoldover = holdoverFrames;
        return true;
    }
    if (remainingHoldover > 0U) {
        --remainingHoldover;
        return true;
    }
    return false;
}

void VoiceJitterBuffer::PushPacket(VoiceFramePacket packet) {
    if (m_primed && static_cast<std::int32_t>(packet.sequence - m_nextSequence) < 0) {
        ++m_dropped;
        return;
    }
    m_frames[packet.sequence] = std::move(packet);
    while (m_frames.size() > m_maxDepth) {
        m_frames.erase(m_frames.begin());
        ++m_dropped;
    }
    if (!m_primed && m_frames.size() >= m_targetDepth) {
        m_primed = true;
        m_nextSequence = m_frames.begin()->first;
    }
}

bool VoiceJitterBuffer::TryPopNext(VoiceFramePacket& outPacket) {
    if (!m_primed || m_frames.empty()) {
        return false;
    }
    const auto it = m_frames.find(m_nextSequence);
    if (it != m_frames.end()) {
        outPacket = std::move(it->second);
        m_frames.erase(it);
        ++m_nextSequence;
        return true;
    }
    // Packet loss: advance sequence and report silence concealment packet.
    if (!m_frames.empty() && static_cast<std::int32_t>(m_frames.begin()->first - m_nextSequence) > 0) {
        outPacket = VoiceFramePacket{};
        outPacket.sequence = m_nextSequence;
        outPacket.sampleCount = static_cast<std::uint16_t>(kDefaultVoiceFrameSamples);
        ++m_nextSequence;
        ++m_dropped;
        return true;
    }
    return false;
}

void VoiceJitterBuffer::Reset() noexcept {
    m_primed = false;
    m_nextSequence = 0U;
    m_frames.clear();
}

VoiceChatSession::VoiceChatSession(const std::uint32_t localPeerId,
                                         std::unique_ptr<IVoiceCodec> codec)
    : m_localPeerId(localPeerId),
      m_codec(codec ? std::move(codec) : std::make_unique<ImaAdpcmVoiceCodec>()) {}

bool VoiceChatSession::CaptureAndEncode(const std::span<const float> monoPcm,
                                              const std::uint32_t sampleRateHz,
                                              VoiceFramePacket& outPacket) {
    if (monoPcm.empty() || !m_vad.EvaluateFrame(monoPcm)) {
        return false;
    }
    outPacket.peerId = m_localPeerId;
    outPacket.sequence = m_txSequence++;
    outPacket.sampleRateHz = sampleRateHz > 0U ? sampleRateHz : kDefaultVoiceSampleRateHz;
    outPacket.sampleCount = static_cast<std::uint16_t>(std::min<std::size_t>(monoPcm.size(), 65535U));
    outPacket.codecId = m_codec->CodecId();
    m_codec->EncodeFrame(monoPcm, outPacket.payload);
    return true;
}

void VoiceChatSession::ReceivePacket(VoiceFramePacket packet) {
    const std::uint32_t peerId = packet.peerId;
    auto it = m_peerBuffers.find(peerId);
    if (it == m_peerBuffers.end()) {
        it = m_peerBuffers.emplace(peerId, VoiceJitterBuffer{}).first;
    }
    it->second.PushPacket(std::move(packet));
}

bool VoiceChatSession::PopPeerPlaybackPcm(const std::uint32_t peerId,
                                                std::vector<float>& outMonoPcm) {
    const auto it = m_peerBuffers.find(peerId);
    if (it == m_peerBuffers.end()) {
        return false;
    }
    VoiceFramePacket pkt{};
    if (!it->second.TryPopNext(pkt)) {
        return false;
    }
    if (pkt.payload.empty()) {
        outMonoPcm.assign(pkt.sampleCount > 0U ? pkt.sampleCount : kDefaultVoiceFrameSamples, 0.0F);
        return true;
    }
    m_codec->DecodeFrame(pkt.payload, pkt.sampleCount, outMonoPcm);
    return true;
}

} // namespace UVE::Networking
