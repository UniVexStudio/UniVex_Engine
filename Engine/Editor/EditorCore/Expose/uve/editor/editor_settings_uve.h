// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "uve/config/settings_registry_uve.h"

namespace UVE::Editor {

/// The ids of the editor's own settings in the settings file, each declared once by
/// RegisterEditorSettingsUVE. The strings are the keys the file has always used, so existing
/// settings files keep working.
namespace EditorSettingIdUVE {
// Session state the editor remembers for itself: Hidden, never offered as a preference.
inline constexpr std::string_view kScenePanelVisibleUVE = "editor.panels.sceneVisible";
inline constexpr std::string_view kViewportPanelVisibleUVE = "editor.panels.viewportVisible";
inline constexpr std::string_view kInspectorPanelVisibleUVE = "editor.panels.inspectorVisible";
inline constexpr std::string_view kBottomDockVisibleUVE = "editor.panels.bottomDockVisible";
inline constexpr std::string_view kBottomDockHeightUVE = "editor.panels.bottomDockHeight";
inline constexpr std::string_view kContentBrowserViewModeUVE = "editor.contentBrowser.viewMode";
inline constexpr std::string_view kContentBrowserModeUVE = "editor.contentBrowser.mode";
inline constexpr std::string_view kActiveWorkspaceUVE = "editor.workspace.active";
inline constexpr std::string_view kActiveRightPanelTabUVE = "editor.rightPanel.activeTab";
inline constexpr std::string_view kActiveBottomDockUVE = "editor.bottomDock.active";
inline constexpr std::string_view kContentCreateRecentUVE = "editor.content.recent";
inline constexpr std::string_view kPersonalShelvesCountUVE = "editor.shelves.count";
[[nodiscard]] inline std::string GetPersonalShelfNameSettingIdUVE(const std::size_t index) {
    return "editor.shelves." + std::to_string(index) + ".name";
}
[[nodiscard]] inline std::string GetPersonalShelfItemsSettingIdUVE(const std::size_t index) {
    return "editor.shelves." + std::to_string(index) + ".items";
}
inline constexpr std::string_view kColorPickerAdvancedOpenUVE = "editor.colorPicker.advancedOpen";
inline constexpr std::string_view kColorPickerSavedUVE = "editor.colorPicker.saved";
inline constexpr std::string_view kColorPickerRecentUVE = "editor.colorPicker.recent";
inline constexpr std::string_view kColorPickerRgbDisplayUVE = "editor.colorPicker.rgbDisplay";
inline constexpr std::string_view kInspectorFoldsUVE = "editor.inspector.folds";
inline constexpr std::string_view kInspectorAngleDisplayUVE = "editor.inspector.angleDisplay";
inline constexpr std::string_view kInspectorFloatPrecisionUVE = "editor.inspector.floatPrecision";
inline constexpr std::string_view kFavoriteProjectsUVE = "editor.favorites";
inline constexpr std::string_view kViewportAxisColorXUVE = "editor.viewport.axisColors.x";
inline constexpr std::string_view kViewportAxisColorYUVE = "editor.viewport.axisColors.y";
inline constexpr std::string_view kViewportAxisColorZUVE = "editor.viewport.axisColors.z";
// Preferences.
inline constexpr std::string_view kSnapEnabledUVE = "editor.viewport.snap.enabled";
inline constexpr std::string_view kSnapTranslateStepUVE = "editor.viewport.snap.translateStep";
inline constexpr std::string_view kSnapRotateStepDegreesUVE = "editor.viewport.snap.rotateStepDegrees";
inline constexpr std::string_view kSnapScaleStepUVE = "editor.viewport.snap.scaleStep";
inline constexpr std::string_view kGridVisibleUVE = "editor.viewport.grid.visible";
inline constexpr std::string_view kGridOpacityUVE = "editor.viewport.grid.opacity";
inline constexpr std::string_view kGridCellSizeUVE = "editor.viewport.grid.cellSize";
inline constexpr std::string_view kGridSubdivisionsUVE = "editor.viewport.grid.subdivisions";
inline constexpr std::string_view kGridFadeStartUVE = "editor.viewport.grid.fadeStart";
inline constexpr std::string_view kGridFadeEndUVE = "editor.viewport.grid.fadeEnd";
inline constexpr std::string_view kGridLineTintUVE = "editor.viewport.grid.lineTint";
inline constexpr std::string_view kGridPlaneUVE = "editor.viewport.grid.plane";
inline constexpr std::string_view kSelectionOutlineVisibleUVE = "editor.viewport.selectionOutline.visible";
/// A colour, so its channels sit at `.r`, `.g` and `.b` beneath this id.
inline constexpr std::string_view kSelectionOutlineColorUVE = "editor.viewport.selectionOutline";
inline constexpr std::string_view kNewObjectsUnderSelectionUVE = "editor.objects.addUnderSelection";
inline constexpr std::string_view kNewObjectPlacementUVE = "editor.objects.placement";
// Per-kind recipe-default extras: one StringList setting per library-creatable object kind, each
// naming extra component ids (see the palette in editor_settings_uve.cpp) a new object of that
// kind is born with. The kind's own type id completes the setting id.
inline constexpr std::string_view kObjectDefaultExtrasPrefixUVE = "editor.objects.defaultExtras.";
[[nodiscard]] inline std::string GetObjectDefaultExtrasSettingIdUVE(const std::string_view kindTypeId) {
    return std::string(kObjectDefaultExtrasPrefixUVE) + std::string(kindTypeId);
}
// The components a default-extras setting may name, in Preferences display order. Audited:
// local-impact components only - identity, baseline, hierarchy, structural, base-layer,
// global-visibility and editor-internal components can never be extras. Creation's attach table
// (editor_uve.cpp) and the settings/creation tests keep in lockstep with this list.
inline constexpr std::array<std::string_view, 25U> kObjectDefaultExtrasPaletteUVE = {
    "component.script",
    "component.object_metadata",
    "component.editor_description",
    "component.mesh",
    "component.primitive_mesh",
    "component.collider",
    "component.rigid_body",
    "component.solid_body",
    "component.physics_object",
    "component.kinematic_3d",
    "component.character_controller",
    "component.area",
    "component.camera",
    "component.audio_source",
    "component.particle_emitter",
    "component.ui_button",
    "component.ui_text",
    "component.ui_image",
    "component.canvas",
    "component.auto_translate",
    "component.thread_group",
    "component.process",
    "component.bone_modifier",
    // AnimationSequencerComponentUVE: the clip player, inert while its clip is invalid.
    "component.animation_player",
    // AnimationGraphComponentUVE: the blend tree, inert while its nodes reference no clip.
    "component.animation_tree",
};
/// True when `componentId` names a palette member.
[[nodiscard]] inline bool IsObjectDefaultExtraComponentUVE(const std::string_view componentId) {
    for (const std::string_view member : kObjectDefaultExtrasPaletteUVE) {
        if (member == componentId) {
            return true;
        }
    }
    return false;
}
inline constexpr std::string_view kPlayPauseOnStartUVE = "editor.play.pauseOnStart";
inline constexpr std::string_view kPlayPauseOnErrorUVE = "editor.play.pauseOnError";
inline constexpr std::string_view kPlaySaveSceneFirstUVE = "editor.play.saveSceneFirst";
inline constexpr std::string_view kPlaySwitchToGameUVE = "editor.play.switchToGame";
inline constexpr std::string_view kPlayTintEnabledUVE = "editor.play.tint.enabled";
inline constexpr std::string_view kPlayTintColorUVE = "editor.play.tint.color";
inline constexpr std::string_view kPlayTintStrengthUVE = "editor.play.tint.strength";
inline constexpr std::string_view kHierarchyRevealSelectionUVE = "editor.hierarchy.revealSelection";
inline constexpr std::string_view kHierarchySelectChildrenUVE = "editor.hierarchy.selectChildren";
inline constexpr std::string_view kHierarchyShowIconsUVE = "editor.hierarchy.showIcons";
inline constexpr std::string_view kHierarchyColorCodeIconsUVE = "editor.hierarchy.colorCodeIcons";
inline constexpr std::string_view kHierarchyShowComponentBadgesUVE = "editor.hierarchy.showComponentBadges";
inline constexpr std::string_view kHierarchyShowTypeNameUVE = "editor.hierarchy.showTypeName";
inline constexpr std::string_view kHierarchyVisibilityColumnUVE = "editor.hierarchy.visibilityColumn";
inline constexpr std::string_view kHierarchyDoubleClickUVE = "editor.hierarchy.doubleClick";
inline constexpr std::string_view kHierarchyRenameModeUVE = "editor.hierarchy.renameMode";
inline constexpr std::string_view kHierarchyDuplicateNameSuffixUVE = "editor.hierarchy.duplicateNameSuffix";
inline constexpr std::string_view kHierarchyFilterModeUVE = "editor.hierarchy.filterMode";
inline constexpr std::string_view kHierarchySortModeUVE = "editor.hierarchy.sortMode";
inline constexpr std::string_view kHierarchyFilterCaseSensitiveUVE = "editor.hierarchy.filterCaseSensitive";
inline constexpr std::string_view kHierarchyFilterKeepAncestorsUVE = "editor.hierarchy.filterKeepAncestors";
inline constexpr std::string_view kHierarchyDragToReparentUVE = "editor.hierarchy.dragToReparent";
inline constexpr std::string_view kHierarchyConfirmLargeSubtreeReparentUVE =
    "editor.hierarchy.confirmLargeSubtreeReparent";
inline constexpr std::string_view kHierarchyConfirmDeleteSubtreeUVE = "editor.hierarchy.confirmDeleteSubtree";
inline constexpr std::string_view kHierarchyTreeLinesUVE = "editor.hierarchy.treeLines";
inline constexpr std::string_view kHierarchyRowHeightUVE = "editor.hierarchy.rowHeight";
inline constexpr std::string_view kHierarchyIndentWidthUVE = "editor.hierarchy.indentWidth";
// Command palette.
inline constexpr std::string_view kCommandPaletteEnabledUVE = "editor.commandPalette.enabled";
inline constexpr std::string_view kCommandPaletteMatchModeUVE = "editor.commandPalette.matchMode";
inline constexpr std::string_view kCommandPaletteRecentCountUVE = "editor.commandPalette.recentCount";
} // namespace EditorSettingIdUVE

/// One setting id that was renamed, and the id it now has. The current descriptor lists the old
/// id in `migratedFrom`, and a matching Deprecated alias points back with `replacementId`, so the
/// registry migrates a value stored before the rename to the current id on load.
struct RenamedSettingIdUVE final {
    std::string_view oldId;
    std::string_view newId;
};

/// The renames so far. `editor.nodes.*` became `editor.objects.*` when the engine's word for a
/// scene node became object.
inline constexpr RenamedSettingIdUVE kRenamedSettingIdsUVE[] = {
    {"editor.nodes.addUnderSelection", EditorSettingIdUVE::kNewObjectsUnderSelectionUVE},
    {"editor.nodes.placement", EditorSettingIdUVE::kNewObjectPlacementUVE},
};

/// Declares every editor setting in `registry`, with defaults and legal ranges taken from the
/// editor's own defaults and setters, so a stored value the registry accepts is one the editor
/// accepts too. False if any declaration is refused - a programming error the editor's settings
/// test catches.
///
/// The content-creation recents, personal-shelf item lists, colour picker's saved/recent colours,
/// remembered inspector folds and favorite projects use hidden StringList descriptors; personal
/// shelves also have hidden count/name descriptors. The viewport axis colours use hidden Color
/// descriptors but are loaded and saved as one palette because their defaults belong to the host.
/// Lists have no editable generic panel row yet.
[[nodiscard]] bool RegisterEditorSettingsUVE(Config::SettingsRegistryUVE& registry);

// What the preferences window is made of, kept free of UI so it can be tested.

/// Whether `descriptor` matches the search `query`: every space-separated word in it appears,
/// ignoring case, in the setting's name, category, id or tooltip. An empty query matches all.
[[nodiscard]] bool MatchesSettingSearchUVE(const Config::SettingDescriptorUVE& descriptor, std::string_view query);

/// Whether a setting in `category` sits at or beneath the category tree object `path`.
[[nodiscard]] bool IsInSettingCategoryUVE(std::string_view category, std::string_view path) noexcept;

/// One object of the category tree: "Editor/Viewport/Grid" is `name` "Grid" at `depth` 2.
struct SettingCategoryObjectUVE final {
    std::string path;
    std::string name;
    int depth = 0;
};

/// The category tree of `descriptors`, every ancestor included, flattened depth first: a parent
/// always comes right before its children, and siblings keep the order they first appear in.
[[nodiscard]] std::vector<SettingCategoryObjectUVE> BuildSettingCategoryTreeUVE(
    const std::vector<const Config::SettingDescriptorUVE*>& descriptors);

/// `value` as a person reads it: On or Off, a number without trailing zeros, an enum entry's
/// label, a colour's hex code.
[[nodiscard]] std::string FormatSettingValueUVE(const Config::SettingDescriptorUVE& descriptor,
                                                const Config::SettingValueUVE& value);

} // namespace UVE::Editor
