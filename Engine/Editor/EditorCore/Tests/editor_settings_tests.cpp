// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_settings.h"

#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"
#include "config_manager.h"
#include "engine_core.h"
#include "editor.h"

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] Core::EngineConfig MakeEditorSettingsObserverTestConfig() {
    Core::EngineConfig config{};
    config.headless = true;
    config.logFilePath = "uve_editor_settings_observer.log";
    config.settingsFilePath = "uve_editor_settings_observer_settings.json";
    config.projectSettingsFilePath = "uve_editor_settings_observer_project_settings.json";
    config.inputMapFilePath = "uve_editor_settings_observer_input_map.json";
    config.assetDatabaseFilePath = "uve_editor_settings_observer_assets.json";
    config.saveDirectoryPath = "uve_editor_settings_observer_saves";
    config.shaderCachePath = "uve_editor_settings_observer_shader_cache";
    config.shaderSourceRealDirectory = ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefix = "shaders";
    return config;
}

using Config::SettingType;

TEST(EditorSettingsTest, EveryEditorSettingRegistersOnceWithALegalDefault) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEditorSettings(registry));
    // Every declared preference, the three viewport axis colours, and one alias per renamed id.
    EXPECT_EQ(registry.GetCount(), 160U + 3U + std::size(kRenamedSettingIds));
    for (const Config::SettingDescriptor* descriptor : registry.GetAll()) {
        EXPECT_EQ(Config::ValidateSettingDescriptor(*descriptor), "") << descriptor->id;
        EXPECT_TRUE(descriptor->id.starts_with("editor.")) << descriptor->id;
        EXPECT_FALSE(descriptor->displayName.empty()) << descriptor->id;
        EXPECT_TRUE(descriptor->category.starts_with("Editor/")) << descriptor->id;
    }
    // A second declaration of the same settings is refused as duplicates.
    EXPECT_FALSE(RegisterEditorSettings(registry));
    EXPECT_EQ(registry.GetCount(), 160U + 3U + std::size(kRenamedSettingIds));
}

TEST(EditorSettingsTest, EveryIdIsDeclaredWithTheTypeTheEditorReadsItAs) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEditorSettings(registry));
    namespace Id = EditorSettingId;
    const std::pair<std::string_view, SettingType> expected[] = {
        {Id::kScenePanelVisible, SettingType::Bool},
        {Id::kInspectorPanelVisible, SettingType::Bool},
        {Id::kBottomDockVisible, SettingType::Bool},
        {Id::kBottomDockHeight, SettingType::Float},
        {Id::kScenePanelWidth, SettingType::Float},
        {Id::kInspectorPanelWidth, SettingType::Float},
        {Id::kAssetTableViewMode, SettingType::Enum},
        {Id::kAssetTableMode, SettingType::Enum},
        {Id::kActiveWorkspace, SettingType::Enum},
        {Id::kActiveRightPanelTab, SettingType::Enum},
        {Id::kActiveBottomDock, SettingType::Enum},
        {Id::kColorPickerAdvancedOpen, SettingType::Bool},
        {Id::kColorPickerSaved, SettingType::StringList},
        {Id::kColorPickerRecent, SettingType::StringList},
        {Id::kInspectorFolds, SettingType::StringList},
        {Id::kFavoriteProjects, SettingType::StringList},
        {Id::kViewportAxisColorX, SettingType::Color},
        {Id::kViewportAxisColorY, SettingType::Color},
        {Id::kViewportAxisColorZ, SettingType::Color},
        {Id::kSnapEnabled, SettingType::Bool},
        {Id::kSnapTranslateStep, SettingType::Float},
        {Id::kSnapRotateStepDegrees, SettingType::Float},
        {Id::kSnapScaleStep, SettingType::Float},
        {Id::kGridVisible, SettingType::Bool},
        {Id::kGridOpacity, SettingType::Float},
        {Id::kGridCellSize, SettingType::Float},
        {Id::kSelectionOutlineVisible, SettingType::Bool},
        {Id::kSelectionOutlineColor, SettingType::Color},
        {Id::kSelectionOutlineThickness, SettingType::Float},
        {Id::kNewObjectsUnderSelection, SettingType::Bool},
        {Id::kNewObjectPlacement, SettingType::Enum},
        {Id::kPlayPauseOnStart, SettingType::Bool},
        {Id::kPlaySaveSceneFirst, SettingType::Bool},
        {Id::kPlaySwitchToGame, SettingType::Bool},
        {Id::kPlayTintEnabled, SettingType::Bool},
        {Id::kPlayTintColor, SettingType::Color},
        {Id::kPlayTintStrength, SettingType::Float},
        {Id::kHierarchyRevealSelection, SettingType::Bool},
        {Id::kHierarchyShowIcons, SettingType::Bool},
        {Id::kHierarchyShowTypeName, SettingType::Bool},
        {Id::kHierarchyVisibilityColumn, SettingType::Enum},
        {Id::kHierarchyDoubleClick, SettingType::Enum},
        {Id::kHierarchyDragToReparent, SettingType::Bool},
        {Id::kHierarchyTreeLines, SettingType::Enum},
        {Id::kHierarchyIndentWidth, SettingType::Float},
    };
    for (const auto& [id, type] : expected) {
        const Config::SettingDescriptor* descriptor = registry.Find(id);
        ASSERT_NE(descriptor, nullptr) << id;
        EXPECT_EQ(descriptor->type, type) << id;
    }
}

