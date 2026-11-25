// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "audio_source_component.h"
#include "entity.h"

namespace UVE::Scene {

class IEntityManager;

/// Authoring definition for the AudioSource3D scene object: the component set and defaults a
/// freshly created AudioSource3D entity attaches.
struct AudioSource3DObjectDefinition final {
    static constexpr std::string_view defaultName = "AudioSource3D";

    AudioSourceComponent source{};
};

[[nodiscard]] bool IsAudioSource3DObjectDefinitionValid(const AudioSource3DObjectDefinition& value) noexcept;

void ApplyAudioSource3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                           const AudioSource3DObjectDefinition& value);

/// Node policy for an `AudioSource3D` object.
///
/// Centralises mixer-group fallback, playability predicates, voice-replacement comparison, and
/// spatial distance attenuation curves shared by `AudioSourceSystem` and scene inspection.
class AudioSource3D final {
public:
    AudioSource3D() = delete;

    /// True when `source` is valid and names a non-empty `audioAssetPath`.
    [[nodiscard]] static bool IsPlayable(const AudioSourceComponent& source) noexcept;

    /// True when `source` is valid, playable, and configured to auto-start on first sync.
    [[nodiscard]] static bool ShouldAutoPlayOnAwake(
        const AudioSourceComponent& source) noexcept;

    /// Returns `source.mixerGroup` when non-empty, or `fallbackMasterGroup` otherwise.
    [[nodiscard]] static std::string_view ResolveEffectiveMixerGroup(
        const AudioSourceComponent& source, std::string_view fallbackMasterGroup) noexcept;

    /// Evaluates the spatial distance gain in `[0, 1]` at `listenerDistance` metres according to
    /// `source.spatial`, `source.minDistance`, `source.maxDistance`, and `source.attenuationCurve`.
    /// Non-spatial sources return `1.0F`; invalid sources or non-finite distances return `0.0F`.
    [[nodiscard]] static float EvaluateDistanceGain(
        const AudioSourceComponent& source, float listenerDistance) noexcept;

    /// Computes `source.volume * EvaluateDistanceGain(source, listenerDistance)`.
    [[nodiscard]] static float ResolveEffectiveGain(
        const AudioSourceComponent& source, float listenerDistance) noexcept;

    /// True when changing from `previous` to `current` requires recreating the underlying audio
    /// voice (clip path, looping, spatial flag, distance bounds, or attenuation curve changed).
    [[nodiscard]] static bool RequiresVoiceReplacement(
        const AudioSourceComponent& previous, const AudioSourceComponent& current) noexcept;
};

} // namespace UVE::Scene
