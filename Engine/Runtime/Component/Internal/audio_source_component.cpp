// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "audio_source_component.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace UVE::Scene {
namespace {

[[nodiscard]] std::string SanitizeBoundedString(const std::string_view input,
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

[[nodiscard]] bool IsAudioAttenuationCurveValid(const AudioAttenuationCurve curve) noexcept {
    return curve == AudioAttenuationCurve::Linear ||
           curve == AudioAttenuationCurve::InverseSquare;
}

[[nodiscard]] bool IsAudioSourceComponentValid(const AudioSourceComponent& source) noexcept {
    const bool spatialDistanceValid =
        !source.spatial || (std::isfinite(source.minDistance) && source.minDistance > 0.0F &&
                            std::isfinite(source.maxDistance) &&
                            source.maxDistance > source.minDistance);
    const bool assetPathValid = source.audioAssetPath.size() <= kMaximumAudioAssetPathBytes &&
                                source.audioAssetPath.find('\0') == std::string::npos;
    const bool mixerGroupValid = source.mixerGroup.size() <= kMaximumAudioMixerGroupNameBytes &&
                                 source.mixerGroup.find('\0') == std::string::npos;
    const bool occlusionValid =
        !source.spatial || !source.occlusionEnabled ||
        (std::isfinite(source.occlusionGain) && source.occlusionGain >= 0.0F && source.occlusionGain <= 1.0F &&
         std::isfinite(source.occlusionLowPassHz) && source.occlusionLowPassHz >= 20.0F &&
         source.occlusionLowPassHz <= 20000.0F);
    return assetPathValid && mixerGroupValid && std::isfinite(source.volume) &&
           source.volume >= 0.0F && std::isfinite(source.pitch) && source.pitch > 0.0F &&
           IsAudioAttenuationCurveValid(source.attenuationCurve) && spatialDistanceValid && occlusionValid;
}

[[nodiscard]] bool HasAudioSourceAssetPath(const AudioSourceComponent& source) noexcept {
    return !source.audioAssetPath.empty() &&
           source.audioAssetPath.size() <= kMaximumAudioAssetPathBytes &&
           source.audioAssetPath.find('\0') == std::string::npos;
}

[[nodiscard]] AudioSourceComponent SanitizeAudioSourceComponent(
    const AudioSourceComponent& source) {
    AudioSourceComponent out = source;
    out.audioAssetPath = SanitizeBoundedString(out.audioAssetPath, kMaximumAudioAssetPathBytes);
    out.mixerGroup = SanitizeBoundedString(out.mixerGroup, kMaximumAudioMixerGroupNameBytes);
    out.volume = (std::isfinite(out.volume) && out.volume >= 0.0F) ? out.volume : 1.0F;
    out.pitch = (std::isfinite(out.pitch) && out.pitch > 0.0F) ? out.pitch : 1.0F;
    if (!IsAudioAttenuationCurveValid(out.attenuationCurve)) {
        out.attenuationCurve = AudioAttenuationCurve::Linear;
    }
    if (!std::isfinite(out.minDistance) || out.minDistance <= 0.0F) {
        out.minDistance = 1.0F;
    }
    if (!std::isfinite(out.maxDistance) || out.maxDistance <= out.minDistance) {
        out.maxDistance = std::max(out.minDistance + 1.0F, 25.0F);
    }
    if (!std::isfinite(out.occlusionGain) || out.occlusionGain < 0.0F || out.occlusionGain > 1.0F) {
        out.occlusionGain = 0.35F;
    }
    if (!std::isfinite(out.occlusionLowPassHz) || out.occlusionLowPassHz < 20.0F ||
        out.occlusionLowPassHz > 20000.0F) {
        out.occlusionLowPassHz = 700.0F;
    }
    return out;
}

[[nodiscard]] float ComputeAudioSourceDistanceAttenuation(const AudioSourceComponent& source,
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
    if (source.attenuationCurve == AudioAttenuationCurve::InverseSquare) {
        const float oneMinusT = 1.0F - t;
        return oneMinusT * oneMinusT;
    }
    return 1.0F - t;
}

[[nodiscard]] float ComputeEffectiveAudioSourceGain(const AudioSourceComponent& source,
                                                       const float distance,
                                                       const float busVolume) noexcept {
    const float safeVolume =
        (std::isfinite(source.volume) && source.volume >= 0.0F) ? source.volume : 0.0F;
    const float safeBus = (std::isfinite(busVolume) && busVolume >= 0.0F) ? busVolume : 0.0F;
    return safeVolume * ComputeAudioSourceDistanceAttenuation(source, distance) * safeBus;
}

} // namespace UVE::Scene
