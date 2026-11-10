// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/WindowChrome.h"
#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Layouts.h"
#include <algorithm>
#include <cmath>

namespace prism {
namespace {

class OsTitleBar : public Widget {
public:
  OsTitleBar(int id, WindowChromeOptions o) : m_id(id), m_o(std::move(o)) {}
  Vec2 measure(Vec2 avail) override { return m_desired = constrain(Vec2(avail.x, kOsTitleBarH)); }

  void arrange(Rect r) override {
    Widget::arrange(r);
    if (Application::exists()) {            // the OS drags the window from this strip; buttons stay clickable
      Rect cap{r.x, r.y, std::max(0.0f, buttonsLeft() - r.x), r.h};
      Application::get().platform().setWindowCaption(m_id, cap);
    }
  }

  void paint(Canvas& c) override {
    const Theme& t = theme();
    const bool active = !Application::exists() || Application::get().activeWindow() == m_id;
    c.fillRect(m_rect, t.header);
    c.fillRect(Rect{m_rect.x, m_rect.bottom() - 1.0f, m_rect.w, 1.0f}, t.border);
    float tr = tabRight();
    Rect hatch{tr + 16.0f, m_rect.y + 8.0f, std::max(0.0f, buttonsLeft() - tr - 26.0f), m_rect.h - 16.0f};
    if (hatch.w > 4) {
      c.pushClip(hatch);
      for (float x = hatch.x - hatch.h; x < hatch.right(); x += 6.0f)
        c.line(Vec2(x, hatch.bottom()), Vec2(x + hatch.h, hatch.y), Color(255, 255, 255, active ? 12 : 7), 1.0f);
      c.popClip();
    }
    float tw = tr - m_rect.x;
    Vec2 poly[4] = {Vec2(m_rect.x, m_rect.y), Vec2(m_rect.x + tw, m_rect.y), Vec2(m_rect.x + tw + 10.0f, m_rect.bottom() - 1.0f), Vec2(m_rect.x, m_rect.bottom() - 1.0f)};
    c.fillPolygon(std::span<const Vec2>(poly, 4), t.accent.alpha(active ? 0.22f : 0.10f), false);
    c.fillRect(Rect{m_rect.x, m_rect.bottom() - 3.0f, tw + 7.0f, 2.0f}, active ? t.accent : t.border);
    float x = m_rect.x + 14.0f;
    if (m_o.icon != Icon::None) { drawIcon(c, m_o.icon, Rect{x, m_rect.y + 8.0f, 16, 16}, active ? t.accentHover : t.textDim); x += 24.0f; }
    c.textIn(*t.semibold, Rect{x, m_rect.y, tw, m_rect.h - 2.0f}, m_o.title, active ? t.textBright : t.textDim, HAlign::Left, false);
    for (int b = 0; b < 3; b++) {
      if (!has(b)) continue;
      Rect r = btnRect(b);
      bool hot = m_hover == b;
      if (hot) {
        Vec2 q[4] = {Vec2(r.x + 5, r.y), Vec2(r.right(), r.y), Vec2(r.right() - 5, r.bottom()), Vec2(r.x, r.bottom())};
        c.fillPolygon(std::span<const Vec2>(q, 4), b == 2 ? Color(206, 64, 64) : t.controlHover, false);
      }
      glyph(c, b, r, hot ? Color(255, 255, 255) : (active ? t.textDim : t.textDisabled));
    }
  }

