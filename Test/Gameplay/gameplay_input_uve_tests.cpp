// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/gameplay/gameplay_input_uve.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/input/key_code_uve.h"

namespace UVE::Gameplay::Tests {
namespace {

TEST(GameplayInputUVETest, DefaultsIncludeMoveJumpInteractAndLook) {
    const std::vector<Input::InputActionUVE> actions = MakeDefaultGameplayActionsUVE();
    ASSERT_EQ(actions.size(), 6U);
    EXPECT_EQ(actions[0].name, kMoveHorizontalActionUVE);
    EXPECT_EQ(actions[2].name, kJumpActionUVE);
    EXPECT_EQ(actions[3].name, kInteractActionUVE);
}

TEST(GameplayInputUVETest, RegisterDefaultDoesNotReplaceAnExistingAction) {
    Events::EventSystemUVE events;
    Input::InputSystemUVE input(events);
    input.RegisterActionUVE(Input::InputActionUVE{
        std::string{kJumpActionUVE}, Input::InputActionTypeUVE::Button,
        {Input::KeyBindingUVE(Input::KeyCodeUVE::Enter)}, {}});
    RegisterDefaultGameplayActionsUVE(input);
    EXPECT_TRUE(input.HasActionUVE(kJumpActionUVE));
    EXPECT_TRUE(input.HasActionUVE(kMoveHorizontalActionUVE));
    input.SetKeyStateUVE(Input::KeyCodeUVE::Enter, true);
    input.UpdateUVE();
    EXPECT_TRUE(input.IsActionHeldUVE(kJumpActionUVE));
    input.SetKeyStateUVE(Input::KeyCodeUVE::Enter, false);
    input.SetKeyStateUVE(Input::KeyCodeUVE::Space, true);
    input.UpdateUVE();
    EXPECT_FALSE(input.IsActionHeldUVE(kJumpActionUVE));
}

TEST(GameplayInputUVETest, CollectReadsWasdJumpAndInteract) {
    Events::EventSystemUVE events;
    Input::InputSystemUVE input(events);
    RegisterDefaultGameplayActionsUVE(input);
    input.SetKeyStateUVE(Input::KeyCodeUVE::W, true);
    input.SetKeyStateUVE(Input::KeyCodeUVE::D, true);
    input.SetKeyStateUVE(Input::KeyCodeUVE::Space, true);
    input.SetKeyStateUVE(Input::KeyCodeUVE::E, true);
    input.UpdateUVE();
    const GameplayInputUVE frame = CollectGameplayInputUVE(input);
    EXPECT_GT(frame.move.x, 0.0F);
    EXPECT_LT(frame.move.z, 0.0F);
    EXPECT_TRUE(frame.jumpPressed);
    EXPECT_TRUE(frame.interactPressed);
    EXPECT_FLOAT_EQ(frame.rise, 1.0F);
}

} // namespace
} // namespace UVE::Gameplay::Tests
