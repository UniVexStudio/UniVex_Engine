// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

struct BoneAttachment3DComponentUVE final {

    std::uint32_t skeletonLocalId = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t boneIndex = std::numeric_limits<std::uint32_t>::max();
    std::string boneName;
    Math::Vector3UVE localPosition{};
    Math::QuaternionUVE localRotation{};
    Math::Vector3UVE localScale{1.0F, 1.0F, 1.0F};
    bool enabled = true;
};

[[nodiscard]] bool IsBoneAttachment3DObjectComponentValidUVE(const BoneAttachment3DComponentUVE& value) noexcept;

/// Returns whether an attachment has enough explicit references to participate in runtime
/// binding. A default attachment is valid scene data but remains inert until this is true.
[[nodiscard]] inline bool IsBoneAttachment3DObjectComponentResolvableUVE(
    const BoneAttachment3DComponentUVE& value) noexcept {
    const bool hasSkeletonReference = value.skeletonLocalId != std::numeric_limits<std::uint32_t>::max();
    const bool hasBoneReference = value.boneIndex != std::numeric_limits<std::uint32_t>::max() ||
                                  !value.boneName.empty();
    return value.enabled && hasSkeletonReference && hasBoneReference;
}

} // namespace UVE::Scene