TEST(EditorSettingsTest, SessionStateIsHiddenAndPreferencesAreNot) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEditorSettings(registry));
    for (const Config::SettingDescriptor* descriptor : registry.GetAll()) {
        // Session state is hidden, and so is a renamed setting's old name: neither is a preference
        // a person chooses, and the alias only exists so a file written before a rename still reads.
        const bool expectedHidden = descriptor->category == "Editor/Session" ||
                                    descriptor->HasFlag(Config::kSettingFlagDeprecated);
        EXPECT_EQ(descriptor->HasFlag(Config::kSettingFlagHidden), expectedHidden) << descriptor->id;
    }
}

TEST(EditorSettingsTest, StoredKeysStayWhereExistingSettingsFilesHaveThem) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEditorSettings(registry));
    Config::ConfigManager store;
    ASSERT_TRUE(registry.SetValue(store, EditorSettingId::kSelectionOutlineColor,
                                     Config::SettingColor{0.25F, 0.5F, 0.75F}));
    ASSERT_TRUE(registry.SetValue(store, EditorSettingId::kFavoriteProjects,
                                     Config::SettingStringList{"Project A", "Project B"}));
    ASSERT_TRUE(registry.SetValue(store, EditorSettingId::kViewportAxisColorX,
                                     Config::SettingColor{0.125F, 0.25F, 0.5F}));
    EXPECT_DOUBLE_EQ(store.GetDouble("editor.viewport.selectionOutline.r", 0.0), 0.25);
    EXPECT_DOUBLE_EQ(store.GetDouble("editor.viewport.selectionOutline.g", 0.0), 0.5);
    EXPECT_DOUBLE_EQ(store.GetDouble("editor.viewport.selectionOutline.b", 0.0), 0.75);
    EXPECT_FALSE(store.HasKey("editor.viewport.selectionOutline.a"));
    EXPECT_EQ(store.GetInt("editor.favorites.count", -1), 2);
    EXPECT_EQ(store.GetString("editor.favorites.0", ""), "Project A");
    EXPECT_EQ(store.GetString("editor.favorites.1", ""), "Project B");
    EXPECT_DOUBLE_EQ(store.GetDouble("editor.viewport.axisColors.x.r", 0.0), 0.125);
    EXPECT_DOUBLE_EQ(store.GetDouble("editor.viewport.axisColors.x.g", 0.0), 0.25);
    EXPECT_DOUBLE_EQ(store.GetDouble("editor.viewport.axisColors.x.b", 0.0), 0.5);
}

