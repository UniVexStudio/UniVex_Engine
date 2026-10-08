// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "uve/core/engine_config_uve.h"

namespace UVE::Core {

enum class RenderBackendKindUVE : std::uint8_t {
    OpenGL,
    Vulkan,
    Null,
};

struct RenderBackendAttemptOrderUVE final {
    std::array<RenderBackendKindUVE, 3U> backends{};
    std::size_t count = 0U;

    [[nodiscard]] constexpr bool Contains(const RenderBackendKindUVE backend) const noexcept {
        for (std::size_t index = 0U; index < count; ++index) {
            if (backends[index] == backend) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr bool operator==(const RenderBackendAttemptOrderUVE&) const noexcept = default;
};

/// Resolves the configured starting backend to the runtime's deterministic fallback order.
/// Auto preserves OpenGL as the production default; forcing Null skips all real backends.
[[nodiscard]] constexpr RenderBackendAttemptOrderUVE ResolveRenderBackendAttemptOrderUVE(
    const RenderBackendPreferenceUVE preference) noexcept {
    switch (preference) {
    case RenderBackendPreferenceUVE::VulkanUVE:
        return {{{RenderBackendKindUVE::Vulkan, RenderBackendKindUVE::OpenGL, RenderBackendKindUVE::Null}}, 3U};
    case RenderBackendPreferenceUVE::NullUVE:
        return {{{RenderBackendKindUVE::Null, RenderBackendKindUVE::OpenGL, RenderBackendKindUVE::Vulkan}}, 1U};
    case RenderBackendPreferenceUVE::AutoUVE:
    case RenderBackendPreferenceUVE::OpenGLUVE:
    default:
        return {{{RenderBackendKindUVE::OpenGL, RenderBackendKindUVE::Null, RenderBackendKindUVE::Vulkan}}, 2U};
    }
}

} // namespace UVE::Core
