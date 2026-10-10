// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/config/settings_registry_uve.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

#include "uve/config/config_manager_uve.h"
#include "uve/config/settings_document_uve.h"
#include "uve/config/settings_stack_uve.h"

namespace UVE::Config::Tests {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfinity = std::numeric_limits<double>::infinity();

SettingDescriptorUVE MakeQualityUVE() {
    return MakeEnumSettingUVE("render.quality", 1, {{0, "Low"}, {1, "Medium"}, {2, "High"}}, "Quality", "Rendering");
}

/// One registry holding one setting of every type, the way a module declares its own.
class SettingsRegistryUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(registry.RegisterUVE(MakeBoolSettingUVE("editor.grid.visible", true, "Show Grid", "Editor/Grid")));
        ASSERT_TRUE(registry.RegisterUVE(
            MakeIntSettingUVE("editor.autosave.minutes", 5, 1, 120, "Autosave Interval", "Editor/General")));
        ASSERT_TRUE(registry.RegisterUVE(
            MakeFloatSettingUVE("editor.grid.opacity", 0.5, 0.0, 1.0, "Grid Opacity", "Editor/Grid")));
        ASSERT_TRUE(
            registry.RegisterUVE(MakeStringSettingUVE("editor.layout.name", "Default", 16, "Layout", "Editor/Layout")));
        ASSERT_TRUE(registry.RegisterUVE(MakeQualityUVE()));
        ASSERT_TRUE(registry.RegisterUVE(
            MakeColorSettingUVE("editor.outline.color", {1.0F, 0.5F, 0.25F}, false, "Outline", "Editor/Viewport")));
        ASSERT_TRUE(registry.RegisterUVE(
            MakeColorSettingUVE("editor.clear.color", {0.1F, 0.2F, 0.3F, 0.4F}, true, "Clear", "Editor/Viewport")));
    }

    SettingsRegistryUVE registry;
    ConfigManagerUVE store;
};

TEST(SettingsStackUVETest, PrecedenceProvenanceAndInvalidValuesFallThrough) {
    SettingsRegistryUVE registry;
    SettingDescriptorUVE platformValue =
        MakeIntSettingUVE("settings.platformValue", 5, 1, 120, "Platform Value", "Test");
    platformValue.flags |= kSettingFlagPerPlatformUVE;
    ASSERT_TRUE(registry.RegisterUVE(std::move(platformValue)));
    SettingDescriptorUVE sessionValue = MakeBoolSettingUVE("settings.sessionValue", false, "Session Value", "Test");
    sessionValue.flags |= kSettingFlagNotPersistedUVE;
    ASSERT_TRUE(registry.RegisterUVE(std::move(sessionValue)));
    ASSERT_TRUE(registry.RegisterUVE(MakeIntSettingUVE("settings.nonPlatformValue", 7, 1, 120,
                                                       "Non-Platform Value", "Test")));

    ConfigManagerUVE project;
    ConfigManagerUVE user;
    ConfigManagerUVE platform;
    ConfigManagerUVE commandLine;
    SettingsStackUVE stack(registry);

    EXPECT_FALSE(stack.AttachLayerUVE(SettingValueSourceUVE::EngineDefault, project));
    ASSERT_TRUE(stack.AttachLayerUVE(SettingValueSourceUVE::Project, project));
    ASSERT_TRUE(stack.AttachLayerUVE(SettingValueSourceUVE::User, user));
    ASSERT_TRUE(stack.AttachLayerUVE(SettingValueSourceUVE::Platform, platform));
    ASSERT_TRUE(stack.AttachLayerUVE(SettingValueSourceUVE::CommandLine, commandLine));

    constexpr std::string_view kSettingId = "settings.platformValue";
    auto resolved = stack.ResolveUVE(kSettingId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 5);
    EXPECT_EQ(resolved->source, SettingValueSourceUVE::EngineDefault);

    project.SetIntUVE(kSettingId, 10);
    user.SetIntUVE(kSettingId, 20);
    platform.SetIntUVE(kSettingId, 30);
    commandLine.SetIntUVE(kSettingId, 40);
    resolved = stack.ResolveUVE(kSettingId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 40);
    EXPECT_EQ(resolved->source, SettingValueSourceUVE::CommandLine);

    // A bad high-priority value is absent, not a reason to mask lower layers.
    commandLine.SetIntUVE(kSettingId, 500);
    EXPECT_FALSE(stack.GetStoredValueUVE(SettingValueSourceUVE::CommandLine, kSettingId).has_value());
    resolved = stack.ResolveUVE(kSettingId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 30);
    EXPECT_EQ(resolved->source, SettingValueSourceUVE::Platform);

    commandLine.SetStringUVE(kSettingId, "not an integer");
    EXPECT_FALSE(stack.GetStoredValueUVE(SettingValueSourceUVE::CommandLine, kSettingId).has_value());
    resolved = stack.ResolveUVE(kSettingId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 30);
    EXPECT_EQ(resolved->source, SettingValueSourceUVE::Platform);

    platform.SetIntUVE("settings.nonPlatformValue", 99);
    resolved = stack.ResolveUVE("settings.nonPlatformValue");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 7);
    EXPECT_EQ(resolved->source, SettingValueSourceUVE::EngineDefault);
    EXPECT_FALSE(stack.GetStoredValueUVE(SettingValueSourceUVE::Platform,
                                        "settings.nonPlatformValue").has_value());

    project.SetBoolUVE("settings.sessionValue", true);
    user.SetBoolUVE("settings.sessionValue", true);
    platform.SetBoolUVE("settings.sessionValue", true);
    resolved = stack.ResolveUVE("settings.sessionValue");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_FALSE(std::get<bool>(resolved->value));
    EXPECT_EQ(resolved->source, SettingValueSourceUVE::EngineDefault);
    commandLine.SetBoolUVE("settings.sessionValue", true);
    resolved = stack.ResolveUVE("settings.sessionValue");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_TRUE(std::get<bool>(resolved->value));
    EXPECT_EQ(resolved->source, SettingValueSourceUVE::CommandLine);

    ASSERT_TRUE(stack.DetachLayerUVE(SettingValueSourceUVE::Platform));
    resolved = stack.ResolveUVE(kSettingId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 20);
    EXPECT_EQ(resolved->source, SettingValueSourceUVE::User);

    ASSERT_TRUE(stack.DetachLayerUVE(SettingValueSourceUVE::User));
    resolved = stack.ResolveUVE(kSettingId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 10);
    EXPECT_EQ(resolved->source, SettingValueSourceUVE::Project);

    ASSERT_TRUE(stack.DetachLayerUVE(SettingValueSourceUVE::Project));
    resolved = stack.ResolveUVE(kSettingId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(std::get<std::int64_t>(resolved->value), 5);
    EXPECT_EQ(resolved->source, SettingValueSourceUVE::EngineDefault);
    EXPECT_FALSE(stack.ResolveUVE("settings.unknown").has_value());
    EXPECT_FALSE(stack.DetachLayerUVE(SettingValueSourceUVE::EngineDefault));
}

TEST(SettingDescriptorUVETest, ValidDescriptorsOfEveryTypeHaveNoProblem) {
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeBoolSettingUVE("a.b", false, "A", "Cat")), "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeIntSettingUVE("a.i", 0, std::nullopt, std::nullopt, "I", "")), "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeFloatSettingUVE("a.f", 2.0, 1.0, 3.0, "F", "Cat/Sub Page")), "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeStringSettingUVE("a.s", "abc", 3, "S", "Cat")), "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeStringListSettingUVE("a.list", {"first", "second"}, 3, 16,
                                                                    "List", "Cat")), "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeFilePathSettingUVE("a.path", "Assets/Scene.uvscene", 256,
                                                                  "Scene", "Project")), "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeKeyBindingSettingUVE("a.shortcut", "Ctrl+Shift+S",
                                                                     "Save", "Editor/Shortcuts")), "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeQualityUVE()), "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeColorSettingUVE("a.c", {0.0F, 1.0F, 0.5F}, false, "C", "Cat")), "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(
                  MakeVector2SettingUVE("a.v2", {1.0, -2.0}, -10.0, 10.0, "V2", "Cat")),
              "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(
                  MakeVector4SettingUVE("a.v4", {1.0, -2.0, 3.0, -4.0}, -10.0, 10.0, "V4", "Cat")),
              "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(
                  MakeLayerMaskSettingUVE("physics.collisionMask", {0xFFFFFFFFU}, "Collision Mask", "Physics")),
              "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(
                  MakeAssetReferenceSettingUVE("application.boot.logo", {0xFEDCBA9876543210ULL}, "Boot Logo", "Application")),
              "");
}

TEST(SettingDescriptorUVETest, MalformedIdsAreRejected) {
    for (const char* id : {"", ".a", "a.", "a..b", "a b", "a/b", "a-b"}) {
        EXPECT_NE(ValidateSettingDescriptorUVE(MakeBoolSettingUVE(id, false, "A", "Cat")), "") << "id: " << id;
    }
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeBoolSettingUVE("editor_2.grid_visible", false, "A", "")), "");
}

TEST(SettingDescriptorUVETest, DocumentVersionKeyIsReservedAndReplacementIdsRequireDeprecatedAliases) {
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeBoolSettingUVE("version", false, "Version", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeBoolSettingUVE("version.settings", false, "Version", "")), "");

    SettingDescriptorUVE live = MakeIntSettingUVE("settings.current", 1, 0, 10, "Current", "");
    live.sinceVersion = 2U;
    live.migratedFrom = {"settings.old"};
    live.replacementId = "settings.other";
    EXPECT_NE(ValidateSettingDescriptorUVE(live), "");

    SettingDescriptorUVE alias = MakeIntSettingUVE("settings.old", 1, 0, 10, "Old", "");
    alias.flags |= kSettingFlagDeprecatedUVE;
    alias.replacementId = "settings.current";
    EXPECT_EQ(ValidateSettingDescriptorUVE(alias), "");
    alias.replacementId = alias.id;
    EXPECT_NE(ValidateSettingDescriptorUVE(alias), "");

    SettingsRegistryUVE registry;
    live.replacementId.clear();
    alias.replacementId = "settings.current";
    EXPECT_FALSE(registry.RegisterUVE(alias)); // The live target must be registered first.
    ASSERT_TRUE(registry.RegisterUVE(std::move(live)));
    ASSERT_NE(registry.FindUVE("settings.current"), nullptr);
    EXPECT_EQ(registry.FindUVE("settings.current")->sinceVersion, std::optional<std::uint32_t>{2U});
    EXPECT_EQ(registry.FindUVE("settings.current")->migratedFrom, std::vector<std::string>{"settings.old"});
    EXPECT_TRUE(registry.RegisterUVE(std::move(alias)));
}

TEST(SettingDescriptorUVETest, ValidatesSinceVersionAndMigratedFromMetadata) {
    SettingDescriptorUVE descriptor = MakeBoolSettingUVE("settings.current", false, "Current", "");
    descriptor.sinceVersion = 1U;
    descriptor.migratedFrom = {"settings.old", "legacy.current"};
    EXPECT_EQ(ValidateSettingDescriptorUVE(descriptor), "");

    descriptor.sinceVersion = 0U;
    EXPECT_NE(ValidateSettingDescriptorUVE(descriptor), "");
    descriptor.sinceVersion.reset();

    for (const std::string& oldId : {std::string{}, std::string{".old"}, std::string{"old."},
                                     std::string{"old..id"}, std::string{"settings.current"},
                                     std::string{"version"}, std::string{"version.old"}}) {
        descriptor.migratedFrom = {oldId};
        EXPECT_NE(ValidateSettingDescriptorUVE(descriptor), "") << "migratedFrom: " << oldId;
    }
    descriptor.migratedFrom = {"settings.old", "settings.old"};
    EXPECT_NE(ValidateSettingDescriptorUVE(descriptor), "");

    descriptor.flags |= kSettingFlagDeprecatedUVE;
    descriptor.migratedFrom = {"settings.old"};
    EXPECT_NE(ValidateSettingDescriptorUVE(descriptor), "");
}

TEST(DeprecatedMigrationMetadataUVETest, AliasMustAppearInReplacementHistory) {
    SettingsRegistryUVE registry;
    SettingDescriptorUVE current = MakeIntSettingUVE("settings.current", 1, 0, 10, "Current", "");
    current.migratedFrom = {"settings.former"};
    ASSERT_TRUE(registry.RegisterUVE(std::move(current)));

    SettingDescriptorUVE alias = MakeIntSettingUVE("settings.legacy", 1, 0, 10, "Legacy", "");
    alias.flags |= kSettingFlagDeprecatedUVE;
    alias.replacementId = "settings.current";
    EXPECT_FALSE(registry.RegisterUVE(alias));

    alias.id = "settings.former";
    EXPECT_TRUE(registry.RegisterUVE(std::move(alias)));
}

TEST(SettingDescriptorUVETest, MalformedCategoriesAreRejected) {
    for (const char* category : {"/Editor", "Editor/", "Editor//Grid", "Editor.Grid"}) {
        EXPECT_NE(ValidateSettingDescriptorUVE(MakeBoolSettingUVE("a.b", false, "A", category)), "")
            << "category: " << category;
    }
}

