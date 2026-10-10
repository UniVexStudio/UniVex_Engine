// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_settings_uve.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "Support/test_scratch_uve.h"
#include "uve/config/config_manager_uve.h"
#include "uve/config/setting_descriptor_uve.h"
#include "uve/core/engine_core_uve.h"
#include "univex/render/GridSettings.h"
#include "uve/editor/editor_uve.h"
#include "uve/object/scene_object_registry_uve.h"

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

[[nodiscard]] Core::EngineConfigUVE MakeEditorAutoSaveTestConfigUVE() {
    Core::EngineConfigUVE config = MakeEditorSettingsObserverTestConfigUVE();
    config.logFilePath = "uve_editor_autosave.log";
    config.settingsFilePath = "uve_editor_autosave_settings.json";
    config.projectSettingsFilePath = "uve_editor_autosave_project_settings.json";
    config.inputMapFilePath = "uve_editor_autosave_input_map.json";
    config.assetDatabaseFilePath = "uve_editor_autosave_assets.json";
    config.saveDirectoryPath = "uve_editor_autosave_saves";
    config.shaderCachePath = "uve_editor_autosave_shader_cache";
    return config;
}

using Config::SettingTypeUVE;

TEST(EditorSettingsUVETest, EveryEditorSettingRegistersOnceWithALegalDefault) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    // Bound editor settings, shelf schema, the three viewport axis colours, one alias per rename,
    // and one default-extras list per library-creatable object kind.
    std::size_t creatableKinds = 0U;
    for (const Scene::Objects::SceneObjectDescriptorUVE& kind : Scene::Objects::GetSceneObjectDescriptorsUVE()) {
        creatableKinds += kind.libraryCreatable ? 1U : 0U;
    }
    const std::size_t expectedCount = 65U + 3U + std::size(kRenamedSettingIdsUVE) + 1U +
                                      (2U * ContentShelvesUVE::kMaxShelvesUVE) + creatableKinds;
    EXPECT_EQ(registry.GetCountUVE(), expectedCount);
    for (const Config::SettingDescriptorUVE* descriptor : registry.GetAllUVE()) {
        EXPECT_EQ(Config::ValidateSettingDescriptorUVE(*descriptor), "") << descriptor->id;
        EXPECT_TRUE(descriptor->id.starts_with("editor.")) << descriptor->id;
        EXPECT_FALSE(descriptor->displayName.empty()) << descriptor->id;
        EXPECT_TRUE(descriptor->category.starts_with("Editor/")) << descriptor->id;
    }
    // The selection outline's width is no longer a setting at all: the 0.20 scale the host applied
    // to it put every value of its 1-6 px range on the renderer's 1 px floor, so the control could
    // not be seen to do anything and was removed. A key left in a document by an older build has no
    // descriptor to find, which is what makes the removal safe to load.
    EXPECT_EQ(registry.FindUVE("editor.viewport.selectionOutline.thickness"), nullptr);

    // A second declaration of the same settings is refused as duplicates.
    EXPECT_FALSE(RegisterEditorSettingsUVE(registry));
    EXPECT_EQ(registry.GetCountUVE(), expectedCount);
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
        {Id::kContentCreateRecentUVE, SettingTypeUVE::StringList},
        {Id::kPersonalShelvesCountUVE, SettingTypeUVE::Int},
        {Id::kColorPickerAdvancedOpenUVE, SettingTypeUVE::Bool},
        {Id::kColorPickerSavedUVE, SettingTypeUVE::StringList},
        {Id::kColorPickerRecentUVE, SettingTypeUVE::StringList},
        {Id::kColorPickerRgbDisplayUVE, SettingTypeUVE::Enum},
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
        {Id::kGridSubdivisionsUVE, SettingTypeUVE::Int},
        {Id::kGridFadeStartUVE, SettingTypeUVE::Float},
        {Id::kGridFadeEndUVE, SettingTypeUVE::Float},
        {Id::kGridLineTintUVE, SettingTypeUVE::Color},
        {Id::kGridPlaneUVE, SettingTypeUVE::Enum},
        {Id::kSelectionOutlineVisibleUVE, SettingTypeUVE::Bool},
        {Id::kSelectionOutlineColorUVE, SettingTypeUVE::Color},
        {Id::kInspectorAngleDisplayUVE, SettingTypeUVE::Enum},
        {Id::kInspectorFloatPrecisionUVE, SettingTypeUVE::Int},
        {Id::kNewObjectsUnderSelectionUVE, SettingTypeUVE::Bool},
        {Id::kNewObjectPlacementUVE, SettingTypeUVE::Enum},
        {Id::kPlayPauseOnStartUVE, SettingTypeUVE::Bool},
        {Id::kPlayPauseOnErrorUVE, SettingTypeUVE::Bool},
        {Id::kPlaySaveSceneFirstUVE, SettingTypeUVE::Bool},
        {Id::kPlaySwitchToGameUVE, SettingTypeUVE::Bool},
        {Id::kPlayTintEnabledUVE, SettingTypeUVE::Bool},
        {Id::kPlayTintColorUVE, SettingTypeUVE::Color},
        {Id::kPlayTintStrengthUVE, SettingTypeUVE::Float},
        {Id::kHierarchyRevealSelectionUVE, SettingTypeUVE::Bool},
        {Id::kHierarchySelectChildrenUVE, SettingTypeUVE::Bool},
        {Id::kHierarchyShowIconsUVE, SettingTypeUVE::Bool},
        {Id::kHierarchyColorCodeIconsUVE, SettingTypeUVE::Bool},
        {Id::kHierarchyShowComponentBadgesUVE, SettingTypeUVE::Bool},
        {Id::kHierarchyShowTypeNameUVE, SettingTypeUVE::Bool},
        {Id::kHierarchyVisibilityColumnUVE, SettingTypeUVE::Enum},
        {Id::kHierarchyDoubleClickUVE, SettingTypeUVE::Enum},
        {Id::kHierarchyRenameModeUVE, SettingTypeUVE::Enum},
        {Id::kHierarchyDuplicateNameSuffixUVE, SettingTypeUVE::String},
        {Id::kHierarchyFilterModeUVE, SettingTypeUVE::Enum},
        {Id::kHierarchySortModeUVE, SettingTypeUVE::Enum},
        {Id::kHierarchyFilterCaseSensitiveUVE, SettingTypeUVE::Bool},
        {Id::kHierarchyFilterKeepAncestorsUVE, SettingTypeUVE::Bool},
        {Id::kHierarchyDragToReparentUVE, SettingTypeUVE::Bool},
        {Id::kHierarchyConfirmLargeSubtreeReparentUVE, SettingTypeUVE::Bool},
        {Id::kHierarchyConfirmDeleteSubtreeUVE, SettingTypeUVE::Bool},
        {Id::kHierarchyTreeLinesUVE, SettingTypeUVE::Enum},
        {Id::kHierarchyRowHeightUVE, SettingTypeUVE::Float},
        {Id::kHierarchyIndentWidthUVE, SettingTypeUVE::Float},
        {Id::kCommandPaletteEnabledUVE, SettingTypeUVE::Bool},
        {Id::kCommandPaletteMatchModeUVE, SettingTypeUVE::Enum},
        {Id::kCommandPaletteRecentCountUVE, SettingTypeUVE::Int},
    };
    for (const auto& [id, type] : expected) {
        const Config::SettingDescriptorUVE* descriptor = registry.FindUVE(id);
        ASSERT_NE(descriptor, nullptr) << id;
        EXPECT_EQ(descriptor->type, type) << id;
    }
    const Config::SettingDescriptorUVE* const sortMode = registry.FindUVE(Id::kHierarchySortModeUVE);
    ASSERT_NE(sortMode, nullptr);
    ASSERT_EQ(sortMode->enumEntries.size(), 3U);
    EXPECT_EQ(sortMode->enumEntries[0U].value, static_cast<std::int64_t>(HierarchySortModeUVE::SceneOrder));
    EXPECT_EQ(sortMode->enumEntries[0U].label, "Scene Order");
    EXPECT_EQ(sortMode->enumEntries[1U].value, static_cast<std::int64_t>(HierarchySortModeUVE::Alphabetical));
    EXPECT_EQ(sortMode->enumEntries[1U].label, "Alphabetical");
    EXPECT_EQ(sortMode->enumEntries[2U].value, static_cast<std::int64_t>(HierarchySortModeUVE::ByType));
    EXPECT_EQ(sortMode->enumEntries[2U].label, "By Type");
    for (std::size_t index = 0U; index < ContentShelvesUVE::kMaxShelvesUVE; ++index) {
        const Config::SettingDescriptorUVE* name =
            registry.FindUVE(Id::GetPersonalShelfNameSettingIdUVE(index));
        const Config::SettingDescriptorUVE* items =
            registry.FindUVE(Id::GetPersonalShelfItemsSettingIdUVE(index));
        ASSERT_NE(name, nullptr) << index;
        ASSERT_NE(items, nullptr) << index;
        EXPECT_EQ(name->type, SettingTypeUVE::String) << index;
        EXPECT_EQ(items->type, SettingTypeUVE::StringList) << index;
        EXPECT_EQ(items->maxItems, ContentShelvesUVE::kMaxItemsPerShelfUVE) << index;
        EXPECT_TRUE(name->HasFlagUVE(Config::kSettingFlagHiddenUVE)) << index;
        EXPECT_TRUE(items->HasFlagUVE(Config::kSettingFlagHiddenUVE)) << index;
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
    ASSERT_TRUE(registry.SetValueUVE(store, EditorSettingIdUVE::kContentCreateRecentUVE,
                                     Config::SettingStringListUVE{"box", "player"}));
    ASSERT_TRUE(registry.SetValueUVE(store, EditorSettingIdUVE::kHierarchySelectChildrenUVE, true));
    ASSERT_TRUE(registry.SetValueUVE(
        store, EditorSettingIdUVE::kHierarchySortModeUVE,
        static_cast<std::int64_t>(HierarchySortModeUVE::Alphabetical)));
    ASSERT_TRUE(registry.SetValueUVE(store, EditorSettingIdUVE::kPersonalShelvesCountUVE, std::int64_t{1}));
    ASSERT_TRUE(registry.SetValueUVE(store, EditorSettingIdUVE::GetPersonalShelfNameSettingIdUVE(0U),
                                     std::string{"Models"}));
    ASSERT_TRUE(registry.SetValueUVE(store, EditorSettingIdUVE::GetPersonalShelfItemsSettingIdUVE(0U),
                                     Config::SettingStringListUVE{"Meshes/Hero.uvmesh", "Meshes/World.uvmesh"}));
    ASSERT_TRUE(registry.SetValueUVE(store, EditorSettingIdUVE::kViewportAxisColorXUVE,
                                     Config::SettingColorUVE{0.125F, 0.25F, 0.5F}));
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.viewport.selectionOutline.r", 0.0), 0.25);
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.viewport.selectionOutline.g", 0.0), 0.5);
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.viewport.selectionOutline.b", 0.0), 0.75);
    EXPECT_FALSE(store.HasKeyUVE("editor.viewport.selectionOutline.a"));
    EXPECT_EQ(store.GetIntUVE("editor.favorites.count", -1), 2);
    EXPECT_EQ(store.GetStringUVE("editor.favorites.0", ""), "Project A");
    EXPECT_EQ(store.GetStringUVE("editor.favorites.1", ""), "Project B");
    EXPECT_EQ(store.GetIntUVE("editor.content.recent.count", -1), 2);
    EXPECT_EQ(store.GetStringUVE("editor.content.recent.0", ""), "box");
    EXPECT_EQ(store.GetStringUVE("editor.content.recent.1", ""), "player");
    EXPECT_TRUE(store.GetBoolUVE("editor.hierarchy.selectChildren", false));
    EXPECT_EQ(store.GetIntUVE("editor.hierarchy.sortMode", -1),
              static_cast<std::int64_t>(HierarchySortModeUVE::Alphabetical));
    EXPECT_EQ(store.GetIntUVE("editor.shelves.count", -1), 1);
    EXPECT_EQ(store.GetStringUVE("editor.shelves.0.name", ""), "Models");
    EXPECT_EQ(store.GetIntUVE("editor.shelves.0.items.count", -1), 2);
    EXPECT_EQ(store.GetStringUVE("editor.shelves.0.items.0", ""), "Meshes/Hero.uvmesh");
    EXPECT_EQ(store.GetStringUVE("editor.shelves.0.items.1", ""), "Meshes/World.uvmesh");
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.viewport.axisColors.x.r", 0.0), 0.125);
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.viewport.axisColors.x.g", 0.0), 0.25);
    EXPECT_DOUBLE_EQ(store.GetDoubleUVE("editor.viewport.axisColors.x.b", 0.0), 0.5);
}

