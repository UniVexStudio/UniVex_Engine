// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

namespace UVE::Scene {

inline constexpr std::size_t kMaximumAnimationClipAssetPathBytesUVE = 1024U;

/// Authored scene animation-player state. The clip path is a project-relative identity; clip
/// decoding and pose evaluation remain owned by the core animation contracts. Runtime playback
/// state is intentionally not stored here so scene serialization remains deterministic.
struct AnimationPlayerComponentUVE final {
    std::string clipAssetPath;
    float playbackSpeed = 1.0F;
    bool looping = true;
    bool playOnAwake = true;
    bool enabled = true;
};

[[nodiscard]] bool IsAnimationClipAssetPathValidUVE(const std::string_view path) noexcept;

[[nodiscard]] bool IsAnimationPlayerComponentValidUVE(
    const AnimationPlayerComponentUVE& component) noexcept;

} // namespace UVE::Scene
