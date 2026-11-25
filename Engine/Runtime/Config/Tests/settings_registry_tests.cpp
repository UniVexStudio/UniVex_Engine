// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "settings_registry.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "config_manager.h"
#include "settings_document.h"
#include "settings_stack.h"

namespace UVE::Config::Tests {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfinity = std::numeric_limits<double>::infinity();

SettingDescriptor MakeQuality() {
    return MakeEnumSetting("render.quality", 1, {{0, "Low"}, {1, "Medium"}, {2, "High"}}, "Quality", "Rendering");
}

/// One registry holding one setting of every type, the way a module declares its own.
class SettingsRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(registry.Register(MakeBoolSetting("editor.grid.visible", true, "Show Grid", "Editor/Grid")));
        ASSERT_TRUE(registry.Register(
            MakeIntSetting("editor.autosave.minutes", 5, 1, 120, "Autosave Interval", "Editor/General")));
        ASSERT_TRUE(registry.Register(
            MakeFloatSetting("editor.grid.opacity", 0.5, 0.0, 1.0, "Grid Opacity", "Editor/Grid")));
        ASSERT_TRUE(
            registry.Register(MakeStringSetting("editor.layout.name", "Default", 16, "Layout", "Editor/Layout")));
        ASSERT_TRUE(registry.Register(MakeQuality()));
        ASSERT_TRUE(registry.Register(
            MakeColorSetting("editor.outline.color", {1.0F, 0.5F, 0.25F}, false, "Outline", "Editor/Viewport")));
        ASSERT_TRUE(registry.Register(
            MakeColorSetting("editor.clear.color", {0.1F, 0.2F, 0.3F, 0.4F}, true, "Clear", "Editor/Viewport")));
    }

    SettingsRegistry registry;
    ConfigManager store;
};

TEST(SettingsStackTest, PrecedenceProvenanceAndInvalidValuesFallThrough) {
    SettingsRegistry registry;
    SettingDescriptor platformValue =
        MakeIntSetting("settings.platformValue", 5, 1, 120, "Platform Value", "Test");
    platformValue.flags |= kSettingFlagPerPlatform;
    ASSERT_TRUE(registry.Register(std::move(platformValue)));
    SettingDescriptor sessionValue = MakeBoolSetting("settings.sessionValue", false, "Session Value", "Test");
    sessionValue.flags |= kSettingFlagNotPersisted;
    ASSERT_TRUE(registry.Register(std::move(sessionValue)));
    ASSERT_TRUE(registry.Register(MakeIntSetting("settings.nonPlatformValue", 7, 1, 120,
                                                       "Non-Platform Value", "Test")));

    ConfigManager project;
    ConfigManager user;
    ConfigManager platform;
    ConfigManager commandLine;
    SettingsStack stack(registry);

    EXPECT_FALSE(stack.AttachLayer(SettingValueSource::EngineDefault, project));
    ASSERT_TRUE(stack.AttachLayer(SettingValueSource::Project, project));
    ASSERT_TRUE(stack.AttachLayer(SettingValueSource::User, user));
    ASSERT_TRUE(stack.AttachLayer(SettingValueSource::Platform, platform));
    ASSERT_TRUE(stack.AttachLayer(SettingValueSource::CommandLine, commandLine));

    constexpr std::string_view kSettingId = "settings.platformValue";
    auto resolved = stack.Resolve(kSettingId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 5);
    EXPECT_EQ(resolved->source, SettingValueSource::EngineDefault);

    project.SetInt(kSettingId, 10);
    user.SetInt(kSettingId, 20);
    platform.SetInt(kSettingId, 30);
    commandLine.SetInt(kSettingId, 40);
    resolved = stack.Resolve(kSettingId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 40);
    EXPECT_EQ(resolved->source, SettingValueSource::CommandLine);

    // A bad high-priority value is absent, not a reason to mask lower layers.
    commandLine.SetInt(kSettingId, 500);
    EXPECT_FALSE(stack.GetStoredValue(SettingValueSource::CommandLine, kSettingId).has_value());
    resolved = stack.Resolve(kSettingId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 30);
    EXPECT_EQ(resolved->source, SettingValueSource::Platform);

    commandLine.SetString(kSettingId, "not an integer");
    EXPECT_FALSE(stack.GetStoredValue(SettingValueSource::CommandLine, kSettingId).has_value());
    resolved = stack.Resolve(kSettingId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 30);
    EXPECT_EQ(resolved->source, SettingValueSource::Platform);

    platform.SetInt("settings.nonPlatformValue", 99);
    resolved = stack.Resolve("settings.nonPlatformValue");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 7);
    EXPECT_EQ(resolved->source, SettingValueSource::EngineDefault);
    EXPECT_FALSE(stack.GetStoredValue(SettingValueSource::Platform,
                                        "settings.nonPlatformValue").has_value());

    project.SetBool("settings.sessionValue", true);
    user.SetBool("settings.sessionValue", true);
    platform.SetBool("settings.sessionValue", true);
    resolved = stack.Resolve("settings.sessionValue");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_FALSE(std::get<bool>(resolved->value));
    EXPECT_EQ(resolved->source, SettingValueSource::EngineDefault);
    commandLine.SetBool("settings.sessionValue", true);
    resolved = stack.Resolve("settings.sessionValue");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_TRUE(std::get<bool>(resolved->value));
    EXPECT_EQ(resolved->source, SettingValueSource::CommandLine);

    ASSERT_TRUE(stack.DetachLayer(SettingValueSource::Platform));
    resolved = stack.Resolve(kSettingId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 20);
    EXPECT_EQ(resolved->source, SettingValueSource::User);

    ASSERT_TRUE(stack.DetachLayer(SettingValueSource::User));
    resolved = stack.Resolve(kSettingId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 10);
    EXPECT_EQ(resolved->source, SettingValueSource::Project);

    ASSERT_TRUE(stack.DetachLayer(SettingValueSource::Project));
    resolved = stack.Resolve(kSettingId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 5);
    EXPECT_EQ(resolved->source, SettingValueSource::EngineDefault);
    EXPECT_FALSE(stack.Resolve("settings.unknown").has_value());
    EXPECT_FALSE(stack.DetachLayer(SettingValueSource::EngineDefault));
}

TEST(SettingDescriptorTest, ValidDescriptorsOfEveryTypeHaveNoProblem) {
    EXPECT_EQ(ValidateSettingDescriptor(MakeBoolSetting("a.b", false, "A", "Cat")), "");
    EXPECT_EQ(ValidateSettingDescriptor(MakeIntSetting("a.i", 0, std::nullopt, std::nullopt, "I", "")), "");
    EXPECT_EQ(ValidateSettingDescriptor(MakeFloatSetting("a.f", 2.0, 1.0, 3.0, "F", "Cat/Sub Page")), "");
    EXPECT_EQ(ValidateSettingDescriptor(MakeStringSetting("a.s", "abc", 3, "S", "Cat")), "");
    EXPECT_EQ(ValidateSettingDescriptor(MakeStringListSetting("a.list", {"first", "second"}, 3, 16,
                                                                    "List", "Cat")), "");
    EXPECT_EQ(ValidateSettingDescriptor(MakeFilePathSetting("a.path", "Assets/Scene.uvscene", 256,
                                                                  "Scene", "Project")), "");
    EXPECT_EQ(ValidateSettingDescriptor(MakeKeyBindingSetting("a.shortcut", "Ctrl+Shift+S",
                                                                     "Save", "Editor/Shortcuts")), "");
    EXPECT_EQ(ValidateSettingDescriptor(MakeQuality()), "");
    EXPECT_EQ(ValidateSettingDescriptor(MakeColorSetting("a.c", {0.0F, 1.0F, 0.5F}, false, "C", "Cat")), "");
}

TEST(SettingDescriptorTest, MalformedIdsAreRejected) {
    for (const char* id : {"", ".a", "a.", "a..b", "a b", "a/b", "a-b"}) {
        EXPECT_NE(ValidateSettingDescriptor(MakeBoolSetting(id, false, "A", "Cat")), "") << "id: " << id;
    }
    EXPECT_EQ(ValidateSettingDescriptor(MakeBoolSetting("editor_2.grid_visible", false, "A", "")), "");
}

