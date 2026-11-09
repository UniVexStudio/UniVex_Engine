// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Window {

enum class CursorInputModeUVE : std::uint8_t {
    Normal,
    Hidden,
    Confined,
};

struct CursorInputResolutionUVE final {
    CursorInputModeUVE mode = CursorInputModeUVE::Normal;
    bool useCustomCursor = true;
    bool visibilityCompromised = false;

    [[nodiscard]] constexpr bool operator==(const CursorInputResolutionUVE&) const noexcept = default;
};

/// Resolves portable cursor visibility/confinement preferences to GLFW's cursor modes. GLFW's
/// portable disabled mode captures and hides the cursor, so it cannot satisfy visible+confined.
[[nodiscard]] constexpr CursorInputResolutionUVE ResolveCursorInputPolicyUVE(
    const bool visible, const bool confined) noexcept {
    if (confined) {
        return {CursorInputModeUVE::Confined, false, visible};
    }
    if (!visible) {
        return {CursorInputModeUVE::Hidden, false, false};
    }
    return {CursorInputModeUVE::Normal, true, false};
}

} // namespace UVE::Window
