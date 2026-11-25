// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_settings.h"

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

#include "editor_settings_binding.h"
#include "editor_color.h"
#include "editor.h"

namespace UVE::Editor {
namespace {

using Config::SettingColor;
using Config::SettingDescriptor;
using Config::SettingEnumEntry;
using Config::SettingStringList;
using Config::SettingValue;

constexpr const char* kSessionCategory = "Editor/Session";
constexpr const char* kSnappingCategory = "Editor/Viewport/Snapping";
constexpr const char* kGridCategory = "Editor/Viewport/Grid";
constexpr const char* kOutlineCategory = "Editor/Viewport/Selection Outline";
constexpr const char* kObjectsCategory = "Editor/Objects";
constexpr const char* kPlayCategory = "Editor/Play Mode";
constexpr const char* kHierarchyCategory = "Editor/Hierarchy";
// Legacy color-list loading scanned up to 64 stored entries before skipping invalid hex strings
// and applying the smaller saved/recent caps to the successfully parsed colors.
constexpr std::size_t kMaxStoredColorPreferenceEntries = 64U;

[[nodiscard]] SettingDescriptor Hidden(SettingDescriptor descriptor) {
    descriptor.flags |= Config::kSettingFlagHidden;
    return descriptor;
}

[[nodiscard]] SettingDescriptor WithStep(SettingDescriptor descriptor, const double step) {
    descriptor.step = step;
    return descriptor;
}

[[nodiscard]] std::string SettingKey(const std::string_view id) {
    return std::string(id);
}

template <typename Enum>
[[nodiscard]] SettingEnumEntry Entry(const Enum value, std::string label) {
    return SettingEnumEntry{static_cast<std::int64_t>(value), std::move(label)};
}

[[nodiscard]] float Float(const SettingValue& value) {
    return static_cast<float>(std::get<double>(value));
}

[[nodiscard]] SettingStringList EncodeColorList(const std::vector<EditorColor>& colors) {
    SettingStringList encoded;
    encoded.reserve(colors.size());
    for (const EditorColor& color : colors) {
        encoded.push_back(FormatColorHex(color, true));
    }
    return encoded;
}

[[nodiscard]] std::vector<EditorColor> DecodeColorList(const SettingStringList& encoded) {
    std::vector<EditorColor> colors;
    colors.reserve(encoded.size());
    for (const std::string& value : encoded) {
        if (const std::optional<EditorColor> color = ParseColorHex(value)) {
            colors.push_back(*color);
        }
    }
    return colors;
}

[[nodiscard]] SettingStringList EncodeInspectorFolds(const std::map<std::string, bool>& folds) {
    SettingStringList encoded;
    encoded.reserve(folds.size());
    for (const auto& [key, open] : folds) {
        encoded.push_back(std::string{open ? "1:" : "0:"} + key);
    }
    return encoded;
}

[[nodiscard]] std::map<std::string, bool> DecodeInspectorFolds(const SettingStringList& encoded) {
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

const std::vector<EditorSettingBinding>& Editor::GetSettingBindings() {
    namespace Id = EditorSettingId;
    using Workspace = EditorWorkspace;
    using RightTab = EditorRightPanelTab;
    using BottomDock = EditorBottomDock;
    using ViewMode = Editor::AssetTableViewMode;
    const EditorTransformSnappingSettings snapping{};
    const ViewportOverlayState overlay{};
    const ColorPickerPreferences picker{};
    const HierarchyViewSettings hierarchy{};

    static const std::vector<EditorSettingBinding> bindings = {
        // Session state the editor remembers for itself.
        {Hidden(Config::MakeBoolSetting(SettingKey(Id::kScenePanelVisible), true, "Scene Panel",
                                              kSessionCategory)),
         [](const Editor& editor) -> SettingValue { return editor.m_scenePanelVisible; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_scenePanelVisible = std::get<bool>(value);
             return true;
         }},
        {Hidden(Config::MakeBoolSetting(SettingKey(Id::kInspectorPanelVisible), true, "Inspector Panel",
                                              kSessionCategory)),
         [](const Editor& editor) -> SettingValue { return editor.m_inspectorPanelVisible; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_inspectorPanelVisible = std::get<bool>(value);
             return true;
         }},
        {Hidden(Config::MakeBoolSetting(SettingKey(Id::kBottomDockVisible), true, "Bottom Dock",
                                              kSessionCategory)),
         [](const Editor& editor) -> SettingValue { return editor.m_bottomDockVisible; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_bottomDockVisible = std::get<bool>(value);
             return true;
         }},
        {Hidden(Config::MakeFloatSetting(SettingKey(Id::kBottomDockHeight), 192.0, 96.0, 4096.0,
                                               "Bottom Dock Height", kSessionCategory)),
         [](const Editor& editor) -> SettingValue { return static_cast<double>(editor.m_bottomDockHeight); },
         [](Editor& editor, const SettingValue& value) {
             editor.m_bottomDockHeight = Float(value);
             return true;
         }},
        {Hidden(Config::MakeFloatSetting(SettingKey(Id::kScenePanelWidth), 240.0, 96.0, 4096.0,
                                               "Outliner Width", kSessionCategory)),
         [](const Editor& editor) -> SettingValue { return static_cast<double>(editor.m_scenePanelWidth); },
         [](Editor& editor, const SettingValue& value) {
             editor.m_scenePanelWidth = Float(value);
             return true;
         }},
        {Hidden(Config::MakeFloatSetting(SettingKey(Id::kInspectorPanelWidth), 320.0, 96.0, 4096.0,
                                               "Inspector Width", kSessionCategory)),
         [](const Editor& editor) -> SettingValue { return static_cast<double>(editor.m_inspectorPanelWidth); },
         [](Editor& editor, const SettingValue& value) {
             editor.m_inspectorPanelWidth = Float(value);
             return true;
         }},
        {Hidden(Config::MakeEnumSetting(
             SettingKey(Id::kAssetTableViewMode), static_cast<std::int64_t>(ViewMode::SmallTiles),
             {Entry(ViewMode::SmallTiles, "Small Tiles"), Entry(ViewMode::LargeTiles, "Large Tiles"),
              Entry(ViewMode::List, "List")},
             "Asset Table View", kSessionCategory)),
         [](const Editor& editor) -> SettingValue {
             return static_cast<std::int64_t>(editor.m_assetTableViewMode);
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_assetTableViewMode = static_cast<ViewMode>(std::get<std::int64_t>(value));
             return true;
         }},
        {Hidden(Config::MakeEnumSetting(
             SettingKey(Id::kAssetTableMode), static_cast<std::int64_t>(AssetTableMode::Tiles),
             {Entry(AssetTableMode::Tiles, "Tiles"), Entry(AssetTableMode::Columns, "Columns"),
              Entry(AssetTableMode::Details, "Details"), Entry(AssetTableMode::Recent, "Recent"),
              Entry(AssetTableMode::Board, "Board")},
             "Asset Table Mode", kSessionCategory)),
         [](const Editor& editor) -> SettingValue {
             return static_cast<std::int64_t>(editor.m_assetTableMode);
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_assetTableMode = static_cast<AssetTableMode>(std::get<std::int64_t>(value));
             return true;
         }},
        // Game is not among the entries, so a session is never restored into it: saving while in
        // Game is refused and leaves the last restorable workspace stored.
        {Hidden(Config::MakeEnumSetting(SettingKey(Id::kActiveWorkspace),
                                              static_cast<std::int64_t>(Workspace::Library),
                                              {Entry(Workspace::Library, "Library"),
                                               Entry(Workspace::Asset, "Asset"),
                                               Entry(Workspace::Scripting, "Scripting"),
                                               Entry(Workspace::Debug, "Debug"),
                                               Entry(Workspace::Plugin, "Plugin")},
                                              "Workspace", kSessionCategory)),
         [](const Editor& editor) -> SettingValue { return static_cast<std::int64_t>(editor.m_activeWorkspace); },
         [](Editor& editor, const SettingValue& value) {
             editor.m_activeWorkspace = static_cast<Workspace>(std::get<std::int64_t>(value));
             return true;
         }},
        {Hidden(Config::MakeEnumSetting(
             SettingKey(Id::kActiveRightPanelTab), static_cast<std::int64_t>(RightTab::Inspector),
             {Entry(RightTab::Inspector, "Inspector"), Entry(RightTab::Import, "Import"),
              Entry(RightTab::Events, "Events")},
             "Right Panel Tab", kSessionCategory)),
         [](const Editor& editor) -> SettingValue {
             return static_cast<std::int64_t>(editor.m_activeRightPanelTab);
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_activeRightPanelTab = static_cast<RightTab>(std::get<std::int64_t>(value));
             return true;
         }},
        {Hidden(Config::MakeEnumSetting(
             SettingKey(Id::kActiveBottomDock), static_cast<std::int64_t>(BottomDock::FileSystem),
             {Entry(BottomDock::Debugger, "Debugger"), Entry(BottomDock::Animator, "Animator"),
              Entry(BottomDock::AIToolbar, "AI Toolbar"), Entry(BottomDock::FileSystem, "File System"),
              Entry(BottomDock::Console, "Console"), Entry(BottomDock::Capture, "Capture"),
              Entry(BottomDock::Profiler, "Profiler"), Entry(BottomDock::Audio, "Audio")},
             "Bottom Dock Panel", kSessionCategory)),
         [](const Editor& editor) -> SettingValue { return static_cast<std::int64_t>(editor.m_activeBottomDock); },
         [](Editor& editor, const SettingValue& value) {
             editor.m_activeBottomDock = static_cast<BottomDock>(std::get<std::int64_t>(value));
             return true;
         }},
        {Hidden(Config::MakeBoolSetting(SettingKey(Id::kColorPickerAdvancedOpen), picker.advancedOpen,
                                              "Colour Picker Advanced", kSessionCategory)),
         [](const Editor& editor) -> SettingValue { return editor.m_colorPickerPreferences.advancedOpen; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_colorPickerPreferences.advancedOpen = std::get<bool>(value);
             return true;
         }},
        {Hidden(Config::MakeStringListSetting(SettingKey(Id::kColorPickerSaved), {},
                                                    kMaxStoredColorPreferenceEntries, 0U,
                                                    "Saved Colours", kSessionCategory)),
         [](const Editor& editor) -> SettingValue {
             return EncodeColorList(editor.GetColorPickerPreferences().saved);
         },
         [](Editor& editor, const SettingValue& value) {
             ColorPickerPreferences preferences = editor.GetColorPickerPreferences();
             preferences.saved = DecodeColorList(std::get<SettingStringList>(value));
             editor.SetColorPickerPreferences(std::move(preferences));
             return true;
         }},
        {Hidden(Config::MakeStringListSetting(SettingKey(Id::kColorPickerRecent), {},
                                                    kMaxStoredColorPreferenceEntries, 0U,
                                                    "Recent Colours", kSessionCategory)),
         [](const Editor& editor) -> SettingValue {
             return EncodeColorList(editor.GetColorPickerPreferences().recents);
         },
         [](Editor& editor, const SettingValue& value) {
             ColorPickerPreferences preferences = editor.GetColorPickerPreferences();
             preferences.recents = DecodeColorList(std::get<SettingStringList>(value));
             editor.SetColorPickerPreferences(std::move(preferences));
             return true;
         }},
        {Hidden(Config::MakeStringListSetting(SettingKey(Id::kInspectorFolds), {},
                                                    Editor::kMaxRememberedInspectorFolds, 0U,
                                                    "Inspector Folds", kSessionCategory)),
         [](const Editor& editor) -> SettingValue { return EncodeInspectorFolds(editor.m_inspectorFoldOpen); },
         [](Editor& editor, const SettingValue& value) {
             editor.m_inspectorFoldOpen = DecodeInspectorFolds(std::get<SettingStringList>(value));
             return true;
         }},
        {Hidden(Config::MakeStringListSetting(SettingKey(Id::kFavoriteProjects), {},
                                                    Editor::kMaxPersistedFavoriteProjects, 0U,
                                                    "Favorite Projects", kSessionCategory)),
         [](const Editor& editor) -> SettingValue {
             const std::size_t count = std::min(editor.m_favoriteProjectPaths.size(),
                                                Editor::kMaxPersistedFavoriteProjects);
             SettingStringList paths;
             paths.reserve(count);
             for (std::size_t index = 0U; index < count; ++index) {
                 paths.push_back(editor.m_favoriteProjectPaths[index].generic_string());
             }
             return paths;
         },
         [](Editor& editor, const SettingValue& value) {
             const SettingStringList& paths = std::get<SettingStringList>(value);
             editor.m_favoriteProjectPaths.clear();
             editor.m_favoriteProjectPaths.reserve(paths.size());
             for (const std::string& path : paths) {
                 if (!path.empty()) {
                     editor.m_favoriteProjectPaths.emplace_back(path);
                 }
             }
             return true;
         }},

        // Snapping. The steps share their bounds with SetTransformSnappingSettings, so a value
        // legal here is one that setter accepts; it is assigned directly because loading happens
        // before authoring commands are allowed.
        {Config::MakeBoolSetting(SettingKey(Id::kSnapEnabled), snapping.enabled, "Snap", kSnappingCategory,
                                    "Snap moves and rotations to the steps below."),
         [](const Editor& editor) -> SettingValue { return editor.m_transformSnappingSettings.enabled; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_transformSnappingSettings.enabled = std::get<bool>(value);
             editor.m_viewportOverlayState.snapEnabled = editor.m_transformSnappingSettings.enabled;
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kSnapTranslateStep), snapping.translateStep,
                                     kMinimumTransformSnapStep, kMaximumTransformSnapTranslateStep, "Move Step",
                                     kSnappingCategory, "Distance a snapped move steps by, in metres."),
         [](const Editor& editor) -> SettingValue {
             return static_cast<double>(editor.m_transformSnappingSettings.translateStep);
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_transformSnappingSettings.translateStep = Float(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kSnapRotateStepDegrees), snapping.rotateStepDegrees,
                                     kMinimumTransformSnapStep, kMaximumTransformSnapRotateStepDegrees,
                                     "Rotate Step", kSnappingCategory,
                                     "Angle a snapped rotation steps by, in degrees."),
         [](const Editor& editor) -> SettingValue {
             return static_cast<double>(editor.m_transformSnappingSettings.rotateStepDegrees);
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_transformSnappingSettings.rotateStepDegrees = Float(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kSnapScaleEnabled), snapping.scaleEnabled, "Snap Scale", kSnappingCategory,
                                    "Snap scales to the Scale Step (its own magnet in the view toolbar)."),
         [](const Editor& editor) -> SettingValue { return editor.m_transformSnappingSettings.scaleEnabled; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_transformSnappingSettings.scaleEnabled = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kSnapScaleStep), snapping.scaleStep, kMinimumTransformSnapStep,
                                     kMaximumTransformSnapScaleStep, "Scale Step", kSnappingCategory,
                                     "Amount a snapped scale steps by."),
         [](const Editor& editor) -> SettingValue {
             return static_cast<double>(editor.m_transformSnappingSettings.scaleStep);
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_transformSnappingSettings.scaleStep = Float(value);
             return true;
         }},

        // Grid.
        {Config::MakeBoolSetting(SettingKey(Id::kGridVisible), overlay.gridVisible, "Show Grid", kGridCategory),
         [](const Editor& editor) -> SettingValue { return editor.m_viewportOverlayState.gridVisible; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_viewportOverlayState.gridVisible = std::get<bool>(value);
             return true;
         }},
        {WithStep(Config::MakeFloatSetting(SettingKey(Id::kGridOpacity), overlay.gridOpacity,
                                                 kMinimumViewportGridOpacity, 1.0, "Opacity", kGridCategory,
                                                 "How strongly the grid is drawn."),
                     0.05),
         [](const Editor& editor) -> SettingValue {
             return static_cast<double>(editor.m_viewportOverlayState.gridOpacity);
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_viewportOverlayState.gridOpacity = Float(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kGridCellSize), overlay.gridCellSize,
                                     kMinimumViewportGridCellSize, kMaximumViewportGridCellSize, "Cell Size",
                                     kGridCategory,
                                     "The smallest grid square, in metres. Zooming out still steps up in tens."),
         [](const Editor& editor) -> SettingValue {
             return static_cast<double>(editor.m_viewportOverlayState.gridCellSize);
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_viewportOverlayState.gridCellSize = Float(value);
             return true;
         }},

        // Selection outline.
        {Config::MakeBoolSetting(SettingKey(Id::kSelectionOutlineVisible), overlay.selectionOutlineVisible,
                                    "Show Outline", kOutlineCategory),
         [](const Editor& editor) -> SettingValue {
             return editor.m_viewportOverlayState.selectionOutlineVisible;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_viewportOverlayState.selectionOutlineVisible = std::get<bool>(value);
             return true;
         }},
        {Config::MakeColorSetting(SettingKey(Id::kSelectionOutlineColor),
                                     SettingColor{overlay.selectionOutlineColor.r, overlay.selectionOutlineColor.g,
                                                     overlay.selectionOutlineColor.b},
                                     false, "Colour", kOutlineCategory),
         [](const Editor& editor) -> SettingValue {
             const ViewportAxisColor& color = editor.m_viewportOverlayState.selectionOutlineColor;
             return SettingColor{color.r, color.g, color.b};
         },
         [](Editor& editor, const SettingValue& value) {
             const auto& color = std::get<SettingColor>(value);
             editor.m_viewportOverlayState.selectionOutlineColor = ViewportAxisColor{color.r, color.g, color.b};
             return true;
         }},
        {WithStep(Config::MakeFloatSetting(SettingKey(Id::kSelectionOutlineThickness),
                                                 overlay.selectionOutlineThickness,
                                                 kMinimumSelectionOutlineThickness,
                                                 kMaximumSelectionOutlineThickness, "Thickness",
                                                 kOutlineCategory, "Outline width, in pixels."),
                     1.0),
         [](const Editor& editor) -> SettingValue {
             return static_cast<double>(editor.m_viewportOverlayState.selectionOutlineThickness);
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_viewportOverlayState.selectionOutlineThickness = Float(value);
             return true;
         }},

        // New objects.
        {Config::MakeBoolSetting(SettingKey(Id::kNewObjectsUnderSelection), true, "Add Under Selection",
                                    kObjectsCategory,
                                    "A new object goes under the selected object. Off, it always goes under the scene root."),
         [](const Editor& editor) -> SettingValue { return editor.m_newObjectsUnderSelection; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_newObjectsUnderSelection = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kNewObjectPlacement),
                                    static_cast<std::int64_t>(EditorNewObjectPlacement::ParentOrigin),
                                    {Entry(EditorNewObjectPlacement::ParentOrigin, "Parent's Origin"),
                                     Entry(EditorNewObjectPlacement::ViewFocus, "View Focus")},
                                    "Placement", kObjectsCategory,
                                    "Where a new 3D object appears: at its parent's origin, or at the point the "
                                    "viewport camera orbits - where you are looking."),
         [](const Editor& editor) -> SettingValue {
             return static_cast<std::int64_t>(editor.m_newObjectPlacement);
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_newObjectPlacement = static_cast<EditorNewObjectPlacement>(std::get<std::int64_t>(value));
             return true;
         }},

