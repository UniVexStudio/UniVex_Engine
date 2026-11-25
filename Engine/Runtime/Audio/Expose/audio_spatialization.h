// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <cmath>

#include "vector3.h"

namespace UVE::Audio {

/// Speed of sound in air at 20 degrees Celsius, in metres per second. The Doppler model below is the
/// classic moving-observer/moving-source form; nothing here is a perceptual approximation.
inline constexpr float kSpeedOfSoundMetersPerSecond = 343.0F;
/// Clamp for the Doppler factor. Beyond this a sound is no longer recognisable as itself, and a
/// perfect collision between the model and c would otherwise divide by ~zero.
inline constexpr float kMinimumAudioDopplerPitch = 0.5F;
inline constexpr float kMaximumAudioDopplerPitch = 2.0F;
/// Position deltas are turned into velocities with the frame delta. A hitch must not manufacture a
/// 200 m/s source out of one late frame, so the modelled step is capped here.
inline constexpr float kMaximumAudioVelocityStepSeconds = 0.1F;
/// Equal-power centre gain: cos(pi/4), i.e. -3 dB per channel before the normalisation below.
inline constexpr float kAudioEqualPowerCentre = 0.70710678118654752440F;

/// Per-channel gains for one voice, already folded into the caller's own gain.
struct AudioPanGains final {
    float left = 1.0F;
    float right = 1.0F;
    [[nodiscard]] bool operator==(const AudioPanGains&) const noexcept = default;
};

/// Equal-power pan law, normalised so that pan == 0 is EXACTLY unity on both channels - which is what
/// this engine's stereo output did before panning existed, so centred content does not change level.
/// The power sum is therefore constant: hard left is sqrt(2) on the left and 0 on the right.
[[nodiscard]] inline AudioPanGains ComputeAudioPanGains(const float pan) noexcept {
    if (!std::isfinite(pan)) return AudioPanGains{}; // a corrupt pan must not poison the mix
    const float clamped = std::clamp(pan, -1.0F, 1.0F);
    const float angle = (clamped + 1.0F) * (0.78539816339744830962F); // [0, pi/2]
    const float normalisation = 1.0F / kAudioEqualPowerCentre;
    // cos(pi/2) is a hair below zero in float, and a negative gain would invert one channel's phase
    // instead of silencing it, so the ends of the range are pinned at exact zero.
    return AudioPanGains{std::max(0.0F, std::cos(angle) * normalisation),
                            std::max(0.0F, std::sin(angle) * normalisation)};
}

/// The listener's right vector, from the same forward/up pair the listener already carries.
/// Returns +X for the default forward (0,0,-1) and up (0,1,0). A degenerate pair (parallel vectors,
/// which IsAudioListenerOrientationValid already rejects) falls back to +X rather than a NaN.
[[nodiscard]] inline Math::Vector3 ComputeAudioListenerRight(const Math::Vector3& forward,
                                                                    const Math::Vector3& up) noexcept {
    const Math::Vector3 right = Math::Cross(forward, up);
    const float lengthSquared = Math::LengthSquared(right);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12F) return Math::Vector3{1.0F, 0.0F, 0.0F};
    return right * (1.0F / std::sqrt(lengthSquared));
}

/// Stereo position of a source, as the projection of its offset onto the listener's right axis.
/// Azimuth-only by design: a stereo pan carries no front/back information, and pretending otherwise
/// would just attenuate centre sources. Returns 0 for a source exactly at the listener.
[[nodiscard]] inline float ComputeAudioPan(const Math::Vector3& listenerToSource,
                                               const Math::Vector3& listenerRight) noexcept {
    const float lengthSquared = Math::LengthSquared(listenerToSource);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12F) return 0.0F;
    const float distance = std::sqrt(lengthSquared);
    return std::clamp(Math::Dot(listenerToSource, listenerRight) / distance, -1.0F, 1.0F);
}

