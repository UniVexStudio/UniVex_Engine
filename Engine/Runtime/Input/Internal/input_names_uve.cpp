// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/input/input_names_uve.h"

#include <array>
#include <cstddef>

namespace UVE::Input {
namespace {

// Indexed by enum value; the order is the enums' own.
constexpr std::array<std::string_view, static_cast<std::size_t>(KeyCodeUVE::Count)> kKeyNamesUVE{
    "",          "A",          "B",        "C",         "D",         "E",        "F",       "G",
    "H",         "I",          "J",        "K",         "L",         "M",        "N",       "O",
    "P",         "Q",          "R",        "S",         "T",         "U",        "V",       "W",
    "X",         "Y",          "Z",        "0",         "1",         "2",        "3",       "4",
    "5",         "6",          "7",        "8",         "9",         "Up",       "Down",    "Left",
    "Right",     "Space",      "Enter",    "Escape",    "Tab",       "Backspace", "LeftShift", "RightShift",
    "LeftCtrl",  "RightCtrl",  "LeftAlt",  "RightAlt",  "F1",        "F2",       "F3",      "F4",
    "F5",        "F6",         "F7",       "F8",        "F9",        "F10",      "F11",     "F12",
};
constexpr std::array<std::string_view, static_cast<std::size_t>(MouseButtonUVE::Count)> kMouseButtonNamesUVE{
    "Left", "Right", "Middle"};
constexpr std::array<std::string_view, static_cast<std::size_t>(GamepadButtonUVE::Count)> kGamepadButtonNamesUVE{
    "South",     "East",       "West",  "North",     "LeftBumper", "RightBumper", "Back",      "Start",
    "Guide",     "LeftStick",  "RightStick", "DPadUp", "DPadDown", "DPadLeft",    "DPadRight", "Miscellaneous"};
constexpr std::array<std::string_view, static_cast<std::size_t>(GamepadAxisUVE::Count)> kGamepadAxisNamesUVE{
    "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger"};
constexpr std::array<std::string_view, 4> kSourceNamesUVE{"Keyboard", "Mouse", "GamepadButton", "GamepadAxis"};

// The tables are positional, so pin the names at the ends and at a few points in between: an enum
// value added without its name fails here instead of shifting every name after it.
static_assert(kKeyNamesUVE[static_cast<std::size_t>(KeyCodeUVE::Z)] == "Z");
static_assert(kKeyNamesUVE[static_cast<std::size_t>(KeyCodeUVE::Num9)] == "9");
static_assert(kKeyNamesUVE[static_cast<std::size_t>(KeyCodeUVE::Space)] == "Space");
static_assert(kKeyNamesUVE[static_cast<std::size_t>(KeyCodeUVE::RightAlt)] == "RightAlt");
static_assert(kKeyNamesUVE[static_cast<std::size_t>(KeyCodeUVE::F12)] == "F12");
static_assert(kMouseButtonNamesUVE[static_cast<std::size_t>(MouseButtonUVE::Middle)] == "Middle");
static_assert(kGamepadButtonNamesUVE[static_cast<std::size_t>(GamepadButtonUVE::Miscellaneous)] == "Miscellaneous");
static_assert(kGamepadAxisNamesUVE[static_cast<std::size_t>(GamepadAxisUVE::RightTrigger)] == "RightTrigger");
static_assert(kSourceNamesUVE[static_cast<std::size_t>(InputBindingSourceUVE::GamepadAxis)] == "GamepadAxis");

template <typename Enum, std::size_t Count>
[[nodiscard]] std::string_view NameOfUVE(const std::array<std::string_view, Count>& names, const Enum value) noexcept {
    const auto index = static_cast<std::size_t>(value);
    return index < Count ? names[index] : std::string_view{};
}

template <typename Enum, std::size_t Count>
[[nodiscard]] std::optional<Enum> ParseUVE(const std::array<std::string_view, Count>& names,
                                           const std::string_view name) noexcept {
    for (std::size_t index = 0U; index < Count; ++index) {
        if (!names[index].empty() && names[index] == name) {
            return static_cast<Enum>(index);
        }
    }
    return std::nullopt;
}

} // namespace

std::string_view GetKeyNameUVE(const KeyCodeUVE key) noexcept {
    return NameOfUVE(kKeyNamesUVE, key);
}
std::optional<KeyCodeUVE> ParseKeyNameUVE(const std::string_view name) noexcept {
    return ParseUVE<KeyCodeUVE>(kKeyNamesUVE, name);
}
std::string_view GetMouseButtonNameUVE(const MouseButtonUVE button) noexcept {
    return NameOfUVE(kMouseButtonNamesUVE, button);
}
std::optional<MouseButtonUVE> ParseMouseButtonNameUVE(const std::string_view name) noexcept {
    return ParseUVE<MouseButtonUVE>(kMouseButtonNamesUVE, name);
}
std::string_view GetGamepadButtonNameUVE(const GamepadButtonUVE button) noexcept {
    return NameOfUVE(kGamepadButtonNamesUVE, button);
}
std::optional<GamepadButtonUVE> ParseGamepadButtonNameUVE(const std::string_view name) noexcept {
    return ParseUVE<GamepadButtonUVE>(kGamepadButtonNamesUVE, name);
}
std::string_view GetGamepadAxisNameUVE(const GamepadAxisUVE axis) noexcept {
    return NameOfUVE(kGamepadAxisNamesUVE, axis);
}
std::optional<GamepadAxisUVE> ParseGamepadAxisNameUVE(const std::string_view name) noexcept {
    return ParseUVE<GamepadAxisUVE>(kGamepadAxisNamesUVE, name);
}
std::string_view GetInputBindingSourceNameUVE(const InputBindingSourceUVE source) noexcept {
    return NameOfUVE(kSourceNamesUVE, source);
}
std::optional<InputBindingSourceUVE> ParseInputBindingSourceNameUVE(const std::string_view name) noexcept {
    return ParseUVE<InputBindingSourceUVE>(kSourceNamesUVE, name);
}

std::string DescribeInputBindingUVE(const InputBindingUVE& binding) {
    const std::string pad = "Pad " + std::to_string(binding.gamepadIndex + 1U) + " ";
    switch (binding.source) {
    case InputBindingSourceUVE::Keyboard:
        return std::string(GetKeyNameUVE(binding.key));
    case InputBindingSourceUVE::Mouse:
        return "Mouse " + std::string(GetMouseButtonNameUVE(binding.mouseButton));
    case InputBindingSourceUVE::GamepadButton:
        return pad + std::string(GetGamepadButtonNameUVE(binding.gamepadButton));
    case InputBindingSourceUVE::GamepadAxis:
        return pad + std::string(GetGamepadAxisNameUVE(binding.gamepadAxis)) + (binding.scale < 0.0F ? " -" : " +");
    }
    return {};
}

bool IsSameInputUVE(const InputBindingUVE& lhs, const InputBindingUVE& rhs) noexcept {
    if (lhs.source != rhs.source) {
        return false;
    }
    switch (lhs.source) {
    case InputBindingSourceUVE::Keyboard:
        return lhs.key == rhs.key;
    case InputBindingSourceUVE::Mouse:
        return lhs.mouseButton == rhs.mouseButton;
    case InputBindingSourceUVE::GamepadButton:
        return lhs.gamepadIndex == rhs.gamepadIndex && lhs.gamepadButton == rhs.gamepadButton;
    case InputBindingSourceUVE::GamepadAxis:
        // One stick direction each: +X and -X of a stick are different inputs.
        return lhs.gamepadIndex == rhs.gamepadIndex && lhs.gamepadAxis == rhs.gamepadAxis &&
               (lhs.scale < 0.0F) == (rhs.scale < 0.0F);
    }
    return false;
}

} // namespace UVE::Input
