// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_settings_uve.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "uve/config/config_manager_uve.h"

namespace UVE::Editor::Tests {
namespace {

using Config::SettingTypeUVE;

TEST(EditorSettingsUVETest, EveryEditorSettingRegistersOnceWithALegalDefault) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    EXPECT_EQ(registry.GetCountUVE(), 18U);
    for (const Config::SettingDescriptorUVE* descriptor : registry.GetAllUVE()) {
        EXPECT_EQ(Config::ValidateSettingDescriptorUVE(*descriptor), "") << descriptor->id;
        EXPECT_TRUE(descriptor->id.starts_with("editor.")) << descriptor->id;
        EXPECT_FALSE(descriptor->displayName.empty()) << descriptor->id;
        EXPECT_TRUE(descriptor->category.starts_with("Editor/")) << descriptor->id;
    }
    // A second declaration of the same settings is refused as duplicates.
    EXPECT_FALSE(RegisterEditorSettingsUVE(registry));
    EXPECT_EQ(registry.GetCountUVE(), 18U);
}

TEST(EditorSettingsUVETest, EveryIdIsDeclaredWithTheTypeTheEditorReadsItAs) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    namespace Id = EditorSettingIdUVE;
    const std::pair<std::string_view, SettingTypeUVE> expected[] = {
        {Id::kScenePanelVisibleUVE, SettingTypeUVE::Bool},
        {Id::kViewportPanelVisibleUVE, SettingTypeUVE::Bool},
        {Id::kInspectorPanelVisibleUVE, SettingTypeUVE::Bool},
        {Id::kBottomDockVisibleUVE, SettingTypeUVE::Bool},
        {Id::kActiveWorkspaceUVE, SettingTypeUVE::Enum},
        {Id::kActiveRightPanelTabUVE, SettingTypeUVE::Enum},
        {Id::kActiveBottomDockUVE, SettingTypeUVE::Enum},
        {Id::kColorPickerAdvancedOpenUVE, SettingTypeUVE::Bool},
        {Id::kSnapEnabledUVE, SettingTypeUVE::Bool},
        {Id::kSnapTranslateStepUVE, SettingTypeUVE::Float},
        {Id::kSnapRotateStepDegreesUVE, SettingTypeUVE::Float},
        {Id::kSnapScaleStepUVE, SettingTypeUVE::Float},
        {Id::kGridVisibleUVE, SettingTypeUVE::Bool},
        {Id::kGridOpacityUVE, SettingTypeUVE::Float},
        {Id::kGridCellSizeUVE, SettingTypeUVE::Float},
        {Id::kSelectionOutlineVisibleUVE, SettingTypeUVE::Bool},
        {Id::kSelectionOutlineColorUVE, SettingTypeUVE::Color},
        {Id::kSelectionOutlineThicknessUVE, SettingTypeUVE::Float},
    };
    for (const auto& [id, type] : expected) {
        const Config::SettingDescriptorUVE* descriptor = registry.FindUVE(id);
        ASSERT_NE(descriptor, nullptr) << id;
        EXPECT_EQ(descriptor->type, type) << id;
    }
}

TEST(EditorSettingsUVETest, SessionStateIsHiddenAndPreferencesAreNot) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    for (const Config::SettingDescriptorUVE* descriptor : registry.GetAllUVE()) {
        EXPECT_EQ(descriptor->HasFlagUVE(Config::kSettingFlagHiddenUVE), descriptor->category == "Editor/Session")
            << descriptor->id;
    }
}

TEST(EditorSettingsUVETest, StoredKeysStayWhereExistingSettingsFilesHaveThem) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    Config::ConfigManagerUVE store;
    ASSERT_TRUE(registry.SetValueUVE(store, EditorSettingIdUVE::kSelectionOutlineColorUVE,
                                     Config::SettingColorUVE{0.25F, 0.5F, 0.75F}));
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.viewport.selectionOutline.r", 0.0), 0.25);
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.viewport.selectionOutline.g", 0.0), 0.5);
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.viewport.selectionOutline.b", 0.0), 0.75);
    EXPECT_FALSE(store.HasKeyUVE("editor.viewport.selectionOutline.a"));
}

TEST(EditorSettingsUVETest, SnapStepsOutsideTheirRangeFallBackInsteadOfReachingAFloat) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    Config::ConfigManagerUVE store;
    store.SetDoubleUVE(EditorSettingIdUVE::kSnapTranslateStepUVE, 1.0e300);
    store.SetDoubleUVE(EditorSettingIdUVE::kSnapRotateStepDegreesUVE, 0.0);
    store.SetDoubleUVE(EditorSettingIdUVE::kSnapScaleStepUVE, 0.5);
    EXPECT_DOUBLE_EQ(registry.GetFloatUVE(store, EditorSettingIdUVE::kSnapTranslateStepUVE), 1.0);
    EXPECT_DOUBLE_EQ(registry.GetFloatUVE(store, EditorSettingIdUVE::kSnapRotateStepDegreesUVE), 15.0);
    EXPECT_DOUBLE_EQ(registry.GetFloatUVE(store, EditorSettingIdUVE::kSnapScaleStepUVE), 0.5);
}

