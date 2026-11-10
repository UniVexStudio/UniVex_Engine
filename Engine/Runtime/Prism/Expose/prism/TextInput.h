// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - text editing: TextEditState (pure logic), TextInput, NumberField (drag-to-edit), Vec3Field.
#pragma once
#include "Controls.h"

namespace prism {

class TextEditState {
public:
  std::string text;
  size_t caret = 0, anchor = 0;

  bool hasSelection() const { return caret != anchor; }
  size_t selStart() const { return std::min(caret, anchor); }
  size_t selEnd() const { return std::max(caret, anchor); }
  std::string selected() const { return text.substr(selStart(), selEnd() - selStart()); }
  void setText(std::string t) { text = std::move(t); caret = anchor = text.size(); }
  void selectAll() { anchor = 0; caret = text.size(); }
  void deleteSelection();
  void insert(std::string_view s);
  void backspace(bool word);
  void del(bool word);
  void moveTo(size_t i, bool select) { caret = std::min(i, text.size()); if (!select) anchor = caret; }
  void moveLeft(bool select, bool word);
  void moveRight(bool select, bool word);
  void selectWordAt(size_t i);
  size_t wordLeft(size_t i) const;
  size_t wordRight(size_t i) const;
};

class TextInput : public Widget {
public:
  explicit TextInput(std::string text = "", std::string placeholder = "");
  void setText(std::string t, bool notify = false);
  const std::string& text() const { return m_ed.text; }
  AccessRole accessRole() const override { return AccessRole::TextBox; }
  std::string accessValue() const override { return m_ed.text; }
  bool accessValueEditable() const override { return true; }
  bool accessSetValue(const std::string& v) override { setText(v); if (onChanged) onChanged(text()); return true; }
  void setPlaceholder(std::string p) { m_placeholder = std::move(p); invalidate(); }
  void setLeadingIcon(Icon i) { m_icon = i; invalidate(); }
  void setClearable(bool c) { m_clearable = c; }
  void setFrameless(bool f) { m_frameless = f; invalidate(); }
  void setCentered(bool c) { m_center = c; invalidate(); }
  void setReadOnly(bool r) { m_readOnly = r; }
  void setFont(FontRole f) { m_font = f; relayout(); }
  void selectAll() { m_ed.selectAll(); invalidate(); }

  Callback<const std::string&> onChanged;
  Callback<const std::string&> onCommit; // Enter
  Callback<> onCancel;                   // Escape
  Callback<> onBlur;

  Vec2 measure(Vec2) override { return m_desired = constrain({120, theme().controlH}); }
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override { m_dragSel = false; return true; }
  bool onKeyDown(const KeyEvent& e) override;
  bool onTextInput(const std::string& s) override;
  void onFocusChanged(bool f) override;
  Cursor cursor() const override { return Cursor::IBeam; }
  bool focusable() const override { return !m_readOnly || true; }
  bool capturesTyping() const override { return true; }

private:
  Rect textRect() const;
  float originX(const Rect& tr) const;
  size_t indexAtX(float x) const;
  void changed();
  void ensureCaretVisible();
  void resetBlink();

  TextEditState m_ed;
  std::string m_placeholder;
  Icon m_icon = Icon::None;
  FontRole m_font = FontRole::Regular;
  bool m_clearable = false, m_frameless = false, m_center = false, m_readOnly = false;
  bool m_dragSel = false, m_caretOn = true;
  float m_scroll = 0;
  int m_timer = 0;
};

class NumberField : public Widget {
public:
  explicit NumberField(float value = 0, float step = 1, int decimals = 2);
  void setRange(float lo, float hi) { m_min = lo; m_max = hi; }
  void setValue(float v, bool notify = false);
  float value() const { return m_value; }
  void setDecimals(int d) { m_dec = d; invalidate(); }
  void setSuffix(std::string s) { m_suffix = std::move(s); invalidate(); }
  void setDragSpeed(float s) { m_speed = s; }
  void setAccentStrip(Color c) { m_strip = c; m_hasStrip = true; invalidate(); }
  void setAxisLabel(std::string label, Color c) { m_axisLabel = std::move(label); m_strip = c; m_hasStrip = true; invalidate(); }

  Callback<float> onChange;  // live, during drag or after typing
  Callback<float> onCommit;  // gesture finished
  Callback<> onBegin;        // gesture started (for undo snapshots)

  Vec2 measure(Vec2) override;
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  Cursor cursor() const override { return m_editing ? Cursor::IBeam : Cursor::SizeWE; }
private:
  void beginEdit();
  void endEdit(bool apply);
  float clampv(float v) const { return std::clamp(v, m_min, m_max); }
  float m_value, m_step, m_min = -1e9f, m_max = 1e9f, m_speed;
  int m_dec;
  std::string m_suffix;
  std::string m_axisLabel;
  Color m_strip;
  bool m_hasStrip = false, m_editing = false, m_dragging = false, m_down = false;
  float m_pressX = 0, m_startValue = 0;
  Ref<TextInput> m_edit;
};

class Vec3Field : public Widget {
public:
  explicit Vec3Field(float step = 0.1f, int decimals = 2);
  void setValue(float x, float y, float z, bool notify = false);
  float get(int i) const { return m_f[size_t(i)]->value(); }
  void setSuffix(const std::string& s) { for (auto& f : m_f) f->setSuffix(s); }
  Callback<int, float> onChange;
  Callback<> onCommit;
  Callback<> onBegin;
  Vec2 measure(Vec2) override { return m_desired = constrain({150, theme().controlH}); }
  void arrange(Rect r) override;
  bool hitSelf() const override { return false; }
private:
  Ref<NumberField> m_f[3];
};

} // namespace prism