TEST(EditorSettingsTest, ARenamedIdMigratesThroughItsDeprecatedAlias) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEditorSettings(registry));
    Config::ConfigManager store;
    for (const RenamedSettingId& renamed : kRenamedSettingIds) {
        const Config::SettingDescriptor* current = registry.Find(renamed.newId);
        const Config::SettingDescriptor* alias = registry.Find(renamed.oldId);
        ASSERT_NE(current, nullptr) << renamed.newId;
        ASSERT_NE(alias, nullptr) << renamed.oldId;
        EXPECT_EQ(alias->type, current->type) << renamed.oldId;
        EXPECT_EQ(alias->defaultValue, current->defaultValue) << renamed.oldId;
        EXPECT_EQ(alias->enumEntries.size(), current->enumEntries.size()) << renamed.oldId;
        EXPECT_TRUE(alias->HasFlag(Config::kSettingFlagDeprecated)) << renamed.oldId;
        EXPECT_EQ(alias->replacementId, renamed.newId);
    }

    // Before migration the new id reads through the alias. The old id itself remains read-only.
    store.SetBool("editor.nodes.addUnderSelection", false);
    EXPECT_EQ(registry.GetStoredValue(store, "editor.nodes.addUnderSelection"), Config::SettingValue{false});
    EXPECT_EQ(registry.GetStoredValue(store, "editor.objects.addUnderSelection"), Config::SettingValue{false});
    EXPECT_FALSE(registry.SetValue(store, "editor.nodes.addUnderSelection", true));

    EXPECT_TRUE(registry.MigrateDeprecatedValues(store));
    EXPECT_EQ(store.GetBool("editor.objects.addUnderSelection", true), false);
    EXPECT_FALSE(store.HasKey("editor.nodes.addUnderSelection"));
    EXPECT_FALSE(registry.MigrateDeprecatedValues(store));

    // A valid current id wins over a conflicting legacy value; migration removes the stale alias.
    store.SetBool("editor.nodes.addUnderSelection", true);
    store.SetBool("editor.objects.addUnderSelection", false);
    EXPECT_TRUE(registry.MigrateDeprecatedValues(store));
    EXPECT_EQ(store.GetBool("editor.objects.addUnderSelection", true), false);
    EXPECT_FALSE(store.HasKey("editor.nodes.addUnderSelection"));
}

TEST(EditorSettingsTest, SnapStepsOutsideTheirRangeFallBackInsteadOfReachingAFloat) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEditorSettings(registry));
    Config::ConfigManager store;
    store.SetDouble(EditorSettingId::kSnapTranslateStep, 1.0e300);
    store.SetDouble(EditorSettingId::kSnapRotateStepDegrees, 0.0);
    store.SetDouble(EditorSettingId::kSnapScaleStep, 0.5);
    EXPECT_DOUBLE_EQ(registry.GetFloat(store, EditorSettingId::kSnapTranslateStep), 1.0);
    EXPECT_DOUBLE_EQ(registry.GetFloat(store, EditorSettingId::kSnapRotateStepDegrees), 15.0);
    EXPECT_DOUBLE_EQ(registry.GetFloat(store, EditorSettingId::kSnapScaleStep), 0.5);
}

TEST(EditorSettingsTest, SearchMatchesEveryWordAnywhereIgnoringCase) {
    Config::SettingDescriptor descriptor = Config::MakeFloatSetting(
        "editor.viewport.grid.opacity", 1.0, 0.1, 1.0, "Opacity", "Editor/Viewport/Grid", "How strongly it is drawn.");
    EXPECT_TRUE(MatchesSettingSearch(descriptor, ""));
    EXPECT_TRUE(MatchesSettingSearch(descriptor, "opac"));
    EXPECT_TRUE(MatchesSettingSearch(descriptor, "GRID opacity"));
    EXPECT_TRUE(MatchesSettingSearch(descriptor, "strongly"));
    EXPECT_TRUE(MatchesSettingSearch(descriptor, "viewport.grid"));
    EXPECT_TRUE(MatchesSettingSearch(descriptor, "  grid   "));
    EXPECT_FALSE(MatchesSettingSearch(descriptor, "grid snap"));
    EXPECT_FALSE(MatchesSettingSearch(descriptor, "outline"));
}

