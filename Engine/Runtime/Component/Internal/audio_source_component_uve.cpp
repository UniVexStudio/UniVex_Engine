// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/audio_source_component_uve.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace UVE::Scene {

[[nodiscard]] bool IsAudioAttenuationCurveValidUVE(const AudioAttenuationCurveUVE curve) noexcept {
    return curve == AudioAttenuationCurveUVE::Linear || curve == AudioAttenuationCurveUVE::InverseSquare;
}

[[nodiscard]] bool IsAudioSourceComponentValidUVE(const AudioSourceComponentUVE& source) noexcept {
    const bool spatialDistanceValid =
        !source.spatial || (std::isfinite(source.minDistance) && source.minDistance > 0.0F &&
                            std::isfinite(source.maxDistance) && source.maxDistance > source.minDistance);
    const bool assetPathValid = source.audioAssetPath.size() <= kMaximumAudioAssetPathBytesUVE &&
                                source.audioAssetPath.find('\0') == std::string::npos;
    const bool mixerGroupValid = source.mixerGroup.size() <= kMaximumAudioMixerGroupNameBytesUVE &&
                                 source.mixerGroup.find('\0') == std::string::npos;
    return assetPathValid && mixerGroupValid && std::isfinite(source.volume) && source.volume >= 0.0F &&
           std::isfinite(source.pitch) && source.pitch > 0.0F &&
           IsAudioAttenuationCurveValidUVE(source.attenuationCurve) && spatialDistanceValid;
}

} // namespace UVE::Scene
