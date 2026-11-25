// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "entity.h"
#include "vector3.h"

namespace UVE::Audio {

/// Occlusion is a single normalized number: 0 means the path between listener and source is clear,
/// 1 means something is fully in the way. Every other decision below is a mapping of that number,
/// so a physics query, an authored volume trigger or a script can all drive the same model.
inline constexpr float kMinimumAudioOcclusion = 0.0F;
inline constexpr float kMaximumAudioOcclusion = 1.0F;

/// Gain at full occlusion. About -9 dB: an occluded source is still audible, because a wall is a
/// low-pass and a level drop, not a switch - completely silent occluders belong to gameplay logic,
/// not to the mixer.
inline constexpr float kDefaultAudioOccludedGain = 0.35F;
/// Cutoff at full occlusion. Heavy enough to read as "through a wall", high enough that speech is
/// still recognisable - which is what makes an occluded guard a useful stealth cue.
inline constexpr float kDefaultAudioOccludedCutoffHz = 700.0F;
/// Cutoff on a clear path. Every partial occlusion interpolates between this and the occluded
/// cutoff in log-frequency, because pitch is perceived logarithmically; a linear interpolation in
/// hertz would spend most of its range in the top octave and sound like nothing is happening.
inline constexpr float kAudioOcclusionClearCutoffHz = 20000.0F;
inline constexpr float kMinimumAudioOcclusionCutoffHz = 20.0F;
inline constexpr float kMaximumAudioOcclusionCutoffHz = kAudioOcclusionClearCutoffHz;

/// Blocking is meant to be noticed immediately; clearing is deliberately slower, so a source that
/// grazes an edge as it moves does not chatter between two filter settings every frame.
inline constexpr float kAudioOcclusionAttackSeconds = 0.06F;
inline constexpr float kAudioOcclusionReleaseSeconds = 0.25F;

/// How often the physics query is repeated per source. Ten hertz is far below the frame rate (so a
/// scene full of sources never turns into a raycast storm) and far above the smoothing time, so
/// the smoothed value never visibly steps.
inline constexpr float kAudioOcclusionQueryIntervalSeconds = 0.1F;

/// Occlusion below this is treated as "clear" and bypasses the filter entirely, so a source that is
/// merely near a corner keeps its full bandwidth instead of being slightly muffled forever.
inline constexpr float kAudioOcclusionBypassThreshold = 1.0e-3F;

/// Gain multiplier for `occlusion` in [0, 1]: 1 when clear, `occludedGain` when fully blocked.
/// Non-finite input is treated as clear - a broken query must not silence the game.
[[nodiscard]] inline float ComputeAudioOcclusionGain(const float occlusion,
                                                        const float occludedGain) noexcept {
    if (!std::isfinite(occlusion) || !std::isfinite(occludedGain)) return 1.0F;
    const float blocked = std::clamp(occlusion, kMinimumAudioOcclusion, kMaximumAudioOcclusion);
    const float floorGain = std::clamp(occludedGain, 0.0F, 1.0F);
    return 1.0F + (floorGain - 1.0F) * blocked;
}

/// Low-pass cutoff for `occlusion` in [0, 1]. Returns exactly 0 for a clear path, which is the
/// device contract for "no filter" - not a very high cutoff that still colours the top end.
[[nodiscard]] inline float ComputeAudioOcclusionCutoff(const float occlusion,
                                                          const float occludedCutoffHz) noexcept {
    if (!std::isfinite(occlusion) || !std::isfinite(occludedCutoffHz)) return 0.0F;
    const float blocked = std::clamp(occlusion, kMinimumAudioOcclusion, kMaximumAudioOcclusion);
    if (blocked < kAudioOcclusionBypassThreshold) return 0.0F;
    const float occluded = std::clamp(occludedCutoffHz, kMinimumAudioOcclusionCutoffHz,
                                      kMaximumAudioOcclusionCutoffHz);
    // Log-frequency interpolation: occlusion 0.5 on a 700 Hz floor lands near 3.7 kHz, which is
    // roughly what a half-covered source sounds like.
    const float ratio = occluded / kAudioOcclusionClearCutoffHz;
    return kAudioOcclusionClearCutoffHz * std::pow(ratio, blocked);
}

/// One-pole smoothing of the occlusion parameter toward its newly queried target, with the
/// asymmetric constants above. A non-positive `deltaSeconds` snaps to the target: no time has
/// passed, so there is nothing to interpolate over, and a caller that does not track frame time
/// (tests, tooling) gets the exact value it asked for instead of a value that never converges.
[[nodiscard]] inline float SmoothAudioOcclusion(const float current, const float target,
                                                   const float deltaSeconds) noexcept {
    const float clampedTarget = std::isfinite(target)
                                    ? std::clamp(target, kMinimumAudioOcclusion, kMaximumAudioOcclusion)
                                    : 0.0F;
    const float clampedCurrent = std::isfinite(current)
                                     ? std::clamp(current, kMinimumAudioOcclusion, kMaximumAudioOcclusion)
                                     : 0.0F;
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) return clampedTarget;
    // An exponential never actually arrives, and "almost clear" is not the same as clear: the
    // device only bypasses its filter at exactly 0, so without this snap a source that has long
    // since been uncovered would stay slightly muffled forever.
    if (std::abs(clampedTarget - clampedCurrent) < kAudioOcclusionBypassThreshold) return clampedTarget;
    const float timeConstant = clampedTarget > clampedCurrent ? kAudioOcclusionAttackSeconds
                                                              : kAudioOcclusionReleaseSeconds;
    const float rate = 1.0F - std::exp(-deltaSeconds / timeConstant);
    return std::clamp(clampedCurrent + (clampedTarget - clampedCurrent) * rate, kMinimumAudioOcclusion,
                      kMaximumAudioOcclusion);
}