TEST(EditorSettingsTest, CategoryMembershipIsByWholeSegments) {
    EXPECT_TRUE(IsInSettingCategory("Editor/Viewport/Grid", "Editor"));
    EXPECT_TRUE(IsInSettingCategory("Editor/Viewport/Grid", "Editor/Viewport"));
    EXPECT_TRUE(IsInSettingCategory("Editor/Viewport/Grid", "Editor/Viewport/Grid"));
    EXPECT_FALSE(IsInSettingCategory("Editor/Viewport/Grid", "Editor/View"));
    EXPECT_FALSE(IsInSettingCategory("Editor/Viewport", "Editor/Viewport/Grid"));
}

TEST(EditorSettingsTest, CategoryTreeListsParentsBeforeChildrenInFirstSeenOrder) {
    const Config::SettingDescriptor a = Config::MakeBoolSetting("a.a", false, "A", "Editor/Viewport/Snapping");
    const Config::SettingDescriptor b = Config::MakeBoolSetting("a.b", false, "B", "Editor/General");
    const Config::SettingDescriptor c = Config::MakeBoolSetting("a.c", false, "C", "Editor/Viewport/Grid");
    const Config::SettingDescriptor d = Config::MakeBoolSetting("a.d", false, "D", "Editor/Viewport/Snapping");
    const Config::SettingDescriptor e = Config::MakeBoolSetting("a.e", false, "E", "");
    const std::vector<SettingCategoryObject> tree = BuildSettingCategoryTree({&a, &b, &c, &d, &e});
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

TEST(EditorSettingsTest, ValuesAreFormattedTheWayAPersonReadsThem) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEditorSettings(registry));
    namespace Id = EditorSettingId;
    const Config::SettingDescriptor& grid = *registry.Find(Id::kGridVisible);
    EXPECT_EQ(FormatSettingValue(grid, true), "On");
    EXPECT_EQ(FormatSettingValue(grid, false), "Off");
    const Config::SettingDescriptor& opacity = *registry.Find(Id::kGridOpacity);
    EXPECT_EQ(FormatSettingValue(opacity, 0.5), "0.5");
    EXPECT_EQ(FormatSettingValue(opacity, 1.0), "1");
    const Config::SettingDescriptor& tab = *registry.Find(Id::kActiveRightPanelTab);
    EXPECT_EQ(FormatSettingValue(tab, std::int64_t{1}), "Import");
    EXPECT_EQ(FormatSettingValue(tab, std::int64_t{9}), "9");
    const Config::SettingDescriptor& outline = *registry.Find(Id::kSelectionOutlineColor);
    EXPECT_EQ(FormatSettingValue(outline, Config::SettingColor{1.0F, 0.0F, 0.0F}), "#FF0000");
    const Config::SettingDescriptor gravity =
        Config::MakeVector3Setting("physics.gravity", {0.0, -9.81, 0.0}, std::nullopt, std::nullopt, "Gravity", "");
    EXPECT_EQ(FormatSettingValue(gravity, gravity.defaultValue), "(0, -9.81, 0)");
}