TEST(EditorSettingsUVETest, ContentCreationRecentsKeepTheirBoundedPrefixFromAnOverLimitStoredList) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    const Config::SettingDescriptorUVE* descriptor =
        registry.FindUVE(EditorSettingIdUVE::kContentCreateRecentUVE);
    ASSERT_NE(descriptor, nullptr);
    Config::ConfigManagerUVE store;
    store.SetIntUVE("editor.content.recent.count", static_cast<std::int64_t>(descriptor->maxItems + 1U));
    for (std::size_t index = 0U; index <= descriptor->maxItems; ++index) {
        store.SetStringUVE("editor.content.recent." + std::to_string(index), "asset-" + std::to_string(index));
    }

    const std::optional<Config::SettingValueUVE> stored =
        registry.GetStoredValueUVE(store, EditorSettingIdUVE::kContentCreateRecentUVE);
    ASSERT_TRUE(stored.has_value());
    const auto* recent = std::get_if<Config::SettingStringListUVE>(&*stored);
    ASSERT_NE(recent, nullptr);
    ASSERT_EQ(recent->size(), descriptor->maxItems);
    EXPECT_EQ(recent->front(), "asset-0");
    EXPECT_EQ(recent->back(), "asset-" + std::to_string(descriptor->maxItems - 1U));
}

