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

void PrismTreeViewUVE::ArrangeUVE(const Math::RectUVE& finalRect) {
    PrismWidgetUVE::ArrangeUVE(finalRect);
    m_flatRows.clear();
    BuildFlatRows(m_roots, 0, m_flatRows);
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

        // 1. Subtle Hierarchy Guide Lines (Unreal Engine 5 style)
        if (row.depth > 0) {
            const float startX = m_layoutRect.position.x + m_style.padding.left;
            for (int d = 1; d <= row.depth; ++d) {
                const float guideX = startX + static_cast<float>(d - 1) * kIndentWidthUVE + 7.0F;
                drawList.AddLine(
                    Math::Vector2UVE{guideX, row.rowRect.position.y},
                    Math::Vector2UVE{guideX, row.rowRect.position.y + kRowHeightUVE},
                    PrismColorUVE{1.0F, 1.0F, 1.0F, 0.06F},
                    1.0F
                );
            }
        }

        // 2. Full-Box Ambient Fade Selection / Hover (No 1px/2px harsh slop lines!)
        if (isSelected) {
            // Full box soft ambient wash
            drawList.AddRectFilled(row.rowRect, PrismColorUVE{0.18F, 0.26F, 0.42F, 0.42F}, theme.radiusSmall);
            drawList.AddRectStroke(row.rowRect, PrismColorUVE{0.32F, 0.48F, 0.78F, 0.38F}, 1.0F, theme.radiusSmall);
        } else if (isHovered) {
            drawList.AddRectFilled(row.rowRect, PrismColorUVE{1.0F, 1.0F, 1.0F, 0.04F}, theme.radiusSmall);
        }

        // 3. Crisp Chevron Arrow Indicator
        if (!row.node->IsLeaf()) {
            const PrismColorUVE chevronCol = isSelected ? theme.accentPrimary : theme.textSecondary;
            const float midX = row.arrowRect.position.x + kArrowSizeUVE * 0.5F;
            const float midY = row.arrowRect.position.y + kArrowSizeUVE * 0.5F;
            if (row.node->IsExpanded()) {
                // Expanded downward chevron: \/
                drawList.AddLine(Math::Vector2UVE{midX - 3.5F, midY - 2.0F}, Math::Vector2UVE{midX, midY + 2.0F}, chevronCol, 1.25F);
                drawList.AddLine(Math::Vector2UVE{midX, midY + 2.0F}, Math::Vector2UVE{midX + 3.5F, midY - 2.0F}, chevronCol, 1.25F);
            } else {
                // Collapsed rightward chevron: >
                drawList.AddLine(Math::Vector2UVE{midX - 2.0F, midY - 3.5F}, Math::Vector2UVE{midX + 2.0F, midY}, chevronCol, 1.25F);
                drawList.AddLine(Math::Vector2UVE{midX + 2.0F, midY}, Math::Vector2UVE{midX - 2.0F, midY + 3.5F}, chevronCol, 1.25F);
            }
        }

        // 4. Crisp Real Typography (Zero placeholder blocks!)
        const float labelX = row.arrowRect.position.x + kArrowSizeUVE + 6.0F;
        const float labelY = row.rowRect.position.y + (kRowHeightUVE - 14.0F) * 0.5F;
        const PrismColorUVE textCol = isSelected
            ? theme.textPrimary
            : (isHovered ? theme.textPrimary : theme.textSecondary);
        drawList.AddText(Math::Vector2UVE{labelX, labelY}, row.node->GetLabel(), textCol, 13.0F);
    }
}

} // namespace UVE::UI::Prism
