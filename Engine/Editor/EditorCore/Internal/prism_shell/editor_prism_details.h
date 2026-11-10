// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"
#include "editor_prism_icons.h"

#include "prism/Widget.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace prism {
class Label;
class PropertyGrid;
class TextInput;
class Widget;
} // namespace prism

namespace UVE::Editor {

/// The Details panel (#17-#22) drawn with PRISM widgets. Everything it shows and edits is the
/// engine's: the selected entity's components and their declared properties come from the scene
/// component metadata registry (the same declarations the previous Inspector drew), in the same
/// section order, and every edit goes through the editor's own write paths:
///
///   * a property: SetSelectedComponentPropertyUVE, or Preview... while a drag is held and
///     CommitComponentPropertyPreviewForUVE when it ends (one undo step per gesture);
///   * the transform: BeginTransformGestureUVE / PreviewTransformGestureValueUVE /
///     CommitTransformGestureUVE, exactly like the transform drawer;
///   * the name: SetSelectedEntityNameUVE.
class EditorPrismDetailsUVE final {
public:
    explicit EditorPrismDetailsUVE(EditorUVE& editor);
    ~EditorPrismDetailsUVE();

    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    /// Rebuilds the rows when the selection, its components or the search change; otherwise
    /// refreshes the shown values (never the one being edited).
    void RefreshUVE();

private:
    using EntryUVE = Core::TypeMetadataEntryUVE;
    using PropertyUVE = Core::TypeMetadataPropertyUVE;

    struct RowUVE final {
        const EntryUVE* entry = nullptr;           // null for the transform rows
        std::function<void(const void* instance)> sync;
        prism::Widget* editor = nullptr;
        bool editing = false;                      // a gesture is held: do not overwrite its value
    };

    void RebuildUVE(Scene::EntityUVE entity);
    void AddTransformUVE(int category, Scene::EntityUVE entity);
    void AddPropertyUVE(int category, const EntryUVE& entry, const PropertyUVE& property, const void* instance,
                        bool writable);
    [[nodiscard]] std::uint64_t StructureSignatureUVE(Scene::EntityUVE entity) const;
    [[nodiscard]] bool MatchesFilterUVE(const std::string& text) const;
    [[nodiscard]] bool IsBeingEditedUVE(const RowUVE& row) const;

    EditorUVE& m_editor;
    EditorPrismIconsUVE m_icons;
    std::shared_ptr<prism::TextInput> m_search;
    std::shared_ptr<prism::Label> m_title;
    std::shared_ptr<prism::Label> m_subtitle;
    std::shared_ptr<prism::PropertyGrid> m_grid;
    std::vector<std::shared_ptr<RowUVE>> m_rows;
    Scene::EntityUVE m_entity = Scene::kInvalidEntityUVE;
    std::uint64_t m_signature = 0;
    std::string m_filter;
};

} // namespace UVE::Editor
