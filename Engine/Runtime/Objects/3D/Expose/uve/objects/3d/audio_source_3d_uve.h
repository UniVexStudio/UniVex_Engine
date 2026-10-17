// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/audio_source_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the AudioSource3D scene object: the component set and defaults a
/// freshly created AudioSource3D entity attaches.
struct AudioSource3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "AudioSource3D";

    AudioSourceComponentUVE source{};
};

[[nodiscard]] bool IsAudioSource3DObjectDefinitionValidUVE(const AudioSource3DObjectDefinitionUVE& value) noexcept;

void ApplyAudioSource3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                           const AudioSource3DObjectDefinitionUVE& value);

/// Node policy for an `AudioSource3D` object.
///
/// Centralises mixer-group fallback, playability predicates, voice-replacement comparison, and
/// spatial distance attenuation curves shared by `AudioSourceSystemUVE` and scene inspection.
class AudioSource3DUVE final {
public:
    AudioSource3DUVE() = delete;

    /// True when `source` is valid and names a non-empty `audioAssetPath`.
    [[nodiscard]] static bool IsPlayableUVE(const AudioSourceComponentUVE& source) noexcept;

    /// True when `source` is valid, playable, and configured to auto-start on first sync.
    [[nodiscard]] static bool ShouldAutoPlayOnAwakeUVE(
        const AudioSourceComponentUVE& source) noexcept;

    /// Returns `source.mixerGroup` when non-empty, or `fallbackMasterGroup` otherwise.
    [[nodiscard]] static std::string_view ResolveEffectiveMixerGroupUVE(
        const AudioSourceComponentUVE& source, std::string_view fallbackMasterGroup) noexcept;

    /// Evaluates the spatial distance gain in `[0, 1]` at `listenerDistance` metres according to
    /// `source.spatial`, `source.minDistance`, `source.maxDistance`, and `source.attenuationCurve`.
    /// Non-spatial sources return `1.0F`; invalid sources or non-finite distances return `0.0F`.
    [[nodiscard]] static float EvaluateDistanceGainUVE(
        const AudioSourceComponentUVE& source, float listenerDistance) noexcept;

    /// Computes `source.volume * EvaluateDistanceGainUVE(source, listenerDistance)`.
    [[nodiscard]] static float ResolveEffectiveGainUVE(
        const AudioSourceComponentUVE& source, float listenerDistance) noexcept;

    /// True when changing from `previous` to `current` requires recreating the underlying audio
    /// voice (clip path, looping, spatial flag, distance bounds, or attenuation curve changed).
    [[nodiscard]] static bool RequiresVoiceReplacementUVE(
        const AudioSourceComponentUVE& previous, const AudioSourceComponentUVE& current) noexcept;
};

} // namespace UVE::Scene