        // Play mode.
        {Config::MakeBoolSetting(SettingKey(Id::kPlayPauseOnStart), false, "Pause on Start", kPlayCategory,
                                    "Enter Play paused, on the first frame, to step from there."),
         [](const Editor& editor) -> SettingValue { return editor.m_playPauseOnStart; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_playPauseOnStart = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kPlaySaveSceneFirst), false, "Save Scene First", kPlayCategory,
                                    "Save the scene, if it has unsaved changes and a file, before Play starts."),
         [](const Editor& editor) -> SettingValue { return editor.m_playSaveSceneFirst; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_playSaveSceneFirst = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kPlaySwitchToGame), true, "Switch to Game Tab", kPlayCategory,
                                    "Show the Game tab while playing, and the tab you were on after. Off, Play "
                                    "runs in the tab you are on."),
         [](const Editor& editor) -> SettingValue { return editor.m_playSwitchToGame; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_playSwitchToGame = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kPlayTintEnabled), true, "Tint While Playing", kPlayCategory,
                                    "Tint the editor's panels while playing, so an edit made in Play - and lost "
                                    "when it stops - is never mistaken for a real one."),
         [](const Editor& editor) -> SettingValue { return editor.m_playTintEnabled; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_playTintEnabled = std::get<bool>(value);
             return true;
         }},
        {Config::MakeColorSetting(SettingKey(Id::kPlayTintColor),
                                     SettingColor{kDefaultPlayTintColor.r, kDefaultPlayTintColor.g,
                                                     kDefaultPlayTintColor.b},
                                     false, "Tint Colour", kPlayCategory),
         [](const Editor& editor) -> SettingValue {
             return SettingColor{editor.m_playTintColor.r, editor.m_playTintColor.g, editor.m_playTintColor.b};
         },
         [](Editor& editor, const SettingValue& value) {
             const auto& color = std::get<SettingColor>(value);
             editor.m_playTintColor = ViewportAxisColor{color.r, color.g, color.b};
             return true;
         }},
        {WithStep(Config::MakeFloatSetting(SettingKey(Id::kPlayTintStrength), kDefaultPlayTintStrength,
                                                 0.05, 0.6, "Tint Strength",
                                                 kPlayCategory, "How far the panels move toward the tint colour."),
                     0.05),
         [](const Editor& editor) -> SettingValue { return static_cast<double>(editor.m_playTintStrength); },
         [](Editor& editor, const SettingValue& value) {
             editor.m_playTintStrength = Float(value);
             return true;
         }},

        // Hierarchy panel.
        {Config::MakeBoolSetting(SettingKey(Id::kHierarchyRevealSelection), hierarchy.revealSelection,
                                    "Reveal Selection", kHierarchyCategory,
                                    "When the selection changes, open the rows above it and scroll it into view."),
         [](const Editor& editor) -> SettingValue { return editor.m_hierarchyView.revealSelection; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_hierarchyView.revealSelection = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kHierarchyShowIcons), hierarchy.showIcons, "Object Icons",
                                    kHierarchyCategory, "Draw each object's type icon before its name."),
         [](const Editor& editor) -> SettingValue { return editor.m_hierarchyView.showIcons; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_hierarchyView.showIcons = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kHierarchyShowTypeName), hierarchy.showTypeName, "Object Type Names",
                                    kHierarchyCategory,
                                    "Write each object's type after its name, dimmed, when the name is not already the "
                                    "type and there is room."),
         [](const Editor& editor) -> SettingValue { return editor.m_hierarchyView.showTypeName; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_hierarchyView.showTypeName = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kHierarchyVisibilityColumn),
                                    static_cast<std::int64_t>(hierarchy.visibilityColumn),
                                    {Entry(HierarchyVisibilityColumn::Always, "Always"),
                                     Entry(HierarchyVisibilityColumn::OnHover, "On Hover"),
                                     Entry(HierarchyVisibilityColumn::Hidden, "Hidden")},
                                    "Visibility Toggles", kHierarchyCategory,
                                    "When a row shows its eye. On Hover still shows it on every hidden object, so a "
                                    "hidden object never looks shown."),
         [](const Editor& editor) -> SettingValue {
             return static_cast<std::int64_t>(editor.m_hierarchyView.visibilityColumn);
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_hierarchyView.visibilityColumn =
                 static_cast<HierarchyVisibilityColumn>(std::get<std::int64_t>(value));
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kHierarchyDoubleClick), static_cast<std::int64_t>(hierarchy.doubleClick),
                                    {Entry(HierarchyDoubleClick::Rename, "Rename"),
                                     Entry(HierarchyDoubleClick::FocusInViewport, "Focus in Viewport"),
                                     Entry(HierarchyDoubleClick::ExpandCollapse, "Expand or Collapse")},
                                    "Double-Click", kHierarchyCategory,
                                    "What a double-click on a row does, unless the row opens something: an instance of an entity "
                                    "asset opens that asset, and the level's Viewport opens its scene. F2 always renames."),
         [](const Editor& editor) -> SettingValue {
             return static_cast<std::int64_t>(editor.m_hierarchyView.doubleClick);
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_hierarchyView.doubleClick = static_cast<HierarchyDoubleClick>(std::get<std::int64_t>(value));
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kHierarchyDragToReparent), hierarchy.dragToReparent,
                                    "Drag to Reparent", kHierarchyCategory,
                                    "Drag a row onto another to move it under that object. Off, rows stay put when "
                                    "dragged."),
         [](const Editor& editor) -> SettingValue { return editor.m_hierarchyView.dragToReparent; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_hierarchyView.dragToReparent = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kHierarchyTreeLines), static_cast<std::int64_t>(hierarchy.treeLines),
                                    {Entry(HierarchyTreeLines::None, "None"),
                                     Entry(HierarchyTreeLines::ToEachChild, "To Each Child"),
                                     Entry(HierarchyTreeLines::FullHeight, "Full Height")},
                                    "Tree Lines", kHierarchyCategory,
                                    "Lines joining each row to its parent. Full Height is cheaper on very large "
                                    "scenes."),
         [](const Editor& editor) -> SettingValue {
             return static_cast<std::int64_t>(editor.m_hierarchyView.treeLines);
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_hierarchyView.treeLines = static_cast<HierarchyTreeLines>(std::get<std::int64_t>(value));
             return true;
         }},
        {WithStep(Config::MakeFloatSetting(SettingKey(Id::kHierarchyIndentWidth), hierarchy.indentWidth,
                                                 kMinimumHierarchyIndent, kMaximumHierarchyIndent,
                                                 "Indent Width", kHierarchyCategory,
                                                 "Pixels each level of the tree is indented by."),
                     1.0),
         [](const Editor& editor) -> SettingValue {
             return static_cast<double>(editor.m_hierarchyView.indentWidth);
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_hierarchyView.indentWidth = Float(value);
             return true;
         }},

        // Part 2.1 Interface & Theme
        {Config::MakeEnumSetting(SettingKey(Id::kThemePreset), 0,
                                    {{0, "Dark"}, {1, "Light"}, {2, "High Contrast"}, {3, "Custom"}},
                                    "Theme Preset", "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.themePreset; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.themePreset = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFilePathSetting(SettingKey(Id::kCustomThemeFile), "", 512U, "Custom Theme File",
                                        "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.customThemeFile; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.customThemeFile = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeColorSetting(SettingKey(Id::kThemeBaseColor),
                                     SettingColor{0.14F, 0.15F, 0.18F, 1.0F}, false, "Base Colour",
                                     "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.themeBaseColor; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.themeBaseColor = std::get<SettingColor>(value);
             return true;
         }},
        {Config::MakeColorSetting(SettingKey(Id::kThemeAccentColor),
                                     SettingColor{0.22F, 0.56F, 0.96F, 1.0F}, false, "Accent Colour",
                                     "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.themeAccentColor; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.themeAccentColor = std::get<SettingColor>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kThemeContrast), 1.0, 0.5, 2.0, "Theme Contrast",
                                     "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.themeContrast; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.themeContrast = std::get<double>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kUiScaleMode), 0, {{0, "Auto from DPI"}, {1, "Manual"}},
                                    "UI Scale Mode", "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.uiScaleMode; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.uiScaleMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kUiScaleFactor), 1.0, 0.5, 3.0, "UI Scale Factor",
                                     "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.uiScaleFactor; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.uiScaleFactor = std::get<double>(value);
             return true;
         }},
        {Config::MakeIntSetting(SettingKey(Id::kFontSizePx), 14, 10, 32, "Font Size (px)", "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.fontSizePx; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.fontSizePx = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kFontHintingMode), 2,
                                    {{0, "None"}, {1, "Slight"}, {2, "Normal"}, {3, "Subpixel"}},
                                    "Font Hinting", "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.fontHintingMode; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.fontHintingMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeIntSetting(SettingKey(Id::kFontOversampling), 2, 1, 4, "Font Oversampling",
                                   "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.fontOversampling; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.fontOversampling = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kIconSaturation), 1.0, 0.0, 1.0, "Icon Saturation",
                                     "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.iconSaturation; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.iconSaturation = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kCornerRadiusPx), 4.0, 0.0, 12.0, "Corner Radius (px)",
                                     "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.cornerRadiusPx; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.cornerRadiusPx = std::get<double>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kSpacingDensity), 1, {{0, "Compact"}, {1, "Comfortable"}},
                                    "Spacing Density", "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.spacingDensity; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.spacingDensity = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kUiAnimationSpeed), 1.0, 0.0, 4.0, "UI Animation Speed",
                                     "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.uiAnimationSpeed; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.uiAnimationSpeed = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kReduceMotion), false, "Reduce Motion", "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.reduceMotion; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.reduceMotion = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kTooltipDelaySeconds), 0.35, 0.0, 5.0, "Tooltip Delay (s)",
                                     "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.tooltipDelaySeconds; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.tooltipDelaySeconds = std::get<double>(value);
             return true;
         }},
        {Config::MakeStringSetting(SettingKey(Id::kEditorLanguage), "en", 32U, "Editor Language",
                                      "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.editorLanguage; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.editorLanguage = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kTouchFriendlyTargets), false, "Touch-Friendly Hit Targets",
                                    "Editor/Interface"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.touchFriendlyTargets; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.touchFriendlyTargets = std::get<bool>(value);
             return true;
         }},

        // Part 2.2 Layout, Docking & Workspaces
        {Config::MakeBoolSetting(SettingKey(Id::kPersistDockLayout), true, "Persist Dock Layout",
                                    "Editor/Layout"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.persistDockLayout; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.persistDockLayout = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kLeftSplitRatio), 0.2, 0.1, 0.5, "Left Dock Split Ratio",
                                     "Editor/Layout"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.leftSplitRatio; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.leftSplitRatio = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kRightSplitRatio), 0.25, 0.1, 0.5, "Right Dock Split Ratio",
                                     "Editor/Layout"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.rightSplitRatio; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.rightSplitRatio = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kAllowFloatingWindows), true, "Allow Floating Windows",
                                    "Editor/Layout"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.allowFloatingWindows; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.allowFloatingWindows = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kTabPosition), 0, {{0, "Top"}, {1, "Bottom"}},
                                    "Tab Bar Position", "Editor/Layout"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.tabPosition; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.tabPosition = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kDistractionFreeMode), false, "Distraction-Free Mode",
                                    "Editor/Layout"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.distractionFreeMode; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.distractionFreeMode = std::get<bool>(value);
             return true;
         }},

        // Part 2.3 Hierarchy Extended
        {Config::MakeIntSetting(SettingKey(Id::kHierarchyExpandDepthLimit), 16, 1, 64, "Expand Depth Limit",
                                   kHierarchyCategory),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.hierarchyExpandDepthLimit;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.hierarchyExpandDepthLimit = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kHierarchyPersistExpansion), true, "Persist Expansion State",
                                    kHierarchyCategory),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.hierarchyPersistExpansion;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.hierarchyPersistExpansion = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kHierarchyRowHeightPx), 22.0, 18.0, 40.0, "Row Height (px)",
                                     kHierarchyCategory),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.hierarchyRowHeightPx; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.hierarchyRowHeightPx = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kHierarchyColorCodeIcons), true, "Colour-Code Icons by Type",
                                    kHierarchyCategory),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.hierarchyColorCodeIcons;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.hierarchyColorCodeIcons = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kHierarchyShowLockColumn), true, "Show Lock Column",
                                    kHierarchyCategory),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.hierarchyShowLockColumn;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.hierarchyShowLockColumn = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kHierarchyFilterMode), 2,
                                    {{0, "Name Only"}, {1, "Name + Type"}, {2, "Name + Type + Component"}},
                                    "Filter Matching Mode", kHierarchyCategory),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.hierarchyFilterMode; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.hierarchyFilterMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kHierarchySortMode), 0,
                                    {{0, "Scene Order"}, {1, "Alphabetical"}, {2, "By Object Type"}},
                                    "Sort Mode", kHierarchyCategory),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.hierarchySortMode; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.hierarchySortMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kHierarchyTintByTagColor), true, "Tint Rows by Tag Colour",
                                    kHierarchyCategory),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.hierarchyTintByTagColor;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.hierarchyTintByTagColor = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kHierarchyRenameMode), 0, {{0, "Inline"}, {1, "Dialog"}},
                                    "Rename Mode", kHierarchyCategory),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.hierarchyRenameMode; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.hierarchyRenameMode = std::get<std::int64_t>(value);
             return true;
         }},

        // Part 2.4 Context Menu
        {Config::MakeStringSetting(SettingKey(Id::kContextDuplicateSuffix), "_{n}", 32U,
                                      "Duplicate Name Suffix", "Editor/Context Menu"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.contextDuplicateSuffix;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.contextDuplicateSuffix = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kContextReparentKeepGlobal), true,
                                    "Reparent Keeps Global Transform", "Editor/Context Menu"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.contextReparentKeepGlobal;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.contextReparentKeepGlobal = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kContextSelectOnOpen), true, "Select Object on Context Menu",
                                    "Editor/Context Menu"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.contextSelectOnOpen; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.contextSelectOnOpen = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kContextTouchHoldSeconds), 0.5, 0.2, 2.0,
                                     "Touch Long-Press Duration (s)", "Editor/Context Menu"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.contextTouchHoldSeconds;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.contextTouchHoldSeconds = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kContextConfirmDeleteSubtree), true,
                                    "Confirm Delete Subtree", "Editor/Context Menu"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.contextConfirmDeleteSubtree;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.contextConfirmDeleteSubtree = std::get<bool>(value);
             return true;
         }},

        // Part 2.5 Commands & Menu Bar
        {Config::MakeEnumSetting(SettingKey(Id::kCommandPaletteFuzzyMode), 0,
                                    {{0, "Smart Fuzzy"}, {1, "Prefix Only"}, {2, "Substring"}},
                                    "Command Palette Matching", "Editor/Commands"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.commandPaletteFuzzyMode;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.commandPaletteFuzzyMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeIntSetting(SettingKey(Id::kCommandPaletteRecentDepth), 8, 0, 32,
                                   "Recent Command History Depth", "Editor/Commands"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.commandPaletteRecentDepth;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.commandPaletteRecentDepth = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeStringSetting(SettingKey(Id::kShortcutProfile), "Default", 64U, "Shortcut Profile",
                                      "Editor/Commands"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.shortcutProfile; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.shortcutProfile = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeIntSetting(SettingKey(Id::kRecentFilesLimit), 15, 1, 50, "Recent Files Limit",
                                   "Editor/Commands"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.recentFilesLimit; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.recentFilesLimit = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeIntSetting(SettingKey(Id::kToolbarButtonSizePx), 24, 16, 48, "Toolbar Button Size (px)",
                                   "Editor/Commands"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.toolbarButtonSizePx;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.toolbarButtonSizePx = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kAutoSavePreferences), true, "Auto-Save Preferences on Change",
                                    "Editor/Commands"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.autoSavePreferences; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.autoSavePreferences = std::get<bool>(value);
             return true;
         }},

        // Part 2.6 Viewport Extended
        {Config::MakeIntSetting(SettingKey(Id::kGridSubdivisions), 10, 1, 16, "Subdivisions", kGridCategory),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.gridSubdivisions; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.gridSubdivisions = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kGridFadeDistance), 200.0, 10.0, 1000.0, "Fade Distance (m)",
                                     kGridCategory),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.gridFadeDistance; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.gridFadeDistance = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kGridFollowsCamera), true, "Grid Follows Camera",
                                    kGridCategory),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.gridFollowsCamera; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.gridFollowsCamera = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kGizmoSizePx), 96.0, 40.0, 200.0, "Gizmo Size (px)",
                                     "Editor/Viewport/Gizmo"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.gizmoSizePx; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.gizmoSizePx = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kGizmoOpacity), 0.95, 0.2, 1.0, "Gizmo Opacity",
                                     "Editor/Viewport/Gizmo"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.gizmoOpacity; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.gizmoOpacity = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kRememberGizmoMode), true, "Remember Active Transform Mode",
                                    "Editor/Viewport/Gizmo"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.rememberGizmoMode; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.rememberGizmoMode = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kDefaultTransformSpace), 0, {{0, "Local"}, {1, "World"}},
                                    "Default Transform Space", "Editor/Viewport/Gizmo"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.defaultTransformSpace;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.defaultTransformSpace = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kPivotMode), 0,
                                    {{0, "Median Point"}, {1, "Active Object"}, {2, "Individual Origins"}},
                                    "Transform Pivot Mode", "Editor/Viewport/Gizmo"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.pivotMode; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.pivotMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kNavigationScheme), 0,
                                    {{0, "UniVex Default"}, {1, "Maya (Alt)"}, {2, "Blender (Middle Mouse)"}, {3, "Right-Click Fly"}},
                                    "Navigation Scheme", "Editor/Viewport/Navigation"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.navigationScheme; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.navigationScheme = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kOrbitSensitivity), 1.0, 0.1, 5.0, "Orbit Sensitivity",
                                     "Editor/Viewport/Navigation"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.orbitSensitivity; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.orbitSensitivity = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kPanSensitivity), 1.0, 0.1, 5.0, "Pan Sensitivity",
                                     "Editor/Viewport/Navigation"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.panSensitivity; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.panSensitivity = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kZoomSensitivity), 1.0, 0.1, 5.0, "Zoom Sensitivity",
                                     "Editor/Viewport/Navigation"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.zoomSensitivity; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.zoomSensitivity = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kCameraInertiaSmoothing), 0.15, 0.0, 1.0,
                                     "Camera Inertia Smoothing", "Editor/Viewport/Navigation"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.cameraInertiaSmoothing;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.cameraInertiaSmoothing = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kFreelookBaseSpeed), 8.0, 0.5, 100.0,
                                     "Freelook Base Speed (m/s)", "Editor/Viewport/Navigation"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.freelookBaseSpeed; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.freelookBaseSpeed = std::get<double>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kDefaultCameraFov), 60.0, 20.0, 120.0,
                                     "Default Camera FOV (deg)", "Editor/Viewport"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.defaultCameraFovDegrees;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.defaultCameraFovDegrees = std::get<double>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kBoxSelectMode), 0, {{0, "Touch / Intersect"}, {1, "Enclose"}},
                                    "Box Select Mode", "Editor/Viewport"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.boxSelectMode; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.boxSelectMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kDefaultViewMode), 0,
                                    {{0, "Lit"}, {1, "Unshaded"}, {2, "Wireframe"}, {3, "Overdraw"}, {4, "Normals"}},
                                    "Default View Mode", "Editor/Viewport"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.defaultViewMode; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.defaultViewMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kViewportRenderScale), 1.0, 0.25, 2.0,
                                     "Viewport Render Scale", "Editor/Viewport"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.viewportRenderScale; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.viewportRenderScale = std::get<double>(value);
             return true;
         }},
        {Config::MakeIntSetting(SettingKey(Id::kViewportPaneCount), 1, 1, 4, "Viewport Pane Count",
                                   "Editor/Viewport"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.viewportPaneCount; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.viewportPaneCount = std::get<std::int64_t>(value);
             return true;
         }},

        // Part 2.7 Inspector Extended
        {Config::MakeFloatSetting(SettingKey(Id::kInspectorLabelRatio), 0.38, 0.2, 0.7, "Label Column Ratio",
                                     "Editor/Inspector"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.inspectorLabelRatio; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.inspectorLabelRatio = std::get<double>(value);
             return true;
         }},
        {Config::MakeIntSetting(SettingKey(Id::kInspectorFloatPrecision), 3, 1, 8, "Float Display Precision",
                                   "Editor/Inspector"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.inspectorFloatPrecision;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.inspectorFloatPrecision = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kInspectorAngleUnit), 0, {{0, "Degrees"}, {1, "Radians"}},
                                    "Angle Display Unit", "Editor/Inspector"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.inspectorAngleUnit; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.inspectorAngleUnit = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kInspectorShowAdvanced), false, "Show Advanced Properties",
                                    "Editor/Inspector"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.inspectorShowAdvanced;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.inspectorShowAdvanced = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kInspectorColorPickerShape), 0,
                                    {{0, "Hue Wheel & Disc"}, {1, "Hue Bar & Rectangle"}, {2, "Value/Hue Circle"},
                                     {3, "Perceptual Lightness Circle"}, {4, "Sliders & Hex Only"}},
                                    "Default Colour Picker Shape", "Editor/Inspector"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.inspectorColorPickerShape;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.inspectorColorPickerShape = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kInspectorColorPickerMode), 0,
                                    {{0, "RGB (0..1)"}, {1, "RGB (0..255)"}, {2, "HSV (deg / %)"}, {3, "Perceptual OKLab"}},
                                    "Default Colour Picker Mode", "Editor/Inspector"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.inspectorColorPickerMode;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.inspectorColorPickerMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kInspectorOpenResourcesMode), 0,
                                    {{0, "Inline Sub-Inspector"}, {1, "New Panel"}},
                                    "Open Resources In", "Editor/Inspector"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.inspectorOpenResourcesMode;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.inspectorOpenResourcesMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kInspectorPlayRefreshHz), 15.0, 1.0, 60.0,
                                     "Play Mode Auto-Refresh (Hz)", "Editor/Inspector"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.inspectorPlayRefreshHz;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.inspectorPlayRefreshHz = std::get<double>(value);
             return true;
         }},

        // Part 2.8 Script & Shader Editor
        {Config::MakeIntSetting(SettingKey(Id::kScriptFontSizePx), 14, 8, 36, "Script Font Size (px)",
                                   "Editor/Script Editor"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.scriptFontSizePx; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.scriptFontSizePx = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kScriptLineNumbers), true, "Show Line Numbers",
                                    "Editor/Script Editor"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.scriptShowLineNumbers;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.scriptShowLineNumbers = std::get<bool>(value);
             return true;
         }},
        {Config::MakeIntSetting(SettingKey(Id::kScriptTabSize), 4, 1, 8, "Tab Size", "Editor/Script Editor"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.scriptTabSize; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.scriptTabSize = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kScriptWordWrapMode), 0,
                                    {{0, "Off"}, {1, "Viewport Width"}, {2, "Wrap Column"}},
                                    "Word Wrap Mode", "Editor/Script Editor"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.scriptWordWrapMode; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.scriptWordWrapMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeStringSetting(SettingKey(Id::kScriptSyntaxTheme), "UniVex Dark", 64U,
                                      "Syntax Highlighting Theme", "Editor/Script Editor"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.scriptSyntaxTheme; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.scriptSyntaxTheme = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kScriptCodeCompletion), true, "Code Completion",
                                    "Editor/Script Editor"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.scriptCodeCompletion;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.scriptCodeCompletion = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kScriptMinimapEnabled), true, "Show Minimap",
                                    "Editor/Script Editor"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.scriptMinimapEnabled;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.scriptMinimapEnabled = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kScriptFormatOnSave), false, "Format on Save",
                                    "Editor/Script Editor"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.scriptFormatOnSave; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.scriptFormatOnSave = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFilePathSetting(SettingKey(Id::kScriptExternalEditorPath), "", 512U,
                                        "External Script Editor Executable", "Editor/Script Editor"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.scriptExternalExecutable;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.scriptExternalExecutable = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kNodeGraphWireStyle), 0,
                                    {{0, "Curved"}, {1, "Straight"}, {2, "Orthogonal"}},
                                    "Visual Graph Connection Style", "Editor/Script Editor"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.nodeGraphWireStyle; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.nodeGraphWireStyle = std::get<std::int64_t>(value);
             return true;
         }},

        // Part 2.9 Asset Browser
        {Config::MakeIntSetting(SettingKey(Id::kAssetThumbnailSizePx), 96, 32, 256, "Thumbnail Size (px)",
                                   "Editor/Asset Browser"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.assetThumbnailSizePx;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.assetThumbnailSizePx = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kAssetSortMode), 0,
                                    {{0, "Name"}, {1, "Type"}, {2, "Modified Time"}, {3, "Size"}},
                                    "Default Sort Mode", "Editor/Asset Browser"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.assetSortMode; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.assetSortMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kAssetShowHiddenFiles), false, "Show Hidden & Internal Files",
                                    "Editor/Asset Browser"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.assetShowHiddenFiles;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.assetShowHiddenFiles = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kAssetSplitRatio), 0.25, 0.1, 0.6, "Tree / Contents Split Ratio",
                                     "Editor/Asset Browser"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.assetSplitRatio; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.assetSplitRatio = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kAssetAutoReimport), true, "Auto-Reimport on External Change",
                                    "Editor/Asset Browser"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.assetAutoReimport; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.assetAutoReimport = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kAssetDeleteToTrash), true, "Move Deleted Assets to Trash",
                                    "Editor/Asset Browser"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.assetDeleteToTrash; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.assetDeleteToTrash = std::get<bool>(value);
             return true;
         }},

        // Part 2.10 Console, Log & Debug
        {Config::MakeBoolSetting(SettingKey(Id::kConsoleCollapseDuplicates), true, "Collapse Duplicate Messages",
                                    "Editor/Console"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.consoleCollapseDuplicates;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.consoleCollapseDuplicates = std::get<bool>(value);
             return true;
         }},
        {Config::MakeIntSetting(SettingKey(Id::kConsoleMaxLines), 5000, 100, 100000, "Max Retained Log Lines",
                                   "Editor/Console"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.consoleMaxLines; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.consoleMaxLines = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kConsoleClearOnPlay), false, "Clear Console on Play",
                                    "Editor/Console"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.consoleClearOnPlay; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.consoleClearOnPlay = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kConsoleShowStackTraces), true, "Show Stack Traces",
                                    "Editor/Console"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.consoleShowStackTraces;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.consoleShowStackTraces = std::get<bool>(value);
             return true;
         }},
        {Config::MakeIntSetting(SettingKey(Id::kConsoleHistoryDepth), 64, 8, 512, "Command History Depth",
                                   "Editor/Console"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.consoleHistoryDepth;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.consoleHistoryDepth = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeIntSetting(SettingKey(Id::kConsoleRemotePort), 9229, 1024, 65535, "Remote Console Port",
                                   "Editor/Console"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.consoleRemotePort; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.consoleRemotePort = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kDebugPhysicsDraw), false, "Physics Debug Draw", "Editor/Debug"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.debugPhysicsDraw; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.debugPhysicsDraw = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kDebugNavigationDraw), false, "Navigation Debug Draw",
                                    "Editor/Debug"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.debugNavigationDraw;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.debugNavigationDraw = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kDebugRendererBounds), false, "Renderer Bounds & Volumes",
                                    "Editor/Debug"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.debugRendererBounds;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.debugRendererBounds = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kDebugAudioRadii), false, "Audio Emitter Radii", "Editor/Debug"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.debugAudioRadii; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.debugAudioRadii = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kDebugAssertionBehavior), 0,
                                    {{0, "Break into Debugger"}, {1, "Log and Continue"}, {2, "Abort"}},
                                    "Assertion Behaviour", "Editor/Debug"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.debugAssertionBehavior;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.debugAssertionBehavior = std::get<std::int64_t>(value);
             return true;
         }},

        // Part 2.11 Play Mode Extended
        {Config::MakeEnumSetting(SettingKey(Id::kPlayWindowMode), 0,
                                    {{0, "Embedded Viewport"}, {1, "Maximised Viewport"}, {2, "Separate Window"}},
                                    "Play Window Mode", kPlayCategory),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.playWindowMode; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.playWindowMode = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeVector2Setting(SettingKey(Id::kPlayWindowSize), Config::SettingVector2{1280.0, 720.0},
                                       64.0, 16384.0, "Standalone Play Window Size", kPlayCategory),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.playWindowSize; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.playWindowSize = std::get<Config::SettingVector2>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kPlaySceneSource), 0,
                                    {{0, "Current Scene"}, {1, "Project Main Scene"}, {2, "Custom Scene"}},
                                    "Scene to Run", kPlayCategory),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.playSceneSource; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.playSceneSource = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kPlayPauseOnError), true, "Pause on Script Error",
                                    kPlayCategory),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.playPauseOnError; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.playPauseOnError = std::get<bool>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kPlayLiveReload), true, "Live Reload Scripts & Scenes",
                                    kPlayCategory),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.playLiveReload; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.playLiveReload = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kPlayTimeScale), 1.0, 0.05, 4.0, "Default Play Time Scale",
                                     kPlayCategory),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.playTimeScale; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.playTimeScale = std::get<double>(value);
             return true;
         }},

        // Part 2.12 Autosave, Recovery & Performance
        {Config::MakeBoolSetting(SettingKey(Id::kAutosaveEnabled), true, "Enable Autosave", "Editor/Autosave"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.autosaveEnabled; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.autosaveEnabled = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFloatSetting(SettingKey(Id::kAutosaveIntervalSeconds), 300.0, 15.0, 3600.0,
                                     "Autosave Interval (s)", "Editor/Autosave"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.autosaveIntervalSeconds;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.autosaveIntervalSeconds = std::get<double>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kCrashRecoveryEnabled), true, "Keep Crash Recovery Snapshots",
                                    "Editor/Autosave"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.crashRecoveryEnabled;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.crashRecoveryEnabled = std::get<bool>(value);
             return true;
         }},
        {Config::MakeIntSetting(SettingKey(Id::kBackupFileCount), 5, 0, 32, "Backup File Count",
                                   "Editor/Autosave"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.backupFileCount; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.backupFileCount = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeIntSetting(SettingKey(Id::kUndoHistoryDepth), 128, 16, 1024, "Undo History Depth",
                                   "Editor/Autosave"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.undoHistoryDepth; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.undoHistoryDepth = std::get<std::int64_t>(value);
             editor.m_historyCapacity = static_cast<std::size_t>(
                 std::clamp<std::int64_t>(editor.m_extendedPreferences.undoHistoryDepth, 16, 1024));
             while (editor.m_undoHistory.size() > editor.m_historyCapacity) {
                 editor.m_undoHistory.pop_front();
             }
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kLowProcessorMode), false, "Low-Processor Mode When Idle",
                                    "Editor/Autosave"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.lowProcessorMode; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.lowProcessorMode = std::get<bool>(value);
             return true;
         }},
        {Config::MakeIntSetting(SettingKey(Id::kBackgroundWorkerThreads), 4, 1, 64,
                                   "Background Worker Threads", "Editor/Autosave"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.backgroundWorkerThreads;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.backgroundWorkerThreads = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeIntSetting(SettingKey(Id::kImportCacheLimitMb), 2048, 128, 32768,
                                   "Import Cache Size Limit (MB)", "Editor/Autosave"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.importCacheLimitMb; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.importCacheLimitMb = std::get<std::int64_t>(value);
             return true;
         }},

        // Part 2.13 Version Control & External Tools
        {Config::MakeBoolSetting(SettingKey(Id::kVcsEnabled), true, "Enable Version Control Integration",
                                    "Editor/Version Control"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.vcsEnabled; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.vcsEnabled = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSetting(SettingKey(Id::kVcsProvider), 1,
                                    {{0, "None"}, {1, "Git"}, {2, "Perforce"}, {3, "Plastic SCM"}},
                                    "VCS Provider", "Editor/Version Control"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.vcsProvider; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.vcsProvider = std::get<std::int64_t>(value);
             return true;
         }},
        {Config::MakeBoolSetting(SettingKey(Id::kVcsShowStatusBadges), true, "Show File Status Badges",
                                    "Editor/Version Control"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.vcsShowStatusBadges;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.vcsShowStatusBadges = std::get<bool>(value);
             return true;
         }},
        {Config::MakeFilePathSetting(SettingKey(Id::kVcsDiffToolPath), "", 512U, "External Diff Tool Path",
                                        "Editor/Version Control"),
         [](const Editor& editor) -> SettingValue { return editor.m_extendedPreferences.vcsDiffToolPath; },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.vcsDiffToolPath = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeFilePathSetting(SettingKey(Id::kExternalImageEditorPath), "", 512U,
                                        "External Image Editor Path", "Editor/Version Control"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.externalImageEditorPath;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.externalImageEditorPath = std::get<std::string>(value);
             return true;
         }},
        {Config::MakeFilePathSetting(SettingKey(Id::kExternal3dToolPath), "", 512U,
                                        "External 3D Tool Path", "Editor/Version Control"),
         [](const Editor& editor) -> SettingValue {
             return editor.m_extendedPreferences.external3dToolPath;
         },
         [](Editor& editor, const SettingValue& value) {
             editor.m_extendedPreferences.external3dToolPath = std::get<std::string>(value);
             return true;
         }},
    };
    return bindings;
}

