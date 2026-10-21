// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace UVE::Audio {

/// Bounded window of interleaved normalized PCM a streaming voice keeps in memory. 8192 frames is
/// a little under a fifth of a second at 48 kHz - deep enough that a 60 Hz pump never underruns it,
/// small enough that a hundred simultaneous streams still cost a few megabytes instead of a few
/// gigabytes. This is the whole point of streaming: the number below is the ceiling on what one
/// long clip can cost.
inline constexpr std::size_t kAudioStreamRingFramesUVE = 8192U;
/// Hard ceiling a caller may ask for. Beyond this a "ring" is just a decoded clip with extra steps.
inline constexpr std::size_t kMaximumAudioStreamRingFramesUVE = 1U << 20U;
/// Channels a stream may carry. Mono is upmixed by the backend exactly like a mono clip.
inline constexpr std::uint32_t kMaximumAudioStreamChannelsUVE = 2U;
/// Sample rates a stream may declare, matching the DSP chain's accepted device range.
inline constexpr std::uint32_t kMinimumAudioStreamSampleRateUVE = 8000U;
inline constexpr std::uint32_t kMaximumAudioStreamSampleRateUVE = 384000U;

/// What a streaming voice's samples mean: frames per second and interleaved channels per frame.
struct AudioStreamFormatUVE final {
    std::uint32_t sampleRate = 48000U;
    std::uint32_t channels = 2U;
    [[nodiscard]] bool operator==(const AudioStreamFormatUVE&) const noexcept = default;
};

[[nodiscard]] constexpr bool IsAudioStreamFormatValidUVE(const AudioStreamFormatUVE& format) noexcept {
    return format.channels >= 1U && format.channels <= kMaximumAudioStreamChannelsUVE &&
           format.sampleRate >= kMinimumAudioStreamSampleRateUVE &&
           format.sampleRate <= kMaximumAudioStreamSampleRateUVE;
}

/// Fixed-capacity FIFO of interleaved frames: one writer (the main-thread pump) and one reader (a
/// backend's audio callback), never both at once on the same frame. The class itself is not
/// thread-safe; each backend serializes it with the same lock it already takes for its voices, so
/// the ring never needs its own synchronization and the audio callback never allocates.
///
/// Every operation is O(1) and allocation-free after ConfigureUVE(); a full ring accepts a partial
/// write and reports exactly how much it took, because dropping the newest frames is how a stream
/// under producer pressure is supposed to degrade.
class AudioStreamRingBufferUVE final {
public:
    AudioStreamRingBufferUVE() = default;
    AudioStreamRingBufferUVE(std::size_t capacityFrames, std::uint32_t channels) noexcept {
        ConfigureUVE(capacityFrames, channels);
    }

    /// Allocates the storage and empties the ring. A zero capacity or channel count leaves it
    /// unconfigured (every operation then reports nothing), which is the "not a streaming voice"
    /// state rather than a hard error.
    void ConfigureUVE(std::size_t capacityFrames, std::uint32_t channels) noexcept;

    /// Appends up to `frameCount` frames, returning how many were actually written.
    [[nodiscard]] std::size_t WriteUVE(const float* interleavedFrames, std::size_t frameCount) noexcept;

    /// Copies out up to `frameCount` frames, returning how many were actually read.
    [[nodiscard]] std::size_t ReadUVE(float* outInterleavedFrames, std::size_t frameCount) noexcept;

    /// Copies the frame `offset` places ahead of the read cursor into `outFrame` (`channels`
    /// floats) WITHOUT consuming it. This is what the mixer uses to interpolate between two frames
    /// while resampling at a fractional step.
    [[nodiscard]] bool PeekFrameUVE(std::size_t offset, float* outFrame) const noexcept;

    /// Drops `frameCount` frames from the front. Values beyond what is buffered are ignored.
    void ConsumeUVE(std::size_t frameCount) noexcept;

    void ClearUVE() noexcept;

