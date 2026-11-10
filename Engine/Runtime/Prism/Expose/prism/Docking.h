// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Docking: edge-drop splitting of tab areas, floating panel windows, and a reusable floating window.
#pragma once
#include "Containers.h"
#include "Tabs.h"
#include <memory>
#include <vector>

namespace prism {

struct FloatingWindowOptions {
  std::string title = "Window";
  Icon icon = Icon::None;     // shown in the title tab
  bool shadeable = true;      // title bar gets a collapse button; double-click the bar also collapses
  Vec2 size{460, 320};
  Vec2 pos{-1, -1};          // top-left; negative = centred
  bool resizable = true;
  Vec2 minSize{240, 140};
  std::function<void()> onClose;
};

// PRISM engine title bar for any floating window: chamfered title tab (icon + name), hatched drag strip,
// optional shade + close buttons. `window` is the popup root being moved. Height = kWindowTitleBarH.
constexpr float kWindowTitleBarH = 30.0f;
Ref<Widget> makeWindowTitleBar(const std::string& title, Icon icon, bool shadeable, Widget* window,
                               std::function<void()> onClose, std::function<void()> onShade = nullptr);

// Shows `content` in a draggable, resizable, non-modal window. Returns its root (pass it to closeFloatingWindow).
Ref<Widget> showFloatingWindow(Ref<Widget> content, FloatingWindowOptions opt);
void closeFloatingWindow(Widget* root);

// Turns a set of TabViews into a dock space:
//  - dropping a tab on the left/right/top/bottom quarter of a view splits that area (new nested view),
//  - dropping a tab on nothing floats it in its own window,
//  - views / windows that become empty collapse automatically.
// Lifetime: the DockSpace must outlive the TabViews it was attached to (own it next to them).
class DockSpace {
public:
  using FrameFactory = std::function<Ref<Widget>(Ref<TabView>)>;   // wraps a new view the way static panels are framed

  explicit DockSpace(std::string group = "main") : m_group(std::move(group)) {}
  ~DockSpace();
  DockSpace(const DockSpace&) = delete;
  DockSpace& operator=(const DockSpace&) = delete;

  void setFrameFactory(FrameFactory f) { m_frame = std::move(f); }
  void attach(TabView& view);                  // a permanent dock area
  bool hasDynamic() const;                     // any split-created area or floating window alive
  void dissolve(TabView& fallback);            // return every dynamic page to `fallback`, close windows, collapse splits
  int floatingCount() const;
  std::function<void()> onChanged;             // structure or membership changed (e.g. save the layout)

  // Direct entry points (also used by the drag handlers).
  TabView* split(TabView* target, TabView::DockZone zone, TabView::Page page);
  TabView* floatPage(TabView::Page page, Vec2 pos, TabView* home);

private:
  struct Window { std::weak_ptr<Widget> root; std::weak_ptr<TabView> view; std::weak_ptr<TabView> home; };
  void wire(TabView& v);
  Ref<TabView> makeView();
  void scheduleSweep();
  void sweep();
  void collapse(TabView& v);
  std::string m_group;
  FrameFactory m_frame;
  std::vector<std::weak_ptr<TabView>> m_dyn, m_static;
  std::vector<Window> m_windows;
  TabView* m_forceHome = nullptr;
  int m_sweepTimer = 0;
};

} // namespace prism
