// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/audio/audio_stream_uve.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <system_error>
#include <limits>
#include <utility>
#include <vector>

#include "uve/asset/audio_asset_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/utilities/binary_buffer_uve.h"

namespace UVE::Audio {

namespace {

/// Payload header of a `.uvaudio` asset: `uint32` schema version, `uint16` channels, `uint32`
/// sampleRate, `uint64` sampleCount, all little-endian, immediately followed by the samples.
constexpr std::size_t kAudioPayloadHeaderBytesUVE =
    sizeof(std::uint32_t) + sizeof(std::uint16_t) + sizeof(std::uint32_t) + sizeof(std::uint64_t);

/// Little-endian `float` from four raw bytes. Built from bytes rather than memcpy'd so the reader is
/// correct on any host, not only on the little-endian targets the engine ships for today.
[[nodiscard]] float DecodeFloatLeUVE(const std::byte* const bytes) noexcept {
    const auto byte = [bytes](const std::size_t index) {
        return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[index]));
    };
    const std::uint32_t bits = byte(0U) | (byte(1U) << 8U) | (byte(2U) << 16U) | (byte(3U) << 24U);
    return std::bit_cast<float>(bits);
}

} // namespace

void AudioStreamRingBufferUVE::ConfigureUVE(const std::size_t capacityFrames, const std::uint32_t channels) noexcept {
    m_capacity = 0U;
    m_channels = 0U;
    m_read = 0U;
    m_count = 0U;
    if (capacityFrames == 0U || capacityFrames > kMaximumAudioStreamRingFramesUVE || channels == 0U ||
        channels > kMaximumAudioStreamChannelsUVE) {
        m_samples.clear();
        m_samples.shrink_to_fit();
        return;
    }
    m_capacity = capacityFrames;
    m_channels = channels;
    m_samples.assign(capacityFrames * static_cast<std::size_t>(channels), 0.0F);
}

std::size_t AudioStreamRingBufferUVE::WriteUVE(const float* const interleavedFrames,
                                               const std::size_t frameCount) noexcept {
    if (!IsConfiguredUVE() || interleavedFrames == nullptr || frameCount == 0U) {
        return 0U;
    }
    const std::size_t frames = std::min(frameCount, GetFreeFramesUVE());
    if (frames == 0U) {
        return 0U;
    }
    const std::size_t channels = static_cast<std::size_t>(m_channels);
    const std::size_t writeIndex = (m_read + m_count) % m_capacity;
    const std::size_t firstRun = std::min(frames, m_capacity - writeIndex);
    std::memcpy(m_samples.data() + writeIndex * channels, interleavedFrames, firstRun * channels * sizeof(float));
    if (frames > firstRun) {
        std::memcpy(m_samples.data(), interleavedFrames + firstRun * channels,
                    (frames - firstRun) * channels * sizeof(float));
    }
    m_count += frames;
    return frames;
}

std::size_t AudioStreamRingBufferUVE::ReadUVE(float* const outInterleavedFrames, const std::size_t frameCount) noexcept {
    if (!IsConfiguredUVE() || outInterleavedFrames == nullptr || frameCount == 0U) {
        return 0U;
    }
    const std::size_t frames = std::min(frameCount, m_count);
    if (frames == 0U) {
        return 0U;
    }
    const std::size_t channels = static_cast<std::size_t>(m_channels);
    const std::size_t firstRun = std::min(frames, m_capacity - m_read);
    std::memcpy(outInterleavedFrames, m_samples.data() + m_read * channels, firstRun * channels * sizeof(float));
    if (frames > firstRun) {
        std::memcpy(outInterleavedFrames + firstRun * channels, m_samples.data(),
                    (frames - firstRun) * channels * sizeof(float));
    }
    ConsumeUVE(frames);
    return frames;
}

bool AudioStreamRingBufferUVE::PeekFrameUVE(const std::size_t offset, float* const outFrame) const noexcept {
    if (!IsConfiguredUVE() || outFrame == nullptr || offset >= m_count) {
        return false;
    }
    const std::size_t channels = static_cast<std::size_t>(m_channels);
    const std::size_t index = (m_read + offset) % m_capacity;
    std::memcpy(outFrame, m_samples.data() + index * channels, channels * sizeof(float));
    return true;
}

void AudioStreamRingBufferUVE::ConsumeUVE(const std::size_t frameCount) noexcept {
    if (!IsConfiguredUVE() || frameCount == 0U) {
        return;
    }
    const std::size_t frames = std::min(frameCount, m_count);
    m_read = (m_read + frames) % m_capacity;
    m_count -= frames;
}

