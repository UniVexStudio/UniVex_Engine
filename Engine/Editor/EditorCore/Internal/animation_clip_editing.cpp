// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_clip_editing.h"

#include <algorithm>
#include <string_view>
#include <cmath>
#include <limits>

#include "quaternion.h"
#include "scalar.h"

namespace UVE::Editor {
namespace {

using Samples = std::vector<Asset::AnimationAssetSample>;

[[nodiscard]] double Snap(const double seconds, const double frameRate, const double duration) {
    const double rate = std::isfinite(frameRate) && frameRate > 0.0 ? frameRate : 30.0;
    return std::clamp(std::round(seconds * rate) / rate, 0.0, std::max(duration, 0.0));
}

[[nodiscard]] bool SameTime(const double a, const double b) {
    return std::abs(a - b) < kClipKeyTimeTolerance;
}

/// Puts `sample` into `samples`, replacing one at the same time, keeping them sorted.
void PlaceSample(Samples& samples, const Asset::AnimationAssetSample& sample) {
    const auto same = std::find_if(samples.begin(), samples.end(), [&sample](const Asset::AnimationAssetSample& s) {
        return SameTime(s.timeSeconds, sample.timeSeconds);
    });
    if (same != samples.end()) {
        *same = sample;
        return;
    }
    const auto at = std::lower_bound(samples.begin(), samples.end(), sample.timeSeconds,
                                     [](const Asset::AnimationAssetSample& s, const double t) {
                                         return s.timeSeconds < t;
                                     });
    samples.insert(at, sample);
}

/// The track's pose at `seconds`: linear position and scale, spherical rotation, held past the ends.
[[nodiscard]] Asset::AnimationAssetPose Sample(const Samples& samples, const double seconds) {
    if (seconds <= samples.front().timeSeconds) {
        return samples.front().pose;
    }
    if (seconds >= samples.back().timeSeconds) {
        return samples.back().pose;
    }
    const auto next = std::lower_bound(samples.begin(), samples.end(), seconds,
                                       [](const Asset::AnimationAssetSample& s, const double t) {
                                           return s.timeSeconds < t;
                                       });
    const auto previous = next - 1;
    const double span = next->timeSeconds - previous->timeSeconds;
    const float alpha = span > 0.0 ? static_cast<float>((seconds - previous->timeSeconds) / span) : 0.0F;
    const auto lerp = [alpha](const Math::Vector3& a, const Math::Vector3& b) { return a + (b - a) * alpha; };
    Asset::AnimationAssetPose pose;
    pose.position = lerp(previous->pose.position, next->pose.position);
    pose.scale = lerp(previous->pose.scale, next->pose.scale);
    if (!Math::TrySlerp(previous->pose.rotation, next->pose.rotation, alpha, pose.rotation)) {
        pose.rotation = alpha < 0.5F ? previous->pose.rotation : next->pose.rotation;
    }
    return pose;
}

} // namespace

std::vector<Asset::AnimationAssetSample>* FindClipTrackSamples(Asset::AnimationClipAsset& clip,
                                                                    const std::string& track) {
    if (track.empty()) {
        return clip.samples.empty() ? nullptr : &clip.samples;
    }
    if (track.front() == kClipObjectTrackPrefix) {
        const std::string_view name = std::string_view(track).substr(1U);
        for (Asset::AnimationAssetBoneTrack& object : clip.objects) {
            if (object.bone == name) {
                return &object.samples;
            }
        }
        return nullptr;
    }
    for (Asset::AnimationAssetBoneTrack& bone : clip.bones) {
        if (bone.bone == track) {
            return &bone.samples;
        }
    }
    return nullptr;
}

const std::vector<Asset::AnimationAssetSample>* FindClipTrackSamples(const Asset::AnimationClipAsset& clip,
                                                                          const std::string& track) {
    return FindClipTrackSamples(const_cast<Asset::AnimationClipAsset&>(clip), track);
}

std::size_t DeleteClipKeys(Asset::AnimationClipAsset& clip, const std::vector<ClipKey>& keys) {
    std::size_t removed = 0U;
    for (const ClipKey& key : keys) {
        Samples* const samples = FindClipTrackSamples(clip, key.track);
        if (samples == nullptr || samples->size() <= 1U) {
            continue;
        }
        const auto found = std::find_if(samples->begin(), samples->end(), [&key](const Asset::AnimationAssetSample& s) {
            return SameTime(s.timeSeconds, key.timeSeconds);
        });
        if (found != samples->end()) {
            samples->erase(found);
            ++removed;
        }
    }
    return removed;
}

std::vector<ClipKey> MoveClipKeys(Asset::AnimationClipAsset& clip, const std::vector<ClipKey>& keys,
                                        const double deltaSeconds, const double frameRate) {
    // Lift every moving sample out first, so keys moving past each other never overwrite one another.
    struct Lifted {
        std::string track;
        Asset::AnimationAssetSample sample;
    };
    std::vector<Lifted> lifted;
    for (const ClipKey& key : keys) {
        Samples* const samples = FindClipTrackSamples(clip, key.track);
        if (samples == nullptr) {
            continue;
        }
        const auto found = std::find_if(samples->begin(), samples->end(), [&key](const Asset::AnimationAssetSample& s) {
            return SameTime(s.timeSeconds, key.timeSeconds);
        });
        if (found != samples->end()) {
            lifted.push_back(Lifted{key.track, *found});
            samples->erase(found);
        }
    }
    std::vector<ClipKey> landed;
    landed.reserve(lifted.size());
    for (Lifted& item : lifted) {
        item.sample.timeSeconds = Snap(item.sample.timeSeconds + deltaSeconds, frameRate, clip.durationSeconds);
        Samples* const dest = item.track.empty() ? &clip.samples : FindClipTrackSamples(clip, item.track);
        if (dest == nullptr) {
            continue;
        }
        PlaceSample(*dest, item.sample);
        landed.push_back(ClipKey{item.track, item.sample.timeSeconds});
    }
    return landed;
}

ClipKeyClipboard CopyClipKeys(const Asset::AnimationClipAsset& clip, const std::vector<ClipKey>& keys) {
    ClipKeyClipboard clipboard;
    double earliest = std::numeric_limits<double>::infinity();
    for (const ClipKey& key : keys) {
        const Samples* const samples = FindClipTrackSamples(clip, key.track);
        if (samples == nullptr) {
            continue;
        }
        for (const Asset::AnimationAssetSample& sample : *samples) {
            if (SameTime(sample.timeSeconds, key.timeSeconds)) {
                clipboard.entries.push_back(ClipKeyClipboard::Entry{key.track, sample.timeSeconds, sample.pose});
                earliest = std::min(earliest, sample.timeSeconds);
                break;
            }
        }
    }
    for (ClipKeyClipboard::Entry& entry : clipboard.entries) {
        entry.offsetSeconds -= earliest;
    }
    return clipboard;
}

std::vector<ClipKey> PasteClipKeys(Asset::AnimationClipAsset& clip, const ClipKeyClipboard& clipboard,
                                         const double atSeconds, const double frameRate) {
    std::vector<ClipKey> pasted;
    const double start = Snap(atSeconds, frameRate, clip.durationSeconds);
    for (const ClipKeyClipboard::Entry& entry : clipboard.entries) {
        const double time = start + entry.offsetSeconds;
        if (time > clip.durationSeconds + kClipKeyTimeTolerance) {
            continue;
        }
        Samples* const samples = FindClipTrackSamples(clip, entry.track);
        if (samples == nullptr) {
            continue;
        }
        Asset::AnimationAssetSample sample;
        sample.timeSeconds = Snap(time, frameRate, clip.durationSeconds);
        sample.pose = entry.pose;
        PlaceSample(*samples, sample);
        pasted.push_back(ClipKey{entry.track, sample.timeSeconds});
    }
    return pasted;
}

bool InsertClipKey(Asset::AnimationClipAsset& clip, const std::string& track, const double atSeconds,
                      const double frameRate) {
    Samples* const samples = FindClipTrackSamples(clip, track);
    if (samples == nullptr || samples->empty()) {
        return false;
    }
    const double time = Snap(atSeconds, frameRate, clip.durationSeconds);
    for (const Asset::AnimationAssetSample& sample : *samples) {
        if (SameTime(sample.timeSeconds, time)) {
            return false;
        }
    }
    Asset::AnimationAssetSample sample;
    sample.timeSeconds = time;
    sample.pose = Sample(*samples, time);
    PlaceSample(*samples, sample);
    return true;
}

float GetClipPoseComponent(const Asset::AnimationAssetPose& pose, const int channel, const int axis) {
    const auto pick = [axis](const Math::Vector3& v) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); };
    if (channel == 0) {
        return pick(pose.position);
    }
    if (channel == 2) {
        return pick(pose.scale);
    }
    Math::Vector3 radians{};
    if (!Math::TryToEuler(pose.rotation, radians)) {
        return 0.0F;
    }
    return Math::RadToDeg(pick(radians));
}

