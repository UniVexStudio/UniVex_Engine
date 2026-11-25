// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>

#include "i_gamepad_input_system.h"
#include "key_code.h"
#include "mouse_button.h"

namespace UVE::Input {

/// Which physical input device family an InputBinding reads from.
enum class InputBindingSource {
    Keyboard,
    Mouse,
    GamepadButton,
    GamepadAxis,
};

/// One physical input bound into an InputAction's positive or negative binding list. Only
/// `key` is meaningful when `source == Keyboard`; only `mouseButton` is meaningful when
/// `source == Mouse`; `gamepadIndex`/`gamepadButton` are meaningful for GamepadButton; and
/// `gamepadIndex`/`gamepadAxis`/`scale` are meaningful for GamepadAxis — construct via the helper
/// functions rather than setting unused fields directly.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct InputBinding {
    InputBindingSource source = InputBindingSource::Keyboard;
    KeyCode key = KeyCode::Unknown;
    MouseButton mouseButton = MouseButton::Left;
    std::size_t gamepadIndex = 0U;
    GamepadButton gamepadButton = GamepadButton::South;
    GamepadAxis gamepadAxis = GamepadAxis::LeftX;
    float scale = 1.0F;
};

/// Builds a keyboard InputBinding.
[[nodiscard]] constexpr InputBinding KeyBinding(KeyCode key) noexcept {
    return InputBinding{InputBindingSource::Keyboard, key, MouseButton::Left, 0U,
                          GamepadButton::South, GamepadAxis::LeftX, 1.0F};
}

/// Builds a mouse InputBinding.
[[nodiscard]] constexpr InputBinding MouseBinding(MouseButton button) noexcept {
    return InputBinding{InputBindingSource::Mouse, KeyCode::Unknown, button, 0U,
                          GamepadButton::South, GamepadAxis::LeftX, 1.0F};
}

[[nodiscard]] constexpr InputBinding GamepadButtonBinding(
    std::size_t gamepadIndex, GamepadButton button) noexcept {
    return InputBinding{InputBindingSource::GamepadButton, KeyCode::Unknown, MouseButton::Left,
                           gamepadIndex, button, GamepadAxis::LeftX, 1.0F};
}

[[nodiscard]] constexpr InputBinding GamepadAxisBinding(
    std::size_t gamepadIndex, GamepadAxis axis, float scale = 1.0F) noexcept {
    return InputBinding{InputBindingSource::GamepadAxis, KeyCode::Unknown, MouseButton::Left,
                           gamepadIndex, GamepadButton::South, axis, scale};
}

} // namespace UVE::Input
