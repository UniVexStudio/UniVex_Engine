// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

struct BoneAttachment3DNodeComponentUVE final {

    std::uint32_t skeletonLocalId = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t boneIndex = std::numeric_limits<std::uint32_t>::max();
    std::string boneName;
    Math::Vector3UVE localPosition{};
    Math::QuaternionUVE localRotation{};
    Math::Vector3UVE localScale{1.0F, 1.0F, 1.0F};
    bool enabled = true;
};

[[nodiscard]] bool IsBoneAttachment3DNodeComponentValidUVE(const BoneAttachment3DNodeComponentUVE& value) noexcept;

/// Returns whether an attachment has enough explicit references to participate in runtime
/// binding. A default attachment is valid scene data but remains inert until this is true.
[[nodiscard]] inline bool IsBoneAttachment3DNodeComponentResolvableUVE(
    const BoneAttachment3DNodeComponentUVE& value) noexcept {
    const bool hasSkeletonReference = value.skeletonLocalId != std::numeric_limits<std::uint32_t>::max();
    const bool hasBoneReference = value.boneIndex != std::numeric_limits<std::uint32_t>::max() ||
                                  !value.boneName.empty();
    return value.enabled && hasSkeletonReference && hasBoneReference;
}

} // namespace UVE::Scene
