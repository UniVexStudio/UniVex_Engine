// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>
#include <vector>

#include "i_input_system.h"
#include "input_action.h"
#include "vector2.h"
#include "vector3.h"

namespace UVE::Gameplay {

inline constexpr std::string_view kMoveHorizontalAction = "MoveHorizontal";
inline constexpr std::string_view kMoveForwardAction = "MoveForward";
inline constexpr std::string_view kJumpAction = "Jump";
inline constexpr std::string_view kInteractAction = "Interact";
inline constexpr std::string_view kLookHorizontalAction = "LookHorizontal";
inline constexpr std::string_view kLookVerticalAction = "LookVertical";

struct GameplayInput final {
    Math::Vector3 move{};
    float rise = 0.0F;
    bool jumpPressed = false;
    Math::Vector2 lookPointer{};
    Math::Vector2 lookStick{};
    bool interactPressed = false;

    [[nodiscard]] bool operator==(const GameplayInput&) const = default;
};

[[nodiscard]] std::vector<Input::InputAction> MakeDefaultGameplayActions();

void RegisterDefaultGameplayActions(Input::IInputSystem& inputSystem);

[[nodiscard]] GameplayInput CollectGameplayInput(const Input::IInputSystem& inputSystem);

} // namespace UVE::Gameplay