TEST(EditorSettingsUVETest, PersonalShelfDescriptorsValidateCountsNamesAndItemPaths) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    const Config::SettingDescriptorUVE* count =
        registry.FindUVE(EditorSettingIdUVE::kPersonalShelvesCountUVE);
    const std::string nameId = EditorSettingIdUVE::GetPersonalShelfNameSettingIdUVE(0U);
    const std::string itemsId = EditorSettingIdUVE::GetPersonalShelfItemsSettingIdUVE(0U);
    const Config::SettingDescriptorUVE* name = registry.FindUVE(nameId);
    const Config::SettingDescriptorUVE* items = registry.FindUVE(itemsId);
    ASSERT_NE(count, nullptr);
    ASSERT_NE(name, nullptr);
    ASSERT_NE(items, nullptr);
    ASSERT_TRUE(count->maximum.has_value());
    ASSERT_GT(name->maxLength, 0U);
    ASSERT_GT(items->maxLength, 0U);
    EXPECT_EQ(count->maximum, static_cast<double>(ContentShelvesUVE::kMaxShelvesUVE));
    EXPECT_EQ(items->maxItems, ContentShelvesUVE::kMaxItemsPerShelfUVE);

    Config::ConfigManagerUVE store;
    store.SetIntUVE("editor.shelves.count",
                    static_cast<std::int64_t>(ContentShelvesUVE::kMaxShelvesUVE + 1U));
    EXPECT_FALSE(registry.GetStoredValueUVE(store, EditorSettingIdUVE::kPersonalShelvesCountUVE).has_value());
    EXPECT_EQ(registry.GetIntUVE(store, EditorSettingIdUVE::kPersonalShelvesCountUVE), 0);

    store.SetStringUVE(nameId, std::string(name->maxLength + 1U, 's'));
    EXPECT_FALSE(registry.GetStoredValueUVE(store, nameId).has_value());
    EXPECT_TRUE(registry.GetStringUVE(store, nameId).empty());

    store.SetIntUVE(itemsId + ".count", 1);
    store.SetStringUVE(itemsId + ".0", std::string(items->maxLength + 1U, 'p'));
    EXPECT_FALSE(registry.GetStoredValueUVE(store, itemsId).has_value());
    EXPECT_TRUE(registry.GetStringListUVE(store, itemsId).empty());
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
        EXPECT_EQ(alias->minimum, current->minimum) << renamed.oldId;
        EXPECT_EQ(alias->maximum, current->maximum) << renamed.oldId;
        EXPECT_EQ(alias->step, current->step) << renamed.oldId;
        EXPECT_EQ(alias->maxLength, current->maxLength) << renamed.oldId;
        EXPECT_EQ(alias->maxItems, current->maxItems) << renamed.oldId;
        EXPECT_EQ(alias->colorHasAlpha, current->colorHasAlpha) << renamed.oldId;
        ASSERT_EQ(alias->enumEntries.size(), current->enumEntries.size()) << renamed.oldId;
        for (std::size_t index = 0U; index < current->enumEntries.size(); ++index) {
            EXPECT_EQ(alias->enumEntries[index].value, current->enumEntries[index].value) << renamed.oldId;
            EXPECT_EQ(alias->enumEntries[index].label, current->enumEntries[index].label) << renamed.oldId;
        }
        EXPECT_TRUE(alias->HasFlagUVE(Config::kSettingFlagDeprecatedUVE)) << renamed.oldId;
        EXPECT_EQ(alias->replacementId, renamed.newId);
        EXPECT_NE(std::find(current->migratedFrom.begin(), current->migratedFrom.end(), renamed.oldId),
                  current->migratedFrom.end());
        EXPECT_TRUE(alias->migratedFrom.empty());
        EXPECT_FALSE(alias->sinceVersion.has_value());
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

