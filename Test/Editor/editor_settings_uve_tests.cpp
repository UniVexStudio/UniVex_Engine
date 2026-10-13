// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_settings_uve.h"

#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"
#include "uve/config/config_manager_uve.h"
#include "uve/core/engine_core_uve.h"
#include "uve/editor/editor_uve.h"

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] Core::EngineConfigUVE MakeEditorSettingsObserverTestConfigUVE() {
    Core::EngineConfigUVE config{};
    config.headlessUVE = true;
    config.logFilePath = "uve_editor_settings_observer.log";
    config.settingsFilePath = "uve_editor_settings_observer_settings.json";
    config.projectSettingsFilePath = "uve_editor_settings_observer_project_settings.json";
    config.inputMapFilePath = "uve_editor_settings_observer_input_map.json";
    config.assetDatabaseFilePath = "uve_editor_settings_observer_assets.json";
    config.saveDirectoryPath = "uve_editor_settings_observer_saves";
    config.shaderCachePath = "uve_editor_settings_observer_shader_cache";
    config.shaderSourceRealDirectoryUVE = ::UVE::Tests::RepositoryRootUVE() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefixUVE = "shaders";
    return config;
}

using Config::SettingTypeUVE;

TEST(EditorSettingsUVETest, EveryEditorSettingRegistersOnceWithALegalDefault) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    // Every declared preference, the three viewport axis colours, and one alias per renamed id.
    EXPECT_EQ(registry.GetCountUVE(), 41U + 3U + std::size(kRenamedSettingIdsUVE));
    for (const Config::SettingDescriptorUVE* descriptor : registry.GetAllUVE()) {
        EXPECT_EQ(Config::ValidateSettingDescriptorUVE(*descriptor), "") << descriptor->id;
        EXPECT_TRUE(descriptor->id.starts_with("editor.")) << descriptor->id;
        EXPECT_FALSE(descriptor->displayName.empty()) << descriptor->id;
        EXPECT_TRUE(descriptor->category.starts_with("Editor/")) << descriptor->id;
    }
    // A second declaration of the same settings is refused as duplicates.
    EXPECT_FALSE(RegisterEditorSettingsUVE(registry));
    EXPECT_EQ(registry.GetCountUVE(), 41U + 3U + std::size(kRenamedSettingIdsUVE));
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
        {Id::kBottomDockHeightUVE, SettingTypeUVE::Float},
        {Id::kContentBrowserViewModeUVE, SettingTypeUVE::Enum},
        {Id::kContentBrowserModeUVE, SettingTypeUVE::Enum},
        {Id::kActiveWorkspaceUVE, SettingTypeUVE::Enum},
        {Id::kActiveRightPanelTabUVE, SettingTypeUVE::Enum},
        {Id::kActiveBottomDockUVE, SettingTypeUVE::Enum},
        {Id::kColorPickerAdvancedOpenUVE, SettingTypeUVE::Bool},
        {Id::kColorPickerSavedUVE, SettingTypeUVE::StringList},
        {Id::kColorPickerRecentUVE, SettingTypeUVE::StringList},
        {Id::kInspectorFoldsUVE, SettingTypeUVE::StringList},
        {Id::kFavoriteProjectsUVE, SettingTypeUVE::StringList},
        {Id::kViewportAxisColorXUVE, SettingTypeUVE::Color},
        {Id::kViewportAxisColorYUVE, SettingTypeUVE::Color},
        {Id::kViewportAxisColorZUVE, SettingTypeUVE::Color},
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
        {Id::kNewObjectsUnderSelectionUVE, SettingTypeUVE::Bool},
        {Id::kNewObjectPlacementUVE, SettingTypeUVE::Enum},
        {Id::kPlayPauseOnStartUVE, SettingTypeUVE::Bool},
        {Id::kPlaySaveSceneFirstUVE, SettingTypeUVE::Bool},
        {Id::kPlaySwitchToGameUVE, SettingTypeUVE::Bool},
        {Id::kPlayTintEnabledUVE, SettingTypeUVE::Bool},
        {Id::kPlayTintColorUVE, SettingTypeUVE::Color},
        {Id::kPlayTintStrengthUVE, SettingTypeUVE::Float},
        {Id::kHierarchyRevealSelectionUVE, SettingTypeUVE::Bool},
        {Id::kHierarchyShowIconsUVE, SettingTypeUVE::Bool},
        {Id::kHierarchyShowTypeNameUVE, SettingTypeUVE::Bool},
        {Id::kHierarchyVisibilityColumnUVE, SettingTypeUVE::Enum},
        {Id::kHierarchyDoubleClickUVE, SettingTypeUVE::Enum},
        {Id::kHierarchyDragToReparentUVE, SettingTypeUVE::Bool},
        {Id::kHierarchyTreeLinesUVE, SettingTypeUVE::Enum},
        {Id::kHierarchyIndentWidthUVE, SettingTypeUVE::Float},
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
        // Session state is hidden, and so is a renamed setting's old name: neither is a preference
        // a person chooses, and the alias only exists so a file written before a rename still reads.
        const bool expectedHidden = descriptor->category == "Editor/Session" ||
                                    descriptor->HasFlagUVE(Config::kSettingFlagDeprecatedUVE);
        EXPECT_EQ(descriptor->HasFlagUVE(Config::kSettingFlagHiddenUVE), expectedHidden) << descriptor->id;
    }
}

