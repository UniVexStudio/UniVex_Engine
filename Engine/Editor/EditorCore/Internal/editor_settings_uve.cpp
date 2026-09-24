// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_settings_uve.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <memory>
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
using Config::SettingValueUVE;

constexpr const char* kSessionCategoryUVE = "Editor/Session";
constexpr const char* kSnappingCategoryUVE = "Editor/Viewport/Snapping";
constexpr const char* kGridCategoryUVE = "Editor/Viewport/Grid";
constexpr const char* kOutlineCategoryUVE = "Editor/Viewport/Selection Outline";
constexpr const char* kNodesCategoryUVE = "Editor/Nodes";
constexpr const char* kPlayCategoryUVE = "Editor/Play Mode";

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

} // namespace

const std::vector<EditorSettingBindingUVE>& EditorUVE::GetSettingBindingsUVE() {
    namespace Id = EditorSettingIdUVE;
    using Workspace = EditorWorkspaceUVE;
    using RightTab = EditorRightPanelTabUVE;
    using BottomDock = EditorBottomDockUVE;
    const EditorTransformSnappingSettingsUVE snapping{};
    const ViewportOverlayStateUVE overlay{};
    const ColorPickerPreferencesUVE picker{};

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
              EntryUVE(RightTab::Signals, "Signals")},
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
              EntryUVE(BottomDock::AIToolbar, "AI Toolbar"), EntryUVE(BottomDock::FileSystem, "File System")},
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

        // Snapping. The steps share their bounds with SetTransformSnappingSettingsUVE, so a value
        // legal here is one that setter accepts; it is assigned directly because loading happens
        // before authoring commands are allowed.
        {Config::MakeBoolSettingUVE(IdUVE(Id::kSnapEnabledUVE), snapping.enabled, "Snap", kSnappingCategoryUVE,
                                    "Snap moves, rotations and scales to the steps below."),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_transformSnappingSettings.enabled; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_transformSnappingSettings.enabled = std::get<bool>(value);
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
             return editor.SetViewportGridUVE(std::get<bool>(value), editor.m_viewportOverlayState.gridOpacity);
         }},
        {WithStepUVE(Config::MakeFloatSettingUVE(IdUVE(Id::kGridOpacityUVE), overlay.gridOpacity,
                                                 kMinimumViewportGridOpacityUVE, 1.0, "Opacity", kGridCategoryUVE,
                                                 "How strongly the grid is drawn."),
                     0.05),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<double>(editor.m_viewportOverlayState.gridOpacity);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             return editor.SetViewportGridUVE(editor.m_viewportOverlayState.gridVisible, FloatUVE(value));
         }},
        {Config::MakeFloatSettingUVE(IdUVE(Id::kGridCellSizeUVE), overlay.gridCellSize,
                                     kMinimumViewportGridCellSizeUVE, kMaximumViewportGridCellSizeUVE, "Cell Size",
                                     kGridCategoryUVE,
                                     "The smallest grid square, in metres. Zooming out still steps up in tens."),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<double>(editor.m_viewportOverlayState.gridCellSize);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             return editor.SetViewportGridCellSizeUVE(FloatUVE(value));
         }},

        // Selection outline.
        {Config::MakeBoolSettingUVE(IdUVE(Id::kSelectionOutlineVisibleUVE), overlay.selectionOutlineVisible,
                                    "Show Outline", kOutlineCategoryUVE),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return editor.m_viewportOverlayState.selectionOutlineVisible;
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             const ViewportOverlayStateUVE& state = editor.m_viewportOverlayState;
             return editor.SetViewportSelectionOutlineUVE(std::get<bool>(value), state.selectionOutlineColor,
                                                         state.selectionOutlineThickness);
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
             const ViewportOverlayStateUVE& state = editor.m_viewportOverlayState;
             const auto& color = std::get<SettingColorUVE>(value);
             return editor.SetViewportSelectionOutlineUVE(state.selectionOutlineVisible,
                                                         ViewportAxisColorUVE{color.r, color.g, color.b},
                                                         state.selectionOutlineThickness);
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
             const ViewportOverlayStateUVE& state = editor.m_viewportOverlayState;
             return editor.SetViewportSelectionOutlineUVE(state.selectionOutlineVisible, state.selectionOutlineColor,
                                                         FloatUVE(value));
         }},

        // New nodes.
        {Config::MakeBoolSettingUVE(IdUVE(Id::kNewNodesUnderSelectionUVE), true, "Add Under Selection",
                                    kNodesCategoryUVE,
                                    "A new node goes under the selected node. Off, it always goes under the scene root."),
         [](const EditorUVE& editor) -> SettingValueUVE { return editor.m_newNodesUnderSelection; },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_newNodesUnderSelection = std::get<bool>(value);
             return true;
         }},
        {Config::MakeEnumSettingUVE(IdUVE(Id::kNewNodePlacementUVE),
                                    static_cast<std::int64_t>(EditorNewNodePlacementUVE::ParentOrigin),
                                    {EntryUVE(EditorNewNodePlacementUVE::ParentOrigin, "Parent's Origin"),
                                     EntryUVE(EditorNewNodePlacementUVE::ViewFocus, "View Focus")},
                                    "Placement", kNodesCategoryUVE,
                                    "Where a new 3D node appears: at its parent's origin, or at the point the "
                                    "viewport camera orbits - where you are looking."),
         [](const EditorUVE& editor) -> SettingValueUVE {
             return static_cast<std::int64_t>(editor.m_newNodePlacement);
         },
         [](EditorUVE& editor, const SettingValueUVE& value) {
             editor.m_newNodePlacement = static_cast<EditorNewNodePlacementUVE>(std::get<std::int64_t>(value));
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
    if (const EditorSettingBindingUVE* binding = FindSettingBindingUVE(id)) {
        return Config::IsSettingValueValidUVE(binding->descriptor, value) && binding->set(*this, value);
    }
    const Config::SettingDescriptorUVE* descriptor = m_settingsRegistry.FindUVE(id);
    return descriptor != nullptr && Config::IsSettingValueValidUVE(*descriptor, value) &&
           SetShortcutSettingUVE(id, value);
}

namespace {

[[nodiscard]] bool ContainsIgnoringCaseUVE(const std::string_view text, const std::string_view word) {
    const auto lower = [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return std::search(text.begin(), text.end(), word.begin(), word.end(),
                       [&lower](const char a, const char b) { return lower(a) == lower(b); }) != text.end();
}

struct CategoryTreeNodeUVE final {
    std::string path;
    std::vector<std::unique_ptr<CategoryTreeNodeUVE>> children;
};

void FlattenCategoryTreeUVE(const CategoryTreeNodeUVE& node, const int depth,
                            std::vector<SettingCategoryNodeUVE>& out) {
    for (const auto& child : node.children) {
        const std::size_t slash = child->path.rfind('/');
        out.push_back(SettingCategoryNodeUVE{
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

std::vector<SettingCategoryNodeUVE> BuildSettingCategoryTreeUVE(
    const std::vector<const Config::SettingDescriptorUVE*>& descriptors) {
    CategoryTreeNodeUVE root;
    for (const Config::SettingDescriptorUVE* descriptor : descriptors) {
        const std::string& category = descriptor->category;
        if (category.empty()) {
            continue;
        }
        // Walk down "Editor", "Editor/Viewport", "Editor/Viewport/Grid", adding what is missing.
        CategoryTreeNodeUVE* node = &root;
        for (std::size_t slash = category.find('/');; slash = category.find('/', slash + 1U)) {
            std::string path = category.substr(0U, slash);
            const auto found = std::find_if(node->children.begin(), node->children.end(),
                                            [&path](const auto& child) { return child->path == path; });
            node = found != node->children.end()
                       ? found->get()
                       : node->children.emplace_back(std::make_unique<CategoryTreeNodeUVE>()).get();
            node->path = std::move(path);
            if (slash == std::string::npos) {
                break;
            }
        }
    }
    std::vector<SettingCategoryNodeUVE> flattened;
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
    const auto* text = std::get_if<std::string>(&value);
    return text != nullptr ? *text : std::string{};
}

bool RegisterEditorSettingsUVE(Config::SettingsRegistryUVE& registry) {
    bool allRegistered = true;
    for (const EditorSettingBindingUVE& binding : EditorUVE::GetSettingBindingsUVE()) {
        allRegistered = registry.RegisterUVE(binding.descriptor) && allRegistered;
    }
    return allRegistered;
}

} // namespace UVE::Editor
