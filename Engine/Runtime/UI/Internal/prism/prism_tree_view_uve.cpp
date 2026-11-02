// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_tree_view_uve.h"
#include <algorithm>

namespace UVE::UI::Prism {

namespace {
constexpr float kRowHeightUVE = 22.0F;
constexpr float kIndentWidthUVE = 16.0F;
constexpr float kArrowSizeUVE = 10.0F;
} // namespace

// ============================================================================
// PrismTreeNodeUVE
// ============================================================================

PrismTreeNodeUVE::PrismTreeNodeUVE(std::string label, std::uint64_t entityId)
    : m_label(std::move(label)), m_entityId(entityId) {}

void PrismTreeNodeUVE::AddChild(std::shared_ptr<PrismTreeNodeUVE> child) {
    if (child) {
        m_children.push_back(std::move(child));
    }
}

void PrismTreeNodeUVE::RemoveChild(const std::shared_ptr<PrismTreeNodeUVE>& child) {
    std::erase(m_children, child);
}

void PrismTreeNodeUVE::ClearChildren() noexcept {
    m_children.clear();
}

// ============================================================================
// PrismTreeViewUVE
// ============================================================================

PrismTreeViewUVE::PrismTreeViewUVE(std::string debugName)
    : PrismWidgetUVE(std::move(debugName)) {
    m_isFocusable = true;
    m_style.padding = PrismEdgesUVE{2.0F, 2.0F};
}

void PrismTreeViewUVE::AddRootNode(std::shared_ptr<PrismTreeNodeUVE> node) {
    if (node) {
        m_roots.push_back(std::move(node));
    }
}

void PrismTreeViewUVE::ClearNodes() noexcept {
    m_roots.clear();
    m_selectedNode.reset();
    m_flatRows.clear();
}

void PrismTreeViewUVE::SelectNode(const std::shared_ptr<PrismTreeNodeUVE>& node) {
    if (m_selectedNode != node) {
        if (m_selectedNode) {
            m_selectedNode->SetSelected(false);
        }
        m_selectedNode = node;
        if (m_selectedNode) {
            m_selectedNode->SetSelected(true);
        }
        if (m_onSelectionChanged) {
            m_onSelectionChanged(m_selectedNode);
        }
    }
}

void PrismTreeViewUVE::BuildFlatRows(
    const std::vector<std::shared_ptr<PrismTreeNodeUVE>>& nodes,
    int depth,
    std::vector<FlatRowUVE>& outRows
) const {
    const float startX = m_layoutRect.position.x + m_style.padding.left;
    const float rowW = std::max(10.0F, m_layoutRect.size.x - m_style.padding.Horizontal());

    for (const auto& node : nodes) {
        if (!node) continue;

        const float rowY = m_layoutRect.position.y + m_style.padding.top + static_cast<float>(outRows.size()) * kRowHeightUVE;
        const float indent = static_cast<float>(depth) * kIndentWidthUVE;

        FlatRowUVE row;
        row.node = node;
        row.depth = depth;
        row.rowRect = Math::RectUVE{{startX, rowY}, {rowW, kRowHeightUVE}};
        row.arrowRect = Math::RectUVE{
            {startX + indent + 2.0F, rowY + (kRowHeightUVE - kArrowSizeUVE) * 0.5F},
            {kArrowSizeUVE, kArrowSizeUVE}
        };

        outRows.push_back(row);

        if (node->IsExpanded() && !node->GetChildren().empty()) {
            BuildFlatRows(node->GetChildren(), depth + 1, outRows);
        }
    }
}

PrismEventResultUVE PrismTreeViewUVE::OnPointerDown(const PrismPointerEventUVE& event) {
    PrismWidgetUVE::OnPointerDown(event);

    if (event.button == Input::MouseButtonUVE::Left) {
        for (auto& row : m_flatRows) {
            if (Math::ContainsUVE(row.rowRect, event.position)) {
                // Click on arrow toggles expand/collapse
                if (!row.node->IsLeaf() && Math::ContainsUVE(row.arrowRect, event.position)) {
                    row.node->SetExpanded(!row.node->IsExpanded());
                    return PrismEventResultUVE::Handled;
                }

                // Click on row selects the node
                SelectNode(row.node);
                return PrismEventResultUVE::Handled;
            }
        }
    }

    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismTreeViewUVE::OnPointerMove(const PrismPointerEventUVE& event) {
    m_hoveredRowIndex = -1;
    for (std::size_t i = 0; i < m_flatRows.size(); ++i) {
        if (Math::ContainsUVE(m_flatRows[i].rowRect, event.position)) {
            m_hoveredRowIndex = static_cast<int>(i);
            break;
        }
    }
    return PrismWidgetUVE::OnPointerMove(event);
}

PrismEventResultUVE PrismTreeViewUVE::OnKeyDown(const PrismKeyEventUVE& event) {
    if (m_flatRows.empty()) return PrismWidgetUVE::OnKeyDown(event);

    int currentIndex = -1;
    for (std::size_t i = 0; i < m_flatRows.size(); ++i) {
        if (m_flatRows[i].node == m_selectedNode) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }

    if (event.keyCode == Input::KeyCodeUVE::Down) {
        const int nextIndex = std::min(static_cast<int>(m_flatRows.size()) - 1, currentIndex + 1);
        SelectNode(m_flatRows[static_cast<std::size_t>(nextIndex)].node);
        return PrismEventResultUVE::Handled;
    }

    if (event.keyCode == Input::KeyCodeUVE::Up) {
        const int prevIndex = std::max(0, currentIndex - 1);
        SelectNode(m_flatRows[static_cast<std::size_t>(prevIndex)].node);
        return PrismEventResultUVE::Handled;
    }

    if (event.keyCode == Input::KeyCodeUVE::Right && m_selectedNode) {
        if (!m_selectedNode->IsLeaf() && !m_selectedNode->IsExpanded()) {
            m_selectedNode->SetExpanded(true);
            return PrismEventResultUVE::Handled;
        }
    }

    if (event.keyCode == Input::KeyCodeUVE::Left && m_selectedNode) {
        if (m_selectedNode->IsExpanded()) {
            m_selectedNode->SetExpanded(false);
            return PrismEventResultUVE::Handled;
        }
    }

    return PrismWidgetUVE::OnKeyDown(event);
}

Math::Vector2UVE PrismTreeViewUVE::OnMeasureContentUVE(Math::Vector2UVE /*availableSize*/) {
    m_flatRows.clear();
    BuildFlatRows(m_roots, 0, m_flatRows);

    const float totalH = static_cast<float>(m_flatRows.size()) * kRowHeightUVE + m_style.padding.Vertical();
    return Math::Vector2UVE{200.0F, totalH};
}

void PrismTreeViewUVE::OnPaintContentUVE(PrismDrawListUVE& drawList) {
    PrismWidgetUVE::OnPaintContentUVE(drawList);

    // Refresh rows for current layout rect
    m_flatRows.clear();
    BuildFlatRows(m_roots, 0, m_flatRows);

    const auto& theme = PrismThemeUVE::Dark();

    for (std::size_t i = 0; i < m_flatRows.size(); ++i) {
        const auto& row = m_flatRows[i];
        const bool isSelected = (row.node == m_selectedNode);
        const bool isHovered = (static_cast<int>(i) == m_hoveredRowIndex);

        // Row background
        if (isSelected) {
            drawList.AddRectFilled(row.rowRect, theme.bgWidgetActive, theme.radiusSmall);
            drawList.AddRectStroke(row.rowRect, theme.accentPrimary, 1.0F, theme.radiusSmall);
        } else if (isHovered) {
            drawList.AddRectFilled(row.rowRect, theme.bgWidgetHover, theme.radiusSmall);
        }

        // Expand arrow indicator (if not leaf)
        if (!row.node->IsLeaf()) {
            const Math::ColorUVE arrowColor = isSelected ? theme.accentPrimary : theme.textSecondary;
            drawList.AddRectFilled(row.arrowRect, arrowColor, PrismCornerRadiiUVE{2.0F});
        }

        // Label text
        const float labelX = row.arrowRect.position.x + kArrowSizeUVE + 6.0F;
        const float labelY = row.rowRect.position.y + (kRowHeightUVE - 12.0F) * 0.5F;
        const float charW = 7.5F;
        const auto& text = row.node->GetLabel();

        for (std::size_t c = 0; c < text.length(); ++c) {
            const float x = labelX + static_cast<float>(c) * charW;
            drawList.AddRectFilled(
                Math::RectUVE{{x, labelY}, {charW, 12.0F}},
                isSelected ? theme.textPrimary : theme.textSecondary
            );
        }
    }
}

} // namespace UVE::UI::Prism
