// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>
#include <vector>

#include "uve/input/i_input_system_uve.h"
#include "uve/input/input_action_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Gameplay {

inline constexpr std::string_view kMoveHorizontalActionUVE = "MoveHorizontal";
inline constexpr std::string_view kMoveForwardActionUVE = "MoveForward";
inline constexpr std::string_view kJumpActionUVE = "Jump";
inline constexpr std::string_view kInteractActionUVE = "Interact";
inline constexpr std::string_view kLookHorizontalActionUVE = "LookHorizontal";
inline constexpr std::string_view kLookVerticalActionUVE = "LookVertical";

struct GameplayInputUVE final {
    Math::Vector3UVE move{};
    float rise = 0.0F;
    bool jumpPressed = false;
    Math::Vector2UVE lookPointer{};
    Math::Vector2UVE lookStick{};
    bool interactPressed = false;

    [[nodiscard]] bool operator==(const GameplayInputUVE&) const = default;
};

[[nodiscard]] std::vector<Input::InputActionUVE> MakeDefaultGameplayActionsUVE();

void RegisterDefaultGameplayActionsUVE(Input::IInputSystemUVE& inputSystem);

[[nodiscard]] GameplayInputUVE CollectGameplayInputUVE(const Input::IInputSystemUVE& inputSystem);

} // namespace UVE::Gameplay
