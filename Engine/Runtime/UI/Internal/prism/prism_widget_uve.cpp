// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_widget_uve.h"
#include <algorithm>

namespace UVE::UI::Prism {

PrismWidgetUVE::PrismWidgetUVE(std::string debugName)
    : m_debugName(std::move(debugName)) {}

void PrismWidgetUVE::AddChild(std::shared_ptr<PrismWidgetUVE> child) {
    if (!child || child.get() == this) {
        return;
    }
    // weak_from_this() (not shared_from_this()) so composite widgets can AddChild() from their own
    // constructor, before any shared_ptr owns them: the link is then empty and ArrangeUVE() fills
    // it in once the parent is owned, instead of throwing std::bad_weak_ptr.
    child->m_parent = weak_from_this();
    m_children.push_back(std::move(child));
}

void PrismWidgetUVE::RemoveChild(const std::shared_ptr<PrismWidgetUVE>& child) {
    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        (*it)->m_parent.reset();
        m_children.erase(it);
    }
}

void PrismWidgetUVE::ClearChildren() {
    for (auto& child : m_children) {
        if (child) {
            child->m_parent.reset();
        }
    }
    m_children.clear();
}

Math::Vector2UVE PrismWidgetUVE::OnMeasureContentUVE(Math::Vector2UVE availableSize) {
    float mainAxisContent = 0.0F;
    float crossAxisContent = 0.0F;
    const bool isRow = (m_style.flexDirection == PrismFlexDirectionUVE::Row);

    const float padMain = isRow ? m_style.padding.Horizontal() : m_style.padding.Vertical();
    const float padCross = isRow ? m_style.padding.Vertical() : m_style.padding.Horizontal();

    const float childAvailMain = std::max(0.0F, (isRow ? availableSize.x : availableSize.y) - padMain);
    const float childAvailCross = std::max(0.0F, (isRow ? availableSize.y : availableSize.x) - padCross);

    for (std::size_t i = 0; i < m_children.size(); ++i) {
        auto& child = m_children[i];
        if (!child) continue;

        const Math::Vector2UVE childAvail = isRow
            ? Math::Vector2UVE{childAvailMain, childAvailCross}
            : Math::Vector2UVE{childAvailCross, childAvailMain};

        const Math::Vector2UVE childSize = child->MeasureUVE(childAvail);
        const float cMain = isRow ? childSize.x : childSize.y;
        const float cCross = isRow ? childSize.y : childSize.x;

        mainAxisContent += cMain;
        if (i > 0) {
            mainAxisContent += m_style.gap;
        }
        crossAxisContent = std::max(crossAxisContent, cCross);
    }

    return isRow
        ? Math::Vector2UVE{mainAxisContent + padMain, crossAxisContent + padCross}
        : Math::Vector2UVE{crossAxisContent + padCross, mainAxisContent + padMain};
}

Math::Vector2UVE PrismWidgetUVE::MeasureUVE(Math::Vector2UVE availableSize) {
    const Math::Vector2UVE contentSize = OnMeasureContentUVE(availableSize);

    float resolvedW = m_style.width.Resolve(availableSize.x, contentSize.x);
    float resolvedH = m_style.height.Resolve(availableSize.y, contentSize.y);

    resolvedW = std::clamp(resolvedW, m_style.minWidth, m_style.maxWidth);
    resolvedH = std::clamp(resolvedH, m_style.minHeight, m_style.maxHeight);

    m_desiredSize = Math::Vector2UVE{resolvedW, resolvedH};
    return m_desiredSize;
}

void PrismWidgetUVE::ArrangeUVE(const Math::RectUVE& finalRect) {
    m_layoutRect = finalRect;
    RelinkChildrenUVE();

    if (m_children.empty()) {
        return;
    }

    const bool isRow = (m_style.flexDirection == PrismFlexDirectionUVE::Row);
    const float contentX = finalRect.position.x + m_style.padding.left;
    const float contentY = finalRect.position.y + m_style.padding.top;
    const float contentW = std::max(0.0F, finalRect.size.x - m_style.padding.Horizontal());
    const float contentH = std::max(0.0F, finalRect.size.y - m_style.padding.Vertical());

    const float containerMain = isRow ? contentW : contentH;
    const float containerCross = isRow ? contentH : contentW;

    float totalBaseMain = 0.0F;
    float totalFlexGrow = 0.0F;

    for (std::size_t i = 0; i < m_children.size(); ++i) {
        auto& child = m_children[i];
        if (!child) continue;

        const Math::Vector2UVE d = child->GetDesiredSize();
        totalBaseMain += (isRow ? d.x : d.y);
        if (i > 0) {
            totalBaseMain += m_style.gap;
        }
        totalFlexGrow += child->GetStyle().flexGrow;
    }

    const float remainingSpace = std::max(0.0F, containerMain - totalBaseMain);

    float mainCursor = isRow ? contentX : contentY;
    float gapOffset = m_style.gap;

    if (totalFlexGrow <= 0.0001F) {
        if (m_style.justifyContent == PrismJustifyUVE::Center) {
            mainCursor += remainingSpace * 0.5F;
        } else if (m_style.justifyContent == PrismJustifyUVE::End) {
            mainCursor += remainingSpace;
        } else if (m_style.justifyContent == PrismJustifyUVE::SpaceBetween && m_children.size() > 1) {
            gapOffset += remainingSpace / static_cast<float>(m_children.size() - 1);
        }
    }

    for (std::size_t i = 0; i < m_children.size(); ++i) {
        auto& child = m_children[i];
        if (!child) continue;

        const Math::Vector2UVE desired = child->GetDesiredSize();
        float childMain = isRow ? desired.x : desired.y;
        float childCross = isRow ? desired.y : desired.x;

        if (totalFlexGrow > 0.0001F && child->GetStyle().flexGrow > 0.0F) {
            childMain += (child->GetStyle().flexGrow / totalFlexGrow) * remainingSpace;
        }

        float crossCursor = isRow ? contentY : contentX;
        if (m_style.alignItems == PrismAlignUVE::Stretch) {
            childCross = containerCross;
        } else if (m_style.alignItems == PrismAlignUVE::Center) {
            crossCursor += (containerCross - childCross) * 0.5F;
        } else if (m_style.alignItems == PrismAlignUVE::End) {
            crossCursor += (containerCross - childCross);
        }

        const Math::RectUVE childRect = isRow
            ? Math::RectUVE{{mainCursor, crossCursor}, {childMain, childCross}}
            : Math::RectUVE{{crossCursor, mainCursor}, {childCross, childMain}};

        child->ArrangeUVE(childRect);
        mainCursor += childMain + gapOffset;
    }
}