TEST(EditorSettingsUVETest, StoredKeysStayWhereExistingSettingsFilesHaveThem) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    Config::ConfigManagerUVE store;
    ASSERT_TRUE(registry.SetValueUVE(store, EditorSettingIdUVE::kSelectionOutlineColorUVE,
                                     Config::SettingColorUVE{0.25F, 0.5F, 0.75F}));
    ASSERT_TRUE(registry.SetValueUVE(store, EditorSettingIdUVE::kFavoriteProjectsUVE,
                                     Config::SettingStringListUVE{"Project A", "Project B"}));
    ASSERT_TRUE(registry.SetValueUVE(store, EditorSettingIdUVE::kViewportAxisColorXUVE,
                                     Config::SettingColorUVE{0.125F, 0.25F, 0.5F}));
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.viewport.selectionOutline.r", 0.0), 0.25);
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.viewport.selectionOutline.g", 0.0), 0.5);
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.viewport.selectionOutline.b", 0.0), 0.75);
    EXPECT_FALSE(store.HasKeyUVE("editor.viewport.selectionOutline.a"));
    EXPECT_EQ(store.GetIntUVE("editor.favorites.count", -1), 2);
    EXPECT_EQ(store.GetStringUVE("editor.favorites.0", ""), "Project A");
    EXPECT_EQ(store.GetStringUVE("editor.favorites.1", ""), "Project B");
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.viewport.axisColors.x.r", 0.0), 0.125);
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.viewport.axisColors.x.g", 0.0), 0.25);
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.viewport.axisColors.x.b", 0.0), 0.5);
}

TEST(EditorSettingsUVETest, ARenamedIdMigratesThroughItsDeprecatedAlias) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    Config::ConfigManagerUVE store;
    for (const RenamedSettingIdUVE& renamed : kRenamedSettingIdsUVE) {
        const Config::SettingDescriptorUVE* current = registry.FindUVE(renamed.newId);
        const Config::SettingDescriptorUVE* alias = registry.FindUVE(renamed.oldId);
        ASSERT_NE(current, nullptr) << renamed.newId;
        ASSERT_NE(alias, nullptr) << renamed.oldId;
        EXPECT_EQ(alias->type, current->type) << renamed.oldId;
        EXPECT_EQ(alias->defaultValue, current->defaultValue) << renamed.oldId;
        EXPECT_EQ(alias->enumEntries.size(), current->enumEntries.size()) << renamed.oldId;
        EXPECT_TRUE(alias->HasFlagUVE(Config::kSettingFlagDeprecatedUVE)) << renamed.oldId;
        EXPECT_EQ(alias->replacementId, renamed.newId);
    }

    // Before migration the new id reads through the alias. The old id itself remains read-only.
    store.SetBoolUVE("editor.nodes.addUnderSelection", false);
    EXPECT_EQ(registry.GetStoredValueUVE(store, "editor.nodes.addUnderSelection"), Config::SettingValueUVE{false});
    EXPECT_EQ(registry.GetStoredValueUVE(store, "editor.objects.addUnderSelection"), Config::SettingValueUVE{false});
    EXPECT_FALSE(registry.SetValueUVE(store, "editor.nodes.addUnderSelection", true));

    EXPECT_TRUE(registry.MigrateDeprecatedValuesUVE(store));
    EXPECT_EQ(store.GetBoolUVE("editor.objects.addUnderSelection", true), false);
    EXPECT_FALSE(store.HasKeyUVE("editor.nodes.addUnderSelection"));
    EXPECT_FALSE(registry.MigrateDeprecatedValuesUVE(store));

    // A valid current id wins over a conflicting legacy value; migration removes the stale alias.
    store.SetBoolUVE("editor.nodes.addUnderSelection", true);
    store.SetBoolUVE("editor.objects.addUnderSelection", false);
    EXPECT_TRUE(registry.MigrateDeprecatedValuesUVE(store));
    EXPECT_EQ(store.GetBoolUVE("editor.objects.addUnderSelection", true), false);
    EXPECT_FALSE(store.HasKeyUVE("editor.nodes.addUnderSelection"));
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
    const std::vector<SettingCategoryObjectUVE> tree = BuildSettingCategoryTreeUVE({&a, &b, &c, &d, &e});
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

