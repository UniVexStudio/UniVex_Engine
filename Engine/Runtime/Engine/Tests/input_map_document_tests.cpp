// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "input_map_document.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "engine_config.h"
#include "engine_core.h"
#include "event_system.h"
#include "input_system.h"

namespace UVE::Core::Tests {
namespace {

using Input::GamepadAxisBinding;
using Input::GamepadAxis;
using Input::GamepadButtonBinding;
using Input::GamepadButton;
using Input::InputActionType;
using Input::InputAction;
using Input::KeyBinding;
using Input::KeyCode;
using Input::MouseBinding;
using Input::MouseButton;

std::vector<InputAction> MakeMap() {
    return {
        InputAction{"jump", InputActionType::Button,
                       {KeyBinding(KeyCode::Space), GamepadButtonBinding(0U, GamepadButton::South)},
                       {}},
        InputAction{"move_x", InputActionType::Axis1D,
                       {KeyBinding(KeyCode::D), GamepadAxisBinding(1U, GamepadAxis::LeftX, 0.5F)},
                       {KeyBinding(KeyCode::A)}},
        InputAction{"fire", InputActionType::Button, {MouseBinding(MouseButton::Left)}, {}},
    };
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::stringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

class InputMapDocumentTest : public ::testing::Test {
protected:
    void SetUp() override { std::filesystem::remove(path); }
    void TearDown() override { std::filesystem::remove(path); }
    const std::filesystem::path path = "uve_input_map_document_tests.uvinput";
};

TEST_F(InputMapDocumentTest, AMissingFileIsAnEmptyMap) {
    InputMapDocument document;
    EXPECT_TRUE(document.Load(path));
    EXPECT_TRUE(document.GetActions().empty());
    EXPECT_FALSE(document.IsDirty());
}

TEST_F(InputMapDocumentTest, EveryKindOfBindingSurvivesASaveAndReadsAsNames) {
    InputMapDocument document;
    ASSERT_TRUE(document.Load(path));
    document.SetActions(MakeMap());
    EXPECT_TRUE(document.IsDirty());
    ASSERT_TRUE(document.Save());
    EXPECT_FALSE(document.IsDirty());

    const std::string text = ReadFile(path);
    EXPECT_NE(text.find("\"Space\""), std::string::npos);
    EXPECT_NE(text.find("\"South\""), std::string::npos);
    EXPECT_NE(text.find("\"LeftX\""), std::string::npos);

    InputMapDocument reloaded;
    ASSERT_TRUE(reloaded.Load(path));
    const std::vector<InputAction>& actions = reloaded.GetActions();
    ASSERT_EQ(actions.size(), 3U);
    EXPECT_EQ(actions[0].name, "jump");
    ASSERT_EQ(actions[0].positiveBindings.size(), 2U);
    EXPECT_EQ(actions[0].positiveBindings[0].key, KeyCode::Space);
    EXPECT_EQ(actions[0].positiveBindings[1].gamepadButton, GamepadButton::South);
    EXPECT_EQ(actions[1].type, InputActionType::Axis1D);
    ASSERT_EQ(actions[1].positiveBindings.size(), 2U);
    EXPECT_EQ(actions[1].positiveBindings[1].gamepadIndex, 1U);
    EXPECT_FLOAT_EQ(actions[1].positiveBindings[1].scale, 0.5F);
    ASSERT_EQ(actions[1].negativeBindings.size(), 1U);
    EXPECT_EQ(actions[1].negativeBindings[0].key, KeyCode::A);
    EXPECT_EQ(actions[2].positiveBindings[0].mouseButton, MouseButton::Left);
}

TEST_F(InputMapDocumentTest, AHandEditedMistakeCostsOnlyWhatItTouches) {
    {
        std::ofstream file(path);
        file << R"({"actions": {"count": 4,
            "0": {"name": "jump", "type": "Button", "positive": {"count": 3,
                  "0": {"device": "Keyboard", "key": "Space"},
                  "1": {"device": "Keyboard", "key": "Spcae"},
                  "2": {"device": "GamepadButton", "pad": 9, "button": "South"}}},
            "1": {"name": "", "type": "Button"},
            "2": {"name": "jump", "type": "Button"},
            "3": {"name": "look", "type": "Sideways"}}})";
    }
    InputMapDocument document;
    ASSERT_TRUE(document.Load(path));
    ASSERT_EQ(document.GetActions().size(), 1U);
    ASSERT_EQ(document.GetActions()[0].positiveBindings.size(), 1U);
    EXPECT_EQ(document.GetActions()[0].positiveBindings[0].key, KeyCode::Space);
}

TEST_F(InputMapDocumentTest, AFileThatDoesNotParseKeepsTheMap) {
    InputMapDocument document;
    ASSERT_TRUE(document.Load(path));
    document.SetActions(MakeMap());
    {
        std::ofstream file(path);
        file << "{ not json";
    }
    EXPECT_FALSE(document.Load(path));
    EXPECT_EQ(document.GetActions().size(), 3U);
}

TEST(InputMapDocumentSanitizeTest, DropsWhatTheInputSystemWouldRefuse) {
    std::vector<InputAction> actions = MakeMap();
    actions.push_back(InputAction{"jump", InputActionType::Button, {}, {}});           // repeated name
    actions.push_back(InputAction{std::string(200, 'x'), InputActionType::Button, {}, {}}); // too long
    actions[0].negativeBindings = {KeyBinding(KeyCode::S)};                             // a button has no negative side
    actions[2].positiveBindings.push_back(KeyBinding(KeyCode::Unknown));
    const std::vector<InputAction> kept = InputMapDocument::Sanitize(actions);
    ASSERT_EQ(kept.size(), 3U);
    EXPECT_TRUE(kept[0].negativeBindings.empty());
    EXPECT_EQ(kept[2].positiveBindings.size(), 1U);
}

TEST(InputMapDocumentApplyTest, RegistersTheMapAndForgetsWhatItDropped) {
    Events::EventSystem events;
    Input::InputSystem input(events);
    InputMapDocument document;
    document.SetActions(MakeMap());
    document.Apply(input);
    // RemapAction succeeds only for a registered action.
    EXPECT_TRUE(input.RemapAction("jump", {}, {}));
    EXPECT_TRUE(input.RemapAction("move_x", {}, {}));

    std::vector<InputAction> fewer = MakeMap();
    fewer.erase(fewer.begin());
    document.SetActions(fewer);
    document.Apply(input);
    EXPECT_FALSE(input.RemapAction("jump", {}, {}));
    EXPECT_TRUE(input.RemapAction("fire", {}, {}));
}

TEST_F(InputMapDocumentTest, TheEngineRegistersTheProjectsActionsAtStartup) {
    {
        InputMapDocument document;
        ASSERT_TRUE(document.Load(path));
        document.SetActions(MakeMap());
        ASSERT_TRUE(document.Save());
    }
    EngineConfig config{};
    config.headless = true;
    config.enableConsoleLogging = false;
    config.logFilePath = "uve_input_map_document_tests.log";
    config.settingsFilePath = "uve_input_map_document_tests.uvsettings";
    config.projectSettingsFilePath = "uve_input_map_document_tests.project.uvsettings";
    config.assetDatabaseFilePath = "uve_input_map_document_tests.uvassetdb";
    config.inputMapFilePath = path;
    EngineCore engine(config);
    engine.Init();
    EXPECT_EQ(engine.GetServices().GetInputMap().GetActions().size(), 3U);
    EXPECT_TRUE(engine.GetServices().GetInputSystem().RemapAction("jump", {}, {}));
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Core::Tests
