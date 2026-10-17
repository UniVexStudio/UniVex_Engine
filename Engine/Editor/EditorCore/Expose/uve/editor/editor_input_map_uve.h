// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "uve/input/i_gamepad_input_system_uve.h"
#include "uve/input/i_input_system_uve.h"
#include "uve/input/input_action_uve.h"

namespace UVE::Editor {

/// The input pressed this frame, as a binding, for the Input Map window's listen-for-input
/// capture: a key, a mouse button (only with `includeMouse`, so clicking the window's own buttons
/// binds nothing), a gamepad button, or a gamepad stick or trigger pushed past half way (bound in
/// the direction it was pushed). Escape is never captured - it cancels listening.
[[nodiscard]] std::optional<Input::InputBindingUVE> CaptureInputBindingUVE(const Input::IInputSystemUVE& input,
                                                                         const Input::IGamepadInputSystemUVE& gamepads,
                                                                         bool includeMouse);

/// The other actions in `actions` that also use the input `binding` names - on either side of an
/// axis - in map order. Empty when `actionIndex` is the only one.
[[nodiscard]] std::vector<std::string> FindInputConflictsUVE(const std::vector<Input::InputActionUVE>& actions,
                                                             std::size_t actionIndex,
                                                             const Input::InputBindingUVE& binding);

/// `base`, or `base_2`, `base_3`... - the first name no action in `actions` has.
[[nodiscard]] std::string MakeUniqueActionNameUVE(const std::vector<Input::InputActionUVE>& actions,
                                                  std::string_view base);

/// Restores one named action in `actions` from `projectDefaults`. Returns false if `actionName`
/// is not present in `projectDefaults`.
bool ResetActionToProjectDefaultUVE(std::vector<Input::InputActionUVE>& actions,
                                    const std::vector<Input::InputActionUVE>& projectDefaults,
                                    std::string_view actionName);

/// Replaces `actions` with `projectDefaults`.
void ResetInputMapToProjectDefaultsUVE(std::vector<Input::InputActionUVE>& actions,
                                       const std::vector<Input::InputActionUVE>& projectDefaults);

/// Applies a player-facing runtime rebinding on `input` via `RemapActionUVE`.
bool ApplyRuntimeActionRebindUVE(Input::IInputSystemUVE& input, std::string_view actionName,
                                 const Input::InputBindingUVE& newBinding);

/// Returns actions belonging to `actionSetPrefix` (e.g. "gameplay" matches "gameplay.*" or
/// unprefixed actions when `actionSetPrefix == "gameplay"`).
[[nodiscard]] std::vector<Input::InputActionUVE> FilterActionsBySetPrefixUVE(
    const std::vector<Input::InputActionUVE>& actions, std::string_view actionSetPrefix);

} // namespace UVE::Editor
