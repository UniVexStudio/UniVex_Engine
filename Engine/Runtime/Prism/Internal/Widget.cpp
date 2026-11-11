// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Widget.h"
#include "prism/Application.h"

namespace prism {

Vec2 Widget::constrain(Vec2 v) const {
  if (m_fixedW >= 0) v.x = m_fixedW;
  if (m_fixedH >= 0) v.y = m_fixedH;
  v.x = std::clamp(v.x, m_minW, m_maxW);
  v.y = std::clamp(v.y, m_minH, m_maxH);
  return v;
}

Vec2 Widget::measure(Vec2 avail) {
  Vec2 d;
  for (auto& c : m_children)
    if (c->visible()) {
      Vec2 cd = c->measure(avail);
      d.x = std::max(d.x, cd.x);
      d.y = std::max(d.y, cd.y);
    }
  return m_desired = constrain(d);
}

void Widget::arrange(Rect r) {
  m_rect = r;
  for (auto& c : m_children)
    if (c->visible()) c->arrange(alignInCell(r.deflated(c->slot.pad), c->desired(), c->slot));
}

void Widget::setVisible(bool v) {
  if (m_visible == v) return;
  m_visible = v;
  relayout();
}

void Widget::addChild(Ref<Widget> c) {
  if (!c) return;
  if (c->m_parent) c->m_parent->removeChild(c.get());
  c->m_parent = this;
  m_children.push_back(std::move(c));
  relayout();
}

Widget::~Widget() {
  for (auto& c : m_children) {
    if (c && c->m_parent == this) c->m_parent = nullptr;
  }
}

void Widget::removeChild(const Widget* c) {
  for (auto it = m_children.begin(); it != m_children.end(); ++it)
    if (it->get() == c) {
      (*it)->m_parent = nullptr;
      m_children.erase(it);
      relayout();
      return;
    }
}

void Widget::clearChildren() {
  for (auto& c : m_children) c->m_parent = nullptr;
  m_children.clear();
  relayout();
}

void Widget::paint(Canvas& c) { paintChildren(c); }

void Widget::paintChildren(Canvas& c) {
  Rect clip = c.clip();
  for (auto& ch : m_children)
    if (ch->visible() && ch->m_rect.intersects(clip)) ch->paint(c);
}

Widget* Widget::hitTest(Vec2 p) {
  if (!m_visible || !m_rect.contains(p)) return nullptr;
  for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
    if (Widget* h = (*it)->hitTest(p)) return h;
  return hitSelf() ? this : nullptr;
}

void Widget::invalidate(InvalidateFlags flags) {
  m_invalidationFlags = m_invalidationFlags | flags;
  if (Application::exists()) {
    if (hasFlag(flags, InvalidateFlags::Layout)) Application::get().invalidateLayout();
    else Application::get().invalidate();
  }
}
void Widget::relayout() {
  invalidate(InvalidateFlags::Layout | InvalidateFlags::Paint);
}

Rect alignInCell(Rect cell, Vec2 ds, const Slot& s) {
  Rect r = cell;
  if (s.h != HAlign::Fill && ds.x < cell.w) {
    r.w = ds.x;
    if (s.h == HAlign::Center) r.x = cell.x + (cell.w - ds.x) * 0.5f;
    else if (s.h == HAlign::Right) r.x = cell.right() - ds.x;
  }
  if (s.v != VAlign::Fill && ds.y < cell.h) {
    r.h = ds.y;
    if (s.v == VAlign::Center) r.y = cell.y + (cell.h - ds.y) * 0.5f;
    else if (s.v == VAlign::Bottom) r.y = cell.bottom() - ds.y;
  }
  return r;
}

WidgetPath WidgetPath::build(Widget* leaf) {
  WidgetPath path;
  for (Widget* w = leaf; w; w = w->parent()) {
    path.widgets.insert(path.widgets.begin(), w);
  }
  return path;
}

} // namespace prism
