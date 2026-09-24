// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/config/settings_registry_uve.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "uve/config/config_manager_uve.h"
#include "uve/config/settings_document_uve.h"

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

TEST(SettingDescriptorUVETest, ValidDescriptorsOfEveryTypeHaveNoProblem) {
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeBoolSettingUVE("a.b", false, "A", "Cat")), "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeIntSettingUVE("a.i", 0, std::nullopt, std::nullopt, "I", "")), "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeFloatSettingUVE("a.f", 2.0, 1.0, 3.0, "F", "Cat/Sub Page")), "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeStringSettingUVE("a.s", "abc", 3, "S", "Cat")), "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeQualityUVE()), "");
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeColorSettingUVE("a.c", {0.0F, 1.0F, 0.5F}, false, "C", "Cat")), "");
}

TEST(SettingDescriptorUVETest, MalformedIdsAreRejected) {
    for (const char* id : {"", ".a", "a.", "a..b", "a b", "a/b", "a-b"}) {
        EXPECT_NE(ValidateSettingDescriptorUVE(MakeBoolSettingUVE(id, false, "A", "Cat")), "") << "id: " << id;
    }
    EXPECT_EQ(ValidateSettingDescriptorUVE(MakeBoolSettingUVE("editor_2.grid_visible", false, "A", "")), "");
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
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeEnumSettingUVE("a.e", 7, {{0, "Zero"}}, "E", "")), "");
    EXPECT_NE(ValidateSettingDescriptorUVE(MakeColorSettingUVE("a.c", {1.5F, 0.0F, 0.0F}, false, "C", "")), "");

    SettingDescriptorUVE wrongAlternative = MakeBoolSettingUVE("a.b", false, "B", "");
    wrongAlternative.defaultValue = std::int64_t{1};
    EXPECT_NE(ValidateSettingDescriptorUVE(wrongAlternative), "");
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

TEST_F(SettingsRegistryUVETest, ValuesSurviveASaveAndReload) {
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.grid.opacity", 0.125));
    ASSERT_TRUE(registry.SetValueUVE(store, "editor.clear.color", SettingColorUVE{1.0F, 0.0F, 0.5F, 0.75F}));
    ASSERT_TRUE(registry.SetValueUVE(store, "render.quality", std::int64_t{0}));
    const std::string path = "uve_settings_registry_roundtrip.uvesettings";
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
    const std::string path = "uve_remove_key_pruned.uvesettings";
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

class SettingsDocumentUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        std::remove(path.c_str());
        ASSERT_TRUE(document.GetRegistryUVE().RegisterUVE(
            MakeFloatSettingUVE("physics.ticks", 60.0, 1.0, 1000.0, "Ticks", "Physics")));
        ASSERT_TRUE(document.GetRegistryUVE().RegisterUVE(
            MakeColorSettingUVE("rendering.clear", {0.1F, 0.1F, 0.1F}, false, "Clear", "Rendering")));
    }
    void TearDown() override { std::remove(path.c_str()); }

    const std::string path = "uve_settings_document_test.uvesettings";
    SettingsDocumentUVE document;
};

TEST_F(SettingsDocumentUVETest, AMissingFileIsEveryDefault) {
    ASSERT_TRUE(document.LoadUVE(path));
    EXPECT_FALSE(document.IsDirtyUVE());
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
