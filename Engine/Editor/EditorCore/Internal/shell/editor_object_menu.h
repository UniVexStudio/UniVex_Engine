// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "entity.h"

#include "prism/Core.h"

#include <functional>

namespace prism {
class TreeView;
struct TreeNode;
} // namespace prism

namespace UVE::Editor {

class EditorUVE;

/// The right-click menu of an object row, the same in the level's Outliner and the Entity
/// Editor's Components (each in its own document):
///
///   <name>                       (and why it cannot be changed, when it cannot)
///   Add Child...  Rename (F2)
///   Cut  Copy  Paste  Paste as Child  Duplicate  Delete
///   Focus  Show / Hide  Lock / Unlock
///   Expand / Collapse Branch  Select Children  Select All Inside
///   Move Up / Down / to Top / to Bottom
///   Copy Name
///   Open Entity  Show in Content Browser     (a placed entity)
///   Save as Entity                           (anything else that can be copied)
class EditorObjectMenuUVE final {
public:
    struct HooksUVE final {
        /// The tree the row is in (Rename, Expand / Collapse).
        prism::TreeView* tree = nullptr;
        /// The tree row of an entity, for selecting children.
        std::function<prism::TreeNode*(Scene::EntityUVE)> nodeOf;
        /// Opens the tree's "+ Add" picker for a child of the selection.
        std::function<void()> addChild;
    };

    /// Shows the menu for `entity` (already the selection: the tree selects the row it was opened
    /// on) at `at` in the current window.
    static void ShowUVE(EditorUVE& editor, Scene::EntityUVE entity, prism::TreeNode* node, prism::Vec2 at, const HooksUVE& hooks);
};

} // namespace UVE::Editor
