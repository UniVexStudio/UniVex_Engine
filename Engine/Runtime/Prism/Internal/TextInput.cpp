// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/TextInput.h"
#include "prism/Application.h"
#include <cstdlib>

namespace prism {

// ------------------------------------------------------------------ TextEditState
namespace {
bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
std::string fmt(float v, int dec) {
  char buf[64];
  snprintf(buf, sizeof buf, "%.*f", dec, double(v));
  std::string s = buf;
  if (s.size() > 1 && s[0] == '-' && s.find_first_not_of("-0.") == std::string::npos) s.erase(0, 1);
  return s;
}
} // namespace

size_t TextEditState::wordLeft(size_t i) const {
  while (i > 0 && isSpace(text[i - 1])) i--;
  while (i > 0 && !isSpace(text[i - 1])) i--;
  return i;
}
size_t TextEditState::wordRight(size_t i) const {
  while (i < text.size() && !isSpace(text[i])) i++;
  while (i < text.size() && isSpace(text[i])) i++;
  return i;
}
void TextEditState::deleteSelection() {
  if (!hasSelection()) return;
  size_t a = selStart(), b = selEnd();
  text.erase(a, b - a);
  caret = anchor = a;
}
void TextEditState::insert(std::string_view s) {
  deleteSelection();
  text.insert(caret, s);
  caret += s.size();
  anchor = caret;
}
void TextEditState::backspace(bool word) {
  if (hasSelection()) { deleteSelection(); return; }
  if (caret == 0) return;
  size_t t = word ? wordLeft(caret) : utf8Prev(text, caret);
  text.erase(t, caret - t);
  caret = anchor = t;
}
void TextEditState::del(bool word) {
  if (hasSelection()) { deleteSelection(); return; }
  if (caret >= text.size()) return;
  size_t t = word ? wordRight(caret) : utf8Next(text, caret);
  text.erase(caret, t - caret);
  anchor = caret;
}
void TextEditState::moveLeft(bool select, bool word) {
  if (!select && hasSelection()) { caret = anchor = selStart(); return; }
  moveTo(caret == 0 ? 0 : (word ? wordLeft(caret) : utf8Prev(text, caret)), select);
}
void TextEditState::moveRight(bool select, bool word) {
  if (!select && hasSelection()) { caret = anchor = selEnd(); return; }
  moveTo(word ? wordRight(caret) : utf8Next(text, caret), select);
}
void TextEditState::selectWordAt(size_t i) {
  size_t a = std::min(i, text.size()), b = a;
  while (a > 0 && !isSpace(text[a - 1])) a--;
  while (b < text.size() && !isSpace(text[b])) b++;
  anchor = a;
  caret = b;
}

// ------------------------------------------------------------------ TextInput
TextInput::TextInput(std::string text, std::string placeholder) : m_placeholder(std::move(placeholder)) {
  m_ed.setText(std::move(text));
}

void TextInput::setText(std::string t, bool notify) {
  if (t == m_ed.text && !notify) return;
  m_ed.setText(std::move(t));
  m_scroll = 0;
  invalidate();
  if (notify) changed();
}

Rect TextInput::textRect() const {
  float px = m_frameless ? 2.f : 7.f;
  Rect r{m_rect.x + px, m_rect.y, m_rect.w - 2 * px, m_rect.h};
  if (m_icon != Icon::None) { r.x += 18; r.w -= 18; }
  if (m_clearable && !m_ed.text.empty()) r.w -= 18;
  return r;
}

float TextInput::originX(const Rect& tr) const {
  float tw = fontFor(m_font).width(m_ed.text);
  if (m_center && tw <= tr.w) return tr.x + std::floor((tr.w - tw) * 0.5f);
  return tr.x - m_scroll;
}

size_t TextInput::indexAtX(float x) const {
  Rect tr = textRect();
  return fontFor(m_font).indexAt(m_ed.text, x - originX(tr));
}

void TextInput::ensureCaretVisible() {
  Rect tr = textRect();
  const Font& f = fontFor(m_font);
  float tw = f.width(m_ed.text), cx = f.widthTo(m_ed.text, m_ed.caret);
  if (tw <= tr.w - 1) { m_scroll = 0; return; }
  if (cx - m_scroll > tr.w - 2) m_scroll = cx - tr.w + 2;
  if (cx - m_scroll < 0) m_scroll = cx;
  m_scroll = std::clamp(m_scroll, 0.f, std::max(0.f, tw - tr.w + 2));
}

void TextInput::resetBlink() { m_caretOn = true; invalidate(); }

void TextInput::changed() {
  resetBlink();
  if (onChanged) onChanged(m_ed.text);
}

void TextInput::paint(Canvas& c) {
  const Theme& t = theme();
  const Font& f = fontFor(m_font);
  if (!m_frameless) {
    c.fillRoundRect(m_rect, 3, t.field);
    c.strokeRoundRect(m_rect, 3, m_focused ? t.accent : (m_hovered ? t.borderStrong : t.borderSoft));
  }
  if (m_icon != Icon::None)
    drawIcon(c, m_icon, {m_rect.x + 5, m_rect.y + (m_rect.h - 16) * 0.5f, 16, 16}, t.textDim, 1.3f);
  bool showClear = m_clearable && !m_ed.text.empty() && (m_hovered || m_focused);
  if (m_clearable && !m_ed.text.empty())
    if (showClear) drawIcon(c, Icon::Close, {m_rect.right() - 20, m_rect.y + (m_rect.h - 16) * 0.5f, 16, 16}, t.textDim, 1.3f);
  Rect tr = textRect();
  ensureCaretVisible();
  float ox = originX(tr);
  c.pushClip({tr.x, m_rect.y, tr.w, m_rect.h});
  if (m_ed.text.empty()) {
    c.textIn(f, {tr.x, m_rect.y, tr.w, m_rect.h}, m_placeholder, t.textDim, m_center ? HAlign::Center : HAlign::Left);
  } else {
    if (m_ed.hasSelection() && m_focused) {
      float x0 = ox + f.widthTo(m_ed.text, m_ed.selStart()), x1 = ox + f.widthTo(m_ed.text, m_ed.selEnd());
      c.fillRect({x0, m_rect.y + 3, x1 - x0, m_rect.h - 6}, t.accent.alpha(0.55f));
    }
    c.textIn(f, {ox, m_rect.y, f.width(m_ed.text) + 4, m_rect.h}, m_ed.text, enabled() ? t.text : t.textDisabled, HAlign::Left, false);
  }
  if (m_focused && m_caretOn) {
    float cx = ox + f.widthTo(m_ed.text, m_ed.caret);
    c.fillRect({std::floor(cx), m_rect.y + 4, 1, m_rect.h - 8}, t.textBright);
  }
  c.popClip();
}

bool TextInput::onMouseDown(const MouseEvent& e) {
  if (e.button != MouseButton::Left) return false;
  if (m_clearable && !m_ed.text.empty() && e.pos.x > m_rect.right() - 22) {
    setText("", true);
    return true;
  }
  size_t i = indexAtX(e.pos.x);
  if (e.clicks >= 3) m_ed.selectAll();
  else if (e.clicks == 2) m_ed.selectWordAt(i);
  else m_ed.moveTo(i, (e.mods & ModShift) != 0);
  m_dragSel = e.clicks == 1;
  resetBlink();
  return true;
}

bool TextInput::onMouseMove(const MouseEvent& e) {
  if (!m_dragSel) return false;
  m_ed.moveTo(indexAtX(e.pos.x), true);
  resetBlink();
  return true;
}

bool TextInput::onKeyDown(const KeyEvent& e) {
  bool ctrl = (e.mods & ModCtrl) != 0, shift = (e.mods & ModShift) != 0;
  Application& app = Application::get();
  switch (e.key) {
  case Key::Left: m_ed.moveLeft(shift, ctrl); break;
  case Key::Right: m_ed.moveRight(shift, ctrl); break;
  case Key::Home: m_ed.moveTo(0, shift); break;
  case Key::End: m_ed.moveTo(m_ed.text.size(), shift); break;
  case Key::Backspace: if (m_readOnly) return true; m_ed.backspace(ctrl); changed(); break;
  case Key::Delete: if (m_readOnly) return true; m_ed.del(ctrl); changed(); break;
  case Key::Enter: if (onCommit) onCommit(m_ed.text); resetBlink(); return true;
  case Key::Escape: if (onCancel) { onCancel(); return true; } return false;
  case Key::A: if (!ctrl) return false; m_ed.selectAll(); break;
  case Key::C: if (!ctrl) return false; if (m_ed.hasSelection()) app.setClipboard(m_ed.selected()); break;
  case Key::X:
    if (!ctrl) return false;
    if (m_ed.hasSelection() && !m_readOnly) { app.setClipboard(m_ed.selected()); m_ed.deleteSelection(); changed(); }
    break;
  case Key::V: {
    if (!ctrl) return false;
    if (m_readOnly) return true;
    std::string s = app.clipboard();
    std::erase_if(s, [](char ch) { return ch == '\n' || ch == '\r' || ch == '\t'; });
    if (!s.empty()) { m_ed.insert(s); changed(); }
    break;
  }
  default: return false;
  }
  resetBlink();
  return true;
}

bool TextInput::onTextInput(const std::string& s) {
  if (m_readOnly || s.empty() || (unsigned char)s[0] < 32) return true;
  m_ed.insert(s);
  changed();
  return true;
}

void TextInput::onFocusChanged(bool f) {
  Application& app = Application::get();
  if (m_timer) { app.cancelTimer(m_timer); m_timer = 0; }
  if (f) {
    m_caretOn = true;
    std::weak_ptr<Widget> w = weak_from_this();
    m_timer = app.addTimer(530, [w] {
      auto s = w.lock();
      if (!s || !s->focused()) return false;
      auto* ti = static_cast<TextInput*>(s.get());
      ti->m_caretOn = !ti->m_caretOn;
      ti->invalidate();
      return true;
    });
  } else if (onBlur) {
    onBlur();
  }
  invalidate();
}

// ------------------------------------------------------------------ NumberField
NumberField::NumberField(float v, float step, int dec) : m_value(v), m_step(step), m_speed(step), m_dec(dec) {
  m_edit = make<TextInput>();
  m_edit->setFrameless(true);
  m_edit->setCentered(true);
  m_edit->setVisible(false);
  addChild(m_edit);
  m_edit->onCommit = [this](const std::string&) { endEdit(true); };
  m_edit->onCancel = [this] { endEdit(false); };
  m_edit->onBlur = [this] { if (m_editing) endEdit(true); };
}

Vec2 NumberField::measure(Vec2) {
  m_edit->measure({100, 100});
  return m_desired = constrain({70, theme().controlH});
}

void NumberField::arrange(Rect r) {
  m_rect = r;
  float leftPad = m_hasStrip ? (!m_axisLabel.empty() ? 17.0f : 4.0f) : 1.0f;
  m_edit->arrange(r.deflated(Margin(leftPad, 1, 1, 1)));
}

void NumberField::setValue(float v, bool notify) {
  v = clampv(v);
  if (v == m_value) return;
  m_value = v;
  invalidate();
  if (notify && onChange) onChange(v);
}

void NumberField::paint(Canvas& c) {
  const Theme& t = theme();
  bool en = enabled();
  c.fillRoundRect(m_rect, 3, t.field);
  Color border = m_editing || m_dragging ? t.accent : (m_hovered && en ? t.borderStrong : t.borderSoft);
  if (m_hasStrip) {
    if (!m_axisLabel.empty()) {
      Rect tagRect{m_rect.x, m_rect.y, 16.0f, m_rect.h};
      c.fillRoundRect(tagRect, 2.0f, en ? m_strip : t.textDisabled);
      c.textIn(*t.small, Rect{tagRect.x, tagRect.y - 1.0f, tagRect.w, tagRect.h}, m_axisLabel, Color(255, 255, 255), HAlign::Center);
    } else {
      c.fillRoundRect({m_rect.x, m_rect.y, 4, m_rect.h}, 2, en ? m_strip : t.textDisabled);
    }
  }
  c.strokeRoundRect(m_rect, 3, border);
  if (m_editing) { m_edit->paint(c); return; }
  float leftPad = m_hasStrip ? (!m_axisLabel.empty() ? 18.0f : 8.0f) : 4.0f;
  Rect tr = m_rect.deflated(Margin(leftPad, 0, 4, 0));
  c.textIn(*t.ui, tr, fmt(m_value, m_dec) + m_suffix, !en ? t.textDisabled : (m_hovered || m_dragging ? t.textBright : t.text), HAlign::Center);
}

bool NumberField::onMouseDown(const MouseEvent& e) {
  if (e.button != MouseButton::Left || m_editing) return false;
  m_down = true;
  m_dragging = false;
  m_pressX = e.pos.x;
  m_startValue = m_value;
  return true;
}

bool NumberField::onMouseMove(const MouseEvent& e) {
  if (!m_down) return false;
  float dx = e.pos.x - m_pressX;
  if (!m_dragging && std::fabs(dx) > 3) { m_dragging = true; if (onBegin) onBegin(); }
  if (m_dragging) {
    float mod = (e.mods & ModShift) ? 0.1f : ((e.mods & ModCtrl) ? 10.f : 1.f);
    float v = clampv(m_startValue + dx * m_speed * mod);
    float q = std::pow(10.f, float(m_dec));
    setValue(std::round(v * q) / q, true);
  }
  return true;
}

bool NumberField::onMouseUp(const MouseEvent&) {
  if (!m_down) return false;
  m_down = false;
  if (m_dragging) {
    m_dragging = false;
    if (onCommit) onCommit(m_value);
    invalidate();
  } else if (enabled()) {
    beginEdit();
  }
  return true;
}

void NumberField::beginEdit() {
  m_editing = true;
  m_edit->setVisible(true);
  m_edit->setText(fmt(m_value, m_dec));
  Application::get().setFocus(m_edit.get());
  m_edit->selectAll();
  invalidate();
}

void NumberField::endEdit(bool apply) {
  if (!m_editing) return;
  m_editing = false;
  std::string s = m_edit->text();
  m_edit->setVisible(false);
  if (m_edit->focused()) Application::get().setFocus(nullptr);
  if (apply) {
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str()) {
      float nv = clampv(float(v));
      if (nv != m_value) {
        if (onBegin) onBegin();
        setValue(nv, true);
        if (onCommit) onCommit(m_value);
      }
    }
  }
  invalidate();
}

