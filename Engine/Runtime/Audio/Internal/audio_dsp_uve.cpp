// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/audio/audio_dsp_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Audio {

namespace {

constexpr float kTwoPiUVE = 6.283185307179586476925287F;
constexpr float kMinimumLinearUVE = 1.0e-12F;
/// Anything below this inside a feedback path is flushed to zero: a decaying reverb tail would
/// otherwise drift into denormals and cost more than the audio it is producing.
constexpr float kDenormalFloorUVE = 1.0e-15F;
/// Unity-gain allpass coefficient used by the Schroeder/Freeverb diffusers.
constexpr float kAllpassFeedbackUVE = 0.5F;
/// Freeverb's comb and allpass lengths in samples, authored at 44.1 kHz and scaled to the device rate.
constexpr std::array<std::size_t, 4> kCombLengthsUVE{1116U, 1188U, 1277U, 1356U};
constexpr std::array<std::size_t, 2> kAllpassLengthsUVE{556U, 441U};
constexpr float kReverbReferenceSampleRateUVE = 44100.0F;
/// Comb feedback range: roomSize 0 is a small room, 1 a large hall. Above 0.98 a comb self-oscillates.
constexpr float kMinimumReverbFeedbackUVE = 0.7F;
constexpr float kMaximumReverbFeedbackUVE = 0.98F;

[[nodiscard]] float DbToLinearUVE(const float decibels) noexcept {
    return std::pow(10.0F, decibels / 20.0F);
}

[[nodiscard]] float LinearToDbUVE(const float linear) noexcept {
    return 20.0F * std::log10(std::max(linear, kMinimumLinearUVE));
}

/// One-pole smoothing coefficient `1 - e^(-1/n)` for a time constant of `timeMs` at `sampleRate`.
[[nodiscard]] float TimeMsToCoefficientUVE(const float timeMs, const float sampleRate) noexcept {
    const float samples = std::max((timeMs * 0.001F) * sampleRate, 1.0F);
    return std::clamp(1.0F - std::exp(-1.0F / samples), 0.0F, 1.0F);
}

[[nodiscard]] bool IsFiniteInRangeUVE(const float value, const float minimum, const float maximum) noexcept {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

/// Delay-line length for one Freeverb tap at the device rate, never zero and never unbounded.
[[nodiscard]] std::size_t ScaleDelayLengthUVE(const std::size_t baseLength, const float sampleRate) noexcept {
    const float scaled = (static_cast<float>(baseLength) / kReverbReferenceSampleRateUVE) * sampleRate;
    const float rounded = std::round(scaled);
    if (!std::isfinite(rounded) || rounded < 1.0F) return 1U;
    // The accepted rate range bounds this to roughly 12k samples; the cast is exact by then.
    return static_cast<std::size_t>(rounded);
}

} // namespace

bool IsAudioInsertDescValidUVE(const AudioInsertDescUVE& desc) noexcept {
    const AudioInsertParamsUVE& params = desc.params;
    switch (desc.kind) {
    case AudioInsertKindUVE::Gain:
        return IsFiniteInRangeUVE(params.gain, kMinimumAudioInsertGainUVE, kMaximumAudioInsertGainUVE);
    case AudioInsertKindUVE::LowPass:
    case AudioInsertKindUVE::HighPass:
        // The upper bound depends on the device rate, so it is enforced when coefficients are built.
        return std::isfinite(params.frequencyHz) && params.frequencyHz >= kMinimumAudioFilterFrequencyHzUVE &&
               IsFiniteInRangeUVE(params.q, kMinimumAudioFilterQUVE, kMaximumAudioFilterQUVE);
    case AudioInsertKindUVE::PeakEq:
        return std::isfinite(params.frequencyHz) && params.frequencyHz >= kMinimumAudioFilterFrequencyHzUVE &&
               IsFiniteInRangeUVE(params.q, kMinimumAudioFilterQUVE, kMaximumAudioFilterQUVE) &&
               IsFiniteInRangeUVE(params.gainDb, -kMaximumAudioEqGainDbUVE, kMaximumAudioEqGainDbUVE);
    case AudioInsertKindUVE::Compressor:
        return IsFiniteInRangeUVE(params.thresholdDb, kMinimumAudioDynamicsThresholdDbUVE,
                                  kMaximumAudioDynamicsThresholdDbUVE) &&
               IsFiniteInRangeUVE(params.ratio, kMinimumAudioCompressorRatioUVE, kMaximumAudioCompressorRatioUVE) &&
               IsFiniteInRangeUVE(params.attackMs, kMinimumAudioDynamicsTimeMsUVE, kMaximumAudioDynamicsTimeMsUVE) &&
               IsFiniteInRangeUVE(params.releaseMs, kMinimumAudioDynamicsTimeMsUVE, kMaximumAudioDynamicsTimeMsUVE) &&
               IsFiniteInRangeUVE(params.gain, kMinimumAudioInsertGainUVE, kMaximumAudioInsertGainUVE);
    case AudioInsertKindUVE::Limiter:
        return IsFiniteInRangeUVE(params.thresholdDb, kMinimumAudioDynamicsThresholdDbUVE,
                                  kMaximumAudioDynamicsThresholdDbUVE) &&
               IsFiniteInRangeUVE(params.releaseMs, kMinimumAudioDynamicsTimeMsUVE, kMaximumAudioDynamicsTimeMsUVE) &&
               IsFiniteInRangeUVE(params.gain, kMinimumAudioInsertGainUVE, kMaximumAudioInsertGainUVE);
    case AudioInsertKindUVE::Reverb:
        return IsFiniteInRangeUVE(params.roomSize, kMinimumAudioReverbRoomSizeUVE, kMaximumAudioReverbRoomSizeUVE) &&
               IsFiniteInRangeUVE(params.damping, kMinimumAudioReverbDampingUVE, kMaximumAudioReverbDampingUVE) &&
               IsFiniteInRangeUVE(params.wet, 0.0F, kMaximumAudioReverbMixUVE) &&
               IsFiniteInRangeUVE(params.dry, 0.0F, kMaximumAudioReverbMixUVE);
    }
    return false;
}

void AudioInsertUVE::ResetStateUVE() noexcept {
    m_biquad = BiquadStateUVE{};
    m_dynamics = DynamicsStateUVE{};
    m_reverb.combIndices = {};
    m_reverb.combDampingMemory = {};
    m_reverb.allpassIndices = {};
    for (std::vector<float>& comb : m_reverb.combBuffers) std::fill(comb.begin(), comb.end(), 0.0F);
    for (std::vector<float>& allpass : m_reverb.allpassBuffers) std::fill(allpass.begin(), allpass.end(), 0.0F);
}

bool AudioInsertUVE::BuildReverbUVE(const float sampleRateHz) {
    ReverbStateUVE rebuilt;
    for (std::size_t index = 0U; index < rebuilt.combBuffers.size(); ++index) {
        rebuilt.combBuffers[index].assign(ScaleDelayLengthUVE(kCombLengthsUVE[index], sampleRateHz), 0.0F);
    }
    for (std::size_t index = 0U; index < rebuilt.allpassBuffers.size(); ++index) {
        rebuilt.allpassBuffers[index].assign(ScaleDelayLengthUVE(kAllpassLengthsUVE[index], sampleRateHz), 0.0F);
    }
    // Swapping only after every allocation succeeded is what makes a failed configure leave the
    // running instance untouched.
    m_reverb = std::move(rebuilt);
    return true;
}

void AudioInsertUVE::ComputeBiquadCoefficientsUVE(const float sampleRateHz) noexcept {
    const float highestCutoff =
        std::max(kMinimumAudioFilterFrequencyHzUVE, sampleRateHz * kMaximumAudioFilterFrequencyRatioUVE);
    const float frequency =
        std::clamp(m_desc.params.frequencyHz, kMinimumAudioFilterFrequencyHzUVE, highestCutoff);
    const float q = std::clamp(m_desc.params.q, kMinimumAudioFilterQUVE, kMaximumAudioFilterQUVE);
    const float w0 = kTwoPiUVE * frequency / sampleRateHz;
    const float cosW0 = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0F * q);

