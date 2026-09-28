// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/animation_clip_editing_uve.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "uve/math/quaternion_uve.h"

namespace UVE::Editor {
namespace {

using SamplesUVE = std::vector<Asset::AnimationAssetSampleUVE>;

[[nodiscard]] double SnapUVE(const double seconds, const double frameRate, const double duration) {
    const double rate = std::isfinite(frameRate) && frameRate > 0.0 ? frameRate : 30.0;
    return std::clamp(std::round(seconds * rate) / rate, 0.0, std::max(duration, 0.0));
}

[[nodiscard]] bool SameTimeUVE(const double a, const double b) {
    return std::abs(a - b) < kClipKeyTimeToleranceUVE;
}

/// Puts `sample` into `samples`, replacing one at the same time, keeping them sorted.
void PlaceSampleUVE(SamplesUVE& samples, const Asset::AnimationAssetSampleUVE& sample) {
    const auto same = std::find_if(samples.begin(), samples.end(), [&sample](const Asset::AnimationAssetSampleUVE& s) {
        return SameTimeUVE(s.timeSeconds, sample.timeSeconds);
    });
    if (same != samples.end()) {
        *same = sample;
        return;
    }
    const auto at = std::lower_bound(samples.begin(), samples.end(), sample.timeSeconds,
                                     [](const Asset::AnimationAssetSampleUVE& s, const double t) {
                                         return s.timeSeconds < t;
                                     });
    samples.insert(at, sample);
}

/// The track's pose at `seconds`: linear position and scale, spherical rotation, held past the ends.
[[nodiscard]] Asset::AnimationAssetPoseUVE SampleUVE(const SamplesUVE& samples, const double seconds) {
    if (seconds <= samples.front().timeSeconds) {
        return samples.front().pose;
    }
    if (seconds >= samples.back().timeSeconds) {
        return samples.back().pose;
    }
    const auto next = std::lower_bound(samples.begin(), samples.end(), seconds,
                                       [](const Asset::AnimationAssetSampleUVE& s, const double t) {
                                           return s.timeSeconds < t;
                                       });
    const auto previous = next - 1;
    const double span = next->timeSeconds - previous->timeSeconds;
    const float alpha = span > 0.0 ? static_cast<float>((seconds - previous->timeSeconds) / span) : 0.0F;
    const auto lerp = [alpha](const Math::Vector3UVE& a, const Math::Vector3UVE& b) { return a + (b - a) * alpha; };
    Asset::AnimationAssetPoseUVE pose;
    pose.position = lerp(previous->pose.position, next->pose.position);
    pose.scale = lerp(previous->pose.scale, next->pose.scale);
    if (!Math::TrySlerpUVE(previous->pose.rotation, next->pose.rotation, alpha, pose.rotation)) {
        pose.rotation = alpha < 0.5F ? previous->pose.rotation : next->pose.rotation;
    }
    return pose;
}

} // namespace

std::vector<Asset::AnimationAssetSampleUVE>* FindClipTrackSamplesUVE(Asset::AnimationClipAssetUVE& clip,
                                                                    const std::string& track) {
    if (track.empty()) {
        return clip.samples.empty() ? nullptr : &clip.samples;
    }
    for (Asset::AnimationAssetBoneTrackUVE& bone : clip.bones) {
        if (bone.bone == track) {
            return &bone.samples;
        }
    }
    return nullptr;
}

const std::vector<Asset::AnimationAssetSampleUVE>* FindClipTrackSamplesUVE(const Asset::AnimationClipAssetUVE& clip,
                                                                          const std::string& track) {
    return FindClipTrackSamplesUVE(const_cast<Asset::AnimationClipAssetUVE&>(clip), track);
}

std::size_t DeleteClipKeysUVE(Asset::AnimationClipAssetUVE& clip, const std::vector<ClipKeyUVE>& keys) {
    std::size_t removed = 0U;
    for (const ClipKeyUVE& key : keys) {
        SamplesUVE* const samples = FindClipTrackSamplesUVE(clip, key.track);
        if (samples == nullptr || samples->size() <= 1U) {
            continue;
        }
        const auto found = std::find_if(samples->begin(), samples->end(), [&key](const Asset::AnimationAssetSampleUVE& s) {
            return SameTimeUVE(s.timeSeconds, key.timeSeconds);
        });
        if (found != samples->end()) {
            samples->erase(found);
            ++removed;
        }
    }
    return removed;
}

std::vector<ClipKeyUVE> MoveClipKeysUVE(Asset::AnimationClipAssetUVE& clip, const std::vector<ClipKeyUVE>& keys,
                                        const double deltaSeconds, const double frameRate) {
    // Lift every moving sample out first, so keys moving past each other never overwrite one another.
    struct LiftedUVE {
        std::string track;
        Asset::AnimationAssetSampleUVE sample;
    };
    std::vector<LiftedUVE> lifted;
    for (const ClipKeyUVE& key : keys) {
        SamplesUVE* const samples = FindClipTrackSamplesUVE(clip, key.track);
        if (samples == nullptr) {
            continue;
        }
        const auto found = std::find_if(samples->begin(), samples->end(), [&key](const Asset::AnimationAssetSampleUVE& s) {
            return SameTimeUVE(s.timeSeconds, key.timeSeconds);
        });
        if (found != samples->end()) {
            lifted.push_back(LiftedUVE{key.track, *found});
            samples->erase(found);
        }
    }
    std::vector<ClipKeyUVE> landed;
    landed.reserve(lifted.size());
    for (LiftedUVE& item : lifted) {
        item.sample.timeSeconds = SnapUVE(item.sample.timeSeconds + deltaSeconds, frameRate, clip.durationSeconds);
        // The track exists: the sample was lifted out of it above.
        PlaceSampleUVE(*FindClipTrackSamplesUVE(clip, item.track), item.sample);
        landed.push_back(ClipKeyUVE{item.track, item.sample.timeSeconds});
    }
    return landed;
}

ClipKeyClipboardUVE CopyClipKeysUVE(const Asset::AnimationClipAssetUVE& clip, const std::vector<ClipKeyUVE>& keys) {
    ClipKeyClipboardUVE clipboard;
    double earliest = std::numeric_limits<double>::infinity();
    for (const ClipKeyUVE& key : keys) {
        const SamplesUVE* const samples = FindClipTrackSamplesUVE(clip, key.track);
        if (samples == nullptr) {
            continue;
        }
        for (const Asset::AnimationAssetSampleUVE& sample : *samples) {
            if (SameTimeUVE(sample.timeSeconds, key.timeSeconds)) {
                clipboard.entries.push_back(ClipKeyClipboardUVE::EntryUVE{key.track, sample.timeSeconds, sample.pose});
                earliest = std::min(earliest, sample.timeSeconds);
                break;
            }
        }
    }
    for (ClipKeyClipboardUVE::EntryUVE& entry : clipboard.entries) {
        entry.offsetSeconds -= earliest;
    }
    return clipboard;
}

std::vector<ClipKeyUVE> PasteClipKeysUVE(Asset::AnimationClipAssetUVE& clip, const ClipKeyClipboardUVE& clipboard,
                                         const double atSeconds, const double frameRate) {
    std::vector<ClipKeyUVE> pasted;
    const double start = SnapUVE(atSeconds, frameRate, clip.durationSeconds);
    for (const ClipKeyClipboardUVE::EntryUVE& entry : clipboard.entries) {
        const double time = start + entry.offsetSeconds;
        if (time > clip.durationSeconds + kClipKeyTimeToleranceUVE) {
            continue;
        }
        SamplesUVE* const samples = FindClipTrackSamplesUVE(clip, entry.track);
        if (samples == nullptr) {
            continue;
        }
        Asset::AnimationAssetSampleUVE sample;
        sample.timeSeconds = SnapUVE(time, frameRate, clip.durationSeconds);
        sample.pose = entry.pose;
        PlaceSampleUVE(*samples, sample);
        pasted.push_back(ClipKeyUVE{entry.track, sample.timeSeconds});
    }
    return pasted;
}

bool InsertClipKeyUVE(Asset::AnimationClipAssetUVE& clip, const std::string& track, const double atSeconds,
                      const double frameRate) {
    SamplesUVE* const samples = FindClipTrackSamplesUVE(clip, track);
    if (samples == nullptr || samples->empty()) {
        return false;
    }
    const double time = SnapUVE(atSeconds, frameRate, clip.durationSeconds);
    for (const Asset::AnimationAssetSampleUVE& sample : *samples) {
        if (SameTimeUVE(sample.timeSeconds, time)) {
            return false;
        }
    }
    Asset::AnimationAssetSampleUVE sample;
    sample.timeSeconds = time;
    sample.pose = SampleUVE(*samples, time);
    PlaceSampleUVE(*samples, sample);
    return true;
}

float GetClipPoseComponentUVE(const Asset::AnimationAssetPoseUVE& pose, const int channel, const int axis) {
    const auto pick = [axis](const Math::Vector3UVE& v) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); };
    if (channel == 0) {
        return pick(pose.position);
    }
    if (channel == 2) {
        return pick(pose.scale);
    }
    Math::Vector3UVE radians{};
    if (!Math::TryToEulerUVE(pose.rotation, radians)) {
        return 0.0F;
    }
    constexpr float kDegreesPerRadianUVE = 57.29577951308232F;
    return pick(radians) * kDegreesPerRadianUVE;
}

