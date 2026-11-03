// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_dock_node_uve.h"
#include <algorithm>

namespace UVE::UI::Prism {

PrismDockNodeUVE::PrismDockNodeUVE(std::string name)
    : m_name(std::move(name)) {}

void PrismDockNodeUVE::SetSplitRatio(float ratio) noexcept {
    m_splitRatio = std::clamp(ratio, 0.05F, 0.95F);
}

void PrismDockNodeUVE::AddPanel(std::shared_ptr<PrismWidgetUVE> panel) {
    if (!panel) return;
    m_panels.push_back(std::move(panel));
    m_activePanelIndex = m_panels.size() - 1;
}

void PrismDockNodeUVE::RemovePanel(const std::shared_ptr<PrismWidgetUVE>& panel) {
    auto it = std::find(m_panels.begin(), m_panels.end(), panel);
    if (it != m_panels.end()) {
        m_panels.erase(it);
        if (m_activePanelIndex >= m_panels.size() && !m_panels.empty()) {
            m_activePanelIndex = m_panels.size() - 1;
        }
    }
}

void PrismDockNodeUVE::SetActivePanelIndex(std::size_t index) noexcept {
    if (index < m_panels.size()) {
        m_activePanelIndex = index;
    }
}

bool PrismDockNodeUVE::Split(PrismDockSplitDirectionUVE direction, float ratio, std::shared_ptr<PrismWidgetUVE> newPanel) {
    if (direction == PrismDockSplitDirectionUVE::None || !IsLeaf()) {
        return false;
    }

    m_splitDirection = direction;
    SetSplitRatio(ratio);

    // First child takes existing panels
    m_firstChild = std::make_shared<PrismDockNodeUVE>(m_name + "_First");
    m_firstChild->m_parentNode = shared_from_this();
    m_firstChild->m_panels = std::move(m_panels);
    m_firstChild->m_activePanelIndex = m_activePanelIndex;

    // Second child takes the newly docked panel
    m_secondChild = std::make_shared<PrismDockNodeUVE>(m_name + "_Second");
    m_secondChild->m_parentNode = shared_from_this();
    if (newPanel) {
        m_secondChild->AddPanel(std::move(newPanel));
    }

    m_panels.clear();
    m_activePanelIndex = 0;
    return true;
}

void PrismDockNodeUVE::ArrangeLayout(const Math::RectUVE& bounds) {
    m_bounds = bounds;

    if (IsLeaf()) {
        // Position active panel if present
        if (m_activePanelIndex < m_panels.size() && m_panels[m_activePanelIndex]) {
            m_panels[m_activePanelIndex]->ArrangeUVE(bounds);
        }
        return;
    }

    if (!m_firstChild || !m_secondChild) {
        return;
    }

    if (m_splitDirection == PrismDockSplitDirectionUVE::Horizontal) {
        const float firstW = bounds.size.x * m_splitRatio;
        const float secondW = bounds.size.x - firstW;

        const Math::RectUVE firstBounds{bounds.position, Math::Vector2UVE{firstW, bounds.size.y}};
        const Math::RectUVE secondBounds{
            Math::Vector2UVE{bounds.position.x + firstW, bounds.position.y},
            Math::Vector2UVE{secondW, bounds.size.y}
        };

        m_firstChild->ArrangeLayout(firstBounds);
        m_secondChild->ArrangeLayout(secondBounds);
    } else if (m_splitDirection == PrismDockSplitDirectionUVE::Vertical) {
        const float firstH = bounds.size.y * m_splitRatio;
        const float secondH = bounds.size.y - firstH;

        const Math::RectUVE firstBounds{bounds.position, Math::Vector2UVE{bounds.size.x, firstH}};
        const Math::RectUVE secondBounds{
            Math::Vector2UVE{bounds.position.x, bounds.position.y + firstH},
            Math::Vector2UVE{bounds.size.x, secondH}
        };

        m_firstChild->ArrangeLayout(firstBounds);
        m_secondChild->ArrangeLayout(secondBounds);
    }
}

PrismDockQuadrantUVE PrismDockNodeUVE::CalculateDropQuadrant(Math::Vector2UVE cursor) const noexcept {
    if (m_bounds.size.x <= 0.0F || m_bounds.size.y <= 0.0F) {
        return PrismDockQuadrantUVE::Center;
    }

    // Normalized coordinate within this dock node
    const float normX = (cursor.x - m_bounds.position.x) / m_bounds.size.x;
    const float normY = (cursor.y - m_bounds.position.y) / m_bounds.size.y;

    // Outer 25% edge margin dictates side splits; inner 50% dictates tab drop into center
    if (normX < 0.25F) return PrismDockQuadrantUVE::Left;
    if (normX > 0.75F) return PrismDockQuadrantUVE::Right;
    if (normY < 0.25F) return PrismDockQuadrantUVE::Top;
    if (normY > 0.75F) return PrismDockQuadrantUVE::Bottom;

    return PrismDockQuadrantUVE::Center;
}

} // namespace UVE::UI::Prism