TEST(EditorSettingsUVETest, EditorSettingObserversReportOnlyEffectiveChangesAndRespectOwnership) {
    Core::EngineCoreUVE engine(MakeEditorSettingsObserverTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE());
        EditorUVE otherEditor(engine.GetServicesUVE());
        const std::string_view id = EditorSettingIdUVE::kPlayPauseOnStartUVE;
        std::vector<Config::SettingChangedEventUVE> exactEvents;
        std::vector<std::string> categoryEventIds;

        const Config::SettingsObserverSubscriptionUVE exact = editor.SubscribeToSettingUVE(
            id, [&exactEvents](const Config::SettingChangedEventUVE& event) { exactEvents.push_back(event); });
        const Config::SettingsObserverSubscriptionUVE category = editor.SubscribeToCategoryUVE(
            "Editor/Play Mode", [&categoryEventIds](const Config::SettingChangedEventUVE& event) {
                categoryEventIds.push_back(event.id);
            });
        ASSERT_TRUE(exact.IsValidUVE());
        ASSERT_TRUE(category.IsValidUVE());
        EXPECT_FALSE(editor.SubscribeToSettingUVE("editor.unknown", [](const auto&) {}).IsValidUVE());
        EXPECT_FALSE(editor.SubscribeToCategoryUVE("Editor/Play Mode Extra", [](const auto&) {}).IsValidUVE());
        EXPECT_FALSE(otherEditor.UnsubscribeUVE(exact));

        EXPECT_TRUE(editor.SetEditorSettingUVE(id, true));
        EXPECT_TRUE(editor.SetEditorSettingUVE(id, true)); // A repeat is not an effective change.
        EXPECT_FALSE(editor.SetEditorSettingUVE(id, std::string{"wrong type"}));
        ASSERT_EQ(exactEvents.size(), 1U);
        EXPECT_EQ(exactEvents.front().id, id);
        EXPECT_EQ(exactEvents.front().previousValue, Config::SettingValueUVE{false});
        EXPECT_EQ(exactEvents.front().newValue, Config::SettingValueUVE{true});
        EXPECT_EQ(categoryEventIds, (std::vector<std::string>{std::string{id}}));

        std::vector<Config::SettingChangedEventUVE> gridEvents;
        const auto gridSubscription = editor.SubscribeToCategoryUVE(
            "Editor/Viewport/Grid", [&gridEvents](const Config::SettingChangedEventUVE& event) {
                gridEvents.push_back(event);
            });
        ASSERT_TRUE(gridSubscription.IsValidUVE());
        const Config::SettingValueUVE previousGridVisible = *editor.GetEditorSettingUVE(
            EditorSettingIdUVE::kGridVisibleUVE);
        const Config::SettingValueUVE previousGridOpacity = *editor.GetEditorSettingUVE(
            EditorSettingIdUVE::kGridOpacityUVE);
        ASSERT_TRUE(editor.SetViewportGridUVE(false, 0.42F));
        ASSERT_EQ(gridEvents.size(), 2U);
        EXPECT_EQ(gridEvents[0U].id, EditorSettingIdUVE::kGridVisibleUVE);
        EXPECT_EQ(gridEvents[0U].previousValue, previousGridVisible);
        EXPECT_EQ(gridEvents[0U].newValue, Config::SettingValueUVE{false});
        EXPECT_EQ(gridEvents[1U].id, EditorSettingIdUVE::kGridOpacityUVE);
        EXPECT_EQ(gridEvents[1U].previousValue, previousGridOpacity);
        EXPECT_EQ(gridEvents[1U].newValue, *editor.GetEditorSettingUVE(EditorSettingIdUVE::kGridOpacityUVE));
        EXPECT_TRUE(editor.UnsubscribeUVE(gridSubscription));

        ASSERT_TRUE(editor.UnsubscribeUVE(exact));
        EXPECT_FALSE(editor.UnsubscribeUVE(exact));
        EXPECT_TRUE(editor.SetEditorSettingUVE(id, false));
        EXPECT_EQ(exactEvents.size(), 1U);
        EXPECT_EQ(categoryEventIds, (std::vector<std::string>{std::string{id}, std::string{id}}));
    }
    engine.Shutdown();
}

