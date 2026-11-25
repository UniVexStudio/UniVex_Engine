// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

namespace UVE::Editor {

/// When the hierarchy draws a row's visibility eye.
enum class HierarchyVisibilityColumn {
    Always,
    /// While the row is hovered, and always for a hidden object, so a hidden object never looks shown.
    OnHover,
    Hidden,
};

/// What a double-click on a hierarchy row does. F2 renames whichever is chosen.
enum class HierarchyDoubleClick {
    Rename,
    FocusInViewport,
    ExpandCollapse,
};

/// The lines that join a row to its parent.
enum class HierarchyTreeLines {
    None,
    /// Down to the last child, with a stub to each child.
    ToEachChild,
    /// Down the whole open branch. Cheaper on very large trees.
    FullHeight,
};

/// How the hierarchy panel looks and responds. Every field is an editor preference
/// (editor_settings_uve.cpp); apart from showTypeName, which came with object types, the defaults
/// are how the panel behaved before they existed.
struct HierarchyViewSettings final {
    bool revealSelection = true;
    bool showIcons = true;
    bool showTypeName = true;
    HierarchyVisibilityColumn visibilityColumn = HierarchyVisibilityColumn::Always;
    HierarchyDoubleClick doubleClick = HierarchyDoubleClick::Rename;
    bool dragToReparent = true;
    HierarchyTreeLines treeLines = HierarchyTreeLines::ToEachChild;
    /// Pixels each level is indented by.
    float indentWidth = 12.0F;
};

inline constexpr float kMinimumHierarchyIndent = 12.0F;
inline constexpr float kMaximumHierarchyIndent = 40.0F;

} // namespace UVE::Editor