const EditorSettingBinding* Editor::FindSettingBinding(const std::string_view id) {
    for (const EditorSettingBinding& binding : GetSettingBindings()) {
        if (binding.descriptor.id == id) {
            return &binding;
        }
    }
    return nullptr;
}

std::optional<Config::SettingValue> Editor::GetEditorSetting(const std::string_view id) const {
    const EditorSettingBinding* binding = FindSettingBinding(id);
    return binding != nullptr ? std::optional<SettingValue>(binding->get(*this)) : GetShortcutSetting(id);
}

bool Editor::SetEditorSetting(const std::string_view id, const Config::SettingValue& value) {
    const Config::SettingDescriptor* descriptor = m_settingsRegistry.Find(id);
    if (descriptor == nullptr || descriptor->HasFlag(Config::kSettingFlagDeprecated) ||
        !Config::IsSettingValueValid(*descriptor, value)) {
        return false;
    }

    const std::optional<SettingValue> previousValue = GetEditorSetting(id);
    if (!previousValue) {
        return false;
    }

    const EditorSettingBinding* binding = FindSettingBinding(id);
    const bool applied = binding != nullptr ? binding->set(*this, value) : SetShortcutSetting(id, value);
    if (!applied) {
        return false;
    }

    if (const std::optional<SettingValue> newValue = GetEditorSetting(id);
        newValue && *previousValue != *newValue) {
        NotifyEditorSettingChanged(id, *previousValue, *newValue);
    }
    return true;
}

