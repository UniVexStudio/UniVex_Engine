// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Controls.h"
#include "prism/AccessibilityBridge.h"
#include "prism/Application.h"
#include "prism/Containers.h"

namespace prism {

const Font& fontFor(FontRole r) {
  const Theme& t = theme();
  switch (r) {
  case FontRole::Medium: return *t.medium;
  case FontRole::Semibold: return *t.semibold;
  case FontRole::Small: return *t.small;
  case FontRole::Mono: return *t.mono;
  default: return *t.ui;
  }
}

namespace {
std::string fmtNum(float v, int dec, const std::string& suffix = "") {
  char buf[64];
  snprintf(buf, sizeof buf, "%.*f", dec, double(v));
  std::string s = buf;
  if (s == "-0" || (s.size() > 1 && s[0] == '-' && s.find_first_not_of("-0.") == std::string::npos)) s.erase(0, 1);
  return s + suffix;
}
} // namespace

// ------------------------------------------------------------------ Label
Label::Label(std::string text, FontRole f) : m_text(std::move(text)), m_font(f) {}

void Label::setText(std::string t) {
  if (t == m_text) return;
  m_text = std::move(t);
  relayout();
}

Vec2 Label::measure(Vec2) {
  const Font& f = fontFor(m_font);
  return m_desired = constrain({std::ceil(f.width(m_text)) + m_pad.horizontal(), f.lineHeight() + m_pad.vertical()});
}

void Label::paint(Canvas& c) {
  Color col = enabled() ? m_color.get() : theme().textDisabled;
  c.textIn(fontFor(m_font), m_rect.deflated(m_pad), m_text, col, m_align);
}

// ------------------------------------------------------------------ Button
Button::Button(std::string text, Icon icon, ButtonStyle st)
    : m_text(std::move(text)), m_icon(icon), m_style(st) {}

Vec2 Button::measure(Vec2) {
  const Theme& t = theme();
  const Font& f = *t.ui;
  bool iconOnly = m_text.empty();
  float w = 0, h = m_style == ButtonStyle::Toolbar ? 26.f : t.controlH + 2;
  if (m_icon != Icon::None) w += 16;
  if (m_icon != Icon::None && !iconOnly) w += 6;
  if (!iconOnly) w += std::ceil(f.width(m_text));
  w += iconOnly ? 10 : (m_style == ButtonStyle::Toolbar ? 14 : 20);
  if (iconOnly) w = std::max(w, h);
  return m_desired = constrain({w, h});
}

void Button::paint(Canvas& c) {
  const Theme& t = theme();
  bool en = enabled(), hot = m_hovered && en, down = m_pressed && hot;
  Color bg{0, 0, 0, 0}, border{0, 0, 0, 0}, fg = en ? t.text : t.textDisabled;
  switch (m_style) {
  case ButtonStyle::Default:
    bg = down ? t.controlPress : (hot ? t.controlHover : t.control);
    border = hot ? t.borderStrong : t.borderSoft;
    break;
  case ButtonStyle::Primary:
    bg = !en ? t.control : (down ? t.accent.darken(0.2f) : (hot ? t.accentHover : t.accent));
    fg = en ? t.accentText : t.textDisabled;
    break;
  case ButtonStyle::Flat:
    bg = down ? t.controlPress : (hot ? t.controlHover : Color(0, 0, 0, 0));
    break;
  case ButtonStyle::Success:
    bg = !en ? t.control : (down ? t.success.darken(0.2f) : (hot ? t.success.lighten(0.1f) : t.success));
    fg = en ? Color(255, 255, 255) : t.textDisabled;
    break;
  case ButtonStyle::Toolbar:
    if (m_checked) bg = down ? t.rowSelected.darken(0.2f) : t.rowSelected;
    else bg = down ? t.controlPress : (hot ? t.controlHover : Color(0, 0, 0, 0));
    if (m_checked && en) fg = t.textBright;
    break;
  }
  if (bg.a) c.fillRoundRect(m_rect, 3, bg);
  if (border.a) c.strokeRoundRect(m_rect, 3, border);
  Color ic = m_hasIconColor ? m_iconColor.get() : fg;
  if (!en) ic = t.textDisabled;
  if (m_checked && m_style == ButtonStyle::Toolbar && en && !m_hasIconColor) ic = t.textBright;
  const Font& f = *t.ui;
  const float tw = m_text.empty() ? 0.f : std::ceil(f.width(m_text));
  const float iconW = m_icon != Icon::None ? 16.f : 0.f;
  const float gap = (m_icon != Icon::None && tw > 0.f) ? 6.f : 0.f;
  const float cw = iconW + gap + tw;
  const float pad = m_text.empty() ? 0.f : std::min(6.f, std::max(0.f, m_rect.w * 0.15f));
  const Rect inner{m_rect.x + pad, m_rect.y, std::max(0.f, m_rect.w - pad * 2.f), m_rect.h};
  if (cw <= inner.w) {
    float x = std::floor(m_rect.x + (m_rect.w - cw) * 0.5f);
    if (m_icon != Icon::None) {
      drawIcon(c, m_icon, {x, m_rect.y + (m_rect.h - 16.f) * 0.5f, 16.f, 16.f}, ic);
      x += 22.f;
    }
    if (tw > 0.f) c.textIn(f, {x, m_rect.y, tw + 1.f, m_rect.h}, m_text, fg, HAlign::Left, false);
  } else if (m_rect.w > 0.f && m_rect.h > 0.f) {
    c.pushClip(m_rect);
    float x = inner.x;
    if (m_icon != Icon::None) {
      drawIcon(c, m_icon, {x, m_rect.y + (m_rect.h - 16.f) * 0.5f, 16.f, 16.f}, ic);
      x += 22.f;
    }
    const float availW = inner.right() - x;
    if (tw > 0.f && availW > 0.f) c.textIn(f, {x, m_rect.y, availW, m_rect.h}, m_text, fg, HAlign::Left, true);
    c.popClip();
  }
}

bool Button::onMouseDown(const MouseEvent& e) {
  if (e.button != MouseButton::Left) return false;
  m_pressed = true;
  invalidate();
  return true;
}

bool Button::onKeyDown(const KeyEvent& e) {
  if (e.key != Key::Space && e.key != Key::Enter) return false;
  if (m_checkable) { m_checked = !m_checked; if (onToggle) onToggle(m_checked); }
  if (onClick) onClick();
  invalidate();
  return true;
}

bool Button::onMouseUp(const MouseEvent& e) {
  bool fire = m_pressed && m_rect.contains(e.pos);
  m_pressed = false;
  invalidate();
  if (fire) {
    if (m_checkable) { m_checked = !m_checked; if (onToggle) onToggle(m_checked); }
    if (onClick) onClick();
  }
  return true;
}

// ------------------------------------------------------------------ CheckBox
Vec2 CheckBox::measure(Vec2) {
  float w = 14 + (m_label.empty() ? 0.f : 7.f + std::ceil(theme().ui->width(m_label)));
  return m_desired = constrain({w, theme().controlH});
}

void CheckBox::paint(Canvas& c) {
  const Theme& t = theme();
  bool en = enabled();
  Rect b{m_rect.x, std::floor(m_rect.y + (m_rect.h - 14) * 0.5f), 14, 14};
  if (m_checked && en) {
    c.fillRoundRect(b, 3, m_hovered ? t.highlight.lighten(0.1f) : t.highlight);
    drawIcon(c, Icon::Check, b.inflated(-1), t.window, 1.7f);
  } else {
    c.fillRoundRect(b, 3, t.field);
    c.strokeRoundRect(b, 3, m_hovered && en ? t.textDim : t.borderStrong);
    if (m_checked) drawIcon(c, Icon::Check, b.inflated(-1), t.textDisabled, 1.7f);
  }
  if (!m_label.empty())
    c.textIn(*t.ui, {m_rect.x + 21, m_rect.y, m_rect.w - 21, m_rect.h}, m_label, en ? t.text : t.textDisabled);
}

bool CheckBox::onMouseUp(const MouseEvent& e) {
  if (m_rect.contains(e.pos)) setChecked(!m_checked, true);
  return true;
}

// ------------------------------------------------------------------ Segmented
float Segmented::segW(int i) const {
  const Item& it = m_items[size_t(i)];
  float w = it.icon != Icon::None ? 16.f : 0.f;
  if (!it.text.empty()) w += (it.icon != Icon::None ? 6.f : 0.f) + std::ceil(theme().ui->width(it.text));
  return std::max(28.f, w + (it.text.empty() ? 12.f : 20.f));
}

Vec2 Segmented::measure(Vec2) {
  float w = 4;
  for (int i = 0; i < int(m_items.size()); i++) w += segW(i);
  return m_desired = constrain({w, 26});
}

int Segmented::indexAt(Vec2 p) const {
  float x = m_rect.x + 2;
  for (int i = 0; i < int(m_items.size()); i++) {
    float w = segW(i);
    if (p.x >= x && p.x < x + w && m_rect.contains(p)) return i;
    x += w;
  }
  return -1;
}

const std::string& Segmented::tooltipAt(Vec2 p) const {
  static const std::string none;
  int i = indexAt(p);
  return i >= 0 ? m_items[size_t(i)].tip : none;
}

void Segmented::paint(Canvas& c) {
  const Theme& t = theme();
  c.fillRoundRect(m_rect, 4, t.field);
  c.strokeRoundRect(m_rect, 4, t.borderSoft);
  float x = m_rect.x + 2;
  for (int i = 0; i < int(m_items.size()); i++) {
    float w = segW(i);
    Rect r{x, m_rect.y + 2, w, m_rect.h - 4};
    bool sel = i == m_sel, hot = i == m_hot && m_hovered;
    if (sel) c.fillRoundRect(r, 3, t.rowSelected);
    else if (hot) c.fillRoundRect(r, 3, t.controlHover);
    Color fg = sel ? t.textBright : (hot ? t.textBright : t.text);
    const Item& it = m_items[size_t(i)];
    float tw = it.text.empty() ? 0 : std::ceil(t.ui->width(it.text));
    float cw = (it.icon != Icon::None ? 16.f : 0.f) + (it.icon != Icon::None && tw > 0 ? 6.f : 0.f) + tw;
    float cx = std::floor(r.x + (r.w - cw) * 0.5f);
    if (it.icon != Icon::None) { drawIcon(c, it.icon, {cx, r.y + (r.h - 16) * 0.5f, 16, 16}, fg); cx += 22; }
    if (tw > 0) c.textIn(*t.ui, {cx, r.y, tw + 1, r.h}, it.text, fg, HAlign::Left, false);
    x += w;
  }
}

bool Segmented::onMouseDown(const MouseEvent& e) {
  if (e.button != MouseButton::Left) return false;
  int i = indexAt(e.pos);
  if (i < 0) return false;
  if (i != m_sel) setSelected(i, true);
  return true;
}

// ------------------------------------------------------------------ Slider
void Slider::setValue(float v, bool notify) {
  v = std::clamp(v, m_min, m_max);
  if (m_step > 0) v = m_min + std::round((v - m_min) / m_step) * m_step;
  if (v == m_value) return;
  m_value = v;
  invalidate();
  if (Application::exists()) Application::get().accessNotify(AccessEvent::ValueChanged, this);
  if (notify && onChange) onChange(v);
}

void Slider::setFromX(float x) {
  float f = std::clamp((x - m_rect.x - 2) / std::max(1.f, m_rect.w - 4), 0.f, 1.f);
  setValue(m_min + f * (m_max - m_min), true);
}

bool Slider::onMouseDown(const MouseEvent& e) {
  if (e.button != MouseButton::Left) return false;
  m_drag = true;
  setFromX(e.pos.x);
  return true;
}
bool Slider::onMouseMove(const MouseEvent& e) {
  if (m_drag) setFromX(e.pos.x);
  return m_drag;
}
bool Slider::onMouseUp(const MouseEvent&) {
  if (m_drag && onCommit) onCommit(m_value);
  m_drag = false;
  return true;
}

void Slider::paint(Canvas& c) {
  const Theme& t = theme();
  bool en = enabled();
  c.fillRoundRect(m_rect, 3, t.field);
  float f = (m_value - m_min) / std::max(1e-6f, m_max - m_min);
  Rect fill{m_rect.x + 1, m_rect.y + 1, std::floor((m_rect.w - 2) * f), m_rect.h - 2};
  if (fill.w > 0 && en) {
    c.pushClip(fill);
    c.fillRoundRect({m_rect.x + 1, m_rect.y + 1, m_rect.w - 2, m_rect.h - 2}, 2, (m_hovered || m_drag) ? t.highlight.alpha(0.75f) : t.highlight.alpha(0.6f));
    c.popClip();
  }
  c.strokeRoundRect(m_rect, 3, m_drag ? t.highlight : (m_hovered && en ? t.borderStrong : t.borderSoft));
  c.textIn(*t.ui, m_rect, fmtNum(m_value, m_dec, m_suffix), en ? t.textBright : t.textDisabled, HAlign::Center);
}

// ------------------------------------------------------------------ ComboBox popup
namespace {
class ListPopup : public Widget {
public:
  ListPopup(std::vector<std::string> items, int sel, float minW, std::function<void(int)> pick,
            std::vector<bool> disabled = {})
      : m_items(std::move(items)), m_disabled(std::move(disabled)), m_sel(sel), m_hover(sel), m_minW(minW),
        m_pick(std::move(pick)) {}
  float rowH() const { return std::max(20.f, theme().rowH); }
  float contentH() const { return float(m_items.size()) * rowH(); }
  Vec2 measure(Vec2 avail) override {
    float w = m_minW;
    for (auto& s : m_items) w = std::max(w, std::ceil(theme().ui->width(s)) + 40);
    const float maxH = avail.y > 48.f ? std::min(avail.y - 24.f, 420.f) : 420.f;
    const float rawH = contentH() + 8.f;
    const float h = std::min(rawH, maxH);
    if (rawH > h + 0.5f) w += VScrollbar::W;
    if (avail.x > 64.f) w = std::min(w, avail.x - 16.f);
    return m_desired = {w, h};
  }
  void arrange(Rect r) override {
    m_rect = r;
    clampScroll();
  }
  void paint(Canvas& c) override {
    const Theme& t = theme();
    c.fillRoundRect(m_rect, 4, t.popup);
    c.strokeRoundRect(m_rect, 4, t.borderStrong);
    const Rect inner = listRect();
    c.pushClip(inner);
    const float rh = rowH();
    const int n = int(m_items.size());
    const int first = std::max(0, int(std::floor(m_scroll / rh)));
    const int last = std::min(n - 1, int(std::floor((m_scroll + inner.h) / rh)));
    for (int i = first; i <= last; i++) {
      Rect r = rowRect(i);
      const bool off = disabled(i);
      bool hot = i == m_hover && !off;
      if (hot) c.fillRoundRect(r, 3, t.rowSelected);
      if (i == m_sel) drawIcon(c, Icon::Check, {r.x + 4, r.y + (r.h - 14.f) * 0.5f, 14, 14}, t.highlight);
      c.textIn(*t.ui, {r.x + 24, r.y, r.w - 28, r.h}, m_items[size_t(i)], off ? t.textDisabled : hot ? t.textBright : t.text);
    }
    c.popClip();
    if (VScrollbar::needed(contentH(), inner.h)) {
      m_bar.paint(c, inner, contentH(), m_scroll, m_hovered && Application::get().mousePos().x >= inner.right() - VScrollbar::W);
    }
  }
  bool onMouseMove(const MouseEvent& e) override {
    const Rect inner = listRect();
    if (m_bar.dragging) {
      m_bar.mouseMove(e.pos, inner, contentH(), m_scroll);
      clampScroll();
      invalidate();
      return true;
    }
    if (m_touchArmed) {
      if (!m_touchDragged && std::abs(e.pos.y - m_touchStartY) >= 6.f) {
        m_touchDragged = true;
      }
      if (m_touchDragged) {
        m_scroll -= (e.pos.y - m_touchLastY);
        m_touchLastY = e.pos.y;
        clampScroll();
        invalidate();
        return true;
      }
    }
    int i = rowAt(e.pos);
    if (i != m_hover && i >= 0) { m_hover = i; invalidate(); }
    return true;
  }
  bool onMouseDown(const MouseEvent& e) override {
    if (e.button != MouseButton::Left) return false;
    const Rect inner = listRect();
    const bool needBar = VScrollbar::needed(contentH(), inner.h);
    const float barHitW = (Application::exists() && Application::get().platform().singleSurface()) ? 20.f : VScrollbar::W;
    if (needBar && e.pos.x >= inner.right() - barHitW) {
      m_bar.mouseDown(e.pos, inner, contentH(), m_scroll);
      clampScroll();
      invalidate();
      return true;
    }
    if (needBar && Application::exists() && Application::get().platform().singleSurface()) {
      m_touchArmed = true;
      m_touchDragged = false;
      m_touchStartY = m_touchLastY = e.pos.y;
    }
    return true;
  }
  bool onMouseUp(const MouseEvent& e) override {
    const bool wasBar = m_bar.dragging;
    const bool wasDrag = m_touchDragged;
    m_bar.mouseUp();
    m_touchArmed = false;
    m_touchDragged = false;
    if (wasBar || wasDrag) return true;
    int i = rowAt(e.pos);
    if (i >= 0 && !disabled(i)) pick(i);
    return true;
  }
  bool onMouseWheel(const MouseEvent& e) override {
    if (!VScrollbar::needed(contentH(), listRect().h)) return false;
    m_scroll -= e.wheel.y * rowH() * 2.5f;
    clampScroll();
    invalidate();
    return true;
  }
  bool onKeyDown(const KeyEvent& e) override {
    int n = int(m_items.size());
    if (n == 0) return false;
    // The arrows step over the items that cannot be chosen.
    const auto step = [&](int dir) {
      for (int k = 0; k < n; k++) {
        m_hover = (m_hover + dir + n) % n;
        if (!disabled(m_hover)) break;
      }
      const float top = float(m_hover) * rowH();
      const float viewH = listRect().h;
      if (top < m_scroll) m_scroll = top;
      else if (top + rowH() > m_scroll + viewH) m_scroll = top + rowH() - viewH;
      clampScroll();
    };
    if (e.key == Key::Down) step(1);
    else if (e.key == Key::Up) step(-1);
    else if (e.key == Key::Enter) { if (!disabled(m_hover)) pick(m_hover); }
    else return false;
    invalidate();
    return true;
  }
private:
  Rect listRect() const { return {m_rect.x + 4.f, m_rect.y + 4.f, std::max(0.f, m_rect.w - 8.f), std::max(0.f, m_rect.h - 8.f)}; }
  void clampScroll() { m_scroll = std::clamp(m_scroll, 0.f, std::max(0.f, contentH() - listRect().h)); }
  Rect rowRect(int i) const {
    const Rect inner = listRect();
    const float w = inner.w - (VScrollbar::needed(contentH(), inner.h) ? VScrollbar::W : 0.f);
    return {inner.x, inner.y + float(i) * rowH() - m_scroll, w, rowH()};
  }
  int rowAt(Vec2 p) const {
    const Rect inner = listRect();
    if (!inner.contains(p)) return -1;
    const int i = int(std::floor((p.y - inner.y + m_scroll) / rowH()));
    return (i >= 0 && i < int(m_items.size())) ? i : -1;
  }
  void pick(int i) {
    auto cb = m_pick;
    Application::get().closePopup(this);
    if (cb && i >= 0) cb(i);
  }
  bool disabled(int i) const { return i >= 0 && i < int(m_disabled.size()) && m_disabled[size_t(i)]; }
  std::vector<std::string> m_items;
  std::vector<bool> m_disabled;
  int m_sel, m_hover;
  float m_minW;
  float m_scroll = 0.f;
  bool m_touchArmed = false;
  bool m_touchDragged = false;
  float m_touchStartY = 0.f, m_touchLastY = 0.f;
  VScrollbar m_bar;
  std::function<void(int)> m_pick;
};
} // namespace

void ComboBox::setSelected(int i, bool notify) {
  if (isItemDisabled(i)) return;
  m_sel = i;
  invalidate();
  if (notify && onChange) onChange(i);
}

Vec2 ComboBox::measure(Vec2) {
  float w = 60;
  for (auto& s : m_items) w = std::max(w, std::ceil(theme().ui->width(s)) + 34);
  return m_desired = constrain({w, theme().controlH});
}

void ComboBox::paint(Canvas& c) {
  const Theme& t = theme();
  bool en = enabled();
  c.fillRoundRect(m_rect, 3, m_hovered && en ? t.controlHover : t.control);
  c.strokeRoundRect(m_rect, 3, m_open ? t.highlight : (m_hovered && en ? t.borderStrong : t.borderSoft));
  c.textIn(*t.ui, {m_rect.x + 8, m_rect.y, m_rect.w - 28, m_rect.h}, text(), en ? t.text : t.textDisabled);
  drawIcon(c, Icon::ChevronDown, {m_rect.right() - 20, m_rect.y + (m_rect.h - 16) * 0.5f, 16, 16}, en ? t.textDim : t.textDisabled, 1.4f);
}

bool ComboBox::onMouseDown(const MouseEvent& e) {
  if (e.button != MouseButton::Left) return false;
  Application& app = Application::get();
  bool s = m_suppress;
  m_suppress = false;
  if (s && app.popupJustDismissed()) return true;
  auto pop = make<ListPopup>(m_items, m_sel, m_rect.w, [this](int i) { setSelected(i, true); }, m_disabled);
  PopupOptions o;
  o.placement = PopupPlacement::Below;
  o.anchor = {m_rect.x, m_rect.bottom() + 2, m_rect.w, 0};
  o.onClose = [this] { m_open = false; m_suppress = true; invalidate(); };
  m_open = true;
  app.showPopup(pop, o);
  return true;
}

// ------------------------------------------------------------------ ProgressBar / Separator
void ProgressBar::paint(Canvas& c) {
  const Theme& t = theme();
  c.fillRoundRect(m_rect, m_rect.h * 0.5f, t.field);
  if (m_v > 0) c.fillRoundRect({m_rect.x, m_rect.y, std::max(m_rect.h, m_rect.w * m_v), m_rect.h}, m_rect.h * 0.5f, t.highlight);
}

void Separator::paint(Canvas& c) {
  Color col = m_c.a ? m_c : theme().borderSoft;
  if (m_o == Orientation::Vertical) c.fillRect({std::floor(m_rect.x + m_rect.w * 0.5f), m_rect.y, 1, m_rect.h}, col);
  else c.fillRect({m_rect.x, std::floor(m_rect.y + m_rect.h * 0.5f), m_rect.w, 1}, col);
}

} // namespace prism