TEST(EditorSettingsUVETest, PreferencesAutoSaveWritesOnceWhenTheAuthorLetsGo) {
    const Core::EngineConfigUVE config = MakeEditorAutoSaveTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    {
        Core::EngineCoreUVE engine(config);
        engine.Init();
        ASSERT_TRUE(engine.Load());
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_autosave.uvscene");
        editor.InitUVE();

        // Restoring stored preferences is not an author edit: a session that has just loaded has
        // nothing to write, and asking writes nothing.
        EXPECT_FALSE(editor.IsPreferencesAutoSavePendingUVE());
        EXPECT_FALSE(editor.FlushPendingPreferencesSaveUVE());

        // A change that did not happen is not a change: a refused value, and a setter handed the
        // value it already holds, both leave nothing behind.
        EXPECT_FALSE(editor.SetViewportGridSubdivisionsUVE(EditorUVE::kMaximumViewportGridSubdivisionsUVE + 1));
        EXPECT_FALSE(editor.SetViewportGridSubdivisionsUVE(1));
        EXPECT_FALSE(editor.IsPreferencesAutoSavePendingUVE());

        // Two changes by two routes - the settings sheet and a panel's own setter - put the document
        // behind; one idle frame writes both, and a second frame finds nothing left to do.
        ASSERT_TRUE(editor.SetEditorSettingUVE(EditorSettingIdUVE::kGridOpacityUVE, 0.5));
        ASSERT_TRUE(editor.SetViewportGridSubdivisionsUVE(5));
        EXPECT_TRUE(editor.IsPreferencesAutoSavePendingUVE());
        EXPECT_TRUE(editor.FlushPendingPreferencesSaveUVE());
        EXPECT_FALSE(editor.IsPreferencesAutoSavePendingUVE());
        EXPECT_FALSE(editor.FlushPendingPreferencesSaveUVE());

        // In the file, not only in this session's memory: the document itself carries both values.
        std::ifstream settingsFile{config.settingsFilePath};
        ASSERT_TRUE(settingsFile.is_open());
        const nlohmann::json document = nlohmann::json::parse(settingsFile);
        EXPECT_DOUBLE_EQ(document.at("editor").at("viewport").at("grid").at("opacity").get<double>(), 0.5);
        EXPECT_EQ(document.at("editor").at("viewport").at("grid").at("subdivisions").get<std::int64_t>(), 5);

        editor.ShutdownUVE();
        engine.Shutdown();
    }
    {
        // A fresh session reads them back as its own state, and still has nothing to save.
        Core::EngineCoreUVE restarted(config);
        restarted.Init();
        ASSERT_TRUE(restarted.Load());
        EditorUVE editor(restarted.GetServicesUVE(), "uve_editor_autosave_restart.uvscene");
        editor.InitUVE();
        EXPECT_FLOAT_EQ(editor.GetViewportGridOpacityUVE(), 0.5F);
        EXPECT_EQ(editor.GetViewportGridSubdivisionsUVE(), 5);
        EXPECT_FALSE(editor.IsPreferencesAutoSavePendingUVE());
        editor.ShutdownUVE();
        restarted.Shutdown();
    }
    std::filesystem::remove(config.settingsFilePath);
}

// The identity unit and the degree's own size are decided by the compiler, not only by the
// expectations below: a factor pair that drifts from pi/180 stops this file from building at all.
static_assert(EditorUVE::AngleDisplayFactorsForUVE(EditorUVE::EditorAngleDisplayUVE::Radians).radiansPerUnit ==
              1.0F);
static_assert(EditorUVE::AngleDisplayFactorsForUVE(EditorUVE::EditorAngleDisplayUVE::Radians).unitsPerRadian ==
              1.0F);
static_assert(EditorUVE::AngleDisplayFactorsForUVE(EditorUVE::EditorAngleDisplayUVE::Degrees).unitsPerRadian >
              57.29F);
static_assert(EditorUVE::AngleDisplayFactorsForUVE(EditorUVE::EditorAngleDisplayUVE::Degrees).unitsPerRadian <
              57.30F);

// The precision's own range is decided by the compiler too: a default outside 0..6, or a
// maximum the format table in editor_axis_input_uve.h does not spell, stops this file from
// building at all.
static_assert(EditorUVE::kInspectorFloatPrecisionMinUVE == 0);
static_assert(EditorUVE::kInspectorFloatPrecisionMaxUVE == 6);
static_assert(EditorUVE::kInspectorFloatPrecisionDefaultUVE == 3);
static_assert(EditorUVE::kInspectorFloatPrecisionDefaultUVE >= EditorUVE::kInspectorFloatPrecisionMinUVE);
static_assert(EditorUVE::kInspectorFloatPrecisionDefaultUVE <= EditorUVE::kInspectorFloatPrecisionMaxUVE);

TEST(EditorSettingsUVETest, AngleDisplayIsRegisteredAsAChoiceOfTwoUnits) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    const Config::SettingDescriptorUVE& angle = *registry.FindUVE(EditorSettingIdUVE::kInspectorAngleDisplayUVE);
    EXPECT_EQ(angle.type, SettingTypeUVE::Enum);
    EXPECT_EQ(angle.category, "Editor/Inspector");
    EXPECT_EQ(angle.defaultValue, Config::SettingValueUVE{std::int64_t{0}});
    ASSERT_EQ(angle.enumEntries.size(), 2U);
    EXPECT_EQ(angle.enumEntries[0U].label, "Degrees");
    EXPECT_EQ(angle.enumEntries[0U].value, 0);
    EXPECT_EQ(angle.enumEntries[1U].label, "Radians");
    EXPECT_EQ(angle.enumEntries[1U].value, 1);
}

TEST(EditorSettingsUVETest, FloatPrecisionIsRegisteredAsAnIntFromZeroToSix) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    const Config::SettingDescriptorUVE& precision =
        *registry.FindUVE(EditorSettingIdUVE::kInspectorFloatPrecisionUVE);
    EXPECT_EQ(precision.type, SettingTypeUVE::Int);
    EXPECT_EQ(precision.category, "Editor/Inspector");
    EXPECT_EQ(precision.defaultValue, Config::SettingValueUVE{std::int64_t{3}});
    EXPECT_EQ(precision.minimum, std::optional<double>{0.0});
    EXPECT_EQ(precision.maximum, std::optional<double>{6.0});
}