    float b0 = 1.0F;
    float b1 = 0.0F;
    float b2 = 0.0F;
    float a0 = 1.0F;
    float a1 = 0.0F;
    float a2 = 0.0F;
    switch (m_desc.kind) {
    case AudioInsertKindUVE::LowPass:
        b0 = (1.0F - cosW0) * 0.5F;
        b1 = 1.0F - cosW0;
        b2 = b0;
        a0 = 1.0F + alpha;
        a1 = -2.0F * cosW0;
        a2 = 1.0F - alpha;
        break;
    case AudioInsertKindUVE::HighPass:
        b0 = (1.0F + cosW0) * 0.5F;
        b1 = -(1.0F + cosW0);
        b2 = b0;
        a0 = 1.0F + alpha;
        a1 = -2.0F * cosW0;
        a2 = 1.0F - alpha;
        break;
    case AudioInsertKindUVE::PeakEq: {
        const float amplitude = DbToLinearUVE(m_desc.params.gainDb * 0.5F);
        b0 = 1.0F + alpha * amplitude;
        b1 = -2.0F * cosW0;
        b2 = 1.0F - alpha * amplitude;
        a0 = 1.0F + alpha / amplitude;
        a1 = -2.0F * cosW0;
        a2 = 1.0F - alpha / amplitude;
        break;
    }
    default:
        break;
    }

