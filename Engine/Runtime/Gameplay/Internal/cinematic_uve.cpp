// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/gameplay/cinematic_uve.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsValidIdUVE(const std::string_view id) noexcept {
    return !id.empty() && id.size() <= kMaximumCinematicEventIdBytesUVE;
}

[[nodiscard]] bool IsValidKeyTimeUVE(const double timeSeconds, const double durationSeconds) noexcept {
    return std::isfinite(timeSeconds) && timeSeconds >= 0.0 && timeSeconds <= durationSeconds;
}

template <typename KeyUVE>
[[nodiscard]] bool IsTimeOrderedUVE(const std::vector<KeyUVE>& keys, const double durationSeconds) noexcept {
    double previous = 0.0;
    bool first = true;
    for (const KeyUVE& key : keys) {
        if (!IsValidKeyTimeUVE(key.timeSeconds, durationSeconds)) {
            return false;
        }
        if (!first && key.timeSeconds < previous) {
            return false;
        }
        previous = key.timeSeconds;
        first = false;
    }
    return true;
}

[[nodiscard]] EntityUVE CameraCutAtUVE(const std::vector<CinematicCameraCutUVE>& cuts,
                                       const double timeSeconds) noexcept {
    EntityUVE camera = kInvalidEntityUVE;
    for (const CinematicCameraCutUVE& cut : cuts) {
        if (cut.timeSeconds > timeSeconds) {
            break;
        }
        camera = cut.camera;
    }
    return camera;
}

template <typename KeyUVE, typename CueUVE, typename ProjectUVE>
void CollectTrackUVE(const std::vector<KeyUVE>& keys, const double fromExclusive,
                     const double toInclusive, std::vector<CueUVE>& out, ProjectUVE project) {
    for (const KeyUVE& key : keys) {
        if (key.timeSeconds > fromExclusive && key.timeSeconds <= toInclusive) {
            out.push_back(project(key));
        }
    }
}

void FirePassedKeysUVE(const CinematicComponentUVE& cinematic, const double fromExclusive,
                       const double toInclusive, CinematicStepResultUVE& result) {
    CollectTrackUVE(cinematic.events, fromExclusive, toInclusive, result.firedEventIds,
                    [](const CinematicEventKeyUVE& key) { return key.eventId; });
    CollectTrackUVE(cinematic.animationKeys, fromExclusive, toInclusive, result.firedAnimationCues,
                    [](const CinematicAnimationKeyUVE& key) {
                        return CinematicAnimationCueUVE{key.target, key.clip};
                    });
    CollectTrackUVE(cinematic.audioKeys, fromExclusive, toInclusive, result.firedAudioCues,
                    [](const CinematicAudioKeyUVE& key) {
                        return CinematicAudioCueUVE{key.audioAssetPath, key.volume, key.spatial,
                                                    key.position, key.minDistance, key.maxDistance};
                    });
}

} // namespace

bool IsCinematicComponentValidUVE(const CinematicComponentUVE& value) noexcept {
    if (!std::isfinite(value.durationSeconds) || value.durationSeconds <= 0.0 ||
        !std::isfinite(value.speed) || !std::isfinite(value.currentTimeSeconds) ||
        value.currentTimeSeconds < 0.0 || value.currentTimeSeconds > value.durationSeconds ||
        value.events.size() > kMaximumCinematicEventsUVE || value.cuts.size() > kMaximumCinematicCutsUVE ||
        value.cameraKeys.size() > kMaximumCinematicCameraKeysUVE ||
        value.animationKeys.size() > kMaximumCinematicAnimationKeysUVE ||
        value.audioKeys.size() > kMaximumCinematicAudioKeysUVE) {
        return false;
    }
    for (const CinematicEventKeyUVE& key : value.events) {
        if (!IsValidIdUVE(key.eventId)) {
            return false;
        }
    }
    for (const CinematicCameraCutUVE& cut : value.cuts) {
        if (cut.camera == kInvalidEntityUVE) {
            return false;
        }
    }
    for (const CinematicCameraKeyUVE& key : value.cameraKeys) {
        if (!std::isfinite(key.position.x) || !std::isfinite(key.position.y) ||
            !std::isfinite(key.position.z) || !std::isfinite(key.rotation.x) ||
            !std::isfinite(key.rotation.y) || !std::isfinite(key.rotation.z) ||
            !std::isfinite(key.rotation.w) ||
            std::abs(Math::LengthSquaredUVE(key.rotation) - 1.0F) > 1e-4F) {
            return false;
        }
    }
    for (const CinematicAnimationKeyUVE& key : value.animationKeys) {
        if (key.target == kInvalidEntityUVE) {
            return false;
        }
    }
    for (const CinematicAudioKeyUVE& key : value.audioKeys) {
        if (key.audioAssetPath.empty() ||
            key.audioAssetPath.size() > kMaximumCinematicAudioPathBytesUVE ||
            !std::isfinite(key.volume) || key.volume < 0.0F ||
            !std::isfinite(key.minDistance) || key.minDistance <= 0.0F ||
            !std::isfinite(key.maxDistance) || key.maxDistance <= key.minDistance ||
            !std::isfinite(key.position.x) || !std::isfinite(key.position.y) ||
            !std::isfinite(key.position.z)) {
            return false;
        }
    }
    return IsTimeOrderedUVE(value.events, value.durationSeconds) &&
           IsTimeOrderedUVE(value.cuts, value.durationSeconds) &&
           IsTimeOrderedUVE(value.cameraKeys, value.durationSeconds) &&
           IsTimeOrderedUVE(value.animationKeys, value.durationSeconds) &&
           IsTimeOrderedUVE(value.audioKeys, value.durationSeconds);
}

