// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "input_system.h"

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "event_system.h"
#include "gamepad_input_system.h"
#include "input_action_triggered_event.h"

namespace UVE::Input::Tests {
namespace {

constexpr float kEpsilon = 1e-5F;

class InputSystemTest : public ::testing::Test {
protected:
    Events::EventSystem eventSystem;
    InputSystem inputSystem{eventSystem};
};

TEST_F(InputSystemTest, SetKeyState_IsKeyDown_RoundTrips) {
    EXPECT_FALSE(inputSystem.IsKeyDown(KeyCode::A));

    inputSystem.SetKeyState(KeyCode::A, true);
    inputSystem.Update();
    EXPECT_TRUE(inputSystem.IsKeyDown(KeyCode::A));

    inputSystem.SetKeyState(KeyCode::A, false);
    inputSystem.Update();
    EXPECT_FALSE(inputSystem.IsKeyDown(KeyCode::A));
}

TEST_F(InputSystemTest, KeyEdgeDetection_PressedHeldReleasedNotHeld) {
    inputSystem.SetKeyState(KeyCode::Space, true);
    inputSystem.Update();
    EXPECT_TRUE(inputSystem.WasKeyPressedThisFrame(KeyCode::Space));
    EXPECT_FALSE(inputSystem.WasKeyReleasedThisFrame(KeyCode::Space));
    EXPECT_TRUE(inputSystem.IsKeyDown(KeyCode::Space));

    inputSystem.Update(); // held, no new Set call
    EXPECT_FALSE(inputSystem.WasKeyPressedThisFrame(KeyCode::Space));
    EXPECT_TRUE(inputSystem.IsKeyDown(KeyCode::Space));

    inputSystem.SetKeyState(KeyCode::Space, false);
    inputSystem.Update();
    EXPECT_TRUE(inputSystem.WasKeyReleasedThisFrame(KeyCode::Space));
    EXPECT_FALSE(inputSystem.IsKeyDown(KeyCode::Space));

    inputSystem.Update(); // not held, no new Set call
    EXPECT_FALSE(inputSystem.WasKeyReleasedThisFrame(KeyCode::Space));
}

TEST_F(InputSystemTest, MouseButtonEdgeDetection_PressedHeldReleased) {
    inputSystem.SetMouseButtonState(MouseButton::Left, true);
    inputSystem.Update();
    EXPECT_TRUE(inputSystem.WasMouseButtonPressedThisFrame(MouseButton::Left));
    EXPECT_TRUE(inputSystem.IsMouseButtonDown(MouseButton::Left));

    inputSystem.Update();
    EXPECT_FALSE(inputSystem.WasMouseButtonPressedThisFrame(MouseButton::Left));

    inputSystem.SetMouseButtonState(MouseButton::Left, false);
    inputSystem.Update();
    EXPECT_TRUE(inputSystem.WasMouseButtonReleasedThisFrame(MouseButton::Left));
    EXPECT_FALSE(inputSystem.IsMouseButtonDown(MouseButton::Left));
}

TEST_F(InputSystemTest, MousePositionAndDelta_ComputedAcrossUpdate) {
    inputSystem.SetMousePosition(Math::Vector2{10.0F, 20.0F});
    inputSystem.Update();
    EXPECT_NEAR(inputSystem.GetMousePosition().x, 10.0F, kEpsilon);
    EXPECT_NEAR(inputSystem.GetMousePosition().y, 20.0F, kEpsilon);
    EXPECT_NEAR(inputSystem.GetMouseDelta().x, 10.0F, kEpsilon); // moved from default (0,0)
    EXPECT_NEAR(inputSystem.GetMouseDelta().y, 20.0F, kEpsilon);

    inputSystem.SetMousePosition(Math::Vector2{13.0F, 15.0F});
    inputSystem.Update();
    EXPECT_NEAR(inputSystem.GetMouseDelta().x, 3.0F, kEpsilon);
    EXPECT_NEAR(inputSystem.GetMouseDelta().y, -5.0F, kEpsilon);

    inputSystem.Update(); // no movement this frame
    EXPECT_NEAR(inputSystem.GetMouseDelta().x, 0.0F, kEpsilon);
    EXPECT_NEAR(inputSystem.GetMouseDelta().y, 0.0F, kEpsilon);
}

TEST_F(InputSystemTest, MousePosition_RejectsNonFiniteAndPreservesLastValidState) {
    inputSystem.SetMousePosition(Math::Vector2{4.0F, 7.0F});
    inputSystem.Update();
    inputSystem.SetMousePosition(Math::Vector2{std::numeric_limits<float>::quiet_NaN(), 9.0F});
    inputSystem.SetMousePosition(Math::Vector2{10.0F, std::numeric_limits<float>::infinity()});
    inputSystem.Update();

    EXPECT_FLOAT_EQ(inputSystem.GetMousePosition().x, 4.0F);
    EXPECT_FLOAT_EQ(inputSystem.GetMousePosition().y, 7.0F);
    EXPECT_FLOAT_EQ(inputSystem.GetMouseDelta().x, 0.0F);
    EXPECT_FLOAT_EQ(inputSystem.GetMouseDelta().y, 0.0F);
}

TEST_F(InputSystemTest, MouseDelta_OverflowFailsClosedWithoutPublishingNonFiniteValue) {
    inputSystem.SetMousePosition(Math::Vector2{-std::numeric_limits<float>::max(), 0.0F});
    inputSystem.Update();
    inputSystem.SetMousePosition(Math::Vector2{std::numeric_limits<float>::max(), 0.0F});
    inputSystem.Update();

    EXPECT_FLOAT_EQ(inputSystem.GetMouseDelta().x, 0.0F);
    EXPECT_FLOAT_EQ(inputSystem.GetMouseDelta().y, 0.0F);
}

TEST_F(InputSystemTest, ScrollDelta_AccumulatesThenResetsAfterUpdate) {
    inputSystem.SetMouseScrollDelta(1.0F);
    inputSystem.SetMouseScrollDelta(0.5F);
    inputSystem.Update();
    EXPECT_NEAR(inputSystem.GetMouseScrollDelta(), 1.5F, kEpsilon);

    inputSystem.Update(); // no new scroll input
    EXPECT_NEAR(inputSystem.GetMouseScrollDelta(), 0.0F, kEpsilon);
}

TEST_F(InputSystemTest, ScrollDelta_RejectsNonFiniteAndOverflowingInput) {
    inputSystem.SetMouseScrollDelta(1.0F);
    inputSystem.SetMouseScrollDelta(std::numeric_limits<float>::quiet_NaN());
    inputSystem.SetMouseScrollDelta(std::numeric_limits<float>::infinity());
    inputSystem.Update();
    EXPECT_FLOAT_EQ(inputSystem.GetMouseScrollDelta(), 1.0F);

    inputSystem.SetMouseScrollDelta(std::numeric_limits<float>::max());
    inputSystem.SetMouseScrollDelta(std::numeric_limits<float>::max());
    inputSystem.Update();
    EXPECT_FLOAT_EQ(inputSystem.GetMouseScrollDelta(), std::numeric_limits<float>::max());
}

TEST_F(InputSystemTest, RegisterAction_RejectsUnboundedAndMalformedNames) {
    const std::string maximumName(kMaximumInputActionNameBytes, 'M');
    inputSystem.RegisterAction(
        InputAction{maximumName, InputActionType::Button, {KeyBinding(KeyCode::Space)}, {}});
    EXPECT_TRUE(inputSystem.RemapAction(maximumName, {KeyBinding(KeyCode::Enter)}, {}));

    inputSystem.RegisterAction(
        InputAction{maximumName + "X", InputActionType::Button, {}, {}});
    EXPECT_FALSE(inputSystem.IsActionHeld(maximumName + "X"));
    inputSystem.RegisterAction(InputAction{"", InputActionType::Button, {}, {}});
    EXPECT_FALSE(inputSystem.IsActionHeld(""));
    const std::string nulName = std::string("bad") + '\0' + "name";
    inputSystem.RegisterAction(InputAction{nulName, InputActionType::Button, {}, {}});
    EXPECT_FALSE(inputSystem.IsActionHeld(nulName));

    inputSystem.RegisterAction(
        InputAction{"InvalidType", InputActionType::Button, {KeyBinding(KeyCode::Space)}, {}});
    inputSystem.RegisterAction(InputAction{"InvalidType", static_cast<InputActionType>(99), {}, {}});
    inputSystem.SetKeyState(KeyCode::Space, true);
    inputSystem.Update();
    EXPECT_TRUE(inputSystem.IsActionHeld("InvalidType"));
}

TEST_F(InputSystemTest, RegisterAction_RejectsNewEntriesAtCountCapButAllowsReplacement) {
    for (std::size_t index = 0U; index < kMaximumInputActions; ++index) {
        inputSystem.RegisterAction(
            InputAction{"Action_" + std::to_string(index), InputActionType::Button, {}, {}});
    }

    const std::string overflowName = "Action_" + std::to_string(kMaximumInputActions);
    inputSystem.RegisterAction(InputAction{overflowName, InputActionType::Button, {}, {}});
    EXPECT_FALSE(inputSystem.RemapAction(overflowName, {KeyBinding(KeyCode::Enter)}, {}));

    inputSystem.RegisterAction(
        InputAction{"Action_0", InputActionType::Button, {KeyBinding(KeyCode::Enter)}, {}});
    EXPECT_TRUE(inputSystem.RemapAction("Action_0", {KeyBinding(KeyCode::Space)}, {}));
}

TEST_F(InputSystemTest, ButtonAction_SingleBinding_TriggeredHeldReleased) {
    inputSystem.RegisterAction(
        InputAction{"Jump", InputActionType::Button, {KeyBinding(KeyCode::Space)}, {}});

    inputSystem.SetKeyState(KeyCode::Space, true);
    inputSystem.Update();
    EXPECT_TRUE(inputSystem.IsActionTriggered("Jump"));
    EXPECT_TRUE(inputSystem.IsActionHeld("Jump"));
    EXPECT_FALSE(inputSystem.IsActionReleased("Jump"));

    inputSystem.Update(); // still held
    EXPECT_FALSE(inputSystem.IsActionTriggered("Jump"));
    EXPECT_TRUE(inputSystem.IsActionHeld("Jump"));

    inputSystem.SetKeyState(KeyCode::Space, false);
    inputSystem.Update();
    EXPECT_FALSE(inputSystem.IsActionHeld("Jump"));
    EXPECT_TRUE(inputSystem.IsActionReleased("Jump"));
}

TEST_F(InputSystemTest, ButtonAction_MultiBindingOr_DoesNotRetriggerOnSecondKey) {
    inputSystem.RegisterAction(InputAction{"Jump",
                                                   InputActionType::Button,
                                                   {KeyBinding(KeyCode::Space), KeyBinding(KeyCode::Enter)},
                                                   {}});

    inputSystem.SetKeyState(KeyCode::Space, true);
    inputSystem.Update();
    EXPECT_TRUE(inputSystem.IsActionTriggered("Jump"));

    // Enter goes down too while Space is still held — must not re-trigger.
    inputSystem.SetKeyState(KeyCode::Enter, true);
    inputSystem.Update();
    EXPECT_FALSE(inputSystem.IsActionTriggered("Jump"));
    EXPECT_TRUE(inputSystem.IsActionHeld("Jump"));
}

TEST_F(InputSystemTest, ButtonAction_GamepadBinding_TriggeredHeldReleased) {
    GamepadInputSystem gamepad;
    InputSystem gamepadInputSystem{eventSystem, &gamepad};
    gamepadInputSystem.RegisterAction(
        InputAction{"Jump", InputActionType::Button,
                       {GamepadButtonBinding(0U, GamepadButton::South)}, {}});

    gamepad.SetConnected(0U, true);
    gamepad.SetButtonState(0U, GamepadButton::South, true);
    gamepad.Update();
    gamepadInputSystem.Update();
    EXPECT_TRUE(gamepadInputSystem.IsActionTriggered("Jump"));
    EXPECT_TRUE(gamepadInputSystem.IsActionHeld("Jump"));

    gamepadInputSystem.Update();
    EXPECT_FALSE(gamepadInputSystem.IsActionTriggered("Jump"));
    EXPECT_TRUE(gamepadInputSystem.IsActionHeld("Jump"));

    gamepad.SetButtonState(0U, GamepadButton::South, false);
    gamepad.Update();
    gamepadInputSystem.Update();
    EXPECT_FALSE(gamepadInputSystem.IsActionHeld("Jump"));
    EXPECT_TRUE(gamepadInputSystem.IsActionReleased("Jump"));
}

TEST_F(InputSystemTest, AxisAction_GamepadBindingUsesScaledClampedAxisValue) {
    GamepadInputSystem gamepad{0.0F};
    InputSystem gamepadInputSystem{eventSystem, &gamepad};
    gamepadInputSystem.RegisterAction(
        InputAction{"MoveHorizontal", InputActionType::Axis1D,
                       {GamepadAxisBinding(0U, GamepadAxis::LeftX, 0.5F)},
                       {GamepadAxisBinding(0U, GamepadAxis::RightX, 2.0F)}});

    gamepad.SetConnected(0U, true);
    gamepad.SetAxisState(0U, GamepadAxis::LeftX, 0.8F);
    gamepad.SetAxisState(0U, GamepadAxis::RightX, -0.25F);
    gamepad.Update();
    gamepadInputSystem.Update();

    EXPECT_NEAR(gamepadInputSystem.GetAxisValue("MoveHorizontal"), 0.9F, kEpsilon);
}

TEST_F(InputSystemTest, AxisAction_ComputesPositiveNegativeBothNeither) {
    inputSystem.RegisterAction(InputAction{"MoveHorizontal",
                                                   InputActionType::Axis1D,
                                                   {KeyBinding(KeyCode::D)},
                                                   {KeyBinding(KeyCode::A)}});

    inputSystem.Update();
    EXPECT_NEAR(inputSystem.GetAxisValue("MoveHorizontal"), 0.0F, kEpsilon);

    inputSystem.SetKeyState(KeyCode::D, true);
    inputSystem.Update();
    EXPECT_NEAR(inputSystem.GetAxisValue("MoveHorizontal"), 1.0F, kEpsilon);

    inputSystem.SetKeyState(KeyCode::D, false);
    inputSystem.SetKeyState(KeyCode::A, true);
    inputSystem.Update();
    EXPECT_NEAR(inputSystem.GetAxisValue("MoveHorizontal"), -1.0F, kEpsilon);

    inputSystem.SetKeyState(KeyCode::D, true); // both held
    inputSystem.Update();
    EXPECT_NEAR(inputSystem.GetAxisValue("MoveHorizontal"), 0.0F, kEpsilon);
}

TEST_F(InputSystemTest, UnregisteredActionName_ReturnsFalseOrZeroWithoutAsserting) {
    EXPECT_FALSE(inputSystem.IsActionTriggered("DoesNotExist"));
    EXPECT_FALSE(inputSystem.IsActionHeld("DoesNotExist"));
    EXPECT_FALSE(inputSystem.IsActionReleased("DoesNotExist"));
    EXPECT_NEAR(inputSystem.GetAxisValue("DoesNotExist"), 0.0F, kEpsilon);
    EXPECT_FALSE(inputSystem.HasAction("DoesNotExist"));
}

TEST_F(InputSystemTest, HasAction_IsTrueAfterRegister) {
    EXPECT_FALSE(inputSystem.HasAction("Jump"));
    inputSystem.RegisterAction(
        InputAction{"Jump", InputActionType::Button, {KeyBinding(KeyCode::Space)}, {}});
    EXPECT_TRUE(inputSystem.HasAction("Jump"));
}

TEST_F(InputSystemTest, UnregisterAction_RemovesActionAndReportsWhetherItExisted) {
    inputSystem.RegisterAction(
        InputAction{"Jump", InputActionType::Button, {KeyBinding(KeyCode::Space)}, {}});

    EXPECT_TRUE(inputSystem.UnregisterAction("Jump"));
    EXPECT_FALSE(inputSystem.UnregisterAction("Jump")); // already gone

    inputSystem.SetKeyState(KeyCode::Space, true);
    inputSystem.Update();
    EXPECT_FALSE(inputSystem.IsActionTriggered("Jump"));
    EXPECT_FALSE(inputSystem.IsActionHeld("Jump"));
}

TEST_F(InputSystemTest, ActionRemap_ReplacesBindingsOnNextFrameAndRejectsInvalidCandidateAtomically) {
    inputSystem.RegisterAction(
        InputAction{"Jump", InputActionType::Button, {KeyBinding(KeyCode::Space)}, {}});

    inputSystem.SetKeyState(KeyCode::Space, true);
    inputSystem.Update();
    EXPECT_TRUE(inputSystem.IsActionHeld("Jump"));

    EXPECT_TRUE(inputSystem.RemapAction("Jump", {KeyBinding(KeyCode::Enter)}, {}));
    EXPECT_FALSE(inputSystem.IsActionTriggered("Jump"));
    EXPECT_FALSE(inputSystem.IsActionHeld("Jump"));

    inputSystem.SetKeyState(KeyCode::Space, false);
    inputSystem.SetKeyState(KeyCode::Enter, true);
    inputSystem.Update();
    EXPECT_TRUE(inputSystem.IsActionTriggered("Jump"));
    EXPECT_TRUE(inputSystem.IsActionHeld("Jump"));

    EXPECT_FALSE(inputSystem.RemapAction(
        "Jump", {GamepadButtonBinding(kMaximumGamepadCount, GamepadButton::South)}, {}));
    EXPECT_TRUE(inputSystem.IsActionHeld("Jump"));
    EXPECT_FALSE(inputSystem.RemapAction("Missing", {KeyBinding(KeyCode::A)}, {}));
}

TEST_F(InputSystemTest, KeyboardAndMouseSentinelInputsAreIgnoredSafely) {
    inputSystem.SetKeyState(KeyCode::Count, true);
    inputSystem.SetMouseButtonState(MouseButton::Count, true);
    inputSystem.Update();

    EXPECT_FALSE(inputSystem.IsKeyDown(KeyCode::Count));
    EXPECT_FALSE(inputSystem.WasKeyPressedThisFrame(KeyCode::Count));
    EXPECT_FALSE(inputSystem.WasKeyReleasedThisFrame(KeyCode::Count));
    EXPECT_FALSE(inputSystem.IsMouseButtonDown(MouseButton::Count));
    EXPECT_FALSE(inputSystem.WasMouseButtonPressedThisFrame(MouseButton::Count));
    EXPECT_FALSE(inputSystem.WasMouseButtonReleasedThisFrame(MouseButton::Count));
}

TEST_F(InputSystemTest, InvalidInitialBindingsAreRejectedAtomically) {
    inputSystem.RegisterAction(
        InputAction{"Jump", InputActionType::Button, {KeyBinding(KeyCode::Space)}, {}});
    inputSystem.RegisterAction(
        InputAction{"Invalid", InputActionType::Button, {KeyBinding(KeyCode::Count),
                                                                  MouseBinding(MouseButton::Count)}, {}});

    std::vector<InputBinding> overCapBindings(33U, KeyBinding(KeyCode::Space));
    inputSystem.RegisterAction(
        InputAction{"OverCap", InputActionType::Axis1D, std::move(overCapBindings), {}});

    inputSystem.SetKeyState(KeyCode::Space, true);
    inputSystem.Update();
    EXPECT_TRUE(inputSystem.IsActionHeld("Jump"));
    EXPECT_FALSE(inputSystem.IsActionTriggered("Invalid"));
    EXPECT_FALSE(inputSystem.IsActionHeld("Invalid"));
    EXPECT_NEAR(inputSystem.GetAxisValue("OverCap"), 0.0F, kEpsilon);

    EXPECT_FALSE(inputSystem.RemapAction("Jump", {KeyBinding(KeyCode::Count)}, {}));
    EXPECT_FALSE(inputSystem.RemapAction("Jump", {MouseBinding(MouseButton::Count)}, {}));
    EXPECT_TRUE(inputSystem.IsActionHeld("Jump"));
}

TEST_F(InputSystemTest, ButtonActionTriggered_QueuesAndDeliversInputActionTriggeredEvent) {
    std::string receivedActionName;
    InputActionType receivedType = InputActionType::Axis1D;
    float receivedAxisValue = -1.0F;
    int receivedCount = 0;
    eventSystem.Subscribe<InputActionTriggeredEvent>(
        [&](const InputActionTriggeredEvent& event) {
            receivedActionName = event.actionName;
            receivedType = event.type;
            receivedAxisValue = event.axisValue;
            ++receivedCount;
        });

    inputSystem.RegisterAction(
        InputAction{"Jump", InputActionType::Button, {KeyBinding(KeyCode::Space)}, {}});

    inputSystem.SetKeyState(KeyCode::Space, true);
    inputSystem.Update();
    ASSERT_EQ(receivedCount, 0); // queued, not yet dispatched
    eventSystem.DispatchQueued();

    EXPECT_EQ(receivedCount, 1);
    EXPECT_EQ(receivedActionName, "Jump");
    EXPECT_EQ(receivedType, InputActionType::Button);
    EXPECT_NEAR(receivedAxisValue, 0.0F, kEpsilon);
}

TEST_F(InputSystemTest, AxisActionCrossingThreshold_QueuesCopiedAxisEvent) {
    int receivedCount = 0;
    InputActionType receivedType = InputActionType::Button;
    float receivedAxisValue = 0.0F;
    eventSystem.Subscribe<InputActionTriggeredEvent>(
        [&](const InputActionTriggeredEvent& event) {
            ++receivedCount;
            receivedType = event.type;
            receivedAxisValue = event.axisValue;
        });

    inputSystem.RegisterAction(InputAction{"MoveHorizontal",
                                                   InputActionType::Axis1D,
                                                   {KeyBinding(KeyCode::D)},
                                                   {KeyBinding(KeyCode::A)}});

    inputSystem.SetKeyState(KeyCode::D, true);
    inputSystem.Update();
    EXPECT_EQ(receivedCount, 0); // queued, not yet dispatched
    eventSystem.DispatchQueued();

    EXPECT_EQ(receivedCount, 1);
    EXPECT_EQ(receivedType, InputActionType::Axis1D);
    EXPECT_NEAR(receivedAxisValue, 1.0F, kEpsilon);
}

TEST_F(InputSystemTest, AxisActionBelowThreshold_DoesNotQueueEvent) {
    GamepadInputSystem gamepad{0.0F};
    InputSystem gamepadInputSystem{eventSystem, &gamepad};
    int receivedCount = 0;
    eventSystem.Subscribe<InputActionTriggeredEvent>(
        [&receivedCount](const InputActionTriggeredEvent&) { ++receivedCount; });
    gamepadInputSystem.RegisterAction(InputAction{
        "MoveHorizontal", InputActionType::Axis1D,
        {GamepadAxisBinding(0U, GamepadAxis::LeftX, 1.0F)}, {}});

    gamepad.SetConnected(0U, true);
    gamepad.SetAxisState(0U, GamepadAxis::LeftX, 0.5F);
    gamepad.Update();
    gamepadInputSystem.Update();
    eventSystem.DispatchQueued();

    EXPECT_EQ(receivedCount, 0);
}

TEST_F(InputSystemTest, AxisActionGamepadCrossingThreshold_UsesPreviousAxisSnapshot) {
    GamepadInputSystem gamepad{0.0F};
    InputSystem gamepadInputSystem{eventSystem, &gamepad};
    float receivedAxisValue = 0.0F;
    int receivedCount = 0;
    eventSystem.Subscribe<InputActionTriggeredEvent>(
        [&](const InputActionTriggeredEvent& event) {
            ++receivedCount;
            receivedAxisValue = event.axisValue;
        });
    gamepadInputSystem.RegisterAction(InputAction{
        "MoveHorizontal", InputActionType::Axis1D,
        {GamepadAxisBinding(0U, GamepadAxis::LeftX, 1.0F)}, {}});

    gamepad.SetConnected(0U, true);
    gamepad.SetAxisState(0U, GamepadAxis::LeftX, 0.8F);
    gamepad.Update();
    gamepadInputSystem.Update();
    eventSystem.DispatchQueued();

    EXPECT_EQ(receivedCount, 1);
    EXPECT_NEAR(receivedAxisValue, 0.8F, kEpsilon);
}

} // namespace
} // namespace UVE::Input::Tests
