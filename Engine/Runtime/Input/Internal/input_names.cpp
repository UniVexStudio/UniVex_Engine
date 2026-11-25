// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "input_names.h"

#include <array>
#include <cstddef>

namespace UVE::Input {
namespace {

// Indexed by enum value; the order is the enums' own.
constexpr std::array<std::string_view, static_cast<std::size_t>(KeyCode::Count)> kKeyNames{
    "",          "A",          "B",        "C",         "D",         "E",        "F",       "G",
    "H",         "I",          "J",        "K",         "L",         "M",        "N",       "O",
    "P",         "Q",          "R",        "S",         "T",         "U",        "V",       "W",
    "X",         "Y",          "Z",        "0",         "1",         "2",        "3",       "4",
    "5",         "6",          "7",        "8",         "9",         "Up",       "Down",    "Left",
    "Right",     "Space",      "Enter",    "Escape",    "Tab",       "Backspace", "LeftShift", "RightShift",
    "LeftCtrl",  "RightCtrl",  "LeftAlt",  "RightAlt",  "F1",        "F2",       "F3",      "F4",
    "F5",        "F6",         "F7",       "F8",        "F9",        "F10",      "F11",     "F12",
};
constexpr std::array<std::string_view, static_cast<std::size_t>(MouseButton::Count)> kMouseButtonNames{
    "Left", "Right", "Middle"};
constexpr std::array<std::string_view, static_cast<std::size_t>(GamepadButton::Count)> kGamepadButtonNames{
    "South",     "East",       "West",  "North",     "LeftBumper", "RightBumper", "Back",      "Start",
    "Guide",     "LeftStick",  "RightStick", "DPadUp", "DPadDown", "DPadLeft",    "DPadRight", "Miscellaneous"};
constexpr std::array<std::string_view, static_cast<std::size_t>(GamepadAxis::Count)> kGamepadAxisNames{
    "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger"};
constexpr std::array<std::string_view, 4> kSourceNames{"Keyboard", "Mouse", "GamepadButton", "GamepadAxis"};

// The tables are positional, so pin the names at the ends and at a few points in between: an enum
// value added without its name fails here instead of shifting every name after it.
static_assert(kKeyNames[static_cast<std::size_t>(KeyCode::Z)] == "Z");
static_assert(kKeyNames[static_cast<std::size_t>(KeyCode::Num9)] == "9");
static_assert(kKeyNames[static_cast<std::size_t>(KeyCode::Space)] == "Space");
static_assert(kKeyNames[static_cast<std::size_t>(KeyCode::RightAlt)] == "RightAlt");
static_assert(kKeyNames[static_cast<std::size_t>(KeyCode::F12)] == "F12");
static_assert(kMouseButtonNames[static_cast<std::size_t>(MouseButton::Middle)] == "Middle");
static_assert(kGamepadButtonNames[static_cast<std::size_t>(GamepadButton::Miscellaneous)] == "Miscellaneous");
static_assert(kGamepadAxisNames[static_cast<std::size_t>(GamepadAxis::RightTrigger)] == "RightTrigger");
static_assert(kSourceNames[static_cast<std::size_t>(InputBindingSource::GamepadAxis)] == "GamepadAxis");

template <typename Enum, std::size_t Count>
[[nodiscard]] std::string_view NameOf(const std::array<std::string_view, Count>& names, const Enum value) noexcept {
    const auto index = static_cast<std::size_t>(value);
    return index < Count ? names[index] : std::string_view{};
}

template <typename Enum, std::size_t Count>
[[nodiscard]] std::optional<Enum> Parse(const std::array<std::string_view, Count>& names,
                                           const std::string_view name) noexcept {
    for (std::size_t index = 0U; index < Count; ++index) {
        if (!names[index].empty() && names[index] == name) {
            return static_cast<Enum>(index);
        }
    }
    return std::nullopt;
}

} // namespace

std::string_view GetKeyName(const KeyCode key) noexcept {
    return NameOf(kKeyNames, key);
}
std::optional<KeyCode> ParseKeyName(const std::string_view name) noexcept {
    return Parse<KeyCode>(kKeyNames, name);
}
std::string_view GetMouseButtonName(const MouseButton button) noexcept {
    return NameOf(kMouseButtonNames, button);
}
std::optional<MouseButton> ParseMouseButtonName(const std::string_view name) noexcept {
    return Parse<MouseButton>(kMouseButtonNames, name);
}
std::string_view GetGamepadButtonName(const GamepadButton button) noexcept {
    return NameOf(kGamepadButtonNames, button);
}
std::optional<GamepadButton> ParseGamepadButtonName(const std::string_view name) noexcept {
    return Parse<GamepadButton>(kGamepadButtonNames, name);
}
std::string_view GetGamepadAxisName(const GamepadAxis axis) noexcept {
    return NameOf(kGamepadAxisNames, axis);
}
std::optional<GamepadAxis> ParseGamepadAxisName(const std::string_view name) noexcept {
    return Parse<GamepadAxis>(kGamepadAxisNames, name);
}
std::string_view GetInputBindingSourceName(const InputBindingSource source) noexcept {
    return NameOf(kSourceNames, source);
}
std::optional<InputBindingSource> ParseInputBindingSourceName(const std::string_view name) noexcept {
    return Parse<InputBindingSource>(kSourceNames, name);
}

std::string DescribeInputBinding(const InputBinding& binding) {
    const std::string pad = "Pad " + std::to_string(binding.gamepadIndex + 1U) + " ";
    switch (binding.source) {
    case InputBindingSource::Keyboard:
        return std::string(GetKeyName(binding.key));
    case InputBindingSource::Mouse:
        return "Mouse " + std::string(GetMouseButtonName(binding.mouseButton));
    case InputBindingSource::GamepadButton:
        return pad + std::string(GetGamepadButtonName(binding.gamepadButton));
    case InputBindingSource::GamepadAxis:
        return pad + std::string(GetGamepadAxisName(binding.gamepadAxis)) + (binding.scale < 0.0F ? " -" : " +");
    }
    return {};
}

bool IsSameInput(const InputBinding& lhs, const InputBinding& rhs) noexcept {
    if (lhs.source != rhs.source) {
        return false;
    }
    switch (lhs.source) {
    case InputBindingSource::Keyboard:
        return lhs.key == rhs.key;
    case InputBindingSource::Mouse:
        return lhs.mouseButton == rhs.mouseButton;
    case InputBindingSource::GamepadButton:
        return lhs.gamepadIndex == rhs.gamepadIndex && lhs.gamepadButton == rhs.gamepadButton;
    case InputBindingSource::GamepadAxis:
        // One stick direction each: +X and -X of a stick are different inputs.
        return lhs.gamepadIndex == rhs.gamepadIndex && lhs.gamepadAxis == rhs.gamepadAxis &&
               (lhs.scale < 0.0F) == (rhs.scale < 0.0F);
    }
    return false;
}

} // namespace UVE::Input
