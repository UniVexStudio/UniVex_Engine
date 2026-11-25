// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>

namespace UVE::Render::Vulkan {

enum class CompositeAlphaMode : std::uint8_t {
    Opaque,
    PreMultiplied,
    PostMultiplied,
    Inherit,
};

enum class CompositeAlphaFallback : std::uint8_t {
    None,
    TransparencyUnavailable,
    OpaqueUnavailable,
    NoSupportedMode,
};

struct CompositeAlphaAvailability final {
    bool opaque = false;
    bool preMultiplied = false;
    bool postMultiplied = false;
    bool inherit = false;
};

struct CompositeAlphaResolution final {
    std::optional<CompositeAlphaMode> mode;
    CompositeAlphaFallback fallback = CompositeAlphaFallback::None;

    [[nodiscard]] constexpr bool operator==(const CompositeAlphaResolution&) const noexcept = default;
};

/// Chooses a surface composite-alpha mode, preferring pre/post/inherit for transparent requests
/// and opaque otherwise. Fallbacks use the deterministic opaque/pre/post/inherit order.
[[nodiscard]] constexpr CompositeAlphaResolution ResolveVulkanCompositeAlpha(
    const bool transparencyRequested, const CompositeAlphaAvailability available) noexcept {
    if (transparencyRequested) {
        if (available.preMultiplied) {
            return {CompositeAlphaMode::PreMultiplied, CompositeAlphaFallback::None};
        }
        if (available.postMultiplied) {
            return {CompositeAlphaMode::PostMultiplied, CompositeAlphaFallback::None};
        }
        if (available.inherit) {
            return {CompositeAlphaMode::Inherit, CompositeAlphaFallback::None};
        }
        if (available.opaque) {
            return {CompositeAlphaMode::Opaque, CompositeAlphaFallback::TransparencyUnavailable};
        }
        return {std::nullopt, CompositeAlphaFallback::TransparencyUnavailable};
    }

    if (available.opaque) {
        return {CompositeAlphaMode::Opaque, CompositeAlphaFallback::None};
    }
    if (available.preMultiplied) {
        return {CompositeAlphaMode::PreMultiplied, CompositeAlphaFallback::OpaqueUnavailable};
    }
    if (available.postMultiplied) {
        return {CompositeAlphaMode::PostMultiplied, CompositeAlphaFallback::OpaqueUnavailable};
    }
    if (available.inherit) {
        return {CompositeAlphaMode::Inherit, CompositeAlphaFallback::OpaqueUnavailable};
    }
    return {std::nullopt, CompositeAlphaFallback::NoSupportedMode};
}

} // namespace UVE::Render::Vulkan
