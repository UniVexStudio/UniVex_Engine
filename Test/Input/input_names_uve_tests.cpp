// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/input/input_names_uve.h"

#include <set>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Input::Tests {
namespace {

TEST(InputNamesUVETest, EveryKeyHasAUniqueNameThatParsesBack) {
    std::set<std::string> seen;
    for (int value = static_cast<int>(KeyCodeUVE::A); value < static_cast<int>(KeyCodeUVE::Count); ++value) {
        const auto key = static_cast<KeyCodeUVE>(value);
        const std::string_view name = GetKeyNameUVE(key);
        ASSERT_FALSE(name.empty()) << value;
        EXPECT_TRUE(seen.insert(std::string(name)).second) << name;
        EXPECT_EQ(ParseKeyNameUVE(name), key) << name;
    }
    EXPECT_EQ(GetKeyNameUVE(KeyCodeUVE::Unknown), "");
    EXPECT_EQ(GetKeyNameUVE(KeyCodeUVE::Count), "");
    EXPECT_FALSE(ParseKeyNameUVE("").has_value());
    EXPECT_FALSE(ParseKeyNameUVE("space").has_value()); // names are exact
    EXPECT_EQ(ParseKeyNameUVE("Space"), KeyCodeUVE::Space);
}

TEST(InputNamesUVETest, ButtonsAxesAndDevicesRoundTrip) {
    for (int value = 0; value < static_cast<int>(MouseButtonUVE::Count); ++value) {
        const auto button = static_cast<MouseButtonUVE>(value);
        EXPECT_EQ(ParseMouseButtonNameUVE(GetMouseButtonNameUVE(button)), button);
    }
    for (int value = 0; value < static_cast<int>(GamepadButtonUVE::Count); ++value) {
        const auto button = static_cast<GamepadButtonUVE>(value);
        EXPECT_EQ(ParseGamepadButtonNameUVE(GetGamepadButtonNameUVE(button)), button);
    }
    for (int value = 0; value < static_cast<int>(GamepadAxisUVE::Count); ++value) {
        const auto axis = static_cast<GamepadAxisUVE>(value);
        EXPECT_EQ(ParseGamepadAxisNameUVE(GetGamepadAxisNameUVE(axis)), axis);
    }
    EXPECT_EQ(ParseInputBindingSourceNameUVE("GamepadAxis"), InputBindingSourceUVE::GamepadAxis);
    EXPECT_FALSE(ParseInputBindingSourceNameUVE("Joystick").has_value());
}

TEST(InputNamesUVETest, BindingsReadTheWayAPersonSaysThem) {
    EXPECT_EQ(DescribeInputBindingUVE(KeyBindingUVE(KeyCodeUVE::Space)), "Space");
    EXPECT_EQ(DescribeInputBindingUVE(MouseBindingUVE(MouseButtonUVE::Right)), "Mouse Right");
    EXPECT_EQ(DescribeInputBindingUVE(GamepadButtonBindingUVE(0U, GamepadButtonUVE::South)), "Pad 1 South");
    EXPECT_EQ(DescribeInputBindingUVE(GamepadAxisBindingUVE(1U, GamepadAxisUVE::LeftX, -1.0F)), "Pad 2 LeftX -");
}

TEST(InputNamesUVETest, SameInputIgnoresScaleButNotStickDirection) {
    EXPECT_TRUE(IsSameInputUVE(KeyBindingUVE(KeyCodeUVE::W), KeyBindingUVE(KeyCodeUVE::W)));
    EXPECT_FALSE(IsSameInputUVE(KeyBindingUVE(KeyCodeUVE::W), KeyBindingUVE(KeyCodeUVE::S)));
    EXPECT_FALSE(IsSameInputUVE(KeyBindingUVE(KeyCodeUVE::A), MouseBindingUVE(MouseButtonUVE::Left)));
    EXPECT_TRUE(IsSameInputUVE(GamepadAxisBindingUVE(0U, GamepadAxisUVE::LeftX, 1.0F),
                               GamepadAxisBindingUVE(0U, GamepadAxisUVE::LeftX, 0.5F)));
    EXPECT_FALSE(IsSameInputUVE(GamepadAxisBindingUVE(0U, GamepadAxisUVE::LeftX, 1.0F),
                                GamepadAxisBindingUVE(0U, GamepadAxisUVE::LeftX, -1.0F)));
    EXPECT_FALSE(IsSameInputUVE(GamepadButtonBindingUVE(0U, GamepadButtonUVE::South),
                                GamepadButtonBindingUVE(1U, GamepadButtonUVE::South)));
}

} // namespace
} // namespace UVE::Input::Tests