/// Directional-emitter gain: full volume inside `innerAngleDegrees`, `outerGain` beyond
/// `outerAngleDegrees`, and a straight interpolation in angle between the two (the convention every
/// middleware cone uses). Both angles are full cone angles measured from the emitter's axis, so 360
/// means omnidirectional and is the default.
[[nodiscard]] inline float ComputeAudioConeAttenuation(const float angleDegrees, const float innerAngleDegrees,
                                                           const float outerAngleDegrees,
                                                           const float outerGain) noexcept {
    if (!std::isfinite(angleDegrees)) return 0.0F;
    const float clampedGain = std::clamp(outerGain, 0.0F, 1.0F);
    if (angleDegrees <= innerAngleDegrees) return 1.0F;
    if (angleDegrees >= outerAngleDegrees) return clampedGain;
    const float span = outerAngleDegrees - innerAngleDegrees;
    if (!(span > 0.0F)) return clampedGain;
    const float position = (angleDegrees - innerAngleDegrees) / span;
    return 1.0F + (clampedGain - 1.0F) * position;
}

/// Classic Doppler: positive contributions are motion TOWARD the other party, so a source closing on
/// the listener raises pitch and a receding one lowers it. `dopplerFactor` scales the effect and 0
/// disables it (returns exactly 1.0). The result is clamped to [0.5, 2], which keeps the device-facing
/// pitch finite and inside the mixer's accepted multiplier range.
[[nodiscard]] inline float ComputeAudioDopplerPitch(const Math::Vector3& listenerVelocity,
                                                        const Math::Vector3& sourceVelocity,
                                                        const Math::Vector3& listenerToSource,
                                                        const float dopplerFactor) noexcept {
    if (!std::isfinite(dopplerFactor) || dopplerFactor <= 0.0F) return 1.0F;
    const float lengthSquared = Math::LengthSquared(listenerToSource);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12F) return 1.0F;
    const Math::Vector3 direction = listenerToSource * (1.0F / std::sqrt(lengthSquared));
    const float listenerApproach = Math::Dot(listenerVelocity, direction);
    const float sourceApproach = -Math::Dot(sourceVelocity, direction);
    if (!std::isfinite(listenerApproach) || !std::isfinite(sourceApproach)) return 1.0F;

    const float numerator = kSpeedOfSoundMetersPerSecond + listenerApproach * dopplerFactor;
    const float denominator = kSpeedOfSoundMetersPerSecond - sourceApproach * dopplerFactor;
    if (!(denominator > 1.0F)) return kMaximumAudioDopplerPitch;
    const float ratio = numerator / denominator;
    if (!std::isfinite(ratio)) return 1.0F;
    return std::clamp(ratio, kMinimumAudioDopplerPitch, kMaximumAudioDopplerPitch);
}

/// Angle between an emitter's axis and the direction from that emitter to the listener, in degrees.
/// A zero-length axis or offset reports 0 degrees, which the omnidirectional default treats as full
/// volume - an unset cone must never be the reason a source goes silent.
[[nodiscard]] inline float ComputeAudioConeAngleDegrees(const Math::Vector3& coneDirection,
                                                            const Math::Vector3& sourceToListener) noexcept {
    const float axisLengthSquared = Math::LengthSquared(coneDirection);
    const float offsetLengthSquared = Math::LengthSquared(sourceToListener);
    if (!std::isfinite(axisLengthSquared) || !std::isfinite(offsetLengthSquared) || axisLengthSquared <= 1.0e-12F ||
        offsetLengthSquared <= 1.0e-12F) {
        return 0.0F;
    }
    const float cosine = Math::Dot(coneDirection, sourceToListener) /
                         std::sqrt(axisLengthSquared * offsetLengthSquared);
    return std::acos(std::clamp(cosine, -1.0F, 1.0F)) * 57.29577951308232087680F;
}

} // namespace UVE::Audio