    // A degenerate denominator would produce a filter that grows without bound; fall back to unity
    // rather than letting one bad parameter poison the bus.
    if (!std::isfinite(a0) || std::fabs(a0) < kMinimumLinearUVE) {
        m_b0 = 1.0F;
        m_b1 = 0.0F;
        m_b2 = 0.0F;
        m_a1 = 0.0F;
        m_a2 = 0.0F;
        return;
    }
    m_b0 = b0 / a0;
    m_b1 = b1 / a0;
    m_b2 = b2 / a0;
    m_a1 = a1 / a0;
    m_a2 = a2 / a0;
}

bool AudioInsertUVE::ConfigureUVE(const AudioInsertDescUVE& desc, const float sampleRateHz) {
    if (!IsAudioInsertDescValidUVE(desc)) return false;
    if (!IsFiniteInRangeUVE(sampleRateHz, kMinimumAudioSampleRateUVE, kMaximumAudioSampleRateUVE)) return false;

    // Reverb is the only kind that needs memory, and it is taken here - never on the audio path.
    if (desc.kind == AudioInsertKindUVE::Reverb) {
        if (m_desc.kind != AudioInsertKindUVE::Reverb || sampleRateHz != m_sampleRate) {
            if (!BuildReverbUVE(sampleRateHz)) return false;
        }
    }

    m_desc = desc;
    m_sampleRate = sampleRateHz;
    ResetStateUVE();

    switch (desc.kind) {
    case AudioInsertKindUVE::LowPass:
    case AudioInsertKindUVE::HighPass:
    case AudioInsertKindUVE::PeakEq:
        ComputeBiquadCoefficientsUVE(sampleRateHz);
        break;
    case AudioInsertKindUVE::Compressor:
        m_attackCoefficient = TimeMsToCoefficientUVE(desc.params.attackMs, sampleRateHz);
        m_releaseCoefficient = TimeMsToCoefficientUVE(desc.params.releaseMs, sampleRateHz);
        m_makeupGain = desc.params.gain;
        break;
    case AudioInsertKindUVE::Limiter:
        m_releaseCoefficient = TimeMsToCoefficientUVE(desc.params.releaseMs, sampleRateHz);
        m_makeupGain = desc.params.gain;
        break;
    default:
        break;
    }
    m_active = true;
    return true;
}

void AudioInsertUVE::ClearUVE() noexcept {
    m_active = false;
    m_desc = AudioInsertDescUVE{};
    ResetStateUVE();
}

bool AudioInsertUVE::SetSampleRateUVE(const float sampleRateHz) noexcept {
    if (!IsFiniteInRangeUVE(sampleRateHz, kMinimumAudioSampleRateUVE, kMaximumAudioSampleRateUVE)) return false;
    if (!m_active) {
        m_sampleRate = sampleRateHz;
        return true;
    }
    if (ConfigureUVE(m_desc, sampleRateHz)) return true;
    // Coefficients for the wrong rate are worse than no processing at all.
    ClearUVE();
    return false;
}