TEST(EditorSettingsTest, EditorSettingObserversReportOnlyEffectiveChangesAndRespectOwnership) {
    Core::EngineCore engine(MakeEditorSettingsObserverTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices());
        Editor otherEditor(engine.GetServices());
        const std::string_view id = EditorSettingId::kPlayPauseOnStart;
        std::vector<Config::SettingChangedEvent> exactEvents;
        std::vector<std::string> categoryEventIds;

        const Config::SettingsObserverSubscription exact = editor.SubscribeToSetting(
            id, [&exactEvents](const Config::SettingChangedEvent& event) { exactEvents.push_back(event); });
        const Config::SettingsObserverSubscription category = editor.SubscribeToCategory(
            "Editor/Play Mode", [&categoryEventIds](const Config::SettingChangedEvent& event) {
                categoryEventIds.push_back(event.id);
            });
        ASSERT_TRUE(exact.IsValid());
        ASSERT_TRUE(category.IsValid());
        EXPECT_FALSE(editor.SubscribeToSetting("editor.unknown", [](const auto&) {}).IsValid());
        EXPECT_FALSE(editor.SubscribeToCategory("Editor/Play Mode Extra", [](const auto&) {}).IsValid());
        EXPECT_FALSE(otherEditor.Unsubscribe(exact));

        EXPECT_TRUE(editor.SetEditorSetting(id, true));
        EXPECT_TRUE(editor.SetEditorSetting(id, true)); // A repeat is not an effective change.
        EXPECT_FALSE(editor.SetEditorSetting(id, std::string{"wrong type"}));
        ASSERT_EQ(exactEvents.size(), 1U);
        EXPECT_EQ(exactEvents.front().id, id);
        EXPECT_EQ(exactEvents.front().previousValue, Config::SettingValue{false});
        EXPECT_EQ(exactEvents.front().newValue, Config::SettingValue{true});
        EXPECT_EQ(categoryEventIds, (std::vector<std::string>{std::string{id}}));

        std::vector<Config::SettingChangedEvent> gridEvents;
        const auto gridSubscription = editor.SubscribeToCategory(
            "Editor/Viewport/Grid", [&gridEvents](const Config::SettingChangedEvent& event) {
                gridEvents.push_back(event);
            });
        ASSERT_TRUE(gridSubscription.IsValid());
        const Config::SettingValue previousGridVisible = *editor.GetEditorSetting(
            EditorSettingId::kGridVisible);
        const Config::SettingValue previousGridOpacity = *editor.GetEditorSetting(
            EditorSettingId::kGridOpacity);
        ASSERT_TRUE(editor.SetViewportGrid(false, 0.42F));
        ASSERT_EQ(gridEvents.size(), 2U);
        EXPECT_EQ(gridEvents[0U].id, EditorSettingId::kGridVisible);
        EXPECT_EQ(gridEvents[0U].previousValue, previousGridVisible);
        EXPECT_EQ(gridEvents[0U].newValue, Config::SettingValue{false});
        EXPECT_EQ(gridEvents[1U].id, EditorSettingId::kGridOpacity);
        EXPECT_EQ(gridEvents[1U].previousValue, previousGridOpacity);
        EXPECT_EQ(gridEvents[1U].newValue, *editor.GetEditorSetting(EditorSettingId::kGridOpacity));
        EXPECT_TRUE(editor.Unsubscribe(gridSubscription));

        ASSERT_TRUE(editor.Unsubscribe(exact));
        EXPECT_FALSE(editor.Unsubscribe(exact));
        EXPECT_TRUE(editor.SetEditorSetting(id, false));
        EXPECT_EQ(exactEvents.size(), 1U);
        EXPECT_EQ(categoryEventIds, (std::vector<std::string>{std::string{id}, std::string{id}}));
    }
    engine.Shutdown();
}

