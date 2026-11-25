// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string>
#include <vector>

#include "input_binding.h"

namespace UVE::Input {

/// Whether an InputAction reports a discrete triggered/held/released state (Button) or a
/// continuous value in [-1, 1] (Axis1D).
enum class InputActionType {
    Button,
    Axis1D,
};

/// A named, spec-style ("like Unity Input System") mapping from one or more physical inputs to a
/// single gameplay-facing action — e.g. `{"Jump", Button, {KeyBinding(KeyCode::Space)}, {}}`
/// or `{"MoveHorizontal", Axis1D, {KeyBinding(KeyCode::D)}, {KeyBinding(KeyCode::A)}}`.
/// `positiveBindings` are OR'd together for a Button action (any bound input down triggers it) or
/// contribute +1 for an Axis1D action; `negativeBindings` are only meaningful for Axis1D
/// (contribute -1) and are ignored for Button actions.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct InputAction {
    std::string name;
    InputActionType type = InputActionType::Button;
    std::vector<InputBinding> positiveBindings;
    std::vector<InputBinding> negativeBindings;
};

} // namespace UVE::Input
