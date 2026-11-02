// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_event_router_uve.h"

namespace UVE::UI::Prism {

void PrismEventRouterUVE::SetRoot(std::shared_ptr<PrismWidgetUVE> root) {
    if (m_root != root) {
        ClearFocus();
        ReleasePointer();
        m_hoveredWidget.reset();
        m_root = std::move(root);
        if (m_root) {
            m_root->RelinkChildrenUVE();
        }
    }
}

std::shared_ptr<PrismWidgetUVE> PrismEventRouterUVE::HitTestDeepest(Math::Vector2UVE screenPoint) const {
    if (!m_root || !m_root->HitTestUVE(screenPoint)) {
        return nullptr;
    }

    std::shared_ptr<PrismWidgetUVE> current = m_root;
    bool foundDeeper = true;

    while (foundDeeper) {
        foundDeeper = false;
        // Search in reverse order so topmost siblings receive input first
        const auto& children = current->GetChildren();
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            const auto& child = *it;
            if (child && child->HitTestUVE(screenPoint)) {
                current = child;
                foundDeeper = true;
                break;
            }
        }
    }

    return current;
}

void PrismEventRouterUVE::UpdateHoveredChain(const std::shared_ptr<PrismWidgetUVE>& target, const PrismPointerEventUVE& event) {
    const auto prevHovered = m_hoveredWidget.lock();
    if (prevHovered == target) {
        return;
    }

    // Leave previous widget
    if (prevHovered) {
        prevHovered->OnPointerLeave(event);
    }

    m_hoveredWidget = target;

    // Enter new widget
    if (target) {
        target->OnPointerEnter(event);
    }
}

bool PrismEventRouterUVE::BubblePointerEvent(const std::shared_ptr<PrismWidgetUVE>& target,
                                             const PrismPointerEventUVE& event,
                                             PrismEventResultUVE (PrismWidgetUVE::*handler)(const PrismPointerEventUVE&)) {
    auto current = target;
    while (current) {
        const PrismEventResultUVE res = (current.get()->*handler)(event);
        if (res == PrismEventResultUVE::Handled) {
            return true;
        }
        current = current->GetParent();
    }
    return false;
}

void PrismEventRouterUVE::DispatchPointerMove(const PrismPointerEventUVE& event) {
    auto captured = m_capturedWidget.lock();
    if (captured) {
        captured->OnPointerMove(event);
        return;
    }

    const auto target = HitTestDeepest(event.position);
    UpdateHoveredChain(target, event);

    if (target) {
        BubblePointerEvent(target, event, &PrismWidgetUVE::OnPointerMove);
    }
}

void PrismEventRouterUVE::DispatchPointerDown(const PrismPointerEventUVE& event) {
    auto captured = m_capturedWidget.lock();
    if (captured) {
        captured->OnPointerDown(event);
        return;
    }

    const auto target = HitTestDeepest(event.position);
    // A press is also a pointer position: hover must follow it, otherwise a click delivered without
    // a preceding move (touch, synthetic/test input) never sees its widget as hovered.
    UpdateHoveredChain(target, event);

    // Update focus if clicking on a focusable widget or its children
    auto focusCandidate = target;
    while (focusCandidate && !focusCandidate->IsFocusable()) {
        focusCandidate = focusCandidate->GetParent();
    }
    SetFocus(focusCandidate);

    if (target) {
        BubblePointerEvent(target, event, &PrismWidgetUVE::OnPointerDown);
    }
}

void PrismEventRouterUVE::DispatchPointerUp(const PrismPointerEventUVE& event) {
    auto captured = m_capturedWidget.lock();
    if (captured) {
        captured->OnPointerUp(event);
        return;
    }

    const auto target = HitTestDeepest(event.position);
    UpdateHoveredChain(target, event);
    if (target) {
        BubblePointerEvent(target, event, &PrismWidgetUVE::OnPointerUp);
    }
}

void PrismEventRouterUVE::DispatchScroll(const PrismScrollEventUVE& event) {
    const auto target = HitTestDeepest(event.position);
    auto current = target;
    while (current) {
        const PrismEventResultUVE res = current->OnScroll(event);
        if (res == PrismEventResultUVE::Handled) {
            return;
        }
        current = current->GetParent();
    }
}

void PrismEventRouterUVE::DispatchKeyDown(const PrismKeyEventUVE& event) {
    auto focused = m_focusedWidget.lock();
    auto current = focused ? focused : m_root;
    while (current) {
        const PrismEventResultUVE res = current->OnKeyDown(event);
        if (res == PrismEventResultUVE::Handled) {
            return;
        }
        current = current->GetParent();
    }
}

void PrismEventRouterUVE::DispatchKeyUp(const PrismKeyEventUVE& event) {
    auto focused = m_focusedWidget.lock();
    auto current = focused ? focused : m_root;
    while (current) {
        const PrismEventResultUVE res = current->OnKeyUp(event);
        if (res == PrismEventResultUVE::Handled) {
            return;
        }
        current = current->GetParent();
    }
}

void PrismEventRouterUVE::DispatchCharInput(char32_t codepoint) {
    auto focused = m_focusedWidget.lock();
    auto current = focused ? focused : m_root;
    while (current) {
        const PrismEventResultUVE res = current->OnCharInput(codepoint);
        if (res == PrismEventResultUVE::Handled) {
            return;
        }
        current = current->GetParent();
    }
}

void PrismEventRouterUVE::DispatchDragOver(PrismDragDropEventUVE& event) {
    const auto target = HitTestDeepest(event.position);
    auto current = target;
    while (current) {
        const PrismEventResultUVE res = current->OnDragOver(event);
        if (res == PrismEventResultUVE::Handled || event.accepted) {
            return;
        }
        current = current->GetParent();
    }
}

void PrismEventRouterUVE::DispatchDragDrop(const PrismDragDropEventUVE& event) {
    const auto target = HitTestDeepest(event.position);
    auto current = target;
    while (current) {
        const PrismEventResultUVE res = current->OnDragDrop(event);
        if (res == PrismEventResultUVE::Handled) {
            return;
        }
        current = current->GetParent();
    }
}

void PrismEventRouterUVE::CancelDrag() {
    ReleasePointer();
}

void PrismEventRouterUVE::SetFocus(const std::shared_ptr<PrismWidgetUVE>& widget) {
    const auto prevFocus = m_focusedWidget.lock();
    if (prevFocus == widget) {
        return;
    }

    if (prevFocus) {
        prevFocus->OnFocusLost();
    }

    m_focusedWidget = widget;

    if (widget) {
        widget->OnFocusGained();
    }
}

void PrismEventRouterUVE::ClearFocus() {
    SetFocus(nullptr);
}

void PrismEventRouterUVE::CapturePointer(const std::shared_ptr<PrismWidgetUVE>& widget) {
    m_capturedWidget = widget;
}

void PrismEventRouterUVE::ReleasePointer() {
    m_capturedWidget.reset();
}

} // namespace UVE::UI::Prism
