// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/core/input_map_document_uve.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "uve/core/engine_config_uve.h"
#include "uve/core/engine_core_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"

namespace UVE::Core::Tests {
namespace {

using Input::GamepadAxisBindingUVE;
using Input::GamepadAxisUVE;
using Input::GamepadButtonBindingUVE;
using Input::GamepadButtonUVE;
using Input::InputActionTypeUVE;
using Input::InputActionUVE;
using Input::KeyBindingUVE;
using Input::KeyCodeUVE;
using Input::MouseBindingUVE;
using Input::MouseButtonUVE;

std::vector<InputActionUVE> MakeMapUVE() {
    return {
        InputActionUVE{"jump", InputActionTypeUVE::Button,
                       {KeyBindingUVE(KeyCodeUVE::Space), GamepadButtonBindingUVE(0U, GamepadButtonUVE::South)},
                       {}},
        InputActionUVE{"move_x", InputActionTypeUVE::Axis1D,
                       {KeyBindingUVE(KeyCodeUVE::D), GamepadAxisBindingUVE(1U, GamepadAxisUVE::LeftX, 0.5F)},
                       {KeyBindingUVE(KeyCodeUVE::A)}},
        InputActionUVE{"fire", InputActionTypeUVE::Button, {MouseBindingUVE(MouseButtonUVE::Left)}, {}},
    };
}

std::string ReadFileUVE(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::stringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

class InputMapDocumentUVETest : public ::testing::Test {
protected:
    void SetUp() override { std::filesystem::remove(path); }
    void TearDown() override { std::filesystem::remove(path); }
    const std::filesystem::path path = "uve_input_map_document_tests.uveinput";
};

TEST_F(InputMapDocumentUVETest, AMissingFileIsAnEmptyMap) {
    InputMapDocumentUVE document;
    EXPECT_TRUE(document.LoadUVE(path));
    EXPECT_TRUE(document.GetActionsUVE().empty());
    EXPECT_FALSE(document.IsDirtyUVE());
}

TEST_F(InputMapDocumentUVETest, EveryKindOfBindingSurvivesASaveAndReadsAsNames) {
    InputMapDocumentUVE document;
    ASSERT_TRUE(document.LoadUVE(path));
    document.SetActionsUVE(MakeMapUVE());
    EXPECT_TRUE(document.IsDirtyUVE());
    ASSERT_TRUE(document.SaveUVE());
    EXPECT_FALSE(document.IsDirtyUVE());

    const std::string text = ReadFileUVE(path);
    EXPECT_NE(text.find("\"Space\""), std::string::npos);
    EXPECT_NE(text.find("\"South\""), std::string::npos);
    EXPECT_NE(text.find("\"LeftX\""), std::string::npos);

    InputMapDocumentUVE reloaded;
    ASSERT_TRUE(reloaded.LoadUVE(path));
    const std::vector<InputActionUVE>& actions = reloaded.GetActionsUVE();
    ASSERT_EQ(actions.size(), 3U);
    EXPECT_EQ(actions[0].name, "jump");
    ASSERT_EQ(actions[0].positiveBindings.size(), 2U);
    EXPECT_EQ(actions[0].positiveBindings[0].key, KeyCodeUVE::Space);
    EXPECT_EQ(actions[0].positiveBindings[1].gamepadButton, GamepadButtonUVE::South);
    EXPECT_EQ(actions[1].type, InputActionTypeUVE::Axis1D);
    ASSERT_EQ(actions[1].positiveBindings.size(), 2U);
    EXPECT_EQ(actions[1].positiveBindings[1].gamepadIndex, 1U);
    EXPECT_FLOAT_EQ(actions[1].positiveBindings[1].scale, 0.5F);
    ASSERT_EQ(actions[1].negativeBindings.size(), 1U);
    EXPECT_EQ(actions[1].negativeBindings[0].key, KeyCodeUVE::A);
    EXPECT_EQ(actions[2].positiveBindings[0].mouseButton, MouseButtonUVE::Left);
}

TEST_F(InputMapDocumentUVETest, AHandEditedMistakeCostsOnlyWhatItTouches) {
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
    InputMapDocumentUVE document;
    ASSERT_TRUE(document.LoadUVE(path));
    ASSERT_EQ(document.GetActionsUVE().size(), 1U);
    ASSERT_EQ(document.GetActionsUVE()[0].positiveBindings.size(), 1U);
    EXPECT_EQ(document.GetActionsUVE()[0].positiveBindings[0].key, KeyCodeUVE::Space);
}

TEST_F(InputMapDocumentUVETest, AFileThatDoesNotParseKeepsTheMap) {
    InputMapDocumentUVE document;
    ASSERT_TRUE(document.LoadUVE(path));
    document.SetActionsUVE(MakeMapUVE());
    {
        std::ofstream file(path);
        file << "{ not json";
    }
    EXPECT_FALSE(document.LoadUVE(path));
    EXPECT_EQ(document.GetActionsUVE().size(), 3U);
}

TEST(InputMapDocumentSanitizeUVETest, DropsWhatTheInputSystemWouldRefuse) {
    std::vector<InputActionUVE> actions = MakeMapUVE();
    actions.push_back(InputActionUVE{"jump", InputActionTypeUVE::Button, {}, {}});           // repeated name
    actions.push_back(InputActionUVE{std::string(200, 'x'), InputActionTypeUVE::Button, {}, {}}); // too long
    actions[0].negativeBindings = {KeyBindingUVE(KeyCodeUVE::S)};                             // a button has no negative side
    actions[2].positiveBindings.push_back(KeyBindingUVE(KeyCodeUVE::Unknown));
    const std::vector<InputActionUVE> kept = InputMapDocumentUVE::SanitizeUVE(actions);
    ASSERT_EQ(kept.size(), 3U);
    EXPECT_TRUE(kept[0].negativeBindings.empty());
    EXPECT_EQ(kept[2].positiveBindings.size(), 1U);
}

TEST(InputMapDocumentApplyUVETest, RegistersTheMapAndForgetsWhatItDropped) {
    Events::EventSystemUVE events;
    Input::InputSystemUVE input(events);
    InputMapDocumentUVE document;
    document.SetActionsUVE(MakeMapUVE());
    document.ApplyUVE(input);
    // RemapActionUVE succeeds only for a registered action.
    EXPECT_TRUE(input.RemapActionUVE("jump", {}, {}));
    EXPECT_TRUE(input.RemapActionUVE("move_x", {}, {}));

    std::vector<InputActionUVE> fewer = MakeMapUVE();
    fewer.erase(fewer.begin());
    document.SetActionsUVE(fewer);
    document.ApplyUVE(input);
    EXPECT_FALSE(input.RemapActionUVE("jump", {}, {}));
    EXPECT_TRUE(input.RemapActionUVE("fire", {}, {}));
}

TEST_F(InputMapDocumentUVETest, TheEngineRegistersTheProjectsActionsAtStartup) {
    {
        InputMapDocumentUVE document;
        ASSERT_TRUE(document.LoadUVE(path));
        document.SetActionsUVE(MakeMapUVE());
        ASSERT_TRUE(document.SaveUVE());
    }
    EngineConfigUVE config{};
    config.headlessUVE = true;
    config.enableConsoleLogging = false;
    config.logFilePath = "uve_input_map_document_tests.log";
    config.settingsFilePath = "uve_input_map_document_tests.uvesettings";
    config.projectSettingsFilePath = "uve_input_map_document_tests.project.uvesettings";
    config.assetDatabaseFilePath = "uve_input_map_document_tests.uveassetdb";
    config.inputMapFilePath = path;
    EngineCoreUVE engine(config);
    engine.Init();
    EXPECT_EQ(engine.GetServicesUVE().GetInputMapUVE().GetActionsUVE().size(), 3U);
    EXPECT_TRUE(engine.GetServicesUVE().GetInputSystemUVE().RemapActionUVE("jump", {}, {}));
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Core::Tests
