// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumSkeletonBonesUVE = 256U;

struct SkeletonBoneUVE final {
    std::string name;
    std::int32_t parentIndex = -1;
    Math::Vector3UVE localPosition{};
    Math::QuaternionUVE localRotation{};
    Math::Vector3UVE localScale{1.0F, 1.0F, 1.0F};
};

struct Skeleton3DNodeComponentUVE final {
    std::string skeletonAssetPath;
    std::vector<SkeletonBoneUVE> bones;
    bool enabled = true;
};

[[nodiscard]] bool IsSkeleton3DNodeComponentValidUVE(const Skeleton3DNodeComponentUVE& value) noexcept;

/// Applies only an explicit authored/imported skeleton asset payload. This function deliberately
/// rejects an empty asset or empty hierarchy so retarget metadata cannot hydrate a Skeleton3D node.
[[nodiscard]] inline bool TryBindExplicitSkeleton3DAssetUVE(
    Skeleton3DNodeComponentUVE& target, std::string assetPath, std::vector<SkeletonBoneUVE> bones) {
    Skeleton3DNodeComponentUVE candidate;
    candidate.skeletonAssetPath = std::move(assetPath);
    candidate.bones = std::move(bones);
    candidate.enabled = target.enabled;
    if (candidate.skeletonAssetPath.empty() || candidate.bones.empty() ||
        !IsSkeleton3DNodeComponentValidUVE(candidate)) {
        return false;
    }
    target = std::move(candidate);
    return true;
}

} // namespace UVE::Scene
