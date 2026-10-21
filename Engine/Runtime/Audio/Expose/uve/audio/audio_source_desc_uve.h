// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cmath>
#include <cstdint>
#include <string>

#include "uve/audio/audio_attenuation_model_uve.h"
#include "uve/audio/audio_occlusion_uve.h"
#include "uve/audio/audio_mixer_group_uve.h"
#include "uve/audio/audio_spatialization_uve.h"

namespace UVE::Audio {

/// Describes a source to create via IAudioSystemUVE::CreateSourceUVE(). The system-level
/// counterpart to AudioVoiceDescUVE (which only carries what the RHI layer needs) — bundles
/// everything AudioSystemUVE's own attenuation/gain computation needs, matching this codebase's
/// Desc-struct convention (Render::BufferDescUVE, Physics::RaycastQueryUVE).
struct AudioSourceDescUVE {
    std::string audioAssetPath;
    std::string mixerGroup = std::string(kMasterAudioMixerGroupNameUVE);
    bool looping = false;
    float volume = 1.0F;
    float pitch = 1.0F;
    /// true = 3D positional (attenuated by distance from the listener via minDistance/
    /// maxDistance/attenuationModel); false = 2D (played at `volume` directly, no attenuation).
    bool spatial = true;
    float minDistance = 1.0F;
    float maxDistance = 25.0F;
    AudioAttenuationModelUVE attenuationModel = AudioAttenuationModelUVE::Linear;

    /// Directional-emitter cone, in full cone angles measured from `coneDirection`. The defaults
    /// describe an omnidirectional source, so leaving them alone reproduces the old behaviour
    /// exactly.
    float coneInnerAngleDegrees = 360.0F;
    float coneOuterAngleDegrees = 360.0F;
    float coneOuterGain = 1.0F;
    Math::Vector3UVE coneDirection{0.0F, 0.0F, -1.0F};

    /// Doppler strength: 1 is the physically correct shift for a source moving through air, 0
    /// disables it for this source. Bounded to [0, 1] because exaggerating it is a stylistic choice
    /// this engine does not expose yet.
    float dopplerFactor = 1.0F;

    /// Physics-driven occlusion. Only a positional source can be occluded - a 2D source has no
    /// path to block - and with no provider installed every source stays clear, so the defaults
    /// reproduce the previous behaviour exactly.
    bool occlusionEnabled = true;
    /// Gain the source falls to when fully blocked, in [0, 1].
    float occlusionGain = kDefaultAudioOccludedGainUVE;
    /// Low-pass corner the source falls to when fully blocked, in [20, 20000] Hz.
    float occlusionLowPassHz = kDefaultAudioOccludedCutoffHzUVE;
    /// Which collision layers can block this source. Everything by default; mask out decorative or
    /// trigger geometry in the level rather than paying for it here.
    std::uint32_t occlusionLayerMask = 0xFFFFFFFFU;
    /// The entity this source belongs to, when it was created for one. The occlusion provider
    /// excludes it so an emitter's own collider cannot block its own sound.
    Scene::EntityUVE ownerEntity = Scene::kInvalidEntityUVE;
};

[[nodiscard]] inline bool IsAudioSourceDescValidUVE(const AudioSourceDescUVE& source) noexcept {
    const bool spatialDistanceValid =
        !source.spatial || (std::isfinite(source.minDistance) && source.minDistance > 0.0F &&
                            std::isfinite(source.maxDistance) && source.maxDistance > source.minDistance);
    const bool attenuationModelValid = source.attenuationModel == AudioAttenuationModelUVE::Linear ||
                                       source.attenuationModel == AudioAttenuationModelUVE::InverseSquare;
    // A cone only means something for a positional source; a 2D source ignores these fields
    // entirely, so it is not forced to carry a sane direction it will never use.
    const bool coneValid = !source.spatial ||
                           (std::isfinite(source.coneInnerAngleDegrees) && source.coneInnerAngleDegrees >= 0.0F &&
                            source.coneInnerAngleDegrees <= 360.0F && std::isfinite(source.coneOuterAngleDegrees) &&
                            source.coneOuterAngleDegrees >= source.coneInnerAngleDegrees &&
                            source.coneOuterAngleDegrees <= 360.0F && std::isfinite(source.coneOuterGain) &&
                            source.coneOuterGain >= 0.0F && source.coneOuterGain <= 1.0F &&
                            std::isfinite(source.coneDirection.x) && std::isfinite(source.coneDirection.y) &&
                            std::isfinite(source.coneDirection.z) &&
                            Math::LengthSquaredUVE(source.coneDirection) > 1.0e-12F);
    const bool dopplerValid = std::isfinite(source.dopplerFactor) && source.dopplerFactor >= 0.0F &&
                              source.dopplerFactor <= 1.0F;
    // As with the cone: these knobs only exist for a positional, occlusion-enabled source, so a 2D
    // or opted-out source is never rejected for carrying values it will not use.
    const bool occlusionValid =
        !source.spatial || !source.occlusionEnabled ||
        (std::isfinite(source.occlusionGain) && source.occlusionGain >= 0.0F && source.occlusionGain <= 1.0F &&
         std::isfinite(source.occlusionLowPassHz) && source.occlusionLowPassHz >= kMinimumAudioOcclusionCutoffHzUVE &&
         source.occlusionLowPassHz <= kMaximumAudioOcclusionCutoffHzUVE);
    return std::isfinite(source.volume) && source.volume >= 0.0F && std::isfinite(source.pitch) &&
           source.pitch > 0.0F && attenuationModelValid && spatialDistanceValid && coneValid && dopplerValid &&
           occlusionValid;
}

} // namespace UVE::Audio
