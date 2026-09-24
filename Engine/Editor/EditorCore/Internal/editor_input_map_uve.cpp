// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_input_map_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/input/input_names_uve.h"

namespace UVE::Editor {
namespace {

/// How far a stick or trigger must travel, from rest, to count as chosen.
constexpr float kAxisCaptureThresholdUVE = 0.5F;

[[nodiscard]] bool HasActionNamedUVE(const std::vector<Input::InputActionUVE>& actions, const std::string_view name) {
    return std::any_of(actions.begin(), actions.end(),
                       [name](const Input::InputActionUVE& action) { return action.name == name; });
}

} // namespace

std::optional<Input::InputBindingUVE> CaptureInputBindingUVE(const Input::IInputSystemUVE& input,
                                                             const Input::IGamepadInputSystemUVE& gamepads,
                                                             const bool includeMouse) {
    for (int value = static_cast<int>(Input::KeyCodeUVE::A); value < static_cast<int>(Input::KeyCodeUVE::Count);
         ++value) {
        const auto key = static_cast<Input::KeyCodeUVE>(value);
        if (key != Input::KeyCodeUVE::Escape && input.WasKeyPressedThisFrameUVE(key)) {
            return Input::KeyBindingUVE(key);
        }
    }
    if (includeMouse) {
        for (int value = 0; value < static_cast<int>(Input::MouseButtonUVE::Count); ++value) {
            const auto button = static_cast<Input::MouseButtonUVE>(value);
            if (input.WasMouseButtonPressedThisFrameUVE(button)) {
                return Input::MouseBindingUVE(button);
            }
        }
    }
    for (std::size_t pad = 0U; pad < Input::kMaximumGamepadCountUVE; ++pad) {
        for (int value = 0; value < static_cast<int>(Input::GamepadButtonUVE::Count); ++value) {
            const auto button = static_cast<Input::GamepadButtonUVE>(value);
            if (gamepads.WasButtonPressedThisFrameUVE(pad, button)) {
                return Input::GamepadButtonBindingUVE(pad, button);
            }
        }
        const Input::GamepadStateSnapshotUVE previous = gamepads.GetPreviousSnapshotUVE(pad);
        const Input::GamepadStateSnapshotUVE current = gamepads.GetSnapshotUVE(pad);
        for (std::size_t axis = 0U; axis < current.axes.size(); ++axis) {
            const float now = current.axes[axis];
            // Crossing the threshold this frame, not merely resting past it, so a stick left
            // leaning when listening starts does not bind itself.
            if (std::abs(now) >= kAxisCaptureThresholdUVE && std::abs(previous.axes[axis]) < kAxisCaptureThresholdUVE) {
                return Input::GamepadAxisBindingUVE(pad, static_cast<Input::GamepadAxisUVE>(axis),
                                                    now < 0.0F ? -1.0F : 1.0F);
            }
        }
    }
    return std::nullopt;
}

std::vector<std::string> FindInputConflictsUVE(const std::vector<Input::InputActionUVE>& actions,
                                               const std::size_t actionIndex, const Input::InputBindingUVE& binding) {
    std::vector<std::string> conflicts;
    for (std::size_t index = 0U; index < actions.size(); ++index) {
        if (index == actionIndex) {
            continue;
        }
        const Input::InputActionUVE& action = actions[index];
        const auto uses = [&binding](const std::vector<Input::InputBindingUVE>& bindings) {
            return std::any_of(bindings.begin(), bindings.end(), [&binding](const Input::InputBindingUVE& other) {
                return Input::IsSameInputUVE(binding, other);
            });
        };
        if (uses(action.positiveBindings) || uses(action.negativeBindings)) {
            conflicts.push_back(action.name);
        }
    }
    return conflicts;
}

std::string MakeUniqueActionNameUVE(const std::vector<Input::InputActionUVE>& actions, const std::string_view base) {
    std::string name(base);
    for (int suffix = 2; HasActionNamedUVE(actions, name); ++suffix) {
        name = std::string(base) + "_" + std::to_string(suffix);
    }
    return name;
}

} // namespace UVE::Editor
