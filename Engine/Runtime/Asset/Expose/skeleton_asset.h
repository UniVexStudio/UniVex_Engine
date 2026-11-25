// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "gltf_skeleton.h"
#include "quaternion.h"
#include "vector3.h"

namespace UVE::Asset {

/// Bounds mirror the animation clip's bone caps — a skeleton never needs more joints than a
/// clip can track, and identifiers stay short enough for stable hashing and logging.
inline constexpr std::size_t kMaximumSkeletonAssetJoints = 256U;
inline constexpr std::size_t kMaximumSkeletonAssetIdentifierBytes = 128U;
inline constexpr std::size_t kMaximumSkeletonAssetPayloadBytes = 1U * 1024U * 1024U;

/// One joint of a standalone skeleton: its name (how clips and meshes find it), its parent (an
/// index into the same list, lower than its own, -1 for a root), and its rest pose. Same shape
/// as AnimationAssetRestBone by design — a clip's `rest` is the skeleton its tracks were
/// made for — but a distinct type so the skeleton schema can evolve on its own.
struct SkeletonJoint final {
    std::string name;
    std::int32_t parent = -1;
    Math::Vector3 position;
    Math::Quaternion rotation;
    Math::Vector3 scale{1.0F, 1.0F, 1.0F};
    [[nodiscard]] bool operator==(const SkeletonJoint&) const noexcept = default;
};

/// The CPU-side, engine-native representation of a skeleton asset: a named, persisted joint
/// hierarchy that animation clips match by bone name and skinned meshes share, instead of every
/// mesh embedding its own copy. MeshAsset::joints stays as-is — a future increment may
/// migrate meshes to reference this by GUID, but nothing here requires that.
struct SkeletonAsset final {
    std::string skeletonId;
    std::vector<SkeletonJoint> joints;
    [[nodiscard]] bool operator==(const SkeletonAsset&) const noexcept = default;
};

/// Validates the CPU skeleton descriptor before persistence or handoff. The id is non-empty and
/// bounded with no NUL; at least one joint and at most the cap; names unique, non-empty, bounded,
/// NUL-free; every parent is -1 or a lower index (parents precede children, so pose resolution
/// runs as one forward pass); TRS finite with a normalizable rotation.
[[nodiscard]] bool IsSkeletonAssetValid(const SkeletonAsset& skeleton) noexcept;

/// Adopts a skeleton read from a model file - a glTF skin or an FBX rig - as a standalone skeleton
/// asset. `skeletonId` names it for logging and for a clip's `rest` to point at.
///
/// The two shapes differ in exactly two ways, and both are decided here, once, so every importer
/// makes the same choice: `GltfJoint::parentIndex` becomes `SkeletonJoint::parent` (both -1 for
/// a root, both parents-first), and a joint's resting pose is carried over as is. Nothing is
/// re-oriented or re-scaled: a skeleton read by ReadGltfSkeleton or ReadFbxSkeleton is already
/// in the engine's space, and a second conversion would move a character that is already upright.
///
/// Returns a descriptor that could hold the joints; `IsSkeletonAssetValid` is what says whether
/// it may be written. Names not unique in `source` keep their first occurrence - uniqueness is the
/// source reader's job, and this bridge does not silently rename a bone a clip already tracks.
[[nodiscard]] SkeletonAsset MakeSkeletonAsset(const GltfSkeleton& source, std::string skeletonId);

/// Loads `path` as a `.uve*` envelope with `AssetKind::Skeleton`, filling `outSkeleton`.
/// Returns false (logging the reason) if the file is missing/malformed, isn't actually a
/// Skeleton asset, or fails validation.
[[nodiscard]] bool LoadSkeletonAsset(const std::filesystem::path& path, SkeletonAsset& outSkeleton);

/// Writes `skeleton` to `path` as a `.uve*` envelope with `AssetKind::Skeleton`. Returns false
/// (logging the reason) for an invalid descriptor or a file publication failure; invalid
/// descriptors are rejected before opening the destination.
[[nodiscard]] bool SaveSkeletonAsset(const SkeletonAsset& skeleton, const std::filesystem::path& path);

} // namespace UVE::Asset