void AudioStreamRingBufferUVE::ClearUVE() noexcept {
    m_read = 0U;
    m_count = 0U;
}

struct AudioFileStreamSourceUVE::ImplUVE {
    std::filesystem::path path;
    std::ifstream file;
    AudioStreamFormatUVE format{};
    std::size_t frameCount = 0U;
    std::size_t nextFrame = 0U;
    /// First payload byte of the first sample; every read seeks from here, so the file is never
    /// copied into memory.
    std::streamoff dataOffset = 0;
    std::vector<std::byte> scratch;
    bool open = false;
};

AudioFileStreamSourceUVE::AudioFileStreamSourceUVE(std::filesystem::path path)
    : m_impl(std::make_unique<ImplUVE>()) {
    m_impl->path = std::move(path);
}

AudioFileStreamSourceUVE::~AudioFileStreamSourceUVE() = default;

bool AudioFileStreamSourceUVE::OpenUVE() {
    CloseUVE();
    ImplUVE& impl = *m_impl;
    impl.file.open(impl.path, std::ios::binary);
    if (!impl.file.is_open()) {
        UVE_ERROR("AudioFileStreamSourceUVE: failed to open \"{}\" for streaming", impl.path.string());
        return false;
    }

    std::vector<std::byte> header(Asset::kUveFileEnvelopeHeaderBytesUVE + kAudioPayloadHeaderBytesUVE);
    impl.file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (static_cast<std::size_t>(impl.file.gcount()) != header.size()) {
        UVE_ERROR("AudioFileStreamSourceUVE: \"{}\" is too short to hold an audio header", impl.path.string());
        CloseUVE();
        return false;
    }

    // The four magic bytes are checked here rather than by ReadUint32LeFromBufferUVE, which would
    // happily read them as the version field and shift every field after it by four bytes.
    constexpr std::array<char, 4> kMagic{'U', 'V', 'E', '\0'};
    if (std::memcmp(header.data(), kMagic.data(), kMagic.size()) != 0) {
        UVE_ERROR("AudioFileStreamSourceUVE: \"{}\" is not a .uve file", impl.path.string());
        CloseUVE();
        return false;
    }

    std::size_t offset = kMagic.size();
    std::uint32_t envelopeVersion = 0U;
    std::uint32_t assetType = 0U;
    std::uint32_t compressionMethod = 0U;
    std::uint64_t payloadLength = 0U;
    if (!Utilities::ReadUint32LeFromBufferUVE(header, offset, envelopeVersion) ||
        !Utilities::ReadUint32LeFromBufferUVE(header, offset, assetType) ||
        !Utilities::ReadUint32LeFromBufferUVE(header, offset, compressionMethod) ||
        !Utilities::ReadUint64LeFromBufferUVE(header, offset, payloadLength) ||
        envelopeVersion != 1U || static_cast<std::uint32_t>(Asset::AssetKindUVE::Audio) != assetType ||
        compressionMethod != 0U) {
        UVE_ERROR("AudioFileStreamSourceUVE: \"{}\" is not an uncompressed audio asset", impl.path.string());
        CloseUVE();
        return false;
    }
    std::uint32_t version = 0U;
    std::uint16_t channels = 0U;
    std::uint32_t sampleRate = 0U;
    std::uint64_t sampleCount = 0U;
    if (!Utilities::ReadUint32LeFromBufferUVE(header, offset, version) ||
        !Utilities::ReadUint16LeFromBufferUVE(header, offset, channels) ||
        !Utilities::ReadUint32LeFromBufferUVE(header, offset, sampleRate) ||
        !Utilities::ReadUint64LeFromBufferUVE(header, offset, sampleCount) ||
        version != Asset::kAudioAssetPayloadVersionUVE || sampleCount == 0U) {
        UVE_ERROR("AudioFileStreamSourceUVE: \"{}\" has an unsupported or empty audio payload", impl.path.string());
        CloseUVE();
        return false;
    }

    const AudioStreamFormatUVE format{sampleRate, static_cast<std::uint32_t>(channels)};
    const std::uint64_t expectedPayload = static_cast<std::uint64_t>(kAudioPayloadHeaderBytesUVE) +
                                          sampleCount * sizeof(float);
    if (!IsAudioStreamFormatValidUVE(format) || sampleCount % format.channels != 0U ||
        sampleCount > std::numeric_limits<std::size_t>::max() || payloadLength != expectedPayload) {
        UVE_ERROR("AudioFileStreamSourceUVE: \"{}\" declares an unusable format or a truncated body",
                  impl.path.string());
        CloseUVE();
        return false;
    }

    // The declared length has to match the bytes actually on disk, or every later window would be
    // a short read discovered one frame block late - and a half-present file must not look playable.
    std::error_code sizeError;
    const std::uintmax_t fileSize = std::filesystem::file_size(impl.path, sizeError);
    const std::uint64_t declaredSize = static_cast<std::uint64_t>(offset) + sampleCount * sizeof(float);
    if (sizeError || static_cast<std::uint64_t>(fileSize) != declaredSize) {
        UVE_ERROR("AudioFileStreamSourceUVE: \"{}\" declares {} bytes but the file does not hold them",
                  impl.path.string(), declaredSize);
        CloseUVE();
        return false;
    }

    impl.format = format;
    impl.frameCount = static_cast<std::size_t>(sampleCount) / static_cast<std::size_t>(format.channels);
    impl.nextFrame = 0U;
    impl.dataOffset = static_cast<std::streamoff>(offset);
    impl.open = true;
    return true;
}