TEST(EditorSettingsUVETest, ShortcutSettingChangesAreValidatedAndObservable) {
    Core::EngineCoreUVE engine(MakeEditorSettingsObserverTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE());
        constexpr std::string_view kShortcutId = "editor.shortcuts.file.saveScene.primary";
        const Config::SettingDescriptorUVE* shortcutDescriptor = editor.GetSettingsRegistryUVE().FindUVE(kShortcutId);
        ASSERT_NE(shortcutDescriptor, nullptr);
        EXPECT_EQ(shortcutDescriptor->type, Config::SettingTypeUVE::KeyBinding);
        std::vector<Config::SettingChangedEventUVE> events;
        const auto subscription = editor.SubscribeToSettingUVE(
            kShortcutId, [&events](const Config::SettingChangedEventUVE& event) { events.push_back(event); });
        ASSERT_TRUE(subscription.IsValidUVE());
        ASSERT_EQ(editor.GetEditorSettingUVE(kShortcutId), Config::SettingValueUVE{std::string{"Ctrl+S"}});

        EXPECT_TRUE(editor.SetEditorSettingUVE(kShortcutId, std::string{"Ctrl+Shift+S"}));
        EXPECT_TRUE(editor.SetEditorSettingUVE(kShortcutId, std::string{"Ctrl+Shift+S"}));
        EXPECT_FALSE(editor.SetEditorSettingUVE(kShortcutId, std::string{"Hyper+S"}));
        ASSERT_EQ(events.size(), 1U);
        EXPECT_EQ(events[0U].previousValue, Config::SettingValueUVE{std::string{"Ctrl+S"}});
        EXPECT_EQ(events[0U].newValue, Config::SettingValueUVE{std::string{"Ctrl+Shift+S"}});

        EXPECT_TRUE(editor.SetEditorSettingUVE(kShortcutId, std::string{}));
        EXPECT_TRUE(editor.SetEditorSettingUVE(kShortcutId, std::string{"Ctrl+S"}));
        ASSERT_EQ(events.size(), 3U);
        EXPECT_EQ(events[1U].previousValue, Config::SettingValueUVE{std::string{"Ctrl+Shift+S"}});
        EXPECT_EQ(events[1U].newValue, Config::SettingValueUVE{std::string{}});
        EXPECT_EQ(events[2U].previousValue, Config::SettingValueUVE{std::string{}});
        EXPECT_EQ(events[2U].newValue, Config::SettingValueUVE{std::string{"Ctrl+S"}});
        EXPECT_TRUE(editor.UnsubscribeUVE(subscription));
    }
    engine.Shutdown();
}

TEST(EditorSettingsUVETest, EditorSettingObserverReentrantChangesDispatchImmediately) {
    Core::EngineCoreUVE engine(MakeEditorSettingsObserverTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE());
        namespace Id = EditorSettingIdUVE;
        std::vector<std::string> dispatchOrder;
        const auto subscription = editor.SubscribeToCategoryUVE(
            "Editor/Play Mode", [&editor, &dispatchOrder](const Config::SettingChangedEventUVE& event) {
                dispatchOrder.push_back(event.id);
                if (event.id == Id::kPlayPauseOnStartUVE && std::get<bool>(event.newValue)) {
                    EXPECT_TRUE(editor.SetEditorSettingUVE(Id::kPlaySaveSceneFirstUVE, true));
                }
            });
        ASSERT_TRUE(subscription.IsValidUVE());

        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kPlayPauseOnStartUVE, true));
        EXPECT_EQ(dispatchOrder, (std::vector<std::string>{std::string{Id::kPlayPauseOnStartUVE},
                                                           std::string{Id::kPlaySaveSceneFirstUVE}}));
        EXPECT_TRUE(editor.UnsubscribeUVE(subscription));
    }
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests
