// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "uve/math/rect_uve.h"
#include "uve/ui/prism/prism_widget_uve.h"

namespace UVE::UI::Prism {

/// How a docking container splits its internal space.
enum class PrismDockSplitDirectionUVE : std::uint8_t {
    None,
    Horizontal, // Left | Right
    Vertical,   // Top / Bottom
};

/// Target drop quadrant when dragging and docking a window/tab.
enum class PrismDockQuadrantUVE : std::uint8_t {
    Center, // Tab into active container
    Left,   // Split container horizontally to the left
    Right,  // Split container horizontally to the right
    Top,    // Split container vertically to the top
    Bottom, // Split container vertically to the bottom
};

/// Mathematical Tree Node for the Docking Engine.
/// Guarantees:
/// 1. Leaf nodes contain dockable panels (as tabs).
/// 2. Split nodes contain exactly two children (first and second) with a normalized ratio (0.05f .. 0.95f).
/// 3. Resizing one splitter cannot invalidate sibling or ancestor trees.
class PrismDockNodeUVE : public std::enable_shared_from_this<PrismDockNodeUVE> {
public:
    explicit PrismDockNodeUVE(std::string name = "DockNode");

    [[nodiscard]] bool IsLeaf() const noexcept { return m_splitDirection == PrismDockSplitDirectionUVE::None; }
    [[nodiscard]] PrismDockSplitDirectionUVE GetSplitDirection() const noexcept { return m_splitDirection; }
    [[nodiscard]] float GetSplitRatio() const noexcept { return m_splitRatio; }
    void SetSplitRatio(float ratio) noexcept;

    // --- Children ---
    [[nodiscard]] std::shared_ptr<PrismDockNodeUVE> GetFirstChild() const noexcept { return m_firstChild; }
    [[nodiscard]] std::shared_ptr<PrismDockNodeUVE> GetSecondChild() const noexcept { return m_secondChild; }
    [[nodiscard]] std::shared_ptr<PrismDockNodeUVE> GetParentNode() const noexcept { return m_parentNode.lock(); }

    // --- Panels (Leaf mode) ---
    void AddPanel(std::shared_ptr<PrismWidgetUVE> panel);
    void RemovePanel(const std::shared_ptr<PrismWidgetUVE>& panel);
    [[nodiscard]] const std::vector<std::shared_ptr<PrismWidgetUVE>>& GetPanels() const noexcept { return m_panels; }
    [[nodiscard]] std::size_t GetActivePanelIndex() const noexcept { return m_activePanelIndex; }
    void SetActivePanelIndex(std::size_t index) noexcept;

    // --- Splitting Operations ---
    /// Splits this leaf node into two child leaf nodes.
    bool Split(PrismDockSplitDirectionUVE direction, float ratio, std::shared_ptr<PrismWidgetUVE> newPanel);

    // --- Layout Propagation ---
    void ArrangeLayout(const Math::RectUVE& bounds);
    [[nodiscard]] Math::RectUVE GetBounds() const noexcept { return m_bounds; }

    /// Returns the hit-tested drop zone quadrant given a cursor position.
    [[nodiscard]] PrismDockQuadrantUVE CalculateDropQuadrant(Math::Vector2UVE cursor) const noexcept;

private:
    std::string                                  m_name;
    Math::RectUVE                                m_bounds{};
    PrismDockSplitDirectionUVE                   m_splitDirection = PrismDockSplitDirectionUVE::None;
    float                                        m_splitRatio = 0.5F; // 0.05f to 0.95f

    std::weak_ptr<PrismDockNodeUVE>              m_parentNode;
    std::shared_ptr<PrismDockNodeUVE>            m_firstChild;
    std::shared_ptr<PrismDockNodeUVE>            m_secondChild;

    std::vector<std::shared_ptr<PrismWidgetUVE>> m_panels;
    std::size_t                                  m_activePanelIndex = 0;
};

} // namespace UVE::UI::Prism
