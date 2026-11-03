// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <memory>
#include "uve/ui/prism/prism_theme_uve.h"
#include "uve/ui/prism/prism_widget_uve.h"

namespace UVE::UI::Prism {

/// Scrollable container widget that clips overflow content and provides
/// smooth mouse-wheel scrolling and interactive vertical scrollbars.
class PrismScrollContainerUVE : public PrismWidgetUVE {
public:
    explicit PrismScrollContainerUVE(std::string debugName = "ScrollContainer");

    void SetScrollOffsetY(float offset) noexcept;
    [[nodiscard]] float GetScrollOffsetY() const noexcept { return m_scrollOffsetY; }

    [[nodiscard]] float GetMaxScrollOffsetY() const noexcept { return m_maxScrollOffsetY; }

    // --- State overrides ---
    PrismEventResultUVE OnScroll(const PrismScrollEventUVE& event) override;
    PrismEventResultUVE OnPointerDown(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnPointerMove(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnPointerUp(const PrismPointerEventUVE& event) override;

    void ArrangeUVE(const Math::RectUVE& finalRect) override;
    void PaintUVE(PrismDrawListUVE& drawList) override;

protected:
    Math::Vector2UVE OnMeasureContentUVE(Math::Vector2UVE availableSize) override;
    void OnPaintContentUVE(PrismDrawListUVE& drawList) override;

private:
    [[nodiscard]] Math::RectUVE GetScrollbarTrackRect() const noexcept;
    [[nodiscard]] Math::RectUVE GetScrollbarThumbRect() const noexcept;

    float m_scrollOffsetY = 0.0F;
    float m_maxScrollOffsetY = 0.0F;
    float m_totalContentHeight = 0.0F;

    bool  m_isDraggingThumb = false;
    float m_dragStartMouseY = 0.0F;
    float m_dragStartScrollY = 0.0F;
};

} // namespace UVE::UI::Prism
