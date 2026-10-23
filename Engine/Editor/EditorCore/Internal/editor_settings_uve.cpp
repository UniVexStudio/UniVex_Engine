// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_settings_uve.h"
#include "uve/editor/editor_theme_uve.h"

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
              EntryUVE(BottomDock::Console, "Console"), EntryUVE(BottomDock::Capture, "Capture"),
              EntryUVE(BottomDock::Profiler, "Profiler"), EntryUVE(BottomDock::Audio, "Audio")},
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
                                    "What a double-click on a row does, unless the row opens something: an instance of an entity "
                                    "asset opens that asset, and the level's Viewport opens its scene. F2 always renames."),
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

        // Part 2.1 Interface & Theme
        {Config::MakeEnumSettingUVE(IdUVE(Id::kThemePresetUVE), 0,
                                    {{0, "Dark"}, {1, "Light"}, {2, "High Contrast"}, {3, "Custom"}},
                                    "Theme Preset", "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.themePreset; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.themePreset = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFilePathSettingUVE(IdUVE(Id::kCustomThemeFileUVE), "", 512U, "Custom Theme File",
                                        "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.customThemeFile; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.customThemeFile = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeColorSettingUVE(IdUVE(Id::kThemeBaseColorUVE),
                                     SettingColorUVE{0.14F, 0.15F, 0.18F, 1.0F}, false, "Base Colour",
                                     "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.themeBaseColor; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.themeBaseColor = std::get<SettingColorUVE>(value);
             return true;
         }},
        {Config::MakeColorSettingUVE(IdUVE(Id::kThemeAccentColorUVE),
                                     SettingColorUVE{0.22F, 0.56F, 0.96F, 1.0F}, false, "Accent Colour",
                                     "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.themeAccentColor; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.themeAccentColor = std::get<SettingColorUVE>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kThemeContrastUVE), 1.0, 0.5, 2.0, "Theme Contrast",
                                     "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.themeContrast; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.themeContrast = std::get<double>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kUiScaleModeUVE), 0, {{0, "Auto from DPI"}, {1, "Manual"}},
                                    "UI Scale Mode", "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.uiScaleMode; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.uiScaleMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kUiScaleFactorUVE), 1.0, 0.5, 3.0, "UI Scale Factor",
                                     "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.uiScaleFactor; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.uiScaleFactor = std::get<double>(value);
             return true;
         }},
        {Config::MakeIntSettingUVE(IdUVE(Id::kFontSizePxUVE), 14, 10, 32, "Font Size (px)", "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.fontSizePx; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.fontSizePx = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kFontHintingModeUVE), 2,
                                    {{0, "None"}, {1, "Slight"}, {2, "Normal"}, {3, "Subpixel"}},
                                    "Font Hinting", "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.fontHintingMode; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.fontHintingMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeIntSettingUVE(IdUVE(Id::kFontOversamplingUVE), 2, 1, 4, "Font Oversampling",
                                   "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.fontOversampling; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.fontOversampling = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kIconSaturationUVE), 1.0, 0.0, 1.0, "Icon Saturation",
                                     "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.iconSaturation; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.iconSaturation = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kCornerRadiusPxUVE), 4.0, 0.0, 12.0, "Corner Radius (px)",
                                     "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.cornerRadiusPx; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.cornerRadiusPx = std::get<double>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kSpacingDensityUVE), 1, {{0, "Compact"}, {1, "Comfortable"}},
                                    "Spacing Density", "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.spacingDensity; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.spacingDensity = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kUiAnimationSpeedUVE), 1.0, 0.0, 4.0, "UI Animation Speed",
                                     "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.uiAnimationSpeed; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.uiAnimationSpeed = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kReduceMotionUVE), false, "Reduce Motion", "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.reduceMotion; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.reduceMotion = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kTooltipDelaySecondsUVE), 0.35, 0.0, 5.0, "Tooltip Delay (s)",
                                     "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.tooltipDelaySeconds; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.tooltipDelaySeconds = std::get<double>(value);
             return true;
         }},
        {Config::MakeStringSettingUVE(IdUVE(Id::kEditorLanguageUVE), "en", 32U, "Editor Language",
                                      "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.editorLanguage; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.editorLanguage = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kTouchFriendlyTargetsUVE), false, "Touch-Friendly Hit Targets",
                                    "Editor/Interface"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.touchFriendlyTargets; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.touchFriendlyTargets = std::get<bool>(value);
             return true;
         }},

        // Part 2.2 Layout, Docking & Workspaces
        {Config::MakeBoolSettingUVE(IdUVE(Id::kPersistDockLayoutUVE), true, "Persist Dock Layout",
                                    "Editor/Layout"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.persistDockLayout; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.persistDockLayout = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kLeftSplitRatioUVE), 0.2, 0.1, 0.5, "Left Dock Split Ratio",
                                     "Editor/Layout"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.leftSplitRatio; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.leftSplitRatio = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kRightSplitRatioUVE), 0.25, 0.1, 0.5, "Right Dock Split Ratio",
                                     "Editor/Layout"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.rightSplitRatio; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.rightSplitRatio = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kAllowFloatingWindowsUVE), true, "Allow Floating Windows",
                                    "Editor/Layout"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.allowFloatingWindows; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.allowFloatingWindows = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kTabPositionUVE), 0, {{0, "Top"}, {1, "Bottom"}},
                                    "Tab Bar Position", "Editor/Layout"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.tabPosition; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.tabPosition = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kDistractionFreeModeUVE), false, "Distraction-Free Mode",
                                    "Editor/Layout"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.distractionFreeMode; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.distractionFreeMode = std::get<bool>(value);
             return true;
         }},

        // Part 2.3 Hierarchy Extended
        {Config::MakeIntSettingUVE(IdUVE(Id::kHierarchyExpandDepthLimitUVE), 16, 1, 64, "Expand Depth Limit",
                                   kHierarchyCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.hierarchyExpandDepthLimit;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.hierarchyExpandDepthLimit = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kHierarchyPersistExpansionUVE), true, "Persist Expansion State",
                                    kHierarchyCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.hierarchyPersistExpansion;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.hierarchyPersistExpansion = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kHierarchyRowHeightPxUVE), 22.0, 18.0, 40.0, "Row Height (px)",
                                     kHierarchyCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.hierarchyRowHeightPx; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.hierarchyRowHeightPx = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kHierarchyColorCodeIconsUVE), true, "Colour-Code Icons by Type",
                                    kHierarchyCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.hierarchyColorCodeIcons;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.hierarchyColorCodeIcons = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kHierarchyShowLockColumnUVE), true, "Show Lock Column",
                                    kHierarchyCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.hierarchyShowLockColumn;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.hierarchyShowLockColumn = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kHierarchyFilterModeUVE), 2,
                                    {{0, "Name Only"}, {1, "Name + Type"}, {2, "Name + Type + Component"}},
                                    "Filter Matching Mode", kHierarchyCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.hierarchyFilterMode; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.hierarchyFilterMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kHierarchySortModeUVE), 0,
                                    {{0, "Scene Order"}, {1, "Alphabetical"}, {2, "By Object Type"}},
                                    "Sort Mode", kHierarchyCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.hierarchySortMode; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.hierarchySortMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kHierarchyTintByTagColorUVE), true, "Tint Rows by Tag Colour",
                                    kHierarchyCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.hierarchyTintByTagColor;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.hierarchyTintByTagColor = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kHierarchyRenameModeUVE), 0, {{0, "Inline"}, {1, "Dialog"}},
                                    "Rename Mode", kHierarchyCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.hierarchyRenameMode; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.hierarchyRenameMode = std::get<std::int64_t>(value);
             return true;
         }},

        // Part 2.4 Context Menu
        {Config::MakeStringSettingUVE(IdUVE(Id::kContextDuplicateSuffixUVE), "_{n}", 32U,
                                      "Duplicate Name Suffix", "Editor/Context Menu"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.contextDuplicateSuffix;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.contextDuplicateSuffix = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kContextReparentKeepGlobalUVE), true,
                                    "Reparent Keeps Global Transform", "Editor/Context Menu"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.contextReparentKeepGlobal;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.contextReparentKeepGlobal = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kContextSelectOnOpenUVE), true, "Select Object on Context Menu",
                                    "Editor/Context Menu"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.contextSelectOnOpen; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.contextSelectOnOpen = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kContextTouchHoldSecondsUVE), 0.5, 0.2, 2.0,
                                     "Touch Long-Press Duration (s)", "Editor/Context Menu"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.contextTouchHoldSeconds;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.contextTouchHoldSeconds = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kContextConfirmDeleteSubtreeUVE), true,
                                    "Confirm Delete Subtree", "Editor/Context Menu"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.contextConfirmDeleteSubtree;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.contextConfirmDeleteSubtree = std::get<bool>(value);
             return true;
         }},

        // Part 2.5 Commands & Menu Bar
        {Config::MakeEnumSettingUVE(IdUVE(Id::kCommandPaletteFuzzyModeUVE), 0,
                                    {{0, "Smart Fuzzy"}, {1, "Prefix Only"}, {2, "Substring"}},
                                    "Command Palette Matching", "Editor/Commands"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.commandPaletteFuzzyMode;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.commandPaletteFuzzyMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeIntSettingUVE(IdUVE(Id::kCommandPaletteRecentDepthUVE), 8, 0, 32,
                                   "Recent Command History Depth", "Editor/Commands"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.commandPaletteRecentDepth;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.commandPaletteRecentDepth = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeStringSettingUVE(IdUVE(Id::kShortcutProfileUVE), "Default", 64U, "Shortcut Profile",
                                      "Editor/Commands"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.shortcutProfile; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.shortcutProfile = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeIntSettingUVE(IdUVE(Id::kRecentFilesLimitUVE), 15, 1, 50, "Recent Files Limit",
                                   "Editor/Commands"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.recentFilesLimit; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.recentFilesLimit = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeIntSettingUVE(IdUVE(Id::kToolbarButtonSizePxUVE), 24, 16, 48, "Toolbar Button Size (px)",
                                   "Editor/Commands"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.toolbarButtonSizePx;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.toolbarButtonSizePx = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kAutoSavePreferencesUVE), true, "Auto-Save Preferences on Change",
                                    "Editor/Commands"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.autoSavePreferences; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.autoSavePreferences = std::get<bool>(value);
             return true;
         }},

        // Part 2.6 Viewport Extended
        {Config::MakeIntSettingUVE(IdUVE(Id::kGridSubdivisionsUVE), 10, 1, 16, "Subdivisions", kGridCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.gridSubdivisions; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.gridSubdivisions = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kGridFadeDistanceUVE), 200.0, 10.0, 1000.0, "Fade Distance (m)",
                                     kGridCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.gridFadeDistance; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.gridFadeDistance = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kGridFollowsCameraUVE), true, "Grid Follows Camera",
                                    kGridCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.gridFollowsCamera; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.gridFollowsCamera = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kGizmoSizePxUVE), 96.0, 40.0, 200.0, "Gizmo Size (px)",
                                     "Editor/Viewport/Gizmo"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.gizmoSizePx; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.gizmoSizePx = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kGizmoOpacityUVE), 0.95, 0.2, 1.0, "Gizmo Opacity",
                                     "Editor/Viewport/Gizmo"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.gizmoOpacity; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.gizmoOpacity = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kRememberGizmoModeUVE), true, "Remember Active Transform Mode",
                                    "Editor/Viewport/Gizmo"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.rememberGizmoMode; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.rememberGizmoMode = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kDefaultTransformSpaceUVE), 0, {{0, "Local"}, {1, "World"}},
                                    "Default Transform Space", "Editor/Viewport/Gizmo"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.defaultTransformSpace;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.defaultTransformSpace = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kPivotModeUVE), 0,
                                    {{0, "Median Point"}, {1, "Active Object"}, {2, "Individual Origins"}},
                                    "Transform Pivot Mode", "Editor/Viewport/Gizmo"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.pivotMode; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.pivotMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kNavigationSchemeUVE), 0,
                                    {{0, "UniVex Default"}, {1, "Maya (Alt)"}, {2, "Blender (Middle Mouse)"}, {3, "Right-Click Fly"}},
                                    "Navigation Scheme", "Editor/Viewport/Navigation"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.navigationScheme; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.navigationScheme = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kOrbitSensitivityUVE), 1.0, 0.1, 5.0, "Orbit Sensitivity",
                                     "Editor/Viewport/Navigation"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.orbitSensitivity; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.orbitSensitivity = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kPanSensitivityUVE), 1.0, 0.1, 5.0, "Pan Sensitivity",
                                     "Editor/Viewport/Navigation"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.panSensitivity; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.panSensitivity = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kZoomSensitivityUVE), 1.0, 0.1, 5.0, "Zoom Sensitivity",
                                     "Editor/Viewport/Navigation"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.zoomSensitivity; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.zoomSensitivity = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kCameraInertiaSmoothingUVE), 0.15, 0.0, 1.0,
                                     "Camera Inertia Smoothing", "Editor/Viewport/Navigation"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.cameraInertiaSmoothing;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.cameraInertiaSmoothing = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kFreelookBaseSpeedUVE), 8.0, 0.5, 100.0,
                                     "Freelook Base Speed (m/s)", "Editor/Viewport/Navigation"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.freelookBaseSpeed; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.freelookBaseSpeed = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kDefaultCameraFovUVE), 60.0, 20.0, 120.0,
                                     "Default Camera FOV (deg)", "Editor/Viewport"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.defaultCameraFovDegrees;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.defaultCameraFovDegrees = std::get<double>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kBoxSelectModeUVE), 0, {{0, "Touch / Intersect"}, {1, "Enclose"}},
                                    "Box Select Mode", "Editor/Viewport"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.boxSelectMode; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.boxSelectMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kDefaultViewModeUVE), 0,
                                    {{0, "Lit"}, {1, "Unshaded"}, {2, "Wireframe"}, {3, "Overdraw"}, {4, "Normals"}},
                                    "Default View Mode", "Editor/Viewport"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.defaultViewMode; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.defaultViewMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kViewportRenderScaleUVE), 1.0, 0.25, 2.0,
                                     "Viewport Render Scale", "Editor/Viewport"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.viewportRenderScale; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.viewportRenderScale = std::get<double>(value);
             return true;
         }},
        {Config::MakeIntSettingUVE(IdUVE(Id::kViewportPaneCountUVE), 1, 1, 4, "Viewport Pane Count",
                                   "Editor/Viewport"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.viewportPaneCount; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.viewportPaneCount = std::get<std::int64_t>(value);
             return true;
         }},

        // Part 2.7 Inspector Extended
        {Config::MakeFloatSettingUVE(IdUVE(Id::kInspectorLabelRatioUVE), 0.38, 0.2, 0.7, "Label Column Ratio",
                                     "Editor/Inspector"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.inspectorLabelRatio; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.inspectorLabelRatio = std::get<double>(value);
             return true;
         }},
        {Config::MakeIntSettingUVE(IdUVE(Id::kInspectorFloatPrecisionUVE), 3, 1, 8, "Float Display Precision",
                                   "Editor/Inspector"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.inspectorFloatPrecision;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.inspectorFloatPrecision = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kInspectorAngleUnitUVE), 0, {{0, "Degrees"}, {1, "Radians"}},
                                    "Angle Display Unit", "Editor/Inspector"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.inspectorAngleUnit; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.inspectorAngleUnit = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kInspectorShowAdvancedUVE), false, "Show Advanced Properties",
                                    "Editor/Inspector"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.inspectorShowAdvanced;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.inspectorShowAdvanced = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kInspectorColorPickerShapeUVE), 0,
                                    {{0, "Hue Wheel & Disc"}, {1, "Hue Bar & Rectangle"}, {2, "Value/Hue Circle"},
                                     {3, "Perceptual Lightness Circle"}, {4, "Sliders & Hex Only"}},
                                    "Default Colour Picker Shape", "Editor/Inspector"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.inspectorColorPickerShape;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.inspectorColorPickerShape = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kInspectorColorPickerModeUVE), 0,
                                    {{0, "RGB (0..1)"}, {1, "RGB (0..255)"}, {2, "HSV (deg / %)"}, {3, "Perceptual OKLab"}},
                                    "Default Colour Picker Mode", "Editor/Inspector"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.inspectorColorPickerMode;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.inspectorColorPickerMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kInspectorOpenResourcesModeUVE), 0,
                                    {{0, "Inline Sub-Inspector"}, {1, "New Panel"}},
                                    "Open Resources In", "Editor/Inspector"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.inspectorOpenResourcesMode;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.inspectorOpenResourcesMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kInspectorPlayRefreshHzUVE), 15.0, 1.0, 60.0,
                                     "Play Mode Auto-Refresh (Hz)", "Editor/Inspector"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.inspectorPlayRefreshHz;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.inspectorPlayRefreshHz = std::get<double>(value);
             return true;
         }},

        // Part 2.8 Script & Shader Editor
        {Config::MakeIntSettingUVE(IdUVE(Id::kScriptFontSizePxUVE), 14, 8, 36, "Script Font Size (px)",
                                   "Editor/Script Editor"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.scriptFontSizePx; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.scriptFontSizePx = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kScriptLineNumbersUVE), true, "Show Line Numbers",
                                    "Editor/Script Editor"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.scriptShowLineNumbers;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.scriptShowLineNumbers = std::get<bool>(value);
             return true;
         }},
        {Config::MakeIntSettingUVE(IdUVE(Id::kScriptTabSizeUVE), 4, 1, 8, "Tab Size", "Editor/Script Editor"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.scriptTabSize; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.scriptTabSize = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kScriptWordWrapModeUVE), 0,
                                    {{0, "Off"}, {1, "Viewport Width"}, {2, "Wrap Column"}},
                                    "Word Wrap Mode", "Editor/Script Editor"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.scriptWordWrapMode; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.scriptWordWrapMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeStringSettingUVE(IdUVE(Id::kScriptSyntaxThemeUVE), "UniVex Dark", 64U,
                                      "Syntax Highlighting Theme", "Editor/Script Editor"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.scriptSyntaxTheme; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.scriptSyntaxTheme = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kScriptCodeCompletionUVE), true, "Code Completion",
                                    "Editor/Script Editor"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.scriptCodeCompletion;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.scriptCodeCompletion = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kScriptMinimapEnabledUVE), true, "Show Minimap",
                                    "Editor/Script Editor"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.scriptMinimapEnabled;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.scriptMinimapEnabled = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kScriptFormatOnSaveUVE), false, "Format on Save",
                                    "Editor/Script Editor"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.scriptFormatOnSave; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.scriptFormatOnSave = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFilePathSettingUVE(IdUVE(Id::kScriptExternalEditorPathUVE), "", 512U,
                                        "External Script Editor Executable", "Editor/Script Editor"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.scriptExternalExecutable;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.scriptExternalExecutable = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kNodeGraphWireStyleUVE), 0,
                                    {{0, "Curved"}, {1, "Straight"}, {2, "Orthogonal"}},
                                    "Visual Graph Connection Style", "Editor/Script Editor"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.nodeGraphWireStyle; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.nodeGraphWireStyle = std::get<std::int64_t>(value);
             return true;
         }},

        // Part 2.9 Asset Browser
        {Config::MakeIntSettingUVE(IdUVE(Id::kAssetThumbnailSizePxUVE), 96, 32, 256, "Thumbnail Size (px)",
                                   "Editor/Asset Browser"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.assetThumbnailSizePx;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.assetThumbnailSizePx = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kAssetSortModeUVE), 0,
                                    {{0, "Name"}, {1, "Type"}, {2, "Modified Time"}, {3, "Size"}},
                                    "Default Sort Mode", "Editor/Asset Browser"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.assetSortMode; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.assetSortMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kAssetShowHiddenFilesUVE), false, "Show Hidden & Internal Files",
                                    "Editor/Asset Browser"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.assetShowHiddenFiles;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.assetShowHiddenFiles = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kAssetSplitRatioUVE), 0.25, 0.1, 0.6, "Tree / Contents Split Ratio",
                                     "Editor/Asset Browser"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.assetSplitRatio; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.assetSplitRatio = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kAssetAutoReimportUVE), true, "Auto-Reimport on External Change",
                                    "Editor/Asset Browser"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.assetAutoReimport; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.assetAutoReimport = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kAssetDeleteToTrashUVE), true, "Move Deleted Assets to Trash",
                                    "Editor/Asset Browser"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.assetDeleteToTrash; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.assetDeleteToTrash = std::get<bool>(value);
             return true;
         }},

        // Part 2.10 Console, Log & Debug
        {Config::MakeBoolSettingUVE(IdUVE(Id::kConsoleCollapseDuplicatesUVE), true, "Collapse Duplicate Messages",
                                    "Editor/Console"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.consoleCollapseDuplicates;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.consoleCollapseDuplicates = std::get<bool>(value);
             return true;
         }},
        {Config::MakeIntSettingUVE(IdUVE(Id::kConsoleMaxLinesUVE), 5000, 100, 100000, "Max Retained Log Lines",
                                   "Editor/Console"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.consoleMaxLines; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.consoleMaxLines = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kConsoleClearOnPlayUVE), false, "Clear Console on Play",
                                    "Editor/Console"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.consoleClearOnPlay; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.consoleClearOnPlay = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kConsoleShowStackTracesUVE), true, "Show Stack Traces",
                                    "Editor/Console"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.consoleShowStackTraces;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.consoleShowStackTraces = std::get<bool>(value);
             return true;
         }},
        {Config::MakeIntSettingUVE(IdUVE(Id::kConsoleHistoryDepthUVE), 64, 8, 512, "Command History Depth",
                                   "Editor/Console"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.consoleHistoryDepth;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.consoleHistoryDepth = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeIntSettingUVE(IdUVE(Id::kConsoleRemotePortUVE), 9229, 1024, 65535, "Remote Console Port",
                                   "Editor/Console"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.consoleRemotePort; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.consoleRemotePort = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kDebugPhysicsDrawUVE), false, "Physics Debug Draw", "Editor/Debug"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.debugPhysicsDraw; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.debugPhysicsDraw = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kDebugNavigationDrawUVE), false, "Navigation Debug Draw",
                                    "Editor/Debug"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.debugNavigationDraw;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.debugNavigationDraw = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kDebugRendererBoundsUVE), false, "Renderer Bounds & Volumes",
                                    "Editor/Debug"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.debugRendererBounds;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.debugRendererBounds = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kDebugAudioRadiiUVE), false, "Audio Emitter Radii", "Editor/Debug"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.debugAudioRadii; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.debugAudioRadii = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kDebugAssertionBehaviorUVE), 0,
                                    {{0, "Break into Debugger"}, {1, "Log and Continue"}, {2, "Abort"}},
                                    "Assertion Behaviour", "Editor/Debug"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.debugAssertionBehavior;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.debugAssertionBehavior = std::get<std::int64_t>(value);
             return true;
         }},

        // Part 2.11 Play Mode Extended
        {Config::MakeEnumSettingUVE(IdUVE(Id::kPlayWindowModeUVE), 0,
                                    {{0, "Embedded Viewport"}, {1, "Maximised Viewport"}, {2, "Separate Window"}},
                                    "Play Window Mode", kPlayCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.playWindowMode; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.playWindowMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeVector2SettingUVE(IdUVE(Id::kPlayWindowSizeUVE), Config::SettingVector2UVE{1280.0, 720.0},
                                       64.0, 16384.0, "Standalone Play Window Size", kPlayCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.playWindowSize; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.playWindowSize = std::get<Config::SettingVector2UVE>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kPlaySceneSourceUVE), 0,
                                    {{0, "Current Scene"}, {1, "Project Main Scene"}, {2, "Custom Scene"}},
                                    "Scene to Run", kPlayCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.playSceneSource; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.playSceneSource = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kPlayPauseOnErrorUVE), true, "Pause on Script Error",
                                    kPlayCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.playPauseOnError; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.playPauseOnError = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kPlayLiveReloadUVE), true, "Live Reload Scripts & Scenes",
                                    kPlayCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.playLiveReload; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.playLiveReload = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kPlayTimeScaleUVE), 1.0, 0.05, 4.0, "Default Play Time Scale",
                                     kPlayCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.playTimeScale; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.playTimeScale = std::get<double>(value);
             return true;
         }},

        // Part 2.12 Autosave, Recovery & Performance
        {Config::MakeBoolSettingUVE(IdUVE(Id::kAutosaveEnabledUVE), true, "Enable Autosave", "Editor/Autosave"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.autosaveEnabled; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.autosaveEnabled = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kAutosaveIntervalSecondsUVE), 300.0, 15.0, 3600.0,
                                     "Autosave Interval (s)", "Editor/Autosave"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.autosaveIntervalSeconds;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.autosaveIntervalSeconds = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kCrashRecoveryEnabledUVE), true, "Keep Crash Recovery Snapshots",
                                    "Editor/Autosave"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.crashRecoveryEnabled;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.crashRecoveryEnabled = std::get<bool>(value);
             return true;
         }},
        {Config::MakeIntSettingUVE(IdUVE(Id::kBackupFileCountUVE), 5, 0, 32, "Backup File Count",
                                   "Editor/Autosave"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.backupFileCount; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.backupFileCount = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeIntSettingUVE(IdUVE(Id::kUndoHistoryDepthUVE), 128, 16, 1024, "Undo History Depth",
                                   "Editor/Autosave"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.undoHistoryDepth; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.undoHistoryDepth = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kLowProcessorModeUVE), false, "Low-Processor Mode When Idle",
                                    "Editor/Autosave"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.lowProcessorMode; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.lowProcessorMode = std::get<bool>(value);
             return true;
         }},
        {Config::MakeIntSettingUVE(IdUVE(Id::kBackgroundWorkerThreadsUVE), 4, 1, 64,
                                   "Background Worker Threads", "Editor/Autosave"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.backgroundWorkerThreads;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.backgroundWorkerThreads = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeIntSettingUVE(IdUVE(Id::kImportCacheLimitMbUVE), 2048, 128, 32768,
                                   "Import Cache Size Limit (MB)", "Editor/Autosave"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.importCacheLimitMb; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.importCacheLimitMb = std::get<std::int64_t>(value);
             return true;
         }},

        // Part 2.13 Version Control & External Tools
        {Config::MakeBoolSettingUVE(IdUVE(Id::kVcsEnabledUVE), true, "Enable Version Control Integration",
                                    "Editor/Version Control"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.vcsEnabled; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.vcsEnabled = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kVcsProviderUVE), 1,
                                    {{0, "None"}, {1, "Git"}, {2, "Perforce"}, {3, "Plastic SCM"}},
                                    "VCS Provider", "Editor/Version Control"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.vcsProvider; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.vcsProvider = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSettingUVE(IdUVE(Id::kVcsShowStatusBadgesUVE), true, "Show File Status Badges",
                                    "Editor/Version Control"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.vcsShowStatusBadges;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.vcsShowStatusBadges = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFilePathSettingUVE(IdUVE(Id::kVcsDiffToolPathUVE), "", 512U, "External Diff Tool Path",
                                        "Editor/Version Control"),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_extendedPreferences.vcsDiffToolPath; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.vcsDiffToolPath = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeFilePathSettingUVE(IdUVE(Id::kExternalImageEditorPathUVE), "", 512U,
                                        "External Image Editor Path", "Editor/Version Control"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.externalImageEditorPath;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.externalImageEditorPath = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeFilePathSettingUVE(IdUVE(Id::kExternal3dToolPathUVE), "", 512U,
                                        "External 3D Tool Path", "Editor/Version Control"),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_extendedPreferences.external3dToolPath;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_extendedPreferences.external3dToolPath = std::get<std::string>(value);
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
    if (id.starts_with("editor.interface.")) {
        ApplyEditorVisualThemeUVE(&m_extendedPreferences);
    }
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
    if (const auto* vector2 = std::get_if<Config::SettingVector2UVE>(&value)) {
        char text[64];
        std::snprintf(text, sizeof(text), "(%.6g, %.6g)", vector2->x, vector2->y);
        return text;
    }
    if (const auto* vector = std::get_if<Config::SettingVector3UVE>(&value)) {
        char text[96];
        std::snprintf(text, sizeof(text), "(%.6g, %.6g, %.6g)", vector->x, vector->y, vector->z);
        return text;
    }
    if (const auto* vector4 = std::get_if<Config::SettingVector4UVE>(&value)) {
        char text[128];
        std::snprintf(text, sizeof(text), "(%.6g, %.6g, %.6g, %.6g)", vector4->x, vector4->y, vector4->z, vector4->w);
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

#if defined(UVE_EDITOR_SETTINGS_STANDALONE_UVE)
// Standalone CPU-only suites link without ImGui, so there is no visual theme to apply.
// The full editor build uses the real implementation in editor_theme_uve.cpp.
void ApplyEditorVisualThemeUVE(const EditorExtendedPreferencesUVE* const) noexcept {}

void EditorUVE::SetColorPickerPreferencesUVE(ColorPickerPreferencesUVE preferences) {
    m_colorPickerPreferences = std::move(preferences);
}

std::optional<Config::SettingValueUVE> EditorUVE::GetShortcutSettingUVE(const std::string_view) const {
    return std::nullopt;
}

bool EditorUVE::SetShortcutSettingUVE(const std::string_view, const Config::SettingValueUVE&) {
    return false;
}
#endif

} // namespace UVE::Editor
