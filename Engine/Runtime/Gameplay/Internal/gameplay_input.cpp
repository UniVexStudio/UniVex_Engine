// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "gameplay_input.h"

#include <cmath>
#include <string>
#include <utility>

#include "input_binding.h"
#include "key_code.h"

namespace UVE::Gameplay {
namespace {

using Input::GamepadAxisBinding;
using Input::GamepadAxis;
using Input::GamepadButtonBinding;
using Input::GamepadButton;
using Input::InputActionType;
using Input::InputAction;
using Input::KeyBinding;
using Input::KeyCode;

void AddMoveFromKeys(Math::Vector3& move, const Input::IInputSystem& inputSystem) {
    if (inputSystem.IsKeyDown(KeyCode::W)) {
        move.z -= 1.0F;
    }
    if (inputSystem.IsKeyDown(KeyCode::S)) {
        move.z += 1.0F;
    }
    if (inputSystem.IsKeyDown(KeyCode::A)) {
        move.x -= 1.0F;
    }
    if (inputSystem.IsKeyDown(KeyCode::D)) {
        move.x += 1.0F;
    }
}

void NormalizePlanar(Math::Vector3& move) {
    const float lengthSquared = move.x * move.x + move.z * move.z;
    if (lengthSquared > 1.0F) {
        const float inverseLength = 1.0F / std::sqrt(lengthSquared);
        move.x *= inverseLength;
        move.z *= inverseLength;
    }
}

} // namespace

std::vector<InputAction> MakeDefaultGameplayActions() {
    std::vector<InputAction> actions;
    actions.push_back(InputAction{std::string{kMoveHorizontalAction}, InputActionType::Axis1D,
                                     {KeyBinding(KeyCode::D), GamepadAxisBinding(0U, GamepadAxis::LeftX)},
                                     {KeyBinding(KeyCode::A)}});
    actions.push_back(InputAction{std::string{kMoveForwardAction}, InputActionType::Axis1D,
                                     {KeyBinding(KeyCode::S), GamepadAxisBinding(0U, GamepadAxis::LeftY)},
                                     {KeyBinding(KeyCode::W)}});
    actions.push_back(InputAction{std::string{kJumpAction}, InputActionType::Button,
                                     {KeyBinding(KeyCode::Space), GamepadButtonBinding(0U, GamepadButton::South)},
                                     {}});
    actions.push_back(InputAction{std::string{kInteractAction}, InputActionType::Button,
                                     {KeyBinding(KeyCode::E), GamepadButtonBinding(0U, GamepadButton::West)},
                                     {}});
    actions.push_back(InputAction{std::string{kLookHorizontalAction}, InputActionType::Axis1D,
                                     {GamepadAxisBinding(0U, GamepadAxis::RightX)},
                                     {}});
    actions.push_back(InputAction{std::string{kLookVerticalAction}, InputActionType::Axis1D,
                                     {GamepadAxisBinding(0U, GamepadAxis::RightY)},
                                     {}});
    return actions;
}

void RegisterDefaultGameplayActions(Input::IInputSystem& inputSystem) {
    for (InputAction& action : MakeDefaultGameplayActions()) {
        if (!inputSystem.HasAction(action.name)) {
            inputSystem.RegisterAction(std::move(action));
        }
    }
}

GameplayInput CollectGameplayInput(const Input::IInputSystem& inputSystem) {
    GameplayInput input;
    input.move.x = inputSystem.GetAxisValue(kMoveHorizontalAction);
    input.move.z = inputSystem.GetAxisValue(kMoveForwardAction);
    if (input.move.x == 0.0F && input.move.z == 0.0F) {
        AddMoveFromKeys(input.move, inputSystem);
    }
    NormalizePlanar(input.move);

    const bool jumpHeld = inputSystem.IsActionHeld(kJumpAction) || inputSystem.IsKeyDown(KeyCode::Space);
    input.jumpPressed =
        inputSystem.IsActionTriggered(kJumpAction) || inputSystem.WasKeyPressedThisFrame(KeyCode::Space);
    const float sink = inputSystem.IsKeyDown(KeyCode::LeftCtrl) ? 1.0F : 0.0F;
    input.rise = (jumpHeld ? 1.0F : 0.0F) - sink;

    input.lookPointer = inputSystem.GetMouseDelta();
    input.lookStick.x = inputSystem.GetAxisValue(kLookHorizontalAction);
    input.lookStick.y = inputSystem.GetAxisValue(kLookVerticalAction);
    input.interactPressed =
        inputSystem.IsActionTriggered(kInteractAction) || inputSystem.WasKeyPressedThisFrame(KeyCode::E);
    return input;
}

} // namespace UVE::Gameplay
