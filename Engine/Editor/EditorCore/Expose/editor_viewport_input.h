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
/// Editor::ViewportOverlayState::input.
///
/// The viewport backends used to read the UI toolkit's global IO directly. They now read only this
/// struct, so the renderer is independent of the UI layer that draws the panels around it: the
/// shell fills it once per frame for the view it is about to render, and applies
/// `requestedCursor` afterwards.
///
/// Semantics follow the conventions the backends were written against:
///   * `clicked` / `released` are edges of this frame; `down` is the current state.
///   * `DragDelta(b)` is the pointer offset from where `b` was pressed (valid while held and on
///     the release frame); `DragDistance(b)` is the largest such offset seen during the press.
///   * `keysPressed` are press edges without key repeat.
struct EditorViewportInput final {
    enum class Button : std::uint8_t { Left = 0, Right, Middle, Count };

    enum class Key : std::uint8_t {
        Num0 = 0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
        Keypad1, Keypad3, Keypad5, Keypad7,
        A, C, D, F, S, W, E, Q,
        Space, Home, Escape, LeftShift, LeftCtrl,
        Count
    };

    enum class Cursor : std::uint8_t { Arrow = 0, Hand, ResizeAll, ResizeNS, ResizeEW, ResizeNWSE, ResizeNESW };

    static constexpr std::size_t kButtonCount = static_cast<std::size_t>(Button::Count);
    static constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Count);

    /// The pointer is over the view's panel/window and nothing in the UI above it claims it.
    bool hovered = false;
    /// Pointer position and the top-left of the view image, both in the same window space.
    Math::Vector2 mousePos{0.0F, 0.0F};
    Math::Vector2 imageOrigin{0.0F, 0.0F};
    Math::Vector2 mouseDelta{0.0F, 0.0F};
    float wheel = 0.0F;
    float deltaTime = 1.0F / 60.0F;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    /// A text field owns the keyboard: hotkeys must not fire.
    bool wantTextInput = false;
    /// Single-surface touch host (Android): enables direct 1-finger orbit + 2-finger pan/pinch.
    bool touchNavigation = false;
    /// Display pixel scale applied to viewport texture dimensions and pointer coordinates.
    float dpiScale = 1.0F;

    std::array<bool, kButtonCount> down{};
    std::array<bool, kButtonCount> clicked{};
    std::array<bool, kButtonCount> released{};
    std::array<Math::Vector2, kButtonCount> pressPos{};
    std::array<float, kButtonCount> dragDistance{};

    std::bitset<kKeyCount> keysDown;
    std::bitset<kKeyCount> keysPressed;

    /// Output: the cursor the view wants this frame. The shell applies it after rendering.
    mutable Cursor requestedCursor = Cursor::Arrow;
    mutable bool cursorRequested = false;

    [[nodiscard]] static constexpr std::size_t Index(const Button b) noexcept {
        return static_cast<std::size_t>(b);
    }
    [[nodiscard]] bool IsDown(const Button b) const noexcept { return down[Index(b)]; }
    [[nodiscard]] bool IsClicked(const Button b) const noexcept { return clicked[Index(b)]; }
    [[nodiscard]] bool IsReleased(const Button b) const noexcept { return released[Index(b)]; }
    /// Held and moved at least `threshold` pixels since the press (0 = any time it is held).
    [[nodiscard]] bool IsDragging(const Button b, const float threshold) const noexcept {
        return down[Index(b)] && dragDistance[Index(b)] >= threshold;
    }
    [[nodiscard]] Math::Vector2 DragDelta(const Button b) const noexcept {
        const std::size_t i = Index(b);
        if (!down[i] && !released[i]) {
            return Math::Vector2{0.0F, 0.0F};
        }
        return Math::Vector2{mousePos.x - pressPos[i].x, mousePos.y - pressPos[i].y};
    }
    [[nodiscard]] bool IsKeyDown(const Key k) const noexcept {
        return keysDown.test(static_cast<std::size_t>(k));
    }
    [[nodiscard]] bool IsKeyPressed(const Key k) const noexcept {
        return keysPressed.test(static_cast<std::size_t>(k));
    }
    /// Pointer position relative to the view image's top-left.
    [[nodiscard]] Math::Vector2 LocalMouse() const noexcept {
        return Math::Vector2{mousePos.x - imageOrigin.x, mousePos.y - imageOrigin.y};
    }
    void RequestCursor(const Cursor cursor) const noexcept {
        requestedCursor = cursor;
        cursorRequested = true;
    }
};

} // namespace UVE::Editor
