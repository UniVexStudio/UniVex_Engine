// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "input_names.h"

#include <set>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Input::Tests {
namespace {

TEST(InputNamesTest, EveryKeyHasAUniqueNameThatParsesBack) {
    std::set<std::string> seen;
    for (int value = static_cast<int>(KeyCode::A); value < static_cast<int>(KeyCode::Count); ++value) {
        const auto key = static_cast<KeyCode>(value);
        const std::string_view name = GetKeyName(key);
        ASSERT_FALSE(name.empty()) << value;
        EXPECT_TRUE(seen.insert(std::string(name)).second) << name;
        EXPECT_EQ(ParseKeyName(name), key) << name;
    }
    EXPECT_EQ(GetKeyName(KeyCode::Unknown), "");
    EXPECT_EQ(GetKeyName(KeyCode::Count), "");
    EXPECT_FALSE(ParseKeyName("").has_value());
    EXPECT_FALSE(ParseKeyName("space").has_value()); // names are exact
    EXPECT_EQ(ParseKeyName("Space"), KeyCode::Space);
}

TEST(InputNamesTest, ButtonsAxesAndDevicesRoundTrip) {
    for (int value = 0; value < static_cast<int>(MouseButton::Count); ++value) {
        const auto button = static_cast<MouseButton>(value);
        EXPECT_EQ(ParseMouseButtonName(GetMouseButtonName(button)), button);
    }
    for (int value = 0; value < static_cast<int>(GamepadButton::Count); ++value) {
        const auto button = static_cast<GamepadButton>(value);
        EXPECT_EQ(ParseGamepadButtonName(GetGamepadButtonName(button)), button);
    }
    for (int value = 0; value < static_cast<int>(GamepadAxis::Count); ++value) {
        const auto axis = static_cast<GamepadAxis>(value);
        EXPECT_EQ(ParseGamepadAxisName(GetGamepadAxisName(axis)), axis);
    }
    EXPECT_EQ(ParseInputBindingSourceName("GamepadAxis"), InputBindingSource::GamepadAxis);
    EXPECT_FALSE(ParseInputBindingSourceName("Joystick").has_value());
}

TEST(InputNamesTest, BindingsReadTheWayAPersonSaysThem) {
    EXPECT_EQ(DescribeInputBinding(KeyBinding(KeyCode::Space)), "Space");
    EXPECT_EQ(DescribeInputBinding(MouseBinding(MouseButton::Right)), "Mouse Right");
    EXPECT_EQ(DescribeInputBinding(GamepadButtonBinding(0U, GamepadButton::South)), "Pad 1 South");
    EXPECT_EQ(DescribeInputBinding(GamepadAxisBinding(1U, GamepadAxis::LeftX, -1.0F)), "Pad 2 LeftX -");
}

TEST(InputNamesTest, SameInputIgnoresScaleButNotStickDirection) {
    EXPECT_TRUE(IsSameInput(KeyBinding(KeyCode::W), KeyBinding(KeyCode::W)));
    EXPECT_FALSE(IsSameInput(KeyBinding(KeyCode::W), KeyBinding(KeyCode::S)));
    EXPECT_FALSE(IsSameInput(KeyBinding(KeyCode::A), MouseBinding(MouseButton::Left)));
    EXPECT_TRUE(IsSameInput(GamepadAxisBinding(0U, GamepadAxis::LeftX, 1.0F),
                               GamepadAxisBinding(0U, GamepadAxis::LeftX, 0.5F)));
    EXPECT_FALSE(IsSameInput(GamepadAxisBinding(0U, GamepadAxis::LeftX, 1.0F),
                                GamepadAxisBinding(0U, GamepadAxis::LeftX, -1.0F)));
    EXPECT_FALSE(IsSameInput(GamepadButtonBinding(0U, GamepadButton::South),
                                GamepadButtonBinding(1U, GamepadButton::South)));
}

} // namespace
} // namespace UVE::Input::Tests
