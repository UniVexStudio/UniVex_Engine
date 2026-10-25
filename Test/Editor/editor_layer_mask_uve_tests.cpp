// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_layer_mask_uve.h"
#include "uve/editor/editor_settings_uve.h"
#include "uve/config/config_manager_uve.h"

#include <gtest/gtest.h>

namespace UVE::Editor::Tests {
namespace {

LayerNamesUVE MakeNamesUVE() {
    LayerNamesUVE names;
    names[0] = "Default";
    names[1] = "Player";
    names[3] = "Enemies";
    return names;
}

TEST(EditorLayerMaskUVETest, UnnamedLayersAreCountedFromOne) {
    const LayerNamesUVE names = MakeNamesUVE();
    EXPECT_EQ(GetLayerLabelUVE(names, 0U), "Default");
    EXPECT_EQ(GetLayerLabelUVE(names, 2U), "Layer 3");
    EXPECT_EQ(GetLayerLabelUVE(names, 31U), "Layer 32");
    EXPECT_EQ(GetLayerLabelUVE(names, 40U), "Layer 41");
}

TEST(EditorLayerMaskUVETest, SummaryNamesTheLayersInTheMask) {
    const LayerNamesUVE names = MakeNamesUVE();
    EXPECT_EQ(FormatLayerMaskSummaryUVE(0U, names), "None");
    EXPECT_EQ(FormatLayerMaskSummaryUVE(0xFFFFFFFFU, names), "All");
    EXPECT_EQ(FormatLayerMaskSummaryUVE(0b1U, names), "Default");
    EXPECT_EQ(FormatLayerMaskSummaryUVE(0b1011U, names), "Default, Player, Enemies");
    EXPECT_EQ(FormatLayerMaskSummaryUVE(0b1111U, names), "Default, Player, Layer 3 +1");
    EXPECT_EQ(FormatLayerMaskSummaryUVE(0x80000000U, names), "Layer 32");
    EXPECT_EQ(FormatLayerMaskSummaryUVE(0xFFFFFFFEU, names, 1U), "Player +30");
}

TEST(EditorLayerMaskUVETest, UnnamedLayerDetectionAndSymmetricCollisionMatrixWorkDeterministically) {
    const LayerNamesUVE names = MakeNamesUVE();
    EXPECT_FALSE(HasUnnamedLayerInMaskUVE(0b1011U, names));
    EXPECT_TRUE(HasUnnamedLayerInMaskUVE(0b1111U, names)); // Layer 3 (bit 2) is unnamed

    LayerCollisionMatrixUVE matrix{};
    EXPECT_TRUE(matrix.CanCollideUVE(1U, 3U));
    matrix.SetCollisionUVE(1U, 3U, false);
    EXPECT_FALSE(matrix.CanCollideUVE(1U, 3U));
    EXPECT_FALSE(matrix.CanCollideUVE(3U, 1U));
    matrix.SetCollisionUVE(3U, 1U, true);
    EXPECT_TRUE(matrix.CanCollideUVE(1U, 3U));
    EXPECT_FALSE(matrix.CanCollideUVE(32U, 0U));
}

TEST(EditorLayerMaskUVETest, Part2EditorSettingsRegisterSearchFormatAndRoundTripThroughStore) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    EXPECT_EQ(registry.GetCountUVE(), 160U + 3U + std::size(kRenamedSettingIdsUVE));
    for (const Config::SettingDescriptorUVE* descriptor : registry.GetAllUVE()) {
        EXPECT_EQ(Config::ValidateSettingDescriptorUVE(*descriptor), "") << descriptor->id;
        EXPECT_TRUE(descriptor->id.starts_with("editor.")) << descriptor->id;
    }

    Config::ConfigManagerUVE store;
    ASSERT_TRUE(registry.SetValueUVE(store, EditorSettingIdUVE::kPlayWindowSizeUVE,
                                     Config::SettingVector2UVE{1920.0, 1080.0}));
    ASSERT_TRUE(registry.SetValueUVE(store, EditorSettingIdUVE::kThemeContrastUVE, 1.25));
    ASSERT_TRUE(registry.SetValueUVE(store, EditorSettingIdUVE::kAutosaveIntervalSecondsUVE, 120.0));
    ASSERT_TRUE(registry.SetValueUVE(store, EditorSettingIdUVE::kVcsProviderUVE, std::int64_t{1}));

    const Config::SettingVector2UVE windowSize =
        registry.GetVector2UVE(store, EditorSettingIdUVE::kPlayWindowSizeUVE);
    EXPECT_DOUBLE_EQ(windowSize.x, 1920.0);
    EXPECT_DOUBLE_EQ(windowSize.y, 1080.0);
    EXPECT_DOUBLE_EQ(registry.GetFloatUVE(store, EditorSettingIdUVE::kThemeContrastUVE), 1.25);
    EXPECT_DOUBLE_EQ(registry.GetFloatUVE(store, EditorSettingIdUVE::kAutosaveIntervalSecondsUVE), 120.0);
    EXPECT_EQ(registry.GetIntUVE(store, EditorSettingIdUVE::kVcsProviderUVE), 1);
}

} // namespace
} // namespace UVE::Editor::Tests