bool SetClipKeyComponent(Asset::AnimationClipAsset& clip, const std::string& track, const double timeSeconds,
                            const int channel, const int axis, const float value) {
    Samples* const samples = FindClipTrackSamples(clip, track);
    if (samples == nullptr || !std::isfinite(value) || channel < 0 || channel > 2 || axis < 0 || axis > 2) {
        return false;
    }
    const auto found = std::find_if(samples->begin(), samples->end(), [timeSeconds](const Asset::AnimationAssetSample& s) {
        return SameTime(s.timeSeconds, timeSeconds);
    });
    if (found == samples->end()) {
        return false;
    }
    const auto set = [axis, value](Math::Vector3& v) { (axis == 0 ? v.x : (axis == 1 ? v.y : v.z)) = value; };
    Asset::AnimationAssetPose& pose = found->pose;
    if (channel == 0) {
        set(pose.position);
        return true;
    }
    if (channel == 2) {
        set(pose.scale);
        return true;
    }
    Math::Vector3 radians{};
    if (!Math::TryToEuler(pose.rotation, radians)) {
        return false;
    }
    (axis == 0 ? radians.x : (axis == 1 ? radians.y : radians.z)) = Math::DegToRad(value);
    return Math::TryMakeEuler(radians, pose.rotation);
}