TEST(EditorSettingsUVETest, ColorPickerRgbDisplayIsRegisteredAsAChoiceOfTwoScales) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    const Config::SettingDescriptorUVE& rgb =
        *registry.FindUVE(EditorSettingIdUVE::kColorPickerRgbDisplayUVE);
    EXPECT_EQ(rgb.type, SettingTypeUVE::Enum);
    EXPECT_EQ(rgb.category, "Editor/Color Picker");
    EXPECT_EQ(rgb.defaultValue, Config::SettingValueUVE{std::int64_t{0}});
    ASSERT_EQ(rgb.enumEntries.size(), 2U);
    EXPECT_EQ(rgb.enumEntries[0U].label, "0-1");
    EXPECT_EQ(rgb.enumEntries[0U].value, 0);
    EXPECT_EQ(rgb.enumEntries[1U].label, "0-255");
    EXPECT_EQ(rgb.enumEntries[1U].value, 1);
}

TEST(EditorSettingsUVETest, GridOptionsAreRegisteredWithTheirOwnBoundsAndAPlanePerValue) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEditorSettingsUVE(registry));
    namespace Id = EditorSettingIdUVE;

    const Config::SettingDescriptorUVE& subdivisions = *registry.FindUVE(Id::kGridSubdivisionsUVE);
    EXPECT_EQ(subdivisions.minimum, std::optional<double>{1.0});
    EXPECT_EQ(subdivisions.maximum, std::optional<double>{10.0});
    EXPECT_EQ(subdivisions.category, "Editor/Viewport/Grid");

    // The fade pair is two settings because each end is its own number, but neither end is legal on
    // its own: the sheet bounds them to the range the shader can still draw, and the binding refuses
    // a pair whose start would cross its end (see GridOptionsRefuseWhatTheViewportCouldNotDrawUVE).
    const Config::SettingDescriptorUVE& fadeStart = *registry.FindUVE(Id::kGridFadeStartUVE);
    const Config::SettingDescriptorUVE& fadeEnd = *registry.FindUVE(Id::kGridFadeEndUVE);
    EXPECT_EQ(fadeStart.minimum, std::optional<double>{4.0});
    EXPECT_EQ(fadeEnd.maximum, std::optional<double>{200.0});

    const Config::SettingDescriptorUVE& tint = *registry.FindUVE(Id::kGridLineTintUVE);
    EXPECT_FALSE(tint.colorHasAlpha);

    const Config::SettingDescriptorUVE& plane = *registry.FindUVE(Id::kGridPlaneUVE);
    ASSERT_EQ(plane.enumEntries.size(), 4U);
    EXPECT_EQ(plane.enumEntries[0U].label, "Follow View");
    EXPECT_EQ(plane.enumEntries[0U].value, 0);
    EXPECT_EQ(plane.enumEntries[3U].label, "Side (ZY)");
    EXPECT_EQ(plane.enumEntries[3U].value, 3);
}