bool AddCinematicEventUVE(CinematicComponentUVE& cinematic, const double timeSeconds, std::string eventId) {
    if (!IsValidIdUVE(eventId) || !IsValidKeyTimeUVE(timeSeconds, cinematic.durationSeconds) ||
        cinematic.events.size() >= kMaximumCinematicEventsUVE) {
        return false;
    }
    CinematicEventKeyUVE key;
    key.timeSeconds = timeSeconds;
    key.eventId = std::move(eventId);
    const auto slot =
        std::upper_bound(cinematic.events.begin(), cinematic.events.end(), timeSeconds,
                         [](const double time, const CinematicEventKeyUVE& existing) { return time < existing.timeSeconds; });
    cinematic.events.insert(slot, std::move(key));
    return true;
}

bool AddCinematicCutUVE(CinematicComponentUVE& cinematic, const double timeSeconds, const EntityUVE camera) {
    if (camera == kInvalidEntityUVE || !IsValidKeyTimeUVE(timeSeconds, cinematic.durationSeconds) ||
        cinematic.cuts.size() >= kMaximumCinematicCutsUVE) {
        return false;
    }
    CinematicCameraCutUVE cut;
    cut.timeSeconds = timeSeconds;
    cut.camera = camera;
    const auto slot =
        std::upper_bound(cinematic.cuts.begin(), cinematic.cuts.end(), timeSeconds,
                         [](const double time, const CinematicCameraCutUVE& existing) { return time < existing.timeSeconds; });
    cinematic.cuts.insert(slot, std::move(cut));
    return true;
}

