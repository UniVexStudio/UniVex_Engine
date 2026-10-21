// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_audio_mixer_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "uve/editor/editor_audio_mixer_uve.h"

namespace UVE::Editor {

namespace {

/// The four buses a project registers, and the setting each one's volume lives in. Kept as one
/// table so adding a fifth bus is a single edit rather than four copies of the same string.
struct BusVolumeSettingUVE final {
    std::string_view bus;
    std::string_view settingId;
};

constexpr std::array<BusVolumeSettingUVE, 4> kBusVolumeSettingsUVE{{
    {"Master", "audio.buses.masterVolume"},
    {"Music", "audio.buses.musicVolume"},
    {"SFX", "audio.buses.sfxVolume"},
    {"Voice", "audio.buses.voiceVolume"},
}};

[[nodiscard]] const BusVolumeSettingUVE* FindBusVolumeSettingUVE(const std::string_view busName) noexcept {
    const auto match = std::find_if(kBusVolumeSettingsUVE.cbegin(), kBusVolumeSettingsUVE.cend(),
                                    [busName](const BusVolumeSettingUVE& entry) { return entry.bus == busName; });
    return match == kBusVolumeSettingsUVE.cend() ? nullptr : &*match;
}

} // namespace

std::string_view EditorAudioBusVolumeSettingIdUVE(const std::string_view busName) noexcept {
    const BusVolumeSettingUVE* const entry = FindBusVolumeSettingUVE(busName);
    return entry != nullptr ? entry->settingId : std::string_view{};
}

std::string FormatEditorAudioDecibelsUVE(const float linear) {
    if (!std::isfinite(linear) || linear <= 0.0F) {
        return "-inf dB";
    }
    const float decibels = Audio::LinearAudioLevelToDecibelsUVE(linear);
    if (decibels <= Audio::kAudioMeterFloorDbUVE) {
        return "-inf dB";
    }
    // One decimal is what a mixer shows: two suggests a precision the meter does not have, while
    // zero hides the difference between -0.4 dB and 0 dB. `%+.1f` keeps the sign, and a level at or
    // just above unity prints "+0.0 dB" rather than a bare "0.0 dB" that reads like silence.
    std::array<char, 32> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%+.1f dB", static_cast<double>(decibels));
    return written > 0 ? std::string{buffer.data()} : std::string{"-inf dB"};
}

float ComputeEditorAudioAttenuationExtentUVE(const Scene::AudioSourceComponentUVE& source) noexcept {
    const float maxDistance =
        std::isfinite(source.maxDistance) && source.maxDistance > 0.0F ? source.maxDistance : 1.0F;
    const float extent = maxDistance * (1.0F + kEditorAudioAttenuationTailUVE);
    return std::isfinite(extent) && extent > 0.0F ? extent : 1.0F;
}

std::vector<EditorAudioAttenuationPointUVE> BuildEditorAudioAttenuationCurveUVE(
    const Scene::AudioSourceComponentUVE& source, const std::size_t sampleCount) {
    std::vector<EditorAudioAttenuationPointUVE> points;
    if (sampleCount < 2U) {
        return points;
    }
    const float extent = ComputeEditorAudioAttenuationExtentUVE(source);
    points.reserve(sampleCount);
    for (std::size_t index = 0U; index < sampleCount; ++index) {
        const float fraction = static_cast<float>(index) / static_cast<float>(sampleCount - 1U);
        EditorAudioAttenuationPointUVE point;
        point.distance = extent * fraction;
        point.gain = Scene::ComputeAudioSourceDistanceAttenuationUVE(source, point.distance);
        points.push_back(point);
    }
    return points;
}

std::vector<float> BuildEditorAudioAttenuationDecibelCurveUVE(const Scene::AudioSourceComponentUVE& source,
                                                             const std::size_t sampleCount) {
    const std::vector<EditorAudioAttenuationPointUVE> points =
        BuildEditorAudioAttenuationCurveUVE(source, sampleCount);
    std::vector<float> decibels;
    decibels.reserve(points.size());
    for (const EditorAudioAttenuationPointUVE& point : points) {
        decibels.push_back(Audio::LinearAudioLevelToDecibelsUVE(point.gain));
    }
    return decibels;
}

EditorAudioAttenuationMarkersUVE ComputeEditorAudioAttenuationMarkersUVE(
    const Scene::AudioSourceComponentUVE& source) noexcept {
    const float extent = ComputeEditorAudioAttenuationExtentUVE(source);
    const auto normalize = [extent](const float distance) {
        if (!std::isfinite(distance) || extent <= 0.0F) {
            return 0.0F;
        }
        return std::clamp(distance / extent, 0.0F, 1.0F);
    };
    EditorAudioAttenuationMarkersUVE markers;
    markers.minNormalized = normalize(source.minDistance);
    markers.maxNormalized = std::max(markers.minNormalized, normalize(source.maxDistance));
    return markers;
}

} // namespace UVE::Editor
