// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "engine_config.h"

namespace UVE::Core {

enum class RenderBackendKind : std::uint8_t {
    OpenGL,
    Vulkan,
    Null,
};

struct RenderBackendAttemptOrder final {
    std::array<RenderBackendKind, 3U> backends{};
    std::size_t count = 0U;

    [[nodiscard]] constexpr bool Contains(const RenderBackendKind backend) const noexcept {
        for (std::size_t index = 0U; index < count; ++index) {
            if (backends[index] == backend) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr bool operator==(const RenderBackendAttemptOrder&) const noexcept = default;
};

/// Resolves the configured starting backend to the runtime's deterministic fallback order.
/// Auto preserves OpenGL as the production default; forcing Null skips all real backends.
[[nodiscard]] constexpr RenderBackendAttemptOrder ResolveRenderBackendAttemptOrder(
    const RenderBackendPreference preference) noexcept {
    switch (preference) {
    case RenderBackendPreference::Vulkan:
        return {{{RenderBackendKind::Vulkan, RenderBackendKind::OpenGL, RenderBackendKind::Null}}, 3U};
    case RenderBackendPreference::Null:
        return {{{RenderBackendKind::Null, RenderBackendKind::OpenGL, RenderBackendKind::Vulkan}}, 1U};
    case RenderBackendPreference::Auto:
    case RenderBackendPreference::OpenGL:
    default:
        return {{{RenderBackendKind::OpenGL, RenderBackendKind::Null, RenderBackendKind::Vulkan}}, 2U};
    }
}

} // namespace UVE::Core