Config::SettingsObserverSubscription Editor::SubscribeToSetting(
    const std::string_view id, Config::SettingsObserverCallback callback) {
    return m_settingsObservers.SubscribeToSetting(id, std::move(callback));
}

Config::SettingsObserverSubscription Editor::SubscribeToCategory(
    const std::string_view categoryPrefix, Config::SettingsObserverCallback callback) {
    return m_settingsObservers.SubscribeToCategory(categoryPrefix, std::move(callback));
}

bool Editor::Unsubscribe(const Config::SettingsObserverSubscription subscription) {
    return m_settingsObservers.Unsubscribe(subscription);
}

namespace {

[[nodiscard]] bool ContainsIgnoringCase(const std::string_view text, const std::string_view word) {
    const auto lower = [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return std::search(text.begin(), text.end(), word.begin(), word.end(),
                       [&lower](const char a, const char b) { return lower(a) == lower(b); }) != text.end();
}

struct CategoryTreeObject final {
    std::string path;
    std::vector<std::unique_ptr<CategoryTreeObject>> children;
};

void FlattenCategoryTree(const CategoryTreeObject& object, const int depth,
                            std::vector<SettingCategoryObject>& out) {
    for (const auto& child : object.children) {
        const std::size_t slash = child->path.rfind('/');
        out.push_back(SettingCategoryObject{
            child->path, slash == std::string::npos ? child->path : child->path.substr(slash + 1U), depth});
        FlattenCategoryTree(*child, depth + 1, out);
    }
}

} // namespace

bool MatchesSettingSearch(const Config::SettingDescriptor& descriptor, const std::string_view query) {
    std::size_t start = 0U;
    while (start < query.size()) {
        std::size_t end = query.find(' ', start);
        end = end == std::string_view::npos ? query.size() : end;
        const std::string_view word = query.substr(start, end - start);
        if (!word.empty() && !ContainsIgnoringCase(descriptor.displayName, word) &&
            !ContainsIgnoringCase(descriptor.category, word) && !ContainsIgnoringCase(descriptor.id, word) &&
            !ContainsIgnoringCase(descriptor.tooltip, word)) {
            return false;
        }
        start = end + 1U;
    }
    return true;
}

bool IsInSettingCategory(const std::string_view category, const std::string_view path) noexcept {
    return category.starts_with(path) && (category.size() == path.size() || category[path.size()] == '/');
}

std::vector<SettingCategoryObject> BuildSettingCategoryTree(
    const std::vector<const Config::SettingDescriptor*>& descriptors) {
    CategoryTreeObject root;
    for (const Config::SettingDescriptor* descriptor : descriptors) {
        const std::string& category = descriptor->category;
        if (category.empty()) {
            continue;
        }
        // Walk down "Editor", "Editor/Viewport", "Editor/Viewport/Grid", adding what is missing.
        CategoryTreeObject* object = &root;
        for (std::size_t slash = category.find('/');; slash = category.find('/', slash + 1U)) {
            std::string path = category.substr(0U, slash);
            const auto found = std::find_if(object->children.begin(), object->children.end(),
                                            [&path](const auto& child) { return child->path == path; });
            object = found != object->children.end()
                       ? found->get()
                       : object->children.emplace_back(std::make_unique<CategoryTreeObject>()).get();
            object->path = std::move(path);
            if (slash == std::string::npos) {
                break;
            }
        }
    }
    std::vector<SettingCategoryObject> flattened;
    FlattenCategoryTree(root, 0, flattened);
    return flattened;
}

std::string FormatSettingValue(const Config::SettingDescriptor& descriptor,
                                  const Config::SettingValue& value) {
    if (const auto* flag = std::get_if<bool>(&value)) {
        return *flag ? "On" : "Off";
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        for (const Config::SettingEnumEntry& entry : descriptor.enumEntries) {
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
    if (const auto* color = std::get_if<Config::SettingColor>(&value)) {
        return FormatColorHex(EditorColor{color->r, color->g, color->b, color->a}, descriptor.colorHasAlpha);
    }
    if (const auto* vector2 = std::get_if<Config::SettingVector2>(&value)) {
        char text[64];
        std::snprintf(text, sizeof(text), "(%.6g, %.6g)", vector2->x, vector2->y);
        return text;
    }
    if (const auto* vector = std::get_if<Config::SettingVector3>(&value)) {
        char text[96];
        std::snprintf(text, sizeof(text), "(%.6g, %.6g, %.6g)", vector->x, vector->y, vector->z);
        return text;
    }
    if (const auto* vector4 = std::get_if<Config::SettingVector4>(&value)) {
        char text[128];
        std::snprintf(text, sizeof(text), "(%.6g, %.6g, %.6g, %.6g)", vector4->x, vector4->y, vector4->z, vector4->w);
        return text;
    }
    if (const auto* list = std::get_if<Config::SettingStringList>(&value)) {
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
[[nodiscard]] Config::SettingDescriptor MakeRenamedSettingAlias(const Config::SettingDescriptor& current,
                                                                     const RenamedSettingId& renamed) {
    Config::SettingDescriptor alias = current;
    alias.id = std::string(renamed.oldId);
    alias.flags |= Config::kSettingFlagHidden | Config::kSettingFlagDeprecated;
    alias.replacementId = std::string(renamed.newId);
    return alias;
}

} // namespace

bool RegisterEditorSettings(Config::SettingsRegistry& registry) {
    const std::vector<EditorSettingBinding>& bindings = Editor::GetSettingBindings();
    bool allRegistered = true;
    for (const EditorSettingBinding& binding : bindings) {
        allRegistered = registry.Register(binding.descriptor) && allRegistered;
    }
    for (const RenamedSettingId& renamed : kRenamedSettingIds) {
        const auto binding = std::find_if(bindings.cbegin(), bindings.cend(), [&renamed](const EditorSettingBinding& candidate) {
            return candidate.descriptor.id == renamed.newId;
        });
        // A rename that names a setting nobody declares is a programming error, not a runtime
        // condition: fail registration so the editor's settings test catches it.
        allRegistered = binding != bindings.cend() &&
                        registry.Register(MakeRenamedSettingAlias(binding->descriptor, renamed)) && allRegistered;
    }

    // The viewport module owns these defaults, so EditorCore registers only the persisted values.
    // Load/save handles them as one palette: the host's default colours are never replaced by a
    // descriptor's placeholder default when no complete stored palette exists.
    allRegistered = registry.Register(Hidden(Config::MakeColorSetting(
                        SettingKey(EditorSettingId::kViewportAxisColorX), {}, false, "X Axis Colour",
                        kSessionCategory))) && allRegistered;
    allRegistered = registry.Register(Hidden(Config::MakeColorSetting(
                        SettingKey(EditorSettingId::kViewportAxisColorY), {}, false, "Y Axis Colour",
                        kSessionCategory))) && allRegistered;
    allRegistered = registry.Register(Hidden(Config::MakeColorSetting(
                        SettingKey(EditorSettingId::kViewportAxisColorZ), {}, false, "Z Axis Colour",
                        kSessionCategory))) && allRegistered;
    return allRegistered;
}

#if defined(UVE_EDITOR_SETTINGS_STANDALONE_UVE)

void Editor::SetColorPickerPreferences(ColorPickerPreferences preferences) {
    m_colorPickerPreferences = std::move(preferences);
}

std::optional<Config::SettingValue> Editor::GetShortcutSetting(const std::string_view) const {
    return std::nullopt;
}

bool Editor::SetShortcutSetting(const std::string_view, const Config::SettingValue&) {
    return false;
}
#endif

void Editor::NotifyEditorSettingChanged(const std::string_view id,
                                               const Config::SettingValue& previousValue,
                                               const Config::SettingValue& newValue) {
    m_settingsObservers.NotifyChanged(id, previousValue, newValue);
}

} // namespace UVE::Editor
