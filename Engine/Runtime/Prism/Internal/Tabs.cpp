// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Tabs.h"
#include "prism/Application.h"

namespace prism {

int TabView::addTab(std::string title, Icon icon, Ref<Widget> page, bool closable) {
  m_tabs.push_back({std::move(title), icon, page, closable, {}});
  addChild(std::move(page));
  return int(m_tabs.size()) - 1;
}

TabView::Page TabView::takeTab(int i) {
  if (i < 0 || i >= count()) return {};
  Tab t = std::move(m_tabs[size_t(i)]);
  m_tabs.erase(m_tabs.begin() + i);
  removeChild(t.page.get());
  if (m_cur > i || m_cur >= count()) m_cur = std::max(0, m_cur - 1);
  relayout();
  return {std::move(t.title), t.icon, std::move(t.page), t.closable};
}

int TabView::insertTab(int index, Page p) {
  index = std::clamp(index, 0, count());
  m_tabs.insert(m_tabs.begin() + index, Tab{std::move(p.title), p.icon, p.page, p.closable, {}});
  addChild(std::move(p.page));
  m_cur = index;
  relayout();
  return index;
}

int TabView::indexOf(const std::string& title) const {
  for (int i = 0; i < count(); i++) if (m_tabs[size_t(i)].title == title) return i;
  return -1;
}

void TabView::setStripExtras(Ref<Widget> w) {
  if (m_extras) removeChild(m_extras.get());
  m_extras = w;
  addChild(std::move(w));
}

void TabView::setCurrent(int i) {
  if (i < 0 || i >= count() || i == m_cur) return;
  m_cur = i;
  relayout();
  if (onChange) onChange(i);
}

void TabView::arrange(Rect r) {
  m_rect = r;
  const Theme& t = theme();
  float x = r.x;
  for (auto& tab : m_tabs) {
    float w = tabWidth(tab);
    tab.rect = {x, r.y, w, t.tabH};
    x += w;
  }
  if (m_extras) {
    Vec2 d = m_extras->measure({r.w, t.tabH});
    m_extras->arrange({r.right() - d.x - 4, r.y, d.x, t.tabH});
  }
  Rect body{r.x, r.y + t.tabH, r.w, std::max(0.f, r.h - t.tabH)};
  if (m_cur >= 0 && m_cur < count()) {
    m_tabs[size_t(m_cur)].page->measure(body.size());
    m_tabs[size_t(m_cur)].page->arrange(body);
  }
}

float TabView::tabWidth(const Tab& tab) const {
  return 14 + (tab.icon != Icon::None ? 22.f : 0.f) + std::ceil(theme().ui->width(tab.title)) + 14 + (tab.closable ? 20.f : 0.f);
}

int TabView::tabAt(Vec2 p) const {
  for (int i = 0; i < count(); i++) if (m_tabs[size_t(i)].rect.contains(p)) return i;
  return -1;
}

Widget* TabView::hitTest(Vec2 p) {
  if (!m_visible || !m_rect.contains(p)) return nullptr;
  if (m_extras && m_extras->visible())
    if (Widget* h = m_extras->hitTest(p)) return h;
  if (m_cur >= 0 && m_cur < count())
    if (Widget* h = m_tabs[size_t(m_cur)].page->hitTest(p)) return h;
  return this;
}

bool TabView::onMouseDown(const MouseEvent& e) {
  if (e.button != MouseButton::Left && e.button != MouseButton::Middle) return false;
  int i = tabAt(e.pos);
  if (i < 0) return false;
  const Tab& tab = m_tabs[size_t(i)];
  if (tab.closable && (e.button == MouseButton::Middle || closeRect(tab).contains(e.pos))) {
    if (onClose) onClose(i);
    return true;
  }
  setCurrent(i);
  if (e.button == MouseButton::Left && !dockGroup.empty()) { m_pressTab = i; m_pressPos = e.pos; }
  return true;
}

// ---- drag & drop of tabs
int TabView::insertionIndex(float x) const {
  int n = 0;
  for (auto& t : m_tabs) if (t.rect.x + t.rect.w * 0.5f < x) n++;
  return n;
}

TabView::DropTarget TabView::findDrop(Vec2 p) {
  DropTarget d;
  Widget* w = Application::get().widgetAt(p);
  TabView* tv = nullptr;
  for (Widget* x = w; x && !tv; x = x->parent()) tv = dynamic_cast<TabView*>(x);
  if (!tv || tv->dockGroup.empty() || tv->dockGroup != dockGroup) return d;
  d.view = tv;
  d.strip = p.y < tv->m_rect.y + theme().tabH;
  if (d.strip) {
    d.index = tv->insertionIndex(p.x);
    d.zone = DockZone::Center;
  } else {
    d.index = tv->count();
    Rect body{tv->m_rect.x, tv->m_rect.y + theme().tabH, tv->m_rect.w, std::max(0.0f, tv->m_rect.h - theme().tabH)};
    float relX = (p.x - body.x) / std::max(1.0f, body.w);
    float relY = (p.y - body.y) / std::max(1.0f, body.h);

    if (relX < 0.25f) {
      d.zone = DockZone::Left;
      d.indicatorRect = Rect{body.x, body.y, body.w * 0.5f, body.h};
    } else if (relX > 0.75f) {
      d.zone = DockZone::Right;
      d.indicatorRect = Rect{body.x + body.w * 0.5f, body.y, body.w * 0.5f, body.h};
    } else if (relY < 0.25f) {
      d.zone = DockZone::Top;
      d.indicatorRect = Rect{body.x, body.y, body.w, body.h * 0.5f};
    } else if (relY > 0.75f) {
      d.zone = DockZone::Bottom;
      d.indicatorRect = Rect{body.x, body.y + body.h * 0.5f, body.w, body.h * 0.5f};
    } else {
      d.zone = DockZone::Center;
      d.indicatorRect = body;
    }
  }
  return d;
}

bool TabView::onMouseMove(const MouseEvent& e) {
  if (m_pressTab >= 0 && !m_dragging && (e.pos - m_pressPos).length() > 6) {
    m_dragging = true;
    Application::get().setDragPainter([this](Canvas& c) { paintDrag(c); });
  }
  if (m_dragging) {
    m_dragPos = e.pos;
    m_drop = findDrop(e.pos);
    invalidate();
    return true;
  }
  int i = tabAt(e.pos);
  if (i != m_hot) { m_hot = i; invalidate(); }
  return false;
}

bool TabView::onMouseUp(const MouseEvent& e) {
  bool wasDragging = m_dragging;
  int from = m_pressTab;
  DropTarget d = m_drop;
  endDrag();
  if (wasDragging && from >= 0 && from < count() && !d.view && onTabTorn) {
    Page p = takeTab(from);
    onTabTorn(this, std::move(p), e.pos);
    if (onTabsChanged) onTabsChanged();
    Application::get().invalidateLayout();
    return true;
  }
  if (!wasDragging || from < 0 || from >= count() || !d.view) return false;
  TabView* dst = d.view;
  int idx = d.index;
  if (dst == this) {
    if (d.zone == DockZone::Center && (idx == from || idx == from + 1)) return true;     // dropped where it already is
    Page p = takeTab(from);
    if (d.zone != DockZone::Center && onDockSplit) {
      onDockSplit(dst, d.zone, std::move(p));
    } else {
      if (idx > from) idx--;
      insertTab(idx, std::move(p));
    }
  } else {
    Page p = takeTab(from);
    if (d.zone != DockZone::Center && onDockSplit) {
      onDockSplit(dst, d.zone, std::move(p));
    } else if (d.zone != DockZone::Center && dst->onDockSplit) {
      dst->onDockSplit(dst, d.zone, std::move(p));
    } else {
      dst->insertTab(idx, std::move(p));
    }
    if (onTabsChanged) onTabsChanged();
  }
  if (dst->onTabsChanged) dst->onTabsChanged();
  else if (dst == this && onTabsChanged) onTabsChanged();
  Application::get().invalidateLayout();
  return true;
}

void TabView::endDrag() {
  if (m_dragging) Application::get().setDragPainter({});
  m_dragging = false;
  m_pressTab = -1;
  m_drop = {};
}

void TabView::paintDrag(Canvas& c) {
  if (!m_dragging || m_pressTab < 0 || m_pressTab >= count()) return;
  const Theme& t = theme();
  // drop indicator
  if (m_drop.view) {
    TabView* v = m_drop.view;
    if (m_drop.strip) {
      float x = v->count() == 0 ? v->m_rect.x + 2 : (m_drop.index < v->count() ? v->m_tabs[size_t(m_drop.index)].rect.x : v->m_tabs.back().rect.right());
      c.fillRect({x - 1, v->m_rect.y + 3, 3, t.tabH - 6}, t.accentHover);
    } else {
      Rect highlight = m_drop.indicatorRect.w > 0 ? m_drop.indicatorRect : Rect{v->m_rect.x, v->m_rect.y + t.tabH, v->m_rect.w, v->m_rect.h - t.tabH};
      c.fillRect(highlight, t.accent.alpha(0.20f));
      c.strokeRect(highlight.deflated(1), t.accent, 2);
    }
  }
  // ghost
  const Tab& tab = m_tabs[size_t(m_pressTab)];
  float w = tabWidth(tab);
  Rect g{m_dragPos.x - w * 0.5f, m_dragPos.y - t.tabH * 0.5f, w, t.tabH};
  c.shadow(g, 3, 8, Color(0, 0, 0, 255));
  c.fillRoundRect(g, 3, t.header);
  c.strokeRoundRect(g, 3, m_drop.view ? t.accent : t.borderStrong);
  float x = g.x + 14;
  if (tab.icon != Icon::None) { drawIcon(c, tab.icon, {x, g.y + (g.h - 16) * 0.5f, 16, 16}, t.accentHover); x += 22; }
  c.textIn(*t.medium, {x, g.y, g.w - (x - g.x), g.h}, tab.title, t.textBright, HAlign::Left, false);
}

void TabView::paint(Canvas& c) {
  const Theme& t = theme();
  Rect strip{m_rect.x, m_rect.y, m_rect.w, t.tabH};
  c.fillRect(strip, t.header);
  c.fillRect({strip.x, strip.bottom() - 1, strip.w, 1}, t.border);
  c.fillRect({m_rect.x, strip.bottom(), m_rect.w, m_rect.h - t.tabH}, t.panel);
  for (int i = 0; i < count(); i++) {
    const Tab& tab = m_tabs[size_t(i)];
    bool cur = i == m_cur, hot = i == m_hot;
    Rect r = tab.rect;
    if (cur) {
      c.fillRect({r.x, r.y, r.w, r.h}, t.panel);
      c.fillRect({r.x, r.y, r.w, 2}, t.accent);
    } else {
      c.fillRect({r.right() - 1, r.y + 6, 1, r.h - 13}, t.borderSoft);
      if (hot) c.fillRect({r.x, r.y + 2, r.w - 1, r.h - 3}, Color(255, 255, 255, 8));
    }
    float x = r.x + 14;
    Color fg = cur ? t.textBright : (hot ? t.text : t.textDim);
    if (tab.icon != Icon::None) {
      drawIcon(c, tab.icon, {x, r.y + (r.h - 16) * 0.5f + 1, 16, 16}, cur ? t.accentHover : fg);
      x += 22;
    }
    float tw = std::ceil((cur ? *t.medium : *t.ui).width(tab.title));
    c.textIn(cur ? *t.medium : *t.ui, {x, r.y + 1, tw + 2, r.h - 1}, tab.title, fg, HAlign::Left, false);
    if (tab.closable) {
      Rect cr = closeRect(tab);
      bool over = hot && cr.contains(Application::get().mousePos());
      if (over) c.fillRoundRect(cr.deflated(1), 3, t.controlHover);
      drawIcon(c, Icon::Close, cr.deflated(2), over ? t.textBright : t.textDim, 1.3f);
    }
  }
  if (m_tabs.empty() && !emptyText.empty())
    c.textIn(*t.ui, {m_rect.x, m_rect.y + t.tabH, m_rect.w, std::max(0.f, m_rect.h - t.tabH)}, emptyText, t.textDisabled, HAlign::Center);
  if (m_extras) m_extras->paint(c);
  if (m_cur >= 0 && m_cur < count()) m_tabs[size_t(m_cur)].page->paint(c);
}

} // namespace prism