void PrismWidgetUVE::OnPaintContentUVE(PrismDrawListUVE& drawList) {
    if (m_style.backgroundColor.a > 0.0F) {
        drawList.AddRectFilled(m_layoutRect, m_style.backgroundColor, m_style.cornerRadii);
    }
    if (m_style.borderWidth > 0.0F && m_style.borderColor.a > 0.0F) {
        drawList.AddRectStroke(m_layoutRect, m_style.borderColor, m_style.borderWidth, m_style.cornerRadii);
    }
}

void PrismWidgetUVE::PaintUVE(PrismDrawListUVE& drawList) {
    if (m_layoutRect.size.x <= 0.0F || m_layoutRect.size.y <= 0.0F) {
        return;
    }

    OnPaintContentUVE(drawList);

    if (m_style.clipChildren) {
        const Math::RectIntUVE clipRect{
            Math::Vector2iUVE{static_cast<std::int32_t>(m_layoutRect.position.x),
                              static_cast<std::int32_t>(m_layoutRect.position.y)},
            Math::Vector2iUVE{static_cast<std::int32_t>(m_layoutRect.size.x),
                              static_cast<std::int32_t>(m_layoutRect.size.y)}
        };
        drawList.PushClipRect(clipRect);
    }

    for (auto& child : m_children) {
        if (child) {
            child->PaintUVE(drawList);
        }
    }

    if (m_style.clipChildren) {
        drawList.PopClipRect();
    }
}

void PrismWidgetUVE::RelinkChildrenUVE() {
    const std::weak_ptr<PrismWidgetUVE> self = weak_from_this();
    if (self.expired()) {
        return;
    }
    for (auto& child : m_children) {
        if (child && child->m_parent.expired()) {
            child->m_parent = self;
            child->RelinkChildrenUVE();
        }
    }
}

bool PrismWidgetUVE::HitTestUVE(Math::Vector2UVE screenPoint) const noexcept {
    return Math::ContainsUVE(m_layoutRect, screenPoint);
}

void PrismWidgetUVE::OnPointerEnter(const PrismPointerEventUVE&) {
    m_isHovered = true;
}

void PrismWidgetUVE::OnPointerLeave(const PrismPointerEventUVE&) {
    m_isHovered = false;
    m_isPressed = false;
}

PrismEventResultUVE PrismWidgetUVE::OnPointerMove(const PrismPointerEventUVE&) {
    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismWidgetUVE::OnPointerDown(const PrismPointerEventUVE& event) {
    if (event.button == Input::MouseButtonUVE::Left) {
        m_isPressed = true;
    }
    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismWidgetUVE::OnPointerUp(const PrismPointerEventUVE& event) {
    if (event.button == Input::MouseButtonUVE::Left) {
        m_isPressed = false;
    }
    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismWidgetUVE::OnScroll(const PrismScrollEventUVE&) {
    return PrismEventResultUVE::Unhandled;
}

void PrismWidgetUVE::OnFocusGained() {
    m_isFocused = true;
}

void PrismWidgetUVE::OnFocusLost() {
    m_isFocused = false;
}

PrismEventResultUVE PrismWidgetUVE::OnKeyDown(const PrismKeyEventUVE&) {
    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismWidgetUVE::OnKeyUp(const PrismKeyEventUVE&) {
    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismWidgetUVE::OnCharInput(char32_t) {
    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismWidgetUVE::OnDragOver(PrismDragDropEventUVE&) {
    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismWidgetUVE::OnDragDrop(const PrismDragDropEventUVE&) {
    return PrismEventResultUVE::Unhandled;
}

} // namespace UVE::UI::Prism
