// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace UVE::Scene {

inline constexpr std::size_t kMaximumAudioAssetPathBytesUVE = 1024U;
inline constexpr std::size_t kMaximumAudioMixerGroupNameBytesUVE = 64U;

/// Distance-attenuation curve shape a spatial AudioSourceComponentUVE uses, consumed by
/// Audio::AudioSourceSystemUVE (which converts it to Audio::AudioAttenuationModelUVE) — kept as a
/// Scene-local enum, not the UVE::Audio type itself, so engine/scene never depends on
/// engine/audio. Mirrors Physics::PhysicsMaterialUVE/MaterialOfUVE's precedent
/// (docs/CODING_STANDARDS.md, Physics section, which names engine/audio explicitly as a future
/// user of this exact pattern).
enum class AudioAttenuationCurveUVE : std::uint8_t { Linear, InverseSquare };

/// One of the master spec's named built-in components (Part 7.3), extended in Part 7.6's
/// AudioSystemUVE (Increment 18) exactly as this component's own original doc comment predicted.
/// `audioAssetPath` remains a path-based identity only. The typed `.uvaudio` AudioAssetUVE envelope
/// and bounded caller-owned stream/effect preparation now exist, but this authored component still
/// owns no decoded samples, stream cursor, effect queue, device voice, or backend state. No runtime
/// "isPlaying" field is stored here — AudioSourceSystemUVE tracks the live entity->voice mapping
/// itself, and playback state is queryable via IAudioSystemUVE::GetSourceStateUVE(), matching
/// WorldTransformComponentUVE's own precedent of keeping derived/runtime state out of authored data.
struct AudioSourceComponentUVE final {
    std::string audioAssetPath;
    /// Empty preserves the legacy/default runtime Master route; non-empty names are resolved by AudioSystemUVE.
    std::string mixerGroup;
    float volume = 1.0F;
    bool looping = false;
    float pitch = 1.0F;
    /// true = 3D positional; false = 2D (played at `volume` directly, no attenuation) — a binary
    /// choice this increment, not a continuous 0..1 spatial blend.
    bool spatial = true;
    float minDistance = 1.0F;
    float maxDistance = 25.0F;
    AudioAttenuationCurveUVE attenuationCurve = AudioAttenuationCurveUVE::Linear;
    /// Whether AudioSourceSystemUVE calls IAudioSystemUVE::PlayUVE() the first time it sees this
    /// entity. No scripting/event trigger consumes AudioSourceComponentUVE yet — playOnAwake is
    /// the only way an ECS-authored sound can start without one.
    bool playOnAwake = true;

    /// Physics-driven occlusion. A positional source behind geometry is both quieter and muffled;
    /// these are the values it falls to when fully blocked. Non-spatial sources ignore them, and an
    /// engine with no occlusion provider installed plays every source exactly as before.
    bool occlusionEnabled = true;
    float occlusionGain = 0.35F;
    float occlusionLowPassHz = 700.0F;
    /// Collision layers that can block this source.
    std::uint32_t occlusionLayerMask = 0xFFFFFFFFU;
};

[[nodiscard]] bool IsAudioAttenuationCurveValidUVE(const AudioAttenuationCurveUVE curve) noexcept;

/// Validates source parameters before persistence and audio-source synchronization. An empty path
/// remains valid as the existing default/no-clip source state; an optional runtime clip resolver owns
/// any path/database policy. Distance ordering is required only for spatial sources because 2D sources ignore it.
[[nodiscard]] bool IsAudioSourceComponentValidUVE(const AudioSourceComponentUVE& source) noexcept;

/// True when `source.audioAssetPath` is non-empty and within `kMaximumAudioAssetPathBytesUVE`.
[[nodiscard]] bool HasAudioSourceAssetPathUVE(const AudioSourceComponentUVE& source) noexcept;

/// Sanitizes `source` so volume, pitch, distances, curve, and string fields pass validation.
[[nodiscard]] AudioSourceComponentUVE SanitizeAudioSourceComponentUVE(
    const AudioSourceComponentUVE& source);

/// Computes the distance-attenuation factor in `[0, 1]` for `source` at `distance`.
[[nodiscard]] float ComputeAudioSourceDistanceAttenuationUVE(
    const AudioSourceComponentUVE& source, float distance) noexcept;

/// Computes the effective linear gain (`source.volume * attenuation * busVolume`).
[[nodiscard]] float ComputeEffectiveAudioSourceGainUVE(
    const AudioSourceComponentUVE& source, float distance, float busVolume = 1.0F) noexcept;

} // namespace UVE::Scene
