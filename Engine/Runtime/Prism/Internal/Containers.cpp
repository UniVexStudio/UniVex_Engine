// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include <algorithm>
#include "prism/Containers.h"
#include "prism/Application.h"

namespace prism {

// ------------------------------------------------------------------ Box
Box& Box::add(Ref<Widget> w, Slot s) {
  w->slot = s;
  addChild(std::move(w));
  return *this;
}

Vec2 Box::measure(Vec2 avail) {
  bool v = m_orient == Orientation::Vertical;
  float main = 0, cross = 0;
  int n = 0;
  Vec2 in = {avail.x - m_pad.horizontal(), avail.y - m_pad.vertical()};
  for (auto& c : m_children) {
    if (!c->visible()) continue;
    const Margin& sp = c->slot.pad;
    Vec2 ds = c->measure({in.x - sp.horizontal(), in.y - sp.vertical()});
    main += (v ? ds.y + sp.vertical() : ds.x + sp.horizontal());
    cross = std::max(cross, v ? ds.x + sp.horizontal() : ds.y + sp.vertical());
    n++;
  }
  if (n > 1) main += m_spacing * float(n - 1);
  Vec2 d = v ? Vec2(cross + m_pad.horizontal(), main + m_pad.vertical()) : Vec2(main + m_pad.horizontal(), cross + m_pad.vertical());
  return m_desired = constrain(d);
}

void Box::arrange(Rect r) {
  m_rect = r;
  bool v = m_orient == Orientation::Vertical;
  Rect in = r.deflated(m_pad);
  std::vector<Widget*> kids;
  float used = 0, fillSum = 0;
  for (auto& c : m_children) {
    if (!c->visible()) continue;
    kids.push_back(c.get());
    float padMain = v ? c->slot.pad.vertical() : c->slot.pad.horizontal();
    if (c->slot.fill > 0) { fillSum += c->slot.fill; used += padMain; }
    else used += (v ? c->desired().y : c->desired().x) + padMain;
  }
  if (kids.size() > 1) used += m_spacing * float(kids.size() - 1);
  float remaining = std::max(0.f, (v ? in.h : in.w) - used);
  float pos = v ? in.y : in.x;
  for (Widget* c : kids) {
    const Slot& s = c->slot;
    Vec2 ds = c->desired();
    float mainSize = s.fill > 0 ? std::floor(remaining * s.fill / fillSum) : (v ? ds.y : ds.x);
    Rect cell = v ? Rect(in.x, pos, in.w, mainSize + s.pad.vertical()) : Rect(pos, in.y, mainSize + s.pad.horizontal(), in.h);
    Rect inner = cell.deflated(s.pad);
    Vec2 dsA = v ? Vec2(ds.x, inner.h) : Vec2(inner.w, ds.y);
    c->arrange(alignInCell(inner, dsA, s));
    pos += mainSize + (v ? s.pad.vertical() : s.pad.horizontal()) + m_spacing;
  }
}

// ------------------------------------------------------------------ Border
Border& Border::setChild(Ref<Widget> w, Slot s) {
  clearChildren();
  w->slot = s;
  addChild(std::move(w));
  return *this;
}

Vec2 Border::measure(Vec2 avail) {
  Vec2 cd;
  float ex = m_border.horizontal() + m_pad.horizontal(), ey = m_border.vertical() + m_pad.vertical();
  for (auto& c : m_children)
    if (c->visible()) {
      const Margin& sp = c->slot.pad;
      cd = c->measure({avail.x - ex - sp.horizontal(), avail.y - ey - sp.vertical()});
      cd = {cd.x + sp.horizontal(), cd.y + sp.vertical()};
    }
  return m_desired = constrain({cd.x + ex, cd.y + ey});
}

void Border::arrange(Rect r) {
  m_rect = r;
  Rect in = r.deflated(m_border).deflated(m_pad);
  for (auto& c : m_children)
    if (c->visible()) c->arrange(alignInCell(in.deflated(c->slot.pad), c->desired(), c->slot));
}

void Border::paint(Canvas& c) {
  Rect r = m_rect;
  const Color bg = m_bg.get(), bc = m_borderColor.get();
  if (m_radius > 0) {
    if (bg.a) c.fillRoundRect(r, m_radius, bg);
    if (bc.a && m_border.l > 0) c.strokeRoundRect(r, m_radius, bc, int(m_border.l));
  } else {
    if (bg.a) c.fillRect(r, bg);
    if (bc.a) {
      if (m_border.t > 0) c.fillRect({r.x, r.y, r.w, m_border.t}, bc);
      if (m_border.b > 0) c.fillRect({r.x, r.bottom() - m_border.b, r.w, m_border.b}, bc);
      if (m_border.l > 0) c.fillRect({r.x, r.y + m_border.t, m_border.l, r.h - m_border.t - m_border.b}, bc);
      if (m_border.r > 0) c.fillRect({r.right() - m_border.r, r.y + m_border.t, m_border.r, r.h - m_border.t - m_border.b}, bc);
    }
  }
  paintChildren(c);
}

// ------------------------------------------------------------------ Splitter
void Splitter::add(Ref<Widget> w, Pane p) {
  m_panes.push_back(p);
  addChild(std::move(w));
}

int Splitter::indexOf(const Widget* w) const {
  for (size_t i = 0; i < m_children.size(); i++) if (m_children[i].get() == w) return int(i);
  return -1;
}

void Splitter::insert(int index, Ref<Widget> w, Pane p) {
  add(std::move(w), p);
  size_t n = m_children.size();
  size_t at = size_t(std::clamp(index, 0, int(n) - 1));
  std::rotate(m_children.begin() + long(at), m_children.end() - 1, m_children.end());
  std::rotate(m_panes.begin() + long(at), m_panes.end() - 1, m_panes.end());
  relayout();
}

void Splitter::removeChild(const Widget* c) {
  int i = indexOf(c);
  if (i < 0) return;
  m_panes.erase(m_panes.begin() + i);
  Widget::removeChild(c);
}

Ref<Widget> Splitter::replace(const Widget* old, Ref<Widget> neu) {
  int i = indexOf(old);
  if (i < 0 || !neu) return nullptr;
  Ref<Widget> keep = m_children[size_t(i)];
  Pane pn = m_panes[size_t(i)];
  insert(i, std::move(neu), pn);          // new child goes in front of the old one...
  removeChild(keep.get());                // ...then the old one leaves
  return keep;
}

void Splitter::arrange(Rect r) {
  m_rect = r;
  bool h = m_orient == Orientation::Horizontal;
  m_kids.clear();
  std::vector<Pane*> panes;
  for (size_t i = 0; i < m_children.size(); i++)
    if (m_children[i]->visible()) { m_kids.push_back(m_children[i].get()); panes.push_back(&m_panes[i]); }
  size_t n = m_kids.size();
  m_handles.assign(n > 0 ? n - 1 : 0, {});
  if (!n) return;
  size_t flex = n - 1;
  for (size_t i = 0; i < n; i++) if (panes[i]->flex) { flex = i; break; }
  float gap = theme().splitter, total = h ? r.w : r.h;
  float avail = total - gap * float(n - 1), fixedSum = 0;
  for (size_t i = 0; i < n; i++)
    if (i != flex) { panes[i]->size = std::clamp(panes[i]->size, panes[i]->minSize, panes[i]->maxSize); fixedSum += panes[i]->size; }
  float flexSize = avail - fixedSum;
  if (flexSize < panes[flex]->minSize) {
    float deficit = panes[flex]->minSize - flexSize;
    for (size_t i = 0; i < n && deficit > 0; i++)
      if (i != flex) {
        float give = std::min(deficit, panes[i]->size - panes[i]->minSize);
        if (give > 0) { panes[i]->size -= give; deficit -= give; }
      }
    flexSize = panes[flex]->minSize;
  }
  float pos = h ? r.x : r.y;
  for (size_t i = 0; i < n; i++) {
    float sz = i == flex ? flexSize : panes[i]->size;
    Rect cell = h ? Rect(pos, r.y, sz, r.h) : Rect(r.x, pos, r.w, sz);
    m_kids[i]->measure(cell.size());
    m_kids[i]->arrange(cell);
    pos += sz;
    if (i + 1 < n) { m_handles[i] = h ? Rect(pos, r.y, gap, r.h) : Rect(r.x, pos, r.w, gap); pos += gap; }
  }
}

int Splitter::handleAt(Vec2 p) const {
  for (size_t i = 0; i < m_handles.size(); i++)
    if (m_handles[i].inflated(2).contains(p)) return int(i);
  return -1;
}

Widget* Splitter::hitTest(Vec2 p) {
  if (!m_visible || !m_rect.contains(p)) return nullptr;
  if (handleAt(p) >= 0) return this;
  for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
    if (Widget* w = (*it)->hitTest(p)) return w;
  return nullptr;
}

Cursor Splitter::cursor() const {
  if (m_drag >= 0 || handleAt(Application::get().mousePos()) >= 0)
    return m_orient == Orientation::Horizontal ? Cursor::SizeWE : Cursor::SizeNS;
  return Cursor::Arrow;
}

bool Splitter::onMouseDown(const MouseEvent& e) {
  if (e.button != MouseButton::Left) return false;
  int i = handleAt(e.pos);
  if (i < 0) return false;
  // find the pane indices (in visible order) on either side of the handle
  std::vector<Pane*> panes;
  for (size_t k = 0; k < m_children.size(); k++) if (m_children[k]->visible()) panes.push_back(&m_panes[k]);
  size_t n = panes.size(), flex = n - 1;
  for (size_t k = 0; k < n; k++) if (panes[k]->flex) { flex = k; break; }
  if (size_t(i) != flex) { m_dragPane = i; m_dragSign = 1; }
  else if (size_t(i + 1) != flex) { m_dragPane = i + 1; m_dragSign = -1; }
  else return true;
  m_drag = i;
  m_dragStart = m_orient == Orientation::Horizontal ? e.pos.x : e.pos.y;
  m_dragSize = panes[size_t(m_dragPane)]->size;
  invalidate();
  return true;
}

bool Splitter::onMouseMove(const MouseEvent& e) {
  if (m_drag < 0) return false;
  std::vector<Pane*> panes;
  for (size_t k = 0; k < m_children.size(); k++) if (m_children[k]->visible()) panes.push_back(&m_panes[k]);
  size_t n = panes.size(), flex = n - 1;
  for (size_t k = 0; k < n; k++) if (panes[k]->flex) { flex = k; break; }
  float d = (m_orient == Orientation::Horizontal ? e.pos.x : e.pos.y) - m_dragStart;
  Pane& p = *panes[size_t(m_dragPane)];
  float total = m_orient == Orientation::Horizontal ? m_rect.w : m_rect.h;
  float others = theme().splitter * float(n - 1) + panes[flex]->minSize;
  for (size_t k = 0; k < n; k++) if (k != flex && int(k) != m_dragPane) others += panes[k]->size;
  p.size = std::clamp(m_dragSize + m_dragSign * d, p.minSize, std::min(p.maxSize, total - others));
  relayout();
  return true;
}

bool Splitter::onMouseUp(const MouseEvent&) {
  m_drag = -1;
  invalidate();
  return true;
}

void Splitter::paint(Canvas& c) {
  const Theme& t = theme();
  for (size_t i = 0; i < m_handles.size(); i++) {
    c.fillRect(m_handles[i], t.window);
    bool active = int(i) == m_drag || (m_hovered && handleAt(Application::get().mousePos()) == int(i));
    if (active) {
      Rect h = m_handles[i];
      if (m_orient == Orientation::Horizontal) c.fillRect({h.x + 1, h.y, h.w - 2, h.h}, t.accent.alpha(m_drag >= 0 ? 0.9f : 0.6f));
      else c.fillRect({h.x, h.y + 1, h.w, h.h - 2}, t.accent.alpha(m_drag >= 0 ? 0.9f : 0.6f));
    }
  }
  paintChildren(c);
}

// ------------------------------------------------------------------ VScrollbar
Rect VScrollbar::thumb(Rect view, float contentH, float scroll) const {
  float ratio = view.h / contentH;
  float th = std::max(28.f, view.h * ratio);
  float travel = view.h - th, maxScroll = contentH - view.h;
  float y = view.y + (maxScroll > 0 ? scroll / maxScroll * travel : 0);
  return {view.right() - W + 2, std::floor(y), W - 4, std::floor(th)};
}

void VScrollbar::paint(Canvas& c, Rect view, float contentH, float scroll, bool hovered) const {
  if (!needed(contentH, view.h)) return;
  Rect t = thumb(view, contentH, scroll);
  float a = dragging ? 0.42f : (hovered ? 0.30f : 0.16f);
  c.fillRoundRect(t, 3, Color(255, 255, 255, uint8_t(255 * a)));
}

bool VScrollbar::mouseDown(Vec2 p, Rect view, float contentH, float& scroll) {
  Rect t = thumb(view, contentH, scroll);
  if (p.y >= t.y && p.y < t.bottom()) { dragging = true; dragOffset = p.y - t.y; return true; }
  scroll += (p.y < t.y ? -1.f : 1.f) * view.h * 0.9f;
  return true;
}

bool VScrollbar::mouseMove(Vec2 p, Rect view, float contentH, float& scroll) {
  if (!dragging) return false;
  Rect t = thumb(view, contentH, scroll);
  float travel = view.h - t.h, maxScroll = contentH - view.h;
  if (travel <= 0) return true;
  scroll = (p.y - dragOffset - view.y) / travel * maxScroll;
  return true;
}

// ------------------------------------------------------------------ ScrollView
void ScrollView::setContent(Ref<Widget> w) {
  clearChildren();
  m_content = w;
  addChild(std::move(w));
}

void ScrollView::arrange(Rect r) {
  m_rect = r;
  if (!m_content) return;
  Vec2 cd = m_content->measure({r.w, 1e6f});
  float w = r.w;
  if (VScrollbar::needed(cd.y, r.h)) { w = r.w - VScrollbar::W; cd = m_content->measure({w, 1e6f}); }
  m_contentH = std::max(cd.y, r.h);
  if (m_toEnd) { m_scroll = m_contentH; m_toEnd = false; }
  m_scroll = std::clamp(m_scroll, 0.f, std::max(0.f, m_contentH - r.h));
  m_content->arrange({r.x, std::floor(r.y - m_scroll), w, m_contentH});
}

void ScrollView::paint(Canvas& c) {
  c.pushClip(m_rect);
  if (m_content) m_content->paint(c);
  c.popClip();
  m_bar.paint(c, m_rect, m_contentH, m_scroll, m_hovered && Application::get().mousePos().x >= m_rect.right() - VScrollbar::W);
}

Widget* ScrollView::hitTest(Vec2 p) {
  if (!m_visible || !m_rect.contains(p)) return nullptr;
  if (overBar(p)) return this;
  if (m_content)
    if (Widget* h = m_content->hitTest(p)) return h;
  return this;
}

bool ScrollView::onMouseDown(const MouseEvent& e) {
  if (e.button == MouseButton::Left && overBar(e.pos)) {
    m_bar.mouseDown(e.pos, m_rect, m_contentH, m_scroll);
    relayout();
    return true;
  }
  return false;
}

bool ScrollView::onMouseMove(const MouseEvent& e) {
  if (m_bar.dragging && m_bar.mouseMove(e.pos, m_rect, m_contentH, m_scroll)) { relayout(); return true; }
  return false;
}

bool ScrollView::onMouseUp(const MouseEvent&) {
  bool was = m_bar.dragging;
  m_bar.mouseUp();
  return was;
}

bool ScrollView::onMouseWheel(const MouseEvent& e) {
  if (!VScrollbar::needed(m_contentH, m_rect.h)) return false;
  m_scroll -= e.wheel.y * 44.f;
  relayout();
  return true;
}

void ScrollView::ensureVisible(Rect a) {
  if (a.y < m_rect.y) m_scroll -= m_rect.y - a.y;
  else if (a.bottom() > m_rect.bottom()) m_scroll += a.bottom() - m_rect.bottom();
  relayout();
}

} // namespace prism