TEST(EditorSettingsUVETest, GridSpacingMirrorMatchesTheShadersOwnDecadeMath) {
    namespace Render = univex::render;
    Render::GridSettings settings{};
    // One pixel of ground per pixel of screen: a 24 m square is the finest one that still reads at
    // 24 px, so the finest decade is 10 m - the same numbers the fragment shader computes.
    const Render::GridLod lod = Render::ComputeGridLod(1.0F, settings);
    EXPECT_FLOAT_EQ(lod.level, 1.3802112F);
    EXPECT_FLOAT_EQ(lod.finestSpacing, 10.0F);
    EXPECT_FLOAT_EQ(Render::ComputeDisplayGridSpacing(1.0F, settings), 10.0F);

    // Sub-lines divide the finest decade, never multiply it: 4 lines per cell put one every 2.5 m.
    settings.subdivisions = 4;
    EXPECT_FLOAT_EQ(Render::ComputeGridSubdivisionSpacing(1.0F, settings), 2.5F);
    // With the option off - and with a hand-edited zero, which must not divide the spacing by zero -
    // the step is the finest decade itself.
    settings.subdivisions = 1;
    EXPECT_FLOAT_EQ(Render::ComputeGridSubdivisionSpacing(1.0F, settings), 10.0F);
    settings.subdivisions = 0;
    EXPECT_FLOAT_EQ(Render::ComputeGridSubdivisionSpacing(1.0F, settings), 10.0F);

    // Zoomed out far enough that the finest tier has mostly faded, the next decade up is what reads
    // as the grid.
    // 100 m/px sits at level 3.38 (fade 0.38): the 1000 m decade still reads as the grid. 200 m/px is
    // level 3.68, past the halfway fade, so the next decade up takes over.
    EXPECT_FLOAT_EQ(Render::ComputeDisplayGridSpacing(100.0F, settings), 1000.0F);
    EXPECT_FLOAT_EQ(Render::ComputeDisplayGridSpacing(200.0F, settings), 10000.0F);

    // The tint multiplies each level's own colour: white is the identity, and a half-strength tint
    // darkens by half rather than replacing the level's hue. The default is white.
    EXPECT_FLOAT_EQ(settings.lineTint.r, 1.0F);
    const Render::GridColor thin{0.4F, 0.5F, 0.6F};
    EXPECT_FLOAT_EQ(Render::TintedGridColor(thin, Render::GridColor{}).r, 0.0F);
    EXPECT_FLOAT_EQ(Render::TintedGridColor(thin, Render::GridColor{1.0F, 1.0F, 1.0F}).g, 0.5F);
    EXPECT_FLOAT_EQ(Render::TintedGridColor(thin, Render::GridColor{0.5F, 0.5F, 0.5F}).b, 0.3F);

    // A grid standing on a plane faces the view that looks at it; the ground keeps everything else,
    // including a top-down view, where a wall grid would be edge-on.
    EXPECT_EQ(Render::GridPlaneFacing(0.0F, 0.0F, 1.0F), Render::GridPlane::XY);
    EXPECT_EQ(Render::GridPlaneFacing(-1.0F, 0.0F, 0.0F), Render::GridPlane::ZY);
    EXPECT_EQ(Render::GridPlaneFacing(0.0F, 1.0F, 0.0F), Render::GridPlane::XZ);
    EXPECT_EQ(Render::GridPlaneFacing(0.5F, 0.5F, 0.7F), Render::GridPlane::XZ);
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

TEST(EditorSettingsUVETest, GridOptionsRefuseWhatTheViewportCouldNotDraw) {
    Core::EngineCoreUVE engine(MakeEditorSettingsObserverTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE());
        namespace Id = EditorSettingIdUVE;
        EXPECT_EQ(editor.GetViewportGridSubdivisionsUVE(), 1);
        EXPECT_EQ(editor.GetViewportGridFadeStartUVE(), EditorUVE::kDefaultViewportGridFadeStartUVE);
        EXPECT_EQ(editor.GetViewportGridFadeEndUVE(), EditorUVE::kDefaultViewportGridFadeEndUVE);
        EXPECT_EQ(editor.GetViewportGridPlaneUVE(), EditorUVE::EditorViewportGridPlaneUVE::FollowView);

        std::vector<std::string> reported;
        const auto subscription = editor.SubscribeToCategoryUVE(
            "Editor/Viewport/Grid", [&reported](const Config::SettingChangedEventUVE& event) {
                reported.push_back(event.id);
            });
        ASSERT_TRUE(subscription.IsValidUVE());

        // Out of range, and a no-op, are both refused: the first would divide by zero in the shader,
        // the second would report a change that did not happen.
        EXPECT_FALSE(editor.SetViewportGridSubdivisionsUVE(0));
        EXPECT_FALSE(editor.SetViewportGridSubdivisionsUVE(EditorUVE::kMaximumViewportGridSubdivisionsUVE + 1));
        EXPECT_FALSE(editor.SetViewportGridSubdivisionsUVE(1));
        EXPECT_TRUE(editor.SetViewportGridSubdivisionsUVE(5));
        EXPECT_EQ(editor.GetViewportGridSubdivisionsUVE(), 5);
        EXPECT_EQ(reported, (std::vector<std::string>{std::string{Id::kGridSubdivisionsUVE}}));

        // A fade that ends before it starts is a hard edge, so the pair moves together or not at all.
        EXPECT_FALSE(editor.SetViewportGridFadeUVE(30.0F, 20.0F));
        EXPECT_FALSE(editor.SetViewportGridFadeUVE(30.0F, 30.0F));
        EXPECT_FALSE(editor.SetViewportGridFadeUVE(std::numeric_limits<float>::quiet_NaN(), 60.0F));
        EXPECT_EQ(editor.GetViewportGridFadeStartUVE(), EditorUVE::kDefaultViewportGridFadeStartUVE);
        EXPECT_TRUE(editor.SetViewportGridFadeUVE(10.0F, 60.0F));
        EXPECT_EQ(editor.GetViewportGridFadeStartUVE(), 10.0F);
        EXPECT_EQ(editor.GetViewportGridFadeEndUVE(), 60.0F);
        EXPECT_NE(std::find(reported.begin(), reported.end(), Id::kGridFadeStartUVE), reported.end());
        EXPECT_NE(std::find(reported.begin(), reported.end(), Id::kGridFadeEndUVE), reported.end());

        // A tint that is not a colour is refused; a real one is stored, on all three channels.
        EXPECT_FALSE(editor.SetViewportGridLineTintUVE(EditorUVE::ViewportAxisColorUVE{2.0F, 0.5F, 0.5F}));
        EXPECT_TRUE(editor.SetViewportGridLineTintUVE(EditorUVE::ViewportAxisColorUVE{0.5F, 0.5F, 0.5F}));
        EXPECT_EQ(editor.GetViewportGridLineTintUVE().r, 0.5F);

        // The plane a person can pick, and nothing else - a value cast in from outside is refused.
        EXPECT_TRUE(editor.SetViewportGridPlaneUVE(EditorUVE::EditorViewportGridPlaneUVE::FrontXY));
        EXPECT_EQ(editor.GetViewportGridPlaneUVE(), EditorUVE::EditorViewportGridPlaneUVE::FrontXY);
        EXPECT_FALSE(editor.SetViewportGridPlaneUVE(static_cast<EditorUVE::EditorViewportGridPlaneUVE>(9)));

        // Through the settings sheet the fade pair is bounded by the sheet's own range AND by the
        // other end of the pair, so a start past the current end cannot be stored at all.
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kGridFadeStartUVE, 90.0F));
        EXPECT_EQ(editor.GetViewportGridFadeStartUVE(), 10.0F);
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kGridFadeEndUVE, 5.0F));
        EXPECT_EQ(editor.GetViewportGridFadeEndUVE(), 60.0F);
        EXPECT_TRUE(editor.SetEditorSettingUVE(Id::kGridSubdivisionsUVE, std::int64_t{2}));
        EXPECT_EQ(editor.GetViewportGridSubdivisionsUVE(), 2);
        EXPECT_TRUE(editor.UnsubscribeUVE(subscription));
    }
    engine.Shutdown();
}

