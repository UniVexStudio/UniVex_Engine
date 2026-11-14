// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "vector2.h"

#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace UVE::Editor {

/// One frame of pointer/keyboard input for a viewport image, captured by whichever UI layer hosts
/// the image (the editor shell) and handed to the viewport renderer through
/// EditorUVE::ViewportOverlayStateUVE::input.
///
/// The viewport backends used to read the UI toolkit's global IO directly. They now read only this
/// struct, so the renderer is independent of the UI layer that draws the panels around it: the
/// shell fills it once per frame for the view it is about to render, and applies
/// `requestedCursor` afterwards.
///
/// Semantics follow the conventions the backends were written against:
///   * `clicked` / `released` are edges of this frame; `down` is the current state.
///   * `DragDeltaUVE(b)` is the pointer offset from where `b` was pressed (valid while held and on
///     the release frame); `DragDistanceUVE(b)` is the largest such offset seen during the press.
///   * `keysPressed` are press edges without key repeat.
struct EditorViewportInputUVE final {
    enum class ButtonUVE : std::uint8_t { Left = 0, Right, Middle, Count };

    enum class KeyUVE : std::uint8_t {
        Num0 = 0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
        Keypad1, Keypad3, Keypad5, Keypad7,
        A, C, D, F, S, W,
        Space, Home, Escape, LeftShift, LeftCtrl,
        Count
    };

    enum class CursorUVE : std::uint8_t { Arrow = 0, Hand, ResizeAll, ResizeNS, ResizeEW, ResizeNWSE, ResizeNESW };

    static constexpr std::size_t kButtonCountUVE = static_cast<std::size_t>(ButtonUVE::Count);
    static constexpr std::size_t kKeyCountUVE = static_cast<std::size_t>(KeyUVE::Count);

    /// The pointer is over the view's panel/window and nothing in the UI above it claims it.
    bool hovered = false;
    /// Pointer position and the top-left of the view image, both in the same window space.
    Math::Vector2UVE mousePos{0.0F, 0.0F};
    Math::Vector2UVE imageOrigin{0.0F, 0.0F};
    Math::Vector2UVE mouseDelta{0.0F, 0.0F};
    float wheel = 0.0F;
    float deltaTime = 1.0F / 60.0F;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    /// A text field owns the keyboard: hotkeys must not fire.
    bool wantTextInput = false;

    std::array<bool, kButtonCountUVE> down{};
    std::array<bool, kButtonCountUVE> clicked{};
    std::array<bool, kButtonCountUVE> released{};
    std::array<Math::Vector2UVE, kButtonCountUVE> pressPos{};
    std::array<float, kButtonCountUVE> dragDistance{};

    std::bitset<kKeyCountUVE> keysDown;
    std::bitset<kKeyCountUVE> keysPressed;

    /// Output: the cursor the view wants this frame. The shell applies it after rendering.
    mutable CursorUVE requestedCursor = CursorUVE::Arrow;
    mutable bool cursorRequested = false;

    [[nodiscard]] static constexpr std::size_t IndexUVE(const ButtonUVE b) noexcept {
        return static_cast<std::size_t>(b);
    }
    [[nodiscard]] bool IsDownUVE(const ButtonUVE b) const noexcept { return down[IndexUVE(b)]; }
    [[nodiscard]] bool IsClickedUVE(const ButtonUVE b) const noexcept { return clicked[IndexUVE(b)]; }
    [[nodiscard]] bool IsReleasedUVE(const ButtonUVE b) const noexcept { return released[IndexUVE(b)]; }
    /// Held and moved at least `threshold` pixels since the press (0 = any time it is held).
    [[nodiscard]] bool IsDraggingUVE(const ButtonUVE b, const float threshold) const noexcept {
        return down[IndexUVE(b)] && dragDistance[IndexUVE(b)] >= threshold;
    }
    [[nodiscard]] Math::Vector2UVE DragDeltaUVE(const ButtonUVE b) const noexcept {
        const std::size_t i = IndexUVE(b);
        if (!down[i] && !released[i]) {
            return Math::Vector2UVE{0.0F, 0.0F};
        }
        return Math::Vector2UVE{mousePos.x - pressPos[i].x, mousePos.y - pressPos[i].y};
    }
    [[nodiscard]] bool IsKeyDownUVE(const KeyUVE k) const noexcept {
        return keysDown.test(static_cast<std::size_t>(k));
    }
    [[nodiscard]] bool IsKeyPressedUVE(const KeyUVE k) const noexcept {
        return keysPressed.test(static_cast<std::size_t>(k));
    }
    /// Pointer position relative to the view image's top-left.
    [[nodiscard]] Math::Vector2UVE LocalMouseUVE() const noexcept {
        return Math::Vector2UVE{mousePos.x - imageOrigin.x, mousePos.y - imageOrigin.y};
    }
    void RequestCursorUVE(const CursorUVE cursor) const noexcept {
        requestedCursor = cursor;
        cursorRequested = true;
    }
};

} // namespace UVE::Editor
