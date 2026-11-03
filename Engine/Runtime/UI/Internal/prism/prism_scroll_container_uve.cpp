// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_scroll_container_uve.h"

namespace UVE::UI::Prism {

namespace {
constexpr float kScrollbarWidthUVE = 8.0F;
constexpr float kMinThumbHeightUVE = 24.0F;
} // namespace

PrismScrollContainerUVE::PrismScrollContainerUVE(std::string debugName)
    : PrismWidgetUVE(std::move(debugName)) {
    m_style.clipChildren = true;
    m_style.flexDirection = PrismFlexDirectionUVE::Column;
}

void PrismScrollContainerUVE::SetScrollOffsetY(float offset) noexcept {
    m_scrollOffsetY = std::clamp(offset, 0.0F, m_maxScrollOffsetY);
}

Math::RectUVE PrismScrollContainerUVE::GetScrollbarTrackRect() const noexcept {
    const float trackX = m_layoutRect.position.x + m_layoutRect.size.x - kScrollbarWidthUVE - 2.0F;
    const float trackY = m_layoutRect.position.y + 2.0F;
    const float trackH = std::max(0.0F, m_layoutRect.size.y - 4.0F);
    return Math::RectUVE{{trackX, trackY}, {kScrollbarWidthUVE, trackH}};
}

Math::RectUVE PrismScrollContainerUVE::GetScrollbarThumbRect() const noexcept {
    const Math::RectUVE track = GetScrollbarTrackRect();
    if (m_totalContentHeight <= m_layoutRect.size.y || track.size.y <= 0.0F) {
        return Math::RectUVE{};
    }

    const float viewRatio = m_layoutRect.size.y / m_totalContentHeight;
    const float thumbH = std::max(kMinThumbHeightUVE, track.size.y * viewRatio);
    const float scrollFraction = (m_maxScrollOffsetY > 0.0F) ? (m_scrollOffsetY / m_maxScrollOffsetY) : 0.0F;
    const float thumbY = track.position.y + scrollFraction * (track.size.y - thumbH);

    return Math::RectUVE{{track.position.x, thumbY}, {track.size.x, thumbH}};
}

Math::Vector2UVE PrismScrollContainerUVE::OnMeasureContentUVE(Math::Vector2UVE availableSize) {
    // Measure all children unconstrained in height to determine total scroll height
    m_totalContentHeight = 0.0F;
    float maxChildWidth = 0.0F;

    for (std::size_t i = 0; i < m_children.size(); ++i) {
        auto& child = m_children[i];
        if (!child) continue;

        const Math::Vector2UVE childSize = child->MeasureUVE(Math::Vector2UVE{availableSize.x, 1000000.0F});
        m_totalContentHeight += childSize.y;
        if (i > 0) {
            m_totalContentHeight += m_style.gap;
        }
        maxChildWidth = std::max(maxChildWidth, childSize.x);
    }

    m_maxScrollOffsetY = std::max(0.0F, m_totalContentHeight - availableSize.y);
    m_scrollOffsetY = std::clamp(m_scrollOffsetY, 0.0F, m_maxScrollOffsetY);

    return Math::Vector2UVE{maxChildWidth, availableSize.y};
}

void PrismScrollContainerUVE::ArrangeUVE(const Math::RectUVE& finalRect) {
    m_layoutRect = finalRect;
    m_maxScrollOffsetY = std::max(0.0F, m_totalContentHeight - finalRect.size.y);
    m_scrollOffsetY = std::clamp(m_scrollOffsetY, 0.0F, m_maxScrollOffsetY);

    // Arrange children with vertical scroll offset applied
    float currentY = finalRect.position.y + m_style.padding.top - m_scrollOffsetY;
    const float contentX = finalRect.position.x + m_style.padding.left;
    const float contentW = std::max(0.0F, finalRect.size.x - m_style.padding.Horizontal() - kScrollbarWidthUVE);

    for (auto& child : m_children) {
        if (!child) continue;
        const Math::Vector2UVE desired = child->GetDesiredSize();
        const Math::RectUVE childRect{{contentX, currentY}, {contentW, desired.y}};
        child->ArrangeUVE(childRect);
        currentY += desired.y + m_style.gap;
    }
}

PrismEventResultUVE PrismScrollContainerUVE::OnScroll(const PrismScrollEventUVE& event) {
    if (m_maxScrollOffsetY <= 0.0F) {
        return PrismEventResultUVE::Unhandled;
    }

    // Scroll by ~30 pixels per mouse wheel step
    const float deltaY = -event.wheelDelta.y * 30.0F;
    SetScrollOffsetY(m_scrollOffsetY + deltaY);
    ArrangeUVE(m_layoutRect);
    return PrismEventResultUVE::Handled;
}

PrismEventResultUVE PrismScrollContainerUVE::OnPointerDown(const PrismPointerEventUVE& event) {
    if (event.button == Input::MouseButtonUVE::Left && m_maxScrollOffsetY > 0.0F) {
        const Math::RectUVE thumb = GetScrollbarThumbRect();
        if (Math::ContainsUVE(thumb, event.position)) {
            m_isDraggingThumb = true;
            m_dragStartMouseY = event.position.y;
            m_dragStartScrollY = m_scrollOffsetY;
            return PrismEventResultUVE::Handled;
        }
    }
    return PrismWidgetUVE::OnPointerDown(event);
}

PrismEventResultUVE PrismScrollContainerUVE::OnPointerMove(const PrismPointerEventUVE& event) {
    if (m_isDraggingThumb && m_maxScrollOffsetY > 0.0F) {
        const Math::RectUVE track = GetScrollbarTrackRect();
        const Math::RectUVE thumb = GetScrollbarThumbRect();
        const float scrollableTrackH = track.size.y - thumb.size.y;
        if (scrollableTrackH > 0.0F) {
            const float mouseDeltaY = event.position.y - m_dragStartMouseY;
            const float scrollDeltaY = (mouseDeltaY / scrollableTrackH) * m_maxScrollOffsetY;
            SetScrollOffsetY(m_dragStartScrollY + scrollDeltaY);
            ArrangeUVE(m_layoutRect);
        }
        return PrismEventResultUVE::Handled;
    }
    return PrismWidgetUVE::OnPointerMove(event);
}

PrismEventResultUVE PrismScrollContainerUVE::OnPointerUp(const PrismPointerEventUVE& event) {
    if (event.button == Input::MouseButtonUVE::Left && m_isDraggingThumb) {
        m_isDraggingThumb = false;
        return PrismEventResultUVE::Handled;
    }
    return PrismWidgetUVE::OnPointerUp(event);
}

void PrismScrollContainerUVE::OnPaintContentUVE(PrismDrawListUVE& drawList) {
    PrismWidgetUVE::OnPaintContentUVE(drawList);

    // Draw scrollbar if content exceeds container height
    if (m_maxScrollOffsetY > 0.0F) {
        const auto& theme = PrismThemeUVE::Dark();
        const Math::RectUVE track = GetScrollbarTrackRect();
        const Math::RectUVE thumb = GetScrollbarThumbRect();

        // Subtle solid matte track
        drawList.AddRectFilled(track, PrismColorUVE{0.06F, 0.07F, 0.09F, 1.0F}, PrismCornerRadiiUVE{4.0F});

        // Thumb
        PrismColorUVE thumbColor = m_isDraggingThumb ? theme.accentPrimary : theme.bgWidgetHover;
        drawList.AddRectFilled(thumb, thumbColor, PrismCornerRadiiUVE{4.0F});
    }
}

void PrismScrollContainerUVE::PaintUVE(PrismDrawListUVE& drawList) {
    // Hierarchical Scissor: Children must be strictly clipped inside container rect
    PrismWidgetUVE::PaintUVE(drawList);
}

} // namespace UVE::UI::Prism