namespace {

/// Re-sorts the events by time and returns where `moved` (identified by address before sorting)
/// now is.
std::size_t SortEvents(Asset::AnimationClipAsset& clip, const std::size_t moved) {
    std::vector<std::size_t> order(clip.events.size());
    for (std::size_t i = 0U; i < order.size(); ++i) {
        order[i] = i;
    }
    std::ranges::stable_sort(order, [&clip](const std::size_t a, const std::size_t b) {
        return clip.events[a].timeSeconds < clip.events[b].timeSeconds;
    });
    std::vector<Asset::AnimationAssetEvent> sorted;
    sorted.reserve(order.size());
    std::size_t where = 0U;
    for (std::size_t i = 0U; i < order.size(); ++i) {
        if (order[i] == moved) {
            where = i;
        }
        sorted.push_back(std::move(clip.events[order[i]]));
    }
    clip.events = std::move(sorted);
    return where;
}

} // namespace

std::size_t AddClipEvent(Asset::AnimationClipAsset& clip, const double atSeconds, const double frameRate) {
    const auto taken = [&clip](const std::string& name) {
        return std::ranges::any_of(clip.events, [&name](const auto& event) { return event.eventId == name; });
    };
    std::string name = "event";
    for (int suffix = 2; taken(name); ++suffix) {
        name = "event_" + std::to_string(suffix);
    }
    clip.events.push_back(Asset::AnimationAssetEvent{Snap(atSeconds, frameRate, clip.durationSeconds), name});
    return SortEvents(clip, clip.events.size() - 1U);
}

std::optional<std::size_t> MoveClipEvent(Asset::AnimationClipAsset& clip, const std::size_t index,
                                            const double toSeconds, const double frameRate) {
    if (index >= clip.events.size()) {
        return std::nullopt;
    }
    clip.events[index].timeSeconds = Snap(toSeconds, frameRate, clip.durationSeconds);
    return SortEvents(clip, index);
}

bool RenameClipEvent(Asset::AnimationClipAsset& clip, const std::size_t index, const std::string& name) {
    if (index >= clip.events.size() || name.empty() || name.size() > Asset::kMaximumAnimationAssetIdentifierBytes) {
        return false;
    }
    clip.events[index].eventId = name;
    return true;
}

bool RemoveClipEvent(Asset::AnimationClipAsset& clip, const std::size_t index) {
    if (index >= clip.events.size()) {
        return false;
    }
    clip.events.erase(clip.events.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

} // namespace UVE::Editor
