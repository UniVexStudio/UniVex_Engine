// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_timeline_tracks_uve.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <utility>

#include "uve/objects/3d/animation_sequencer_uve.h"

namespace UVE::Editor {
namespace {

constexpr int kChannelCountUVE = 3;
constexpr const char* kChannelNameUVE[kChannelCountUVE] = {"Position", "Rotation", "Scale"};
/// The clip's own object track has no name of its own; this is the one every part of the tab uses
/// for it, so the row, its channels and the selection all agree.
constexpr const char* kObjectTrackLabelUVE = "Transform";
constexpr float kMorphWeightEpsilonUVE = 1.0e-5F;

[[nodiscard]] std::string LowercaseUVE(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// True when `label` contains `filter`, case-insensitively. An empty filter matches everything.
[[nodiscard]] bool MatchesFilterUVE(const std::string& label, const std::string& filter) {
    if (filter.empty()) {
        return true;
    }
    return LowercaseUVE(label).find(LowercaseUVE(filter)) != std::string::npos;
}

[[nodiscard]] bool IsExpandedUVE(const std::span<const std::string> expandedTracks, const std::string& track) {
    return std::find(expandedTracks.begin(), expandedTracks.end(), track) != expandedTracks.end();
}

void PushChannelRowsUVE(std::vector<TimelineRowUVE>& rows, const std::string& track, const std::size_t trackIndex,
                        const bool objectTrack, const int depth) {
    for (int channel = 0; channel < kChannelCountUVE; ++channel) {
        TimelineRowUVE row;
        row.kind = objectTrack ? TimelineRowKindUVE::TransformChannel : TimelineRowKindUVE::BoneChannel;
        row.label = kChannelNameUVE[channel];
        row.track = track;
        row.trackIndex = trackIndex;
        row.depth = depth;
        row.channel = channel;
        rows.push_back(std::move(row));
    }
}

void PushTrackUVE(std::vector<TimelineRowUVE>& rows, const std::span<const std::string> expandedTracks,
                  const TimelineRowKindUVE kind, const std::string& label, const std::size_t trackIndex,
                  const int depth) {
    TimelineRowUVE row;
    row.kind = kind;
    row.label = label;
    row.track = label;
    row.trackIndex = trackIndex;
    row.depth = depth;
    rows.push_back(row);
    if (IsExpandedUVE(expandedTracks, label)) {
        PushChannelRowsUVE(rows, label, trackIndex, kind == TimelineRowKindUVE::Transform, depth + 1);
    }
}

/// True when the two weight vectors differ by more than the epsilon, or in length.
[[nodiscard]] bool WeightsDifferUVE(const std::vector<float>& left, const std::vector<float>& right) {
    if (left.size() != right.size()) {
        return true;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (std::abs(left[index] - right[index]) > kMorphWeightEpsilonUVE) {
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<TimelineRowUVE> BuildTimelineRowsUVE(const Asset::AnimationClipAssetUVE& clip,
                                                 const TimelineSkeletonUVE& skeleton,
                                                 const std::string_view filter,
                                                 const std::span<const std::string> expandedTracks) {
    const std::string needle{filter};
    std::vector<TimelineRowUVE> rows;
    rows.reserve(1U + 4U * (clip.bones.size() + 1U) + clip.morphWeights.size());

    // Always there, so the first event can be added on a clip that has nothing else.
    TimelineRowUVE events;
    events.kind = TimelineRowKindUVE::Events;
    events.label = "Events";
    rows.push_back(std::move(events));

    // The clip's own motion, never filtered: it is the row an empty-looking tab is usually missing.
    if (!clip.samples.empty()) {
        PushTrackUVE(rows, expandedTracks, TimelineRowKindUVE::Transform, kObjectTrackLabelUVE, 0U, 0);
    }

    // Bone rows follow the RIG when there is one: a track for a bone the skeleton does not have is
    // not drawn, and the bones are indented as they are joined. Without a skeleton the clip's own
    // order is all there is to go on, and every row is a root.
    if (skeleton.present) {
        for (const TimelineSkeletonUVE::BoneUVE& bone : skeleton.bones) {
            const auto track = std::find_if(clip.bones.begin(), clip.bones.end(),
                                            [&bone](const Asset::AnimationAssetBoneTrackUVE& candidate) {
                                                return candidate.bone == bone.name;
                                            });
            if (track == clip.bones.end() || !MatchesFilterUVE(bone.name, needle)) {
                continue;
            }
            PushTrackUVE(rows, expandedTracks, TimelineRowKindUVE::Bone, bone.name,
                         static_cast<std::size_t>(std::distance(clip.bones.begin(), track)), bone.depth);
        }
    } else {
        for (std::size_t index = 0U; index < clip.bones.size(); ++index) {
            const Asset::AnimationAssetBoneTrackUVE& track = clip.bones[index];
            if (MatchesFilterUVE(track.bone, needle)) {
                PushTrackUVE(rows, expandedTracks, TimelineRowKindUVE::Bone, track.bone, index, 0);
            }
        }
    }

    // Blend shapes last, in the clip's own order, so a 90-bone rig does not push the face off the
    // bottom of the tab.
    for (std::size_t index = 0U; index < clip.morphWeights.size(); ++index) {
        const Asset::AnimationAssetMorphTrackUVE& track = clip.morphWeights[index];
        if (!MatchesFilterUVE(track.node, needle)) {
            continue;
        }
        TimelineRowUVE row;
        row.kind = TimelineRowKindUVE::Morph;
        row.label = track.node;
        row.track = track.node;
        row.trackIndex = index;
        rows.push_back(std::move(row));
    }

    return rows;
}

TimelineMorphPreviewUVE ResolveTimelineMorphPreviewUVE(const Asset::AnimationClipAssetUVE& clip,
                                                       const double timeSeconds,
                                                       const TimelineMorphSceneUVE& scene) {
    TimelineMorphPreviewUVE preview;
    if (!scene.entityForNode || !scene.targetNames) {
        preview.unresolvedTracks = clip.morphWeights.size();
        return preview;
    }

    // One buffer for the whole clip: every key of a track carries the same count, so the sampler
    // reuses its allocation instead of growing a vector per track per frame.
    std::vector<float> sampled;
    for (const Asset::AnimationAssetMorphTrackUVE& track : clip.morphWeights) {
        const std::optional<Scene::EntityUVE> entity = scene.entityForNode(track.node);
        if (!entity.has_value()) {
            ++preview.unresolvedTracks;
            continue;
        }
        const std::span<const std::string> names = scene.targetNames(*entity);
        if (names.empty() || !Scene::SampleMorphWeightTrackUVE(track, timeSeconds, sampled) ||
            sampled.size() != names.size()) {
            ++preview.unresolvedTracks;
            continue;
        }
        bool finite = true;
        for (const float weight : sampled) {
            finite = finite && std::isfinite(weight);
        }
        if (!finite) {
            ++preview.unresolvedTracks;
            continue;
        }

        TimelineMorphWriteUVE write;
        write.entity = *entity;
        write.weights.reserve(names.size());
        for (std::size_t index = 0U; index < names.size(); ++index) {
            write.weights.push_back(Scene::MorphTargetWeightUVE{names[index], sampled[index]});
        }
        preview.writes.push_back(std::move(write));
    }
    return preview;
}

bool ShowsMorphKeyUVE(const Asset::AnimationAssetMorphTrackUVE& track, const std::size_t index) {
    const std::vector<Asset::AnimationAssetMorphWeightSampleUVE>& samples = track.samples;
    if (index >= samples.size()) {
        return false;
    }
    // The ends of a track are always keys, so a shape that holds still still shows where it is
    // authored to be.
    if (samples.size() == 1U || index == 0U || index + 1U == samples.size()) {
        return true;
    }
    return WeightsDifferUVE(samples[index - 1U].weights, samples[index].weights) ||
           WeightsDifferUVE(samples[index].weights, samples[index + 1U].weights);
}

} // namespace UVE::Editor
