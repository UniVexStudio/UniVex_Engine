// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "quaternion.h"
#include "vector3.h"

namespace UVE::Asset {

/// A skeletal clip is one track per bone, so the payload bound is sized for a long take on a full
/// humanoid (about 70 bones, 30 fps, a couple of minutes) rather than for one object's motion.
inline constexpr std::size_t kMaximumAnimationAssetPayloadBytes = 96U * 1024U * 1024U;
inline constexpr std::size_t kMaximumAnimationAssetSamples = 32768U;
inline constexpr std::size_t kMaximumAnimationAssetEvents = 1024U;
inline constexpr std::size_t kMaximumAnimationAssetIdentifierBytes = 128U;
inline constexpr std::size_t kMaximumAnimationAssetBones = 256U;
/// How many blend-shape tracks one clip may carry. A facial clip drives ONE mesh (the head) and
/// occasionally a second (eyes as a separate mesh), so 32 is far past what content needs while
/// still bounding the per-frame work of a clip that was built to be hostile.
inline constexpr std::size_t kMaximumAnimationAssetMorphTracks = 32U;

struct AnimationAssetPose final {
    Math::Vector3 position;
    Math::Quaternion rotation;
    Math::Vector3 scale{1.0F, 1.0F, 1.0F};
    [[nodiscard]] bool operator==(const AnimationAssetPose&) const noexcept = default;
};

struct AnimationAssetSample final {
    double timeSeconds = 0.0;
    AnimationAssetPose pose;
    [[nodiscard]] bool operator==(const AnimationAssetSample&) const noexcept = default;
};

struct AnimationAssetEvent final {
    double timeSeconds = 0.0;
    std::string eventId;
    [[nodiscard]] bool operator==(const AnimationAssetEvent&) const noexcept = default;
};

/// One bone's motion: its local pose (relative to its parent bone, in the engine's metres and +Y
/// up) over time. Matched to a skeleton by name, so a clip plays on any skeleton with those bones.
struct AnimationAssetBoneTrack final {
    std::string bone;
    std::vector<AnimationAssetSample> samples;
    [[nodiscard]] bool operator==(const AnimationAssetBoneTrack&) const noexcept = default;
};

/// One blend-shape weight reading: the time, and one weight per target of the tracked mesh.
///
/// The whole weight vector is stored at every key rather than one track per shape. glTF's `weights`
/// channel is defined that way - its sampler output is a flat array of `keyCount * targetCount`
/// scalars - and keeping the shape of the source means an exporter's keys, and the order they were
/// written in, survive the round trip unchanged.
struct AnimationAssetMorphWeightSample final {
    double timeSeconds = 0.0;
    std::vector<float> weights;
    [[nodiscard]] bool operator==(const AnimationAssetMorphWeightSample&) const noexcept = default;
};

/// A blend-shape animation track: which mesh it drives, by node name, and its weight keys.
///
/// Keyed by NAME rather than by entity because a clip is asset data: it is authored against a rig,
/// stored in a file, and played on whatever entity the sequencer is attached to. A name is the only
/// identifier that survives that trip; an entity handle would mean nothing in the file and would
/// change every time a scene is rebuilt.
struct AnimationAssetMorphTrack final {
    std::string node;
    std::vector<AnimationAssetMorphWeightSample> samples;
    [[nodiscard]] bool operator==(const AnimationAssetMorphTrack&) const noexcept = default;
};

/// One bone of the skeleton a clip was made for, at rest: its parent (an index into the list, lower
/// than its own, -1 for a root) and its local pose. A clip's tracks say how bones move but not how
/// they are joined or how long they are; this is what retargeting needs to know.
struct AnimationAssetRestBone final {
    std::string bone;
    std::int32_t parent = -1;
    Math::Vector3 position;
    Math::Quaternion rotation;
    Math::Vector3 scale{1.0F, 1.0F, 1.0F};
    [[nodiscard]] bool operator==(const AnimationAssetRestBone&) const noexcept = default;
};

/// A clip moves an object (`samples`), a skeleton (`bones`), or both. At least one of them has
/// samples; every track's times are sorted and inside [0, durationSeconds].
struct AnimationClipAsset final {
    std::string clipId;
    double durationSeconds = 0.0;
    std::vector<AnimationAssetSample> samples;
    std::vector<AnimationAssetEvent> events;
    std::vector<AnimationAssetBoneTrack> bones;
    /// Other objects the clip moves: one track per object under the animated one, matched by the
    /// object's name (`bone` holds it), each in its own local space. Written only when present.
    std::vector<AnimationAssetBoneTrack> objects;

    /// The skeleton the tracks were made for, parents first; empty when the file did not say (older
    /// clips).
    std::vector<AnimationAssetRestBone> rest;
    /// Blend-shape tracks, one per tracked mesh. Empty for every clip authored before facial
    /// animation existed, which is why it is written only when it has content.
    std::vector<AnimationAssetMorphTrack> morphWeights;
    /// True once retargeting has made the clip the humanoid's: its bones carry the humanoid's names
    /// and frames, and `rest` is the conformed skeleton (the humanoid's bones and frames, in the
    /// proportions of the rig the clip came from).
    bool conformed = false;
    [[nodiscard]] bool IsSkeletal() const noexcept { return !bones.empty(); }
    [[nodiscard]] bool HasMorphTracks() const noexcept { return !morphWeights.empty(); }
};

/// Validates the bounded serialized animation payload without performing runtime sampling.
[[nodiscard]] bool IsAnimationClipAssetValid(const AnimationClipAsset& clip) noexcept;

/// Loads a `.uvanim` envelope: the `uve-animation-v3` JSON payload (v2 plus the rest skeleton and
/// the conformed flag), the `uve-animation-v2` one, or the older `uve-animation-v1` (an object track
/// with no bones). Saving writes v3 only when the clip has a rest skeleton or is conformed, so a
/// clip with neither is byte-for-byte what it always was.
/// Output is published only after envelope, schema, bounds, and finite-pose validation succeed.
[[nodiscard]] bool LoadAnimationClipAsset(const std::filesystem::path& path,
                                              AnimationClipAsset& outClip);

/// Saves a validated animation clip as an AssetKind::Animation envelope.
[[nodiscard]] bool SaveAnimationClipAsset(const AnimationClipAsset& clip,
                                              const std::filesystem::path& path);
/// Lightweight metadata for batch scanning (Retarget planner) without parsing keyframe curves.
struct AnimationClipSummary final {
    bool valid = false;
    bool conformed = false;
    bool hasRest = false;
    bool hasBones = false;
};

/// Inspects a serialized clip payload via streaming SAX without allocating DOM nodes or keyframe arrays.
[[nodiscard]] AnimationClipSummary InspectAnimationClipSummary(std::span<const std::byte> payload);
/// Inspects a `.uvanim` file or `<model>.uvmodel#<take>` path without parsing keyframe curves.
[[nodiscard]] AnimationClipSummary InspectAnimationClipFileSummary(const std::filesystem::path& path);

/// A clip's payload (the JSON a .uvanim file's envelope holds), in memory: what a model keeps
/// for each of its takes. False for an invalid or oversized clip.
[[nodiscard]] bool SerializeAnimationClipPayload(const AnimationClipAsset& clip, std::vector<std::byte>& outPayload);
[[nodiscard]] bool ParseAnimationClipPayload(const std::vector<std::byte>& payload, AnimationClipAsset& outClip);

} // namespace UVE::Asset
