// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Application.h"
#include "prism/WindowChrome.h"
#include <type_traits>
#include "prism/AccessibilityBridge.h"
#include <chrono>

namespace prism {

void Application::swapState(WinState& w) {
  std::swap(m_root, w.root);
  m_canvas.swapContents(w.canvas);        // not std::swap: Canvas has no move constructor, that would copy frames
  std::swap(m_popups, w.popups);
  std::swap(m_hoverChain, w.hoverChain);
  std::swap(m_focus, w.focus);
  std::swap(m_capture, w.capture);
  std::swap(m_mouse, w.mouse);
  std::swap(m_dirty, w.dirty);
  std::swap(m_layoutDirty, w.layoutDirty);
}

Application::WinState* Application::findWindow(int id) {
  for (auto& w : m_windows) if (w.id == id) return &w;
  return nullptr;
}
const Application::WinState* Application::findWindow(int id) const {
  for (auto& w : m_windows) if (w.id == id) return &w;
  return nullptr;
}


// Runs fn with window `id` as the current UI context (its root / canvas / popups / focus are swapped in).
template <class F> auto Application::inWindow(int id, F&& fn) {
  if (id == m_ctxWindow) return fn();
  WinState* w = findWindow(id);
  if (!w) {
    using R = decltype(fn());
    if constexpr (std::is_void_v<R>) return; else return R{};
  }
  hideTooltip();                                  // tooltips belong to the window that started them
  int prev = m_ctxWindow;
  // only window 0 <-> one secondary window are ever swapped: return to 0 first if another is current
  WinState* cur = prev ? findWindow(prev) : nullptr;
  if (cur) swapState(*cur);
  swapState(*w);
  m_ctxWindow = id;
  struct Restore {
    Application& a; WinState* w; WinState* cur; int prev;
    ~Restore() { a.hideTooltip(); a.swapState(*w); if (cur) a.swapState(*cur); a.m_ctxWindow = prev; }
  } restore{*this, w, cur, prev};
  return fn();
}



Application* Application::s_instance = nullptr;

Application::Application() { s_instance = this; }

Application::~Application() {
  m_a11y.reset();       // bridges reference the widget tree
  m_popups.clear();
  m_root.reset();
  m_windows.clear();
  m_backend.reset();    // GPU resources must go while the GL context (owned by the window host) still exists
  m_windowHost.reset(); // destroys the SDL window if there is one
  m_platform.reset();
  s_instance = nullptr;
}

bool Application::initHosted(const HostDesc& d, std::shared_ptr<Platform> platform) {
  m_platform = platform ? std::move(platform) : std::make_shared<HeadlessPlatform>();
  themeMutable().loadFonts(d.assetDir);
  if (!theme().ui->ok()) {
    fprintf(stderr, "PRISM: could not load fonts from '%s/fonts'\n", d.assetDir.c_str());
    return false;
  }
  m_canvas.resize(std::max(1, d.width), std::max(1, d.height));
  m_rec.resize(m_canvas.width(), m_canvas.height());
  invalidateLayout();
  return true;
}

void Application::setRoot(Ref<Widget> r) {
  if (r && m_mainChrome && m_ctxWindow == 0) {
    WindowChromeOptions o;
    o.title = m_desc.title;
    o.icon = Icon(m_desc.icon);
    r = wrapWithWindowChrome(std::move(r), 0, o);
  }
  m_root = std::move(r);
  invalidateLayout();
  if (m_root) accessNotify(AccessEvent::StructureChanged, m_root.get());
}

uint64_t Application::now() const { return m_platform ? m_platform->nowMs() : 0; }

void Application::quit() {
  m_running = false;
  if (m_platform) m_platform->requestQuit();
}

// ------------------------------------------------------------------ layout & paint
void Application::layoutIfNeeded() {
  if (!m_layoutDirty || !m_root) return;
  m_layoutDirty = false;
  Vec2 ws = windowSize();
  m_root->measure(ws);
  m_root->arrange({0, 0, ws.x, ws.y});
  for (auto& p : m_popups) arrangePopup(p);
}

void Application::arrangePopup(Popup& p) {
  Vec2 ws = windowSize();
  Vec2 ds = p.widget->measure(ws);
  ds.x = std::min(ds.x, ws.x - 8);
  ds.y = std::min(ds.y, ws.y - 8);
  Rect a = p.opt.anchor;
  Vec2 pos;
  switch (p.opt.placement) {
  case PopupPlacement::Below:
    pos = {a.x, a.bottom()};
    if (pos.y + ds.y > ws.y - 4) pos.y = std::max(4.f, a.y - ds.y);
    if (pos.x + ds.x > ws.x - 4) pos.x = ws.x - 4 - ds.x;
    break;
  case PopupPlacement::Right:
    pos = {a.right() - 2, a.y - 4};
    if (pos.x + ds.x > ws.x - 4) pos.x = a.x - ds.x + 2;
    if (pos.y + ds.y > ws.y - 4) pos.y = ws.y - 4 - ds.y;
    break;
  case PopupPlacement::AtPoint:
    pos = {a.x, a.y};
    if (pos.x + ds.x > ws.x - 4) pos.x = a.x - ds.x;
    if (pos.y + ds.y > ws.y - 4) pos.y = a.y - ds.y;
    break;
  case PopupPlacement::Center: pos = (ws - ds) * 0.5f; break;
  }
  pos.x = std::max(0.f, pos.x);
  pos.y = std::max(0.f, pos.y);
  p.widget->arrange({std::floor(pos.x), std::floor(pos.y), std::ceil(ds.x), std::ceil(ds.y)});
}

void Application::paintAll(Canvas& c) {
  layoutIfNeeded();
  c.clear(theme().window);
  if (m_root) m_root->paint(c);
  Rect full(0, 0, float(c.width()), float(c.height()));
  for (auto& p : m_popups) {
    if (p.opt.modal) c.fillRect(full, Color(0, 0, 0, 120));
    if (p.opt.shadow) c.shadow(p.widget->rect(), 4, 10, Color(0, 0, 0, 255));
    p.widget->paint(c);
  }
  if (m_dragPainter) m_dragPainter(c);
  if (m_activeDragDrop) m_activeDragDrop->paintDecorator(c, m_mouse);
  if (m_ctxWindow == 0) paintToasts(c);          // toasts and the inspector belong to the main window
  if (m_inspector && m_ctxWindow == 0) paintInspector(c);
  if (m_activeTooltipWidget) {
    m_activeTooltipWidget->arrange(m_tooltipRect);
    m_activeTooltipWidget->paint(c);
  } else if (m_tooltipRect.w > 0) {
    const Theme& t = theme();
    c.shadow(m_tooltipRect, 3, 6, Color(0, 0, 0, 255));
    c.fillRoundRect(m_tooltipRect, 3, t.control);
    c.strokeRoundRect(m_tooltipRect, 3, t.borderStrong);
    c.textIn(*t.ui, m_tooltipRect.deflated(Margin(8, 0)), m_tooltipText, t.text, HAlign::Left, false);
  }
}

void Application::renderFrame() {
  auto t0 = std::chrono::steady_clock::now();
  if (m_backend) {
    m_rec.resize(m_canvas.width(), m_canvas.height());   // a secondary window may have recorded at its own size
    m_rec.beginFrame();
    paintAll(m_rec);
    m_backend->submit(m_rec.list());
  } else {
    paintAll(m_canvas);
  }
  m_dirty = false;
  m_frameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

const DrawList& Application::renderToDrawList() {
  layoutIfNeeded();
  m_rec.resize(m_canvas.width(), m_canvas.height());
  m_rec.beginFrame();
  paintAll(m_rec);
  m_dirty = false;
  return m_rec.list();
}

void Application::setRenderBackend(std::shared_ptr<RenderBackend> b) {
  m_backend = std::move(b);
  if (m_backend) m_backend->resize(m_canvas.width(), m_canvas.height());
  invalidate();
}

bool Application::saveScreenshot(const std::string& path) {
  layoutIfNeeded();
  renderFrame();
  PixelView v = frame();
  return v && Canvas::writePng(path, v.data, v.width, v.height, v.stride);
}

// ------------------------------------------------------------------ popups
void Application::showPopup(Ref<Widget> w, const PopupOptions& o) {
  hideTooltip();
  Popup p{std::move(w), o};
  m_popups.push_back(std::move(p));
  layoutIfNeeded();
  arrangePopup(m_popups.back());
  invalidate();
}

void Application::movePopup(const Widget* w, Vec2 topLeft) {
  for (auto& p : m_popups)
    if (p.widget.get() == w) {
      Vec2 ws = windowSize();
      Rect r = p.widget->rect();
      topLeft.x = std::clamp(topLeft.x, 0.f - r.w + 80.f, ws.x - 80.f);
      topLeft.y = std::clamp(topLeft.y, 0.f, ws.y - 32.f);
      p.opt.placement = PopupPlacement::AtPoint;
      p.opt.anchor = Rect{topLeft.x, topLeft.y, 0, 0};
      p.widget->arrange({std::floor(topLeft.x), std::floor(topLeft.y), r.w, r.h});
      invalidate();
      return;
    }
}

void Application::closePopup(const Widget* w) {
  for (size_t i = 0; i < m_popups.size(); i++)
    if (m_popups[i].widget.get() == w) {
      auto cb = std::move(m_popups[i].opt.onClose);
      auto keep = m_popups[i].widget;
      m_popups.erase(m_popups.begin() + long(i));
      if (auto f = m_focus.lock(); f && f->isDescendantOf(keep.get())) setFocus(nullptr);
      invalidate();
      if (cb) cb();
      return;
    }
}

void Application::closeAllPopups() {
  while (!m_popups.empty()) closePopup(m_popups.back().widget.get());
}

// ------------------------------------------------------------------ focus / capture / timers
void Application::focusNext(bool reverse) {
  auto order = focusOrder(m_root.get());
  if (order.empty()) return;
  int n = int(order.size()), cur = -1;
  for (int i = 0; i < n; i++) if (order[size_t(i)] == focus()) cur = i;
  int next = cur < 0 ? (reverse ? n - 1 : 0) : (cur + (reverse ? n - 1 : 1)) % n;
  setFocus(order[size_t(next)]);
}

void Application::setFocus(Widget* w) {
  Widget* old = focus();
  if (old == w) return;
  if (old) { old->m_focused = false; old->onFocusChanged(false); }
  m_focus.reset();
  if (w) { m_focus = w->weak_from_this(); w->m_focused = true; w->onFocusChanged(true); }
  m_platform->setTextInputActive(w && w->capturesTyping());
  invalidate();
  if (w) accessNotify(AccessEvent::FocusChanged, w);
}

void Application::accessNotify(AccessEvent e, Widget* w) { if (m_a11y && w) m_a11y->notify(e, w); }

void Application::captureMouse(Widget* w) { m_capture = w ? w->weak_from_this() : std::weak_ptr<Widget>(); }
void Application::releaseMouse() { m_capture.reset(); }

int Application::addTimer(int ms, std::function<bool()> cb) {
  int id = m_nextTimerId++;
  m_timers.push_back({id, now() + uint64_t(ms), ms, std::move(cb)});
  return id;
}

void Application::cancelTimer(int id) {
  std::erase_if(m_timers, [&](const Timer& t) { return t.id == id; });
}

void Application::runTimers() {
  uint64_t t = now();
  m_dueTimers.clear();
  for (auto& tm : m_timers) if (tm.due <= t) m_dueTimers.push_back(tm.id);
  for (int id : m_dueTimers) {
    auto it = std::find_if(m_timers.begin(), m_timers.end(), [&](const Timer& x) { return x.id == id; });
    if (it == m_timers.end()) continue;
    auto cb = it->cb;
    bool again = cb();
    it = std::find_if(m_timers.begin(), m_timers.end(), [&](const Timer& x) { return x.id == id; });
    if (it == m_timers.end()) continue;
    if (again) it->due = now() + uint64_t(it->interval);
    else m_timers.erase(it);
  }
}

int Application::nextWaitMs() const {
  if (m_timers.empty()) return 500;
  uint64_t t = now(), best = t + 500;
  for (auto& tm : m_timers) best = std::min(best, tm.due);
  return int(best > t ? best - t : 0);
}

void Application::addShortcut(Key key, uint8_t mods, std::function<void()> fn) {
  m_shortcuts.push_back({key, mods, std::move(fn)});
}

void Application::setCursor(Cursor c) {
  if (c == m_cursor) return;
  m_cursor = c;
  m_platform->setCursor(c);
}

std::string Application::clipboard() const { return m_platform->clipboard(); }
void Application::setClipboard(const std::string& s) { m_platform->setClipboard(s); }

// ------------------------------------------------------------------ tooltips & drag-drop
void Application::hideTooltip() {
  if (m_tooltipTimer) { cancelTimer(m_tooltipTimer); m_tooltipTimer = 0; }
  m_activeTooltipWidget.reset();
  if (m_tooltipRect.w > 0) { m_tooltipRect = {}; invalidate(); }
}

void Application::showTooltip() {
  m_tooltipTimer = 0;
  if (!m_popups.empty()) return;
  if (m_activeTooltipWidget) {
    Vec2 ws = windowSize();
    Vec2 ds = m_activeTooltipWidget->measure(ws);
    Vec2 p = m_mouse + Vec2(12, 20);
    if (p.x + ds.x > ws.x - 4) p.x = ws.x - 4 - ds.x;
    if (p.y + ds.y > ws.y - 4) p.y = m_mouse.y - ds.y - 8;
    m_tooltipRect = {std::floor(p.x), std::floor(p.y), std::ceil(ds.x), std::ceil(ds.y)};
    invalidate();
    return;
  }
  if (m_tooltipText.empty()) return;
  const Theme& t = theme();
  float w = t.ui->width(m_tooltipText) + 16, h = 22;
  Vec2 ws = windowSize();
  Vec2 p = m_mouse + Vec2(12, 20);
  if (p.x + w > ws.x - 4) p.x = ws.x - 4 - w;
  if (p.y + h > ws.y - 4) p.y = m_mouse.y - h - 8;
  m_tooltipRect = {std::floor(p.x), std::floor(p.y), std::ceil(w), h};
  invalidate();
}

void Application::startDragDrop(std::shared_ptr<DragDropOperation> op) {
  m_activeDragDrop = std::move(op);
  invalidate();
}

void Application::cancelDragDrop() {
  if (auto dt = m_dragTarget.lock()) {
    dt->onDragLeave();
  }
  m_dragTarget.reset();
  m_activeDragDrop.reset();
  invalidate();
}

// ------------------------------------------------------------------ input dispatch
Widget* Application::hitTestAll(Vec2 p, bool* inPopup) {
  layoutIfNeeded(); // rectangles must be current: a host may deliver several events before the next tick/render
  if (inPopup) *inPopup = false;
  bool modal = false;
  for (auto it = m_popups.rbegin(); it != m_popups.rend(); ++it) {
    if (it->opt.modal) modal = true;
    if (it->widget->rect().contains(p))
      if (Widget* h = it->widget->hitTest(p)) {
        if (inPopup) *inPopup = true;
        return h;
      }
  }
  if (modal) return nullptr;
  return m_root ? m_root->hitTest(p) : nullptr;
}

void Application::updateHover(Widget* target) {
  std::vector<Widget*> now;
  for (Widget* w = target; w; w = w->m_parent) now.push_back(w);
  for (auto& wk : m_hoverChain)
    if (auto w = wk.lock())
      if (std::find(now.begin(), now.end(), w.get()) == now.end()) { w->m_hovered = false; w->onMouseLeave(); invalidate(); }
  std::vector<std::weak_ptr<Widget>> chain;
  for (Widget* w : now) {
    bool had = false;
    for (auto& wk : m_hoverChain) if (wk.lock().get() == w) { had = true; break; }
    if (!had) { w->m_hovered = true; w->onMouseEnter(); invalidate(); }
    chain.push_back(w->weak_from_this());
  }
  m_hoverChain = std::move(chain);
}

void Application::dispatchMouseMove(MouseEvent e) {
  m_mouse = e.pos;
  e.mods = m_mods;
  if (m_activeDragDrop) {
    Widget* hit = hitTestAll(e.pos);
    Widget* currentTarget = nullptr;
    for (Widget* w = hit; w; w = w->m_parent) {
      DragDropEvent dev{e.pos, m_activeDragDrop, m_mods};
      if (w->enabled() && w->onDragOver(dev)) {
        currentTarget = w;
        break;
      }
    }
    auto prevTarget = m_dragTarget.lock().get();
    if (prevTarget != currentTarget) {
      if (prevTarget) prevTarget->onDragLeave();
      if (currentTarget) {
        DragDropEvent dev{e.pos, m_activeDragDrop, m_mods};
        currentTarget->onDragEnter(dev);
        m_dragTarget = currentTarget->weak_from_this();
      } else {
        m_dragTarget.reset();
      }
    }
    invalidate();
    return;
  }
  if (auto cw = m_capture.lock()) {
    cw->onMouseMove(e);
    setCursor(cw->cursor());
    invalidate();
    return;
  }
  Widget* t = hitTestAll(e.pos);
  Widget* prev = m_hoverChain.empty() ? nullptr : m_hoverChain.front().lock().get();
  updateHover(t);
  Cursor cur = Cursor::Arrow;
  std::string tip;
  Ref<Widget> richTip;
  for (Widget* w = t; w; w = w->m_parent) {
    if (cur == Cursor::Arrow && w->enabled()) cur = w->cursor();
    if (!richTip && w->tooltipWidget()) richTip = w->tooltipWidget();
    if (tip.empty() && !w->tooltipAt(e.pos).empty()) tip = w->tooltipAt(e.pos);
  }
  setCursor(cur);
  for (Widget* w = t; w; w = w->m_parent)
    if (w->enabled() && w->onMouseMove(e)) break;
  if (t != prev || m_tooltipRect.w > 0) hideTooltip();
  m_activeTooltipWidget = richTip;
  if (tip != m_tooltipText || (t != prev && !tip.empty()) || richTip) {
    m_tooltipText = tip;
    if (m_tooltipTimer) { cancelTimer(m_tooltipTimer); m_tooltipTimer = 0; }
  }
  if ((!tip.empty() || richTip) && m_tooltipRect.w <= 0 && !m_tooltipTimer)
    m_tooltipTimer = addTimer(550, [this] { showTooltip(); return false; });
  invalidate();
}

void Application::dispatchMouseDown(MouseEvent e) {
  m_mouse = e.pos;
  e.mods = m_mods;
  m_dismissedThisEvent = false;
  hideTooltip();
  bool inPopup = false;
  Widget* t = hitTestAll(e.pos, &inPopup);
  if (!m_popups.empty() && !inPopup) {
    bool closed = false;
    while (!m_popups.empty() && m_popups.back().opt.dismissOnOutside) {
      closePopup(m_popups.back().widget.get());
      closed = true;
    }
    m_dismissedThisEvent = closed;
    t = hitTestAll(e.pos, &inPopup);
    bool anyModal = false;
    for (auto& pp : m_popups) anyModal = anyModal || pp.opt.modal;
    if (anyModal && !inPopup) return; // a modal popup swallows the click; floating (non-modal) windows let it through
  }
  {
    Widget* f = nullptr;
    for (Widget* w = t; w; w = w->m_parent) if (w->enabled() && w->focusable()) { f = w; break; }
    if (f || !inPopup) setFocus(f);
  }

  // Preview / Tunnel Phase (Root -> Leaf)
  if (t) {
    WidgetPath path = WidgetPath::build(t);
    for (Widget* w : path.widgets) {
      if (w->enabled()) {
        Reply r = w->onPreviewMouseDown(e);
        if (r.isHandled()) {
          if (r.captureTarget()) captureMouse(r.captureTarget());
          if (r.focusTarget()) setFocus(r.focusTarget());
          invalidate();
          return;
        }
      }
    }
  }

  // Bubble Phase (Leaf -> Root)
  for (Widget* w = t; w; w = w->m_parent)
    if (w->enabled() && w->onMouseDown(e)) { captureMouse(w); break; }
  invalidate();
}

void Application::dispatchMouseUp(MouseEvent e) {
  m_mouse = e.pos;
  e.mods = m_mods;
  if (m_activeDragDrop) {
    releaseMouse();   // the widget that started the drag held the mouse; the drop ends that too
    Widget* hit = hitTestAll(e.pos);
    for (Widget* w = hit; w; w = w->m_parent) {
      DragDropEvent dev{e.pos, m_activeDragDrop, m_mods};
      if (w->enabled() && w->onDrop(dev)) break;
    }
    if (auto dt = m_dragTarget.lock()) dt->onDragLeave();
    m_dragTarget.reset();
    m_activeDragDrop.reset();
    invalidate();
    return;
  }
  if (auto cw = m_capture.lock()) {
    releaseMouse();
    cw->onMouseUp(e);
  } else {
    Widget* t = hitTestAll(e.pos);
    if (t) {
      WidgetPath path = WidgetPath::build(t);
      for (Widget* w : path.widgets) {
        if (w->enabled()) {
          Reply r = w->onPreviewMouseUp(e);
          if (r.isHandled()) {
            if (r.shouldReleaseMouse()) releaseMouse();
            invalidate();
            return;
          }
        }
      }
    }
    for (Widget* w = t; w; w = w->m_parent)
      if (w->enabled() && w->onMouseUp(e)) break;
  }
  MouseEvent mv = e;
  mv.button = MouseButton::None;
  dispatchMouseMove(mv);
}

void Application::dispatchWheel(MouseEvent e) {
  e.mods = m_mods;
  bool inPopup = false;
  Widget* t = hitTestAll(e.pos, &inPopup);
  for (Widget* w = t; w; w = w->m_parent)
    if (w->enabled() && w->onMouseWheel(e)) break;
  hideTooltip();
  invalidate();
}

void Application::dispatchKey(KeyEvent e) {
  e.mods = m_mods;
  if (!m_popups.empty()) {
    auto w = m_popups.back().widget;
    Widget* pf = focus();
    if (pf && pf->isDescendantOf(w.get()))
      for (Widget* x = pf; x; x = x->m_parent) {
        if (x->enabled() && x->onKeyDown(e)) { invalidate(); return; }
        if (x == w.get()) break;
      }
    else if (w->onKeyDown(e)) { invalidate(); return; }
    if (e.key == Key::Escape) closePopup(w.get());
    return;
  }
  Widget* f = focus();
  if (f) {
    WidgetPath path = WidgetPath::build(f);
    for (Widget* w : path.widgets) {
      if (w->enabled()) {
        Reply r = w->onPreviewKeyDown(e);
        if (r.isHandled()) { invalidate(); return; }
      }
    }
  }
  bool typing = f && f->capturesTyping();
  for (Widget* w = f; w; w = w->m_parent)
    if (w->enabled() && w->onKeyDown(e)) { invalidate(); return; }
  if (e.key == Key::Tab && !(e.mods & (ModCtrl | ModAlt))) { focusNext(e.mods & ModShift); invalidate(); return; }
  if (typing && !(e.mods & (ModCtrl | ModAlt))) return;
  for (auto& s : m_shortcuts)
    if (s.key == e.key && s.mods == (e.mods & (ModCtrl | ModAlt | ModShift))) { s.fn(); invalidate(); return; }
  if (m_shortcutHandler && m_shortcutHandler(e.key, e.mods)) invalidate();
}

void Application::injectMouseMove(Vec2 p) { MouseEvent e; e.pos = p; dispatchMouseMove(e); }
void Application::injectMouseButton(Vec2 p, MouseButton b, bool down, int clicks) {
  MouseEvent e; e.pos = p; e.button = b; e.clicks = clicks;
  if (down) dispatchMouseDown(e); else dispatchMouseUp(e);
}
void Application::injectKey(Key k, uint8_t mods) { m_mods = mods; KeyEvent e; e.key = k; e.mods = mods; dispatchKey(e); }

// ------------------------------------------------------------------ hosted mode
void Application::hostResize(int w, int h) {
  w = std::max(1, w); h = std::max(1, h);
  if (w == m_canvas.width() && h == m_canvas.height()) return;
  closeAllPopups();
  m_canvas.resize(w, h);
  m_rec.resize(w, h);
  if (m_backend) m_backend->resize(w, h);
  invalidateLayout();
}

void Application::hostMouseMove(Vec2 p) { MouseEvent e; e.pos = p; dispatchMouseMove(e); }

void Application::hostMouseButton(Vec2 p, MouseButton b, bool down, int clicks) {
  if (b == MouseButton::None) return;
  MouseEvent e; e.pos = p; e.button = b; e.clicks = clicks;
  if (down) dispatchMouseDown(e); else dispatchMouseUp(e);
}

void Application::hostMouseWheel(Vec2 p, Vec2 delta) {
  m_mouse = p;
  MouseEvent e; e.pos = p; e.wheel = delta;
  dispatchWheel(e);
}

void Application::hostMouseLeave() {
  if (!m_capture.lock()) { updateHover(nullptr); hideTooltip(); invalidate(); }
}

void Application::hostKey(Key k, uint8_t mods, bool down, bool repeat) {
  m_mods = mods;
  if (!down || k == Key::Unknown) return;
  KeyEvent e; e.key = k; e.mods = mods; e.repeat = repeat;
  dispatchKey(e);
}

void Application::hostText(const std::string& utf8) {
  if (Widget* f = focus()) { f->onTextInput(utf8); invalidate(); }
}

bool Application::tick() {
  if (m_a11y) m_a11y->pump();
  runTimers();
  for (auto& w : m_windows) { int id = w.id; inWindow(id, [&] { layoutIfNeeded(); }); }
  layoutIfNeeded();
  return needsRender();
}

PixelView Application::render() {
  renderFrame();
  return frame();
}

PixelView Application::frame() const {
  if (m_backend) return m_backend->readback();
  return {m_canvas.pixels(), m_canvas.width(), m_canvas.height(), m_canvas.width()};
}

bool Application::wantsPointerAt(Vec2 p) {
  layoutIfNeeded();
  // A drag-and-drop in progress, or a widget holding the mouse (a tab or splitter being dragged, a slider ...),
  // keeps the pointer wherever it goes.
  if (m_activeDragDrop) return true;
  if (auto held = m_capture.lock(); held && held.get() != m_root.get() && held->consumesHostInput()) return true;
  Widget* t = hitTestAll(p);
  return t != nullptr && t != m_root.get() && t->consumesHostInput();
}

bool Application::wantsKeyboard() const {
  if (hasBlockingPopup()) return true;   // a floating window takes the keyboard only through what has focus in it
  Widget* f = focus();
  return f && f->capturesTyping();
}

// ------------------------------------------------------------------ native multi-window

int Application::openWindow(Ref<Widget> root, const OsWindowDesc& desc) {
  if (!root) return 0;
  WinState w;
  w.id = m_nextWindowId++;
  w.desc = desc;
  if (desc.prismChrome) {
    WindowChromeOptions o;
    o.title = desc.title;
    o.icon = Icon(desc.icon);
    root = wrapWithWindowChrome(std::move(root), w.id, o);
  }
  w.root = std::move(root);
  w.canvas.resize(std::max(1, desc.width), std::max(1, desc.height));
  int id = w.id;
  m_windows.push_back(std::move(w));
  m_zOrder.push_back(id);
  if (m_platform && !m_platform->createNativeWindow(id, desc) && m_windowHost) {
    m_windows.pop_back(); m_zOrder.pop_back();      // windowed mode but the OS refused the window
    return 0;
  }
  activateWindow(id);
  return id;
}

void Application::closeWindow(int id) {
  if (id == 0) return;
  WinState* w = findWindow(id);
  if (!w) return;
  inWindow(id, [&] { closeAllPopups(); });
  for (size_t i = 0; i < m_windows.size(); i++) if (m_windows[i].id == id) { m_windows.erase(m_windows.begin() + long(i)); break; }
  m_zOrder.erase(std::remove(m_zOrder.begin(), m_zOrder.end(), id), m_zOrder.end());
  if (m_activeWindow == id) m_activeWindow = m_zOrder.empty() ? 0 : m_zOrder.back();
  if (m_platform) m_platform->destroyNativeWindow(id);
  if (onWindowClosed) onWindowClosed(id);
  invalidate();
}

bool Application::hasWindow(int id) const { return id == 0 || findWindow(id) != nullptr; }
std::vector<int> Application::windowOrder() const { return m_zOrder; }

void Application::activateWindow(int id) {
  if (!hasWindow(id)) return;
  m_zOrder.erase(std::remove(m_zOrder.begin(), m_zOrder.end(), id), m_zOrder.end());
  m_zOrder.push_back(id);
  m_activeWindow = id;
}

Widget* Application::windowRoot(int id) const {
  if (id == m_ctxWindow) return m_root.get();
  if (id == 0) { for (auto& w : m_windows) if (w.id == m_ctxWindow) return w.root.get(); return nullptr; }
  const WinState* w = findWindow(id);
  return w ? w->root.get() : nullptr;
}

Vec2 Application::windowSizeOf(int id) { return inWindow(id, [&] { return windowSize(); }); }

// A press in window `id` dismisses outside-dismissable popups (menus, pickers) of every other window.
void Application::windowResize(int id, int w, int h) { if (id == 0) { hostResize(w, h); return; } inWindow(id, [&] { hostResize(w, h); }); }
void Application::windowMouseMove(int id, Vec2 p) { if (id == 0 && m_ctxWindow == 0) { hostMouseMove(p); return; } inWindow(id, [&] { hostMouseMove(p); }); }
void Application::windowMouseButton(int id, Vec2 p, MouseButton b, bool down, int clicks) {
  if (down) {
    activateWindow(id);
    for (int other : std::vector<int>(m_zOrder)) {
      if (other == id) continue;
      inWindow(other, [&] {
        for (size_t i = m_popups.size(); i-- > 0;)
          if (m_popups[i].opt.dismissOnOutside && !m_popups[i].opt.modal) closePopup(m_popups[i].widget.get());
      });
    }
  }
  if (id == 0 && m_ctxWindow == 0) { hostMouseButton(p, b, down, clicks); return; }
  inWindow(id, [&] { hostMouseButton(p, b, down, clicks); });
}
void Application::windowMouseWheel(int id, Vec2 p, Vec2 d) { if (id == 0 && m_ctxWindow == 0) { hostMouseWheel(p, d); return; } inWindow(id, [&] { hostMouseWheel(p, d); }); }
void Application::windowMouseLeave(int id) { if (id == 0 && m_ctxWindow == 0) { hostMouseLeave(); return; } inWindow(id, [&] { hostMouseLeave(); }); }
void Application::windowKey(int id, Key k, uint8_t mods, bool down, bool repeat) {
  activateWindow(id);
  if (id == 0 && m_ctxWindow == 0) { hostKey(k, mods, down, repeat); return; }
  inWindow(id, [&] { hostKey(k, mods, down, repeat); });
}
void Application::windowText(int id, const std::string& t) { if (id == 0 && m_ctxWindow == 0) { hostText(t); return; } inWindow(id, [&] { hostText(t); }); }
bool Application::windowNeedsRender(int id) {
  if (id == 0 && m_ctxWindow == 0) return needsRender();
  WinState* w = findWindow(id);
  if (!w) return false;
  uint64_t gen = m_paintGen;
  bool need = inWindow(id, [&] { layoutIfNeeded(); return needsRender(); });
  return need || w->paintedGen != gen;
}
PixelView Application::renderWindow(int id) {
  if (id == 0 && m_ctxWindow == 0) return render();
  return inWindow(id, [&] {
    layoutIfNeeded();
    paintAll(m_canvas);                      // secondary windows always paint on the CPU canvas
    m_dirty = false;
    if (WinState* ws = findWindow(id)) ws->paintedGen = m_paintGen;
    return PixelView{m_canvas.pixels(), m_canvas.width(), m_canvas.height(), m_canvas.width()};
  });
}
Widget* Application::windowFocus(int id) { return inWindow(id, [&] { return focus(); }); }
void Application::layoutWindow(int id) { inWindow(id, [&] { layoutIfNeeded(); }); }
void Application::runInWindow(int id, const std::function<void()>& fn) { if (fn) inWindow(id, [&] { fn(); }); }
void Application::renderWindowTo(int id, RenderBackend& backend) {
  inWindow(id, [&] {
    layoutIfNeeded();
    m_rec.resize(m_canvas.width(), m_canvas.height());
    m_rec.beginFrame();
    paintAll(m_rec);
    backend.submit(m_rec.list());
    m_dirty = false;
    if (WinState* ws = findWindow(id)) ws->paintedGen = m_paintGen;
  });
}
bool Application::windowWantsPointerAt(int id, Vec2 p) { return inWindow(id, [&] { return wantsPointerAt(p); }); }
bool Application::windowHasBlockingPopup(int id) { return inWindow(id, [&] { return hasBlockingPopup(); }); }
bool Application::windowWantsKeyboard(int id) { return inWindow(id, [&] { return wantsKeyboard(); }); }

} // namespace prism
