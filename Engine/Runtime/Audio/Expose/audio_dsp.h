// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace UVE::Audio {

/// Fixed insert-slot count per mixer bus. Slots run in ascending index order, which is the order the
/// caller authored: slot 0 sees the signal first, the last slot sees what the earlier ones produced.
inline constexpr std::size_t kMaximumAudioBusInserts = 4U;

inline constexpr float kDefaultAudioSampleRate = 48000.0F;
inline constexpr float kMinimumAudioSampleRate = 8000.0F;
inline constexpr float kMaximumAudioSampleRate = 384000.0F;

inline constexpr float kMinimumAudioFilterFrequencyHz = 10.0F;
/// Biquads are only well behaved strictly below Nyquist, and every design formula stays accurate at
/// 0.45 of it - so a cutoff is clamped here instead of being allowed to blow the filter up.
inline constexpr float kMaximumAudioFilterFrequencyRatio = 0.45F;
inline constexpr float kMinimumAudioFilterQ = 0.1F;
inline constexpr float kMaximumAudioFilterQ = 10.0F;

/// Amplitude gain ceiling for the Gain insert (+24 dB). Bounded so a typo cannot produce a
/// denormal-driven mush or an audio-path surprise; the bus output is clamped to [-1, 1] anyway.
inline constexpr float kMinimumAudioInsertGain = 0.0F;
inline constexpr float kMaximumAudioInsertGain = 16.0F;
inline constexpr float kMaximumAudioEqGainDb = 24.0F;

inline constexpr float kMinimumAudioDynamicsThresholdDb = -96.0F;
inline constexpr float kMaximumAudioDynamicsThresholdDb = 0.0F;
inline constexpr float kMinimumAudioCompressorRatio = 1.0F;
inline constexpr float kMaximumAudioCompressorRatio = 100.0F;
inline constexpr float kMinimumAudioDynamicsTimeMs = 0.1F;
inline constexpr float kMaximumAudioDynamicsTimeMs = 2000.0F;

inline constexpr float kMinimumAudioReverbRoomSize = 0.0F;
inline constexpr float kMaximumAudioReverbRoomSize = 1.0F;
inline constexpr float kMinimumAudioReverbDamping = 0.0F;
inline constexpr float kMaximumAudioReverbDamping = 1.0F;
inline constexpr float kMaximumAudioReverbMix = 1.0F;

enum class AudioInsertKind : std::uint8_t {
    Gain = 0,
    /// RBJ low-pass; the filter an occluded or muffled source is routed through.
    LowPass,
    /// RBJ high-pass; removes rumble and DC before dynamics.
    HighPass,
    /// RBJ peaking EQ; the tone-shaping band a bus needs.
    PeakEq,
    /// Feed-forward compressor with a hard knee and makeup gain.
    Compressor,
    /// Instant-attack brick wall: gain drops within the same sample, releases smoothly.
    Limiter,
    /// Schroeder/Freeverb-style mono reverb: four damped parallel combs into two allpasses.
    Reverb,
};

/// Every field an insert can read. Only the fields the kind names are validated and used, so a
/// caller can leave the rest at their defaults.
struct AudioInsertParams final {
    /// Gain insert multiplier, and the compressor's output makeup gain.
    float gain = 1.0F;
    /// LowPass/HighPass cutoff, PeakEq centre, in Hz.
    float frequencyHz = 1000.0F;
    /// LowPass/HighPass/PeakEq resonance, in RBJ Q terms.
    float q = 0.70710678F;
    /// PeakEq boost/cut, in dB.
    float gainDb = 0.0F;
    /// Compressor/Limiter threshold, in dBFS.
    float thresholdDb = -18.0F;
    /// Compressor ratio; 1:1 is transparent. The Limiter kind ignores it.
    float ratio = 4.0F;
    float attackMs = 10.0F;
    float releaseMs = 120.0F;
    /// Comb feedback, mapped to [0.7, 0.98]: 0 is a small room, 1 a large hall.
    float roomSize = 0.8F;
    /// High-frequency damping inside the comb feedback path.
    float damping = 0.4F;
    float wet = 0.3F;
    float dry = 1.0F;

    [[nodiscard]] bool operator==(const AudioInsertParams&) const noexcept = default;
};

struct AudioInsertDesc final {
    AudioInsertKind kind = AudioInsertKind::Gain;
    AudioInsertParams params;

    [[nodiscard]] bool operator==(const AudioInsertDesc&) const noexcept = default;
};

/// True when every field this kind reads is finite and inside its documented range. A cleared slot
/// is not a description - use ClearInsert() rather than relying on some "kind == None" value.
[[nodiscard]] bool IsAudioInsertDescValid(const AudioInsertDesc& desc) noexcept;

/// One insert slot's processor. Configured at control rate; Process() runs on the audio path and
/// never allocates, throws, or reads configuration the caller is still writing.
///
/// References, since none of this is invented here:
///  - biquad coefficient set and topology: Robert Bristow-Johnson's "Audio EQ Cookbook" (the
///    standard RBJ low/high/peaking forms, transposed direct form II).
///  - reverb comb/allpass lengths and the damping scheme: Jezar at Dreampoint's Freeverb (public
///    domain), itself Manfred Schroeder's and James Moorer's topology.
///
/// Thread-safety: not thread-safe. Configure on the main/audio thread between blocks, exactly like
/// every other audio-path object in this module.
class AudioInsert final {
public:
    AudioInsert() = default;
    ~AudioInsert() = default;
    AudioInsert(const AudioInsert&) = delete;
    AudioInsert& operator=(const AudioInsert&) = delete;
    AudioInsert(AudioInsert&&) noexcept = default;
    AudioInsert& operator=(AudioInsert&&) noexcept = default;