  bool onMouseDown(const MouseEvent& e) override {
    if (e.button != MouseButton::Left) return false;
    int b = hitButton(e.pos);
    if (b >= 0) { act(b); return true; }
    if (e.clicks >= 2 && m_o.maximize) { Application::get().platform().toggleMaximizeWindow(m_id); invalidate(); return true; }
    return true;
  }
  bool onMouseMove(const MouseEvent& e) override {
    int h = hitButton(e.pos);
    if (h != m_hover) { m_hover = h; invalidate(); }
    return false;
  }
  void onMouseLeave() override { if (m_hover != -1) { m_hover = -1; invalidate(); } }
  Cursor cursor() const override { return Cursor::Arrow; }

private:
  bool has(int b) const { return b == 2 || (b == 0 && m_o.minimize) || (b == 1 && m_o.maximize); }
  Rect btnRect(int b) const {            // 0 minimize, 1 maximize / restore, 2 close (right-aligned)
    const float w = 36.0f, h = 22.0f, gap = 2.0f;
    int fromRight = 2 - b;
    int hidden = 0;
    for (int k = b + 1; k < 3; k++) if (!has(k)) hidden++;
    fromRight -= hidden;
    return Rect{m_rect.right() - 6.0f - w * float(fromRight + 1) - gap * float(fromRight), m_rect.y + (m_rect.h - h) * 0.5f - 1.0f, w, h};
  }
  float buttonsLeft() const {
    for (int b = 0; b < 3; b++) if (has(b)) return btnRect(b).x - 6.0f;
    return m_rect.right();
  }
  float tabRight() const {
    const Theme& t = theme();
    return m_rect.x + 14.0f + (m_o.icon != Icon::None ? 24.0f : 0.0f) + std::ceil(t.semibold->width(m_o.title)) + 18.0f;
  }
  int hitButton(Vec2 p) const {
    for (int b = 0; b < 3; b++) if (has(b) && btnRect(b).contains(p)) return b;
    return -1;
  }
  void act(int b) {
    Application& app = Application::get();
    if (b == 0) app.platform().minimizeWindow(m_id);
    else if (b == 1) app.platform().toggleMaximizeWindow(m_id);
    else if (m_id == 0) app.quit();
    else { int id = m_id; app.addTimer(0, [id] { Application::get().closeWindow(id); return false; }); }   // never destroy the window mid-event
    invalidate();
  }
  void glyph(Canvas& c, int b, Rect r, Color col) const {
    Vec2 m = r.center();
    const float s = 5.0f, w = 1.3f;
    if (b == 0) c.line({m.x - s, m.y + 3.0f}, {m.x + s, m.y + 3.0f}, col, w);
    else if (b == 1) {
      bool max = Application::exists() && Application::get().platform().windowMaximized(m_id);
      if (!max) c.strokeRect({m.x - s, m.y - s + 0.5f, s * 2, s * 2 - 1.0f}, col, 1);
      else {                                                      // restore: two overlapping frames
        c.strokeRect({m.x - s, m.y - s + 3.0f, s * 2 - 3.0f, s * 2 - 3.0f}, col, 1);
        c.line({m.x - s + 3.0f, m.y - s + 3.0f}, {m.x - s + 3.0f, m.y - s}, col, w);
        c.line({m.x - s + 3.0f, m.y - s}, {m.x + s, m.y - s}, col, w);
        c.line({m.x + s, m.y - s}, {m.x + s, m.y + s - 3.0f}, col, w);
        c.line({m.x + s, m.y + s - 3.0f}, {m.x + s - 3.0f, m.y + s - 3.0f}, col, w);
      }
    } else {
      c.line({m.x - s + 0.5f, m.y - s + 0.5f}, {m.x + s - 0.5f, m.y + s - 0.5f}, col, w);
      c.line({m.x + s - 0.5f, m.y - s + 0.5f}, {m.x - s + 0.5f, m.y + s - 0.5f}, col, w);
    }
  }
  int m_id;
  WindowChromeOptions m_o;
  int m_hover = -1;
};

} // namespace

Ref<Widget> wrapWithWindowChrome(Ref<Widget> content, int windowId, const WindowChromeOptions& opt) {
  const Theme& t = theme();
  auto frame = make<Border>();
  frame->setBackground(t.panel);
  frame->setBorder(t.borderStrong, Margin(1));
  auto col = vbox();
  col->add(make<OsTitleBar>(windowId, opt));
  col->add(std::move(content), {.fill = 1});
  frame->setChild(col);
  return frame;
}

} // namespace prism
