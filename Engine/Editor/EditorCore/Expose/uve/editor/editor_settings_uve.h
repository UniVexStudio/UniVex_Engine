// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

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
inline constexpr std::string_view kActiveWorkspaceUVE = "editor.workspace.active";
inline constexpr std::string_view kActiveRightPanelTabUVE = "editor.rightPanel.activeTab";
inline constexpr std::string_view kActiveBottomDockUVE = "editor.bottomDock.active";
inline constexpr std::string_view kColorPickerAdvancedOpenUVE = "editor.colorPicker.advancedOpen";
// Preferences.
inline constexpr std::string_view kSnapEnabledUVE = "editor.viewport.snap.enabled";
inline constexpr std::string_view kSnapTranslateStepUVE = "editor.viewport.snap.translateStep";
inline constexpr std::string_view kSnapRotateStepDegreesUVE = "editor.viewport.snap.rotateStepDegrees";
inline constexpr std::string_view kSnapScaleStepUVE = "editor.viewport.snap.scaleStep";
inline constexpr std::string_view kGridVisibleUVE = "editor.viewport.grid.visible";
inline constexpr std::string_view kGridOpacityUVE = "editor.viewport.grid.opacity";
inline constexpr std::string_view kGridCellSizeUVE = "editor.viewport.grid.cellSize";
inline constexpr std::string_view kSelectionOutlineVisibleUVE = "editor.viewport.selectionOutline.visible";
/// A colour, so its channels sit at `.r`, `.g` and `.b` beneath this id.
inline constexpr std::string_view kSelectionOutlineColorUVE = "editor.viewport.selectionOutline";
inline constexpr std::string_view kSelectionOutlineThicknessUVE = "editor.viewport.selectionOutline.thickness";
} // namespace EditorSettingIdUVE

/// Declares every editor setting in `registry`, with defaults and legal ranges taken from the
/// editor's own defaults and setters, so a stored value the registry accepts is one the editor
/// accepts too. False if any declaration is refused - a programming error the editor's settings
/// test catches.
///
/// Not yet here, because the descriptor vocabulary cannot express them: the colour picker's saved
/// and recent colours, remembered inspector folds and favourite projects (lists), and the
/// viewport axis colours, whose defaults belong to the viewport module and are seeded by the host.
[[nodiscard]] bool RegisterEditorSettingsUVE(Config::SettingsRegistryUVE& registry);

// What the preferences window is made of, kept free of UI so it can be tested.

/// Whether `descriptor` matches the search `query`: every space-separated word in it appears,
/// ignoring case, in the setting's name, category, id or tooltip. An empty query matches all.
[[nodiscard]] bool MatchesSettingSearchUVE(const Config::SettingDescriptorUVE& descriptor, std::string_view query);

/// Whether a setting in `category` sits at or beneath the category tree node `path`.
[[nodiscard]] bool IsInSettingCategoryUVE(std::string_view category, std::string_view path) noexcept;

/// One node of the category tree: "Editor/Viewport/Grid" is `name` "Grid" at `depth` 2.
struct SettingCategoryNodeUVE final {
    std::string path;
    std::string name;
    int depth = 0;
};

/// The category tree of `descriptors`, every ancestor included, flattened depth first: a parent
/// always comes right before its children, and siblings keep the order they first appear in.
[[nodiscard]] std::vector<SettingCategoryNodeUVE> BuildSettingCategoryTreeUVE(
    const std::vector<const Config::SettingDescriptorUVE*>& descriptors);

/// `value` as a person reads it: On or Off, a number without trailing zeros, an enum entry's
/// label, a colour's hex code.
[[nodiscard]] std::string FormatSettingValueUVE(const Config::SettingDescriptorUVE& descriptor,
                                                const Config::SettingValueUVE& value);

} // namespace UVE::Editor