void AudioInsertUVE::ProcessUVE(float* samples, const std::size_t frameCount) noexcept {
    if (!m_active || samples == nullptr || frameCount == 0U) return;
    switch (m_desc.kind) {
    case AudioInsertKindUVE::Gain:
        ProcessGainUVE(samples, frameCount);
        break;
    case AudioInsertKindUVE::LowPass:
    case AudioInsertKindUVE::HighPass:
    case AudioInsertKindUVE::PeakEq:
        ProcessBiquadUVE(samples, frameCount);
        break;
    case AudioInsertKindUVE::Compressor:
        ProcessCompressorUVE(samples, frameCount);
        break;
    case AudioInsertKindUVE::Limiter:
        ProcessLimiterUVE(samples, frameCount);
        break;
    case AudioInsertKindUVE::Reverb:
        ProcessReverbUVE(samples, frameCount);
        break;
    }
}

void AudioInsertUVE::ProcessGainUVE(float* samples, const std::size_t frameCount) noexcept {
    const float gain = m_desc.params.gain;
    for (std::size_t index = 0U; index < frameCount; ++index) samples[index] *= gain;
}

void AudioInsertUVE::ProcessBiquadUVE(float* samples, const std::size_t frameCount) noexcept {
    // Transposed direct form II: the numerically well-behaved arrangement for a float biquad.
    float z1 = m_biquad.z1;
    float z2 = m_biquad.z2;
    for (std::size_t index = 0U; index < frameCount; ++index) {
        const float input = samples[index];
        const float output = m_b0 * input + z1;
        z1 = m_b1 * input - m_a1 * output + z2;
        z2 = m_b2 * input - m_a2 * output;
        samples[index] = output;
    }
    m_biquad.z1 = z1;
    m_biquad.z2 = z2;
}

void AudioInsertUVE::ProcessCompressorUVE(float* samples, const std::size_t frameCount) noexcept {
    const float thresholdDb = m_desc.params.thresholdDb;
    // A 1:1 ratio must be bit-transparent, not merely close, so the slope is computed once.
    const float slope = 1.0F - (1.0F / m_desc.params.ratio);
    float envelope = m_dynamics.envelope;
    for (std::size_t index = 0U; index < frameCount; ++index) {
        const float input = samples[index];
        const float magnitude = std::fabs(input);
        const float coefficient = magnitude > envelope ? m_attackCoefficient : m_releaseCoefficient;
        envelope += (magnitude - envelope) * coefficient;
        const float overDb = LinearToDbUVE(envelope) - thresholdDb;
        const float reductionDb = overDb > 0.0F ? overDb * slope : 0.0F;
        samples[index] = input * DbToLinearUVE(-reductionDb) * m_makeupGain;
    }
    m_dynamics.envelope = envelope;
}

void AudioInsertUVE::ProcessLimiterUVE(float* samples, const std::size_t frameCount) noexcept {
    const float ceiling = DbToLinearUVE(m_desc.params.thresholdDb);
    float gain = m_dynamics.gain;
    for (std::size_t index = 0U; index < frameCount; ++index) {
        const float input = samples[index];
        const float magnitude = std::fabs(input);
        // Instant attack: the target is computed from THIS sample, so the same sample already obeys
        // the ceiling. Only the recovery is smoothed, which is what keeps the limiter inaudible.
        const float targetGain = magnitude > ceiling ? ceiling / magnitude : 1.0F;
        gain = targetGain < gain ? targetGain : gain + (targetGain - gain) * m_releaseCoefficient;
        if (gain < kDenormalFloorUVE) gain = 0.0F;
        samples[index] = input * gain * m_makeupGain;
    }
    m_dynamics.gain = gain;
}

