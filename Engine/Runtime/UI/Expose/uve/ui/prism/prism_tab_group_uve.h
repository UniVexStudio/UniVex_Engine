// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "uve/ui/prism/prism_theme_uve.h"
#include "uve/ui/prism/prism_widget_uve.h"

namespace UVE::UI::Prism {

/// Represents a single tab in a tab group or dock node.
struct PrismTabItemUVE {
    std::string                      title;
    std::shared_ptr<PrismWidgetUVE>  content;
    bool                             closable = true;
};

/// High-performance Tab bar & container for dockable panels and workspace views.
class PrismTabGroupUVE : public PrismWidgetUVE {
public:
    explicit PrismTabGroupUVE(std::string debugName = "TabGroup");

    void AddTab(std::string title, std::shared_ptr<PrismWidgetUVE> content, bool closable = true);
    void RemoveTab(std::size_t index);
    void ClearTabs() noexcept;

    [[nodiscard]] const std::vector<PrismTabItemUVE>& GetTabs() const noexcept { return m_tabs; }
    [[nodiscard]] std::size_t GetActiveTabIndex() const noexcept { return m_activeTabIndex; }
    void SetActiveTabIndex(std::size_t index) noexcept;

    [[nodiscard]] std::shared_ptr<PrismWidgetUVE> GetActiveContent() const noexcept;

    // --- Callbacks ---
    void SetOnTabSelected(std::function<void(std::size_t)> callback) {
        m_onTabSelected = std::move(callback);
    }
    void SetOnTabCloseRequested(std::function<void(std::size_t)> callback) {
        m_onTabCloseRequested = std::move(callback);
    }
    void SetOnTabDragStarted(std::function<void(std::size_t, Math::Vector2UVE)> callback) {
        m_onTabDragStarted = std::move(callback);
    }

    // --- State overrides ---
    PrismEventResultUVE OnPointerDown(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnPointerMove(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnPointerUp(const PrismPointerEventUVE& event) override;

    void ArrangeUVE(const Math::RectUVE& finalRect) override;

protected:
    Math::Vector2UVE OnMeasureContentUVE(Math::Vector2UVE availableSize) override;
    void OnPaintContentUVE(PrismDrawListUVE& drawList) override;

private:
    struct TabGeometryUVE {
        Math::RectUVE tabRect;
        Math::RectUVE closeRect;
    };

    [[nodiscard]] Math::RectUVE GetTabBarRect() const noexcept;
    [[nodiscard]] Math::RectUVE GetContentRect() const noexcept;
    void ComputeTabGeometries(std::vector<TabGeometryUVE>& outGeometries) const;

    std::vector<PrismTabItemUVE> m_tabs;
    std::size_t                  m_activeTabIndex = 0;

    int                          m_hoveredTabIndex = -1;
    bool                         m_hoveredCloseButton = false;

    bool                         m_isPotentialDrag = false;
    bool                         m_isDragging = false;
    std::size_t                  m_dragTabIndex = 0;
    Math::Vector2UVE             m_dragStartPos{};

    std::function<void(std::size_t)>                     m_onTabSelected;
    std::function<void(std::size_t)>                     m_onTabCloseRequested;
    std::function<void(std::size_t, Math::Vector2UVE)>   m_onTabDragStarted;
};

} // namespace UVE::UI::Prism
