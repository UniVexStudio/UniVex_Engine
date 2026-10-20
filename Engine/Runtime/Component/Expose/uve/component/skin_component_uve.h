// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "uve/math/matrix4x4_uve.h"

namespace UVE::Scene {

/// A single bone bind entry in a Skin resource / SkinComponentUVE.
struct SkinBoneBindUVE final {
    std::string boneName;
    std::int32_t boneIndex = -1;
    Math::Matrix4x4UVE inverseBindPose = Math::Matrix4x4UVE::IdentityUVE();

    [[nodiscard]] bool operator==(const SkinBoneBindUVE&) const = default;
};

/// Represents the Skin resource attached to a MeshInstance3D / mesh entity.
/// Defines the bind count and per-bone inverse bind poses for skeleton skinning.
struct SkinComponentUVE final {
    std::uint32_t bindCount = 0;
    std::vector<SkinBoneBindUVE> boneBinds;

    [[nodiscard]] bool operator==(const SkinComponentUVE&) const = default;
};

/// Validates that bindCount is consistent and valid.
[[nodiscard]] bool IsSkinComponentValidUVE(const SkinComponentUVE& skin) noexcept;

} // namespace UVE::Scene
