// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_hierarchy_widget_uve.h"
#include <algorithm>

namespace UVE::UI::Prism {

namespace {
constexpr float kToolbarHeightUVE = 24.0F;
constexpr float kAddButtonWidthUVE = 24.0F;
} // namespace

PrismHierarchyWidgetUVE::PrismHierarchyWidgetUVE(std::string debugName)
    : PrismWidgetUVE(std::move(debugName)) {
    m_isFocusable = true;
    m_style.padding = PrismEdgesUVE{4.0F, 4.0F};

    // 1. Add Object Button
    m_addButton = std::make_shared<PrismButtonUVE>("+", [this]() {
        if (m_onAddObjectRequested) {
            m_onAddObjectRequested();
        }
    });
    m_addButton->GetStyle().width = PrismLengthUVE::Px(kAddButtonWidthUVE);
    m_addButton->GetStyle().height = PrismLengthUVE::Px(22.0F);
    AddChild(m_addButton);

    // 2. Search Filter Input
    m_searchInput = std::make_shared<PrismTextInputUVE>("Search Objects...");
    m_searchInput->SetOnTextChanged([this](const std::string& text) {
        m_filterText = text;
        if (m_onFilterChanged) {
            m_onFilterChanged(m_filterText);
        }
    });
    AddChild(m_searchInput);

    // 3. Tree View inside Scroll Container
    m_scrollContainer = std::make_shared<PrismScrollContainerUVE>("HierarchyScroll");
    m_treeView = std::make_shared<PrismTreeViewUVE>("HierarchyTree");
    m_treeView->SetOnSelectionChanged([this](const std::shared_ptr<PrismTreeNodeUVE>& node) {
        if (node) {
            m_selectedEntityId = node->GetEntityId();
            if (m_onEntitySelected) {
                m_onEntitySelected(m_selectedEntityId);
            }
        }
    });
    m_scrollContainer->AddChild(m_treeView);
    AddChild(m_scrollContainer);
}

std::shared_ptr<PrismTreeNodeUVE> PrismHierarchyWidgetUVE::BuildTreeNode(
    const PrismHierarchyItemUVE& item,
    std::uint64_t selectedId,
    std::shared_ptr<PrismTreeNodeUVE>& outSelectedNode
) {
    auto node = std::make_shared<PrismTreeNodeUVE>(item.name, item.id);
    node->SetExpanded(true);

    if (item.id != 0 && item.id == selectedId) {
        node->SetSelected(true);
        outSelectedNode = node;
    }

    for (const auto& childItem : item.children) {
        node->AddChild(BuildTreeNode(childItem, selectedId, outSelectedNode));
    }

    return node;
}

void PrismHierarchyWidgetUVE::SyncTree(
    const std::vector<PrismHierarchyItemUVE>& items,
    std::uint64_t selectedEntityId
) {
    m_selectedEntityId = selectedEntityId;
    m_treeView->ClearNodes();

    std::shared_ptr<PrismTreeNodeUVE> selectedNode = nullptr;
    for (const auto& item : items) {
        auto node = BuildTreeNode(item, selectedEntityId, selectedNode);
        m_treeView->AddRootNode(node);
    }

    if (selectedNode) {
        m_treeView->SelectNode(selectedNode);
    }
}

void PrismHierarchyWidgetUVE::ArrangeUVE(const Math::RectUVE& finalRect) {
    RelinkChildrenUVE();
    m_layoutRect = finalRect;

    const float startX = finalRect.position.x + m_style.padding.left;
    const float width = std::max(10.0F, finalRect.size.x - m_style.padding.Horizontal());

    // 1. Toolbar Rect (+ Button and Search)
    const float toolbarY = finalRect.position.y + m_style.padding.top;
    m_addButton->ArrangeUVE(Math::RectUVE{{startX, toolbarY}, {kAddButtonWidthUVE, 22.0F}});

    const float searchX = startX + kAddButtonWidthUVE + 4.0F;
    const float searchW = std::max(10.0F, width - kAddButtonWidthUVE - 4.0F);
    m_searchInput->ArrangeUVE(Math::RectUVE{{searchX, toolbarY}, {searchW, 22.0F}});

    // 2. Scroll Container and Tree Rect
    const float scrollY = toolbarY + kToolbarHeightUVE + 4.0F;
    const float scrollH = std::max(10.0F, finalRect.position.y + finalRect.size.y - scrollY - m_style.padding.bottom);
    const Math::RectUVE scrollRect{{startX, scrollY}, {width, scrollH}};
    m_scrollContainer->ArrangeUVE(scrollRect);
}

Math::Vector2UVE PrismHierarchyWidgetUVE::OnMeasureContentUVE(Math::Vector2UVE availableSize) {
    const float padH = m_style.padding.Horizontal();
    const float padV = m_style.padding.Vertical();
    const float innerW = std::max(10.0F, availableSize.x - padH);
    const float innerH = std::max(10.0F, availableSize.y - padV);

    m_addButton->MeasureUVE(Math::Vector2UVE{kAddButtonWidthUVE, 22.0F});
    const float searchW = std::max(10.0F, innerW - kAddButtonWidthUVE - 4.0F);
    m_searchInput->MeasureUVE(Math::Vector2UVE{searchW, 22.0F});

    const float scrollH = std::max(10.0F, innerH - kToolbarHeightUVE - 4.0F);
    m_scrollContainer->MeasureUVE(Math::Vector2UVE{innerW, scrollH});

    return availableSize;
}

void PrismHierarchyWidgetUVE::OnPaintContentUVE(PrismDrawListUVE& drawList) {
    PrismWidgetUVE::OnPaintContentUVE(drawList);

    const auto& theme = PrismThemeUVE::Dark();

    // Panel background and border
    drawList.AddRectFilled(m_layoutRect, theme.bgPanel, theme.radiusSmall);
    drawList.AddRectStroke(m_layoutRect, theme.borderSubtle, 1.0F, theme.radiusSmall);

    // Separator line under search toolbar
    const float sepY = m_layoutRect.position.y + m_style.padding.top + kToolbarHeightUVE + 2.0F;
    drawList.AddRectFilled(
        Math::RectUVE{{m_layoutRect.position.x + 4.0F, sepY}, {m_layoutRect.size.x - 8.0F, 1.0F}},
        theme.borderSubtle
    );
}

} // namespace UVE::UI::Prism
