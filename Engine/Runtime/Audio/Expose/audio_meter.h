// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace UVE::Audio {

/// Metering is split the way the rest of the audio module is: this header owns the arithmetic and
/// nothing else, so the numbers a mixer panel shows can be asserted without a sound card, an audio
/// thread or an editor.
///
/// Three decisions are baked in here rather than left to each drawing site:
///
/// 1. The scale is decibels with a floor, because a linear bar spends nine tenths of its width on
///    the top 20 dB and a quiet-but-audible bed reads as "silent".
/// 2. Peak release is rate-limited in dB per second. A peak that simply followed the block maximum
///    would flicker at frame rate; one that decayed linearly in amplitude would look like it fell
///    off a cliff and then crawl.
/// 3. Combining two readings is a power sum, not a max and not a plain sum. A max hides the second
///    voice, a plain sum overstates two uncorrelated sources by 3 dB each time it is applied.
inline constexpr float kAudioMeterFloorDbUVE = -96.0F;
inline constexpr float kAudioMeterCeilingDbUVE = 0.0F;
/// 20 dB in one second: the reading is off the peak almost immediately but still readable to the eye.
inline constexpr float kAudioMeterPeakReleaseDbPerSecondUVE = 20.0F;
/// How much of the past a reading averages over. Short enough to follow a gunshot, long enough that
/// a single silent block never drops the bar to zero.
inline constexpr float kAudioMeterRmsWindowSecondsUVE = 0.3F;

/// One meter value, as it is displayed: a peak that holds and releases, and a windowed average.
/// Both are linear amplitudes in [0, ...]; a device may exceed 1 when voices sum past full scale,
/// which is exactly the case a mixer is supposed to make visible.
struct AudioMeterReadingUVE final {
    float peak = 0.0F;
    float rms = 0.0F;

    [[nodiscard]] bool operator==(const AudioMeterReadingUVE&) const noexcept = default;
};

inline constexpr AudioMeterReadingUVE kSilentAudioMeterUVE{};

/// Linear amplitude to dBFS, floored at kAudioMeterFloorDbUVE so a silent bus reads "-inf dB" on a
/// panel rather than the -inf the logarithm actually returns. Non-finite input is silence, never a
/// NaN that would poison every comparison downstream.
[[nodiscard]] inline float LinearAudioLevelToDecibelsUVE(const float linear) noexcept {
    if (!std::isfinite(linear) || linear <= 0.0F) return kAudioMeterFloorDbUVE;
    const float decibels = 20.0F * std::log10(linear);
    return std::isfinite(decibels) ? std::max(decibels, kAudioMeterFloorDbUVE) : kAudioMeterFloorDbUVE;
}

/// dBFS back to a linear amplitude. The inverse of the function above over the range a meter can
/// represent, clamped at both ends so a "-inf" from a panel cannot produce an infinity here.
[[nodiscard]] inline float DecibelsToLinearAudioLevelUVE(const float decibels) noexcept {
    if (!std::isfinite(decibels)) return 0.0F;
    const float clamped = std::clamp(decibels, kAudioMeterFloorDbUVE, kAudioMeterCeilingDbUVE);
    return std::pow(10.0F, clamped / 20.0F);
}

/// Where a reading sits in a 0..1 bar, on the decibel scale: 0 at the floor, 1 at 0 dBFS. This is
/// the only place the mapping lives, so every meter in the editor agrees with every other one.
[[nodiscard]] inline float NormalizedAudioMeterLevelUVE(const float linear) noexcept {
    const float decibels = LinearAudioLevelToDecibelsUVE(linear);
    const float span = kAudioMeterCeilingDbUVE - kAudioMeterFloorDbUVE;
    if (!(span > 0.0F)) return 0.0F;
    return std::clamp((decibels - kAudioMeterFloorDbUVE) / span, 0.0F, 1.0F);
}

/// Advances one voice's meter by one mixed block.
///
/// `blockPeak` and `blockMeanSquare` are what the audio thread measured while mixing that block.
/// The peak holds and then releases at kAudioMeterPeakReleaseDbPerSecondUVE; the mean square is a
/// one-pole average whose time constant is kAudioMeterRmsWindowSecondsUVE. A non-positive or
/// non-finite `deltaSeconds` means "no time passed": the block is still accounted for (a caller
/// that renders offline should see its level), but nothing is released or smoothed over time.
[[nodiscard]] inline AudioMeterReadingUVE IntegrateAudioMeterUVE(const AudioMeterReadingUVE previous,
                                                                const float blockPeak,
                                                                const float blockMeanSquare,
                                                                const float deltaSeconds) noexcept {
    const auto sanitize = [](const float value) { return (std::isfinite(value) && value > 0.0F) ? value : 0.0F; };
    const float heldPeak = sanitize(previous.peak);
    const float heldMeanSquare = sanitize(previous.rms * previous.rms);
    const float incomingPeak = sanitize(blockPeak);
    const float incomingMeanSquare = sanitize(blockMeanSquare);

    AudioMeterReadingUVE reading;
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
        reading.peak = std::max(heldPeak, incomingPeak);
        reading.rms = std::sqrt(std::max(heldMeanSquare, incomingMeanSquare));
        return reading;
    }

    // Release in dB, not in amplitude: 20 dB/s is a constant visual slope on a dBFS meter. Reaching
    // the floor means exactly zero rather than the floor's own amplitude, so "silent" is a value a
    // caller can compare against instead of an inaudible epsilon it has to know about.
    const float heldDb = LinearAudioLevelToDecibelsUVE(heldPeak);
    const float releasedDb = std::max(heldDb - kAudioMeterPeakReleaseDbPerSecondUVE * deltaSeconds,
                                      kAudioMeterFloorDbUVE);
    const float releasedPeak =
        releasedDb <= kAudioMeterFloorDbUVE ? 0.0F : DecibelsToLinearAudioLevelUVE(releasedDb);
    reading.peak = std::max(releasedPeak, incomingPeak);

    const float alpha = 1.0F - std::exp(-deltaSeconds / kAudioMeterRmsWindowSecondsUVE);
    const float smoothedMeanSquare = heldMeanSquare + (incomingMeanSquare - heldMeanSquare) * alpha;
    reading.rms = std::sqrt(std::max(smoothedMeanSquare, 0.0F));
    return reading;
}

/// Power-sums two readings. Used when a bus's level is assembled from the voices routed through it:
/// the signals are uncorrelated in practice, so their powers add even though their samples do not.
[[nodiscard]] inline AudioMeterReadingUVE CombineAudioMeterReadingsUVE(const AudioMeterReadingUVE& first,
                                                                      const AudioMeterReadingUVE& second) noexcept {
    const auto power = [](const float value) { return (std::isfinite(value) && value > 0.0F) ? value * value : 0.0F; };
    AudioMeterReadingUVE combined;
    combined.peak = std::sqrt(power(first.peak) + power(second.peak));
    combined.rms = std::sqrt(power(first.rms) + power(second.rms));
    return combined;
}

/// True when a reading is at or past full scale, i.e. what a mixer should flag. Checked on the peak
/// rather than the average: clipping is a property of the loudest sample, not of the block's energy.
[[nodiscard]] inline bool IsAudioMeterClippingUVE(const AudioMeterReadingUVE& reading) noexcept {
    return std::isfinite(reading.peak) && reading.peak > 1.0F;
}

} // namespace UVE::Audio
