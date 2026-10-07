// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>

namespace UVE::Render::Vulkan {

enum class CompositeAlphaModeUVE : std::uint8_t {
    Opaque,
    PreMultiplied,
    PostMultiplied,
    Inherit,
};

enum class CompositeAlphaFallbackUVE : std::uint8_t {
    None,
    TransparencyUnavailable,
    OpaqueUnavailable,
    NoSupportedMode,
};

struct CompositeAlphaAvailabilityUVE final {
    bool opaque = false;
    bool preMultiplied = false;
    bool postMultiplied = false;
    bool inherit = false;
};

struct CompositeAlphaResolutionUVE final {
    std::optional<CompositeAlphaModeUVE> mode;
    CompositeAlphaFallbackUVE fallback = CompositeAlphaFallbackUVE::None;

    [[nodiscard]] constexpr bool operator==(const CompositeAlphaResolutionUVE&) const noexcept = default;
};

/// Chooses a surface composite-alpha mode, preferring pre/post/inherit for transparent requests
/// and opaque otherwise. Fallbacks use the deterministic opaque/pre/post/inherit order.
[[nodiscard]] constexpr CompositeAlphaResolutionUVE ResolveVulkanCompositeAlphaUVE(
    const bool transparencyRequested, const CompositeAlphaAvailabilityUVE available) noexcept {
    if (transparencyRequested) {
        if (available.preMultiplied) {
            return {CompositeAlphaModeUVE::PreMultiplied, CompositeAlphaFallbackUVE::None};
        }
        if (available.postMultiplied) {
            return {CompositeAlphaModeUVE::PostMultiplied, CompositeAlphaFallbackUVE::None};
        }
        if (available.inherit) {
            return {CompositeAlphaModeUVE::Inherit, CompositeAlphaFallbackUVE::None};
        }
        if (available.opaque) {
            return {CompositeAlphaModeUVE::Opaque, CompositeAlphaFallbackUVE::TransparencyUnavailable};
        }
        return {std::nullopt, CompositeAlphaFallbackUVE::TransparencyUnavailable};
    }

    if (available.opaque) {
        return {CompositeAlphaModeUVE::Opaque, CompositeAlphaFallbackUVE::None};
    }
    if (available.preMultiplied) {
        return {CompositeAlphaModeUVE::PreMultiplied, CompositeAlphaFallbackUVE::OpaqueUnavailable};
    }
    if (available.postMultiplied) {
        return {CompositeAlphaModeUVE::PostMultiplied, CompositeAlphaFallbackUVE::OpaqueUnavailable};
    }
    if (available.inherit) {
        return {CompositeAlphaModeUVE::Inherit, CompositeAlphaFallbackUVE::OpaqueUnavailable};
    }
    return {std::nullopt, CompositeAlphaFallbackUVE::NoSupportedMode};
}

} // namespace UVE::Render::Vulkan
