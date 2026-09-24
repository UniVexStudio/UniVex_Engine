// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_input_map_uve.h"

#include <gtest/gtest.h>

#include "uve/events/event_system_uve.h"
#include "uve/input/gamepad_input_system_uve.h"
#include "uve/input/input_system_uve.h"

namespace UVE::Editor::Tests {
namespace {

using Input::InputActionTypeUVE;
using Input::InputActionUVE;
using Input::KeyBindingUVE;
using Input::KeyCodeUVE;

class InputCaptureUVETest : public ::testing::Test {
protected:
    /// One frame: the gamepads commit, then the input system, as the engine orders them.
    void FrameUVE() {
        gamepads.UpdateUVE();
        input.UpdateUVE();
    }

    Events::EventSystemUVE events;
    Input::GamepadInputSystemUVE gamepads;
    Input::InputSystemUVE input{events, &gamepads};
};

TEST_F(InputCaptureUVETest, NothingPressedCapturesNothing) {
    FrameUVE();
    EXPECT_FALSE(CaptureInputBindingUVE(input, gamepads, true).has_value());
}

TEST_F(InputCaptureUVETest, AKeyPressedThisFrameIsCapturedButNotOneHeldFromBefore) {
    input.SetKeyStateUVE(KeyCodeUVE::Space, true);
    FrameUVE();
    const auto captured = CaptureInputBindingUVE(input, gamepads, false);
    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(captured->source, Input::InputBindingSourceUVE::Keyboard);
    EXPECT_EQ(captured->key, KeyCodeUVE::Space);
    FrameUVE(); // still held
    EXPECT_FALSE(CaptureInputBindingUVE(input, gamepads, false).has_value());
}

TEST_F(InputCaptureUVETest, EscapeCancelsRatherThanBinds) {
    input.SetKeyStateUVE(KeyCodeUVE::Escape, true);
    FrameUVE();
    EXPECT_FALSE(CaptureInputBindingUVE(input, gamepads, true).has_value());
}

TEST_F(InputCaptureUVETest, MouseButtonsBindOnlyWhenAllowed) {
    input.SetMouseButtonStateUVE(Input::MouseButtonUVE::Right, true);
    FrameUVE();
    EXPECT_FALSE(CaptureInputBindingUVE(input, gamepads, false).has_value());
    const auto captured = CaptureInputBindingUVE(input, gamepads, true);
    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(captured->mouseButton, Input::MouseButtonUVE::Right);
}

TEST_F(InputCaptureUVETest, GamepadButtonsAndPushedSticksAreCaptured) {
    gamepads.SetConnectedUVE(1U, true);
    FrameUVE();
    gamepads.SetButtonStateUVE(1U, Input::GamepadButtonUVE::North, true);
    FrameUVE();
    auto captured = CaptureInputBindingUVE(input, gamepads, false);
    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(captured->source, Input::InputBindingSourceUVE::GamepadButton);
    EXPECT_EQ(captured->gamepadIndex, 1U);
    EXPECT_EQ(captured->gamepadButton, Input::GamepadButtonUVE::North);

    gamepads.SetButtonStateUVE(1U, Input::GamepadButtonUVE::North, false);
    gamepads.SetAxisStateUVE(1U, Input::GamepadAxisUVE::LeftY, -0.9F);
    FrameUVE();
    captured = CaptureInputBindingUVE(input, gamepads, false);
    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(captured->source, Input::InputBindingSourceUVE::GamepadAxis);
    EXPECT_EQ(captured->gamepadAxis, Input::GamepadAxisUVE::LeftY);
    EXPECT_LT(captured->scale, 0.0F);

    // Left leaning, the stick does not bind itself again.
    FrameUVE();
    EXPECT_FALSE(CaptureInputBindingUVE(input, gamepads, false).has_value());
}

TEST(InputMapEditorUVETest, ConflictsNameEveryOtherActionOnTheSameInput) {
    const std::vector<InputActionUVE> actions{
        {"jump", InputActionTypeUVE::Button, {KeyBindingUVE(KeyCodeUVE::Space)}, {}},
        {"confirm", InputActionTypeUVE::Button, {KeyBindingUVE(KeyCodeUVE::Space), KeyBindingUVE(KeyCodeUVE::Enter)}, {}},
        {"move", InputActionTypeUVE::Axis1D, {KeyBindingUVE(KeyCodeUVE::D)}, {KeyBindingUVE(KeyCodeUVE::Space)}},
    };
    EXPECT_EQ(FindInputConflictsUVE(actions, 0U, KeyBindingUVE(KeyCodeUVE::Space)),
              (std::vector<std::string>{"confirm", "move"}));
    EXPECT_TRUE(FindInputConflictsUVE(actions, 1U, KeyBindingUVE(KeyCodeUVE::Enter)).empty());
}

TEST(InputMapEditorUVETest, NewActionNamesNeverCollide) {
    std::vector<InputActionUVE> actions;
    EXPECT_EQ(MakeUniqueActionNameUVE(actions, "new_action"), "new_action");
    actions.push_back({"new_action", InputActionTypeUVE::Button, {}, {}});
    actions.push_back({"new_action_2", InputActionTypeUVE::Button, {}, {}});
    EXPECT_EQ(MakeUniqueActionNameUVE(actions, "new_action"), "new_action_3");
}

} // namespace
} // namespace UVE::Editor::Tests