TEST(SettingDescriptorUVETest, DefaultMustSatisfyItsOwnConstraints) {
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeIntSettingUVE("a.i", 0, 1, 10, "I", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeFloatSettingUVE("a.f", 11.0, 1.0, 10.0, "F", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeFloatSettingUVE("a.f", kNaN, std::nullopt, std::nullopt, "F", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeStringSettingUVE("a.s", "abcd", 3, "S", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeStringListSettingUVE("a.list", {"one", "two"}, 1, 8,
                                                                     "List", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeStringListSettingUVE("a.list", {"too-long"}, 2, 3,
                                                                     "List", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeFilePathSettingUVE("a.path", std::string(5, 'x'), 4,
                                                                  "Path", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeKeyBindingSettingUVE("a.shortcut", "Ctrl+Hyper", "Shortcut", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeKeyBindingSettingUVE("a.shortcut", "Ctrl+Ctrl+S", "Shortcut", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeEnumSettingUVE("a.e", 7, {{0, "Zero"}}, "E", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeColorSettingUVE("a.c", {1.5F, 0.0F, 0.0F}, false, "C", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeVector2SettingUVE("a.v2", {0.0, 11.0}, -10.0, 10.0, "V2", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(
                  MakeVector4SettingUVE("a.v4", {0.0, 0.0, 0.0, kNaN}, -10.0, 10.0, "V4", "")),
              "");

    SettingDescriptorUVE boundedMask =
        MakeLayerMaskSettingUVE("a.mask", {0U}, "Mask", "");
    boundedMask.minimum = 0.0;
    EXPECT_NE(ValidateSettingDescriptorUVE(boundedMask), "");
    SettingDescriptorUVE wrongMaskAlternative = MakeLayerMaskSettingUVE("a.mask", {0U}, "Mask", "");
    wrongMaskAlternative.defaultValue = std::int64_t{1};
    EXPECT_NE(ValidateSettingDescriptorUVE(wrongMaskAlternative), "");
    SettingDescriptorUVE wrongAssetReferenceAlternative =
        MakeAssetReferenceSettingUVE("a.asset", {0U}, "Asset", "");
    wrongAssetReferenceAlternative.defaultValue = std::string{"not-a-guid"};
    EXPECT_NE(ValidateSettingDescriptorUVE(wrongAssetReferenceAlternative), "");

    SettingDescriptorUVE wrongAlternative = MakeBoolSettingUVE("a.b", false, "B", "");
    wrongAlternative.defaultValue = std::int64_t{1};
    EXPECT_NE(ValidateSettingDescriptorUVE(wrongAlternative), "");
}

TEST(SettingDescriptorUVETest, StringListRequiresABoundedItemCount) {
    SettingDescriptorUVE noItemLimit = MakeStringListSettingUVE("a.list", {}, 0U, 0U, "List", "");
    EXPECT_NE(ValidateSettingDescriptorUVE(noItemLimit), "");

    SettingDescriptorUVE excessiveLimit = MakeStringListSettingUVE(
        "a.list", {}, kMaximumSettingStringListItemsUVE + 1U, 0U, "List", "");
    EXPECT_NE(ValidateSettingDescriptorUVE(excessiveLimit), "");

    SettingDescriptorUVE notAList = MakeBoolSettingUVE("a.flag", false, "Flag", "");
    notAList.maxItems = 1U;
    EXPECT_NE(ValidateSettingDescriptorUVE(notAList), "");
}

TEST(SettingDescriptorUVETest, BoundsAndStepMustBeCoherent) {
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeFloatSettingUVE("a.f", 1.0, 2.0, 0.0, "F", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeFloatSettingUVE("a.f", 1.0, -kInfinity, 2.0, "F", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeFloatSettingUVE("a.f", 1.0, 0.0, kNaN, "F", "")), "");

    SettingDescriptorUVE zeroStep = MakeFloatSettingUVE("a.f", 1.0, 0.0, 2.0, "F", "");
    zeroStep.step = 0.0;
    EXPECT_NE(ValidateSettingDescriptorUVE(zeroStep), "");

    SettingDescriptorUVE boundedBool = MakeBoolSettingUVE("a.b", false, "B", "");
    boundedBool.maximum = 1.0;
    EXPECT_NE(ValidateSettingDescriptorUVE(boundedBool), "");
}

TEST(SettingDescriptorUVETest, EnumEntriesMustBePresentLabelledAndDistinct) {
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeEnumSettingUVE("a.e", 0, {}, "E", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeEnumSettingUVE("a.e", 0, {{0, ""}}, "E", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeEnumSettingUVE("a.e", 0, {{0, "A"}, {0, "B"}}, "E", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeEnumSettingUVE("a.e", 0, {{0, "A"}, {1, "A"}}, "E", "")), "");

    SettingDescriptorUVE intWithEntries = MakeIntSettingUVE("a.i", 0, std::nullopt, std::nullopt, "I", "");
    intWithEntries.enumEntries = {{0, "Zero"}};
    EXPECT_NE(ValidateSettingDescriptorUVE(intWithEntries), "");
}

TEST(SettingDescriptorUVETest, NonFiniteValuesAreNeverLegal) {
    const SettingDescriptorUVE unbounded = MakeFloatSettingUVE("a.f", 0.0, std::nullopt, std::nullopt, "F", "");
    EXPECT_TRUE(IsSettingValueValidUVE(unbounded, 1.0e300));
    EXPECT_FALSE(IsSettingValueValidUVE(unbounded, kNaN));
    EXPECT_FALSE(IsSettingValueValidUVE(unbounded, kInfinity));
    EXPECT_FALSE(IsSettingValueValidUVE(unbounded, -kInfinity));

    const SettingDescriptorUVE color = MakeColorSettingUVE("a.c", {}, true, "C", "");
    EXPECT_FALSE(IsSettingValueValidUVE(color, SettingColorUVE{0.0F, std::nanf(""), 0.0F, 1.0F}));
    EXPECT_FALSE(IsSettingValueValidUVE(color, SettingColorUVE{0.0F, 0.0F, 0.0F, std::nanf("")}));
}

TEST_F(SettingsRegistryUVETest, KeepsRegistrationOrderAndFindsById) {
    const auto all = registry.GetAllUVE();
    ASSERT_EQ(all.size(), registry.GetCountUVE());
    ASSERT_EQ(all.size(), 7U);
    EXPECT_EQ(all.front()->id, "editor.grid.visible");
    EXPECT_EQ(all.back()->id, "editor.clear.color");
    ASSERT_NE(registry.FindUVE("render.quality"), nullptr);
    EXPECT_EQ(registry.FindUVE("render.quality")->displayName, "Quality");
    EXPECT_EQ(registry.FindUVE("render.missing"), nullptr);
}

TEST_F(SettingsRegistryUVETest, RefusesDuplicatesMalformedAndNestedIds) {
    EXPECT_FALSE(registry.RegisterUVE(MakeBoolSettingUVE("editor.grid.visible", false, "Again", "")));
    EXPECT_FALSE(registry.RegisterUVE(MakeIntSettingUVE("a.i", 0, 1, 10, "Bad default", "")));
    // Under a registered value, and above one.
    EXPECT_FALSE(registry.RegisterUVE(MakeBoolSettingUVE("editor.grid.visible.extra", false, "Child", "")));
    EXPECT_FALSE(registry.RegisterUVE(MakeBoolSettingUVE("editor.grid", false, "Parent", "")));
    // A colour's channels are its own, alpha included even when it has none...
    EXPECT_FALSE(registry.RegisterUVE(MakeFloatSettingUVE("editor.outline.color.r", 0.0, 0.0, 1.0, "Red", "")));
    EXPECT_FALSE(registry.RegisterUVE(MakeFloatSettingUVE("editor.outline.color.a", 0.0, 0.0, 1.0, "Alpha", "")));
    // ...and a colour cannot sit where a value or another setting's object already is.
    EXPECT_FALSE(registry.RegisterUVE(MakeColorSettingUVE("editor.grid.visible", {}, false, "Under a value", "")));
    ASSERT_TRUE(registry.RegisterUVE(MakeBoolSettingUVE("editor.tint.r.locked", false, "Locked", "")));
    EXPECT_FALSE(registry.RegisterUVE(MakeColorSettingUVE("editor.tint", {}, false, "Channel is an object", "")));
    EXPECT_EQ(registry.GetCountUVE(), 8U);
    // A sibling is fine, and so is a setting beside a colour's channels.
    EXPECT_TRUE(registry.RegisterUVE(MakeFloatSettingUVE("editor.grid.fade", 10.0, 0.0, 100.0, "Fade", "")));
    EXPECT_TRUE(registry.RegisterUVE(MakeFloatSettingUVE("editor.outline.color.width", 2.0, 1.0, 6.0, "Width", "")));
    EXPECT_TRUE(registry.RegisterUVE(MakeBoolSettingUVE("editor.outline.color.visible", true, "Visible", "")));
}

TEST_F(SettingsRegistryUVETest, EveryRegisteredDefaultIsLegalAndReadsBackUnmodified) {
    for (const SettingDescriptorUVE* descriptor : registry.GetAllUVE()) {
        EXPECT_EQ(ValidateSettingDescriptorUVE(*descriptor), "") << descriptor->id;
        EXPECT_EQ(registry.GetValueUVE(store, descriptor->id), descriptor->defaultValue) << descriptor->id;
        EXPECT_FALSE(registry.IsModifiedUVE(store, descriptor->id)) << descriptor->id;
    }
}

TEST_F(SettingsRegistryUVETest, TypedGettersReturnStoredValues) {
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.grid.visible", false));
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.autosave.minutes", std::int64_t{30}));
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.grid.opacity", 0.75));
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.layout.name", std::string("Wide")));
    ASSERT_TRUE(registry.SetValueUVE(store, "render.quality", std::int64_t{2}));
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.clear.color", SettingColorUVE{0.0F, 0.5F, 1.0F, 0.25F}));

    EXPECT_FALSE(registry.GetBoolUVE(store, "editor.grid.visible", true));
    EXPECT_EQ(registry.GetIntUVE(store, "editor.autosave.minutes"), 30);
    EXPECT_DOUBLE_EQ(registry.GetFloatUVE(store, "editor.grid.opacity"), 0.75);
    EXPECT_EQ(registry.GetStringUVE(store, "editor.layout.name"), "Wide");
    EXPECT_EQ(registry.GetIntUVE(store, "render.quality"), 2);
    EXPECT_EQ(registry.GetColorUVE(store, "editor.clear.color"), (SettingColorUVE{0.0F, 0.5F, 1.0F, 0.25F}));
    EXPECT_TRUE(registry.IsModifiedUVE(store, "render.quality"));

    // The store holds them where the rest of the engine already looks.
    EXPECT_EQ(store.GetIntUVE("editor.autosave.minutes", 0), 30);
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.clear.color.a", 0.0), 0.25);
}

TEST_F(SettingsRegistryUVETest, FilePathAndKeyBindingUseTextStorageWithTypeSpecificValidation) {
    ASSERT_TRUE(registry.RegisterUVE(MakeFilePathSettingUVE("application.iconPath", "Assets/icon.png", 256,
                                                            "Application Icon", "Application")));
    ASSERT_TRUE(registry.RegisterUVE(MakeKeyBindingSettingUVE("editor.shortcuts.save", "Ctrl+S", "Save", "Editor")));

    ASSERT_TRUE(registry.SetValueUVE(store, "application.iconPath", std::string{"Assets/Studio Icon.png"}));
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.shortcuts.save", std::string{"Ctrl+Shift+F12"}));
    EXPECT_EQ(registry.GetStringUVE(store, "application.iconPath"), "Assets/Studio Icon.png");
    EXPECT_EQ(registry.GetStringUVE(store, "editor.shortcuts.save"), "Ctrl+Shift+F12");

    EXPECT_FALSE(registry.SetValueUVE(store, "application.iconPath", std::string(257U, 'x')));
    EXPECT_FALSE(registry.SetValueUVE(store, "application.iconPath", std::string{"bad\0path", 8U}));
    EXPECT_FALSE(registry.SetValueUVE(store, "editor.shortcuts.save", std::string{"Hyper+S"}));
    EXPECT_FALSE(registry.SetValueUVE(store, "editor.shortcuts.save", std::string{"Ctrl+Ctrl+S"}));
    EXPECT_FALSE(registry.SetValueUVE(store, "editor.shortcuts.save", std::string{"F13"}));

    store.SetStringUVE("editor.shortcuts.save", "Hyper+S");
    EXPECT_EQ(registry.GetValueUVE(store, "editor.shortcuts.save"), SettingValueUVE{std::string{"Ctrl+S"}});
}

TEST_F(SettingsRegistryUVETest, StringListUsesBoundedNumberedStringStorage) {
    constexpr std::string_view kListId = "editor.favoriteProjects";
    const SettingStringListUVE defaultValue{"Default/Project"};
    ASSERT_TRUE(registry.RegisterUVE(MakeStringListSettingUVE(std::string{kListId}, defaultValue, 3U, 32U,
                                                              "Favorite Projects", "Editor/Session")));

    const SettingStringListUVE values{"First/Project", "Second/Project"};
    ASSERT_TRUE(registry.SetValueUVE(store, kListId, values));
    EXPECT_EQ(store.GetIntUVE("editor.favoriteProjects.count", -1), 2);
    EXPECT_EQ(store.GetStringUVE("editor.favoriteProjects.0", ""), values[0]);
    EXPECT_EQ(store.GetStringUVE("editor.favoriteProjects.1", ""), values[1]);
    EXPECT_EQ(registry.GetStringListUVE(store, kListId), values);
    EXPECT_EQ(registry.GetValueUVE(store, kListId), SettingValueUVE{values});
    EXPECT_EQ(registry.GetStringListUVE(store, "editor.grid.visible", {"fallback"}),
              (SettingStringListUVE{"fallback"}));

    EXPECT_FALSE(registry.SetValueUVE(store, kListId, SettingStringListUVE{"one", "two", "three", "four"}));
    EXPECT_FALSE(registry.SetValueUVE(store, kListId, SettingStringListUVE{std::string(33U, 'x')}));
    EXPECT_EQ(registry.GetStringListUVE(store, kListId), values)
        << "a rejected list write must leave all stored entries unchanged";

    // List item paths are reserved, while a nonnumeric sibling under the same JSON object remains legal.
    EXPECT_FALSE(registry.RegisterUVE(MakeBoolSettingUVE("editor.favoriteProjects.1", false, "Collision", "")));
    EXPECT_FALSE(registry.RegisterUVE(MakeBoolSettingUVE("editor.favoriteProjects", false, "Parent", "")));
    EXPECT_TRUE(registry.RegisterUVE(
        MakeBoolSettingUVE("editor.favoriteProjects.descriptionVisible", true, "Sibling", "")));

    ASSERT_TRUE(registry.ClearValueUVE(store, kListId));
    EXPECT_FALSE(store.HasKeyUVE("editor.favoriteProjects.count"));
    EXPECT_FALSE(store.HasKeyUVE("editor.favoriteProjects.0"));
    EXPECT_FALSE(store.HasKeyUVE("editor.favoriteProjects.1"));
    EXPECT_EQ(registry.GetStringListUVE(store, kListId), defaultValue);
}

TEST_F(SettingsRegistryUVETest, TypedGettersAnswerFallbackForUnknownOrMismatchedIds) {
    EXPECT_TRUE(registry.GetBoolUVE(store, "editor.unknown", true));
    EXPECT_EQ(registry.GetIntUVE(store, "editor.grid.opacity", 9), 9);
    EXPECT_EQ(registry.GetStringUVE(store, "editor.grid.visible", "fallback"), "fallback");
    EXPECT_FALSE(registry.GetValueUVE(store, "editor.unknown").has_value());
}

TEST_F(SettingsRegistryUVETest, SetterRejectsIllegalValuesAndLeavesTheStoreUntouched) {
    EXPECT_FALSE(registry.SetValueUVE(store, "editor.autosave.minutes", std::int64_t{0}));
    EXPECT_FALSE(registry.SetValueUVE(store, "editor.autosave.minutes", 5.0));
    EXPECT_FALSE(registry.SetValueUVE(store, "editor.grid.opacity", kNaN));
    EXPECT_FALSE(registry.SetValueUVE(store, "editor.grid.opacity", 1.5));
    EXPECT_FALSE(registry.SetValueUVE(store, "editor.layout.name", std::string(17, 'x')));
    EXPECT_FALSE(registry.SetValueUVE(store, "render.quality", std::int64_t{3}));
    EXPECT_FALSE(registry.SetValueUVE(store, "editor.unknown", true));

    for (const char* key : {"editor.autosave.minutes", "editor.grid.opacity", "editor.layout.name", "render.quality",
                            "editor.unknown"}) {
        EXPECT_FALSE(store.HasKeyUVE(key)) << key;
    }
}

TEST_F(SettingsRegistryUVETest, OneBadChannelRefusesTheWholeColour) {
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.outline.color", SettingColorUVE{0.2F, 0.4F, 0.6F}));
    EXPECT_FALSE(registry.SetValueUVE(store, "editor.outline.color", SettingColorUVE{0.9F, 0.9F, 1.1F}));
    EXPECT_EQ(registry.GetColorUVE(store, "editor.outline.color"), (SettingColorUVE{0.2F, 0.4F, 0.6F}));
}

TEST_F(SettingsRegistryUVETest, ColourWithoutAlphaIsAlwaysOpaqueAndStoresNoAlpha) {
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.outline.color", SettingColorUVE{0.2F, 0.4F, 0.6F, 0.1F}));
    EXPECT_FALSE(store.HasKeyUVE("editor.outline.color.a"));
    EXPECT_FLOAT_EQ(registry.GetColorUVE(store, "editor.outline.color").a, 1.0F);
}

TEST_F(SettingsRegistryUVETest, CorruptValuesFallBackToDefaultsPerSetting) {
    store.SetStringUVE("editor.grid.visible", "yes");  // wrong type
    store.SetIntUVE("editor.autosave.minutes", 500);   // out of range
    store.SetDoubleUVE("editor.grid.opacity", 0.25);   // legal - must survive its neighbours
    store.SetStringUVE("editor.layout.name", std::string(40, 'x'));
    store.SetIntUVE("render.quality", -1);             // not an entry
    store.SetDoubleUVE("editor.outline.color.r", 0.1); // one channel legal...
    store.SetDoubleUVE("editor.outline.color.g", 7.0); // ...one out of range...
    store.SetDoubleUVE("editor.outline.color.b", 0.1);
    store.SetDoubleUVE("editor.clear.color.r", 0.5);   // ...and a colour missing its alpha
    store.SetDoubleUVE("editor.clear.color.g", 0.5);
    store.SetDoubleUVE("editor.clear.color.b", 0.5);

    EXPECT_TRUE(registry.GetBoolUVE(store, "editor.grid.visible"));
    EXPECT_EQ(registry.GetIntUVE(store, "editor.autosave.minutes"), 5);
    EXPECT_DOUBLE_EQ(registry.GetFloatUVE(store, "editor.grid.opacity"), 0.25);
    EXPECT_EQ(registry.GetStringUVE(store, "editor.layout.name"), "Default");
    EXPECT_EQ(registry.GetIntUVE(store, "render.quality"), 1);
    EXPECT_EQ(registry.GetColorUVE(store, "editor.outline.color"), (SettingColorUVE{1.0F, 0.5F, 0.25F}));
    EXPECT_EQ(registry.GetColorUVE(store, "editor.clear.color"), (SettingColorUVE{0.1F, 0.2F, 0.3F, 0.4F}));
    EXPECT_FALSE(registry.IsModifiedUVE(store, "editor.autosave.minutes"));
    EXPECT_TRUE(registry.IsModifiedUVE(store, "editor.grid.opacity"));
}

TEST_F(SettingsRegistryUVETest, HugeStoredChannelFallsBackInsteadOfOverflowing) {
    store.SetDoubleUVE("editor.outline.color.r", 1.0e300);
    store.SetDoubleUVE("editor.outline.color.g", 0.0);
    store.SetDoubleUVE("editor.outline.color.b", 0.0);
    EXPECT_EQ(registry.GetColorUVE(store, "editor.outline.color"), (SettingColorUVE{1.0F, 0.5F, 0.25F}));
}

TEST_F(SettingsRegistryUVETest, WholeNumberWrittenAsDecimalReadsAsInt) {
    store.SetDoubleUVE("editor.autosave.minutes", 15.0);
    EXPECT_EQ(registry.GetIntUVE(store, "editor.autosave.minutes"), 15);
    store.SetDoubleUVE("editor.autosave.minutes", 15.5);
    EXPECT_EQ(registry.GetIntUVE(store, "editor.autosave.minutes"), 5);
    store.SetDoubleUVE("editor.autosave.minutes", 1.0e30);
    EXPECT_EQ(registry.GetIntUVE(store, "editor.autosave.minutes"), 5);
}

TEST_F(SettingsRegistryUVETest, ResetRestoresTheDefault) {
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.grid.opacity", 0.9));
    ASSERT_TRUE(registry.IsModifiedUVE(store, "editor.grid.opacity"));
    ASSERT_TRUE(registry.ResetUVE(store, "editor.grid.opacity"));
    EXPECT_DOUBLE_EQ(registry.GetFloatUVE(store, "editor.grid.opacity"), 0.5);
    EXPECT_FALSE(registry.IsModifiedUVE(store, "editor.grid.opacity"));
    EXPECT_FALSE(registry.ResetUVE(store, "editor.unknown"));
}

TEST_F(SettingsRegistryUVETest, DeprecatedSettingsAreReadButNeverWritten) {
    SettingDescriptorUVE old = MakeIntSettingUVE("editor.legacy.size", 3, 0, 10, "Old Size", "");
    old.flags = kSettingFlagDeprecatedUVE | kSettingFlagHiddenUVE;
    ASSERT_TRUE(registry.RegisterUVE(old));
    EXPECT_TRUE(registry.FindUVE("editor.legacy.size")->HasFlagUVE(kSettingFlagHiddenUVE));

    store.SetIntUVE("editor.legacy.size", 7);
    EXPECT_EQ(registry.GetIntUVE(store, "editor.legacy.size"), 7);
    EXPECT_FALSE(registry.SetValueUVE(store, "editor.legacy.size", std::int64_t{2}));
    EXPECT_EQ(store.GetIntUVE("editor.legacy.size", 0), 7);
}

TEST(DeprecatedAliasUVETest, ReadsMigratesAndThenRemovesTheOldId) {
    SettingsRegistryUVE registry;
    SettingDescriptorUVE current =
        MakeIntSettingUVE("rendering.shadow.resolution", 2048, 512, 4096, "Resolution", "Rendering");
    current.sinceVersion = 2U;
    current.migratedFrom = {"rendering.shadow.mapSize"};
    ASSERT_TRUE(registry.RegisterUVE(std::move(current)));
    SettingDescriptorUVE alias =
        MakeIntSettingUVE("rendering.shadow.mapSize", 2048, 512, 4096, "Legacy Resolution", "Rendering");
    alias.flags = kSettingFlagDeprecatedUVE | kSettingFlagHiddenUVE;
    alias.replacementId = "rendering.shadow.resolution";
    ASSERT_TRUE(registry.RegisterUVE(alias));

    ConfigManagerUVE store;
    store.SetIntUVE("rendering.shadow.mapSize", 1024);
    EXPECT_EQ(registry.GetStoredValueUVE(store, "rendering.shadow.mapSize"), SettingValueUVE{1024});
    EXPECT_EQ(registry.GetStoredValueUVE(store, "rendering.shadow.resolution"), SettingValueUVE{1024});
    EXPECT_FALSE(registry.SetValueUVE(store, "rendering.shadow.mapSize", std::int64_t{4096}));

    EXPECT_TRUE(registry.MigrateDeprecatedValuesUVE(store));
    EXPECT_EQ(store.GetIntUVE("rendering.shadow.resolution", 0), 1024);
    EXPECT_FALSE(store.HasKeyUVE("rendering.shadow.mapSize"));
    EXPECT_FALSE(registry.MigrateDeprecatedValuesUVE(store));

    // The live id wins if both forms are present; the stale alias is removed.
    store.SetIntUVE("rendering.shadow.mapSize", 512);
    store.SetIntUVE("rendering.shadow.resolution", 2048);
    EXPECT_TRUE(registry.MigrateDeprecatedValuesUVE(store));
    EXPECT_EQ(store.GetIntUVE("rendering.shadow.resolution", 0), 2048);
    EXPECT_FALSE(store.HasKeyUVE("rendering.shadow.mapSize"));

    // Resetting the live id also clears any raw deprecated key so it cannot reappear as fallback.
    store.SetIntUVE("rendering.shadow.mapSize", 1024);
    EXPECT_TRUE(registry.ClearValueUVE(store, "rendering.shadow.resolution"));
    EXPECT_FALSE(store.HasKeyUVE("rendering.shadow.mapSize"));
    EXPECT_FALSE(registry.GetStoredValueUVE(store, "rendering.shadow.resolution").has_value());
}

TEST_F(SettingsRegistryUVETest, ValuesSurviveASaveAndReload) {
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.grid.opacity", 0.125));
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.clear.color", SettingColorUVE{1.0F, 0.0F, 0.5F, 0.75F}));
    ASSERT_TRUE(registry.SetValueUVE(store, "render.quality", std::int64_t{0}));
    const std::string path = "uve_settings_registry_roundtrip.uvsettings";
    ASSERT_TRUE(store.SaveUVE(path));

    ConfigManagerUVE reloaded;
    ASSERT_TRUE(reloaded.LoadUVE(path));
    EXPECT_DOUBLE_EQ(registry.GetFloatUVE(reloaded, "editor.grid.opacity"), 0.125);
    EXPECT_EQ(registry.GetColorUVE(reloaded, "editor.clear.color"), (SettingColorUVE{1.0F, 0.0F, 0.5F, 0.75F}));
    EXPECT_EQ(registry.GetIntUVE(reloaded, "render.quality"), 0);
    std::remove(path.c_str());
}

TEST(ConfigManagerRemoveKeyUVETest, RemovesTheLeafAndPrunesObjectsLeftEmpty) {
    ConfigManagerUVE store;
    store.SetIntUVE("a.b.c", 1);
    store.SetIntUVE("a.d", 2);
    EXPECT_TRUE(store.RemoveKeyUVE("a.b.c"));
    EXPECT_FALSE(store.HasKeyUVE("a.b.c"));
    EXPECT_TRUE(store.HasKeyUVE("a.d"));
    // "a.b" is gone with its last value; "a" stays because it still holds "a.d".
    store.SetIntUVE("a.b", 3);
    EXPECT_TRUE(store.HasKeyUVE("a.b"));
    EXPECT_TRUE(store.RemoveKeyUVE("a.d"));
    EXPECT_TRUE(store.RemoveKeyUVE("a.b"));
    const std::string path = "uve_remove_key_pruned.uvsettings";
    ASSERT_TRUE(store.SaveUVE(path));
    ConfigManagerUVE reloaded;
    ASSERT_TRUE(reloaded.LoadUVE(path));
    reloaded.SetIntUVE("probe", 1);
    EXPECT_FALSE(reloaded.HasKeyUVE("a"));
    std::remove(path.c_str());
}

TEST(ConfigManagerRemoveKeyUVETest, RefusesMissingKeysAndObjects) {
    ConfigManagerUVE store;
    store.SetIntUVE("a.b", 1);
    EXPECT_FALSE(store.RemoveKeyUVE("a"));
    EXPECT_FALSE(store.RemoveKeyUVE("a.missing"));
    EXPECT_FALSE(store.RemoveKeyUVE("a.b.c"));
    EXPECT_FALSE(store.RemoveKeyUVE(""));
    EXPECT_TRUE(store.HasKeyUVE("a.b"));
}

TEST_F(SettingsRegistryUVETest, StoredValueIsOnlyALegalValueTheStoreReallyHolds) {
    EXPECT_FALSE(registry.GetStoredValueUVE(store, "editor.grid.visible").has_value());
    store.SetStringUVE("editor.grid.visible", "yes");
    EXPECT_FALSE(registry.GetStoredValueUVE(store, "editor.grid.visible").has_value());
    store.SetBoolUVE("editor.grid.visible", false);
    EXPECT_EQ(registry.GetStoredValueUVE(store, "editor.grid.visible"), SettingValueUVE{false});

    store.SetBoolUVE("editor.layout.name", true);
    EXPECT_FALSE(registry.GetStoredValueUVE(store, "editor.layout.name").has_value());
    store.SetStringUVE("editor.layout.name", "");
    EXPECT_EQ(registry.GetStoredValueUVE(store, "editor.layout.name"), SettingValueUVE{std::string()});

    store.SetStringUVE("editor.grid.opacity", "half");
    EXPECT_FALSE(registry.GetStoredValueUVE(store, "editor.grid.opacity").has_value());
    EXPECT_DOUBLE_EQ(registry.GetFloatUVE(store, "editor.grid.opacity"), 0.5);
    store.SetDoubleUVE("editor.clear.color.r", 0.5);
    EXPECT_FALSE(registry.GetStoredValueUVE(store, "editor.clear.color").has_value());
}

TEST_F(SettingsRegistryUVETest, ClearRemovesEveryKeyOfTheSetting) {
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.clear.color", SettingColorUVE{0.1F, 0.2F, 0.3F, 0.4F}));
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.grid.opacity", 0.75));
    EXPECT_TRUE(registry.ClearValueUVE(store, "editor.clear.color"));
    for (const char* key : {"editor.clear.color.r", "editor.clear.color.g", "editor.clear.color.b",
                            "editor.clear.color.a"}) {
        EXPECT_FALSE(store.HasKeyUVE(key)) << key;
    }
    EXPECT_TRUE(store.HasKeyUVE("editor.grid.opacity"));
    EXPECT_FALSE(registry.ClearValueUVE(store, "editor.clear.color"));
    EXPECT_FALSE(registry.ClearValueUVE(store, "editor.unknown"));
}

TEST(SettingVector3UVETest, IsStoredPerComponentAndValidatedWhole) {
    SettingsRegistryUVE registry;
    ASSERT_TRUE(registry.RegisterUVE(
        MakeVector3SettingUVE("physics.gravity", {0.0, -9.81, 0.0}, -100.0, 100.0, "Gravity", "Physics")));
    ConfigManagerUVE store;
    EXPECT_EQ(registry.GetVector3UVE(store, "physics.gravity"), (SettingVector3UVE{0.0, -9.81, 0.0}));

    ASSERT_TRUE(registry.SetValueUVE(store, "physics.gravity", SettingVector3UVE{1.0, -2.0, 3.0}));
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("physics.gravity.y", 0.0), -2.0);
    EXPECT_EQ(registry.GetVector3UVE(store, "physics.gravity"), (SettingVector3UVE{1.0, -2.0, 3.0}));

    // Bounds hold for each component, and one bad component refuses the vector.
    EXPECT_FALSE(registry.SetValueUVE(store, "physics.gravity", SettingVector3UVE{0.0, -200.0, 0.0}));
    EXPECT_FALSE(registry.SetValueUVE(store, "physics.gravity", SettingVector3UVE{0.0, kNaN, 0.0}));
    EXPECT_EQ(registry.GetVector3UVE(store, "physics.gravity"), (SettingVector3UVE{1.0, -2.0, 3.0}));

    // A vector missing a component reads as its default, never half stored.
    store.RemoveKeyUVE("physics.gravity.z");
    EXPECT_FALSE(registry.GetStoredValueUVE(store, "physics.gravity").has_value());
    EXPECT_EQ(registry.GetVector3UVE(store, "physics.gravity"), (SettingVector3UVE{0.0, -9.81, 0.0}));

    ASSERT_TRUE(registry.SetValueUVE(store, "physics.gravity", SettingVector3UVE{1.0, 1.0, 1.0}));
    EXPECT_TRUE(registry.ClearValueUVE(store, "physics.gravity"));
    EXPECT_FALSE(store.HasKeyUVE("physics.gravity.x"));

    // Its components are its own, but a sibling beside them is fine.
    EXPECT_FALSE(registry.RegisterUVE(MakeFloatSettingUVE("physics.gravity.x", 0.0, 0.0, 1.0, "X", "")));
    EXPECT_TRUE(registry.RegisterUVE(MakeFloatSettingUVE("physics.gravity.scale", 1.0, 0.0, 10.0, "Scale", "")));
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeVector3SettingUVE("a.v", {0.0, 5.0, 0.0}, 0.0, 1.0, "V", "")), "");
}

TEST(SettingCompositeConcurrencyUVETest, VectorAndStringListReadsNeverMixAtomicUpdates) {
    SettingsRegistryUVE registry;
    ASSERT_TRUE(registry.RegisterUVE(
        MakeVector3SettingUVE("physics.gravity", {0.0, 1.0, 2.0}, std::nullopt, std::nullopt, "Gravity", "Physics")));
    ASSERT_TRUE(registry.RegisterUVE(
        MakeStringListSettingUVE("editor.recent", {"first", "one"}, 4U, 16U, "Recent", "Editor")));
    ConfigManagerUVE store;
    ASSERT_TRUE(registry.SetValueUVE(store, "physics.gravity", SettingVector3UVE{0.0, 1.0, 2.0}));
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.recent", SettingStringListUVE{"first", "one"}));

    constexpr std::int64_t kIterations = 20000;
    std::atomic<bool> start{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> snapshotsConsistent{true};
    std::thread writer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::int64_t value = 1; value <= kIterations; ++value) {
            if (!registry.SetValueUVE(store, "physics.gravity",
                                      SettingVector3UVE{static_cast<double>(value),
                                                       static_cast<double>(value + 1),
                                                       static_cast<double>(value + 2)})) {
                snapshotsConsistent.store(false, std::memory_order_release);
                break;
            }
            const SettingStringListUVE list = (value % 2) == 0 ? SettingStringListUVE{"even", "value"}
                                                                : SettingStringListUVE{"odd", "entry", "value"};
            if (!registry.SetValueUVE(store, "editor.recent", list)) {
                snapshotsConsistent.store(false, std::memory_order_release);
                break;
            }
            if ((value % 32) == 0) {
                std::this_thread::yield();
            }
        }
        finished.store(true, std::memory_order_release);
    });

    start.store(true, std::memory_order_release);
    do {
        const SettingVector3UVE vector = registry.GetVector3UVE(store, "physics.gravity");
        if (vector.y != vector.x + 1.0 || vector.z != vector.x + 2.0) {
            snapshotsConsistent.store(false, std::memory_order_release);
        }
        const SettingStringListUVE list = registry.GetStringListUVE(store, "editor.recent");
        const bool validList = list == SettingStringListUVE{"first", "one"} ||
                               list == SettingStringListUVE{"even", "value"} ||
                               list == SettingStringListUVE{"odd", "entry", "value"};
        if (!validList) {
            snapshotsConsistent.store(false, std::memory_order_release);
        }
    } while (!finished.load(std::memory_order_acquire));
    writer.join();

    EXPECT_TRUE(snapshotsConsistent.load(std::memory_order_acquire));
}

TEST(SettingVector2UVETest, IsStoredPerComponentAndValidatedAsOneValue) {
    SettingsRegistryUVE registry;
    ASSERT_TRUE(registry.RegisterUVE(
        MakeVector2SettingUVE("viewport.center", {0.5, 0.5}, 0.0, 1.0, "Center", "Viewport")));
    ConfigManagerUVE store;

    EXPECT_EQ(registry.GetVector2UVE(store, "viewport.center"), (SettingVector2UVE{0.5, 0.5}));
    ASSERT_TRUE(registry.SetValueUVE(store, "viewport.center", SettingVector2UVE{0.25, 0.75}));
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("viewport.center.x", 0.0), 0.25);
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("viewport.center.y", 0.0), 0.75);
    EXPECT_EQ(registry.GetVector2UVE(store, "viewport.center"), (SettingVector2UVE{0.25, 0.75}));
    const std::string path = "uve_settings_vector2_roundtrip.uvsettings";
    ASSERT_TRUE(store.SaveUVE(path));
    ConfigManagerUVE reloaded;
    ASSERT_TRUE(reloaded.LoadUVE(path));
    EXPECT_EQ(registry.GetVector2UVE(reloaded, "viewport.center"), (SettingVector2UVE{0.25, 0.75}));
    std::remove(path.c_str());

    EXPECT_FALSE(registry.SetValueUVE(store, "viewport.center", SettingVector2UVE{0.1, 1.5}));
    EXPECT_EQ(registry.GetVector2UVE(store, "viewport.center"), (SettingVector2UVE{0.25, 0.75}));
    store.RemoveKeyUVE("viewport.center.y");
    EXPECT_FALSE(registry.GetStoredValueUVE(store, "viewport.center").has_value());
    EXPECT_EQ(registry.GetVector2UVE(store, "viewport.center"), (SettingVector2UVE{0.5, 0.5}));

    ASSERT_TRUE(registry.SetValueUVE(store, "viewport.center", SettingVector2UVE{0.2, 0.8}));
    EXPECT_TRUE(registry.ClearValueUVE(store, "viewport.center"));
    EXPECT_FALSE(store.HasKeyUVE("viewport.center.x"));
    EXPECT_FALSE(store.HasKeyUVE("viewport.center.y"));
    EXPECT_FALSE(registry.RegisterUVE(MakeFloatSettingUVE("viewport.center.x", 0.0, 0.0, 1.0, "X", "")));
}

TEST(SettingVector4UVETest, IsStoredPerComponentAndValidatedAsOneValue) {
    SettingsRegistryUVE registry;
    ASSERT_TRUE(registry.RegisterUVE(
        MakeVector4SettingUVE("render.viewport", {0.0, 0.0, 1.0, 1.0}, 0.0, 1.0, "Viewport", "Rendering")));
    ConfigManagerUVE store;

    EXPECT_EQ(registry.GetVector4UVE(store, "render.viewport"), (SettingVector4UVE{0.0, 0.0, 1.0, 1.0}));
    const SettingVector4UVE value{0.1, 0.2, 0.7, 0.8};
    ASSERT_TRUE(registry.SetValueUVE(store, "render.viewport", value));
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("render.viewport.w", 0.0), 0.8);
    EXPECT_EQ(registry.GetVector4UVE(store, "render.viewport"), value);
    const std::string path = "uve_settings_vector4_roundtrip.uvsettings";
    ASSERT_TRUE(store.SaveUVE(path));
    ConfigManagerUVE reloaded;
    ASSERT_TRUE(reloaded.LoadUVE(path));
    EXPECT_EQ(registry.GetVector4UVE(reloaded, "render.viewport"), value);
    std::remove(path.c_str());

    EXPECT_FALSE(registry.SetValueUVE(store, "render.viewport", SettingVector4UVE{0.1, 0.2, 0.7, -0.1}));
    EXPECT_EQ(registry.GetVector4UVE(store, "render.viewport"), value);
    store.SetStringUVE("render.viewport.z", "corrupt");
    EXPECT_FALSE(registry.GetStoredValueUVE(store, "render.viewport").has_value());
    EXPECT_EQ(registry.GetVector4UVE(store, "render.viewport"), (SettingVector4UVE{0.0, 0.0, 1.0, 1.0}));

    ASSERT_TRUE(registry.SetValueUVE(store, "render.viewport", value));
    EXPECT_TRUE(registry.ClearValueUVE(store, "render.viewport"));
    for (const char component : std::string_view{"xyzw"}) {
        EXPECT_FALSE(store.HasKeyUVE(std::string("render.viewport.") + component));
    }
    EXPECT_FALSE(registry.RegisterUVE(MakeFloatSettingUVE("render.viewport.w", 0.0, 0.0, 1.0, "W", "")));
}

TEST(SettingLayerMaskUVETest, StoresUnsignedBitsAndRejectsCorruptOrOutOfRangeScalars) {
    SettingsRegistryUVE registry;
    constexpr std::string_view kId = "physics.collisionMask";
    ASSERT_TRUE(registry.RegisterUVE(
        MakeLayerMaskSettingUVE(std::string{kId}, {0xFFFFFFFFU}, "Collision Mask", "Physics")));
    ConfigManagerUVE store;

    EXPECT_EQ(registry.GetLayerMaskUVE(store, kId), (SettingLayerMaskUVE{0xFFFFFFFFU}));
    EXPECT_FALSE(registry.SetValueUVE(store, kId, std::int64_t{1}));
    EXPECT_FALSE(store.HasKeyUVE(kId));
    constexpr SettingLayerMaskUVE kMask{0x80000005U};
    ASSERT_TRUE(registry.SetValueUVE(store, kId, kMask));
    EXPECT_EQ(store.GetIntUVE(kId, -1), static_cast<std::int64_t>(kMask.bits));
    EXPECT_EQ(registry.GetLayerMaskUVE(store, kId), kMask);

    const std::string path = "uve_settings_layer_mask_roundtrip.uvsettings";
    ASSERT_TRUE(store.SaveUVE(path));
    ConfigManagerUVE reloaded;
    ASSERT_TRUE(reloaded.LoadUVE(path));
    EXPECT_EQ(registry.GetLayerMaskUVE(reloaded, kId), kMask);
    std::remove(path.c_str());

    store.SetDoubleUVE(kId, 4294967295.0); // An exactly representable whole-number JSON value is accepted.
    EXPECT_EQ(registry.GetLayerMaskUVE(store, kId), (SettingLayerMaskUVE{0xFFFFFFFFU}));
    store.SetDoubleUVE(kId, 4294967296.0);
    EXPECT_FALSE(registry.GetStoredValueUVE(store, kId).has_value());
    EXPECT_EQ(registry.GetLayerMaskUVE(store, kId), (SettingLayerMaskUVE{0xFFFFFFFFU}));
    store.SetIntUVE(kId, -1);
    EXPECT_FALSE(registry.GetStoredValueUVE(store, kId).has_value());
    store.SetStringUVE(kId, "0xFFFFFFFF");
    EXPECT_FALSE(registry.GetStoredValueUVE(store, kId).has_value());

    ASSERT_TRUE(registry.SetValueUVE(store, kId, kMask));
    EXPECT_TRUE(registry.ClearValueUVE(store, kId));
    EXPECT_FALSE(store.HasKeyUVE(kId));
    EXPECT_EQ(registry.GetLayerMaskUVE(store, kId), (SettingLayerMaskUVE{0xFFFFFFFFU}));
    EXPECT_FALSE(registry.RegisterUVE(MakeBoolSettingUVE("physics.collisionMask.child", false, "Child", "")));
    EXPECT_EQ(registry.GetLayerMaskUVE(store, "physics.otherMask", {7U}), (SettingLayerMaskUVE{7U}));
}

TEST(SettingAssetReferenceUVETest, PreservesFullGuidWidthAndRejectsMalformedStoredIds) {
    SettingsRegistryUVE registry;
    constexpr std::string_view kId = "application.boot.logo";
    ASSERT_TRUE(registry.RegisterUVE(
        MakeAssetReferenceSettingUVE(std::string{kId}, {0U}, "Boot Logo", "Application")));
    ConfigManagerUVE store;

    EXPECT_EQ(registry.GetAssetReferenceUVE(store, kId), (SettingAssetReferenceUVE{0U}));
    constexpr SettingAssetReferenceUVE kReference{0xFEDCBA9876543210ULL};
    EXPECT_EQ(FormatSettingAssetReferenceUVE(kReference), "fedcba9876543210");
    EXPECT_EQ(ParseSettingAssetReferenceUVE("fedcba9876543210"),
              std::optional<SettingAssetReferenceUVE>{kReference});
    EXPECT_EQ(ParseSettingAssetReferenceUVE(""),
              std::optional<SettingAssetReferenceUVE>{SettingAssetReferenceUVE{0U}});
    EXPECT_FALSE(ParseSettingAssetReferenceUVE("fedcba987654321g").has_value());
    EXPECT_FALSE(ParseSettingAssetReferenceUVE("fedcba987654321").has_value());
    ASSERT_TRUE(registry.SetValueUVE(store, kId, kReference));
    EXPECT_EQ(store.GetStringUVE(kId, ""), "fedcba9876543210");
    EXPECT_EQ(registry.GetAssetReferenceUVE(store, kId), kReference);

    const std::string path = "uve_settings_asset_reference_roundtrip.uvsettings";
    ASSERT_TRUE(store.SaveUVE(path));
    ConfigManagerUVE reloaded;
    ASSERT_TRUE(reloaded.LoadUVE(path));
    EXPECT_EQ(registry.GetAssetReferenceUVE(reloaded, kId), kReference);
    std::remove(path.c_str());

    constexpr SettingAssetReferenceUVE kMaximumReference{std::numeric_limits<std::uint64_t>::max()};
    ASSERT_TRUE(registry.SetValueUVE(store, kId, kMaximumReference));
    EXPECT_EQ(store.GetStringUVE(kId, ""), "ffffffffffffffff");
    EXPECT_EQ(registry.GetAssetReferenceUVE(store, kId), kMaximumReference);

    store.SetStringUVE(kId, "fedcba987654321g");
    EXPECT_FALSE(registry.GetStoredValueUVE(store, kId).has_value());
    EXPECT_EQ(registry.GetAssetReferenceUVE(store, kId), (SettingAssetReferenceUVE{0U}));
    store.SetStringUVE(kId, "fedcba987654321"); // Short, non-canonical values are corrupt.
    EXPECT_FALSE(registry.GetStoredValueUVE(store, kId).has_value());
    store.SetIntUVE(kId, 42);
    EXPECT_FALSE(registry.GetStoredValueUVE(store, kId).has_value());
    store.SetStringUVE(kId, ""); // Empty text is a valid no-asset value for hand-edited documents.
    EXPECT_EQ(registry.GetAssetReferenceUVE(store, kId), (SettingAssetReferenceUVE{0U}));

    ASSERT_TRUE(registry.SetValueUVE(store, kId, kReference));
    EXPECT_TRUE(registry.ClearValueUVE(store, kId));
    EXPECT_FALSE(store.HasKeyUVE(kId));
    EXPECT_EQ(registry.GetAssetReferenceUVE(store, kId), (SettingAssetReferenceUVE{0U}));
    EXPECT_EQ(registry.GetAssetReferenceUVE(store, "application.unknown", {123U}),
              (SettingAssetReferenceUVE{123U}));
}

class SettingsDocumentUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        std::remove(path.c_str());
        std::remove(invalidPath.c_str());
        ASSERT_TRUE(document.GetRegistryUVE().RegisterUVE(
            MakeFloatSettingUVE("physics.ticks", 60.0, 1.0, 1000.0, "Ticks", "Physics")));
        ASSERT_TRUE(document.GetRegistryUVE().RegisterUVE(
            MakeColorSettingUVE("rendering.clear", {0.1F, 0.1F, 0.1F}, false, "Clear", "Rendering")));
    }
    void TearDown() override {
        std::remove(path.c_str());
        std::remove(invalidPath.c_str());
    }

    const std::string path = "uve_settings_document_test.uvsettings";
    const std::string invalidPath = "uve_settings_document_invalid_test.uvsettings";
    SettingsDocumentUVE document;
};

TEST_F(SettingsDocumentUVETest, LoadsEveryHistoricalFixtureThroughForwardMigrationSteps) {
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
    const auto registerSchema = [](SettingsDocumentUVE& versioned) {
        if (!versioned.GetRegistryUVE().RegisterUVE(
                MakeFloatSettingUVE("physics.ticksPerSecond", 60.0, 1.0, 1000.0, "Ticks", "Physics")) ||
            !versioned.SetCurrentVersionUVE(3U)) {
            return false;
        }
        if (!versioned.RegisterMigrationUVE(0U, [](IConfigManagerUVE& store) {
                if (!store.HasKeyUVE("physics.legacyTicks")) {
                    return true;
                }
                const std::int64_t ticks = store.GetIntUVE("physics.legacyTicks", -1);
                if (ticks < 1) {
                    return false;
                }
                store.SetIntUVE("physics.ticks", ticks);
                return store.RemoveKeyUVE("physics.legacyTicks");
            })) {
            return false;
        }
        if (!versioned.RegisterMigrationUVE(1U, [](IConfigManagerUVE& store) {
                if (!store.HasKeyUVE("physics.ticks")) {
                    return true;
                }
                const std::int64_t ticks = store.GetIntUVE("physics.ticks", -1);
                if (ticks < 1) {
                    return false;
                }
                store.SetDoubleUVE("simulation.ticks", static_cast<double>(ticks));
                return store.RemoveKeyUVE("physics.ticks");
            })) {
            return false;
        }
        return versioned.RegisterMigrationUVE(2U, [](IConfigManagerUVE& store) {
            if (!store.HasKeyUVE("simulation.ticks")) {
                return true;
            }
            const double ticks = store.GetDoubleUVE("simulation.ticks", -1.0);
            if (ticks < 1.0) {
                return false;
            }
            store.SetDoubleUVE("physics.ticksPerSecond", ticks);
            return store.RemoveKeyUVE("simulation.ticks");
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

        SettingsDocumentUVE versioned;
        ASSERT_TRUE(registerSchema(versioned)) << fixture.name;
        ASSERT_TRUE(versioned.LoadUVE(fixturePath)) << fixture.name;
        EXPECT_EQ(versioned.GetCurrentVersionUVE(), 3U);
        EXPECT_EQ(versioned.GetStoreUVE().GetIntUVE(kSettingsDocumentVersionKeyUVE, -1), 3);
        EXPECT_EQ(versioned.GetValueUVE("physics.ticksPerSecond"), SettingValueUVE{fixture.ticksPerSecond});
        EXPECT_EQ(versioned.IsDirtyUVE(), !fixture.alreadyCurrent) << fixture.name;
        EXPECT_FALSE(versioned.GetStoreUVE().HasKeyUVE("physics.legacyTicks"));
        EXPECT_FALSE(versioned.GetStoreUVE().HasKeyUVE("physics.ticks"));
        EXPECT_FALSE(versioned.GetStoreUVE().HasKeyUVE("simulation.ticks"));

        ASSERT_TRUE(versioned.SaveUVE()) << fixture.name;
        ConfigManagerUVE persisted;
        ASSERT_TRUE(persisted.LoadUVE(fixturePath)) << fixture.name;
        EXPECT_EQ(persisted.GetIntUVE(kSettingsDocumentVersionKeyUVE, -1), 3);
        EXPECT_DOUBLE_EQ(persisted.GetDoubleUVE("physics.ticksPerSecond", -1.0), fixture.ticksPerSecond);
        std::remove(fixturePath.c_str());
    }
}

TEST_F(SettingsDocumentUVETest, FailedFutureAndStepMigrationsLeaveTheLoadedDocumentUntouched) {
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

    SettingsDocumentUVE versioned;
    ASSERT_TRUE(versioned.GetRegistryUVE().RegisterUVE(
        MakeFloatSettingUVE("physics.ticks", 60.0, 1.0, 1000.0, "Ticks", "Physics")));
    ASSERT_TRUE(versioned.SetCurrentVersionUVE(2U));
    ASSERT_TRUE(versioned.RegisterMigrationUVE(1U, [](IConfigManagerUVE& store) {
        store.SetDoubleUVE("physics.ticks", 1.0); // Prove a failed callback's partial edits are discarded.
        return false;
    }));
    ASSERT_TRUE(versioned.LoadUVE(validPath));
    ASSERT_TRUE(versioned.SetValueUVE("physics.ticks", 180.0));
    const std::filesystem::path pathBeforeFailure = versioned.GetPathUVE();

    EXPECT_FALSE(versioned.LoadUVE(futurePath));
    EXPECT_EQ(versioned.GetPathUVE(), pathBeforeFailure);
    EXPECT_TRUE(versioned.IsDirtyUVE());
    EXPECT_EQ(versioned.GetValueUVE("physics.ticks"), SettingValueUVE{180.0});
    EXPECT_EQ(versioned.GetStoreUVE().GetIntUVE(kSettingsDocumentVersionKeyUVE, -1), 2);

    EXPECT_FALSE(versioned.LoadUVE(invalidVersionPath));
    EXPECT_EQ(versioned.GetPathUVE(), pathBeforeFailure);
    EXPECT_TRUE(versioned.IsDirtyUVE());
    EXPECT_EQ(versioned.GetValueUVE("physics.ticks"), SettingValueUVE{180.0});
    EXPECT_EQ(versioned.GetStoreUVE().GetIntUVE(kSettingsDocumentVersionKeyUVE, -1), 2);

    EXPECT_FALSE(versioned.LoadUVE(failedStepPath));
    EXPECT_EQ(versioned.GetPathUVE(), pathBeforeFailure);
    EXPECT_TRUE(versioned.IsDirtyUVE());
    EXPECT_EQ(versioned.GetValueUVE("physics.ticks"), SettingValueUVE{180.0});
    EXPECT_EQ(versioned.GetStoreUVE().GetIntUVE(kSettingsDocumentVersionKeyUVE, -1), 2);
    EXPECT_FALSE(versioned.SetCurrentVersionUVE(3U));
    EXPECT_FALSE(versioned.RegisterMigrationUVE(1U, [](IConfigManagerUVE&) { return true; }));

    for (const std::string& fixturePath : {validPath, futurePath, invalidVersionPath, failedStepPath}) {
        std::remove(fixturePath.c_str());
    }
}

TEST_F(SettingsDocumentUVETest, LoadingMigratesDeprecatedAliasToItsReplacement) {
    const std::string aliasPath = "uve_settings_document_deprecated_alias.uvsettings";
    std::remove(aliasPath.c_str());
    {
        std::ofstream file(aliasPath);
        ASSERT_TRUE(file.is_open());
        file << R"({"version":1,"physics":{"legacyTicks":120.0}})";
    }

    SettingsDocumentUVE versioned;
    SettingDescriptorUVE current =
        MakeFloatSettingUVE("physics.ticks", 60.0, 1.0, 1000.0, "Ticks", "Physics");
    current.migratedFrom = {"physics.legacyTicks"};
    ASSERT_TRUE(versioned.GetRegistryUVE().RegisterUVE(std::move(current)));
    SettingDescriptorUVE alias = MakeFloatSettingUVE("physics.legacyTicks", 60.0, 1.0, 1000.0,
                                                     "Old Ticks", "Physics");
    alias.flags = kSettingFlagDeprecatedUVE | kSettingFlagHiddenUVE;
    alias.replacementId = "physics.ticks";
    ASSERT_TRUE(versioned.GetRegistryUVE().RegisterUVE(std::move(alias)));

    ASSERT_TRUE(versioned.LoadUVE(aliasPath));
    EXPECT_EQ(versioned.GetValueUVE("physics.ticks"), SettingValueUVE{120.0});
    EXPECT_FALSE(versioned.GetStoreUVE().HasKeyUVE("physics.legacyTicks"));
    EXPECT_TRUE(versioned.IsDirtyUVE());
    ASSERT_TRUE(versioned.SaveUVE());
    std::remove(aliasPath.c_str());
}

TEST_F(SettingsDocumentUVETest, AMissingFileIsEveryDefault) {
    ASSERT_TRUE(document.LoadUVE(path));
    EXPECT_FALSE(document.IsDirtyUVE());
    EXPECT_EQ(document.GetStoreUVE().GetIntUVE(kSettingsDocumentVersionKeyUVE, -1), 1);
    EXPECT_EQ(document.GetValueUVE("physics.ticks"), SettingValueUVE{60.0});
    EXPECT_FALSE(document.GetStoredValueUVE("physics.ticks").has_value());
    EXPECT_FALSE(document.IsModifiedUVE("physics.ticks"));
}

TEST_F(SettingsDocumentUVETest, HoldsOnlyWhatDiffersFromTheDefault) {
    ASSERT_TRUE(document.LoadUVE(path));
    ASSERT_TRUE(document.SetValueUVE("physics.ticks", 120.0));
    EXPECT_TRUE(document.IsDirtyUVE());
    EXPECT_TRUE(document.GetStoreUVE().HasKeyUVE("physics.ticks"));
    ASSERT_TRUE(document.SaveUVE());
    EXPECT_FALSE(document.IsDirtyUVE());

    // Setting the value it already has changes nothing.
    ASSERT_TRUE(document.SetValueUVE("physics.ticks", 120.0));
    EXPECT_FALSE(document.IsDirtyUVE());

    // Setting the default takes the key out of the file.
    ASSERT_TRUE(document.SetValueUVE("physics.ticks", 60.0));
    EXPECT_TRUE(document.IsDirtyUVE());
    EXPECT_FALSE(document.GetStoreUVE().HasKeyUVE("physics.ticks"));
    EXPECT_FALSE(document.GetStoreUVE().HasKeyUVE("physics"));
}

TEST_F(SettingsDocumentUVETest, SettingObserversFireOnlyForEffectiveChangesAndCanUnsubscribe) {
    ASSERT_TRUE(document.LoadUVE(path));
    std::vector<SettingChangedEventUVE> changes;
    const SettingsObserverSubscriptionUVE subscription = document.SubscribeToSettingUVE(
        "physics.ticks", [&changes](const SettingChangedEventUVE& event) { changes.push_back(event); });
    ASSERT_TRUE(subscription.IsValidUVE());

    ASSERT_TRUE(document.SetValueUVE("physics.ticks", 120.0));
    ASSERT_EQ(changes.size(), 1U);
    EXPECT_EQ(changes[0].id, "physics.ticks");
    EXPECT_EQ(changes[0].previousValue, SettingValueUVE{60.0});
    EXPECT_EQ(changes[0].newValue, SettingValueUVE{120.0});

    // A repeated write and a rejected write do not represent a value change.
    ASSERT_TRUE(document.SetValueUVE("physics.ticks", 120.0));
    EXPECT_FALSE(document.SetValueUVE("physics.ticks", 0.0));
    EXPECT_EQ(changes.size(), 1U);

    ASSERT_TRUE(document.ResetUVE("physics.ticks"));
    ASSERT_EQ(changes.size(), 2U);
    EXPECT_EQ(changes[1].previousValue, SettingValueUVE{120.0});
    EXPECT_EQ(changes[1].newValue, SettingValueUVE{60.0});
    EXPECT_TRUE(document.UnsubscribeUVE(subscription));
    EXPECT_FALSE(document.UnsubscribeUVE(subscription));
    ASSERT_TRUE(document.SetValueUVE("physics.ticks", 90.0));
    EXPECT_EQ(changes.size(), 2U);
}

TEST_F(SettingsDocumentUVETest, RemovingInvalidStoredDataDoesNotNotifyWhenEffectiveValueIsUnchanged) {
    {
        std::ofstream fixture(path);
        ASSERT_TRUE(fixture.is_open());
        fixture << R"({"physics":{"ticks":5000}})";
    }
    ASSERT_TRUE(document.LoadUVE(path));
    EXPECT_EQ(document.GetValueUVE("physics.ticks"), SettingValueUVE{60.0});
    EXPECT_FALSE(document.GetStoredValueUVE("physics.ticks").has_value());

    int notifications = 0;
    ASSERT_TRUE(document.SubscribeToSettingUVE(
        "physics.ticks", [&notifications](const SettingChangedEventUVE&) { ++notifications; }).IsValidUVE());
    ASSERT_TRUE(document.SetValueUVE("physics.ticks", 60.0));

    EXPECT_TRUE(document.IsDirtyUVE()); // The corrupt raw key was removed from disk state.
    EXPECT_FALSE(document.GetStoreUVE().HasKeyUVE("physics.ticks"));
    EXPECT_EQ(notifications, 0); // The effective value was the default both before and after cleanup.
}

TEST_F(SettingsDocumentUVETest, CategoryObserversUseSlashBoundaries) {
    ASSERT_TRUE(document.GetRegistryUVE().RegisterUVE(
        MakeFloatSettingUVE("rendering.viewport.scale", 1.0, 0.1, 4.0, "Scale", "Rendering/Viewport")));
    ASSERT_TRUE(document.GetRegistryUVE().RegisterUVE(
        MakeFloatSettingUVE("renderingExtra.scale", 1.0, 0.1, 4.0, "Extra Scale", "RenderingExtra")));
    ASSERT_TRUE(document.LoadUVE(path));

    std::vector<std::string> changedIds;
    const SettingsObserverSubscriptionUVE subscription = document.SubscribeToCategoryUVE(
        "Rendering", [&changedIds](const SettingChangedEventUVE& event) { changedIds.push_back(event.id); });
    ASSERT_TRUE(subscription.IsValidUVE());
    EXPECT_FALSE(document.SubscribeToCategoryUVE("Render", [](const SettingChangedEventUVE&) {}).IsValidUVE());
    EXPECT_FALSE(document.SubscribeToCategoryUVE("", [](const SettingChangedEventUVE&) {}).IsValidUVE());
    EXPECT_FALSE(document.SubscribeToSettingUVE("rendering.unknown", [](const SettingChangedEventUVE&) {})
                     .IsValidUVE());

    ASSERT_TRUE(document.SetValueUVE("rendering.clear", SettingColorUVE{0.5F, 0.25F, 0.75F}));
    ASSERT_TRUE(document.SetValueUVE("rendering.viewport.scale", 2.0));
    ASSERT_TRUE(document.SetValueUVE("renderingExtra.scale", 2.0));
    EXPECT_EQ(changedIds, (std::vector<std::string>{"rendering.clear", "rendering.viewport.scale"}));
    EXPECT_TRUE(document.UnsubscribeUVE(subscription));
}

TEST_F(SettingsDocumentUVETest, UnsubscribeDuringDispatchSkipsTheRemovedObserver) {
    ASSERT_TRUE(document.LoadUVE(path));
    int firstCalls = 0;
    int secondCalls = 0;
    SettingsObserverSubscriptionUVE secondSubscription;
    const SettingsObserverSubscriptionUVE firstSubscription = document.SubscribeToSettingUVE(
        "physics.ticks", [this, &firstCalls, &secondSubscription](const SettingChangedEventUVE&) {
            ++firstCalls;
            EXPECT_TRUE(document.UnsubscribeUVE(secondSubscription));
        });
    secondSubscription = document.SubscribeToSettingUVE(
        "physics.ticks", [&secondCalls](const SettingChangedEventUVE&) { ++secondCalls; });
    ASSERT_TRUE(firstSubscription.IsValidUVE());
    ASSERT_TRUE(secondSubscription.IsValidUVE());

    ASSERT_TRUE(document.SetValueUVE("physics.ticks", 120.0));
    EXPECT_EQ(firstCalls, 1);
    EXPECT_EQ(secondCalls, 0);
    EXPECT_FALSE(document.UnsubscribeUVE(secondSubscription));
}

TEST_F(SettingsDocumentUVETest, ReentrantSettingChangesNotifyImmediatelyWithCompleteValues) {
    ASSERT_TRUE(document.LoadUVE(path));
    std::vector<SettingChangedEventUVE> changes;
    const SettingsObserverSubscriptionUVE subscription = document.SubscribeToSettingUVE(
        "physics.ticks", [this, &changes](const SettingChangedEventUVE& event) {
            changes.push_back(event);
            if (event.newValue == SettingValueUVE{120.0}) {
                EXPECT_TRUE(document.SetValueUVE("physics.ticks", 180.0));
            }
        });
    ASSERT_TRUE(subscription.IsValidUVE());

    ASSERT_TRUE(document.SetValueUVE("physics.ticks", 120.0));

    ASSERT_EQ(changes.size(), 2U);
    EXPECT_EQ(changes[0].previousValue, SettingValueUVE{60.0});
    EXPECT_EQ(changes[0].newValue, SettingValueUVE{120.0});
    EXPECT_EQ(changes[1].previousValue, SettingValueUVE{120.0});
    EXPECT_EQ(changes[1].newValue, SettingValueUVE{180.0});
    EXPECT_EQ(document.GetValueUVE("physics.ticks"), SettingValueUVE{180.0});
}

TEST_F(SettingsDocumentUVETest, ObserverAddedDuringDispatchStartsWithTheNextChange) {
    ASSERT_TRUE(document.LoadUVE(path));
    int lateCalls = 0;
    bool added = false;
    SettingsObserverSubscriptionUVE lateSubscription;
    const SettingsObserverSubscriptionUVE earlySubscription = document.SubscribeToSettingUVE(
        "physics.ticks", [this, &added, &lateCalls, &lateSubscription](const SettingChangedEventUVE&) {
            if (!added) {
                lateSubscription = document.SubscribeToSettingUVE(
                    "physics.ticks", [&lateCalls](const SettingChangedEventUVE&) { ++lateCalls; });
                added = true;
            }
        });
    ASSERT_TRUE(earlySubscription.IsValidUVE());

    ASSERT_TRUE(document.SetValueUVE("physics.ticks", 120.0));
    ASSERT_TRUE(lateSubscription.IsValidUVE());
    EXPECT_EQ(lateCalls, 0);
    ASSERT_TRUE(document.SetValueUVE("physics.ticks", 180.0));
    EXPECT_EQ(lateCalls, 1);
}

TEST_F(SettingsDocumentUVETest, ObserverHandlesCannotUnsubscribeAnotherDocumentsListener) {
    ASSERT_TRUE(document.LoadUVE(path));
    SettingsDocumentUVE otherDocument;
    ASSERT_TRUE(otherDocument.GetRegistryUVE().RegisterUVE(
        MakeFloatSettingUVE("physics.ticks", 60.0, 1.0, 1000.0, "Ticks", "Physics")));
    const SettingsObserverSubscriptionUVE subscription =
        otherDocument.SubscribeToSettingUVE("physics.ticks", [](const SettingChangedEventUVE&) {});
    ASSERT_TRUE(subscription.IsValidUVE());

    EXPECT_FALSE(document.UnsubscribeUVE(subscription));
    EXPECT_TRUE(otherDocument.UnsubscribeUVE(subscription));
}

TEST_F(SettingsDocumentUVETest, SuccessfulLoadNotifiesChangesButFailedLoadLeavesStateAlone) {
    ASSERT_TRUE(document.LoadUVE(path));
    std::vector<SettingChangedEventUVE> changes;
    ASSERT_TRUE(document.SubscribeToSettingUVE(
        "physics.ticks", [&changes](const SettingChangedEventUVE& event) { changes.push_back(event); }).IsValidUVE());

    ASSERT_TRUE(document.SetValueUVE("physics.ticks", 120.0));
    ASSERT_TRUE(document.SaveUVE());
    ASSERT_TRUE(document.SetValueUVE("physics.ticks", 60.0));
    ASSERT_EQ(changes.size(), 2U);
    ASSERT_TRUE(document.IsDirtyUVE());

    {
        std::ofstream malformed(invalidPath);
        ASSERT_TRUE(malformed.is_open());
        malformed << "{ not valid json";
    }
    const std::filesystem::path pathBeforeFailedLoad = document.GetPathUVE();
    EXPECT_FALSE(document.LoadUVE(invalidPath));
    EXPECT_EQ(document.GetPathUVE(), pathBeforeFailedLoad);
    EXPECT_TRUE(document.IsDirtyUVE());
    EXPECT_EQ(document.GetValueUVE("physics.ticks"), SettingValueUVE{60.0});
    EXPECT_EQ(changes.size(), 2U);

    ASSERT_TRUE(document.LoadUVE(path));
    EXPECT_FALSE(document.IsDirtyUVE());
    EXPECT_EQ(document.GetValueUVE("physics.ticks"), SettingValueUVE{120.0});
    ASSERT_EQ(changes.size(), 3U);
    EXPECT_EQ(changes[2].previousValue, SettingValueUVE{60.0});
    EXPECT_EQ(changes[2].newValue, SettingValueUVE{120.0});
}

TEST_F(SettingsDocumentUVETest, LoadingAMissingFileClearsPriorValuesAndNotifiesTheReset) {
    ASSERT_TRUE(document.LoadUVE(path));
    std::vector<SettingChangedEventUVE> changes;
    ASSERT_TRUE(document.SubscribeToSettingUVE(
        "physics.ticks", [&changes](const SettingChangedEventUVE& event) { changes.push_back(event); }).IsValidUVE());
    ASSERT_TRUE(document.SetValueUVE("physics.ticks", 120.0));
    ASSERT_EQ(changes.size(), 1U);

    ASSERT_TRUE(document.LoadUVE(path)); // The file is still missing; this is a successful empty load.
    EXPECT_FALSE(document.IsDirtyUVE());
    EXPECT_EQ(document.GetValueUVE("physics.ticks"), SettingValueUVE{60.0});
    ASSERT_EQ(changes.size(), 2U);
    EXPECT_EQ(changes[1].previousValue, SettingValueUVE{120.0});
    EXPECT_EQ(changes[1].newValue, SettingValueUVE{60.0});
}

TEST_F(SettingsDocumentUVETest, IgnoredAlphaOnOpaqueColourDoesNotDirtyOrNotify) {
    ASSERT_TRUE(document.LoadUVE(path));
    int notifications = 0;
    ASSERT_TRUE(document.SubscribeToSettingUVE(
        "rendering.clear", [&notifications](const SettingChangedEventUVE&) { ++notifications; }).IsValidUVE());

    ASSERT_TRUE(document.SetValueUVE("rendering.clear", SettingColorUVE{0.1F, 0.1F, 0.1F, 0.25F}));

    EXPECT_FALSE(document.IsDirtyUVE());
    EXPECT_FALSE(document.IsModifiedUVE("rendering.clear"));
    EXPECT_FALSE(document.GetStoredValueUVE("rendering.clear").has_value());
    EXPECT_EQ(document.GetValueUVE("rendering.clear"),
              (SettingValueUVE{SettingColorUVE{0.1F, 0.1F, 0.1F, 1.0F}}));
    EXPECT_EQ(notifications, 0);
}

TEST_F(SettingsDocumentUVETest, RefusesIllegalValuesAndSurvivesAReload) {
    ASSERT_TRUE(document.LoadUVE(path));
    EXPECT_FALSE(document.SetValueUVE("physics.ticks", 0.0));
    EXPECT_FALSE(document.SetValueUVE("physics.unknown", 1.0));
    EXPECT_FALSE(document.IsDirtyUVE());
    ASSERT_TRUE(document.SetValueUVE("rendering.clear", SettingColorUVE{0.5F, 0.25F, 1.0F}));
    ASSERT_TRUE(document.SaveUVE());

    SettingsDocumentUVE reloaded;
    ASSERT_TRUE(reloaded.GetRegistryUVE().RegisterUVE(
        MakeColorSettingUVE("rendering.clear", {0.1F, 0.1F, 0.1F}, false, "Clear", "Rendering")));
    ASSERT_TRUE(reloaded.LoadUVE(path));
    EXPECT_EQ(reloaded.GetValueUVE("rendering.clear"), (SettingValueUVE{SettingColorUVE{0.5F, 0.25F, 1.0F}}));
    EXPECT_TRUE(reloaded.IsModifiedUVE("rendering.clear"));
    EXPECT_TRUE(reloaded.ResetUVE("rendering.clear"));
    EXPECT_TRUE(reloaded.IsDirtyUVE());
    EXPECT_FALSE(reloaded.IsModifiedUVE("rendering.clear"));
}

} // namespace
} // namespace UVE::Config::Tests
