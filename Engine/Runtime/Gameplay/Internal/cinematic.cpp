// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "cinematic.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsValidId(const std::string_view id) noexcept {
    return !id.empty() && id.size() <= kMaximumCinematicEventIdBytes;
}

[[nodiscard]] bool IsValidKeyTime(const double timeSeconds, const double durationSeconds) noexcept {
    return std::isfinite(timeSeconds) && timeSeconds >= 0.0 && timeSeconds <= durationSeconds;
}

template <typename Key>
[[nodiscard]] bool IsTimeOrdered(const std::vector<Key>& keys, const double durationSeconds) noexcept {
    double previous = 0.0;
    bool first = true;
    for (const Key& key : keys) {
        if (!IsValidKeyTime(key.timeSeconds, durationSeconds)) {
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

[[nodiscard]] Entity CameraCutAt(const std::vector<CinematicCameraCut>& cuts,
                                       const double timeSeconds) noexcept {
    Entity camera = kInvalidEntity;
    for (const CinematicCameraCut& cut : cuts) {
        if (cut.timeSeconds > timeSeconds) {
            break;
        }
        camera = cut.camera;
    }
    return camera;
}

template <typename Key, typename Cue, typename Project>
void CollectTrack(const std::vector<Key>& keys, const double fromExclusive,
                     const double toInclusive, std::vector<Cue>& out, Project project) {
    for (const Key& key : keys) {
        if (key.timeSeconds > fromExclusive && key.timeSeconds <= toInclusive) {
            out.push_back(project(key));
        }
    }
}

void FirePassedKeys(const CinematicComponent& cinematic, const double fromExclusive,
                       const double toInclusive, CinematicStepResult& result) {
    CollectTrack(cinematic.events, fromExclusive, toInclusive, result.firedEventIds,
                    [](const CinematicEventKey& key) { return key.eventId; });
    CollectTrack(cinematic.animationKeys, fromExclusive, toInclusive, result.firedAnimationCues,
                    [](const CinematicAnimationKey& key) {
                        return CinematicAnimationCue{key.target, key.clip};
                    });
    CollectTrack(cinematic.audioKeys, fromExclusive, toInclusive, result.firedAudioCues,
                    [](const CinematicAudioKey& key) {
                        return CinematicAudioCue{key.audioAssetPath, key.volume, key.spatial,
                                                    key.position, key.minDistance, key.maxDistance};
                    });
}

} // namespace

bool IsCinematicComponentValid(const CinematicComponent& value) noexcept {
    if (!std::isfinite(value.durationSeconds) || value.durationSeconds <= 0.0 ||
        !std::isfinite(value.speed) || !std::isfinite(value.currentTimeSeconds) ||
        value.currentTimeSeconds < 0.0 || value.currentTimeSeconds > value.durationSeconds ||
        value.events.size() > kMaximumCinematicEvents || value.cuts.size() > kMaximumCinematicCuts ||
        value.cameraKeys.size() > kMaximumCinematicCameraKeys ||
        value.animationKeys.size() > kMaximumCinematicAnimationKeys ||
        value.audioKeys.size() > kMaximumCinematicAudioKeys) {
        return false;
    }
    for (const CinematicEventKey& key : value.events) {
        if (!IsValidId(key.eventId)) {
            return false;
        }
    }
    for (const CinematicCameraCut& cut : value.cuts) {
        if (cut.camera == kInvalidEntity) {
            return false;
        }
    }
    for (const CinematicCameraKey& key : value.cameraKeys) {
        if (!std::isfinite(key.position.x) || !std::isfinite(key.position.y) ||
            !std::isfinite(key.position.z) || !std::isfinite(key.rotation.x) ||
            !std::isfinite(key.rotation.y) || !std::isfinite(key.rotation.z) ||
            !std::isfinite(key.rotation.w) ||
            std::abs(Math::LengthSquared(key.rotation) - 1.0F) > 1e-4F) {
            return false;
        }
    }
    for (const CinematicAnimationKey& key : value.animationKeys) {
        if (key.target == kInvalidEntity) {
            return false;
        }
    }
    for (const CinematicAudioKey& key : value.audioKeys) {
        if (key.audioAssetPath.empty() ||
            key.audioAssetPath.size() > kMaximumCinematicAudioPathBytes ||
            !std::isfinite(key.volume) || key.volume < 0.0F ||
            !std::isfinite(key.minDistance) || key.minDistance <= 0.0F ||
            !std::isfinite(key.maxDistance) || key.maxDistance <= key.minDistance ||
            !std::isfinite(key.position.x) || !std::isfinite(key.position.y) ||
            !std::isfinite(key.position.z)) {
            return false;
        }
    }
    return IsTimeOrdered(value.events, value.durationSeconds) &&
           IsTimeOrdered(value.cuts, value.durationSeconds) &&
           IsTimeOrdered(value.cameraKeys, value.durationSeconds) &&
           IsTimeOrdered(value.animationKeys, value.durationSeconds) &&
           IsTimeOrdered(value.audioKeys, value.durationSeconds);
}

bool AddCinematicEvent(CinematicComponent& cinematic, const double timeSeconds, std::string eventId) {
    if (!IsValidId(eventId) || !IsValidKeyTime(timeSeconds, cinematic.durationSeconds) ||
        cinematic.events.size() >= kMaximumCinematicEvents) {
        return false;
    }
    CinematicEventKey key;
    key.timeSeconds = timeSeconds;
    key.eventId = std::move(eventId);
    const auto slot =
        std::upper_bound(cinematic.events.begin(), cinematic.events.end(), timeSeconds,
                         [](const double time, const CinematicEventKey& existing) { return time < existing.timeSeconds; });
    cinematic.events.insert(slot, std::move(key));
    return true;
}

bool AddCinematicCut(CinematicComponent& cinematic, const double timeSeconds, const Entity camera) {
    if (camera == kInvalidEntity || !IsValidKeyTime(timeSeconds, cinematic.durationSeconds) ||
        cinematic.cuts.size() >= kMaximumCinematicCuts) {
        return false;
    }
    CinematicCameraCut cut;
    cut.timeSeconds = timeSeconds;
    cut.camera = camera;
    const auto slot =
        std::upper_bound(cinematic.cuts.begin(), cinematic.cuts.end(), timeSeconds,
                         [](const double time, const CinematicCameraCut& existing) { return time < existing.timeSeconds; });
    cinematic.cuts.insert(slot, std::move(cut));
    return true;
}

bool RemoveCinematicEvent(CinematicComponent& cinematic, const std::size_t index) {
    if (index >= cinematic.events.size()) {
        return false;
    }
    cinematic.events.erase(cinematic.events.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool RemoveCinematicCut(CinematicComponent& cinematic, const std::size_t index) {
    if (index >= cinematic.cuts.size()) {
        return false;
    }
    cinematic.cuts.erase(cinematic.cuts.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool AddCinematicCameraKey(CinematicComponent& cinematic, const double timeSeconds,
                              const Math::Vector3 position, const Math::Quaternion rotation) {
    Math::Quaternion normalized{};
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
        !Math::TryNormalize(rotation, normalized) ||
        !IsValidKeyTime(timeSeconds, cinematic.durationSeconds) ||
        cinematic.cameraKeys.size() >= kMaximumCinematicCameraKeys) {
        return false;
    }
    CinematicCameraKey key;
    key.timeSeconds = timeSeconds;
    key.position = position;
    key.rotation = normalized;
    const auto slot = std::upper_bound(
        cinematic.cameraKeys.begin(), cinematic.cameraKeys.end(), timeSeconds,
        [](const double time, const CinematicCameraKey& existing) { return time < existing.timeSeconds; });
    cinematic.cameraKeys.insert(slot, std::move(key));
    return true;
}

bool RemoveCinematicCameraKey(CinematicComponent& cinematic, const std::size_t index) {
    if (index >= cinematic.cameraKeys.size()) {
        return false;
    }
    cinematic.cameraKeys.erase(cinematic.cameraKeys.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool AddCinematicAnimationKey(CinematicComponent& cinematic, const double timeSeconds,
                                 const Entity target, const Asset::AssetGuid clip) {
    if (target == kInvalidEntity || !IsValidKeyTime(timeSeconds, cinematic.durationSeconds) ||
        cinematic.animationKeys.size() >= kMaximumCinematicAnimationKeys) {
        return false;
    }
    CinematicAnimationKey key;
    key.timeSeconds = timeSeconds;
    key.target = target;
    key.clip = clip;
    const auto slot = std::upper_bound(
        cinematic.animationKeys.begin(), cinematic.animationKeys.end(), timeSeconds,
        [](const double time, const CinematicAnimationKey& existing) {
            return time < existing.timeSeconds;
        });
    cinematic.animationKeys.insert(slot, std::move(key));
    return true;
}

bool RemoveCinematicAnimationKey(CinematicComponent& cinematic, const std::size_t index) {
    if (index >= cinematic.animationKeys.size()) {
        return false;
    }
    cinematic.animationKeys.erase(cinematic.animationKeys.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool AddCinematicAudioKey(CinematicComponent& cinematic, const double timeSeconds,
                             std::string audioAssetPath, const float volume) {
    CinematicAudioKey key;
    key.timeSeconds = timeSeconds;
    key.audioAssetPath = std::move(audioAssetPath);
    key.volume = volume;
    return AddCinematicAudioKey(cinematic, std::move(key));
}

bool AddCinematicAudioKey(CinematicComponent& cinematic, CinematicAudioKey key) {
    if (key.audioAssetPath.empty() || key.audioAssetPath.size() > kMaximumCinematicAudioPathBytes ||
        !std::isfinite(key.volume) || key.volume < 0.0F ||
        !std::isfinite(key.minDistance) || key.minDistance <= 0.0F ||
        !std::isfinite(key.maxDistance) || key.maxDistance <= key.minDistance ||
        !std::isfinite(key.position.x) || !std::isfinite(key.position.y) ||
        !std::isfinite(key.position.z) || !IsValidKeyTime(key.timeSeconds, cinematic.durationSeconds) ||
        cinematic.audioKeys.size() >= kMaximumCinematicAudioKeys) {
        return false;
    }
    const double timeSeconds = key.timeSeconds;
    const auto slot = std::upper_bound(
        cinematic.audioKeys.begin(), cinematic.audioKeys.end(), timeSeconds,
        [](const double time, const CinematicAudioKey& existing) { return time < existing.timeSeconds; });
    cinematic.audioKeys.insert(slot, std::move(key));
    return true;
}

bool RemoveCinematicAudioKey(CinematicComponent& cinematic, const std::size_t index) {
    if (index >= cinematic.audioKeys.size()) {
        return false;
    }
    cinematic.audioKeys.erase(cinematic.audioKeys.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

std::optional<CinematicCameraPose> SampleCinematicCamera(const CinematicComponent& cinematic,
                                                               const double timeSeconds) noexcept {
    if (cinematic.cameraKeys.empty() || !std::isfinite(timeSeconds)) {
        return std::nullopt;
    }
    const auto upper = std::upper_bound(
        cinematic.cameraKeys.begin(), cinematic.cameraKeys.end(), timeSeconds,
        [](const double time, const CinematicCameraKey& existing) { return time < existing.timeSeconds; });
    if (upper == cinematic.cameraKeys.begin()) {
        const CinematicCameraKey& first = cinematic.cameraKeys.front();
        return CinematicCameraPose{first.position, first.rotation};
    }
    if (upper == cinematic.cameraKeys.end()) {
        const CinematicCameraKey& last = cinematic.cameraKeys.back();
        return CinematicCameraPose{last.position, last.rotation};
    }
    const CinematicCameraKey& right = *upper;
    const CinematicCameraKey& left = *(upper - 1);
    const double span = right.timeSeconds - left.timeSeconds;
    const float alpha =
        span <= 0.0 ? 0.0F : static_cast<float>((timeSeconds - left.timeSeconds) / span);
    CinematicCameraPose pose;
    pose.position = left.position + (right.position - left.position) * alpha;
    if (!Math::TrySlerp(left.rotation, right.rotation, alpha, pose.rotation)) {
        pose.rotation = left.rotation;
    }
    return pose;
}

void WriteCinematicCameraPose(const CinematicCameraPose& pose, TransformComponent& target) noexcept {
    target.localPosition = pose.position;
    Math::Quaternion normalized{};
    if (Math::TryNormalize(pose.rotation, normalized)) {
        target.localRotation = normalized;
        Math::Vector3 euler{};
        if (Math::TryToEulerOrdered(normalized, target.eulerOrder, euler)) {
            target.localEulerRadians = euler;
        }
    }
}

CinematicPlayResult PlayCinematic(CinematicComponent& cinematic) noexcept {
    cinematic.isPlaying = true;
    cinematic.finished = false;
    cinematic.currentTimeSeconds = 0.0;
    CinematicPlayResult fired;
    if (cinematic.durationSeconds <= 0.0) {
        return fired;
    }
    for (const CinematicEventKey& key : cinematic.events) {
        if (key.timeSeconds > 0.0) {
            break;
        }
        fired.firedEventIds.push_back(key.eventId);
    }
    for (const CinematicAnimationKey& key : cinematic.animationKeys) {
        if (key.timeSeconds > 0.0) {
            break;
        }
        fired.animationCues.push_back(CinematicAnimationCue{key.target, key.clip});
    }
    for (const CinematicAudioKey& key : cinematic.audioKeys) {
        if (key.timeSeconds > 0.0) {
            break;
        }
        fired.audioCues.push_back(CinematicAudioCue{key.audioAssetPath, key.volume, key.spatial,
                                                               key.position, key.minDistance,
                                                               key.maxDistance});
    }
    return fired;
}

void StopCinematic(CinematicComponent& cinematic) noexcept {
    cinematic.isPlaying = false;
}

CinematicStepResult StepCinematic(CinematicComponent& cinematic, const float deltaSeconds) {
    CinematicStepResult result;
    if (!cinematic.isPlaying || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F ||
        !std::isfinite(cinematic.durationSeconds) || cinematic.durationSeconds <= 0.0) {
        return result;
    }
    const double from = cinematic.currentTimeSeconds;
    const double advanced = from + static_cast<double>(deltaSeconds) * static_cast<double>(cinematic.speed);
    if (cinematic.loopMode == CinematicLoopMode::Loop && cinematic.durationSeconds > 0.0) {
        double wrapped = std::fmod(advanced, cinematic.durationSeconds);
        if (wrapped < 0.0) {
            wrapped += cinematic.durationSeconds;
        }
        if (advanced >= cinematic.durationSeconds) {
            FirePassedKeys(cinematic, from, cinematic.durationSeconds, result);
            FirePassedKeys(cinematic, -1.0, wrapped, result);
        } else if (advanced < 0.0) {
            // Reverse motion rewinds the clock and resolves the camera, but fires nothing.
        } else {
            FirePassedKeys(cinematic, from, wrapped, result);
        }
        cinematic.currentTimeSeconds = wrapped;
    } else {
        const double to = std::clamp(advanced, 0.0, cinematic.durationSeconds);
        if (to > from) {
            FirePassedKeys(cinematic, from, to, result);
        }
        cinematic.currentTimeSeconds = to;
        if (advanced >= cinematic.durationSeconds) {
            cinematic.isPlaying = false;
            cinematic.finished = true;
            result.justFinished = true;
        }
    }
    result.activeCamera = CameraCutAt(cinematic.cuts, cinematic.currentTimeSeconds);
    if (!cinematic.cameraKeys.empty()) {
        result.cameraPose = SampleCinematicCamera(cinematic, cinematic.currentTimeSeconds);
    }
    return result;
}

Entity ActiveCinematicCamera(const CinematicComponent& cinematic, const double timeSeconds) noexcept {
    if (!std::isfinite(timeSeconds)) {
        return kInvalidEntity;
    }
    return CameraCutAt(cinematic.cuts, timeSeconds);
}

} // namespace UVE::Scene
