// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include "uve/input/key_code_uve.h"
#include "uve/input/mouse_button_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::UI::Prism {

/// Whether an event was consumed by a widget or allowed to bubble to ancestors.
enum class PrismEventResultUVE : std::uint8_t {
    Unhandled, // Continue bubbling up the widget tree
    Handled,   // Consumed; stop propagation immediately
};

/// Bitflags for keyboard modifier keys.
enum class PrismKeyModifiersUVE : std::uint8_t {
    None    = 0,
    Shift   = 1 << 0,
    Control = 1 << 1,
    Alt     = 1 << 2,
};

[[nodiscard]] constexpr PrismKeyModifiersUVE operator|(PrismKeyModifiersUVE lhs, PrismKeyModifiersUVE rhs) noexcept {
    return static_cast<PrismKeyModifiersUVE>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr bool HasModifierUVE(PrismKeyModifiersUVE state, PrismKeyModifiersUVE flag) noexcept {
    return (static_cast<std::uint8_t>(state) & static_cast<std::uint8_t>(flag)) != 0;
}

/// Pointer (mouse/touch/stylus) event data.
struct PrismPointerEventUVE final {
    Math::Vector2UVE      position{0.0F, 0.0F};
    Math::Vector2UVE      delta{0.0F, 0.0F};
    Input::MouseButtonUVE button = Input::MouseButtonUVE::Left;
    PrismKeyModifiersUVE  modifiers = PrismKeyModifiersUVE::None;

    [[nodiscard]] constexpr bool operator==(const PrismPointerEventUVE&) const noexcept = default;
};

/// Mouse wheel / trackpad scroll event data.
struct PrismScrollEventUVE final {
    Math::Vector2UVE      position{0.0F, 0.0F};
    Math::Vector2UVE      wheelDelta{0.0F, 0.0F};
    PrismKeyModifiersUVE  modifiers = PrismKeyModifiersUVE::None;

    [[nodiscard]] constexpr bool operator==(const PrismScrollEventUVE&) const noexcept = default;
};

/// Keyboard key event data.
struct PrismKeyEventUVE final {
    Input::KeyCodeUVE    keyCode = Input::KeyCodeUVE::Unknown;
    PrismKeyModifiersUVE modifiers = PrismKeyModifiersUVE::None;
    bool                 isRepeat = false;

    [[nodiscard]] constexpr bool operator==(const PrismKeyEventUVE&) const noexcept = default;
};

/// Drag and drop event payload data.
struct PrismDragDropEventUVE final {
    Math::Vector2UVE position{0.0F, 0.0F};
    std::string      payloadType;
    std::string      payloadData;
    bool             accepted = false;

    [[nodiscard]] bool operator==(const PrismDragDropEventUVE&) const noexcept = default;
};

} // namespace UVE::UI::Prism
