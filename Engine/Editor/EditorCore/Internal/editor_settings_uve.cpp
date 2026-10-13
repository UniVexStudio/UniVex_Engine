// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_settings_uve.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "editor_settings_binding_uve.h"
#include "uve/editor/editor_color_uve.h"
#include "uve/editor/editor_uve.h"

namespace UVE::Editor {
namespace {

using Config::SettingColorUVE;
using Config::SettingDescriptorUVE;
using Config::SettingEnumEntryUVE;
using Config::SettingStringListUVE;
using Config::SettingValueUVE;

constexpr const char* kSessionCategoryUVE = "Editor/Session";
constexpr const char* kSnappingCategoryUVE = "Editor/Viewport/Snapping";
constexpr const char* kGridCategoryUVE = "Editor/Viewport/Grid";
constexpr const char* kOutlineCategoryUVE = "Editor/Viewport/Selection Outline";
constexpr const char* kObjectsCategoryUVE = "Editor/Objects";
constexpr const char* kPlayCategoryUVE = "Editor/Play Mode";
constexpr const char* kHierarchyCategoryUVE = "Editor/Hierarchy";
// Legacy color-list loading scanned up to 64 stored entries before skipping invalid hex strings
// and applying the smaller saved/recent caps to the successfully parsed colors.
constexpr std::size_t kMaxStoredColorPreferenceEntriesUVE = 64U;

[[nodiscard]] SettingDescriptorUVE HiddenUVE(SettingDescriptorUVE descriptor) {
    descriptor.flags |= Config::kSettingFlagHiddenUVE;
    return descriptor;
}

[[nodiscard]] SettingDescriptorUVE WithStepUVE(SettingDescriptorUVE descriptor, const double step) {
    descriptor.step = step;
    return descriptor;
}

[[nodiscard]] std::string IdUVE(const std::string_view id) {
    return std::string(id);
}

template <typename Enum>
[[nodiscard]] SettingEnumEntryUVE EntryUVE(const Enum value, std::string label) {
    return SettingEnumEntryUVE{static_cast<std::int64_t>(value), std::move(label)};
}

[[nodiscard]] float FloatUVE(const SettingValueUVE& value) {
    return static_cast<float>(std::get<double>(value));
}

[[nodiscard]] SettingStringListUVE EncodeColorListUVE(const std::vector<EditorColorUVE>& colors) {
    SettingStringListUVE encoded;
    encoded.reserve(colors.size());
    for (const EditorColorUVE& color : colors) {
        encoded.push_back(FormatColorHexUVE(color, true));
    }
    return encoded;
}

[[nodiscard]] std::vector<EditorColorUVE> DecodeColorListUVE(const SettingStringListUVE& encoded) {
    std::vector<EditorColorUVE> colors;
    colors.reserve(encoded.size());
    for (const std::string& value : encoded) {
        if (const std::optional<EditorColorUVE> color = ParseColorHexUVE(value)) {
            colors.push_back(*color);
        }
    }
    return colors;
}

[[nodiscard]] SettingStringListUVE EncodeInspectorFoldsUVE(const std::map<std::string, bool>& folds) {
    SettingStringListUVE encoded;
    encoded.reserve(folds.size());
    for (const auto& [key, open] : folds) {
        encoded.push_back(std::string{open ? "1:" : "0:"} + key);
    }
    return encoded;
}

[[nodiscard]] std::map<std::string, bool> DecodeInspectorFoldsUVE(const SettingStringListUVE& encoded) {
    std::map<std::string, bool> folds;
    for (const std::string& entry : encoded) {
        if (entry.size() <= 2U || entry[1U] != ':' || (entry[0U] != '0' && entry[0U] != '1')) {
            continue;
        }
        folds.insert_or_assign(entry.substr(2U), entry[0U] == '1');
    }
    return folds;
}

} // namespace

const std::vector<EditorSettingBindingUVE>& EditorUVE::GetSettingBindingsUVE() {
    namespace Id = EditorSettingIdUVE;
    using Workspace = EditorWorkspaceUVE;
    using RightTab = EditorRightPanelTabUVE;
    using BottomDock = EditorBottomDockUVE;
    using ViewMode = EditorUVE::ContentBrowserViewModeUVE;
    const EditorTransformSnappingSettingsUVE snapping{};
    const ViewportOverlayStateUVE overlay{};
    const ColorPickerPreferencesUVE picker{};
    const HierarchyViewSettingsUVE hierarchy{};

    static const std::vector<EditorSettingBindingUVE> bindings = {
        // Session state the editor remembers for itself.
        {HiddenUVE(Config::MakeBoolSettingUVE(IdUVE(Id::kScenePanelVisibleUVE), true, "Scene Panel",
                                              kSessionCategoryUVE)),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_scenePanelVisible; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_scenePanelVisible = std::get<bool>(value);
             return true;
         }},
        {HiddenUVE(Config::MakeBoolSettingUVE(IdUVE(Id::kViewportPanelVisibleUVE), true, "Viewport Panel",
                                              kSessionCategoryUVE)),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_viewportPanelVisible; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_viewportPanelVisible = std::get<bool>(value);
             return true;
         }},
        {HiddenUVE(Config::MakeBoolSettingUVE(IdUVE(Id::kInspectorPanelVisibleUVE), true, "Inspector Panel",
                                              kSessionCategoryUVE)),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_inspectorPanelVisible; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_inspectorPanelVisible = std::get<bool>(value);
             return true;
         }},
        {HiddenUVE(Config::MakeBoolSettingUVE(IdUVE(Id::kBottomDockVisibleUVE), true, "Bottom Dock",
                                              kSessionCategoryUVE)),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_bottomDockVisible; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_bottomDockVisible = std::get<bool>(value);
             return true;
         }},
        {HiddenUVE(Config::MakeFloatSettingUVE(IdUVE(Id::kBottomDockHeightUVE), 192.0, 96.0, 4096.0,
                                               "Bottom Dock Height", kSessionCategoryUVE)),
         [](const EditorUVE& editor) -> SettingValueUVE { return static_cast<double>(editor.m_bottomDockHeight); },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_bottomDockHeight = FloatUVE(value);
             return true;
         }},
        {HiddenUVE(Config::MakeEnumSettingUVE(
             IdUVE(Id::kContentBrowserViewModeUVE), static_cast<std::int64_t>(ViewMode::SmallTiles),
             {EntryUVE(ViewMode::SmallTiles, "Small Tiles"), EntryUVE(ViewMode::LargeTiles, "Large Tiles"),
              EntryUVE(ViewMode::List, "List")},
             "Content Browser View", kSessionCategoryUVE)),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<std::int64_t>(editor.m_contentBrowserViewMode);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_contentBrowserViewMode = static_cast<ViewMode>(std::get<std::int64_t>(value));
             return true;
         }},
        {HiddenUVE(Config::MakeEnumSettingUVE(
             IdUVE(Id::kContentBrowserModeUVE), static_cast<std::int64_t>(ContentBrowserModeUVE::Tiles),
             {EntryUVE(ContentBrowserModeUVE::Tiles, "Tiles"), EntryUVE(ContentBrowserModeUVE::Columns, "Columns"),
              EntryUVE(ContentBrowserModeUVE::Details, "Details"), EntryUVE(ContentBrowserModeUVE::Recent, "Recent"),
              EntryUVE(ContentBrowserModeUVE::Board, "Board")},
             "Content Browser Mode", kSessionCategoryUVE)),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<std::int64_t>(editor.m_contentBrowserMode);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_contentBrowserMode = static_cast<ContentBrowserModeUVE>(std::get<std::int64_t>(value));
             return true;
         }},
        // Game is not among the entries, so a session is never restored into it: saving while in
        // Game is refused and leaves the last restorable workspace stored.
        {HiddenUVE(Config::MakeEnumSettingUVE(IdUVE(Id::kActiveWorkspaceUVE),
                                              static_cast<std::int64_t>(Workspace::Library),
                                              {EntryUVE(Workspace::Library, "Library"),
                                               EntryUVE(Workspace::Asset, "Asset"),
                                               EntryUVE(Workspace::Scripting, "Scripting"),
                                               EntryUVE(Workspace::Debug, "Debug"),
                                               EntryUVE(Workspace::Plugin, "Plugin")},
                                              "Workspace", kSessionCategoryUVE)),
         [](const EditorUVE& editor) -> SettingValueUVE { return static_cast<std::int64_t>(editor.m_activeWorkspace); },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_activeWorkspace = static_cast<Workspace>(std::get<std::int64_t>(value));
             return true;
         }},
        {HiddenUVE(Config::MakeEnumSettingUVE(
             IdUVE(Id::kActiveRightPanelTabUVE), static_cast<std::int64_t>(RightTab::Inspector),
             {EntryUVE(RightTab::Inspector, "Inspector"), EntryUVE(RightTab::Import, "Import"),
              EntryUVE(RightTab::Events, "Events")},
             "Right Panel Tab", kSessionCategoryUVE)),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<std::int64_t>(editor.m_activeRightPanelTab);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_activeRightPanelTab = static_cast<RightTab>(std::get<std::int64_t>(value));
             return true;
         }},
        {HiddenUVE(Config::MakeEnumSettingUVE(
             IdUVE(Id::kActiveBottomDockUVE), static_cast<std::int64_t>(BottomDock::FileSystem),
             {EntryUVE(BottomDock::Debugger, "Debugger"), EntryUVE(BottomDock::Animator, "Animator"),
              EntryUVE(BottomDock::AIToolbar, "AI Toolbar"), EntryUVE(BottomDock::FileSystem, "File System"),
              EntryUVE(BottomDock::Console, "Console")},
             "Bottom Dock Panel", kSessionCategoryUVE)),
         [](const EditorUVE& editor) -> SettingValueUVE { return static_cast<std::int64_t>(editor.m_activeBottomDock); },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_activeBottomDock = static_cast<BottomDock>(std::get<std::int64_t>(value));
             return true;
         }},
        {HiddenUVE(Config::MakeBoolSettingUVE(IdUVE(Id::kColorPickerAdvancedOpenUVE), picker.advancedOpen,
                                              "Colour Picker Advanced", kSessionCategoryUVE)),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_colorPickerPreferences.advancedOpen; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_colorPickerPreferences.advancedOpen = std::get<bool>(value);
             return true;
         }},
        {HiddenUVE(Config::MakeStringListSettingUVE(IdUVE(Id::kColorPickerSavedUVE), {},
                                                    kMaxStoredColorPreferenceEntriesUVE, 0U,
                                                    "Saved Colours", kSessionCategoryUVE)),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return EncodeColorListUVE(editor.GetColorPickerPreferencesUVE().saved);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             ColorPickerPreferencesUVE preferences = editor.GetColorPickerPreferencesUVE();
             preferences.saved = DecodeColorListUVE(std::get<SettingStringListUVE>(value));
             editor.SetColorPickerPreferencesUVE(std::move(preferences));
             return true;
         }},
        {HiddenUVE(Config::MakeStringListSettingUVE(IdUVE(Id::kColorPickerRecentUVE), {},
                                                    kMaxStoredColorPreferenceEntriesUVE, 0U,
                                                    "Recent Colours", kSessionCategoryUVE)),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return EncodeColorListUVE(editor.GetColorPickerPreferencesUVE().recents);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             ColorPickerPreferencesUVE preferences = editor.GetColorPickerPreferencesUVE();
             preferences.recents = DecodeColorListUVE(std::get<SettingStringListUVE>(value));
             editor.SetColorPickerPreferencesUVE(std::move(preferences));
             return true;
         }},
        {HiddenUVE(Config::MakeStringListSettingUVE(IdUVE(Id::kInspectorFoldsUVE), {},
                                                    EditorUVE::kMaxRememberedInspectorFoldsUVE, 0U,
                                                    "Inspector Folds", kSessionCategoryUVE)),
         [](const EditorUVE& editor) -> SettingValueUVE { return EncodeInspectorFoldsUVE(editor.m_inspectorFoldOpen); },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_inspectorFoldOpen = DecodeInspectorFoldsUVE(std::get<SettingStringListUVE>(value));
             return true;
         }},
        {HiddenUVE(Config::MakeStringListSettingUVE(IdUVE(Id::kFavoriteProjectsUVE), {},
                                                    EditorUVE::kMaxPersistedFavoriteProjectsUVE, 0U,
                                                    "Favorite Projects", kSessionCategoryUVE)),
         [](const EditorUVE& editor) -> SettingValueUVE {
             const std::size_t count = std::min(editor.m_favoriteProjectPaths.size(),
                                                EditorUVE::kMaxPersistedFavoriteProjectsUVE);
             SettingStringListUVE paths;
             paths.reserve(count);
             for (std::size_t index = 0U; index < count; ++index) {
                 paths.push_back(editor.m_favoriteProjectPaths[index].generic_string());
             }
             return paths;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             const SettingStringListUVE& paths = std::get<SettingStringListUVE>(value);
             editor.m_favoriteProjectPaths.clear();
             editor.m_favoriteProjectPaths.reserve(paths.size());
             for (const std::string& path : paths) {
                 if (!path.empty()) {
                     editor.m_favoriteProjectPaths.emplace_back(path);
                 }
             }
             return true;
         }},

        // Snapping. The steps share their bounds with SetTransformSnappingSettingsUVE, so a value
        // legal here is one that setter accepts; it is assigned directly because loading happens
        // before authoring commands are allowed.
        {Config::MakeBoolSettingUVE(IdUVE(Id::kSnapEnabledUVE), snapping.enabled, "Snap", kSnappingCategoryUVE,
                                    "Snap moves, rotations and scales to the steps below."),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_transformSnappingSettings.enabled; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_transformSnappingSettings.enabled = std::get<bool>(value);
             editor.m_viewportOverlayState.snapEnabled = editor.m_transformSnappingSettings.enabled;
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kSnapTranslateStepUVE), snapping.translateStep,
                                     kMinimumTransformSnapStepUVE, kMaximumTransformSnapTranslateStepUVE, "Move Step",
                                     kSnappingCategoryUVE, "Distance a snapped move steps by, in metres."),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<double>(editor.m_transformSnappingSettings.translateStep);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_transformSnappingSettings.translateStep = FloatUVE(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kSnapRotateStepDegreesUVE), snapping.rotateStepDegrees,
                                     kMinimumTransformSnapStepUVE, kMaximumTransformSnapRotateStepDegreesUVE,
                                     "Rotate Step", kSnappingCategoryUVE,
                                     "Angle a snapped rotation steps by, in degrees."),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<double>(editor.m_transformSnappingSettings.rotateStepDegrees);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_transformSnappingSettings.rotateStepDegrees = FloatUVE(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kSnapScaleStepUVE), snapping.scaleStep, kMinimumTransformSnapStepUVE,
                                     kMaximumTransformSnapScaleStepUVE, "Scale Step", kSnappingCategoryUVE,
                                     "Amount a snapped scale steps by."),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<double>(editor.m_transformSnappingSettings.scaleStep);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_transformSnappingSettings.scaleStep = FloatUVE(value);
             return true;
         }},

        // Grid.
        {Config::MakeBoolSettingUVE(IdUVE(Id::kGridVisibleUVE), overlay.gridVisible, "Show Grid", kGridCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_viewportOverlayState.gridVisible; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_viewportOverlayState.gridVisible = std::get<bool>(value);
             return true;
         }},
        {WithStepUVE(Config::MakeFloatSettingUVE(IdUVE(Id::kGridOpacityUVE), overlay.gridOpacity,
                                                 kMinimumViewportGridOpacityUVE, 1.0, "Opacity", kGridCategoryUVE,
                                                 "How strongly the grid is drawn."),
                     0.05),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<double>(editor.m_viewportOverlayState.gridOpacity);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_viewportOverlayState.gridOpacity = FloatUVE(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kGridCellSizeUVE), overlay.gridCellSize,
                                     kMinimumViewportGridCellSizeUVE, kMaximumViewportGridCellSizeUVE, "Cell Size",
                                     kGridCategoryUVE,
                                     "The smallest grid square, in metres. Zooming out still steps up in tens."),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<double>(editor.m_viewportOverlayState.gridCellSize);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_viewportOverlayState.gridCellSize = FloatUVE(value);
             return true;
         }},

        // Selection outline.
        {Config::MakeBoolSettingUVE(IdUVE(Id::kSelectionOutlineVisibleUVE), overlay.selectionOutlineVisible,
                                    "Show Outline", kOutlineCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_viewportOverlayState.selectionOutlineVisible;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_viewportOverlayState.selectionOutlineVisible = std::get<bool>(value);
             return true;
         }},
        {Config::MakeColorSettingUVE(IdUVE(Id::kSelectionOutlineColorUVE),
                                     SettingColorUVE{overlay.selectionOutlineColor.r, overlay.selectionOutlineColor.g,
                                                     overlay.selectionOutlineColor.b},
                                     false, "Colour", kOutlineCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE {
             const ViewportAxisColorUVE& color = editor.m_viewportOverlayState.selectionOutlineColor;
             return SettingColorUVE{color.r, color.g, color.b};
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             const auto& color = std::get<SettingColorUVE>(value);
             editor.m_viewportOverlayState.selectionOutlineColor = ViewportAxisColorUVE{color.r, color.g, color.b};
             return true;
         }},
        {WithStepUVE(Config::MakeFloatSettingUVE(IdUVE(Id::kSelectionOutlineThicknessUVE),
                                                 overlay.selectionOutlineThickness,
                                                 kMinimumSelectionOutlineThicknessUVE,
                                                 kMaximumSelectionOutlineThicknessUVE, "Thickness",
                                                 kOutlineCategoryUVE, "Outline width, in pixels."),
                     1.0),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<double>(editor.m_viewportOverlayState.selectionOutlineThickness);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_viewportOverlayState.selectionOutlineThickness = FloatUVE(value);
             return true;
         }},

        // New objects.
        {Config::MakeBoolSettingUVE(IdUVE(Id::kNewObjectsUnderSelectionUVE), true, "Add Under Selection",
                                    kObjectsCategoryUVE,
                                    "A new object goes under the selected object. Off, it always goes under the scene root."),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_newObjectsUnderSelection; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_newObjectsUnderSelection = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kNewObjectPlacementUVE),
                                    static_cast<std::int64_t>(EditorNewObjectPlacementUVE::ParentOrigin),
                                    {EntryUVE(EditorNewObjectPlacementUVE::ParentOrigin, "Parent's Origin"),
                                     EntryUVE(EditorNewObjectPlacementUVE::ViewFocus, "View Focus")},
                                    "Placement", kObjectsCategoryUVE,
                                    "Where a new 3D object appears: at its parent's origin, or at the point the "
                                    "viewport camera orbits - where you are looking."),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<std::int64_t>(editor.m_newObjectPlacement);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_newObjectPlacement = static_cast<EditorNewObjectPlacementUVE>(std::get<std::int64_t>(value));
             return true;
         }},

        // Play mode.
        {Config::MakeBoolSettingUVE(IdUVE(Id::kPlayPauseOnStartUVE), false, "Pause on Start", kPlayCategoryUVE,
                                    "Enter Play paused, on the first frame, to step from there."),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_playPauseOnStart; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_playPauseOnStart = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kPlaySaveSceneFirstUVE), false, "Save Scene First", kPlayCategoryUVE,
                                    "Save the scene, if it has unsaved changes and a file, before Play starts."),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_playSaveSceneFirst; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_playSaveSceneFirst = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kPlaySwitchToGameUVE), true, "Switch to Game Tab", kPlayCategoryUVE,
                                    "Show the Game tab while playing, and the tab you were on after. Off, Play "
                                    "runs in the tab you are on."),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_playSwitchToGame; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_playSwitchToGame = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kPlayTintEnabledUVE), true, "Tint While Playing", kPlayCategoryUVE,
                                    "Tint the editor's panels while playing, so an edit made in Play - and lost "
                                    "when it stops - is never mistaken for a real one."),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_playTintEnabled; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_playTintEnabled = std::get<bool>(value);
             return true;
         }},
        {Config::MakeColorSettingUVE(IdUVE(Id::kPlayTintColorUVE),
                                     SettingColorUVE{kDefaultPlayTintColorUVE.r, kDefaultPlayTintColorUVE.g,
                                                     kDefaultPlayTintColorUVE.b},
                                     false, "Tint Colour", kPlayCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return SettingColorUVE{editor.m_playTintColor.r, editor.m_playTintColor.g, editor.m_playTintColor.b};
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             const auto& color = std::get<SettingColorUVE>(value);
             editor.m_playTintColor = ViewportAxisColorUVE{color.r, color.g, color.b};
             return true;
         }},
        {WithStepUVE(Config::MakeFloatSettingUVE(IdUVE(Id::kPlayTintStrengthUVE), kDefaultPlayTintStrengthUVE,
                                                 0.05, 0.6, "Tint Strength",
                                                 kPlayCategoryUVE, "How far the panels move toward the tint colour."),
                     0.05),
         [](const EditorUVE& editor) -> SettingValueUVE { return static_cast<double>(editor.m_playTintStrength); },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_playTintStrength = FloatUVE(value);
             return true;
         }},

        // Hierarchy panel.
        {Config::MakeBoolSettingUVE(IdUVE(Id::kHierarchyRevealSelectionUVE), hierarchy.revealSelection,
                                    "Reveal Selection", kHierarchyCategoryUVE,
                                    "When the selection changes, open the rows above it and scroll it into view."),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_hierarchyView.revealSelection; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_hierarchyView.revealSelection = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kHierarchyShowIconsUVE), hierarchy.showIcons, "Object Icons",
                                    kHierarchyCategoryUVE, "Draw each object's type icon before its name."),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_hierarchyView.showIcons; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_hierarchyView.showIcons = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kHierarchyShowTypeNameUVE), hierarchy.showTypeName, "Object Type Names",
                                    kHierarchyCategoryUVE,
                                    "Write each object's type after its name, dimmed, when the name is not already the "
                                    "type and there is room."),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_hierarchyView.showTypeName; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_hierarchyView.showTypeName = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kHierarchyVisibilityColumnUVE),
                                    static_cast<std::int64_t>(hierarchy.visibilityColumn),
                                    {EntryUVE(HierarchyVisibilityColumnUVE::Always, "Always"),
                                     EntryUVE(HierarchyVisibilityColumnUVE::OnHover, "On Hover"),
                                     EntryUVE(HierarchyVisibilityColumnUVE::Hidden, "Hidden")},
                                    "Visibility Toggles", kHierarchyCategoryUVE,
                                    "When a row shows its eye. On Hover still shows it on every hidden object, so a "
                                    "hidden object never looks shown."),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<std::int64_t>(editor.m_hierarchyView.visibilityColumn);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_hierarchyView.visibilityColumn =
                 static_cast<HierarchyVisibilityColumnUVE>(std::get<std::int64_t>(value));
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kHierarchyDoubleClickUVE), static_cast<std::int64_t>(hierarchy.doubleClick),
                                    {EntryUVE(HierarchyDoubleClickUVE::Rename, "Rename"),
                                     EntryUVE(HierarchyDoubleClickUVE::FocusInViewport, "Focus in Viewport"),
                                     EntryUVE(HierarchyDoubleClickUVE::ExpandCollapse, "Expand or Collapse")},
                                    "Double-Click", kHierarchyCategoryUVE,
                                    "What a double-click on a row does. F2 always renames."),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<std::int64_t>(editor.m_hierarchyView.doubleClick);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_hierarchyView.doubleClick = static_cast<HierarchyDoubleClickUVE>(std::get<std::int64_t>(value));
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kHierarchyDragToReparentUVE), hierarchy.dragToReparent,
                                    "Drag to Reparent", kHierarchyCategoryUVE,
                                    "Drag a row onto another to move it under that object. Off, rows stay put when "
                                    "dragged."),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_hierarchyView.dragToReparent; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_hierarchyView.dragToReparent = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kHierarchyTreeLinesUVE), static_cast<std::int64_t>(hierarchy.treeLines),
                                    {EntryUVE(HierarchyTreeLinesUVE::None, "None"),
                                     EntryUVE(HierarchyTreeLinesUVE::ToEachChild, "To Each Child"),
                                     EntryUVE(HierarchyTreeLinesUVE::FullHeight, "Full Height")},
                                    "Tree Lines", kHierarchyCategoryUVE,
                                    "Lines joining each row to its parent. Full Height is cheaper on very large "
                                    "scenes."),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<std::int64_t>(editor.m_hierarchyView.treeLines);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_hierarchyView.treeLines = static_cast<HierarchyTreeLinesUVE>(std::get<std::int64_t>(value));
             return true;
         }},
        {WithStepUVE(Config::MakeFloatSettingUVE(IdUVE(Id::kHierarchyIndentWidthUVE), hierarchy.indentWidth,
                                                 kMinimumHierarchyIndentUVE, kMaximumHierarchyIndentUVE,
                                                 "Indent Width", kHierarchyCategoryUVE,
                                                 "Pixels each level of the tree is indented by."),
                     1.0),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<double>(editor.m_hierarchyView.indentWidth);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_hierarchyView.indentWidth = FloatUVE(value);
             return true;
         }},
    };
    return bindings;
}

