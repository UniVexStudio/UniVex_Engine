// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "uve/component/entity_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Audio {

/// Occlusion is a single normalized number: 0 means the path between listener and source is clear,
/// 1 means something is fully in the way. Every other decision below is a mapping of that number,
/// so a physics query, an authored volume trigger or a script can all drive the same model.
inline constexpr float kMinimumAudioOcclusionUVE = 0.0F;
inline constexpr float kMaximumAudioOcclusionUVE = 1.0F;

/// Gain at full occlusion. About -9 dB: an occluded source is still audible, because a wall is a
/// low-pass and a level drop, not a switch - completely silent occluders belong to gameplay logic,
/// not to the mixer.
inline constexpr float kDefaultAudioOccludedGainUVE = 0.35F;
/// Cutoff at full occlusion. Heavy enough to read as "through a wall", high enough that speech is
/// still recognisable - which is what makes an occluded guard a useful stealth cue.
inline constexpr float kDefaultAudioOccludedCutoffHzUVE = 700.0F;
/// Cutoff on a clear path. Every partial occlusion interpolates between this and the occluded
/// cutoff in log-frequency, because pitch is perceived logarithmically; a linear interpolation in
/// hertz would spend most of its range in the top octave and sound like nothing is happening.
inline constexpr float kAudioOcclusionClearCutoffHzUVE = 20000.0F;
inline constexpr float kMinimumAudioOcclusionCutoffHzUVE = 20.0F;
inline constexpr float kMaximumAudioOcclusionCutoffHzUVE = kAudioOcclusionClearCutoffHzUVE;

/// Blocking is meant to be noticed immediately; clearing is deliberately slower, so a source that
/// grazes an edge as it moves does not chatter between two filter settings every frame.
inline constexpr float kAudioOcclusionAttackSecondsUVE = 0.06F;
inline constexpr float kAudioOcclusionReleaseSecondsUVE = 0.25F;

/// How often the physics query is repeated per source. Ten hertz is far below the frame rate (so a
/// scene full of sources never turns into a raycast storm) and far above the smoothing time, so
/// the smoothed value never visibly steps.
inline constexpr float kAudioOcclusionQueryIntervalSecondsUVE = 0.1F;

/// Occlusion below this is treated as "clear" and bypasses the filter entirely, so a source that is
/// merely near a corner keeps its full bandwidth instead of being slightly muffled forever.
inline constexpr float kAudioOcclusionBypassThresholdUVE = 1.0e-3F;

/// Gain multiplier for `occlusion` in [0, 1]: 1 when clear, `occludedGain` when fully blocked.
/// Non-finite input is treated as clear - a broken query must not silence the game.
[[nodiscard]] inline float ComputeAudioOcclusionGainUVE(const float occlusion,
                                                        const float occludedGain) noexcept {
    if (!std::isfinite(occlusion) || !std::isfinite(occludedGain)) return 1.0F;
    const float blocked = std::clamp(occlusion, kMinimumAudioOcclusionUVE, kMaximumAudioOcclusionUVE);
    const float floorGain = std::clamp(occludedGain, 0.0F, 1.0F);
    return 1.0F + (floorGain - 1.0F) * blocked;
}

/// Low-pass cutoff for `occlusion` in [0, 1]. Returns exactly 0 for a clear path, which is the
/// device contract for "no filter" - not a very high cutoff that still colours the top end.
[[nodiscard]] inline float ComputeAudioOcclusionCutoffUVE(const float occlusion,
                                                          const float occludedCutoffHz) noexcept {
    if (!std::isfinite(occlusion) || !std::isfinite(occludedCutoffHz)) return 0.0F;
    const float blocked = std::clamp(occlusion, kMinimumAudioOcclusionUVE, kMaximumAudioOcclusionUVE);
    if (blocked < kAudioOcclusionBypassThresholdUVE) return 0.0F;
    const float occluded = std::clamp(occludedCutoffHz, kMinimumAudioOcclusionCutoffHzUVE,
                                      kMaximumAudioOcclusionCutoffHzUVE);
    // Log-frequency interpolation: occlusion 0.5 on a 700 Hz floor lands near 3.7 kHz, which is
    // roughly what a half-covered source sounds like.
    const float ratio = occluded / kAudioOcclusionClearCutoffHzUVE;
    return kAudioOcclusionClearCutoffHzUVE * std::pow(ratio, blocked);
}

