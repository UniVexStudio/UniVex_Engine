// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - layout containers: Box (H/V stack), Overlay, Border (panel), Spacer, Splitter, ScrollView.
#pragma once
#include "Widget.h"

namespace prism {

// Linear stack. Children with slot.fill > 0 share the remaining main-axis space.
class Box : public Widget {
public:
  explicit Box(Orientation o, float spacing = 0) : m_orient(o), m_spacing(spacing) { }
  Box& add(Ref<Widget> w, Slot s = {});
  Box& setPadding(Margin m) { m_pad = m; relayout(); return *this; }
  Box& setSpacing(float s) { m_spacing = s; relayout(); return *this; }
  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  bool hitSelf() const override { return false; }
private:
  Orientation m_orient;
  float m_spacing;
  Margin m_pad;
};

inline Ref<Box> hbox(float spacing = 0) { return make<Box>(Orientation::Horizontal, spacing); }
inline Ref<Box> vbox(float spacing = 0) { return make<Box>(Orientation::Vertical, spacing); }

// Stacks all children on top of each other; each is aligned by its own slot.
class Overlay : public Widget {
public:
  Overlay& add(Ref<Widget> w, Slot s = {}) { w->slot = s; addChild(std::move(w)); return *this; }
  bool hitSelf() const override { return false; }
};

// One child with an optional background, border and padding. The workhorse "panel".
class Border : public Widget {
public:
  Border& setChild(Ref<Widget> w, Slot s = {});
  Border& setBackground(ThemeColor c) { m_bg = c; invalidate(); return *this; }
  Border& setBorder(ThemeColor c, Margin thickness = Margin(1)) { m_borderColor = c; m_border = thickness; relayout(); return *this; }
  Border& setPadding(Margin m) { m_pad = m; relayout(); return *this; }
  Border& setRadius(float r) { m_radius = r; invalidate(); return *this; }
  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  bool hitSelf() const override { return m_bg.get().a > 0; }
private:
  ThemeColor m_bg, m_borderColor;
  Margin m_border, m_pad;
  float m_radius = 0;
};

class Spacer : public Widget {
public:
  Spacer(float w = 0, float h = 0) : m_w(w), m_h(h) {}
  Vec2 measure(Vec2) override { return m_desired = constrain({m_w, m_h}); }
  bool hitSelf() const override { return false; }
private:
  float m_w, m_h;
};

// Docks panes side by side (or stacked) with draggable gaps. Exactly one pane flexes.
class Splitter : public Widget {
public:
  struct Pane { float size = 200, minSize = 80, maxSize = 1e9f; bool flex = false; };
  explicit Splitter(Orientation o) : m_orient(o) {}
  void add(Ref<Widget> w, Pane p);
  // Structure editing (used by docking): all keep the pane parameters in sync with the children.
  void insert(int index, Ref<Widget> w, Pane p);
  Ref<Widget> replace(const Widget* old, Ref<Widget> neu);   // `neu` takes the pane of `old`; returns old
  void removeChild(const Widget* c) override;
  int indexOf(const Widget* w) const;
  int paneCount() const { return int(m_panes.size()); }
  Pane* paneAt(int i) { return i >= 0 && i < int(m_panes.size()) ? &m_panes[size_t(i)] : nullptr; }
  Orientation orientation() const { return m_orient; }
  Vec2 measure(Vec2) override { return m_desired = constrain({0, 0}); }
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  Widget* hitTest(Vec2 p) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  Cursor cursor() const override;
private:
  int handleAt(Vec2 p) const;
  Orientation m_orient;
  std::vector<Pane> m_panes;
  std::vector<Widget*> m_kids;
  std::vector<Rect> m_handles;
  int m_drag = -1, m_dragPane = -1;
  float m_dragStart = 0, m_dragSize = 0, m_dragSign = 1;
};

// Shared vertical scrollbar behaviour (used by ScrollView and virtualised lists).
struct VScrollbar {
  static constexpr float W = 10;
  bool dragging = false;
  float dragOffset = 0;
  static bool needed(float contentH, float viewH) { return contentH > viewH + 0.5f; }
  Rect thumb(Rect view, float contentH, float scroll) const;
  void paint(Canvas& c, Rect view, float contentH, float scroll, bool hovered) const;
  bool mouseDown(Vec2 p, Rect view, float contentH, float& scroll);
  bool mouseMove(Vec2 p, Rect view, float contentH, float& scroll);
  void mouseUp() { dragging = false; }
};

class ScrollView : public Widget {
public:
  void setContent(Ref<Widget> w);
  Widget* content() const { return m_content.get(); }
  float scroll() const { return m_scroll; }
  void setScroll(float y) { m_scroll = y; relayout(); }
  void scrollToEnd() { m_toEnd = true; relayout(); }
  bool atBottom() const { return m_scroll >= m_contentH - m_rect.h - 1; }
  void ensureVisible(Rect absRect);
  Vec2 measure(Vec2) override { return m_desired = constrain({0, 0}); }
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  Widget* hitTest(Vec2 p) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  bool onMouseWheel(const MouseEvent& e) override;
private:
  bool overBar(Vec2 p) const { return VScrollbar::needed(m_contentH, m_rect.h) && p.x >= m_rect.right() - VScrollbar::W; }
  Ref<Widget> m_content;
  float m_scroll = 0, m_contentH = 0;
  bool m_toEnd = false;
  VScrollbar m_bar;
};

} // namespace prism
