// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/gameplay/gameplay_input_uve.h"

#include <cmath>
#include <string>
#include <utility>

#include "uve/input/input_binding_uve.h"
#include "uve/input/key_code_uve.h"

namespace UVE::Gameplay {
namespace {

using Input::GamepadAxisBindingUVE;
using Input::GamepadAxisUVE;
using Input::GamepadButtonBindingUVE;
using Input::GamepadButtonUVE;
using Input::InputActionTypeUVE;
using Input::InputActionUVE;
using Input::KeyBindingUVE;
using Input::KeyCodeUVE;

void AddMoveFromKeysUVE(Math::Vector3UVE& move, const Input::IInputSystemUVE& inputSystem) {
    if (inputSystem.IsKeyDownUVE(KeyCodeUVE::W)) {
        move.z -= 1.0F;
    }
    if (inputSystem.IsKeyDownUVE(KeyCodeUVE::S)) {
        move.z += 1.0F;
    }
    if (inputSystem.IsKeyDownUVE(KeyCodeUVE::A)) {
        move.x -= 1.0F;
    }
    if (inputSystem.IsKeyDownUVE(KeyCodeUVE::D)) {
        move.x += 1.0F;
    }
}

void NormalizePlanarUVE(Math::Vector3UVE& move) {
    const float lengthSquared = move.x * move.x + move.z * move.z;
    if (lengthSquared > 1.0F) {
        const float inverseLength = 1.0F / std::sqrt(lengthSquared);
        move.x *= inverseLength;
        move.z *= inverseLength;
    }
}

} // namespace

std::vector<InputActionUVE> MakeDefaultGameplayActionsUVE() {
    std::vector<InputActionUVE> actions;
    actions.push_back(InputActionUVE{std::string{kMoveHorizontalActionUVE}, InputActionTypeUVE::Axis1D,
                                     {KeyBindingUVE(KeyCodeUVE::D), GamepadAxisBindingUVE(0U, GamepadAxisUVE::LeftX)},
                                     {KeyBindingUVE(KeyCodeUVE::A)}});
    actions.push_back(InputActionUVE{std::string{kMoveForwardActionUVE}, InputActionTypeUVE::Axis1D,
                                     {KeyBindingUVE(KeyCodeUVE::S), GamepadAxisBindingUVE(0U, GamepadAxisUVE::LeftY)},
                                     {KeyBindingUVE(KeyCodeUVE::W)}});
    actions.push_back(InputActionUVE{std::string{kJumpActionUVE}, InputActionTypeUVE::Button,
                                     {KeyBindingUVE(KeyCodeUVE::Space), GamepadButtonBindingUVE(0U, GamepadButtonUVE::South)},
                                     {}});
    actions.push_back(InputActionUVE{std::string{kInteractActionUVE}, InputActionTypeUVE::Button,
                                     {KeyBindingUVE(KeyCodeUVE::E), GamepadButtonBindingUVE(0U, GamepadButtonUVE::West)},
                                     {}});
    actions.push_back(InputActionUVE{std::string{kLookHorizontalActionUVE}, InputActionTypeUVE::Axis1D,
                                     {GamepadAxisBindingUVE(0U, GamepadAxisUVE::RightX)},
                                     {}});
    actions.push_back(InputActionUVE{std::string{kLookVerticalActionUVE}, InputActionTypeUVE::Axis1D,
                                     {GamepadAxisBindingUVE(0U, GamepadAxisUVE::RightY)},
                                     {}});
    return actions;
}

void RegisterDefaultGameplayActionsUVE(Input::IInputSystemUVE& inputSystem) {
    for (InputActionUVE& action : MakeDefaultGameplayActionsUVE()) {
        if (!inputSystem.HasActionUVE(action.name)) {
            inputSystem.RegisterActionUVE(std::move(action));
        }
    }
}

GameplayInputUVE CollectGameplayInputUVE(const Input::IInputSystemUVE& inputSystem) {
    GameplayInputUVE input;
    input.move.x = inputSystem.GetAxisValueUVE(kMoveHorizontalActionUVE);
    input.move.z = inputSystem.GetAxisValueUVE(kMoveForwardActionUVE);
    if (input.move.x == 0.0F && input.move.z == 0.0F) {
        AddMoveFromKeysUVE(input.move, inputSystem);
    }
    NormalizePlanarUVE(input.move);

    const bool jumpHeld = inputSystem.IsActionHeldUVE(kJumpActionUVE) || inputSystem.IsKeyDownUVE(KeyCodeUVE::Space);
    input.jumpPressed =
        inputSystem.IsActionTriggeredUVE(kJumpActionUVE) || inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::Space);
    const float sink = inputSystem.IsKeyDownUVE(KeyCodeUVE::LeftCtrl) ? 1.0F : 0.0F;
    input.rise = (jumpHeld ? 1.0F : 0.0F) - sink;

    input.lookPointer = inputSystem.GetMouseDeltaUVE();
    input.lookStick.x = inputSystem.GetAxisValueUVE(kLookHorizontalActionUVE);
    input.lookStick.y = inputSystem.GetAxisValueUVE(kLookVerticalActionUVE);
    input.interactPressed =
        inputSystem.IsActionTriggeredUVE(kInteractActionUVE) || inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::E);
    return input;
}

} // namespace UVE::Gameplay