/// One-pole low-pass coefficient for a cutoff in hertz. `alpha == 1` is the exact bypass the
/// devices take when a voice reports cutoff 0, so a clear voice runs the untouched sample path
/// rather than a filter tuned to look transparent.
[[nodiscard]] inline float ComputeAudioLowPassCoefficient(const float cutoffHz,
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
class AudioVoiceLowPass final {
public:
    void Configure(const float cutoffHz, const float sampleRateHz) noexcept {
        m_alpha = ComputeAudioLowPassCoefficient(cutoffHz, sampleRateHz);
        if (m_alpha >= 1.0F) Reset();
    }

    void Reset() noexcept {
        m_left = 0.0F;
        m_right = 0.0F;
    }

    [[nodiscard]] bool IsBypassed() const noexcept { return m_alpha >= 1.0F; }

    /// Filters one stereo frame in place. A bypassed filter touches nothing at all, which is what
    /// keeps an unoccluded voice bit-identical to the pre-occlusion mixer.
    void Process(float& left, float& right) noexcept {
        if (m_alpha >= 1.0F) return;
        m_left += m_alpha * (left - m_left);
        m_right += m_alpha * (right - m_right);
        left = m_left;
        right = m_right;
    }

    [[nodiscard]] float GetCoefficient() const noexcept { return m_alpha; }

private:
    float m_alpha = 1.0F;
    float m_left = 0.0F;
    float m_right = 0.0F;
};

/// What the audio system asks an occlusion provider. Everything the provider needs to trace the
/// path is here; the provider is expected to be read-only and callable from the engine thread.
struct AudioOcclusionQuery final {
    Math::Vector3 listenerPosition{};
    Math::Vector3 sourcePosition{};
    /// The listener's right axis, so a provider can trace to each ear instead of a single centre
    /// line - the difference between a hard oracle and a source that fades as it slides behind a
    /// doorway. +X is the default listener's right.
    Math::Vector3 listenerRight{1.0F, 0.0F, 0.0F};
    /// The emitter's own entity, so a provider can exclude it: a radio with a collider must not be
    /// occluded by its own geometry. Invalid when the source was not created from an entity.
    Scene::Entity sourceEntity{};
    std::uint32_t layerMask = 0xFFFFFFFFU;
};

/// Occlusion source for AudioSystem. The audio module cannot depend on physics (physics has no
/// reason to know about audio, and the dependency would be a cycle at the library level), so the
/// system asks this narrow interface instead; RaycastAudioOcclusion in uve_core is the concrete
/// implementation that traces against IRaycastSystem.
class IAudioOcclusionProvider {
public:
    virtual ~IAudioOcclusionProvider() = default;

    /// Returns blockage in [0, 1], where 0 is a clear path. Implementations must be cheap enough
    /// for a per-source query and must never throw; a provider that cannot answer should return 0
    /// rather than guess, because the caller treats a non-finite answer as clear.
    [[nodiscard]] virtual float QueryOcclusion(const AudioOcclusionQuery& query) const = 0;
};

} // namespace UVE::Audio
