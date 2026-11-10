// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - TabView: a tab strip over switchable pages (docked-panel look).
// Tabs can be reordered and dragged between TabViews that share a non-empty `dockGroup`.
#pragma once
#include "Widget.h"

namespace prism {

class TabView : public Widget {
public:
  // A detached tab: everything needed to move it into another TabView.
  struct Page { std::string title; Icon icon = Icon::None; Ref<Widget> page; bool closable = false; };

  int addTab(std::string title, Icon icon, Ref<Widget> page, bool closable = false);
  Page takeTab(int i);                           // removes tab i and returns it (page stays alive)
  int insertTab(int index, Page p);              // inserts and selects; returns the final index
  int indexOf(const std::string& title) const;   // -1 if absent
  std::string titleAt(int i) const { return m_tabs[size_t(i)].title; }
  Rect tabRect(int i) const { return m_tabs[size_t(i)].rect; }
  void setCurrent(int i);
  int current() const { return m_cur; }
  int count() const { return int(m_tabs.size()); }
  void setStripExtras(Ref<Widget> w);            // widget docked to the right end of the strip

  enum class DockZone : uint8_t {
    None,
    Center,
    Left,
    Right,
    Top,
    Bottom
  };

  std::string dockGroup;                         // tabs may be dropped between views of the same non-empty group
  std::string emptyText = "Drop a tab here";
  Callback<int> onChange;
  Callback<int> onClose;
  std::function<void()> onTabsChanged;           // order/membership changed (reorder, move in or out)
  std::function<void(TabView* target, DockZone zone, Page page)> onDockSplit; // Split area on edge drop
  std::function<void(TabView* source, Page page, Vec2 pos)> onTabTorn;        // dropped on no dock target: float it

  Vec2 measure(Vec2) override { return m_desired = constrain({0, 0}); }
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  Widget* hitTest(Vec2 p) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  void onMouseLeave() override { m_hot = -1; invalidate(); }
  Cursor cursor() const override { return m_dragging ? Cursor::SizeAll : Cursor::Arrow; }

private:
  struct Tab { std::string title; Icon icon; Ref<Widget> page; bool closable; Rect rect; };
  struct DropTarget {
    TabView* view = nullptr;
    int index = -1;
    bool strip = false;
    DockZone zone = DockZone::None;
    Rect indicatorRect;
  };
  int tabAt(Vec2 p) const;
  DropTarget findDrop(Vec2 p);
  int insertionIndex(float x) const;
  float tabWidth(const Tab& t) const;
  void paintDrag(Canvas& c);
  void endDrag();
  Rect closeRect(const Tab& t) const { return {t.rect.right() - 22, t.rect.y + (t.rect.h - 16) * 0.5f + 1, 16, 16}; }
  std::vector<Tab> m_tabs;
  Ref<Widget> m_extras;
  int m_cur = 0, m_hot = -1;
  // drag state
  int m_pressTab = -1;
  Vec2 m_pressPos, m_dragPos;
  bool m_dragging = false;
  DropTarget m_drop;
};

} // namespace prism
