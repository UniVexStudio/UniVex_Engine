// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "uve/ui/prism/prism_button_uve.h"
#include "uve/ui/prism/prism_scroll_container_uve.h"
#include "uve/ui/prism/prism_text_input_uve.h"
#include "uve/ui/prism/prism_tree_view_uve.h"
#include "uve/ui/prism/prism_widget_uve.h"

namespace UVE::UI::Prism {

/// Plain data model describing an entity or folder in the scene hierarchy tree.
struct PrismHierarchyItemUVE final {
    std::uint64_t                      id = 0;
    std::string                        name;
    std::vector<PrismHierarchyItemUVE> children;
};

/// High-performance modern Scene Hierarchy (Outliner) Panel.
/// Displays an expandable tree of scene entities with depth indentation,
/// active entity selection, search filtering, and an Add-Object trigger.
class PrismHierarchyWidgetUVE : public PrismWidgetUVE {
public:
    explicit PrismHierarchyWidgetUVE(std::string debugName = "SceneHierarchy");

    /// Rebuilds or synchronizes the tree nodes from the scene entity hierarchy.
    void SyncTree(const std::vector<PrismHierarchyItemUVE>& items, std::uint64_t selectedEntityId);

    void SetOnEntitySelected(std::function<void(std::uint64_t)> callback) {
        m_onEntitySelected = std::move(callback);
    }

    void SetOnAddObjectRequested(std::function<void()> callback) {
        m_onAddObjectRequested = std::move(callback);
    }

    void SetOnFilterChanged(std::function<void(const std::string&)> callback) {
        m_onFilterChanged = std::move(callback);
    }

    [[nodiscard]] std::uint64_t GetSelectedEntityId() const noexcept { return m_selectedEntityId; }
    [[nodiscard]] const std::string& GetFilterText() const noexcept { return m_filterText; }

    // --- State overrides ---
    void ArrangeUVE(const Math::RectUVE& finalRect) override;

protected:
    Math::Vector2UVE OnMeasureContentUVE(Math::Vector2UVE availableSize) override;
    void OnPaintContentUVE(PrismDrawListUVE& drawList) override;

private:
    std::shared_ptr<PrismTreeNodeUVE> BuildTreeNode(
        const PrismHierarchyItemUVE& item,
        std::uint64_t selectedId,
        std::shared_ptr<PrismTreeNodeUVE>& outSelectedNode
    );

    std::uint64_t                                  m_selectedEntityId = 0;
    std::string                                    m_filterText;

    std::shared_ptr<PrismButtonUVE>                m_addButton;
    std::shared_ptr<PrismTextInputUVE>             m_searchInput;
    std::shared_ptr<PrismScrollContainerUVE>       m_scrollContainer;
    std::shared_ptr<PrismTreeViewUVE>              m_treeView;

    std::function<void(std::uint64_t)>             m_onEntitySelected;
    std::function<void()>                          m_onAddObjectRequested;
    std::function<void(const std::string&)>        m_onFilterChanged;
};

} // namespace UVE::UI::Prism
