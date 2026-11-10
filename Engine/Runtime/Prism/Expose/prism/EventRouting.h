// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Event Routing: Preview (Tunnel) & Bubble phases, Reply, WidgetPath.
#pragma once
#include "Core.h"
#include <memory>
#include <vector>

namespace prism {

class Widget;

// Result of processing an input event, indicating whether it was consumed or unhandled.
class Reply {
public:
  static Reply handled() {
    Reply r;
    r.m_handled = true;
    return r;
  }

  static Reply unhandled() {
    Reply r;
    r.m_handled = false;
    return r;
  }

  bool isHandled() const { return m_handled; }
  explicit operator bool() const { return m_handled; }

  // Requests mouse capture
  Reply& captureMouse(Widget* w) {
    m_captureWidget = w;
    m_handled = true;
    return *this;
  }

  // Requests mouse release
  Reply& releaseMouse() {
    m_releaseMouse = true;
    m_handled = true;
    return *this;
  }

  // Requests keyboard focus transfer
  Reply& setFocus(Widget* w) {
    m_focusWidget = w;
    m_handled = true;
    return *this;
  }

  Widget* captureTarget() const { return m_captureWidget; }
  Widget* focusTarget() const { return m_focusWidget; }
  bool shouldReleaseMouse() const { return m_releaseMouse; }

private:
  bool m_handled = false;
  bool m_releaseMouse = false;
  Widget* m_captureWidget = nullptr;
  Widget* m_focusWidget = nullptr;
};

// Represents the hierarchical path from root down to a leaf widget.
struct WidgetPath {
  std::vector<Widget*> widgets; // widgets[0] is Root, widgets.back() is Leaf

  bool empty() const { return widgets.empty(); }
  size_t size() const { return widgets.size(); }
  Widget* leaf() const { return widgets.empty() ? nullptr : widgets.back(); }
  Widget* root() const { return widgets.empty() ? nullptr : widgets.front(); }

  bool contains(const Widget* w) const {
    for (auto* item : widgets) {
      if (item == w) return true;
    }
    return false;
  }

  // Construct path from leaf upwards to root
  static WidgetPath build(Widget* leaf);
};

} // namespace prism
