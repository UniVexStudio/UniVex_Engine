// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "input_binding.h"

namespace UVE::Input {

// Stable names for every key, button, axis and device a binding can name - what an input map file
// stores and what a person reads, so a file says "Space" rather than 36. Each Get returns an empty
// view for a sentinel or out-of-range value; each Parse returns nothing for a name it does not know.

[[nodiscard]] std::string_view GetKeyName(KeyCode key) noexcept;
[[nodiscard]] std::optional<KeyCode> ParseKeyName(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetMouseButtonName(MouseButton button) noexcept;
[[nodiscard]] std::optional<MouseButton> ParseMouseButtonName(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetGamepadButtonName(GamepadButton button) noexcept;
[[nodiscard]] std::optional<GamepadButton> ParseGamepadButtonName(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetGamepadAxisName(GamepadAxis axis) noexcept;
[[nodiscard]] std::optional<GamepadAxis> ParseGamepadAxisName(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetInputBindingSourceName(InputBindingSource source) noexcept;
[[nodiscard]] std::optional<InputBindingSource> ParseInputBindingSourceName(std::string_view name) noexcept;

/// A binding as a person reads it: "Space", "Mouse Left", "Pad 1 South", "Pad 2 LeftX -".
[[nodiscard]] std::string DescribeInputBinding(const InputBinding& binding);

/// Whether two bindings name the same physical input, whatever their scale - the test for two
/// actions fighting over one key.
[[nodiscard]] bool IsSameInput(const InputBinding& lhs, const InputBinding& rhs) noexcept;

} // namespace UVE::Input