// ------------------------------------------------------------------ Vec3Field
Vec3Field::Vec3Field(float step, int dec) {
  const Theme& t = theme();
  Color axis[3] = {t.axisX, t.axisY, t.axisZ};
  const char* labels[3] = {"X", "Y", "Z"};
  for (int i = 0; i < 3; i++) {
    m_f[i] = make<NumberField>(0.f, step, dec);
    m_f[i]->setAxisLabel(labels[i], axis[i]);
    m_f[i]->onChange = [this, i](float v) { if (onChange) onChange(i, v); };
    m_f[i]->onCommit = [this](float) { if (onCommit) onCommit(); };
    m_f[i]->onBegin = [this] { if (onBegin) onBegin(); };
    addChild(m_f[i]);
  }
}

void Vec3Field::setValue(float x, float y, float z, bool notify) {
  m_f[0]->setValue(x, notify);
  m_f[1]->setValue(y, notify);
  m_f[2]->setValue(z, notify);
}

void Vec3Field::arrange(Rect r) {
  m_rect = r;
  float gap = 4, w = std::floor((r.w - gap * 2) / 3);
  for (int i = 0; i < 3; i++) {
    float x = r.x + float(i) * (w + gap);
    float cw = i == 2 ? r.right() - x : w;
    m_f[i]->measure({cw, r.h});
    m_f[i]->arrange({x, r.y, cw, r.h});
  }
}

} // namespace prism
