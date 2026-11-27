// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Docking.h"
#include "prism/Application.h"
#include "prism/Layouts.h"
#include <algorithm>

namespace prism {

namespace {

// PRISM floating-window title bar: a chamfered title tab (icon + name) on the left, a hatched drag area,
// and shade / close buttons on the right. Drag moves the window, double-click shades (collapses) it.
class WindowTitleBar : public Widget {
public:
  WindowTitleBar(std::string title, Icon icon, bool shadeable, std::function<void()> onClose,
                 std::function<void(Vec2)> onMove, std::function<void()> onShade)
      : m_title(std::move(title)), m_icon(icon), m_shadeable(shadeable), m_close(std::move(onClose)),
        m_move(std::move(onMove)), m_shade(std::move(onShade)) {}
  Widget* window = nullptr;
  bool shaded = false;
  static constexpr float kH = 30.0f;
  Vec2 measure(Vec2 avail) override { return m_desired = constrain(Vec2(avail.x, kH)); }
  void paint(Canvas& c) override {
    const Theme& t = theme();
    c.fillRect(m_rect, t.header);
    c.fillRect(Rect{m_rect.x, m_rect.bottom() - 1.0f, m_rect.w, 1.0f}, t.border);
    // drag-area hatch
    Rect hatch{tabRight() + 14.0f, m_rect.y + 6.0f, std::max(0.0f, btnLeft() - tabRight() - 22.0f), m_rect.h - 12.0f};
    if (hatch.w > 4) {
      c.pushClip(hatch);
      for (float x = hatch.x - hatch.h; x < hatch.right(); x += 6.0f)
        c.line(Vec2(x, hatch.bottom()), Vec2(x + hatch.h, hatch.y), Color(255, 255, 255, 12), 1.0f);
      c.popClip();
    }
    // chamfered title tab
    float tw = tabRight() - m_rect.x;
    Vec2 poly[4] = {Vec2(m_rect.x, m_rect.y), Vec2(m_rect.x + tw, m_rect.y), Vec2(m_rect.x + tw + 9.0f, m_rect.bottom() - 1.0f), Vec2(m_rect.x, m_rect.bottom() - 1.0f)};
    c.fillPolygon(std::span<const Vec2>(poly, 4), t.highlight.alpha(0.10f), false);
    c.fillRect(Rect{m_rect.x, m_rect.bottom() - 3.0f, tw + 6.0f, 2.0f}, t.highlight);
    float x = m_rect.x + 12.0f;
    if (m_icon != Icon::None) { drawIcon(c, m_icon, Rect{x, m_rect.y + 7.0f, 16, 16}, t.highlight); x += 22.0f; }
    c.textIn(*t.semibold, Rect{x, m_rect.y, tw, m_rect.h - 2.0f}, m_title, t.textBright, HAlign::Left, false);
    // buttons
    if (m_shadeable) drawBtn(c, shadeRect(), shaded ? Icon::TriRight : Icon::TriDown, m_hover == 1, false);
    drawBtn(c, closeRect(), Icon::Close, m_hover == 2, true);
  }
  bool onMouseDown(const MouseEvent& e) override {
    if (e.button != MouseButton::Left) return false;
    if (closeRect().contains(e.pos)) { if (m_close) m_close(); return true; }
    if (m_shadeable && shadeRect().contains(e.pos)) { if (m_shade) m_shade(); return true; }
    if (e.clicks >= 2 && m_shadeable) { if (m_shade) m_shade(); return true; }
    m_dragging = true;
    m_grab = e.pos - (window ? Vec2(window->rect().x, window->rect().y) : Vec2(m_rect.x, m_rect.y));
    return true;
  }
  bool onMouseMove(const MouseEvent& e) override {
    int h = closeRect().contains(e.pos) ? 2 : (m_shadeable && shadeRect().contains(e.pos) ? 1 : 0);
    if (h != m_hover) { m_hover = h; invalidate(); }
    if (m_dragging && m_move) m_move(e.pos - m_grab);
    return m_dragging;
  }
  bool onMouseUp(const MouseEvent&) override {
    const bool was = m_dragging;
    m_dragging = false;
    return was;
  }
  void onMouseLeave() override { m_hover = 0; invalidate(); }
  Cursor cursor() const override { return Cursor::SizeAll; }
private:
  float tabRight() const {
    const Theme& t = theme();
    return m_rect.x + 12.0f + (m_icon != Icon::None ? 22.0f : 0.0f) + std::ceil(t.semibold->width(m_title)) + 16.0f;
  }
  float btnLeft() const { return (m_shadeable ? shadeRect() : closeRect()).x; }
  Rect closeRect() const { return Rect{m_rect.right() - 32.0f, m_rect.y + 4.0f, 28.0f, 20.0f}; }
  Rect shadeRect() const { return Rect{m_rect.right() - 62.0f, m_rect.y + 4.0f, 28.0f, 20.0f}; }
  void drawBtn(Canvas& c, Rect r, Icon icon, bool hot, bool danger) {
    const Theme& t = theme();
    if (hot) {   // chamfered hover plate
      Vec2 q[4] = {Vec2(r.x + 4, r.y), Vec2(r.right(), r.y), Vec2(r.right() - 4, r.bottom()), Vec2(r.x, r.bottom())};
      c.fillPolygon(std::span<const Vec2>(q, 4), danger ? Color(206, 64, 64) : t.controlHover, false);
    }
    drawIcon(c, icon, r.inflated(-5.0f), hot ? Color(255, 255, 255) : t.textDim, 1.3f);
  }
  std::string m_title;
  Icon m_icon;
  bool m_shadeable;
  std::function<void()> m_close;
  std::function<void(Vec2)> m_move;
  std::function<void()> m_shade;
  bool m_dragging = false;
  int m_hover = 0;
  Vec2 m_grab;
public:
  void setShadeHandler(std::function<void()> f) { m_shade = std::move(f); }
};

} // namespace

uint8_t ResizablePopupFrame::hitEdges(Vec2 p) const {
  if (!resizable || m_rect.w <= 0.f || m_rect.h <= 0.f) return 0;
  const bool touch = Application::exists() && Application::get().platform().singleSurface();
  const float edge = touch ? 14.f : 6.f;
  const float corner = edge * 1.5f;
  if (!m_rect.contains(p)) return 0;
  const float dl = p.x - m_rect.x;
  const float dr = m_rect.right() - p.x;
  const float dt = p.y - m_rect.y;
  const float db = m_rect.bottom() - p.y;
  uint8_t e = 0;
  if (dl <= edge) e |= 1;
  else if (dr <= edge) e |= 2;
  if (dt <= edge) e |= 4;
  else if (db <= edge) e |= 8;
  // Widen corner hit zones so diagonal grabs feel effortless.
  if (e != 0) {
    if ((e & (4 | 8)) != 0) {
      if (dl <= corner) e |= 1;
      else if (dr <= corner) e |= 2;
    }
    if ((e & (1 | 2)) != 0) {
      if (dt <= corner) e |= 4;
      else if (db <= corner) e |= 8;
    }
  }
  return e;
}

Reply ResizablePopupFrame::onPreviewMouseDown(const MouseEvent& e) {
  if (e.button != MouseButton::Left || !resizable) return Reply::unhandled();
  const uint8_t edges = hitEdges(e.pos);
  if (edges == 0) return Reply::unhandled();
  m_edges = edges;
  m_hoverEdges = edges;
  m_dragStart = e.pos;
  m_startRect = m_rect;
  invalidate();
  return Reply::handled().captureMouse(this);
}

Reply ResizablePopupFrame::onPreviewMouseMove(const MouseEvent& e) {
  if (m_edges != 0) return Reply::unhandled();
  const uint8_t hovered = hitEdges(e.pos);
  if (hovered != m_hoverEdges) {
    m_hoverEdges = hovered;
    invalidate();
  }
  return Reply::unhandled();
}

bool ResizablePopupFrame::onMouseMove(const MouseEvent& e) {
  if (m_edges == 0) {
    const uint8_t hovered = hitEdges(e.pos);
    if (hovered != m_hoverEdges) {
      m_hoverEdges = hovered;
      invalidate();
    }
    return false;
  }
  const float dx = e.pos.x - m_dragStart.x;
  const float dy = e.pos.y - m_dragStart.y;
  Rect r = m_startRect;
  const Vec2 ws = Application::exists() ? Application::get().windowSize() : Vec2{1920.f, 1080.f};
  const float maxW = std::max(minSize.x, ws.x - 8.f);
  const float maxH = std::max(minSize.y, ws.y - 8.f);
  if ((m_edges & 1) != 0) {
    const float nw = std::clamp(m_startRect.w - dx, minSize.x, maxW);
    r.x = m_startRect.right() - nw;
    r.w = nw;
  }
  if ((m_edges & 2) != 0) {
    r.w = std::clamp(m_startRect.w + dx, minSize.x, std::max(minSize.x, ws.x - r.x - 4.f));
  }
  if ((m_edges & 4) != 0) {
    const float nh = std::clamp(m_startRect.h - dy, minSize.y, maxH);
    r.y = m_startRect.bottom() - nh;
    r.h = nh;
  }
  if ((m_edges & 8) != 0) {
    r.h = std::clamp(m_startRect.h + dy, minSize.y, std::max(minSize.y, ws.y - r.y - 4.f));
  }
  if (Application::exists()) {
    Application::get().setPopupRect(this, r);
  }
  return true;
}

bool ResizablePopupFrame::onMouseUp(const MouseEvent&) {
  const bool was = m_edges != 0;
  m_edges = 0;
  if (was) invalidate();
  return was;
}

void ResizablePopupFrame::onMouseLeave() {
  if (m_edges == 0 && m_hoverEdges != 0) {
    m_hoverEdges = 0;
    invalidate();
  }
}

Cursor ResizablePopupFrame::cursor() const {
  const uint8_t e = m_edges != 0 ? m_edges : m_hoverEdges;
  if (e == 0) return Cursor::Arrow;
  const bool l = (e & 1) != 0, r = (e & 2) != 0, t = (e & 4) != 0, b = (e & 8) != 0;
  if ((l && t) || (r && b)) return Cursor::SizeNWSE;
  if ((r && t) || (l && b)) return Cursor::SizeNESW;
  if (l || r) return Cursor::SizeWE;
  if (t || b) return Cursor::SizeNS;
  return Cursor::Arrow;
}

void ResizablePopupFrame::paint(Canvas& c) {
  Border::paint(c);
  if (!resizable) return;
  const Theme& t = theme();
  const uint8_t active = m_edges != 0 ? m_edges : m_hoverEdges;
  // Subtle diagonal corner grip in the bottom-right corner.
  const Color gripColor = ((active & (2 | 8)) == (2 | 8)) ? t.accent : t.textDisabled.withAlpha(160);
  for (int i = 0; i < 3; ++i) {
    const float o = 4.0f + 3.0f * float(i);
    c.line(Vec2(m_rect.right() - o, m_rect.bottom() - 2.0f),
           Vec2(m_rect.right() - 2.0f, m_rect.bottom() - o), gripColor, 1.0f);
  }
  // Crisp hover/active edge accent bar.
  if (active != 0) {
    const Color accent = m_edges != 0 ? t.accent : t.accent.withAlpha(150);
    if ((active & 1) != 0) c.fillRect({m_rect.x, m_rect.y + 3.f, 2.f, std::max(0.f, m_rect.h - 6.f)}, accent);
    if ((active & 2) != 0) c.fillRect({m_rect.right() - 2.f, m_rect.y + 3.f, 2.f, std::max(0.f, m_rect.h - 6.f)}, accent);
    if ((active & 4) != 0) c.fillRect({m_rect.x + 3.f, m_rect.y, std::max(0.f, m_rect.w - 6.f), 2.f}, accent);
    if ((active & 8) != 0) c.fillRect({m_rect.x + 3.f, m_rect.bottom() - 2.f, std::max(0.f, m_rect.w - 6.f), 2.f}, accent);
  }
}

Ref<Widget> makeWindowTitleBar(const std::string& title, Icon icon, bool shadeable, Widget* window,
                               std::function<void()> onClose, std::function<void()> onShade) {
  std::weak_ptr<Widget> wr = window->shared_from_this();
  auto bar = make<WindowTitleBar>(title, icon, shadeable, std::move(onClose),
      [wr](Vec2 p) { if (auto r = wr.lock()) Application::get().movePopup(r.get(), p); }, std::move(onShade));
  bar->window = window;
  return bar;
}

Ref<Widget> showFloatingWindow(Ref<Widget> content, FloatingWindowOptions o) {
  const Theme& t = theme();
  auto root = make<ResizablePopupFrame>();
  root->resizable = o.resizable;
  root->minSize = o.minSize;
  root->setBackground(t.panel);
  root->setBorder(t.borderStrong, Margin(1));
  root->setRadius(5);
  root->setFixedWidth(o.size.x);
  root->setFixedHeight(o.size.y);

  std::weak_ptr<ResizablePopupFrame> wr = root;
  auto close = [wr] { if (auto r = wr.lock()) Application::get().closePopup(r.get()); };

  auto col = vbox();
  auto shaded = std::make_shared<bool>(false);
  auto savedH = std::make_shared<float>(o.size.y);
  const bool wasResizable = o.resizable;
  Ref<Widget> contentRef = content;
  auto bar = make<WindowTitleBar>(o.title, o.icon, o.shadeable, close,
      [wr](Vec2 p) { if (auto r = wr.lock()) Application::get().movePopup(r.get(), p); }, nullptr);
  bar->window = root.get();
  col->add(bar);
  col->add(std::move(content), {.fill = 1});
  // shade toggle needs the finished widgets, so wire it after construction
  std::weak_ptr<WindowTitleBar> wb = bar;
  bar->setShadeHandler([wr, wb, shaded, savedH, wasResizable, contentRef] {
    auto r = wr.lock();
    auto b = wb.lock();
    if (!r || !b) return;
    *shaded = !*shaded;
    b->shaded = *shaded;
    if (*shaded) *savedH = r->rect().h;
    r->resizable = !*shaded && wasResizable;
    contentRef->setVisible(!*shaded);
    r->setFixedHeight(*shaded ? WindowTitleBar::kH + 2.0f : *savedH);
    Application::get().invalidateLayout();
  });
  root->setChild(col);

  PopupOptions po;
  po.modal = false;
  po.dismissOnOutside = false;
  po.shadow = true;
  if (o.pos.x < 0 || o.pos.y < 0) po.placement = PopupPlacement::Center;
  else { po.placement = PopupPlacement::AtPoint; po.anchor = Rect{o.pos.x, o.pos.y, 0, 0}; }
  po.onClose = o.onClose;
  Application::get().showPopup(root, po);
  return root;
}

void closeFloatingWindow(Widget* root) {
  if (root) Application::get().closePopup(root);
}

// ------------------------------------------------------------------ DockSpace
DockSpace::~DockSpace() {
  if (m_sweepTimer && Application::exists()) Application::get().cancelTimer(m_sweepTimer);
}

void DockSpace::wire(TabView& v) {
  v.dockGroup = m_group;
  auto prev = v.onTabsChanged;
  v.onTabsChanged = [this, prev] {
    if (prev) prev();
    scheduleSweep();
    if (onChanged) onChanged();
  };
  v.onDockSplit = [this](TabView* target, TabView::DockZone z, TabView::Page p) { split(target, z, std::move(p)); };
  v.onTabTorn = [this](TabView* src, TabView::Page p, Vec2 pos) {
    std::weak_ptr<TabView> keepAlive;
    floatPage(std::move(p), pos, src);
  };
}

void DockSpace::attach(TabView& view) {
  wire(view);
  m_static.push_back(std::static_pointer_cast<TabView>(view.shared_from_this()));
}

Ref<TabView> DockSpace::makeView() {
  auto v = make<TabView>();
  wire(*v);
  m_dyn.push_back(v);
  return v;
}

bool DockSpace::hasDynamic() const {
  for (auto& w : m_dyn) if (!w.expired()) return true;
  return false;
}

int DockSpace::floatingCount() const {
  int n = 0;
  for (auto& w : m_windows) if (!w.root.expired()) n++;
  return n;
}

TabView* DockSpace::split(TabView* target, TabView::DockZone zone, TabView::Page page) {
  Widget* frame = target;
  while (frame->parent() && !dynamic_cast<Splitter*>(frame->parent())) frame = frame->parent();
  auto* sp = frame->parent() ? dynamic_cast<Splitter*>(frame->parent()) : nullptr;
  if (!sp) {                                   // not inside a splitter (e.g. a floating window): just add a tab
    target->insertTab(target->count(), std::move(page));
    return target;
  }
  auto nv = makeView();
  nv->insertTab(0, std::move(page));
  Ref<Widget> nframe = m_frame ? m_frame(nv) : Ref<Widget>(nv);

  bool horiz = zone == TabView::DockZone::Left || zone == TabView::DockZone::Right;
  Orientation want = horiz ? Orientation::Horizontal : Orientation::Vertical;
  bool before = zone == TabView::DockZone::Left || zone == TabView::DockZone::Top;
  Ref<Widget> frameRef = frame->shared_from_this();
  float total = horiz ? frame->rect().w : frame->rect().h;
  if (total < 50) total = 400;
  float half = std::max(90.0f, std::floor(total * 0.5f - theme().splitter * 0.5f));
  int idx = sp->indexOf(frame);

  if (sp->orientation() == want) {
    if (Splitter::Pane* tp = sp->paneAt(idx); tp && !tp->flex) tp->size = half;
    sp->insert(before ? idx : idx + 1, nframe, {.size = half, .minSize = 90});
  } else {
    auto ns = make<Splitter>(want);
    sp->replace(frame, ns);
    ns->add(before ? nframe : frameRef, before ? Splitter::Pane{.size = half, .minSize = 90} : Splitter::Pane{.size = 0, .minSize = 90, .flex = true});
    ns->add(before ? frameRef : nframe, before ? Splitter::Pane{.size = 0, .minSize = 90, .flex = true} : Splitter::Pane{.size = half, .minSize = 90});
  }
  Application::get().invalidateLayout();
  return nv.get();
}

TabView* DockSpace::floatPage(TabView::Page page, Vec2 pos, TabView* home) {
  auto fv = makeView();
  fv->emptyText = "Drop a tab here";
  std::string title = page.title;
  fv->insertTab(0, std::move(page));
  Window w;
  w.view = fv;
  if (home) w.home = std::static_pointer_cast<TabView>(home->shared_from_this());
  FloatingWindowOptions o;
  o.title = title;
  o.pos = Vec2(std::max(0.f, pos.x - 40.f), std::max(0.f, pos.y - 14.f));
  std::weak_ptr<TabView> wv = fv, wh = w.home;
  o.onClose = [this, wv, wh] {
    auto v = wv.lock();
    if (!v || v->count() == 0) { if (onChanged) onChanged(); return; }
    TabView* dst = m_forceHome;
    std::shared_ptr<TabView> keep = wh.lock();
    if (!dst && keep) dst = keep.get();
    if (!dst) for (auto& s : m_static) if (auto sv = s.lock()) { keep = sv; dst = sv.get(); break; }
    while (v->count() && dst) dst->insertTab(dst->count(), v->takeTab(0));
    Application::get().invalidateLayout();
    if (onChanged) onChanged();
  };
  w.root = showFloatingWindow(fv, o);
  m_windows.push_back(w);
  if (onChanged) onChanged();
  return fv.get();
}

void DockSpace::scheduleSweep() {
  if (m_sweepTimer) return;
  m_sweepTimer = Application::get().addTimer(1, [this] {
    m_sweepTimer = 0;
    sweep();
    return false;
  });
}

void DockSpace::collapse(TabView& v) {
  Widget* frame = &v;
  while (frame->parent() && !dynamic_cast<Splitter*>(frame->parent())) frame = frame->parent();
  auto* sp = frame->parent() ? dynamic_cast<Splitter*>(frame->parent()) : nullptr;
  if (!sp) return;
  Ref<Widget> keep = frame->shared_from_this();
  sp->removeChild(frame);
  if (sp->paneCount() == 1 && sp->parent()) {
    if (auto* psp = dynamic_cast<Splitter*>(sp->parent())) {
      Ref<Widget> only = sp->children().front();
      psp->replace(sp, only);
    }
  }
  Application::get().invalidateLayout();
}

void DockSpace::sweep() {
  bool changed = false;
  for (auto& wk : m_dyn) {
    auto v = wk.lock();
    if (!v || v->count() > 0) continue;
    bool inWindow = false;
    for (auto& w : m_windows)
      if (w.view.lock() == v) {
        inWindow = true;
        if (auto r = w.root.lock()) closeFloatingWindow(r.get());
      }
    if (!inWindow) collapse(*v);
    changed = true;
  }
  std::erase_if(m_dyn, [](auto& w) { return w.expired(); });
  std::erase_if(m_windows, [](auto& w) { return w.root.expired(); });
  if (changed && onChanged) onChanged();
}

void DockSpace::dissolve(TabView& fallback) {
  m_forceHome = &fallback;
  for (auto& w : m_windows) if (auto r = w.root.lock()) closeFloatingWindow(r.get());
  m_forceHome = nullptr;
  for (auto& wk : m_dyn)
    if (auto v = wk.lock())
      while (v->count()) fallback.insertTab(fallback.count(), v->takeTab(0));
  sweep();
  std::erase_if(m_dyn, [](auto& w) { return w.expired(); });
  Application::get().invalidateLayout();
}

} // namespace prism
