// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

namespace UVE::Editor {

/// When the hierarchy draws a row's visibility eye.
enum class HierarchyVisibilityColumnUVE {
    Always,
    /// While the row is hovered, and always for a hidden node, so a hidden node never looks shown.
    OnHover,
    Hidden,
};

/// What a double-click on a hierarchy row does. F2 renames whichever is chosen.
enum class HierarchyDoubleClickUVE {
    Rename,
    FocusInViewport,
    ExpandCollapse,
};

/// The lines that join a row to its parent.
enum class HierarchyTreeLinesUVE {
    None,
    /// Down to the last child, with a stub to each child.
    ToEachChild,
    /// Down the whole open branch. Cheaper on very large trees.
    FullHeight,
};

/// How the hierarchy panel looks and responds. Every field is an editor preference
/// (editor_settings_uve.cpp); apart from showTypeName, which came with node types, the defaults
/// are how the panel behaved before they existed.
struct HierarchyViewSettingsUVE final {
    bool revealSelection = true;
    bool showIcons = true;
    bool showTypeName = true;
    HierarchyVisibilityColumnUVE visibilityColumn = HierarchyVisibilityColumnUVE::Always;
    HierarchyDoubleClickUVE doubleClick = HierarchyDoubleClickUVE::Rename;
    bool dragToReparent = true;
    HierarchyTreeLinesUVE treeLines = HierarchyTreeLinesUVE::None;
    /// Pixels each level is indented by.
    float indentWidth = 21.0F;
};

inline constexpr float kMinimumHierarchyIndentUVE = 12.0F;
inline constexpr float kMaximumHierarchyIndentUVE = 40.0F;

/// Whether a row's eye is drawn this frame. The column itself is kept whenever the mode is not
/// Hidden, so names do not shift as the pointer moves over rows.
[[nodiscard]] bool ShouldDrawHierarchyEyeUVE(HierarchyVisibilityColumnUVE mode, bool rowHovered,
                                             bool nodeVisible) noexcept;

/// The type shown after a row's name, or empty when it would only repeat the name (a node still
/// called by its type's name).
[[nodiscard]] std::string_view GetHierarchyTypeHintUVE(std::string_view name, std::string_view type) noexcept;

} // namespace UVE::Editor
