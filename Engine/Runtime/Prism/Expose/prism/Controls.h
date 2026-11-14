// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - basic controls: Label, Button, CheckBox, Segmented, Slider, ComboBox, ProgressBar, Separator.
#pragma once
#include <cstdlib>
#include "Widget.h"

namespace prism {

enum class FontRole { Regular, Medium, Semibold, Small, Mono };
const Font& fontFor(FontRole r);

class Label : public Widget {
public:
  explicit Label(std::string text = "", FontRole f = FontRole::Regular);
  void setText(std::string t);
  const std::string& text() const { return m_text; }
  void setColor(ThemeColor c) { m_color = c; invalidate(); }
  void setFont(FontRole f) { m_font = f; relayout(); }
  void setAlign(HAlign a) { m_align = a; invalidate(); }
  void setPadding(Margin m) { m_pad = m; relayout(); }
  Vec2 measure(Vec2 avail) override;
  void paint(Canvas& c) override;
  bool hitSelf() const override { return !m_tooltip.empty(); }
  AccessRole accessRole() const override { return AccessRole::Label; }
  std::string accessName() const override { return m_accessName.empty() ? m_text : m_accessName; }
private:
  std::string m_text;
  FontRole m_font;
  ThemeColor m_color{&Theme::text};
  HAlign m_align = HAlign::Left;
  Margin m_pad;
};

enum class ButtonStyle { Default, Primary, Flat, Toolbar, Success };   // Success: a green call to action ("+ Add")

class Button : public Widget {
public:
  explicit Button(std::string text = "", Icon icon = Icon::None, ButtonStyle st = ButtonStyle::Default);
  void setText(std::string t) { m_text = std::move(t); relayout(); }
  const std::string& text() const { return m_text; }
  AccessRole accessRole() const override { return m_checkable ? AccessRole::ToggleButton : AccessRole::Button; }
  std::string accessName() const override { return !m_accessName.empty() ? m_accessName : !m_text.empty() ? m_text : m_tooltip; }
  bool accessChecked() const override { return m_checked; }
  bool accessActivate() override { KeyEvent k; k.key = Key::Space; return onKeyDown(k); }
  bool onKeyDown(const KeyEvent& e) override;
  void setIcon(Icon i) { m_icon = i; relayout(); }
  void setIconColor(ThemeColor c) { m_iconColor = c; m_hasIconColor = true; invalidate(); }
  void setStyle(ButtonStyle s) { m_style = s; relayout(); }
  void setCheckable(bool c) { m_checkable = c; }
  void setChecked(bool c) { m_checked = c; invalidate(); }
  bool checked() const { return m_checked; }
  Callback<> onClick;
  Callback<bool> onToggle;
  Vec2 measure(Vec2 avail) override;
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  void onMouseLeave() override { m_pressed = false; invalidate(); }
  bool focusable() const override { return true; }
private:
  std::string m_text;
  Icon m_icon;
  ButtonStyle m_style;
  ThemeColor m_iconColor{&Theme::text};
  bool m_hasIconColor = false, m_checkable = false, m_checked = false, m_pressed = false;
};

class CheckBox : public Widget {
public:
  explicit CheckBox(std::string label = "", bool checked = false) : m_label(std::move(label)), m_checked(checked) {}
  bool checked() const { return m_checked; }
  AccessRole accessRole() const override { return AccessRole::CheckBox; }
  std::string accessName() const override { return m_accessName.empty() ? m_label : m_accessName; }
  bool accessChecked() const override { return m_checked; }
  bool accessActivate() override { setChecked(!m_checked, true); return true; }
  bool onKeyDown(const KeyEvent& e) override { if (e.key == Key::Space || e.key == Key::Enter) { setChecked(!m_checked, true); return true; } return false; }
  void setChecked(bool c, bool notify = false) { m_checked = c; invalidate(); if (notify && onChange) onChange(c); }
  Callback<bool> onChange;
  bool focusable() const override { return true; }
  Vec2 measure(Vec2 avail) override;
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override { return e.button == MouseButton::Left; }
  bool onMouseUp(const MouseEvent& e) override;
private:
  std::string m_label;
  bool m_checked;
};

// A joined group of toggle buttons where exactly one is selected.
class Segmented : public Widget {
public:
  struct Item { std::string text; Icon icon; std::string tip; };
  Segmented& add(std::string text, Icon icon = Icon::None, std::string tip = "") {
    m_items.push_back({std::move(text), icon, std::move(tip)}); relayout(); return *this;
  }
  void setSelected(int i, bool notify = false) { m_sel = i; invalidate(); if (notify && onSelect) onSelect(i); }
  void setTip(int i, std::string tip) { if (i >= 0 && i < int(m_items.size())) m_items[size_t(i)].tip = std::move(tip); }
  int selected() const { return m_sel; }
  Callback<int> onSelect;
  Vec2 measure(Vec2 avail) override;
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override { m_hot = indexAt(e.pos); invalidate(); return false; }
  void onMouseLeave() override { m_hot = -1; invalidate(); }
  const std::string& tooltipAt(Vec2 p) const override;
private:
  int indexAt(Vec2 p) const;
  float segW(int i) const;
  std::vector<Item> m_items;
  int m_sel = 0, m_hot = -1;
};

class Slider : public Widget {
public:
  Slider(float min, float max, float value, float step = 0) : m_min(min), m_max(max), m_value(value), m_step(step) {}
  float value() const { return m_value; }
  AccessRole accessRole() const override { return AccessRole::Slider; }
  std::string accessValue() const override { return std::to_string(m_value); }
  bool accessValueEditable() const override { return true; }
  bool accessSetValue(const std::string& v) override { char* e = nullptr; float f = std::strtof(v.c_str(), &e); if (e == v.c_str()) return false; setValue(f, true); return true; }
  void setValue(float v, bool notify = false);
  void setDecimals(int d) { m_dec = d; invalidate(); }
  void setSuffix(std::string s) { m_suffix = std::move(s); invalidate(); }
  Callback<float> onChange;
  Callback<float> onCommit;
  Vec2 measure(Vec2) override { return m_desired = constrain({120, theme().controlH}); }
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  Cursor cursor() const override { return Cursor::SizeWE; }
private:
  void setFromX(float x);
  float m_min, m_max, m_value, m_step;
  int m_dec = 2;
  std::string m_suffix;
  bool m_drag = false;
};

class ComboBox : public Widget {
public:
  explicit ComboBox(std::vector<std::string> items = {}, int sel = 0) : m_items(std::move(items)), m_sel(sel) {}
  void setItems(std::vector<std::string> v) { m_items = std::move(v); relayout(); }
  void setSelected(int i, bool notify = false);
  int selected() const { return m_sel; }
  AccessRole accessRole() const override { return AccessRole::ComboBox; }
  std::string accessValue() const override { return text(); }
  bool accessValueEditable() const override { return true; }
  bool accessSetValue(const std::string& v) override { for (size_t i = 0; i < m_items.size(); i++) if (m_items[i] == v) { setSelected(int(i), true); return true; } return false; }
  std::string text() const { return m_sel >= 0 && m_sel < int(m_items.size()) ? m_items[size_t(m_sel)] : ""; }
  Callback<int> onChange;
  Vec2 measure(Vec2) override;
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  void onMouseLeave() override { m_suppress = false; }
private:
  std::vector<std::string> m_items;
  int m_sel;
  bool m_open = false, m_suppress = false;
};

class ProgressBar : public Widget {
public:
  void setValue(float v) { m_v = std::clamp(v, 0.f, 1.f); invalidate(); }
  Vec2 measure(Vec2) override { return m_desired = constrain({80, 6}); }
  void paint(Canvas& c) override;
  bool hitSelf() const override { return false; }
private:
  float m_v = 0;
};

class Separator : public Widget {
public:
  explicit Separator(Orientation o = Orientation::Vertical, Color c = {}) : m_o(o), m_c(c) {}
  Vec2 measure(Vec2) override { return m_desired = constrain(m_o == Orientation::Vertical ? Vec2(1, 0) : Vec2(0, 1)); }
  void paint(Canvas& c) override;
  bool hitSelf() const override { return false; }
private:
  Orientation m_o;
  Color m_c;
};

} // namespace prism