bool RemoveCinematicEventUVE(CinematicComponentUVE& cinematic, const std::size_t index) {
    if (index >= cinematic.events.size()) {
        return false;
    }
    cinematic.events.erase(cinematic.events.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool RemoveCinematicCutUVE(CinematicComponentUVE& cinematic, const std::size_t index) {
    if (index >= cinematic.cuts.size()) {
        return false;
    }
    cinematic.cuts.erase(cinematic.cuts.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool AddCinematicCameraKeyUVE(CinematicComponentUVE& cinematic, const double timeSeconds,
                              const Math::Vector3UVE position, const Math::QuaternionUVE rotation) {
    Math::QuaternionUVE normalized{};
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
        !Math::TryNormalizeUVE(rotation, normalized) ||
        !IsValidKeyTimeUVE(timeSeconds, cinematic.durationSeconds) ||
        cinematic.cameraKeys.size() >= kMaximumCinematicCameraKeysUVE) {
        return false;
    }
    CinematicCameraKeyUVE key;
    key.timeSeconds = timeSeconds;
    key.position = position;
    key.rotation = normalized;
    const auto slot = std::upper_bound(
        cinematic.cameraKeys.begin(), cinematic.cameraKeys.end(), timeSeconds,
        [](const double time, const CinematicCameraKeyUVE& existing) { return time < existing.timeSeconds; });
    cinematic.cameraKeys.insert(slot, std::move(key));
    return true;
}

bool RemoveCinematicCameraKeyUVE(CinematicComponentUVE& cinematic, const std::size_t index) {
    if (index >= cinematic.cameraKeys.size()) {
        return false;
    }
    cinematic.cameraKeys.erase(cinematic.cameraKeys.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool AddCinematicAnimationKeyUVE(CinematicComponentUVE& cinematic, const double timeSeconds,
                                 const EntityUVE target, const Asset::AssetGuidUVE clip) {
    if (target == kInvalidEntityUVE || !IsValidKeyTimeUVE(timeSeconds, cinematic.durationSeconds) ||
        cinematic.animationKeys.size() >= kMaximumCinematicAnimationKeysUVE) {
        return false;
    }
    CinematicAnimationKeyUVE key;
    key.timeSeconds = timeSeconds;
    key.target = target;
    key.clip = clip;
    const auto slot = std::upper_bound(
        cinematic.animationKeys.begin(), cinematic.animationKeys.end(), timeSeconds,
        [](const double time, const CinematicAnimationKeyUVE& existing) {
            return time < existing.timeSeconds;
        });
    cinematic.animationKeys.insert(slot, std::move(key));
    return true;
}

bool RemoveCinematicAnimationKeyUVE(CinematicComponentUVE& cinematic, const std::size_t index) {
    if (index >= cinematic.animationKeys.size()) {
        return false;
    }
    cinematic.animationKeys.erase(cinematic.animationKeys.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool AddCinematicAudioKeyUVE(CinematicComponentUVE& cinematic, const double timeSeconds,
                             std::string audioAssetPath, const float volume) {
    CinematicAudioKeyUVE key;
    key.timeSeconds = timeSeconds;
    key.audioAssetPath = std::move(audioAssetPath);
    key.volume = volume;
    return AddCinematicAudioKeyUVE(cinematic, std::move(key));
}

bool AddCinematicAudioKeyUVE(CinematicComponentUVE& cinematic, CinematicAudioKeyUVE key) {
    if (key.audioAssetPath.empty() || key.audioAssetPath.size() > kMaximumCinematicAudioPathBytesUVE ||
        !std::isfinite(key.volume) || key.volume < 0.0F ||
        !std::isfinite(key.minDistance) || key.minDistance <= 0.0F ||
        !std::isfinite(key.maxDistance) || key.maxDistance <= key.minDistance ||
        !std::isfinite(key.position.x) || !std::isfinite(key.position.y) ||
        !std::isfinite(key.position.z) || !IsValidKeyTimeUVE(key.timeSeconds, cinematic.durationSeconds) ||
        cinematic.audioKeys.size() >= kMaximumCinematicAudioKeysUVE) {
        return false;
    }
    const double timeSeconds = key.timeSeconds;
    const auto slot = std::upper_bound(
        cinematic.audioKeys.begin(), cinematic.audioKeys.end(), timeSeconds,
        [](const double time, const CinematicAudioKeyUVE& existing) { return time < existing.timeSeconds; });
    cinematic.audioKeys.insert(slot, std::move(key));
    return true;
}

bool RemoveCinematicAudioKeyUVE(CinematicComponentUVE& cinematic, const std::size_t index) {
    if (index >= cinematic.audioKeys.size()) {
        return false;
    }
    cinematic.audioKeys.erase(cinematic.audioKeys.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

std::optional<CinematicCameraPoseUVE> SampleCinematicCameraUVE(const CinematicComponentUVE& cinematic,
                                                               const double timeSeconds) noexcept {
    if (cinematic.cameraKeys.empty() || !std::isfinite(timeSeconds)) {
        return std::nullopt;
    }
    const auto upper = std::upper_bound(
        cinematic.cameraKeys.begin(), cinematic.cameraKeys.end(), timeSeconds,
        [](const double time, const CinematicCameraKeyUVE& existing) { return time < existing.timeSeconds; });
    if (upper == cinematic.cameraKeys.begin()) {
        const CinematicCameraKeyUVE& first = cinematic.cameraKeys.front();
        return CinematicCameraPoseUVE{first.position, first.rotation};
    }
    if (upper == cinematic.cameraKeys.end()) {
        const CinematicCameraKeyUVE& last = cinematic.cameraKeys.back();
        return CinematicCameraPoseUVE{last.position, last.rotation};
    }
    const CinematicCameraKeyUVE& right = *upper;
    const CinematicCameraKeyUVE& left = *(upper - 1);
    const double span = right.timeSeconds - left.timeSeconds;
    const float alpha =
        span <= 0.0 ? 0.0F : static_cast<float>((timeSeconds - left.timeSeconds) / span);
    CinematicCameraPoseUVE pose;
    pose.position = left.position + (right.position - left.position) * alpha;
    if (!Math::TrySlerpUVE(left.rotation, right.rotation, alpha, pose.rotation)) {
        pose.rotation = left.rotation;
    }
    return pose;
}

void WriteCinematicCameraPoseUVE(const CinematicCameraPoseUVE& pose, TransformComponentUVE& target) noexcept {
    target.localPosition = pose.position;
    Math::QuaternionUVE normalized{};
    if (Math::TryNormalizeUVE(pose.rotation, normalized)) {
        target.localRotation = normalized;
        Math::Vector3UVE euler{};
        if (Math::TryToEulerOrderedUVE(normalized, target.eulerOrder, euler)) {
            target.localEulerRadians = euler;
        }
    }
}

CinematicPlayResultUVE PlayCinematicUVE(CinematicComponentUVE& cinematic) noexcept {
    cinematic.isPlaying = true;
    cinematic.finished = false;
    cinematic.currentTimeSeconds = 0.0;
    CinematicPlayResultUVE fired;
    if (cinematic.durationSeconds <= 0.0) {
        return fired;
    }
    for (const CinematicEventKeyUVE& key : cinematic.events) {
        if (key.timeSeconds > 0.0) {
            break;
        }
        fired.firedEventIds.push_back(key.eventId);
    }
    for (const CinematicAnimationKeyUVE& key : cinematic.animationKeys) {
        if (key.timeSeconds > 0.0) {
            break;
        }
        fired.animationCues.push_back(CinematicAnimationCueUVE{key.target, key.clip});
    }
    for (const CinematicAudioKeyUVE& key : cinematic.audioKeys) {
        if (key.timeSeconds > 0.0) {
            break;
        }
        fired.audioCues.push_back(CinematicAudioCueUVE{key.audioAssetPath, key.volume, key.spatial,
                                                               key.position, key.minDistance,
                                                               key.maxDistance});
    }
    return fired;
}

void StopCinematicUVE(CinematicComponentUVE& cinematic) noexcept {
    cinematic.isPlaying = false;
}

CinematicStepResultUVE StepCinematicUVE(CinematicComponentUVE& cinematic, const float deltaSeconds) {
    CinematicStepResultUVE result;
    if (!cinematic.isPlaying || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F ||
        !std::isfinite(cinematic.durationSeconds) || cinematic.durationSeconds <= 0.0) {
        return result;
    }
    const double from = cinematic.currentTimeSeconds;
    const double advanced = from + static_cast<double>(deltaSeconds) * static_cast<double>(cinematic.speed);
    if (cinematic.loopMode == CinematicLoopModeUVE::Loop && cinematic.durationSeconds > 0.0) {
        double wrapped = std::fmod(advanced, cinematic.durationSeconds);
        if (wrapped < 0.0) {
            wrapped += cinematic.durationSeconds;
        }
        if (advanced >= cinematic.durationSeconds) {
            FirePassedKeysUVE(cinematic, from, cinematic.durationSeconds, result);
            FirePassedKeysUVE(cinematic, -1.0, wrapped, result);
        } else if (advanced < 0.0) {
            // Reverse motion rewinds the clock and resolves the camera, but fires nothing.
        } else {
            FirePassedKeysUVE(cinematic, from, wrapped, result);
        }
        cinematic.currentTimeSeconds = wrapped;
    } else {
        const double to = std::clamp(advanced, 0.0, cinematic.durationSeconds);
        if (to > from) {
            FirePassedKeysUVE(cinematic, from, to, result);
        }
        cinematic.currentTimeSeconds = to;
        if (advanced >= cinematic.durationSeconds) {
            cinematic.isPlaying = false;
            cinematic.finished = true;
            result.justFinished = true;
        }
    }
    result.activeCamera = CameraCutAtUVE(cinematic.cuts, cinematic.currentTimeSeconds);
    if (!cinematic.cameraKeys.empty()) {
        result.cameraPose = SampleCinematicCameraUVE(cinematic, cinematic.currentTimeSeconds);
    }
    return result;
}

EntityUVE ActiveCinematicCameraUVE(const CinematicComponentUVE& cinematic, const double timeSeconds) noexcept {
    if (!std::isfinite(timeSeconds)) {
        return kInvalidEntityUVE;
    }
    return CameraCutAtUVE(cinematic.cuts, timeSeconds);
}

} // namespace UVE::Scene
