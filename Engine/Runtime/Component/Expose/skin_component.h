// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "matrix4x4.h"

namespace UVE::Scene {

/// A single bone bind entry in a Skin resource / SkinComponent.
struct SkinBoneBind final {
    std::string boneName;
    std::int32_t boneIndex = -1;
    Math::Matrix4x4 inverseBindPose = Math::Matrix4x4::Identity();

    [[nodiscard]] bool operator==(const SkinBoneBind&) const = default;
};

/// Represents the Skin resource attached to a MeshInstance3D / mesh entity.
/// Defines the bind count and per-bone inverse bind poses for skeleton skinning.
struct SkinComponent final {
    std::uint32_t bindCount = 0;
    std::vector<SkinBoneBind> boneBinds;

    [[nodiscard]] bool operator==(const SkinComponent&) const = default;
};

/// Validates that bindCount is consistent and valid.
[[nodiscard]] bool IsSkinComponentValid(const SkinComponent& skin) noexcept;

} // namespace UVE::Scene
