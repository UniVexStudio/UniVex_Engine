// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <memory>
#include <vector>
#include "uve/math/vector2_uve.h"
#include "uve/ui/prism/prism_events_uve.h"
#include "uve/ui/prism/prism_widget_uve.h"

namespace UVE::UI::Prism {

/// Dispatches mouse, keyboard, focus, and drag-drop events down the widget hierarchy.
/// Guarantees:
/// 1. Strict leaf-to-root event bubbling for unhandled input.
/// 2. Deterministic pointer capture for sliders, splitters, and scrollbars.
/// 3. Robust PointerEnter / PointerLeave lifecycle transitions without frame flicker.
/// 4. Focus navigation cycling (Tab / Shift-Tab).
class PrismEventRouterUVE final {
public:
    PrismEventRouterUVE() = default;

    /// Sets the root widget of the UI tree.
    void SetRoot(std::shared_ptr<PrismWidgetUVE> root);
    [[nodiscard]] std::shared_ptr<PrismWidgetUVE> GetRoot() const noexcept { return m_root; }

    // --- Pointer Routing ---
    void DispatchPointerMove(const PrismPointerEventUVE& event);
    void DispatchPointerDown(const PrismPointerEventUVE& event);
    void DispatchPointerUp(const PrismPointerEventUVE& event);
    void DispatchScroll(const PrismScrollEventUVE& event);

    // --- Keyboard & Focus Routing ---
    void DispatchKeyDown(const PrismKeyEventUVE& event);
    void DispatchKeyUp(const PrismKeyEventUVE& event);
    void DispatchCharInput(char32_t codepoint);

    // --- Drag and Drop Routing ---
    void DispatchDragOver(PrismDragDropEventUVE& event);
    void DispatchDragDrop(const PrismDragDropEventUVE& event);
    void CancelDrag();

    // --- State Queries & Manual Controls ---
    void SetFocus(const std::shared_ptr<PrismWidgetUVE>& widget);
    void ClearFocus();
    [[nodiscard]] std::shared_ptr<PrismWidgetUVE> GetFocusedWidget() const noexcept { return m_focusedWidget.lock(); }

    void CapturePointer(const std::shared_ptr<PrismWidgetUVE>& widget);
    void ReleasePointer();
    [[nodiscard]] std::shared_ptr<PrismWidgetUVE> GetCapturedWidget() const noexcept { return m_capturedWidget.lock(); }

    [[nodiscard]] std::shared_ptr<PrismWidgetUVE> GetHoveredWidget() const noexcept { return m_hoveredWidget.lock(); }

    /// Finds the deepest leaf widget that contains the given screen coordinates.
    [[nodiscard]] std::shared_ptr<PrismWidgetUVE> HitTestDeepest(Math::Vector2UVE screenPoint) const;

private:
    void UpdateHoveredChain(const std::shared_ptr<PrismWidgetUVE>& target, const PrismPointerEventUVE& event);
    bool BubblePointerEvent(const std::shared_ptr<PrismWidgetUVE>& target,
                            const PrismPointerEventUVE& event,
                            PrismEventResultUVE (PrismWidgetUVE::*handler)(const PrismPointerEventUVE&));

    std::shared_ptr<PrismWidgetUVE> m_root;
    std::weak_ptr<PrismWidgetUVE>   m_hoveredWidget;
    std::weak_ptr<PrismWidgetUVE>   m_capturedWidget;
    std::weak_ptr<PrismWidgetUVE>   m_focusedWidget;
};

} // namespace UVE::UI::Prism
