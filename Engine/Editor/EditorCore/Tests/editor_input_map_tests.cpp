// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_input_map.h"

#include <gtest/gtest.h>

#include "event_system.h"
#include "gamepad_input_system.h"
#include "input_system.h"

namespace UVE::Editor::Tests {
namespace {

using Input::InputActionType;
using Input::InputAction;
using Input::KeyBinding;
using Input::KeyCode;

class InputCaptureTest : public ::testing::Test {
protected:
    /// One frame: the gamepads commit, then the input system, as the engine orders them.
    void Frame() {
        gamepads.Update();
        input.Update();
    }

    Events::EventSystem events;
    Input::GamepadInputSystem gamepads;
    Input::InputSystem input{events, &gamepads};
};

TEST_F(InputCaptureTest, NothingPressedCapturesNothing) {
    Frame();
    EXPECT_FALSE(CaptureInputBinding(input, gamepads, true).has_value());
}

TEST_F(InputCaptureTest, AKeyPressedThisFrameIsCapturedButNotOneHeldFromBefore) {
    input.SetKeyState(KeyCode::Space, true);
    Frame();
    const auto captured = CaptureInputBinding(input, gamepads, false);
    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(captured->source, Input::InputBindingSource::Keyboard);
    EXPECT_EQ(captured->key, KeyCode::Space);
    Frame(); // still held
    EXPECT_FALSE(CaptureInputBinding(input, gamepads, false).has_value());
}

TEST_F(InputCaptureTest, EscapeCancelsRatherThanBinds) {
    input.SetKeyState(KeyCode::Escape, true);
    Frame();
    EXPECT_FALSE(CaptureInputBinding(input, gamepads, true).has_value());
}

TEST_F(InputCaptureTest, MouseButtonsBindOnlyWhenAllowed) {
    input.SetMouseButtonState(Input::MouseButton::Right, true);
    Frame();
    EXPECT_FALSE(CaptureInputBinding(input, gamepads, false).has_value());
    const auto captured = CaptureInputBinding(input, gamepads, true);
    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(captured->mouseButton, Input::MouseButton::Right);
}

TEST_F(InputCaptureTest, GamepadButtonsAndPushedSticksAreCaptured) {
    gamepads.SetConnected(1U, true);
    Frame();
    gamepads.SetButtonState(1U, Input::GamepadButton::North, true);
    Frame();
    auto captured = CaptureInputBinding(input, gamepads, false);
    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(captured->source, Input::InputBindingSource::GamepadButton);
    EXPECT_EQ(captured->gamepadIndex, 1U);
    EXPECT_EQ(captured->gamepadButton, Input::GamepadButton::North);

    gamepads.SetButtonState(1U, Input::GamepadButton::North, false);
    gamepads.SetAxisState(1U, Input::GamepadAxis::LeftY, -0.9F);
    Frame();
    captured = CaptureInputBinding(input, gamepads, false);
    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(captured->source, Input::InputBindingSource::GamepadAxis);
    EXPECT_EQ(captured->gamepadAxis, Input::GamepadAxis::LeftY);
    EXPECT_LT(captured->scale, 0.0F);

    // Left leaning, the stick does not bind itself again.
    Frame();
    EXPECT_FALSE(CaptureInputBinding(input, gamepads, false).has_value());
}

TEST(InputMapEditorTest, ConflictsNameEveryOtherActionOnTheSameInput) {
    const std::vector<InputAction> actions{
        {"jump", InputActionType::Button, {KeyBinding(KeyCode::Space)}, {}},
        {"confirm", InputActionType::Button, {KeyBinding(KeyCode::Space), KeyBinding(KeyCode::Enter)}, {}},
        {"move", InputActionType::Axis1D, {KeyBinding(KeyCode::D)}, {KeyBinding(KeyCode::Space)}},
    };
    EXPECT_EQ(FindInputConflicts(actions, 0U, KeyBinding(KeyCode::Space)),
              (std::vector<std::string>{"confirm", "move"}));
    EXPECT_TRUE(FindInputConflicts(actions, 1U, KeyBinding(KeyCode::Enter)).empty());
}

TEST(InputMapEditorTest, NewActionNamesNeverCollide) {
    std::vector<InputAction> actions;
    EXPECT_EQ(MakeUniqueActionName(actions, "new_action"), "new_action");
    actions.push_back({"new_action", InputActionType::Button, {}, {}});
    actions.push_back({"new_action_2", InputActionType::Button, {}, {}});
    EXPECT_EQ(MakeUniqueActionName(actions, "new_action"), "new_action_3");
}

TEST_F(InputCaptureTest, ResetActionMapRuntimeRebindAndActionSetFilteringWorkDeterministically) {
    const std::vector<InputAction> projectDefaults{
        {"jump", InputActionType::Button, {KeyBinding(KeyCode::Space)}, {}},
        {"ui.accept", InputActionType::Button, {KeyBinding(KeyCode::Enter)}, {}},
    };
    std::vector<InputAction> actions = projectDefaults;
    actions[0U].positiveBindings = {KeyBinding(KeyCode::E)};

    EXPECT_TRUE(ResetActionToProjectDefault(actions, projectDefaults, "jump"));
    ASSERT_EQ(actions[0U].positiveBindings.size(), 1U);
    EXPECT_EQ(actions[0U].positiveBindings[0U].key, KeyCode::Space);
    EXPECT_FALSE(ResetActionToProjectDefault(actions, projectDefaults, "unknown"));

    actions.clear();
    ResetInputMapToProjectDefaults(actions, projectDefaults);
    EXPECT_EQ(actions.size(), 2U);

    const std::vector<InputAction> uiSet = FilterActionsBySetPrefix(actions, "ui");
    ASSERT_EQ(uiSet.size(), 1U);
    EXPECT_EQ(uiSet[0U].name, "ui.accept");

    input.RegisterAction(InputAction{projectDefaults[0U]});
    EXPECT_TRUE(ApplyRuntimeActionRebind(input, "jump", KeyBinding(KeyCode::J)));
    input.SetKeyState(KeyCode::J, true);
    Frame();
    EXPECT_TRUE(input.IsActionHeld("jump"));
}

} // namespace
} // namespace UVE::Editor::Tests