TEST(EditorSettingsUVETest, AngleDisplaySwitchesTheUnitAndLeavesStoredRadiansAlone) {
    Core::EngineCoreUVE engine(MakeEditorSettingsObserverTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE());
        namespace Id = EditorSettingIdUVE;
        using Display = EditorUVE::EditorAngleDisplayUVE;
        EXPECT_EQ(editor.GetInspectorAngleDisplayUVE(), Display::Degrees);

        std::vector<std::string> reported;
        const auto subscription = editor.SubscribeToSettingUVE(
            Id::kInspectorAngleDisplayUVE,
            [&reported](const Config::SettingChangedEventUVE& event) { reported.push_back(event.id); });
        ASSERT_TRUE(subscription.IsValidUVE());

        // A value that is neither unit, and one already in effect, are both refused and report
        // nothing - only a real change reaches the observers (and, through them, the auto-save).
        EXPECT_FALSE(editor.SetInspectorAngleDisplayUVE(static_cast<Display>(9)));
        EXPECT_FALSE(editor.SetInspectorAngleDisplayUVE(Display::Degrees));
        EXPECT_TRUE(reported.empty());
        EXPECT_TRUE(editor.SetInspectorAngleDisplayUVE(Display::Radians));
        EXPECT_EQ(editor.GetInspectorAngleDisplayUVE(), Display::Radians);
        EXPECT_EQ(reported, (std::vector<std::string>{std::string{Id::kInspectorAngleDisplayUVE}}));

        // The two units are one factor pair each, so the same stored radian value reads as its
        // degree or radian self, and a shown value goes back to exactly the radian it came from.
        const EditorUVE::AngleDisplayFactorsUVE degrees =
            EditorUVE::AngleDisplayFactorsForUVE(Display::Degrees);
        const EditorUVE::AngleDisplayFactorsUVE radians =
            EditorUVE::AngleDisplayFactorsForUVE(Display::Radians);
        EXPECT_FLOAT_EQ(radians.radiansPerUnit, 1.0F);
        EXPECT_FLOAT_EQ(radians.unitsPerRadian, 1.0F);
        EXPECT_FLOAT_EQ(degrees.radiansPerUnit * degrees.unitsPerRadian, 1.0F);
        constexpr float kQuarterTurn = std::numbers::pi_v<float> * 0.5F;
        EXPECT_FLOAT_EQ(kQuarterTurn * degrees.unitsPerRadian, 90.0F);
        EXPECT_NEAR((kQuarterTurn * degrees.unitsPerRadian) * degrees.radiansPerUnit, kQuarterTurn, 1.0e-6F);
        EXPECT_FLOAT_EQ(kQuarterTurn * radians.unitsPerRadian, kQuarterTurn);

        // The settings sheet drives the same state: a stored Radians reaches the editor's own
        // getter, and storing it back is the same value.
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kInspectorAngleDisplayUVE, std::int64_t{1}));
        EXPECT_EQ(editor.GetInspectorAngleDisplayUVE(), Display::Radians);
        EXPECT_TRUE(editor.SetEditorSettingUVE(Id::kInspectorAngleDisplayUVE, std::int64_t{0}));
        EXPECT_EQ(editor.GetInspectorAngleDisplayUVE(), Display::Degrees);
        EXPECT_TRUE(editor.UnsubscribeUVE(subscription));
    }
    engine.Shutdown();
}

TEST(EditorSettingsUVETest, FloatPrecisionRefusesWhatTheFormatTableCannotSpell) {
    Core::EngineCoreUVE engine(MakeEditorSettingsObserverTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE());
        namespace Id = EditorSettingIdUVE;
        EXPECT_EQ(editor.GetInspectorFloatPrecisionUVE(), 3);

        std::vector<std::string> reported;
        const auto subscription = editor.SubscribeToSettingUVE(
            Id::kInspectorFloatPrecisionUVE,
            [&reported](const Config::SettingChangedEventUVE& event) { reported.push_back(event.id); });
        ASSERT_TRUE(subscription.IsValidUVE());

        // Outside 0..6, and the value already in effect, are both refused and report
        // nothing - only a real change reaches the observers (and, through them, the auto-save).
        EXPECT_FALSE(editor.SetInspectorFloatPrecisionUVE(-1));
        EXPECT_FALSE(editor.SetInspectorFloatPrecisionUVE(7));
        EXPECT_FALSE(editor.SetInspectorFloatPrecisionUVE(3));
        EXPECT_TRUE(reported.empty());
        EXPECT_EQ(editor.GetInspectorFloatPrecisionUVE(), 3);
        EXPECT_TRUE(editor.SetInspectorFloatPrecisionUVE(1));
        EXPECT_EQ(editor.GetInspectorFloatPrecisionUVE(), 1);
        EXPECT_EQ(reported, (std::vector<std::string>{std::string{Id::kInspectorFloatPrecisionUVE}}));

        // Both ends of the range are real choices, and the settings sheet drives the same
        // state - including refusing what the descriptor's own bounds reject.
        EXPECT_TRUE(editor.SetInspectorFloatPrecisionUVE(0));
        EXPECT_TRUE(editor.SetInspectorFloatPrecisionUVE(6));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kInspectorFloatPrecisionUVE, std::int64_t{2}));
        EXPECT_EQ(editor.GetInspectorFloatPrecisionUVE(), 2);
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kInspectorFloatPrecisionUVE, std::int64_t{9}));
        EXPECT_EQ(editor.GetInspectorFloatPrecisionUVE(), 2);
        EXPECT_TRUE(editor.UnsubscribeUVE(subscription));
    }
    engine.Shutdown();
}

TEST(EditorSettingsUVETest, ColorPickerRgbDisplayRoundTripsThroughTheSheetAndTheBulkSetter) {
    Core::EngineCoreUVE engine(MakeEditorSettingsObserverTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE());
        namespace Id = EditorSettingIdUVE;
        using Display = ColorPickerRgbDisplayUVE;
        EXPECT_EQ(editor.GetColorPickerPreferencesUVE().rgbDisplay, Display::ZeroToOne);

        std::vector<std::string> reported;
        const auto subscription = editor.SubscribeToSettingUVE(
            Id::kColorPickerRgbDisplayUVE,
            [&reported](const Config::SettingChangedEventUVE& event) { reported.push_back(event.id); });
        ASSERT_TRUE(subscription.IsValidUVE());

        // The settings sheet drives the picker's state, and refuses what is neither scale.
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kColorPickerRgbDisplayUVE, std::int64_t{1}));
        EXPECT_EQ(editor.GetColorPickerPreferencesUVE().rgbDisplay, Display::ZeroTo255);
        EXPECT_EQ(reported, (std::vector<std::string>{std::string{Id::kColorPickerRgbDisplayUVE}}));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kColorPickerRgbDisplayUVE, std::int64_t{9}));
        EXPECT_EQ(editor.GetColorPickerPreferencesUVE().rgbDisplay, Display::ZeroTo255);
        EXPECT_EQ(reported.size(), 1U);
        EXPECT_TRUE(editor.SetEditorSettingUVE(Id::kColorPickerRgbDisplayUVE, std::int64_t{0}));
        EXPECT_EQ(editor.GetColorPickerPreferencesUVE().rgbDisplay, Display::ZeroToOne);

        // The bulk setter sanitizes a value from nowhere back to 0-1, and only a real change
        // notifies: rebuilding the same struct (as the saved/recent bindings do on every load)
        // must not look like a display change.
        reported.clear();
        ColorPickerPreferencesUVE sanitized = editor.GetColorPickerPreferencesUVE();
        sanitized.rgbDisplay = static_cast<Display>(9);
        editor.SetColorPickerPreferencesUVE(sanitized);
        EXPECT_EQ(editor.GetColorPickerPreferencesUVE().rgbDisplay, Display::ZeroToOne);
        EXPECT_TRUE(reported.empty());
        ColorPickerPreferencesUVE changed = editor.GetColorPickerPreferencesUVE();
        changed.rgbDisplay = Display::ZeroTo255;
        editor.SetColorPickerPreferencesUVE(changed);
        EXPECT_EQ(reported, (std::vector<std::string>{std::string{Id::kColorPickerRgbDisplayUVE}}));
        editor.SetColorPickerPreferencesUVE(editor.GetColorPickerPreferencesUVE());
        EXPECT_EQ(reported.size(), 1U);
        EXPECT_TRUE(editor.UnsubscribeUVE(subscription));
    }
    engine.Shutdown();
}

