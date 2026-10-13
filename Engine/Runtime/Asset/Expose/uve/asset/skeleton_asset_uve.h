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

/// Bounds mirror the animation clip's bone caps — a skeleton never needs more joints than a
/// clip can track, and identifiers stay short enough for stable hashing and logging.
inline constexpr std::size_t kMaximumSkeletonAssetJointsUVE = 256U;
inline constexpr std::size_t kMaximumSkeletonAssetIdentifierBytesUVE = 128U;
inline constexpr std::size_t kMaximumSkeletonAssetPayloadBytesUVE = 1U * 1024U * 1024U;

/// One joint of a standalone skeleton: its name (how clips and meshes find it), its parent (an
/// index into the same list, lower than its own, -1 for a root), and its rest pose. Same shape
/// as AnimationAssetRestBoneUVE by design — a clip's `rest` is the skeleton its tracks were
/// made for — but a distinct type so the skeleton schema can evolve on its own.
struct SkeletonJointUVE final {
    std::string name;
    std::int32_t parent = -1;
    Math::Vector3UVE position;
    Math::QuaternionUVE rotation;
    Math::Vector3UVE scale{1.0F, 1.0F, 1.0F};
    [[nodiscard]] bool operator==(const SkeletonJointUVE&) const noexcept = default;
};

/// The CPU-side, engine-native representation of a skeleton asset: a named, persisted joint
/// hierarchy that animation clips match by bone name and skinned meshes share, instead of every
/// mesh embedding its own copy. MeshAssetUVE::joints stays as-is — a future increment may
/// migrate meshes to reference this by GUID, but nothing here requires that.
struct SkeletonAssetUVE final {
    std::string skeletonId;
    std::vector<SkeletonJointUVE> joints;
    [[nodiscard]] bool operator==(const SkeletonAssetUVE&) const noexcept = default;
};

/// Validates the CPU skeleton descriptor before persistence or handoff. The id is non-empty and
/// bounded with no NUL; at least one joint and at most the cap; names unique, non-empty, bounded,
/// NUL-free; every parent is -1 or a lower index (parents precede children, so pose resolution
/// runs as one forward pass); TRS finite with a normalizable rotation.
[[nodiscard]] bool IsSkeletonAssetValidUVE(const SkeletonAssetUVE& skeleton) noexcept;

/// Loads `path` as a `.uve*` envelope with `AssetKindUVE::Skeleton`, filling `outSkeleton`.
/// Returns false (logging the reason) if the file is missing/malformed, isn't actually a
/// Skeleton asset, or fails validation.
[[nodiscard]] bool LoadSkeletonAssetUVE(const std::filesystem::path& path, SkeletonAssetUVE& outSkeleton);

/// Writes `skeleton` to `path` as a `.uve*` envelope with `AssetKindUVE::Skeleton`. Returns false
/// (logging the reason) for an invalid descriptor or a file publication failure; invalid
/// descriptors are rejected before opening the destination.
[[nodiscard]] bool SaveSkeletonAssetUVE(const SkeletonAssetUVE& skeleton, const std::filesystem::path& path);

} // namespace UVE::Asset