    /// Validates, then installs `desc`. Reverb delay lines - the only thing here that needs memory -
    /// are allocated HERE so Process() never can. On false nothing changes: the previous
    /// description, the previous coefficients, and the running DSP state all survive.
    [[nodiscard]] bool Configure(const AudioInsertDesc& desc, float sampleRateHz);

    /// Bypasses the slot and resets its history. The description is discarded.
    void Clear() noexcept;

    [[nodiscard]] bool IsActive() const noexcept { return m_active; }
    [[nodiscard]] const AudioInsertDesc& GetDescription() const noexcept { return m_desc; }
    [[nodiscard]] AudioInsertKind GetKind() const noexcept { return m_desc.kind; }

    /// Rebuilds coefficients and delay lines for a new device rate. False when the rebuild could not
    /// happen (only an allocation failure can cause that), in which case the slot is bypassed rather
    /// than left running coefficients for the wrong rate.
    [[nodiscard]] bool SetSampleRate(float sampleRateHz) noexcept;

    /// In-place. A bypassed slot leaves the buffer untouched. No allocation, no throw.
    void Process(float* samples, std::size_t frameCount) noexcept;

private:
    /// Transposed direct form II: two history words per biquad, no per-sample coefficient math.
    struct BiquadState final {
        float z1 = 0.0F;
        float z2 = 0.0F;
    };
    struct DynamicsState final {
        /// Attack/release follower over the rectified signal, in linear amplitude.
        float envelope = 0.0F;
        /// Smoothed gain the limiter applies; dropped instantly, raised on release.
        float gain = 1.0F;
    };
    struct ReverbState final {
        std::array<std::vector<float>, 4> combBuffers;
        std::array<std::size_t, 4> combIndices{};
        /// One sample of damping memory per comb (Freeverb's lowpass inside the feedback path).
        std::array<float, 4> combDampingMemory{};
        std::array<std::vector<float>, 2> allpassBuffers;
        std::array<std::size_t, 2> allpassIndices{};
    };

    [[nodiscard]] bool BuildReverb(float sampleRateHz);
    void ResetState() noexcept;
    void ComputeBiquadCoefficients(float sampleRateHz) noexcept;
    void ProcessBiquad(float* samples, std::size_t frameCount) noexcept;
    void ProcessGain(float* samples, std::size_t frameCount) noexcept;
    void ProcessCompressor(float* samples, std::size_t frameCount) noexcept;
    void ProcessLimiter(float* samples, std::size_t frameCount) noexcept;
    void ProcessReverb(float* samples, std::size_t frameCount) noexcept;

    AudioInsertDesc m_desc;
    bool m_active = false;
    float m_sampleRate = kDefaultAudioSampleRate;

    float m_b0 = 1.0F;
    float m_b1 = 0.0F;
    float m_b2 = 0.0F;
    float m_a1 = 0.0F;
    float m_a2 = 0.0F;
    BiquadState m_biquad;
    DynamicsState m_dynamics;
    /// One-pole coefficients for the envelope follower and the limiter's release.
    float m_attackCoefficient = 1.0F;
    float m_releaseCoefficient = 1.0F;
    /// Compressor makeup gain, precomputed.
    float m_makeupGain = 1.0F;
    ReverbState m_reverb;
};

/// Fixed, ordered insert stack owned by one mixer bus. Owns no bus, device, voice, stream or sample
/// buffer: the caller supplies the samples it processes.
class AudioBusInsertChain final {
public:
    AudioBusInsertChain() = default;
    ~AudioBusInsertChain() = default;
    AudioBusInsertChain(const AudioBusInsertChain&) = delete;
    AudioBusInsertChain& operator=(const AudioBusInsertChain&) = delete;
    AudioBusInsertChain(AudioBusInsertChain&&) noexcept = default;
    AudioBusInsertChain& operator=(AudioBusInsertChain&&) noexcept = default;

    [[nodiscard]] bool SetInsert(std::size_t slot, const AudioInsertDesc& desc);
    /// Clears one slot; false for an out-of-range index.
    [[nodiscard]] bool ClearInsert(std::size_t slot) noexcept;
    /// Reads one slot's description; false when the index is out of range or the slot is bypassed.
    [[nodiscard]] bool GetInsert(std::size_t slot, AudioInsertDesc& outDesc) const noexcept;
    [[nodiscard]] bool HasActiveInserts() const noexcept;

    void SetSampleRate(float sampleRateHz) noexcept;
    [[nodiscard]] float GetSampleRate() const noexcept { return m_sampleRate; }

    /// Copies input to output once, runs every active insert in ascending slot order in place, then
    /// clamps the result into [-1, 1] exactly once - inserts are not limited individually, because a
    /// stage that clips before the next filter is run is a distortion the caller never asked for.
    void Process(const float* inputSamples, float* outputSamples, std::size_t frameCount) noexcept;
    /// Same chain without the copy, for a caller that already owns the buffer in place.
    void ProcessInPlace(float* samples, std::size_t frameCount) noexcept;

private:
    std::array<AudioInsert, kMaximumAudioBusInserts> m_inserts;
    float m_sampleRate = kDefaultAudioSampleRate;
};

} // namespace UVE::Audio
