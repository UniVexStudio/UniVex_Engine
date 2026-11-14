// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - OS accessibility bridges. The semantic tree (Accessibility.h) is published to screen readers and UI
// automation by a platform bridge:
//   * AT-SPI (Linux)   - AtkBridge,  src/AccessibilityAtk.cpp  (PRISM_HAS_ATK; verified end-to-end with an AT-SPI client)
//   * UI Automation    - UiaBridge,  src/AccessibilityUia.cpp  (Windows; the host forwards WM_GETOBJECT)
// Every bridge sees the same view of the widget tree (AccessView) and the same events, so behaviour is identical.
#pragma once
#include "Accessibility.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace prism {

class Application;

class AccessibilityBridge {
public:
  virtual ~AccessibilityBridge() = default;
  virtual void notify(AccessEvent e, Widget* w) = 0;
  virtual void pump() {}                       // called every Application::tick (event-loop integration)
};

// The tree a bridge publishes: only meaningful widgets (role, name or focusable), structural containers hoisted away.
struct AccessView {
  static bool meaningful(const Widget* w);
  static Widget* parentOf(const Widget* w, const Widget* root);           // nearest meaningful ancestor (or root)
  static std::vector<Widget*> children(const Widget* w);                  // meaningful visible descendants, hoisted
  static int indexInParent(const Widget* w, const Widget* root);
  static std::vector<std::string> actions(const Widget* w);              // "press" / "toggle" / "focus"
  static bool doAction(Widget* w, int index);
};

#if defined(_WIN32)
// UI Automation: engines with their own WndProc call this first for every message (returns true when answered).
bool uiaHandleWindowMessage(void* hwnd, unsigned msg, uintptr_t wParam, intptr_t lParam, intptr_t* result);
#endif

// The bridge for this platform (null when the build has none, or the OS accessibility service is unavailable).
std::shared_ptr<AccessibilityBridge> makePlatformAccessibilityBridge(Application& app, const std::string& appName);

} // namespace prism