void AudioFileStreamSourceUVE::CloseUVE() noexcept {
    ImplUVE& impl = *m_impl;
    if (impl.file.is_open()) {
        impl.file.close();
    }
    impl.open = false;
    impl.frameCount = 0U;
    impl.nextFrame = 0U;
    impl.dataOffset = 0;
    impl.format = AudioStreamFormatUVE{};
}

bool AudioFileStreamSourceUVE::IsOpenUVE() const noexcept {
    return m_impl->open;
}

AudioStreamFormatUVE AudioFileStreamSourceUVE::GetFormatUVE() const noexcept {
    return m_impl->format;
}

std::size_t AudioFileStreamSourceUVE::GetFrameCountUVE() const noexcept {
    return m_impl->frameCount;
}

std::size_t AudioFileStreamSourceUVE::ReadFramesUVE(const std::size_t requestedFrames,
                                                     std::vector<float>& outFrames) {
    outFrames.clear();
    ImplUVE& impl = *m_impl;
    if (!impl.open || requestedFrames == 0U || impl.nextFrame >= impl.frameCount) {
        return 0U;
    }

    const std::size_t channels = static_cast<std::size_t>(impl.format.channels);
    const std::size_t frames = std::min(requestedFrames, impl.frameCount - impl.nextFrame);
    const std::size_t samples = frames * channels;
    // One seek per window: the file stays on disk and only this window is ever resident.
    const std::streamoff sampleOffset =
        impl.dataOffset + static_cast<std::streamoff>(impl.nextFrame * channels * sizeof(float));
    impl.scratch.resize(samples * sizeof(float));
    impl.file.clear();
    impl.file.seekg(sampleOffset, std::ios::beg);
    if (!impl.file) {
        UVE_ERROR("AudioFileStreamSourceUVE: \"{}\" could not seek to frame {}", impl.path.string(),
                  impl.nextFrame);
        return 0U;
    }
    impl.file.read(reinterpret_cast<char*>(impl.scratch.data()),
                   static_cast<std::streamsize>(impl.scratch.size()));
    if (static_cast<std::size_t>(impl.file.gcount()) != impl.scratch.size()) {
        UVE_ERROR("AudioFileStreamSourceUVE: \"{}\" ended inside a sample window at frame {}", impl.path.string(),
                  impl.nextFrame);
        return 0U;
    }

    outFrames.resize(samples);
    for (std::size_t index = 0U; index < samples; ++index) {
        const float sample = DecodeFloatLeUVE(impl.scratch.data() + index * sizeof(float));
        if (!std::isfinite(sample) || sample < -1.0F || sample > 1.0F) {
            // A corrupt payload must not reach the mixer; the caller sees end of stream.
            UVE_ERROR("AudioFileStreamSourceUVE: \"{}\" holds a non-finite or out-of-range sample", impl.path.string());
            outFrames.clear();
            return 0U;
        }
        outFrames[index] = sample;
    }
    impl.nextFrame += frames;
    return frames;
}

bool AudioFileStreamSourceUVE::SeekToFrameUVE(const std::size_t frame) {
    if (!m_impl->open || frame > m_impl->frameCount) {
        return false;
    }
    m_impl->nextFrame = frame;
    return true;
}

} // namespace UVE::Audio
