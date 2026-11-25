// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Window {

enum class CursorInputMode : std::uint8_t {
    Normal,
    Hidden,
    Confined,
};

struct CursorInputResolution final {
    CursorInputMode mode = CursorInputMode::Normal;
    bool useCustomCursor = true;
    bool visibilityCompromised = false;

    [[nodiscard]] constexpr bool operator==(const CursorInputResolution&) const noexcept = default;
};

/// Resolves portable cursor visibility/confinement preferences to GLFW's cursor modes. GLFW's
/// portable disabled mode captures and hides the cursor, so it cannot satisfy visible+confined.
[[nodiscard]] constexpr CursorInputResolution ResolveCursorInputPolicy(
    const bool visible, const bool confined) noexcept {
    if (confined) {
        return {CursorInputMode::Confined, false, visible};
    }
    if (!visible) {
        return {CursorInputMode::Hidden, false, false};
    }
    return {CursorInputMode::Normal, true, false};
}

} // namespace UVE::Window