TEST(SettingDescriptorTest, DocumentVersionKeyIsReservedAndReplacementIdsRequireDeprecatedAliases) {
    EXPECT_NE(ValidateSettingDescriptor(MakeBoolSetting("version", false, "Version", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeBoolSetting("version.settings", false, "Version", "")), "");

    SettingDescriptor live = MakeIntSetting("settings.current", 1, 0, 10, "Current", "");
    live.replacementId = "settings.other";
    EXPECT_NE(ValidateSettingDescriptor(live), "");

    SettingDescriptor alias = MakeIntSetting("settings.old", 1, 0, 10, "Old", "");
    alias.flags |= kSettingFlagDeprecated;
    alias.replacementId = "settings.current";
    EXPECT_EQ(ValidateSettingDescriptor(alias), "");
    alias.replacementId = alias.id;
    EXPECT_NE(ValidateSettingDescriptor(alias), "");

    SettingsRegistry registry;
    live.replacementId.clear();
    alias.replacementId = "settings.current";
    EXPECT_FALSE(registry.Register(alias)); // The live target must be registered first.
    ASSERT_TRUE(registry.Register(std::move(live)));
    EXPECT_TRUE(registry.Register(std::move(alias)));
}

TEST(SettingDescriptorTest, MalformedCategoriesAreRejected) {
    for (const char* category : {"/Editor", "Editor/", "Editor//Grid", "Editor.Grid"}) {
        EXPECT_NE(ValidateSettingDescriptor(MakeBoolSetting("a.b", false, "A", category)), "")
            << "category: " << category;
    }
}

TEST(SettingDescriptorTest, DefaultMustSatisfyItsOwnConstraints) {
    EXPECT_NE(ValidateSettingDescriptor(MakeIntSetting("a.i", 0, 1, 10, "I", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeFloatSetting("a.f", 11.0, 1.0, 10.0, "F", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeFloatSetting("a.f", kNaN, std::nullopt, std::nullopt, "F", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeStringSetting("a.s", "abcd", 3, "S", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeStringListSetting("a.list", {"one", "two"}, 1, 8,
                                                                     "List", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeStringListSetting("a.list", {"too-long"}, 2, 3,
                                                                     "List", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeFilePathSetting("a.path", std::string(5, 'x'), 4,
                                                                  "Path", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeKeyBindingSetting("a.shortcut", "Ctrl+Hyper", "Shortcut", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeKeyBindingSetting("a.shortcut", "Ctrl+Ctrl+S", "Shortcut", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeEnumSetting("a.e", 7, {{0, "Zero"}}, "E", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeColorSetting("a.c", {1.5F, 0.0F, 0.0F}, false, "C", "")), "");

    SettingDescriptor wrongAlternative = MakeBoolSetting("a.b", false, "B", "");
    wrongAlternative.defaultValue = std::int64_t{1};
    EXPECT_NE(ValidateSettingDescriptor(wrongAlternative), "");
}

TEST(SettingDescriptorTest, StringListRequiresABoundedItemCount) {
    SettingDescriptor noItemLimit = MakeStringListSetting("a.list", {}, 0U, 0U, "List", "");
    EXPECT_NE(ValidateSettingDescriptor(noItemLimit), "");

    SettingDescriptor excessiveLimit = MakeStringListSetting(
        "a.list", {}, kMaximumSettingStringListItems + 1U, 0U, "List", "");
    EXPECT_NE(ValidateSettingDescriptor(excessiveLimit), "");

    SettingDescriptor notAList = MakeBoolSetting("a.flag", false, "Flag", "");
    notAList.maxItems = 1U;
    EXPECT_NE(ValidateSettingDescriptor(notAList), "");
}

TEST(SettingDescriptorTest, BoundsAndStepMustBeCoherent) {
    EXPECT_NE(ValidateSettingDescriptor(MakeFloatSetting("a.f", 1.0, 2.0, 0.0, "F", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeFloatSetting("a.f", 1.0, -kInfinity, 2.0, "F", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeFloatSetting("a.f", 1.0, 0.0, kNaN, "F", "")), "");

    SettingDescriptor zeroStep = MakeFloatSetting("a.f", 1.0, 0.0, 2.0, "F", "");
    zeroStep.step = 0.0;
    EXPECT_NE(ValidateSettingDescriptor(zeroStep), "");

    SettingDescriptor boundedBool = MakeBoolSetting("a.b", false, "B", "");
    boundedBool.maximum = 1.0;
    EXPECT_NE(ValidateSettingDescriptor(boundedBool), "");
}

TEST(SettingDescriptorTest, EnumEntriesMustBePresentLabelledAndDistinct) {
    EXPECT_NE(ValidateSettingDescriptor(MakeEnumSetting("a.e", 0, {}, "E", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeEnumSetting("a.e", 0, {{0, ""}}, "E", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeEnumSetting("a.e", 0, {{0, "A"}, {0, "B"}}, "E", "")), "");
    EXPECT_NE(ValidateSettingDescriptor(MakeEnumSetting("a.e", 0, {{0, "A"}, {1, "A"}}, "E", "")), "");

    SettingDescriptor intWithEntries = MakeIntSetting("a.i", 0, std::nullopt, std::nullopt, "I", "");
    intWithEntries.enumEntries = {{0, "Zero"}};
    EXPECT_NE(ValidateSettingDescriptor(intWithEntries), "");
}

TEST(SettingDescriptorTest, NonFiniteValuesAreNeverLegal) {
    const SettingDescriptor unbounded = MakeFloatSetting("a.f", 0.0, std::nullopt, std::nullopt, "F", "");
    EXPECT_TRUE(IsSettingValueValid(unbounded, 1.0e300));
    EXPECT_FALSE(IsSettingValueValid(unbounded, kNaN));
    EXPECT_FALSE(IsSettingValueValid(unbounded, kInfinity));
    EXPECT_FALSE(IsSettingValueValid(unbounded, -kInfinity));

    const SettingDescriptor color = MakeColorSetting("a.c", {}, true, "C", "");
    EXPECT_FALSE(IsSettingValueValid(color, SettingColor{0.0F, std::nanf(""), 0.0F, 1.0F}));
    EXPECT_FALSE(IsSettingValueValid(color, SettingColor{0.0F, 0.0F, 0.0F, std::nanf("")}));
}

TEST_F(SettingsRegistryTest, KeepsRegistrationOrderAndFindsById) {
    const auto all = registry.GetAll();
    ASSERT_EQ(all.size(), registry.GetCount());
    ASSERT_EQ(all.size(), 7U);
    EXPECT_EQ(all.front()->id, "editor.grid.visible");
    EXPECT_EQ(all.back()->id, "editor.clear.color");
    ASSERT_NE(registry.Find("render.quality"), nullptr);
    EXPECT_EQ(registry.Find("render.quality")->displayName, "Quality");
    EXPECT_EQ(registry.Find("render.missing"), nullptr);
}

TEST_F(SettingsRegistryTest, RefusesDuplicatesMalformedAndNestedIds) {
    EXPECT_FALSE(registry.Register(MakeBoolSetting("editor.grid.visible", false, "Again", "")));
    EXPECT_FALSE(registry.Register(MakeIntSetting("a.i", 0, 1, 10, "Bad default", "")));
    // Under a registered value, and above one.
    EXPECT_FALSE(registry.Register(MakeBoolSetting("editor.grid.visible.extra", false, "Child", "")));
    EXPECT_FALSE(registry.Register(MakeBoolSetting("editor.grid", false, "Parent", "")));
    // A colour's channels are its own, alpha included even when it has none...
    EXPECT_FALSE(registry.Register(MakeFloatSetting("editor.outline.color.r", 0.0, 0.0, 1.0, "Red", "")));
    EXPECT_FALSE(registry.Register(MakeFloatSetting("editor.outline.color.a", 0.0, 0.0, 1.0, "Alpha", "")));
    // ...and a colour cannot sit where a value or another setting's object already is.
    EXPECT_FALSE(registry.Register(MakeColorSetting("editor.grid.visible", {}, false, "Under a value", "")));
    ASSERT_TRUE(registry.Register(MakeBoolSetting("editor.tint.r.locked", false, "Locked", "")));
    EXPECT_FALSE(registry.Register(MakeColorSetting("editor.tint", {}, false, "Channel is an object", "")));
    EXPECT_EQ(registry.GetCount(), 8U);
    // A sibling is fine, and so is a setting beside a colour's channels.
    EXPECT_TRUE(registry.Register(MakeFloatSetting("editor.grid.fade", 10.0, 0.0, 100.0, "Fade", "")));
    EXPECT_TRUE(registry.Register(MakeFloatSetting("editor.outline.color.width", 2.0, 1.0, 6.0, "Width", "")));
    EXPECT_TRUE(registry.Register(MakeBoolSetting("editor.outline.color.visible", true, "Visible", "")));
}

TEST_F(SettingsRegistryTest, EveryRegisteredDefaultIsLegalAndReadsBackUnmodified) {
    for (const SettingDescriptor* descriptor : registry.GetAll()) {
        EXPECT_EQ(ValidateSettingDescriptor(*descriptor), "") << descriptor->id;
        EXPECT_EQ(registry.GetValue(store, descriptor->id), descriptor->defaultValue) << descriptor->id;
        EXPECT_FALSE(registry.IsModified(store, descriptor->id)) << descriptor->id;
    }
}

TEST_F(SettingsRegistryTest, TypedGettersReturnStoredValues) {
    ASSERT_TRUE(registry.SetValue(store, "editor.grid.visible", false));
    ASSERT_TRUE(registry.SetValue(store, "editor.autosave.minutes", std::int64_t{30}));
    ASSERT_TRUE(registry.SetValue(store, "editor.grid.opacity", 0.75));
    ASSERT_TRUE(registry.SetValue(store, "editor.layout.name", std::string("Wide")));
    ASSERT_TRUE(registry.SetValue(store, "render.quality", std::int64_t{2}));
    ASSERT_TRUE(registry.SetValue(store, "editor.clear.color", SettingColor{0.0F, 0.5F, 1.0F, 0.25F}));

    EXPECT_FALSE(registry.GetBool(store, "editor.grid.visible", true));
    EXPECT_EQ(registry.GetInt(store, "editor.autosave.minutes"), 30);
    EXPECT_DOUBLE_EQ(registry.GetFloat(store, "editor.grid.opacity"), 0.75);
    EXPECT_EQ(registry.GetString(store, "editor.layout.name"), "Wide");
    EXPECT_EQ(registry.GetInt(store, "render.quality"), 2);
    EXPECT_EQ(registry.GetColor(store, "editor.clear.color"), (SettingColor{0.0F, 0.5F, 1.0F, 0.25F}));
    EXPECT_TRUE(registry.IsModified(store, "render.quality"));

    // The store holds them where the rest of the engine already looks.
    EXPECT_EQ(store.GetInt("editor.autosave.minutes", 0), 30);
    EXPECT_DOUBLE_EQ(store.GetDouble("editor.clear.color.a", 0.0), 0.25);
}

TEST_F(SettingsRegistryTest, FilePathAndKeyBindingUseTextStorageWithTypeSpecificValidation) {
    ASSERT_TRUE(registry.Register(MakeFilePathSetting("application.iconPath", "Assets/icon.png", 256,
                                                            "Application Icon", "Application")));
    ASSERT_TRUE(registry.Register(MakeKeyBindingSetting("editor.shortcuts.save", "Ctrl+S", "Save", "Editor")));

    ASSERT_TRUE(registry.SetValue(store, "application.iconPath", std::string{"Assets/Studio Icon.png"}));
    ASSERT_TRUE(registry.SetValue(store, "editor.shortcuts.save", std::string{"Ctrl+Shift+F12"}));
    EXPECT_EQ(registry.GetString(store, "application.iconPath"), "Assets/Studio Icon.png");
    EXPECT_EQ(registry.GetString(store, "editor.shortcuts.save"), "Ctrl+Shift+F12");

    EXPECT_FALSE(registry.SetValue(store, "application.iconPath", std::string(257U, 'x')));
    EXPECT_FALSE(registry.SetValue(store, "application.iconPath", std::string{"bad\0path", 8U}));
    EXPECT_FALSE(registry.SetValue(store, "editor.shortcuts.save", std::string{"Hyper+S"}));
    EXPECT_FALSE(registry.SetValue(store, "editor.shortcuts.save", std::string{"Ctrl+Ctrl+S"}));
    EXPECT_FALSE(registry.SetValue(store, "editor.shortcuts.save", std::string{"F13"}));

    store.SetString("editor.shortcuts.save", "Hyper+S");
    EXPECT_EQ(registry.GetValue(store, "editor.shortcuts.save"), SettingValue{std::string{"Ctrl+S"}});
}

TEST_F(SettingsRegistryTest, StringListUsesBoundedNumberedStringStorage) {
    constexpr std::string_view kListId = "editor.favoriteProjects";
    const SettingStringList defaultValue{"Default/Project"};
    ASSERT_TRUE(registry.Register(MakeStringListSetting(std::string{kListId}, defaultValue, 3U, 32U,
                                                              "Favorite Projects", "Editor/Session")));

    const SettingStringList values{"First/Project", "Second/Project"};
    ASSERT_TRUE(registry.SetValue(store, kListId, values));
    EXPECT_EQ(store.GetInt("editor.favoriteProjects.count", -1), 2);
    EXPECT_EQ(store.GetString("editor.favoriteProjects.0", ""), values[0]);
    EXPECT_EQ(store.GetString("editor.favoriteProjects.1", ""), values[1]);
    EXPECT_EQ(registry.GetStringList(store, kListId), values);
    EXPECT_EQ(registry.GetValue(store, kListId), SettingValue{values});
    EXPECT_EQ(registry.GetStringList(store, "editor.grid.visible", {"fallback"}),
              (SettingStringList{"fallback"}));

    EXPECT_FALSE(registry.SetValue(store, kListId, SettingStringList{"one", "two", "three", "four"}));
    EXPECT_FALSE(registry.SetValue(store, kListId, SettingStringList{std::string(33U, 'x')}));
    EXPECT_EQ(registry.GetStringList(store, kListId), values)
        << "a rejected list write must leave all stored entries unchanged";

    // List item paths are reserved, while a nonnumeric sibling under the same JSON object remains legal.
    EXPECT_FALSE(registry.Register(MakeBoolSetting("editor.favoriteProjects.1", false, "Collision", "")));
    EXPECT_FALSE(registry.Register(MakeBoolSetting("editor.favoriteProjects", false, "Parent", "")));
    EXPECT_TRUE(registry.Register(
        MakeBoolSetting("editor.favoriteProjects.descriptionVisible", true, "Sibling", "")));

    ASSERT_TRUE(registry.ClearValue(store, kListId));
    EXPECT_FALSE(store.HasKey("editor.favoriteProjects.count"));
    EXPECT_FALSE(store.HasKey("editor.favoriteProjects.0"));
    EXPECT_FALSE(store.HasKey("editor.favoriteProjects.1"));
    EXPECT_EQ(registry.GetStringList(store, kListId), defaultValue);
}

TEST_F(SettingsRegistryTest, TypedGettersAnswerFallbackForUnknownOrMismatchedIds) {
    EXPECT_TRUE(registry.GetBool(store, "editor.unknown", true));
    EXPECT_EQ(registry.GetInt(store, "editor.grid.opacity", 9), 9);
    EXPECT_EQ(registry.GetString(store, "editor.grid.visible", "fallback"), "fallback");
    EXPECT_FALSE(registry.GetValue(store, "editor.unknown").has_value());
}

TEST_F(SettingsRegistryTest, SetterRejectsIllegalValuesAndLeavesTheStoreUntouched) {
    EXPECT_FALSE(registry.SetValue(store, "editor.autosave.minutes", std::int64_t{0}));
    EXPECT_FALSE(registry.SetValue(store, "editor.autosave.minutes", 5.0));
    EXPECT_FALSE(registry.SetValue(store, "editor.grid.opacity", kNaN));
    EXPECT_FALSE(registry.SetValue(store, "editor.grid.opacity", 1.5));
    EXPECT_FALSE(registry.SetValue(store, "editor.layout.name", std::string(17, 'x')));
    EXPECT_FALSE(registry.SetValue(store, "render.quality", std::int64_t{3}));
    EXPECT_FALSE(registry.SetValue(store, "editor.unknown", true));

    for (const char* key : {"editor.autosave.minutes", "editor.grid.opacity", "editor.layout.name", "render.quality",
                            "editor.unknown"}) {
        EXPECT_FALSE(store.HasKey(key)) << key;
    }
}

TEST_F(SettingsRegistryTest, OneBadChannelRefusesTheWholeColour) {
    ASSERT_TRUE(registry.SetValue(store, "editor.outline.color", SettingColor{0.2F, 0.4F, 0.6F}));
    EXPECT_FALSE(registry.SetValue(store, "editor.outline.color", SettingColor{0.9F, 0.9F, 1.1F}));
    EXPECT_EQ(registry.GetColor(store, "editor.outline.color"), (SettingColor{0.2F, 0.4F, 0.6F}));
}

TEST_F(SettingsRegistryTest, ColourWithoutAlphaIsAlwaysOpaqueAndStoresNoAlpha) {
    ASSERT_TRUE(registry.SetValue(store, "editor.outline.color", SettingColor{0.2F, 0.4F, 0.6F, 0.1F}));
    EXPECT_FALSE(store.HasKey("editor.outline.color.a"));
    EXPECT_FLOAT_EQ(registry.GetColor(store, "editor.outline.color").a, 1.0F);
}

TEST_F(SettingsRegistryTest, CorruptValuesFallBackToDefaultsPerSetting) {
    store.SetString("editor.grid.visible", "yes");  // wrong type
    store.SetInt("editor.autosave.minutes", 500);   // out of range
    store.SetDouble("editor.grid.opacity", 0.25);   // legal - must survive its neighbours
    store.SetString("editor.layout.name", std::string(40, 'x'));
    store.SetInt("render.quality", -1);             // not an entry
    store.SetDouble("editor.outline.color.r", 0.1); // one channel legal...
    store.SetDouble("editor.outline.color.g", 7.0); // ...one out of range...
    store.SetDouble("editor.outline.color.b", 0.1);
    store.SetDouble("editor.clear.color.r", 0.5);   // ...and a colour missing its alpha
    store.SetDouble("editor.clear.color.g", 0.5);
    store.SetDouble("editor.clear.color.b", 0.5);

    EXPECT_TRUE(registry.GetBool(store, "editor.grid.visible"));
    EXPECT_EQ(registry.GetInt(store, "editor.autosave.minutes"), 5);
    EXPECT_DOUBLE_EQ(registry.GetFloat(store, "editor.grid.opacity"), 0.25);
    EXPECT_EQ(registry.GetString(store, "editor.layout.name"), "Default");
    EXPECT_EQ(registry.GetInt(store, "render.quality"), 1);
    EXPECT_EQ(registry.GetColor(store, "editor.outline.color"), (SettingColor{1.0F, 0.5F, 0.25F}));
    EXPECT_EQ(registry.GetColor(store, "editor.clear.color"), (SettingColor{0.1F, 0.2F, 0.3F, 0.4F}));
    EXPECT_FALSE(registry.IsModified(store, "editor.autosave.minutes"));
    EXPECT_TRUE(registry.IsModified(store, "editor.grid.opacity"));
}

TEST_F(SettingsRegistryTest, HugeStoredChannelFallsBackInsteadOfOverflowing) {
    store.SetDouble("editor.outline.color.r", 1.0e300);
    store.SetDouble("editor.outline.color.g", 0.0);
    store.SetDouble("editor.outline.color.b", 0.0);
    EXPECT_EQ(registry.GetColor(store, "editor.outline.color"), (SettingColor{1.0F, 0.5F, 0.25F}));
}

TEST_F(SettingsRegistryTest, WholeNumberWrittenAsDecimalReadsAsInt) {
    store.SetDouble("editor.autosave.minutes", 15.0);
    EXPECT_EQ(registry.GetInt(store, "editor.autosave.minutes"), 15);
    store.SetDouble("editor.autosave.minutes", 15.5);
    EXPECT_EQ(registry.GetInt(store, "editor.autosave.minutes"), 5);
    store.SetDouble("editor.autosave.minutes", 1.0e30);
    EXPECT_EQ(registry.GetInt(store, "editor.autosave.minutes"), 5);
}

TEST_F(SettingsRegistryTest, ResetRestoresTheDefault) {
    ASSERT_TRUE(registry.SetValue(store, "editor.grid.opacity", 0.9));
    ASSERT_TRUE(registry.IsModified(store, "editor.grid.opacity"));
    ASSERT_TRUE(registry.Reset(store, "editor.grid.opacity"));
    EXPECT_DOUBLE_EQ(registry.GetFloat(store, "editor.grid.opacity"), 0.5);
    EXPECT_FALSE(registry.IsModified(store, "editor.grid.opacity"));
    EXPECT_FALSE(registry.Reset(store, "editor.unknown"));
}

TEST_F(SettingsRegistryTest, DeprecatedSettingsAreReadButNeverWritten) {
    SettingDescriptor old = MakeIntSetting("editor.legacy.size", 3, 0, 10, "Old Size", "");
    old.flags = kSettingFlagDeprecated | kSettingFlagHidden;
    ASSERT_TRUE(registry.Register(old));
    EXPECT_TRUE(registry.Find("editor.legacy.size")->HasFlag(kSettingFlagHidden));

    store.SetInt("editor.legacy.size", 7);
    EXPECT_EQ(registry.GetInt(store, "editor.legacy.size"), 7);
    EXPECT_FALSE(registry.SetValue(store, "editor.legacy.size", std::int64_t{2}));
    EXPECT_EQ(store.GetInt("editor.legacy.size", 0), 7);
}

TEST(DeprecatedAliasTest, ReadsMigratesAndThenRemovesTheOldId) {
    SettingsRegistry registry;
    ASSERT_TRUE(registry.Register(
        MakeIntSetting("rendering.shadow.resolution", 2048, 512, 4096, "Resolution", "Rendering")));
    SettingDescriptor alias =
        MakeIntSetting("rendering.shadow.mapSize", 2048, 512, 4096, "Legacy Resolution", "Rendering");
    alias.flags = kSettingFlagDeprecated | kSettingFlagHidden;
    alias.replacementId = "rendering.shadow.resolution";
    ASSERT_TRUE(registry.Register(alias));

    ConfigManager store;
    store.SetInt("rendering.shadow.mapSize", 1024);
    EXPECT_EQ(registry.GetStoredValue(store, "rendering.shadow.mapSize"), SettingValue{1024});
    EXPECT_EQ(registry.GetStoredValue(store, "rendering.shadow.resolution"), SettingValue{1024});
    EXPECT_FALSE(registry.SetValue(store, "rendering.shadow.mapSize", std::int64_t{4096}));

    EXPECT_TRUE(registry.MigrateDeprecatedValues(store));
    EXPECT_EQ(store.GetInt("rendering.shadow.resolution", 0), 1024);
    EXPECT_FALSE(store.HasKey("rendering.shadow.mapSize"));
    EXPECT_FALSE(registry.MigrateDeprecatedValues(store));

    // The live id wins if both forms are present; the stale alias is removed.
    store.SetInt("rendering.shadow.mapSize", 512);
    store.SetInt("rendering.shadow.resolution", 2048);
    EXPECT_TRUE(registry.MigrateDeprecatedValues(store));
    EXPECT_EQ(store.GetInt("rendering.shadow.resolution", 0), 2048);
    EXPECT_FALSE(store.HasKey("rendering.shadow.mapSize"));

    // Resetting the live id also clears any raw deprecated key so it cannot reappear as fallback.
    store.SetInt("rendering.shadow.mapSize", 1024);
    EXPECT_TRUE(registry.ClearValue(store, "rendering.shadow.resolution"));
    EXPECT_FALSE(store.HasKey("rendering.shadow.mapSize"));
    EXPECT_FALSE(registry.GetStoredValue(store, "rendering.shadow.resolution").has_value());
}

TEST_F(SettingsRegistryTest, ValuesSurviveASaveAndReload) {
    ASSERT_TRUE(registry.SetValue(store, "editor.grid.opacity", 0.125));
    ASSERT_TRUE(registry.SetValue(store, "editor.clear.color", SettingColor{1.0F, 0.0F, 0.5F, 0.75F}));
    ASSERT_TRUE(registry.SetValue(store, "render.quality", std::int64_t{0}));
    const std::string path = "uve_settings_registry_roundtrip.uvsettings";
    ASSERT_TRUE(store.Save(path));

    ConfigManager reloaded;
    ASSERT_TRUE(reloaded.Load(path));
    EXPECT_DOUBLE_EQ(registry.GetFloat(reloaded, "editor.grid.opacity"), 0.125);
    EXPECT_EQ(registry.GetColor(reloaded, "editor.clear.color"), (SettingColor{1.0F, 0.0F, 0.5F, 0.75F}));
    EXPECT_EQ(registry.GetInt(reloaded, "render.quality"), 0);
    std::remove(path.c_str());
}

TEST(ConfigManagerRemoveKeyTest, RemovesTheLeafAndPrunesObjectsLeftEmpty) {
    ConfigManager store;
    store.SetInt("a.b.c", 1);
    store.SetInt("a.d", 2);
    EXPECT_TRUE(store.RemoveKey("a.b.c"));
    EXPECT_FALSE(store.HasKey("a.b.c"));
    EXPECT_TRUE(store.HasKey("a.d"));
    // "a.b" is gone with its last value; "a" stays because it still holds "a.d".
    store.SetInt("a.b", 3);
    EXPECT_TRUE(store.HasKey("a.b"));
    EXPECT_TRUE(store.RemoveKey("a.d"));
    EXPECT_TRUE(store.RemoveKey("a.b"));
    const std::string path = "uve_remove_key_pruned.uvsettings";
    ASSERT_TRUE(store.Save(path));
    ConfigManager reloaded;
    ASSERT_TRUE(reloaded.Load(path));
    reloaded.SetInt("probe", 1);
    EXPECT_FALSE(reloaded.HasKey("a"));
    std::remove(path.c_str());
}

TEST(ConfigManagerRemoveKeyTest, RefusesMissingKeysAndObjects) {
    ConfigManager store;
    store.SetInt("a.b", 1);
    EXPECT_FALSE(store.RemoveKey("a"));
    EXPECT_FALSE(store.RemoveKey("a.missing"));
    EXPECT_FALSE(store.RemoveKey("a.b.c"));
    EXPECT_FALSE(store.RemoveKey(""));
    EXPECT_TRUE(store.HasKey("a.b"));
}

TEST_F(SettingsRegistryTest, StoredValueIsOnlyALegalValueTheStoreReallyHolds) {
    EXPECT_FALSE(registry.GetStoredValue(store, "editor.grid.visible").has_value());
    store.SetString("editor.grid.visible", "yes");
    EXPECT_FALSE(registry.GetStoredValue(store, "editor.grid.visible").has_value());
    store.SetBool("editor.grid.visible", false);
    EXPECT_EQ(registry.GetStoredValue(store, "editor.grid.visible"), SettingValue{false});

    store.SetBool("editor.layout.name", true);
    EXPECT_FALSE(registry.GetStoredValue(store, "editor.layout.name").has_value());
    store.SetString("editor.layout.name", "");
    EXPECT_EQ(registry.GetStoredValue(store, "editor.layout.name"), SettingValue{std::string()});

    store.SetString("editor.grid.opacity", "half");
    EXPECT_FALSE(registry.GetStoredValue(store, "editor.grid.opacity").has_value());
    EXPECT_DOUBLE_EQ(registry.GetFloat(store, "editor.grid.opacity"), 0.5);
    store.SetDouble("editor.clear.color.r", 0.5);
    EXPECT_FALSE(registry.GetStoredValue(store, "editor.clear.color").has_value());
}

TEST_F(SettingsRegistryTest, ClearRemovesEveryKeyOfTheSetting) {
    ASSERT_TRUE(registry.SetValue(store, "editor.clear.color", SettingColor{0.1F, 0.2F, 0.3F, 0.4F}));
    ASSERT_TRUE(registry.SetValue(store, "editor.grid.opacity", 0.75));
    EXPECT_TRUE(registry.ClearValue(store, "editor.clear.color"));
    for (const char* key : {"editor.clear.color.r", "editor.clear.color.g", "editor.clear.color.b",
                            "editor.clear.color.a"}) {
        EXPECT_FALSE(store.HasKey(key)) << key;
    }
    EXPECT_TRUE(store.HasKey("editor.grid.opacity"));
    EXPECT_FALSE(registry.ClearValue(store, "editor.clear.color"));
    EXPECT_FALSE(registry.ClearValue(store, "editor.unknown"));
}

TEST(SettingVector3Test, IsStoredPerComponentAndValidatedWhole) {
    SettingsRegistry registry;
    ASSERT_TRUE(registry.Register(
        MakeVector3Setting("physics.gravity", {0.0, -9.81, 0.0}, -100.0, 100.0, "Gravity", "Physics")));
    ConfigManager store;
    EXPECT_EQ(registry.GetVector3(store, "physics.gravity"), (SettingVector3{0.0, -9.81, 0.0}));

    ASSERT_TRUE(registry.SetValue(store, "physics.gravity", SettingVector3{1.0, -2.0, 3.0}));
    EXPECT_DOUBLE_EQ(store.GetDouble("physics.gravity.y", 0.0), -2.0);
    EXPECT_EQ(registry.GetVector3(store, "physics.gravity"), (SettingVector3{1.0, -2.0, 3.0}));

    // Bounds hold for each component, and one bad component refuses the vector.
    EXPECT_FALSE(registry.SetValue(store, "physics.gravity", SettingVector3{0.0, -200.0, 0.0}));
    EXPECT_FALSE(registry.SetValue(store, "physics.gravity", SettingVector3{0.0, kNaN, 0.0}));
    EXPECT_EQ(registry.GetVector3(store, "physics.gravity"), (SettingVector3{1.0, -2.0, 3.0}));

    // A vector missing a component reads as its default, never half stored.
    store.RemoveKey("physics.gravity.z");
    EXPECT_FALSE(registry.GetStoredValue(store, "physics.gravity").has_value());
    EXPECT_EQ(registry.GetVector3(store, "physics.gravity"), (SettingVector3{0.0, -9.81, 0.0}));

    ASSERT_TRUE(registry.SetValue(store, "physics.gravity", SettingVector3{1.0, 1.0, 1.0}));
    EXPECT_TRUE(registry.ClearValue(store, "physics.gravity"));
    EXPECT_FALSE(store.HasKey("physics.gravity.x"));

    // Its components are its own, but a sibling beside them is fine.
    EXPECT_FALSE(registry.Register(MakeFloatSetting("physics.gravity.x", 0.0, 0.0, 1.0, "X", "")));
    EXPECT_TRUE(registry.Register(MakeFloatSetting("physics.gravity.scale", 1.0, 0.0, 10.0, "Scale", "")));
    EXPECT_NE(ValidateSettingDescriptor(MakeVector3Setting("a.v", {0.0, 5.0, 0.0}, 0.0, 1.0, "V", "")), "");
}

class SettingsDocumentTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::remove(path.c_str());
        std::remove(invalidPath.c_str());
        ASSERT_TRUE(document.GetRegistry().Register(
            MakeFloatSetting("physics.ticks", 60.0, 1.0, 1000.0, "Ticks", "Physics")));
        ASSERT_TRUE(document.GetRegistry().Register(
            MakeColorSetting("rendering.clear", {0.1F, 0.1F, 0.1F}, false, "Clear", "Rendering")));
    }
    void TearDown() override {
        std::remove(path.c_str());
        std::remove(invalidPath.c_str());
    }

    const std::string path = "uve_settings_document_test.uvsettings";
    const std::string invalidPath = "uve_settings_document_invalid_test.uvsettings";
    SettingsDocument document;
};

TEST_F(SettingsDocumentTest, LoadsEveryHistoricalFixtureThroughForwardMigrationSteps) {
    struct Fixture final {
        std::string name;
        std::string contents;
        double ticksPerSecond;
        bool alreadyCurrent;
    };
    const std::vector<Fixture> fixtures{
        {"v0", R"({"physics":{"legacyTicks":30}})", 30.0, false},
        {"v1", R"({"version":1,"physics":{"ticks":45}})", 45.0, false},
        {"v2", R"({"version":2,"simulation":{"ticks":90.0}})", 90.0, false},
        {"v3", R"({"version":3,"physics":{"ticksPerSecond":120.0}})", 120.0, true},
    };
    const auto registerSchema = [](SettingsDocument& versioned) {
        if (!versioned.GetRegistry().Register(
                MakeFloatSetting("physics.ticksPerSecond", 60.0, 1.0, 1000.0, "Ticks", "Physics")) ||
            !versioned.SetCurrentVersion(3U)) {
            return false;
        }
        if (!versioned.RegisterMigration(0U, [](IConfigManager& store) {
                if (!store.HasKey("physics.legacyTicks")) {
                    return true;
                }
                const std::int64_t ticks = store.GetInt("physics.legacyTicks", -1);
                if (ticks < 1) {
                    return false;
                }
                store.SetInt("physics.ticks", ticks);
                return store.RemoveKey("physics.legacyTicks");
            })) {
            return false;
        }
        if (!versioned.RegisterMigration(1U, [](IConfigManager& store) {
                if (!store.HasKey("physics.ticks")) {
                    return true;
                }
                const std::int64_t ticks = store.GetInt("physics.ticks", -1);
                if (ticks < 1) {
                    return false;
                }
                store.SetDouble("simulation.ticks", static_cast<double>(ticks));
                return store.RemoveKey("physics.ticks");
            })) {
            return false;
        }
        return versioned.RegisterMigration(2U, [](IConfigManager& store) {
            if (!store.HasKey("simulation.ticks")) {
                return true;
            }
            const double ticks = store.GetDouble("simulation.ticks", -1.0);
            if (ticks < 1.0) {
                return false;
            }
            store.SetDouble("physics.ticksPerSecond", ticks);
            return store.RemoveKey("simulation.ticks");
        });
    };

    for (const Fixture& fixture : fixtures) {
        const std::string fixturePath = "uve_settings_document_migration_" + fixture.name + ".uvsettings";
        std::remove(fixturePath.c_str());
        {
            std::ofstream file(fixturePath);
            ASSERT_TRUE(file.is_open()) << fixture.name;
            file << fixture.contents;
        }

        SettingsDocument versioned;
        ASSERT_TRUE(registerSchema(versioned)) << fixture.name;
        ASSERT_TRUE(versioned.Load(fixturePath)) << fixture.name;
        EXPECT_EQ(versioned.GetCurrentVersion(), 3U);
        EXPECT_EQ(versioned.GetStore().GetInt(kSettingsDocumentVersionKey, -1), 3);
        EXPECT_EQ(versioned.GetValue("physics.ticksPerSecond"), SettingValue{fixture.ticksPerSecond});
        EXPECT_EQ(versioned.IsDirty(), !fixture.alreadyCurrent) << fixture.name;
        EXPECT_FALSE(versioned.GetStore().HasKey("physics.legacyTicks"));
        EXPECT_FALSE(versioned.GetStore().HasKey("physics.ticks"));
        EXPECT_FALSE(versioned.GetStore().HasKey("simulation.ticks"));

        ASSERT_TRUE(versioned.Save()) << fixture.name;
        ConfigManager persisted;
        ASSERT_TRUE(persisted.Load(fixturePath)) << fixture.name;
        EXPECT_EQ(persisted.GetInt(kSettingsDocumentVersionKey, -1), 3);
        EXPECT_DOUBLE_EQ(persisted.GetDouble("physics.ticksPerSecond", -1.0), fixture.ticksPerSecond);
        std::remove(fixturePath.c_str());
    }
}

TEST_F(SettingsDocumentTest, FailedFutureAndStepMigrationsLeaveTheLoadedDocumentUntouched) {
    const std::string validPath = "uve_settings_document_version_current.uvsettings";
    const std::string futurePath = "uve_settings_document_version_future.uvsettings";
    const std::string invalidVersionPath = "uve_settings_document_version_invalid.uvsettings";
    const std::string failedStepPath = "uve_settings_document_version_failed_step.uvsettings";
    for (const std::string& fixturePath : {validPath, futurePath, invalidVersionPath, failedStepPath}) {
        std::remove(fixturePath.c_str());
    }
    {
        std::ofstream file(validPath);
        ASSERT_TRUE(file.is_open());
        file << R"({"version":2,"physics":{"ticks":120.0}})";
    }
    {
        std::ofstream file(futurePath);
        ASSERT_TRUE(file.is_open());
        file << R"({"version":3,"physics":{"ticks":240.0}})";
    }
    {
        std::ofstream file(invalidVersionPath);
        ASSERT_TRUE(file.is_open());
        file << R"({"version":{"nested":1},"physics":{"ticks":30.0}})";
    }
    {
        std::ofstream file(failedStepPath);
        ASSERT_TRUE(file.is_open());
        file << R"({"version":1,"physics":{"ticks":30.0}})";
    }

    SettingsDocument versioned;
    ASSERT_TRUE(versioned.GetRegistry().Register(
        MakeFloatSetting("physics.ticks", 60.0, 1.0, 1000.0, "Ticks", "Physics")));
    ASSERT_TRUE(versioned.SetCurrentVersion(2U));
    ASSERT_TRUE(versioned.RegisterMigration(1U, [](IConfigManager& store) {
        store.SetDouble("physics.ticks", 1.0); // Prove a failed callback's partial edits are discarded.
        return false;
    }));
    ASSERT_TRUE(versioned.Load(validPath));
    ASSERT_TRUE(versioned.SetValue("physics.ticks", 180.0));
    const std::filesystem::path pathBeforeFailure = versioned.GetPath();

    EXPECT_FALSE(versioned.Load(futurePath));
    EXPECT_EQ(versioned.GetPath(), pathBeforeFailure);
    EXPECT_TRUE(versioned.IsDirty());
    EXPECT_EQ(versioned.GetValue("physics.ticks"), SettingValue{180.0});
    EXPECT_EQ(versioned.GetStore().GetInt(kSettingsDocumentVersionKey, -1), 2);

    EXPECT_FALSE(versioned.Load(invalidVersionPath));
    EXPECT_EQ(versioned.GetPath(), pathBeforeFailure);
    EXPECT_TRUE(versioned.IsDirty());
    EXPECT_EQ(versioned.GetValue("physics.ticks"), SettingValue{180.0});
    EXPECT_EQ(versioned.GetStore().GetInt(kSettingsDocumentVersionKey, -1), 2);

    EXPECT_FALSE(versioned.Load(failedStepPath));
    EXPECT_EQ(versioned.GetPath(), pathBeforeFailure);
    EXPECT_TRUE(versioned.IsDirty());
    EXPECT_EQ(versioned.GetValue("physics.ticks"), SettingValue{180.0});
    EXPECT_EQ(versioned.GetStore().GetInt(kSettingsDocumentVersionKey, -1), 2);
    EXPECT_FALSE(versioned.SetCurrentVersion(3U));
    EXPECT_FALSE(versioned.RegisterMigration(1U, [](IConfigManager&) { return true; }));

    for (const std::string& fixturePath : {validPath, futurePath, invalidVersionPath, failedStepPath}) {
        std::remove(fixturePath.c_str());
    }
}

TEST_F(SettingsDocumentTest, LoadingMigratesDeprecatedAliasToItsReplacement) {
    const std::string aliasPath = "uve_settings_document_deprecated_alias.uvsettings";
    std::remove(aliasPath.c_str());
    {
        std::ofstream file(aliasPath);
        ASSERT_TRUE(file.is_open());
        file << R"({"version":1,"physics":{"legacyTicks":120.0}})";
    }

    SettingsDocument versioned;
    ASSERT_TRUE(versioned.GetRegistry().Register(
        MakeFloatSetting("physics.ticks", 60.0, 1.0, 1000.0, "Ticks", "Physics")));
    SettingDescriptor alias = MakeFloatSetting("physics.legacyTicks", 60.0, 1.0, 1000.0,
                                                     "Old Ticks", "Physics");
    alias.flags = kSettingFlagDeprecated | kSettingFlagHidden;
    alias.replacementId = "physics.ticks";
    ASSERT_TRUE(versioned.GetRegistry().Register(std::move(alias)));

    ASSERT_TRUE(versioned.Load(aliasPath));
    EXPECT_EQ(versioned.GetValue("physics.ticks"), SettingValue{120.0});
    EXPECT_FALSE(versioned.GetStore().HasKey("physics.legacyTicks"));
    EXPECT_TRUE(versioned.IsDirty());
    ASSERT_TRUE(versioned.Save());
    std::remove(aliasPath.c_str());
}

TEST_F(SettingsDocumentTest, AMissingFileIsEveryDefault) {
    ASSERT_TRUE(document.Load(path));
    EXPECT_FALSE(document.IsDirty());
    EXPECT_EQ(document.GetStore().GetInt(kSettingsDocumentVersionKey, -1), 1);
    EXPECT_EQ(document.GetValue("physics.ticks"), SettingValue{60.0});
    EXPECT_FALSE(document.GetStoredValue("physics.ticks").has_value());
    EXPECT_FALSE(document.IsModified("physics.ticks"));
}

TEST_F(SettingsDocumentTest, HoldsOnlyWhatDiffersFromTheDefault) {
    ASSERT_TRUE(document.Load(path));
    ASSERT_TRUE(document.SetValue("physics.ticks", 120.0));
    EXPECT_TRUE(document.IsDirty());
    EXPECT_TRUE(document.GetStore().HasKey("physics.ticks"));
    ASSERT_TRUE(document.Save());
    EXPECT_FALSE(document.IsDirty());

    // Setting the value it already has changes nothing.
    ASSERT_TRUE(document.SetValue("physics.ticks", 120.0));
    EXPECT_FALSE(document.IsDirty());

    // Setting the default takes the key out of the file.
    ASSERT_TRUE(document.SetValue("physics.ticks", 60.0));
    EXPECT_TRUE(document.IsDirty());
    EXPECT_FALSE(document.GetStore().HasKey("physics.ticks"));
    EXPECT_FALSE(document.GetStore().HasKey("physics"));
}

TEST_F(SettingsDocumentTest, SettingObserversFireOnlyForEffectiveChangesAndCanUnsubscribe) {
    ASSERT_TRUE(document.Load(path));
    std::vector<SettingChangedEvent> changes;
    const SettingsObserverSubscription subscription = document.SubscribeToSetting(
        "physics.ticks", [&changes](const SettingChangedEvent& event) { changes.push_back(event); });
    ASSERT_TRUE(subscription.IsValid());

    ASSERT_TRUE(document.SetValue("physics.ticks", 120.0));
    ASSERT_EQ(changes.size(), 1U);
    EXPECT_EQ(changes[0].id, "physics.ticks");
    EXPECT_EQ(changes[0].previousValue, SettingValue{60.0});
    EXPECT_EQ(changes[0].newValue, SettingValue{120.0});

    // A repeated write and a rejected write do not represent a value change.
    ASSERT_TRUE(document.SetValue("physics.ticks", 120.0));
    EXPECT_FALSE(document.SetValue("physics.ticks", 0.0));
    EXPECT_EQ(changes.size(), 1U);

    ASSERT_TRUE(document.Reset("physics.ticks"));
    ASSERT_EQ(changes.size(), 2U);
    EXPECT_EQ(changes[1].previousValue, SettingValue{120.0});
    EXPECT_EQ(changes[1].newValue, SettingValue{60.0});
    EXPECT_TRUE(document.Unsubscribe(subscription));
    EXPECT_FALSE(document.Unsubscribe(subscription));
    ASSERT_TRUE(document.SetValue("physics.ticks", 90.0));
    EXPECT_EQ(changes.size(), 2U);
}

TEST_F(SettingsDocumentTest, RemovingInvalidStoredDataDoesNotNotifyWhenEffectiveValueIsUnchanged) {
    {
        std::ofstream fixture(path);
        ASSERT_TRUE(fixture.is_open());
        fixture << R"({"physics":{"ticks":5000}})";
    }
    ASSERT_TRUE(document.Load(path));
    EXPECT_EQ(document.GetValue("physics.ticks"), SettingValue{60.0});
    EXPECT_FALSE(document.GetStoredValue("physics.ticks").has_value());

    int notifications = 0;
    ASSERT_TRUE(document.SubscribeToSetting(
        "physics.ticks", [&notifications](const SettingChangedEvent&) { ++notifications; }).IsValid());
    ASSERT_TRUE(document.SetValue("physics.ticks", 60.0));

    EXPECT_TRUE(document.IsDirty()); // The corrupt raw key was removed from disk state.
    EXPECT_FALSE(document.GetStore().HasKey("physics.ticks"));
    EXPECT_EQ(notifications, 0); // The effective value was the default both before and after cleanup.
}

TEST_F(SettingsDocumentTest, CategoryObserversUseSlashBoundaries) {
    ASSERT_TRUE(document.GetRegistry().Register(
        MakeFloatSetting("rendering.viewport.scale", 1.0, 0.1, 4.0, "Scale", "Rendering/Viewport")));
    ASSERT_TRUE(document.GetRegistry().Register(
        MakeFloatSetting("renderingExtra.scale", 1.0, 0.1, 4.0, "Extra Scale", "RenderingExtra")));
    ASSERT_TRUE(document.Load(path));

    std::vector<std::string> changedIds;
    const SettingsObserverSubscription subscription = document.SubscribeToCategory(
        "Rendering", [&changedIds](const SettingChangedEvent& event) { changedIds.push_back(event.id); });
    ASSERT_TRUE(subscription.IsValid());
    EXPECT_FALSE(document.SubscribeToCategory("Render", [](const SettingChangedEvent&) {}).IsValid());
    EXPECT_FALSE(document.SubscribeToCategory("", [](const SettingChangedEvent&) {}).IsValid());
    EXPECT_FALSE(document.SubscribeToSetting("rendering.unknown", [](const SettingChangedEvent&) {})
                     .IsValid());

    ASSERT_TRUE(document.SetValue("rendering.clear", SettingColor{0.5F, 0.25F, 0.75F}));
    ASSERT_TRUE(document.SetValue("rendering.viewport.scale", 2.0));
    ASSERT_TRUE(document.SetValue("renderingExtra.scale", 2.0));
    EXPECT_EQ(changedIds, (std::vector<std::string>{"rendering.clear", "rendering.viewport.scale"}));
    EXPECT_TRUE(document.Unsubscribe(subscription));
}

TEST_F(SettingsDocumentTest, UnsubscribeDuringDispatchSkipsTheRemovedObserver) {
    ASSERT_TRUE(document.Load(path));
    int firstCalls = 0;
    int secondCalls = 0;
    SettingsObserverSubscription secondSubscription;
    const SettingsObserverSubscription firstSubscription = document.SubscribeToSetting(
        "physics.ticks", [this, &firstCalls, &secondSubscription](const SettingChangedEvent&) {
            ++firstCalls;
            EXPECT_TRUE(document.Unsubscribe(secondSubscription));
        });
    secondSubscription = document.SubscribeToSetting(
        "physics.ticks", [&secondCalls](const SettingChangedEvent&) { ++secondCalls; });
    ASSERT_TRUE(firstSubscription.IsValid());
    ASSERT_TRUE(secondSubscription.IsValid());

    ASSERT_TRUE(document.SetValue("physics.ticks", 120.0));
    EXPECT_EQ(firstCalls, 1);
    EXPECT_EQ(secondCalls, 0);
    EXPECT_FALSE(document.Unsubscribe(secondSubscription));
}

TEST_F(SettingsDocumentTest, ReentrantSettingChangesNotifyImmediatelyWithCompleteValues) {
    ASSERT_TRUE(document.Load(path));
    std::vector<SettingChangedEvent> changes;
    const SettingsObserverSubscription subscription = document.SubscribeToSetting(
        "physics.ticks", [this, &changes](const SettingChangedEvent& event) {
            changes.push_back(event);
            if (event.newValue == SettingValue{120.0}) {
                EXPECT_TRUE(document.SetValue("physics.ticks", 180.0));
            }
        });
    ASSERT_TRUE(subscription.IsValid());

    ASSERT_TRUE(document.SetValue("physics.ticks", 120.0));

    ASSERT_EQ(changes.size(), 2U);
    EXPECT_EQ(changes[0].previousValue, SettingValue{60.0});
    EXPECT_EQ(changes[0].newValue, SettingValue{120.0});
    EXPECT_EQ(changes[1].previousValue, SettingValue{120.0});
    EXPECT_EQ(changes[1].newValue, SettingValue{180.0});
    EXPECT_EQ(document.GetValue("physics.ticks"), SettingValue{180.0});
}

TEST_F(SettingsDocumentTest, ObserverAddedDuringDispatchStartsWithTheNextChange) {
    ASSERT_TRUE(document.Load(path));
    int lateCalls = 0;
    bool added = false;
    SettingsObserverSubscription lateSubscription;
    const SettingsObserverSubscription earlySubscription = document.SubscribeToSetting(
        "physics.ticks", [this, &added, &lateCalls, &lateSubscription](const SettingChangedEvent&) {
            if (!added) {
                lateSubscription = document.SubscribeToSetting(
                    "physics.ticks", [&lateCalls](const SettingChangedEvent&) { ++lateCalls; });
                added = true;
            }
        });
    ASSERT_TRUE(earlySubscription.IsValid());

    ASSERT_TRUE(document.SetValue("physics.ticks", 120.0));
    ASSERT_TRUE(lateSubscription.IsValid());
    EXPECT_EQ(lateCalls, 0);
    ASSERT_TRUE(document.SetValue("physics.ticks", 180.0));
    EXPECT_EQ(lateCalls, 1);
}

TEST_F(SettingsDocumentTest, ObserverHandlesCannotUnsubscribeAnotherDocumentsListener) {
    ASSERT_TRUE(document.Load(path));
    SettingsDocument otherDocument;
    ASSERT_TRUE(otherDocument.GetRegistry().Register(
        MakeFloatSetting("physics.ticks", 60.0, 1.0, 1000.0, "Ticks", "Physics")));
    const SettingsObserverSubscription subscription =
        otherDocument.SubscribeToSetting("physics.ticks", [](const SettingChangedEvent&) {});
    ASSERT_TRUE(subscription.IsValid());

    EXPECT_FALSE(document.Unsubscribe(subscription));
    EXPECT_TRUE(otherDocument.Unsubscribe(subscription));
}

TEST_F(SettingsDocumentTest, SuccessfulLoadNotifiesChangesButFailedLoadLeavesStateAlone) {
    ASSERT_TRUE(document.Load(path));
    std::vector<SettingChangedEvent> changes;
    ASSERT_TRUE(document.SubscribeToSetting(
        "physics.ticks", [&changes](const SettingChangedEvent& event) { changes.push_back(event); }).IsValid());

    ASSERT_TRUE(document.SetValue("physics.ticks", 120.0));
    ASSERT_TRUE(document.Save());
    ASSERT_TRUE(document.SetValue("physics.ticks", 60.0));
    ASSERT_EQ(changes.size(), 2U);
    ASSERT_TRUE(document.IsDirty());

    {
        std::ofstream malformed(invalidPath);
        ASSERT_TRUE(malformed.is_open());
        malformed << "{ not valid json";
    }
    const std::filesystem::path pathBeforeFailedLoad = document.GetPath();
    EXPECT_FALSE(document.Load(invalidPath));
    EXPECT_EQ(document.GetPath(), pathBeforeFailedLoad);
    EXPECT_TRUE(document.IsDirty());
    EXPECT_EQ(document.GetValue("physics.ticks"), SettingValue{60.0});
    EXPECT_EQ(changes.size(), 2U);

    ASSERT_TRUE(document.Load(path));
    EXPECT_FALSE(document.IsDirty());
    EXPECT_EQ(document.GetValue("physics.ticks"), SettingValue{120.0});
    ASSERT_EQ(changes.size(), 3U);
    EXPECT_EQ(changes[2].previousValue, SettingValue{60.0});
    EXPECT_EQ(changes[2].newValue, SettingValue{120.0});
}

TEST_F(SettingsDocumentTest, LoadingAMissingFileClearsPriorValuesAndNotifiesTheReset) {
    ASSERT_TRUE(document.Load(path));
    std::vector<SettingChangedEvent> changes;
    ASSERT_TRUE(document.SubscribeToSetting(
        "physics.ticks", [&changes](const SettingChangedEvent& event) { changes.push_back(event); }).IsValid());
    ASSERT_TRUE(document.SetValue("physics.ticks", 120.0));
    ASSERT_EQ(changes.size(), 1U);

    ASSERT_TRUE(document.Load(path)); // The file is still missing; this is a successful empty load.
    EXPECT_FALSE(document.IsDirty());
    EXPECT_EQ(document.GetValue("physics.ticks"), SettingValue{60.0});
    ASSERT_EQ(changes.size(), 2U);
    EXPECT_EQ(changes[1].previousValue, SettingValue{120.0});
    EXPECT_EQ(changes[1].newValue, SettingValue{60.0});
}

TEST_F(SettingsDocumentTest, IgnoredAlphaOnOpaqueColourDoesNotDirtyOrNotify) {
    ASSERT_TRUE(document.Load(path));
    int notifications = 0;
    ASSERT_TRUE(document.SubscribeToSetting(
        "rendering.clear", [&notifications](const SettingChangedEvent&) { ++notifications; }).IsValid());

    ASSERT_TRUE(document.SetValue("rendering.clear", SettingColor{0.1F, 0.1F, 0.1F, 0.25F}));

    EXPECT_FALSE(document.IsDirty());
    EXPECT_FALSE(document.IsModified("rendering.clear"));
    EXPECT_FALSE(document.GetStoredValue("rendering.clear").has_value());
    EXPECT_EQ(document.GetValue("rendering.clear"),
              (SettingValue{SettingColor{0.1F, 0.1F, 0.1F, 1.0F}}));
    EXPECT_EQ(notifications, 0);
}

TEST_F(SettingsDocumentTest, RefusesIllegalValuesAndSurvivesAReload) {
    ASSERT_TRUE(document.Load(path));
    EXPECT_FALSE(document.SetValue("physics.ticks", 0.0));
    EXPECT_FALSE(document.SetValue("physics.unknown", 1.0));
    EXPECT_FALSE(document.IsDirty());
    ASSERT_TRUE(document.SetValue("rendering.clear", SettingColor{0.5F, 0.25F, 1.0F}));
    ASSERT_TRUE(document.Save());

    SettingsDocument reloaded;
    ASSERT_TRUE(reloaded.GetRegistry().Register(
        MakeColorSetting("rendering.clear", {0.1F, 0.1F, 0.1F}, false, "Clear", "Rendering")));
    ASSERT_TRUE(reloaded.Load(path));
    EXPECT_EQ(reloaded.GetValue("rendering.clear"), (SettingValue{SettingColor{0.5F, 0.25F, 1.0F}}));
    EXPECT_TRUE(reloaded.IsModified("rendering.clear"));
    EXPECT_TRUE(reloaded.Reset("rendering.clear"));
    EXPECT_TRUE(reloaded.IsDirty());
    EXPECT_FALSE(reloaded.IsModified("rendering.clear"));
}

TEST_F(SettingsRegistryTest,
       Vector2Vector4AssetReferenceLayerMaskAndSinceVersionMaintainWholeOrNothingSemantics) {
    SettingDescriptor vec2Desc =
        MakeVector2Setting("physics.2d.gravity", SettingVector2{0.0, -980.0}, -10000.0,
                              10000.0, "2D Gravity", "Physics/2D");
    vec2Desc.sinceVersion = 2U;
    ASSERT_TRUE(registry.Register(vec2Desc));
    EXPECT_EQ(registry.Find("physics.2d.gravity")->sinceVersion, 2U);

    ASSERT_TRUE(registry.Register(
        MakeVector4Setting("rendering.viewport.margins", SettingVector4{1.0, 2.0, 3.0, 4.0},
                              0.0, 100.0, "Margins", "Rendering")));
    ASSERT_TRUE(registry.Register(MakeAssetReferenceSetting(
        "rendering.environment.defaultResource", "Environments/default.uvenv", 256U,
        "Default Environment", "Rendering/Environment")));
    ASSERT_TRUE(registry.Register(
        MakeLayerMaskSetting("physics.defaultCollisionMask", 0xFFFFFFFFU,
                                "Default Collision Mask", "Physics")));

    ASSERT_TRUE(registry.SetValue(store, "physics.2d.gravity", SettingVector2{10.0, -500.0}));
    EXPECT_EQ(registry.GetVector2(store, "physics.2d.gravity"),
              (SettingVector2{10.0, -500.0}));
    EXPECT_FALSE(
        registry.SetValue(store, "physics.2d.gravity", SettingVector2{10.0, 50000.0}));
    EXPECT_EQ(registry.GetVector2(store, "physics.2d.gravity"),
              (SettingVector2{10.0, -500.0}))
        << "an out-of-range component must leave the entire Vector2 untouched";

    // Removing one component from storage causes ReadStored to fall back to the default as a whole.
    ASSERT_TRUE(store.RemoveKey("physics.2d.gravity.y"));
    EXPECT_EQ(registry.GetVector2(store, "physics.2d.gravity"),
              (SettingVector2{0.0, -980.0}));

    ASSERT_TRUE(registry.SetValue(store, "rendering.viewport.margins",
                                     SettingVector4{5.0, 6.0, 7.0, 8.0}));
    EXPECT_EQ(registry.GetVector4(store, "rendering.viewport.margins"),
              (SettingVector4{5.0, 6.0, 7.0, 8.0}));

    ASSERT_TRUE(registry.SetValue(store, "rendering.environment.defaultResource",
                                     std::string{"Environments/sky.uvenv"}));
    EXPECT_EQ(registry.GetString(store, "rendering.environment.defaultResource"),
              "Environments/sky.uvenv");
    EXPECT_FALSE(registry.SetValue(store, "rendering.environment.defaultResource",
                                      std::string{"../escape.uvenv"}));

    ASSERT_TRUE(registry.SetValue(store, "physics.defaultCollisionMask", std::int64_t{0x00FFLL}));
    EXPECT_EQ(registry.GetLayerMask(store, "physics.defaultCollisionMask"), 0x00FFU);
    EXPECT_FALSE(registry.SetValue(store, "physics.defaultCollisionMask", std::int64_t{-1}));
}

} // namespace
} // namespace UVE::Config::Tests