    [[nodiscard]] std::size_t GetBufferedFramesUVE() const noexcept { return m_count; }
    [[nodiscard]] std::size_t GetFreeFramesUVE() const noexcept { return m_capacity - m_count; }
    [[nodiscard]] std::size_t GetCapacityFramesUVE() const noexcept { return m_capacity; }
    [[nodiscard]] std::uint32_t GetChannelsUVE() const noexcept { return m_channels; }
    [[nodiscard]] bool IsConfiguredUVE() const noexcept { return m_capacity != 0U && m_channels != 0U; }

private:
    std::vector<float> m_samples;
    std::size_t m_capacity = 0U;
    std::size_t m_read = 0U;
    std::size_t m_count = 0U;
    std::uint32_t m_channels = 0U;
};

/// A pull-based source of PCM for a streaming voice. The system asks for exactly as many frames as
/// the device can accept, on the main thread, once per frame - so an implementation may read from
/// disk, decode a window, or synthesize, and the audio callback never touches it.
class IAudioStreamSourceUVE {
public:
    virtual ~IAudioStreamSourceUVE() = default;

    /// Never changes for the life of the stream; the voice is created from it.
    [[nodiscard]] virtual AudioStreamFormatUVE GetFormatUVE() const noexcept = 0;

    /// Total frames in the stream, or 0 when the length is unknown (a live feed).
    [[nodiscard]] virtual std::size_t GetFrameCountUVE() const noexcept = 0;

    /// Clears `outFrames` and appends up to `requestedFrames` interleaved frames, returning how many
    /// were appended. Fewer than requested means end of stream; 0 means already at the end.
    [[nodiscard]] virtual std::size_t ReadFramesUVE(std::size_t requestedFrames,
                                                    std::vector<float>& outFrames) = 0;

    /// Restarts at `frame`. Returns false when the source cannot seek (a live feed), which is also
    /// the answer a looping stream is refused with.
    [[nodiscard]] virtual bool SeekToFrameUVE(std::size_t frame) = 0;
};

/// A `.uvaudio` envelope read straight off disk, one bounded window at a time.
///
/// The full clip is never resident: the header is parsed once (magic, asset kind, version, channels,
/// rate, frame count) and each ReadFramesUVE() seeks to the requested frame and reads only that
/// window. Memory therefore stays at one window plus the ring, regardless of clip length - which is
/// what makes a ten-minute music bed affordable.
///
/// Every window is validated before it is published: a short read, a non-finite sample or one
/// outside [-1, 1] fails the read and leaves the caller's vector cleared, so a corrupt file can
/// never push garbage into the mixer.
class AudioFileStreamSourceUVE final : public IAudioStreamSourceUVE {
public:
    explicit AudioFileStreamSourceUVE(std::filesystem::path path);
    ~AudioFileStreamSourceUVE() override;

    AudioFileStreamSourceUVE(const AudioFileStreamSourceUVE&) = delete;
    AudioFileStreamSourceUVE& operator=(const AudioFileStreamSourceUVE&) = delete;

    /// Reads and validates the header and positions at frame 0. Returns false (logging the reason)
    /// for a missing file, a wrong asset kind, an unsupported payload version, or a truncated body.
    [[nodiscard]] bool OpenUVE();
    void CloseUVE() noexcept;
    [[nodiscard]] bool IsOpenUVE() const noexcept;

    [[nodiscard]] AudioStreamFormatUVE GetFormatUVE() const noexcept override;
    [[nodiscard]] std::size_t GetFrameCountUVE() const noexcept override;
    [[nodiscard]] std::size_t ReadFramesUVE(std::size_t requestedFrames,
                                            std::vector<float>& outFrames) override;
    [[nodiscard]] bool SeekToFrameUVE(std::size_t frame) override;

private:
    struct ImplUVE;
    std::unique_ptr<ImplUVE> m_impl;
};

} // namespace UVE::Audio
