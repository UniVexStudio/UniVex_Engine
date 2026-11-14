// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Controls.h"
#include "prism/AccessibilityBridge.h"
#include "prism/Application.h"

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
const Color kPopupBg = Color::hex(0x252527);
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
    if (m_checked) bg = down ? t.accentDim.darken(0.2f) : t.accentDim;
    else bg = down ? t.controlPress : (hot ? t.controlHover : Color(0, 0, 0, 0));
    if (m_checked && en) fg = t.textBright;
    break;
  }
  if (bg.a) c.fillRoundRect(m_rect, 3, bg);
  if (border.a) c.strokeRoundRect(m_rect, 3, border);
  Color ic = m_hasIconColor ? m_iconColor.get() : fg;
  if (!en) ic = t.textDisabled;
  if (m_checked && m_style == ButtonStyle::Toolbar && en && !m_hasIconColor) ic = t.accentHover.lighten(0.15f);
  const Font& f = *t.ui;
  float tw = m_text.empty() ? 0 : std::ceil(f.width(m_text));
  float cw = (m_icon != Icon::None ? 16.f : 0.f) + (m_icon != Icon::None && tw > 0 ? 6.f : 0.f) + tw;
  float x = std::floor(m_rect.x + (m_rect.w - cw) * 0.5f);
  if (m_icon != Icon::None) {
    drawIcon(c, m_icon, {x, m_rect.y + (m_rect.h - 16) * 0.5f, 16, 16}, ic);
    x += 22;
  }
  if (tw > 0) c.textIn(f, {x, m_rect.y, tw + 1, m_rect.h}, m_text, fg, HAlign::Left, false);
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
    c.fillRoundRect(b, 3, m_hovered ? t.accentHover : t.accent);
    drawIcon(c, Icon::Check, b.inflated(-1), t.accentText, 1.7f);
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
    if (sel) c.fillRoundRect(r, 3, t.accent);
    else if (hot) c.fillRoundRect(r, 3, t.controlHover);
    Color fg = sel ? t.accentText : (hot ? t.textBright : t.text);
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
    c.fillRoundRect({m_rect.x + 1, m_rect.y + 1, m_rect.w - 2, m_rect.h - 2}, 2, (m_hovered || m_drag) ? t.accent.alpha(0.75f) : t.accent.alpha(0.6f));
    c.popClip();
  }
  c.strokeRoundRect(m_rect, 3, m_drag ? t.accent : (m_hovered && en ? t.borderStrong : t.borderSoft));
  c.textIn(*t.ui, m_rect, fmtNum(m_value, m_dec, m_suffix), en ? t.textBright : t.textDisabled, HAlign::Center);
}

// ------------------------------------------------------------------ ComboBox popup
namespace {
class ListPopup : public Widget {
public:
  ListPopup(std::vector<std::string> items, int sel, float minW, std::function<void(int)> pick)
      : m_items(std::move(items)), m_sel(sel), m_hover(sel), m_minW(minW), m_pick(std::move(pick)) {}
  Vec2 measure(Vec2) override {
    float w = m_minW;
    for (auto& s : m_items) w = std::max(w, std::ceil(theme().ui->width(s)) + 40);
    return m_desired = {w, float(m_items.size()) * 22 + 8};
  }
  void paint(Canvas& c) override {
    const Theme& t = theme();
    c.fillRoundRect(m_rect, 4, kPopupBg);
    c.strokeRoundRect(m_rect, 4, t.borderStrong);
    for (int i = 0; i < int(m_items.size()); i++) {
      Rect r = rowRect(i);
      bool hot = i == m_hover;
      if (hot) c.fillRoundRect(r, 3, t.accent);
      if (i == m_sel) drawIcon(c, Icon::Check, {r.x + 4, r.y + 3, 16, 16}, hot ? t.accentText : t.accent);
      c.textIn(*t.ui, {r.x + 26, r.y, r.w - 30, r.h}, m_items[size_t(i)], hot ? t.accentText : t.text);
    }
  }
  bool onMouseMove(const MouseEvent& e) override {
    int i = rowAt(e.pos);
    if (i != m_hover && i >= 0) { m_hover = i; invalidate(); }
    return true;
  }
  bool onMouseDown(const MouseEvent& e) override { return e.button == MouseButton::Left; }
  bool onMouseUp(const MouseEvent& e) override {
    int i = rowAt(e.pos);
    if (i >= 0) pick(i);
    return true;
  }
  bool onKeyDown(const KeyEvent& e) override {
    int n = int(m_items.size());
    if (e.key == Key::Down) m_hover = (m_hover + 1) % n;
    else if (e.key == Key::Up) m_hover = (m_hover + n - 1) % n;
    else if (e.key == Key::Enter) pick(m_hover);
    else return false;
    invalidate();
    return true;
  }
private:
  Rect rowRect(int i) const { return {m_rect.x + 4, m_rect.y + 4 + float(i) * 22, m_rect.w - 8, 22}; }
  int rowAt(Vec2 p) const {
    for (int i = 0; i < int(m_items.size()); i++) if (rowRect(i).contains(p)) return i;
    return -1;
  }
  void pick(int i) {
    auto cb = m_pick;
    Application::get().closePopup(this);
    if (cb && i >= 0) cb(i);
  }
  std::vector<std::string> m_items;
  int m_sel, m_hover;
  float m_minW;
  std::function<void(int)> m_pick;
};
} // namespace

void ComboBox::setSelected(int i, bool notify) {
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
  c.strokeRoundRect(m_rect, 3, m_open ? t.accent : (m_hovered && en ? t.borderStrong : t.borderSoft));
  c.textIn(*t.ui, {m_rect.x + 8, m_rect.y, m_rect.w - 28, m_rect.h}, text(), en ? t.text : t.textDisabled);
  drawIcon(c, Icon::ChevronDown, {m_rect.right() - 20, m_rect.y + (m_rect.h - 16) * 0.5f, 16, 16}, en ? t.textDim : t.textDisabled, 1.4f);
}

bool ComboBox::onMouseDown(const MouseEvent& e) {
  if (e.button != MouseButton::Left) return false;
  Application& app = Application::get();
  bool s = m_suppress;
  m_suppress = false;
  if (s && app.popupJustDismissed()) return true;
  auto pop = make<ListPopup>(m_items, m_sel, m_rect.w, [this](int i) { setSelected(i, true); });
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
  if (m_v > 0) c.fillRoundRect({m_rect.x, m_rect.y, std::max(m_rect.h, m_rect.w * m_v), m_rect.h}, m_rect.h * 0.5f, t.accent);
}

void Separator::paint(Canvas& c) {
  Color col = m_c.a ? m_c : theme().borderSoft;
  if (m_o == Orientation::Vertical) c.fillRect({std::floor(m_rect.x + m_rect.w * 0.5f), m_rect.y, 1, m_rect.h}, col);
  else c.fillRect({m_rect.x, std::floor(m_rect.y + m_rect.h * 0.5f), m_rect.w, 1}, col);
}

} // namespace prism
