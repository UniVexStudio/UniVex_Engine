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
/// humanoid (about 70 bones, 30 fps, a couple of minutes) rather than for one node's motion.
inline constexpr std::size_t kMaximumAnimationAssetPayloadBytesUVE = 96U * 1024U * 1024U;
inline constexpr std::size_t kMaximumAnimationAssetSamplesUVE = 4096U;
inline constexpr std::size_t kMaximumAnimationAssetEventsUVE = 1024U;
inline constexpr std::size_t kMaximumAnimationAssetIdentifierBytesUVE = 128U;
inline constexpr std::size_t kMaximumAnimationAssetBonesUVE = 256U;

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

/// A clip moves a node (`samples`), a skeleton (`bones`), or both. At least one of them has
/// samples; every track's times are sorted and inside [0, durationSeconds].
struct AnimationClipAssetUVE final {
    std::string clipId;
    double durationSeconds = 0.0;
    std::vector<AnimationAssetSampleUVE> samples;
    std::vector<AnimationAssetEventUVE> events;
    std::vector<AnimationAssetBoneTrackUVE> bones;

    [[nodiscard]] bool IsSkeletalUVE() const noexcept { return !bones.empty(); }
};

/// Validates the bounded serialized animation payload without performing runtime sampling.
[[nodiscard]] bool IsAnimationClipAssetValidUVE(const AnimationClipAssetUVE& clip) noexcept;

/// Loads a `.uvanim` envelope: the `uve-animation-v2` JSON payload, or the older
/// `uve-animation-v1` one (a node track with no bones).
/// Output is published only after envelope, schema, bounds, and finite-pose validation succeed.
[[nodiscard]] bool LoadAnimationClipAssetUVE(const std::filesystem::path& path,
                                              AnimationClipAssetUVE& outClip);

/// Saves a validated animation clip as an AssetKindUVE::Animation envelope.
[[nodiscard]] bool SaveAnimationClipAssetUVE(const AnimationClipAssetUVE& clip,
                                              const std::filesystem::path& path);

} // namespace UVE::Asset
