// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_audio_mixer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "editor_audio_mixer.h"

namespace UVE::Editor {

namespace {

/// The four buses a project registers, and the setting each one's volume lives in. Kept as one
/// table so adding a fifth bus is a single edit rather than four copies of the same string.
struct BusVolumeSetting final {
    std::string_view bus;
    std::string_view settingId;
};

constexpr std::array<BusVolumeSetting, 4> kBusVolumeSettings{{
    {"Master", "audio.buses.masterVolume"},
    {"Music", "audio.buses.musicVolume"},
    {"SFX", "audio.buses.sfxVolume"},
    {"Voice", "audio.buses.voiceVolume"},
}};

[[nodiscard]] const BusVolumeSetting* FindBusVolumeSetting(const std::string_view busName) noexcept {
    const auto match = std::find_if(kBusVolumeSettings.cbegin(), kBusVolumeSettings.cend(),
                                    [busName](const BusVolumeSetting& entry) { return entry.bus == busName; });
    return match == kBusVolumeSettings.cend() ? nullptr : &*match;
}

} // namespace

std::string_view EditorAudioBusVolumeSettingId(const std::string_view busName) noexcept {
    const BusVolumeSetting* const entry = FindBusVolumeSetting(busName);
    return entry != nullptr ? entry->settingId : std::string_view{};
}

std::string FormatEditorAudioDecibels(const float linear) {
    if (!std::isfinite(linear) || linear <= 0.0F) {
        return "-inf dB";
    }
    const float decibels = Audio::LinearAudioLevelToDecibels(linear);
    if (decibels <= Audio::kAudioMeterFloorDb) {
        return "-inf dB";
    }
    // One decimal is what a mixer shows: two suggests a precision the meter does not have, while
    // zero hides the difference between -0.4 dB and 0 dB. `%+.1f` keeps the sign, and a level at or
    // just above unity prints "+0.0 dB" rather than a bare "0.0 dB" that reads like silence.
    std::array<char, 32> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%+.1f dB", static_cast<double>(decibels));
    return written > 0 ? std::string{buffer.data()} : std::string{"-inf dB"};
}

float ComputeEditorAudioAttenuationExtent(const Scene::AudioSourceComponent& source) noexcept {
    const float maxDistance =
        std::isfinite(source.maxDistance) && source.maxDistance > 0.0F ? source.maxDistance : 1.0F;
    const float extent = maxDistance * (1.0F + kEditorAudioAttenuationTail);
    return std::isfinite(extent) && extent > 0.0F ? extent : 1.0F;
}

std::vector<EditorAudioAttenuationPoint> BuildEditorAudioAttenuationCurve(
    const Scene::AudioSourceComponent& source, const std::size_t sampleCount) {
    std::vector<EditorAudioAttenuationPoint> points;
    if (sampleCount < 2U) {
        return points;
    }
    const float extent = ComputeEditorAudioAttenuationExtent(source);
    points.reserve(sampleCount);
    for (std::size_t index = 0U; index < sampleCount; ++index) {
        const float fraction = static_cast<float>(index) / static_cast<float>(sampleCount - 1U);
        EditorAudioAttenuationPoint point;
        point.distance = extent * fraction;
        point.gain = Scene::ComputeAudioSourceDistanceAttenuation(source, point.distance);
        points.push_back(point);
    }
    return points;
}

std::vector<float> BuildEditorAudioAttenuationDecibelCurve(const Scene::AudioSourceComponent& source,
                                                             const std::size_t sampleCount) {
    const std::vector<EditorAudioAttenuationPoint> points =
        BuildEditorAudioAttenuationCurve(source, sampleCount);
    std::vector<float> decibels;
    decibels.reserve(points.size());
    for (const EditorAudioAttenuationPoint& point : points) {
        decibels.push_back(Audio::LinearAudioLevelToDecibels(point.gain));
    }
    return decibels;
}

EditorAudioAttenuationMarkers ComputeEditorAudioAttenuationMarkers(
    const Scene::AudioSourceComponent& source) noexcept {
    const float extent = ComputeEditorAudioAttenuationExtent(source);
    const auto normalize = [extent](const float distance) {
        if (!std::isfinite(distance) || extent <= 0.0F) {
            return 0.0F;
        }
        return std::clamp(distance / extent, 0.0F, 1.0F);
    };
    EditorAudioAttenuationMarkers markers;
    markers.minNormalized = normalize(source.minDistance);
    markers.maxNormalized = std::max(markers.minNormalized, normalize(source.maxDistance));
    return markers;
}

} // namespace UVE::Editor
