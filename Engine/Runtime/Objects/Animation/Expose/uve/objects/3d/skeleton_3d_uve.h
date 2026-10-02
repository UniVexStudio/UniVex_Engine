// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

inline constexpr std::size_t kMaximumSkeletonBonesUVE = 256U;

struct SkeletonBoneUVE final {
    std::string name;
    std::int32_t parentIndex = -1;
    Math::Vector3UVE localPosition{};
    Math::QuaternionUVE localRotation{};
    Math::Vector3UVE localScale{1.0F, 1.0F, 1.0F};

    [[nodiscard]] bool operator==(const SkeletonBoneUVE& other) const noexcept {
        return name == other.name && parentIndex == other.parentIndex &&
               localPosition.x == other.localPosition.x && localPosition.y == other.localPosition.y &&
               localPosition.z == other.localPosition.z && localRotation.x == other.localRotation.x &&
               localRotation.y == other.localRotation.y && localRotation.z == other.localRotation.z &&
               localRotation.w == other.localRotation.w && localScale.x == other.localScale.x &&
               localScale.y == other.localScale.y && localScale.z == other.localScale.z;
    }
};

/// Skeleton3D: a bone hierarchy in its rest pose. A new one is empty - bones are authored in a DCC
/// tool (a Blender armature, say) and arrive with the model they were exported with; this object
/// never invents them. `skeletonAssetPath` names that model, content-relative.
/// One bone's pose right now: local to its parent bone, like SkeletonBoneUVE's rest pose.
struct SkeletonBonePoseUVE final {
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE scale{1.0F, 1.0F, 1.0F};
};

struct Skeleton3DComponentUVE final {
    std::string skeletonAssetPath;
    std::vector<SkeletonBoneUVE> bones;
    bool enabled = true;

    // ---- Runtime state, written by animation; never saved --------------------------------------
    /// One entry per bone while something animates the skeleton; empty means the rest pose.
    std::vector<SkeletonBonePoseUVE> pose;

    /// Authored data only: a playing skeleton equals its saved self.
    [[nodiscard]] bool operator==(const Skeleton3DComponentUVE& other) const {
        return skeletonAssetPath == other.skeletonAssetPath && bones == other.bones && enabled == other.enabled;
    }
};

/// The pose a renderer should draw: `pose` when it covers every bone, else the rest pose.
[[nodiscard]] std::vector<SkeletonBonePoseUVE> GetSkeletonCurrentPoseUVE(const Skeleton3DComponentUVE& skeleton);

[[nodiscard]] bool IsSkeleton3DObjectComponentValidUVE(const Skeleton3DComponentUVE& value) noexcept;

/// Applies only an explicit authored/imported skeleton asset payload. This function deliberately
/// rejects an empty asset or empty hierarchy so retarget metadata cannot hydrate a Skeleton3D object.
[[nodiscard]] inline bool TryBindExplicitSkeleton3DAssetUVE(
    Skeleton3DComponentUVE& target, std::string assetPath, std::vector<SkeletonBoneUVE> bones) {
    Skeleton3DComponentUVE candidate;
    candidate.skeletonAssetPath = std::move(assetPath);
    candidate.bones = std::move(bones);
    candidate.enabled = target.enabled;
    if (candidate.skeletonAssetPath.empty() || candidate.bones.empty() ||
        !IsSkeleton3DObjectComponentValidUVE(candidate)) {
        return false;
    }
    target = std::move(candidate);
    return true;
}

struct Skeleton3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Skeleton3D";
    Skeleton3DComponentUVE skeleton{};
};

/// The Object3D recipe under this object's name, then the skeleton component.
void ApplySkeleton3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                      const Skeleton3DObjectDefinitionUVE& value);

} // namespace UVE::Scene