bool SetClipKeyComponentUVE(Asset::AnimationClipAssetUVE& clip, const std::string& track, const double timeSeconds,
                            const int channel, const int axis, const float value) {
    SamplesUVE* const samples = FindClipTrackSamplesUVE(clip, track);
    if (samples == nullptr || !std::isfinite(value) || channel < 0 || channel > 2 || axis < 0 || axis > 2) {
        return false;
    }
    const auto found = std::find_if(samples->begin(), samples->end(), [timeSeconds](const Asset::AnimationAssetSampleUVE& s) {
        return SameTimeUVE(s.timeSeconds, timeSeconds);
    });
    if (found == samples->end()) {
        return false;
    }
    const auto set = [axis, value](Math::Vector3UVE& v) { (axis == 0 ? v.x : (axis == 1 ? v.y : v.z)) = value; };
    Asset::AnimationAssetPoseUVE& pose = found->pose;
    if (channel == 0) {
        set(pose.position);
        return true;
    }
    if (channel == 2) {
        set(pose.scale);
        return true;
    }
    Math::Vector3UVE radians{};
    if (!Math::TryToEulerUVE(pose.rotation, radians)) {
        return false;
    }
    constexpr float kRadiansPerDegreeUVE = 0.017453292519943295F;
    (axis == 0 ? radians.x : (axis == 1 ? radians.y : radians.z)) = value * kRadiansPerDegreeUVE;
    return Math::TryMakeEulerUVE(radians, pose.rotation);
}

} // namespace UVE::Editor
