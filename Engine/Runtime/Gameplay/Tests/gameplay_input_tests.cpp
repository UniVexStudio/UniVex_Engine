// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "gameplay_input.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "event_system.h"
#include "input_system.h"
#include "key_code.h"

namespace UVE::Gameplay::Tests {
namespace {

TEST(GameplayInputTest, DefaultsIncludeMoveJumpInteractAndLook) {
    const std::vector<Input::InputAction> actions = MakeDefaultGameplayActions();
    ASSERT_EQ(actions.size(), 6U);
    EXPECT_EQ(actions[0].name, kMoveHorizontalAction);
    EXPECT_EQ(actions[2].name, kJumpAction);
    EXPECT_EQ(actions[3].name, kInteractAction);
}

TEST(GameplayInputTest, RegisterDefaultDoesNotReplaceAnExistingAction) {
    Events::EventSystem events;
    Input::InputSystem input(events);
    input.RegisterAction(Input::InputAction{
        std::string{kJumpAction}, Input::InputActionType::Button,
        {Input::KeyBinding(Input::KeyCode::Enter)}, {}});
    RegisterDefaultGameplayActions(input);
    EXPECT_TRUE(input.HasAction(kJumpAction));
    EXPECT_TRUE(input.HasAction(kMoveHorizontalAction));
    input.SetKeyState(Input::KeyCode::Enter, true);
    input.Update();
    EXPECT_TRUE(input.IsActionHeld(kJumpAction));
    input.SetKeyState(Input::KeyCode::Enter, false);
    input.SetKeyState(Input::KeyCode::Space, true);
    input.Update();
    EXPECT_FALSE(input.IsActionHeld(kJumpAction));
}

TEST(GameplayInputTest, CollectReadsWasdJumpAndInteract) {
    Events::EventSystem events;
    Input::InputSystem input(events);
    RegisterDefaultGameplayActions(input);
    input.SetKeyState(Input::KeyCode::W, true);
    input.SetKeyState(Input::KeyCode::D, true);
    input.SetKeyState(Input::KeyCode::Space, true);
    input.SetKeyState(Input::KeyCode::E, true);
    input.Update();
    const GameplayInput frame = CollectGameplayInput(input);
    EXPECT_GT(frame.move.x, 0.0F);
    EXPECT_LT(frame.move.z, 0.0F);
    EXPECT_TRUE(frame.jumpPressed);
    EXPECT_TRUE(frame.interactPressed);
    EXPECT_FLOAT_EQ(frame.rise, 1.0F);
}

} // namespace
} // namespace UVE::Gameplay::Tests