const EditorSettingBindingUVE* EditorUVE::FindSettingBindingUVE(const std::string_view id) {
    for (const EditorSettingBindingUVE& binding : GetSettingBindingsUVE()) {
        if (binding.descriptor.id == id) {
            return &binding;
        }
    }
    return nullptr;
}

std::optional<Config::SettingValueUVE> EditorUVE::GetEditorSettingUVE(const std::string_view id) const {
    const EditorSettingBindingUVE* binding = FindSettingBindingUVE(id);
    return binding != nullptr ? std::optional<SettingValueUVE>(binding->get(*this)) : GetShortcutSettingUVE(id);
}

bool EditorUVE::SetEditorSettingUVE(const std::string_view id, const Config::SettingValueUVE& value) {
    const Config::SettingDescriptorUVE* descriptor = m_settingsRegistry.FindUVE(id);
    if (descriptor == nullptr || descriptor->HasFlagUVE(Config::kSettingFlagDeprecatedUVE) ||
        !Config::IsSettingValueValidUVE(*descriptor, value)) {
        return false;
    }

    const std::optional<SettingValueUVE> previousValue = GetEditorSettingUVE(id);
    if (!previousValue) {
        return false;
    }

    const EditorSettingBindingUVE* binding = FindSettingBindingUVE(id);
    const bool applied = binding != nullptr ? binding->set(*this, value) : SetShortcutSettingUVE(id, value);
    if (!applied) {
        return false;
    }

    if (const std::optional<SettingValueUVE> newValue = GetEditorSettingUVE(id);
        newValue && *previousValue != *newValue) {
        NotifyEditorSettingChangedUVE(id, *previousValue, *newValue);
    }
    return true;
}