TEST(EditorSettingsTest, ShortcutSettingChangesAreValidatedAndObservable) {
    Core::EngineCore engine(MakeEditorSettingsObserverTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices());
        constexpr std::string_view kShortcutId = "editor.shortcuts.file.saveScene.primary";
        const Config::SettingDescriptor* shortcutDescriptor = editor.GetSettingsRegistry().Find(kShortcutId);
        ASSERT_NE(shortcutDescriptor, nullptr);
        EXPECT_EQ(shortcutDescriptor->type, Config::SettingType::KeyBinding);
        std::vector<Config::SettingChangedEvent> events;
        const auto subscription = editor.SubscribeToSetting(
            kShortcutId, [&events](const Config::SettingChangedEvent& event) { events.push_back(event); });
        ASSERT_TRUE(subscription.IsValid());
        ASSERT_EQ(editor.GetEditorSetting(kShortcutId), Config::SettingValue{std::string{"Ctrl+S"}});

        EXPECT_TRUE(editor.SetEditorSetting(kShortcutId, std::string{"Ctrl+Shift+S"}));
        EXPECT_TRUE(editor.SetEditorSetting(kShortcutId, std::string{"Ctrl+Shift+S"}));
        EXPECT_FALSE(editor.SetEditorSetting(kShortcutId, std::string{"Hyper+S"}));
        ASSERT_EQ(events.size(), 1U);
        EXPECT_EQ(events[0U].previousValue, Config::SettingValue{std::string{"Ctrl+S"}});
        EXPECT_EQ(events[0U].newValue, Config::SettingValue{std::string{"Ctrl+Shift+S"}});

        EXPECT_TRUE(editor.SetEditorSetting(kShortcutId, std::string{}));
        EXPECT_TRUE(editor.SetEditorSetting(kShortcutId, std::string{"Ctrl+S"}));
        ASSERT_EQ(events.size(), 3U);
        EXPECT_EQ(events[1U].previousValue, Config::SettingValue{std::string{"Ctrl+Shift+S"}});
        EXPECT_EQ(events[1U].newValue, Config::SettingValue{std::string{}});
        EXPECT_EQ(events[2U].previousValue, Config::SettingValue{std::string{}});
        EXPECT_EQ(events[2U].newValue, Config::SettingValue{std::string{"Ctrl+S"}});
        EXPECT_TRUE(editor.Unsubscribe(subscription));
    }
    engine.Shutdown();
}

TEST(EditorSettingsTest, EditorSettingObserverReentrantChangesDispatchImmediately) {
    Core::EngineCore engine(MakeEditorSettingsObserverTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices());
        namespace Id = EditorSettingId;
        std::vector<std::string> dispatchOrder;
        const auto subscription = editor.SubscribeToCategory(
            "Editor/Play Mode", [&editor, &dispatchOrder](const Config::SettingChangedEvent& event) {
                dispatchOrder.push_back(event.id);
                if (event.id == Id::kPlayPauseOnStart && std::get<bool>(event.newValue)) {
                    EXPECT_TRUE(editor.SetEditorSetting(Id::kPlaySaveSceneFirst, true));
                }
            });
        ASSERT_TRUE(subscription.IsValid());

        ASSERT_TRUE(editor.SetEditorSetting(Id::kPlayPauseOnStart, true));
        EXPECT_EQ(dispatchOrder, (std::vector<std::string>{std::string{Id::kPlayPauseOnStart},
                                                           std::string{Id::kPlaySaveSceneFirst}}));
        EXPECT_TRUE(editor.Unsubscribe(subscription));
    }
    engine.Shutdown();
}

TEST(EditorSettingsTest, ExtendedPreferences_SetAndPropagateThroughEditor) {
    Core::EngineCore engine(MakeEditorSettingsObserverTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices());
        EditorExtendedPreferences prefs = editor.GetExtendedPreferences();
        prefs.themePreset = 2;
        prefs.cornerRadiusPx = 8.0;
        prefs.spacingDensity = 0;
        prefs.orbitSensitivity = 2.0;
        prefs.panSensitivity = 1.5;
        prefs.zoomSensitivity = 1.2;
        prefs.hierarchyColorCodeIcons = true;
        prefs.hierarchyExpandDepthLimit = 4;
        editor.SetExtendedPreferences(prefs);

        const EditorExtendedPreferences& updated = editor.GetExtendedPreferences();
        EXPECT_EQ(updated.themePreset, 2);
        EXPECT_DOUBLE_EQ(updated.cornerRadiusPx, 8.0);
        EXPECT_EQ(updated.spacingDensity, 0);
        EXPECT_DOUBLE_EQ(updated.orbitSensitivity, 2.0);
        EXPECT_DOUBLE_EQ(updated.panSensitivity, 1.5);
        EXPECT_DOUBLE_EQ(updated.zoomSensitivity, 1.2);
        EXPECT_TRUE(updated.hierarchyColorCodeIcons);
        EXPECT_EQ(updated.hierarchyExpandDepthLimit, 4);
    }
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests
