// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/audio_source_component_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace UVE::Scene {
namespace {

[[nodiscard]] std::string SanitizeBoundedStringUVE(const std::string_view input,
                                                   const std::size_t maxBytes) {
    std::string out;
    out.reserve(std::min(input.size(), maxBytes));
    for (const char ch : input) {
        if (ch == '\0') {
            continue;
        }
        out.push_back(ch);
        if (out.size() >= maxBytes) {
            break;
        }
    }
    return out;
}

} // namespace

[[nodiscard]] bool IsAudioAttenuationCurveValidUVE(const AudioAttenuationCurveUVE curve) noexcept {
    return curve == AudioAttenuationCurveUVE::Linear ||
           curve == AudioAttenuationCurveUVE::InverseSquare;
}

[[nodiscard]] bool IsAudioSourceComponentValidUVE(const AudioSourceComponentUVE& source) noexcept {
    const bool spatialDistanceValid =
        !source.spatial || (std::isfinite(source.minDistance) && source.minDistance > 0.0F &&
                            std::isfinite(source.maxDistance) &&
                            source.maxDistance > source.minDistance);
    const bool assetPathValid = source.audioAssetPath.size() <= kMaximumAudioAssetPathBytesUVE &&
                                source.audioAssetPath.find('\0') == std::string::npos;
    const bool mixerGroupValid = source.mixerGroup.size() <= kMaximumAudioMixerGroupNameBytesUVE &&
                                 source.mixerGroup.find('\0') == std::string::npos;
    return assetPathValid && mixerGroupValid && std::isfinite(source.volume) &&
           source.volume >= 0.0F && std::isfinite(source.pitch) && source.pitch > 0.0F &&
           IsAudioAttenuationCurveValidUVE(source.attenuationCurve) && spatialDistanceValid;
}

[[nodiscard]] bool HasAudioSourceAssetPathUVE(const AudioSourceComponentUVE& source) noexcept {
    return !source.audioAssetPath.empty() &&
           source.audioAssetPath.size() <= kMaximumAudioAssetPathBytesUVE &&
           source.audioAssetPath.find('\0') == std::string::npos;
}

[[nodiscard]] AudioSourceComponentUVE SanitizeAudioSourceComponentUVE(
    const AudioSourceComponentUVE& source) {
    AudioSourceComponentUVE out = source;
    out.audioAssetPath = SanitizeBoundedStringUVE(out.audioAssetPath, kMaximumAudioAssetPathBytesUVE);
    out.mixerGroup = SanitizeBoundedStringUVE(out.mixerGroup, kMaximumAudioMixerGroupNameBytesUVE);
    out.volume = (std::isfinite(out.volume) && out.volume >= 0.0F) ? out.volume : 1.0F;
    out.pitch = (std::isfinite(out.pitch) && out.pitch > 0.0F) ? out.pitch : 1.0F;
    if (!IsAudioAttenuationCurveValidUVE(out.attenuationCurve)) {
        out.attenuationCurve = AudioAttenuationCurveUVE::Linear;
    }
    if (!std::isfinite(out.minDistance) || out.minDistance <= 0.0F) {
        out.minDistance = 1.0F;
    }
    if (!std::isfinite(out.maxDistance) || out.maxDistance <= out.minDistance) {
        out.maxDistance = std::max(out.minDistance + 1.0F, 25.0F);
    }
    return out;
}

[[nodiscard]] float ComputeAudioSourceDistanceAttenuationUVE(const AudioSourceComponentUVE& source,
                                                             const float distance) noexcept {
    if (!source.spatial) {
        return 1.0F;
    }
    if (!std::isfinite(distance) || distance <= source.minDistance) {
        return 1.0F;
    }
    if (distance >= source.maxDistance || source.maxDistance <= source.minDistance) {
        return 0.0F;
    }
    const float span = source.maxDistance - source.minDistance;
    const float t = std::clamp((distance - source.minDistance) / span, 0.0F, 1.0F);
    if (source.attenuationCurve == AudioAttenuationCurveUVE::InverseSquare) {
        const float oneMinusT = 1.0F - t;
        return oneMinusT * oneMinusT;
    }
    return 1.0F - t;
}

[[nodiscard]] float ComputeEffectiveAudioSourceGainUVE(const AudioSourceComponentUVE& source,
                                                       const float distance,
                                                       const float busVolume) noexcept {
    const float safeVolume =
        (std::isfinite(source.volume) && source.volume >= 0.0F) ? source.volume : 0.0F;
    const float safeBus = (std::isfinite(busVolume) && busVolume >= 0.0F) ? busVolume : 0.0F;
    return safeVolume * ComputeAudioSourceDistanceAttenuationUVE(source, distance) * safeBus;
}

} // namespace UVE::Scene