Config::SettingsObserverSubscriptionUVE EditorUVE::SubscribeToSettingUVE(
    const std::string_view id, Config::SettingsObserverCallbackUVE callback) {
    return m_settingsObservers.SubscribeToSettingUVE(id, std::move(callback));
}

Config::SettingsObserverSubscriptionUVE EditorUVE::SubscribeToCategoryUVE(
    const std::string_view categoryPrefix, Config::SettingsObserverCallbackUVE callback) {
    return m_settingsObservers.SubscribeToCategoryUVE(categoryPrefix, std::move(callback));
}

bool EditorUVE::UnsubscribeUVE(const Config::SettingsObserverSubscriptionUVE subscription) {
    return m_settingsObservers.UnsubscribeUVE(subscription);
}

void EditorUVE::NotifyEditorSettingChangedUVE(const std::string_view id,
                                               const Config::SettingValueUVE& previousValue,
                                               const Config::SettingValueUVE& newValue) {
    m_settingsObservers.NotifyChangedUVE(id, previousValue, newValue);
}

namespace {

[[nodiscard]] bool ContainsIgnoringCaseUVE(const std::string_view text, const std::string_view word) {
    const auto lower = [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return std::search(text.begin(), text.end(), word.begin(), word.end(),
                       [&lower](const char a, const char b) { return lower(a) == lower(b); }) != text.end();
}

struct CategoryTreeObjectUVE final {
    std::string path;
    std::vector<std::unique_ptr<CategoryTreeObjectUVE>> children;
};

void FlattenCategoryTreeUVE(const CategoryTreeObjectUVE& object, const int depth,
                            std::vector<SettingCategoryObjectUVE>& out) {
    for (const auto& child : object.children) {
        const std::size_t slash = child->path.rfind('/');
        out.push_back(SettingCategoryObjectUVE{
            child->path, slash == std::string::npos ? child->path : child->path.substr(slash + 1U), depth});
        FlattenCategoryTreeUVE(*child, depth + 1, out);
    }
}

} // namespace

bool MatchesSettingSearchUVE(const Config::SettingDescriptorUVE& descriptor, const std::string_view query) {
    std::size_t start = 0U;
    while (start < query.size()) {
        std::size_t end = query.find(' ', start);
        end = end == std::string_view::npos ? query.size() : end;
        const std::string_view word = query.substr(start, end - start);
        if (!word.empty() && !ContainsIgnoringCaseUVE(descriptor.displayName, word) &&
            !ContainsIgnoringCaseUVE(descriptor.category, word) && !ContainsIgnoringCaseUVE(descriptor.id, word) &&
            !ContainsIgnoringCaseUVE(descriptor.tooltip, word)) {
            return false;
        }
        start = end + 1U;
    }
    return true;
}

bool IsInSettingCategoryUVE(const std::string_view category, const std::string_view path) noexcept {
    return category.starts_with(path) && (category.size() == path.size() || category[path.size()] == '/');
}

std::vector<SettingCategoryObjectUVE> BuildSettingCategoryTreeUVE(
    const std::vector<const Config::SettingDescriptorUVE*>& descriptors) {
    CategoryTreeObjectUVE root;
    for (const Config::SettingDescriptorUVE* descriptor : descriptors) {
        const std::string& category = descriptor->category;
        if (category.empty()) {
            continue;
        }
        // Walk down "Editor", "Editor/Viewport", "Editor/Viewport/Grid", adding what is missing.
        CategoryTreeObjectUVE* object = &root;
        for (std::size_t slash = category.find('/');; slash = category.find('/', slash + 1U)) {
            std::string path = category.substr(0U, slash);
            const auto found = std::find_if(object->children.begin(), object->children.end(),
                                            [&path](const auto& child) { return child->path == path; });
            object = found != object->children.end()
                       ? found->get()
                       : object->children.emplace_back(std::make_unique<CategoryTreeObjectUVE>()).get();
            object->path = std::move(path);
            if (slash == std::string::npos) {
                break;
            }
        }
    }
    std::vector<SettingCategoryObjectUVE> flattened;
    FlattenCategoryTreeUVE(root, 0, flattened);
    return flattened;
}

std::string FormatSettingValueUVE(const Config::SettingDescriptorUVE& descriptor,
                                  const Config::SettingValueUVE& value) {
    if (const auto* flag = std::get_if<bool>(&value)) {
        return *flag ? "On" : "Off";
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        for (const Config::SettingEnumEntryUVE& entry : descriptor.enumEntries) {
            if (entry.value == *integer) {
                return entry.label;
            }
        }
        return std::to_string(*integer);
    }
    if (const auto* number = std::get_if<double>(&value)) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.6g", *number);
        return text;
    }
    if (const auto* color = std::get_if<Config::SettingColorUVE>(&value)) {
        return FormatColorHexUVE(EditorColorUVE{color->r, color->g, color->b, color->a}, descriptor.colorHasAlpha);
    }
    if (const auto* vector = std::get_if<Config::SettingVector3UVE>(&value)) {
        char text[96];
        std::snprintf(text, sizeof(text), "(%.6g, %.6g, %.6g)", vector->x, vector->y, vector->z);
        return text;
    }
    if (const auto* list = std::get_if<Config::SettingStringListUVE>(&value)) {
        return std::to_string(list->size()) + (list->size() == 1U ? " item" : " items");
    }
    const auto* text = std::get_if<std::string>(&value);
    return text != nullptr ? *text : std::string{};
}