TEST(EditorSettingsUVETest, ObjectDefaultExtrasUVE_EveryCreatableKindHasAStringListSetting) {
    Core::EngineCoreUVE engine(MakeEditorSettingsObserverTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_object_default_extras.uvscene");
        editor.InitUVE();
        std::size_t creatable = 0U;
        for (const Scene::Objects::SceneObjectDescriptorUVE& descriptor :
             Scene::Objects::GetSceneObjectDescriptorsUVE()) {
            if (!descriptor.libraryCreatable) {
                continue;
            }
            ++creatable;
            const std::string id =
                EditorSettingIdUVE::GetObjectDefaultExtrasSettingIdUVE(descriptor.typeId);
            const Config::SettingDescriptorUVE* const setting =
                editor.GetSettingsRegistryUVE().FindUVE(id);
            ASSERT_NE(setting, nullptr) << descriptor.typeId;
            EXPECT_EQ(setting->type, Config::SettingTypeUVE::StringList) << id;
            EXPECT_EQ(setting->category, "Editor/Objects") << id;
            EXPECT_EQ(setting->enumEntries.size(), 25U) << id;
            const std::optional<Config::SettingValueUVE> value = editor.GetEditorSettingUVE(id);
            ASSERT_TRUE(value.has_value()) << id;
            EXPECT_TRUE(std::get<Config::SettingStringListUVE>(*value).empty()) << id;
        }
        EXPECT_GT(creatable, 0U);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorSettingsUVETest, ObjectDefaultExtrasUVE_SetRejectsUnknownComponentIds) {
    Core::EngineCoreUVE engine(MakeEditorSettingsObserverTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_object_default_extras.uvscene");
        editor.InitUVE();
        const std::string id = EditorSettingIdUVE::GetObjectDefaultExtrasSettingIdUVE("marker_3d");
        EXPECT_FALSE(editor.SetEditorSettingUVE(
            id, Config::SettingStringListUVE{"component.script", "component.nope"}));
        const std::optional<Config::SettingValueUVE> value = editor.GetEditorSettingUVE(id);
        ASSERT_TRUE(value.has_value());
        EXPECT_TRUE(std::get<Config::SettingStringListUVE>(*value).empty());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorSettingsUVETest, ObjectDefaultExtrasUVE_SetDedupesKeepingFirstOrder) {
    Core::EngineCoreUVE engine(MakeEditorSettingsObserverTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_object_default_extras.uvscene");
        editor.InitUVE();
        const std::string id =
            EditorSettingIdUVE::GetObjectDefaultExtrasSettingIdUVE("mesh_instance_3d");
        ASSERT_TRUE(editor.SetEditorSettingUVE(
            id,
            Config::SettingStringListUVE{"component.mesh", "component.script", "component.mesh"}));
        const std::optional<Config::SettingValueUVE> value = editor.GetEditorSettingUVE(id);
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(std::get<Config::SettingStringListUVE>(*value),
                  (Config::SettingStringListUVE{"component.mesh", "component.script"}));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorSettingsUVETest, ObjectDefaultExtrasUVE_ExtrasSurviveASettingsRoundTrip) {
    Core::EngineConfigUVE config = MakeEditorSettingsObserverTestConfigUVE();
    config.settingsFilePath = "uve_object_default_extras_settings.json";
    config.logFilePath = "uve_object_default_extras.log";
    std::filesystem::remove(config.settingsFilePath);
    const std::string markerId = EditorSettingIdUVE::GetObjectDefaultExtrasSettingIdUVE("marker_3d");
    {
        Core::EngineCoreUVE engine(config);
        engine.Init();
        ASSERT_TRUE(engine.Load());
        EditorUVE editor(engine.GetServicesUVE(), "uve_object_default_extras.uvscene");
        editor.InitUVE();
        ASSERT_TRUE(editor.SetEditorSettingUVE(
            markerId, Config::SettingStringListUVE{"component.script", "component.collider"}));
        ASSERT_TRUE(editor.FlushPendingPreferencesSaveUVE());
        std::ifstream settingsFile{config.settingsFilePath};
        ASSERT_TRUE(settingsFile.is_open());
        const nlohmann::json document = nlohmann::json::parse(settingsFile);
        const nlohmann::json& node =
            document.at("editor").at("objects").at("defaultExtras").at("marker_3d");
        EXPECT_EQ(node.at("count").get<std::int64_t>(), 2);
        EXPECT_EQ(node.at("0").get<std::string>(), "component.script");
        EXPECT_EQ(node.at("1").get<std::string>(), "component.collider");
        editor.ShutdownUVE();
        engine.Shutdown();
    }
    {
        Core::EngineCoreUVE restarted(config);
        restarted.Init();
        ASSERT_TRUE(restarted.Load());
        EditorUVE editor(restarted.GetServicesUVE(), "uve_object_default_extras_restart.uvscene");
        editor.InitUVE();
        const std::optional<Config::SettingValueUVE> value = editor.GetEditorSettingUVE(markerId);
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(std::get<Config::SettingStringListUVE>(*value),
                  (Config::SettingStringListUVE{"component.script", "component.collider"}));
        EXPECT_FALSE(editor.IsPreferencesAutoSavePendingUVE());
        editor.ShutdownUVE();
        restarted.Shutdown();
    }
    std::filesystem::remove(config.settingsFilePath);
}

} // namespace
} // namespace UVE::Editor::Tests