/// One-pole smoothing of the occlusion parameter toward its newly queried target, with the
/// asymmetric constants above. A non-positive `deltaSeconds` snaps to the target: no time has
/// passed, so there is nothing to interpolate over, and a caller that does not track frame time
/// (tests, tooling) gets the exact value it asked for instead of a value that never converges.
[[nodiscard]] inline float SmoothAudioOcclusionUVE(const float current, const float target,
                                                   const float deltaSeconds) noexcept {
    const float clampedTarget = std::isfinite(target)
                                    ? std::clamp(target, kMinimumAudioOcclusionUVE, kMaximumAudioOcclusionUVE)
                                    : 0.0F;
    const float clampedCurrent = std::isfinite(current)
                                     ? std::clamp(current, kMinimumAudioOcclusionUVE, kMaximumAudioOcclusionUVE)
                                     : 0.0F;
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) return clampedTarget;
    // An exponential never actually arrives, and "almost clear" is not the same as clear: the
    // device only bypasses its filter at exactly 0, so without this snap a source that has long
    // since been uncovered would stay slightly muffled forever.
    if (std::abs(clampedTarget - clampedCurrent) < kAudioOcclusionBypassThresholdUVE) return clampedTarget;
    const float timeConstant = clampedTarget > clampedCurrent ? kAudioOcclusionAttackSecondsUVE
                                                              : kAudioOcclusionReleaseSecondsUVE;
    const float rate = 1.0F - std::exp(-deltaSeconds / timeConstant);
    return std::clamp(clampedCurrent + (clampedTarget - clampedCurrent) * rate, kMinimumAudioOcclusionUVE,
                      kMaximumAudioOcclusionUVE);
}

/// One-pole low-pass coefficient for a cutoff in hertz. `alpha == 1` is the exact bypass the
/// devices take when a voice reports cutoff 0, so a clear voice runs the untouched sample path
/// rather than a filter tuned to look transparent.
[[nodiscard]] inline float ComputeAudioLowPassCoefficientUVE(const float cutoffHz,
                                                             const float sampleRateHz) noexcept {
    if (!std::isfinite(cutoffHz) || !std::isfinite(sampleRateHz) || cutoffHz <= 0.0F || sampleRateHz <= 0.0F) {
        return 1.0F;
    }
    const float clamped = std::min(cutoffHz, sampleRateHz * 0.45F);
    const float alpha = 1.0F - std::exp(-6.28318530717958647692F * clamped / sampleRateHz);
    return std::clamp(alpha, 0.0F, 1.0F);
}

/// Per-voice one-pole low-pass the devices run to muffle an occluded source. Two channels of state,
/// because the mixer always writes stereo; the filter runs on the interpolated sample before gain
/// and pan, so its response never depends on how loud the voice happens to be.
///
/// The filter is deliberately minimal: one pole, no resonance, no overshoot. Its job is to take the
/// highs off a blocked path, not to be a synthesis filter - a resonant filter on an occluded voice
/// would ring on transients and call attention to the effect.
class AudioVoiceLowPassUVE final {
public:
    void ConfigureUVE(const float cutoffHz, const float sampleRateHz) noexcept {
        m_alpha = ComputeAudioLowPassCoefficientUVE(cutoffHz, sampleRateHz);
        if (m_alpha >= 1.0F) ResetUVE();
    }

    void ResetUVE() noexcept {
        m_left = 0.0F;
        m_right = 0.0F;
    }

    [[nodiscard]] bool IsBypassedUVE() const noexcept { return m_alpha >= 1.0F; }

    /// Filters one stereo frame in place. A bypassed filter touches nothing at all, which is what
    /// keeps an unoccluded voice bit-identical to the pre-occlusion mixer.
    void ProcessUVE(float& left, float& right) noexcept {
        if (m_alpha >= 1.0F) return;
        m_left += m_alpha * (left - m_left);
        m_right += m_alpha * (right - m_right);
        left = m_left;
        right = m_right;
    }

    [[nodiscard]] float GetCoefficientUVE() const noexcept { return m_alpha; }

private:
    float m_alpha = 1.0F;
    float m_left = 0.0F;
    float m_right = 0.0F;
};

/// What the audio system asks an occlusion provider. Everything the provider needs to trace the
/// path is here; the provider is expected to be read-only and callable from the engine thread.
struct AudioOcclusionQueryUVE final {
    Math::Vector3UVE listenerPosition{};
    Math::Vector3UVE sourcePosition{};
    /// The listener's right axis, so a provider can trace to each ear instead of a single centre
    /// line - the difference between a hard oracle and a source that fades as it slides behind a
    /// doorway. +X is the default listener's right.
    Math::Vector3UVE listenerRight{1.0F, 0.0F, 0.0F};
    /// The emitter's own entity, so a provider can exclude it: a radio with a collider must not be
    /// occluded by its own geometry. Invalid when the source was not created from an entity.
    Scene::EntityUVE sourceEntity{};
    std::uint32_t layerMask = 0xFFFFFFFFU;
};

/// Occlusion source for AudioSystemUVE. The audio module cannot depend on physics (physics has no
/// reason to know about audio, and the dependency would be a cycle at the library level), so the
/// system asks this narrow interface instead; RaycastAudioOcclusionUVE in uve_core is the concrete
/// implementation that traces against IRaycastSystemUVE.
class IAudioOcclusionProviderUVE {
public:
    virtual ~IAudioOcclusionProviderUVE() = default;

    /// Returns blockage in [0, 1], where 0 is a clear path. Implementations must be cheap enough
    /// for a per-source query and must never throw; a provider that cannot answer should return 0
    /// rather than guess, because the caller treats a non-finite answer as clear.
    [[nodiscard]] virtual float QueryOcclusionUVE(const AudioOcclusionQueryUVE& query) const = 0;
};

} // namespace UVE::Audio