void AudioInsertUVE::ProcessReverbUVE(float* samples, const std::size_t frameCount) noexcept {
    const float feedback =
        kMinimumReverbFeedbackUVE +
        (kMaximumReverbFeedbackUVE - kMinimumReverbFeedbackUVE) * m_desc.params.roomSize;
    const float damping = m_desc.params.damping;
    const float wet = m_desc.params.wet;
    const float dry = m_desc.params.dry;
    const float combScale = 1.0F / static_cast<float>(m_reverb.combBuffers.size());

    for (std::size_t frame = 0U; frame < frameCount; ++frame) {
        const float input = samples[frame];
        float combSum = 0.0F;
        for (std::size_t comb = 0U; comb < m_reverb.combBuffers.size(); ++comb) {
            std::vector<float>& buffer = m_reverb.combBuffers[comb];
            const std::size_t index = m_reverb.combIndices[comb];
            const float delayed = buffer[index];
            // Freeverb's damping: a one-pole lowpass inside the feedback path, so each pass through
            // the comb loses a little more high end - the sound of a real room.
            m_reverb.combDampingMemory[comb] =
                delayed * (1.0F - damping) + m_reverb.combDampingMemory[comb] * damping;
            if (std::fabs(m_reverb.combDampingMemory[comb]) < kDenormalFloorUVE) {
                m_reverb.combDampingMemory[comb] = 0.0F;
            }
            buffer[index] = input + m_reverb.combDampingMemory[comb] * feedback;
            m_reverb.combIndices[comb] = index + 1U >= buffer.size() ? 0U : index + 1U;
            combSum += delayed;
        }

        float diffused = combSum * combScale;
        for (std::size_t allpass = 0U; allpass < m_reverb.allpassBuffers.size(); ++allpass) {
            std::vector<float>& buffer = m_reverb.allpassBuffers[allpass];
            const std::size_t index = m_reverb.allpassIndices[allpass];
            const float buffered = buffer[index];
            const float output = buffered - diffused;
            buffer[index] = diffused + output * kAllpassFeedbackUVE;
            m_reverb.allpassIndices[allpass] = index + 1U >= buffer.size() ? 0U : index + 1U;
            diffused = output;
        }
        samples[frame] = input * dry + diffused * wet;
    }
}

bool AudioBusInsertChainUVE::SetInsertUVE(const std::size_t slot, const AudioInsertDescUVE& desc) {
    if (slot >= m_inserts.size()) return false;
    return m_inserts[slot].ConfigureUVE(desc, m_sampleRate);
}

bool AudioBusInsertChainUVE::ClearInsertUVE(const std::size_t slot) noexcept {
    if (slot >= m_inserts.size()) return false;
    m_inserts[slot].ClearUVE();
    return true;
}

bool AudioBusInsertChainUVE::GetInsertUVE(const std::size_t slot, AudioInsertDescUVE& outDesc) const noexcept {
    if (slot >= m_inserts.size() || !m_inserts[slot].IsActiveUVE()) return false;
    outDesc = m_inserts[slot].GetDescriptionUVE();
    return true;
}

bool AudioBusInsertChainUVE::HasActiveInsertsUVE() const noexcept {
    return std::any_of(m_inserts.begin(), m_inserts.end(),
                       [](const AudioInsertUVE& insert) { return insert.IsActiveUVE(); });
}

void AudioBusInsertChainUVE::SetSampleRateUVE(const float sampleRateHz) noexcept {
    m_sampleRate = sampleRateHz;
    for (AudioInsertUVE& insert : m_inserts) static_cast<void>(insert.SetSampleRateUVE(sampleRateHz));
}

void AudioBusInsertChainUVE::ProcessUVE(const float* inputSamples, float* outputSamples,
                                         const std::size_t frameCount) noexcept {
    if (outputSamples == nullptr || frameCount == 0U) return;
    if (inputSamples != outputSamples && inputSamples != nullptr) {
        std::copy_n(inputSamples, frameCount, outputSamples);
    }
    ProcessInPlaceUVE(outputSamples, frameCount);
}

void AudioBusInsertChainUVE::ProcessInPlaceUVE(float* samples, const std::size_t frameCount) noexcept {
    if (samples == nullptr || frameCount == 0U) return;
    // Ascending slot order is the authored order. Inserts never clamp individually; the chain does
    // it once at the end so an intermediate stage cannot add distortion the caller did not ask for.
    for (AudioInsertUVE& insert : m_inserts) insert.ProcessUVE(samples, frameCount);
    for (std::size_t index = 0U; index < frameCount; ++index) {
        const float value = samples[index];
        // A NaN or infinity that reached the bus is scrubbed here rather than being handed to the
        // device: one poisoned sample would otherwise silence the voice for the rest of its life.
        // The check runs before the clamp, because clamping an infinity would hide it as full scale.
        samples[index] = std::isfinite(value) ? std::clamp(value, -1.0F, 1.0F) : 0.0F;
    }
}

} // namespace UVE::Audio
