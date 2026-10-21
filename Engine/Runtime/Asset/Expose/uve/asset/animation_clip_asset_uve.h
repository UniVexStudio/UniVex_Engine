// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Asset {

/// A skeletal clip is one track per bone, so the payload bound is sized for a long take on a full
/// humanoid (about 70 bones, 30 fps, a couple of minutes) rather than for one object's motion.
inline constexpr std::size_t kMaximumAnimationAssetPayloadBytesUVE = 96U * 1024U * 1024U;
inline constexpr std::size_t kMaximumAnimationAssetSamplesUVE = 4096U;
inline constexpr std::size_t kMaximumAnimationAssetEventsUVE = 1024U;
inline constexpr std::size_t kMaximumAnimationAssetIdentifierBytesUVE = 128U;
inline constexpr std::size_t kMaximumAnimationAssetBonesUVE = 256U;
/// How many blend-shape tracks one clip may carry. A facial clip drives ONE mesh (the head) and
/// occasionally a second (eyes as a separate mesh), so 32 is far past what content needs while
/// still bounding the per-frame work of a clip that was built to be hostile.
inline constexpr std::size_t kMaximumAnimationAssetMorphTracksUVE = 32U;

struct AnimationAssetPoseUVE final {
    Math::Vector3UVE position;
    Math::QuaternionUVE rotation;
    Math::Vector3UVE scale{1.0F, 1.0F, 1.0F};
    [[nodiscard]] bool operator==(const AnimationAssetPoseUVE&) const noexcept = default;
};

struct AnimationAssetSampleUVE final {
    double timeSeconds = 0.0;
    AnimationAssetPoseUVE pose;
    [[nodiscard]] bool operator==(const AnimationAssetSampleUVE&) const noexcept = default;
};

struct AnimationAssetEventUVE final {
    double timeSeconds = 0.0;
    std::string eventId;
    [[nodiscard]] bool operator==(const AnimationAssetEventUVE&) const noexcept = default;
};

/// One bone's motion: its local pose (relative to its parent bone, in the engine's metres and +Y
/// up) over time. Matched to a skeleton by name, so a clip plays on any skeleton with those bones.
struct AnimationAssetBoneTrackUVE final {
    std::string bone;
    std::vector<AnimationAssetSampleUVE> samples;
    [[nodiscard]] bool operator==(const AnimationAssetBoneTrackUVE&) const noexcept = default;
};

/// One blend-shape weight reading: the time, and one weight per target of the tracked mesh.
///
/// The whole weight vector is stored at every key rather than one track per shape. glTF's `weights`
/// channel is defined that way - its sampler output is a flat array of `keyCount * targetCount`
/// scalars - and keeping the shape of the source means an exporter's keys, and the order they were
/// written in, survive the round trip unchanged.
struct AnimationAssetMorphWeightSampleUVE final {
    double timeSeconds = 0.0;
    std::vector<float> weights;
    [[nodiscard]] bool operator==(const AnimationAssetMorphWeightSampleUVE&) const noexcept = default;
};

/// A blend-shape animation track: which mesh it drives, by node name, and its weight keys.
///
/// Keyed by NAME rather than by entity because a clip is asset data: it is authored against a rig,
/// stored in a file, and played on whatever entity the sequencer is attached to. A name is the only
/// identifier that survives that trip; an entity handle would mean nothing in the file and would
/// change every time a scene is rebuilt.
struct AnimationAssetMorphTrackUVE final {
    std::string node;
    std::vector<AnimationAssetMorphWeightSampleUVE> samples;
    [[nodiscard]] bool operator==(const AnimationAssetMorphTrackUVE&) const noexcept = default;
};

/// One bone of the skeleton a clip was made for, at rest: its parent (an index into the list, lower
/// than its own, -1 for a root) and its local pose. A clip's tracks say how bones move but not how
/// they are joined or how long they are; this is what retargeting needs to know.
struct AnimationAssetRestBoneUVE final {
    std::string bone;
    std::int32_t parent = -1;
    Math::Vector3UVE position;
    Math::QuaternionUVE rotation;
    Math::Vector3UVE scale{1.0F, 1.0F, 1.0F};
    [[nodiscard]] bool operator==(const AnimationAssetRestBoneUVE&) const noexcept = default;
};

/// A clip moves an object (`samples`), a skeleton (`bones`), or both. At least one of them has
/// samples; every track's times are sorted and inside [0, durationSeconds].
struct AnimationClipAssetUVE final {
    std::string clipId;
    double durationSeconds = 0.0;
    std::vector<AnimationAssetSampleUVE> samples;
    std::vector<AnimationAssetEventUVE> events;
    std::vector<AnimationAssetBoneTrackUVE> bones;

    /// The skeleton the tracks were made for, parents first; empty when the file did not say (older
    /// clips).
    std::vector<AnimationAssetRestBoneUVE> rest;
    /// Blend-shape tracks, one per tracked mesh. Empty for every clip authored before facial
    /// animation existed, which is why it is written only when it has content.
    std::vector<AnimationAssetMorphTrackUVE> morphWeights;
    /// True once retargeting has made the clip the humanoid's: its bones carry the humanoid's names
    /// and frames, and `rest` is the conformed skeleton (the humanoid's bones and frames, in the
    /// proportions of the rig the clip came from).
    bool conformed = false;
    [[nodiscard]] bool IsSkeletalUVE() const noexcept { return !bones.empty(); }
    [[nodiscard]] bool HasMorphTracksUVE() const noexcept { return !morphWeights.empty(); }
};

/// Validates the bounded serialized animation payload without performing runtime sampling.
[[nodiscard]] bool IsAnimationClipAssetValidUVE(const AnimationClipAssetUVE& clip) noexcept;

/// Loads a `.uvanim` envelope: the `uve-animation-v3` JSON payload (v2 plus the rest skeleton and
/// the conformed flag), the `uve-animation-v2` one, or the older `uve-animation-v1` (an object track
/// with no bones). Saving writes v3 only when the clip has a rest skeleton or is conformed, so a
/// clip with neither is byte-for-byte what it always was.
/// Output is published only after envelope, schema, bounds, and finite-pose validation succeed.
[[nodiscard]] bool LoadAnimationClipAssetUVE(const std::filesystem::path& path,
                                              AnimationClipAssetUVE& outClip);

/// Saves a validated animation clip as an AssetKindUVE::Animation envelope.
[[nodiscard]] bool SaveAnimationClipAssetUVE(const AnimationClipAssetUVE& clip,
                                              const std::filesystem::path& path);

} // namespace UVE::Asset