namespace {

/// The old name of a renamed setting, declared as an alias of the setting that replaced it: same
/// type, bounds and enum entries, so a value stored under the old name is read exactly as the new
/// one validates. Hidden, so the preferences window never offers a name the editor no longer uses;
/// Deprecated, so the registry migrates a legacy value to the replacement and removes the old key.
[[nodiscard]] Config::SettingDescriptorUVE MakeRenamedSettingAliasUVE(const Config::SettingDescriptorUVE& current,
                                                                     const RenamedSettingIdUVE& renamed) {
    Config::SettingDescriptorUVE alias = current;
    alias.id = std::string(renamed.oldId);
    alias.flags |= Config::kSettingFlagHiddenUVE | Config::kSettingFlagDeprecatedUVE;
    alias.replacementId = std::string(renamed.newId);
    return alias;
}

} // namespace

bool RegisterEditorSettingsUVE(Config::SettingsRegistryUVE& registry) {
    const std::vector<EditorSettingBindingUVE>& bindings = EditorUVE::GetSettingBindingsUVE();
    bool allRegistered = true;
    for (const EditorSettingBindingUVE& binding : bindings) {
        allRegistered = registry.RegisterUVE(binding.descriptor) && allRegistered;
    }
    for (const RenamedSettingIdUVE& renamed : kRenamedSettingIdsUVE) {
        const auto binding = std::find_if(bindings.cbegin(), bindings.cend(), [&renamed](const EditorSettingBindingUVE& candidate) {
            return candidate.descriptor.id == renamed.newId;
        });
        // A rename that names a setting nobody declares is a programming error, not a runtime
        // condition: fail registration so the editor's settings test catches it.
        allRegistered = binding != bindings.cend() &&
                        registry.RegisterUVE(MakeRenamedSettingAliasUVE(binding->descriptor, renamed)) && allRegistered;
    }

    // The viewport module owns these defaults, so EditorCore registers only the persisted values.
    // Load/save handles them as one palette: the host's default colours are never replaced by a
    // descriptor's placeholder default when no complete stored palette exists.
    allRegistered = registry.RegisterUVE(HiddenUVE(Config::MakeColorSettingUVE(
                        IdUVE(EditorSettingIdUVE::kViewportAxisColorXUVE), {}, false, "X Axis Colour",
                        kSessionCategoryUVE))) && allRegistered;
    allRegistered = registry.RegisterUVE(HiddenUVE(Config::MakeColorSettingUVE(
                        IdUVE(EditorSettingIdUVE::kViewportAxisColorYUVE), {}, false, "Y Axis Colour",
                        kSessionCategoryUVE))) && allRegistered;
    allRegistered = registry.RegisterUVE(HiddenUVE(Config::MakeColorSettingUVE(
                        IdUVE(EditorSettingIdUVE::kViewportAxisColorZUVE), {}, false, "Z Axis Colour",
                        kSessionCategoryUVE))) && allRegistered;
    return allRegistered;
}

} // namespace UVE::Editor
