// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_input_map.h"

#include <algorithm>
#include <cmath>

#include "input_names.h"

namespace UVE::Editor {
namespace {

/// How far a stick or trigger must travel, from rest, to count as chosen.
constexpr float kAxisCaptureThreshold = 0.5F;

[[nodiscard]] bool HasActionNamed(const std::vector<Input::InputAction>& actions, const std::string_view name) {
    return std::any_of(actions.begin(), actions.end(),
                       [name](const Input::InputAction& action) { return action.name == name; });
}

} // namespace

std::optional<Input::InputBinding> CaptureInputBinding(const Input::IInputSystem& input,
                                                             const Input::IGamepadInputSystem& gamepads,
                                                             const bool includeMouse) {
    for (int value = static_cast<int>(Input::KeyCode::A); value < static_cast<int>(Input::KeyCode::Count);
         ++value) {
        const auto key = static_cast<Input::KeyCode>(value);
        if (key != Input::KeyCode::Escape && input.WasKeyPressedThisFrame(key)) {
            return Input::KeyBinding(key);
        }
    }
    if (includeMouse) {
        for (int value = 0; value < static_cast<int>(Input::MouseButton::Count); ++value) {
            const auto button = static_cast<Input::MouseButton>(value);
            if (input.WasMouseButtonPressedThisFrame(button)) {
                return Input::MouseBinding(button);
            }
        }
    }
    for (std::size_t pad = 0U; pad < Input::kMaximumGamepadCount; ++pad) {
        for (int value = 0; value < static_cast<int>(Input::GamepadButton::Count); ++value) {
            const auto button = static_cast<Input::GamepadButton>(value);
            if (gamepads.WasButtonPressedThisFrame(pad, button)) {
                return Input::GamepadButtonBinding(pad, button);
            }
        }
        const Input::GamepadStateSnapshot previous = gamepads.GetPreviousSnapshot(pad);
        const Input::GamepadStateSnapshot current = gamepads.GetSnapshot(pad);
        for (std::size_t axis = 0U; axis < current.axes.size(); ++axis) {
            const float now = current.axes[axis];
            // Crossing the threshold this frame, not merely resting past it, so a stick left
            // leaning when listening starts does not bind itself.
            if (std::abs(now) >= kAxisCaptureThreshold && std::abs(previous.axes[axis]) < kAxisCaptureThreshold) {
                return Input::GamepadAxisBinding(pad, static_cast<Input::GamepadAxis>(axis),
                                                    now < 0.0F ? -1.0F : 1.0F);
            }
        }
    }
    return std::nullopt;
}

std::vector<std::string> FindInputConflicts(const std::vector<Input::InputAction>& actions,
                                               const std::size_t actionIndex, const Input::InputBinding& binding) {
    std::vector<std::string> conflicts;
    for (std::size_t index = 0U; index < actions.size(); ++index) {
        if (index == actionIndex) {
            continue;
        }
        const Input::InputAction& action = actions[index];
        const auto uses = [&binding](const std::vector<Input::InputBinding>& bindings) {
            return std::any_of(bindings.begin(), bindings.end(), [&binding](const Input::InputBinding& other) {
                return Input::IsSameInput(binding, other);
            });
        };
        if (uses(action.positiveBindings) || uses(action.negativeBindings)) {
            conflicts.push_back(action.name);
        }
    }
    return conflicts;
}

std::string MakeUniqueActionName(const std::vector<Input::InputAction>& actions, const std::string_view base) {
    std::string name(base);
    for (int suffix = 2; HasActionNamed(actions, name); ++suffix) {
        name = std::string(base) + "_" + std::to_string(suffix);
    }
    return name;
}

bool ResetActionToProjectDefault(std::vector<Input::InputAction>& actions,
                                    const std::vector<Input::InputAction>& projectDefaults,
                                    const std::string_view actionName) {
    const auto defaultIt = std::find_if(projectDefaults.begin(), projectDefaults.end(),
                                        [actionName](const Input::InputAction& candidate) {
                                            return candidate.name == actionName;
                                        });
    if (defaultIt == projectDefaults.end()) {
        return false;
    }
    const auto targetIt = std::find_if(actions.begin(), actions.end(),
                                       [actionName](const Input::InputAction& candidate) {
                                           return candidate.name == actionName;
                                       });
    if (targetIt != actions.end()) {
        *targetIt = *defaultIt;
    } else {
        actions.push_back(*defaultIt);
    }
    return true;
}

void ResetInputMapToProjectDefaults(std::vector<Input::InputAction>& actions,
                                       const std::vector<Input::InputAction>& projectDefaults) {
    actions = projectDefaults;
}

bool ApplyRuntimeActionRebind(Input::IInputSystem& input, const std::string_view actionName,
                                 const Input::InputBinding& newBinding) {
    if (actionName.empty()) {
        return false;
    }
    return input.RemapAction(actionName, {newBinding}, {});
}

std::vector<Input::InputAction> FilterActionsBySetPrefix(
    const std::vector<Input::InputAction>& actions, const std::string_view actionSetPrefix) {
    std::vector<Input::InputAction> filtered;
    const std::string dotPrefix = std::string(actionSetPrefix) + ".";
    for (const Input::InputAction& action : actions) {
        if (actionSetPrefix.empty() || action.name.starts_with(dotPrefix) ||
            (actionSetPrefix == "gameplay" && action.name.find('.') == std::string::npos)) {
            filtered.push_back(action);
        }
    }
    return filtered;
}

} // namespace UVE::Editor
