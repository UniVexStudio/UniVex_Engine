// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "uve/input/input_binding_uve.h"

namespace UVE::Input {

// Stable names for every key, button, axis and device a binding can name - what an input map file
// stores and what a person reads, so a file says "Space" rather than 36. Each Get returns an empty
// view for a sentinel or out-of-range value; each Parse returns nothing for a name it does not know.

[[nodiscard]] std::string_view GetKeyNameUVE(KeyCodeUVE key) noexcept;
[[nodiscard]] std::optional<KeyCodeUVE> ParseKeyNameUVE(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetMouseButtonNameUVE(MouseButtonUVE button) noexcept;
[[nodiscard]] std::optional<MouseButtonUVE> ParseMouseButtonNameUVE(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetGamepadButtonNameUVE(GamepadButtonUVE button) noexcept;
[[nodiscard]] std::optional<GamepadButtonUVE> ParseGamepadButtonNameUVE(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetGamepadAxisNameUVE(GamepadAxisUVE axis) noexcept;
[[nodiscard]] std::optional<GamepadAxisUVE> ParseGamepadAxisNameUVE(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetInputBindingSourceNameUVE(InputBindingSourceUVE source) noexcept;
[[nodiscard]] std::optional<InputBindingSourceUVE> ParseInputBindingSourceNameUVE(std::string_view name) noexcept;

/// A binding as a person reads it: "Space", "Mouse Left", "Pad 1 South", "Pad 2 LeftX -".
[[nodiscard]] std::string DescribeInputBindingUVE(const InputBindingUVE& binding);

/// Whether two bindings name the same physical input, whatever their scale - the test for two
/// actions fighting over one key.
[[nodiscard]] bool IsSameInputUVE(const InputBindingUVE& lhs, const InputBindingUVE& rhs) noexcept;

} // namespace UVE::Input