TEST(EditorSettingsUVETest, SearchMatchesEveryWordAnywhereIgnoringCase) {
    Config::SettingDescriptorUVE descriptor = Config::MakeFloatSettingUVE(
        "editor.viewport.grid.opacity", 1.0, 0.1, 1.0, "Opacity", "Editor/Viewport/Grid", "How strongly it is drawn.");
    EXPECT_TRUE(MatchesSettingSearchUVE(descriptor, ""));
    EXPECT_TRUE(MatchesSettingSearchUVE(descriptor, "opac"));
    EXPECT_TRUE(MatchesSettingSearchUVE(descriptor, "GRID opacity"));
    EXPECT_TRUE(MatchesSettingSearchUVE(descriptor, "strongly"));
    EXPECT_TRUE(MatchesSettingSearchUVE(descriptor, "viewport.grid"));
    EXPECT_TRUE(MatchesSettingSearchUVE(descriptor, "  grid   "));
    EXPECT_FALSE(MatchesSettingSearchUVE(descriptor, "grid snap"));
    EXPECT_FALSE(MatchesSettingSearchUVE(descriptor, "outline"));
}

TEST(EditorSettingsUVETest, CategoryMembershipIsByWholeSegments) {
    EXPECT_TRUE(IsInSettingCategoryUVE("Editor/Viewport/Grid", "Editor"));
    EXPECT_TRUE(IsInSettingCategoryUVE("Editor/Viewport/Grid", "Editor/Viewport"));
    EXPECT_TRUE(IsInSettingCategoryUVE("Editor/Viewport/Grid", "Editor/Viewport/Grid"));
    EXPECT_FALSE(IsInSettingCategoryUVE("Editor/Viewport/Grid", "Editor/View"));
    EXPECT_FALSE(IsInSettingCategoryUVE("Editor/Viewport", "Editor/Viewport/Grid"));
}

TEST(EditorSettingsUVETest, CategoryTreeListsParentsBeforeChildrenInFirstSeenOrder) {
    const Config::SettingDescriptorUVE a = Config::MakeBoolSettingUVE("a.a", false, "A", "Editor/Viewport/Snapping");
    const Config::SettingDescriptorUVE b = Config::MakeBoolSettingUVE("a.b", false, "B", "Editor/General");
    const Config::SettingDescriptorUVE c = Config::MakeBoolSettingUVE("a.c", false, "C", "Editor/Viewport/Grid");
    const Config::SettingDescriptorUVE d = Config::MakeBoolSettingUVE("a.d", false, "D", "Editor/Viewport/Snapping");
    const Config::SettingDescriptorUVE e = Config::MakeBoolSettingUVE("a.e", false, "E", "");
    const std::vector<SettingCategoryNodeUVE> tree = BuildSettingCategoryTreeUVE({&a, &b, &c, &d, &e});
    const std::vector<std::pair<std::string, int>> expected = {
        {"Editor", 0}, {"Editor/Viewport", 1}, {"Editor/Viewport/Snapping", 2}, {"Editor/Viewport/Grid", 2},
        {"Editor/General", 1}};
    ASSERT_EQ(tree.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(tree[index].path, expected[index].first);
        EXPECT_EQ(tree[index].depth, expected[index].second);
    }
    EXPECT_EQ(tree[3].name, "Grid");
    EXPECT_EQ(tree[0].name, "Editor");
}

TEST(EditorSettingsUVETest, ValuesAreFormattedTheWayAPersonReadsThem) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    namespace Id = EditorSettingIdUVE;
    const Config::SettingDescriptorUVE& grid = *registry.FindUVE(Id::kGridVisibleUVE);
    EXPECT_EQ(FormatSettingValueUVE(grid, true), "On");
    EXPECT_EQ(FormatSettingValueUVE(grid, false), "Off");
    const Config::SettingDescriptorUVE& opacity = *registry.FindUVE(Id::kGridOpacityUVE);
    EXPECT_EQ(FormatSettingValueUVE(opacity, 0.5), "0.5");
    EXPECT_EQ(FormatSettingValueUVE(opacity, 1.0), "1");
    const Config::SettingDescriptorUVE& tab = *registry.FindUVE(Id::kActiveRightPanelTabUVE);
    EXPECT_EQ(FormatSettingValueUVE(tab, std::int64_t{1}), "Import");
    EXPECT_EQ(FormatSettingValueUVE(tab, std::int64_t{9}), "9");
    const Config::SettingDescriptorUVE& outline = *registry.FindUVE(Id::kSelectionOutlineColorUVE);
    EXPECT_EQ(FormatSettingValueUVE(outline, Config::SettingColorUVE{1.0F, 0.0F, 0.0F}), "#FF0000");
    const Config::SettingDescriptorUVE gravity =
        Config::MakeVector3SettingUVE("physics.gravity", {0.0, -9.81, 0.0}, std::nullopt, std::nullopt, "Gravity", "");
    EXPECT_EQ(FormatSettingValueUVE(gravity, gravity.defaultValue), "(0, -9.81, 0)");
}

} // namespace
} // namespace UVE::Editor::Tests
