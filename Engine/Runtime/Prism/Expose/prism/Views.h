// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - data views: VirtualList base, TreeView, LogView, PropertyGrid, ColorField/ColorPicker, Dialog.
#pragma once
#include "Containers.h"
#include "Controls.h"
#include "TextInput.h"
#include <memory>
#include <map>
#include <set>

namespace prism {

// ------------------------------------------------------------------ VirtualList
// Uniform-height scrolling list that only paints visible rows.
class VirtualList : public Widget {
public:
  void setRowHeight(float h) { m_rowH = h; relayout(); }
  float rowHeight() const { return m_rowH; }
  Vec2 measure(Vec2) override { return m_desired = constrain({0, 0}); }
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onMouseWheel(const MouseEvent& e) override;
  void onMouseLeave() override { m_hoverRow = -1; invalidate(); }
  bool focusable() const override { return true; }
  AccessRole accessRole() const override { return AccessRole::List; }
  void scrollToEnd() { m_toEnd = true; relayout(); }
  bool atBottom() const { return m_scroll >= contentH() - m_rect.h - 1; }
  void ensureVisible(int row);
protected:
  virtual int rowCount() const = 0;
  virtual void paintRow(Canvas& c, int row, Rect r, bool hover) = 0;
  virtual bool onRowDown(int /*row*/, const MouseEvent& /*e*/, Rect /*r*/) { return false; }
  virtual void paintOverlay(Canvas&) {}
  float contentH() const { return float(rowCount()) * m_rowH; }
  Rect rowRect(int i) const { return {m_rect.x, m_rect.y + float(i) * m_rowH - m_scroll, rowWidth(), m_rowH}; }
  float rowWidth() const { return m_rect.w - (VScrollbar::needed(contentH(), m_rect.h) ? VScrollbar::W : 0.f); }
  int rowAt(Vec2 p) const;
  float m_rowH = 22, m_scroll = 0;
  int m_hoverRow = -1;
  bool m_toEnd = false;
  VScrollbar m_bar;
};

// ------------------------------------------------------------------ TreeView
struct TreeNode {
  int id = 0;
  std::string label, badge;
  Icon icon = Icon::None;
  Color tint;
  bool hasTint = false;
  bool expanded = true, visible = true, locked = false, toggles = true;
  // Host-owned icon image (square, 0xAARRGGBB, straight alpha) drawn instead of `icon` when set.
  const uint32_t* image = nullptr;
  int imageSize = 0;
  TreeNode* parent = nullptr;
  std::vector<std::unique_ptr<TreeNode>> kids;
};

class TreeView : public VirtualList {
public:
  TreeView();
  TreeNode* addNode(TreeNode* parent, std::string label, Icon icon, int id = 0);
  void clear();
  TreeNode* find(int id);
  void setSelection(const std::vector<int>& ids);
  std::vector<TreeNode*> selectedNodes();
  bool isSelected(const TreeNode* n) const { return m_sel.count(n->id) > 0; }
  void setFilter(std::string f);
  void beginRename(TreeNode* n);
  void refresh() { m_dirty = true; invalidate(); }
  // Guide lines joining each object to its parent (a vertical line per open level, an elbow per row).
  void setShowGuides(bool on) { m_guides = on; invalidate(); }
  // The node on the row under canvas point p (drops, context menus), or null.
  TreeNode* nodeAt(Vec2 p) const {
    const int r = rowAt(p);
    const auto& rs = rows();
    return r >= 0 && r < int(rs.size()) ? rs[size_t(r)].n : nullptr;
  }

  Callback<const std::vector<TreeNode*>&> onSelect;
  Callback<TreeNode*> onVisibilityToggle, onLockToggle, onActivate, onDelete;
  Callback<TreeNode*, const std::string&> onRename;
  Callback<TreeNode*, Vec2> onContext;

  void arrange(Rect r) override;
  bool onKeyDown(const KeyEvent& e) override;
  AccessRole accessRole() const override { return AccessRole::Tree; }
protected:
  struct Row { TreeNode* n; int depth; };
  int rowCount() const override { return int(rows().size()); }
  void paintRow(Canvas& c, int row, Rect r, bool hover) override;
  bool onRowDown(int row, const MouseEvent& e, Rect r) override;
  void paintOverlay(Canvas& c) override;
private:
  const std::vector<Row>& rows() const;
  bool matches(const TreeNode* n) const;
  void fire();
  void selectRow(int row, const MouseEvent* e);
  void endRename(bool apply);
  TreeNode m_root;
  std::set<int> m_sel;
  int m_anchor = -1;
  std::string m_filter;
  mutable bool m_dirty = true;
  mutable std::vector<Row> m_rows;
  Ref<TextInput> m_edit;
  TreeNode* m_renaming = nullptr;
  bool m_guides = false;
};

// ------------------------------------------------------------------ LogView
enum class LogLevel { Info, Warning, Error, Command, Success };

class LogView : public VirtualList {
public:
  LogView();
  // `category` can also be given as a "[Category] message" prefix; `source` ("file.cpp:42") is found in the text if empty.
  void add(LogLevel lv, std::string text, std::string category = "");
  void clear();
  void setLevelEnabled(LogLevel lv, bool on);
  void setTextFilter(std::string s);
  // Categories: every distinct category seen so far can be shown / hidden.
  std::vector<std::string> categories() const;
  void setCategoryEnabled(const std::string& cat, bool on);
  bool categoryEnabled(const std::string& cat) const;
  // Verbosity: 0 = everything, 1 = hide commands, 2 = warnings + errors, 3 = errors only.
  void setVerbosity(int v) { m_verbosity = v; rebuild(); }
  int verbosity() const { return m_verbosity; }
  // Copy: selected line, or every visible line as text.
  std::string selectedText() const;
  std::string visibleText() const;
  int visibleCount() const { return int(m_vis.size()); }
  int count(LogLevel lv) const { return m_counts[int(lv)]; }
  int total() const { return int(m_entries.size()); }
  // Clickable sources: "path/File.cpp:123" tokens in a line are underlined and raise onSourceClick.
  Callback<> onCountsChanged;
  Callback<const std::string&, int> onSourceClick;
  struct Source { size_t begin = 0, end = 0; std::string path; int line = 0; };
  static bool findSource(const std::string& text, Source& out);
  Cursor cursor() const override;
protected:
  int rowCount() const override { return int(m_vis.size()); }
  void paintRow(Canvas& c, int row, Rect r, bool hover) override;
  bool onRowDown(int row, const MouseEvent& e, Rect r) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onKeyDown(const KeyEvent& e) override;
private:
  struct Entry { LogLevel level; std::string time, text, category; };
  static int severity(LogLevel l);
  bool sourceAt(int row, Vec2 p, Rect r, Source* out) const;
  void rebuild();
  std::vector<Entry> m_entries;
  std::vector<int> m_vis;
  std::map<std::string, bool> m_cats;
  bool m_on[5] = {true, true, true, true, true};
  int m_counts[5] = {0, 0, 0, 0, 0};
  int m_verbosity = 0;
  std::string m_filter;
  int m_sel = -1;
  bool m_overSource = false;
};

// ------------------------------------------------------------------ PropertyGrid
class PropertyGrid : public Widget {
public:
  int addCategory(std::string name, bool expanded = true);
  void addRow(int category, std::string label, Ref<Widget> editor, std::string tip = "");
  void clear();
  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& /*e*/) override { m_dragDiv = false; return true; }
  Cursor cursor() const override;
  const std::string& tooltipAt(Vec2 p) const override;
private:
  struct Row { std::string label, tip; Ref<Widget> editor; Rect rect; float h = 26; };
  struct Cat { std::string name; bool expanded; std::vector<Row> rows; Rect header; };
  float labelW(float w) const { return std::clamp(std::floor(w * m_ratio), 90.f, std::max(90.f, w - 110)); }
  void applyVisibility();
  std::vector<Cat> m_cats;
  float m_ratio = 0.40f;
  bool m_dragDiv = false;
};

// ------------------------------------------------------------------ Colour
class ColorField : public Widget {
public:
  explicit ColorField(Color c = Color(255, 255, 255)) : m_color(c) {}
  Color color() const { return m_color; }
  void setColor(Color c, bool notify = false) { m_color = c; invalidate(); if (notify && onChange) onChange(c); }
  Callback<Color> onChange;
  Callback<> onCommit;
  Vec2 measure(Vec2) override { return m_desired = constrain({90, theme().controlH}); }
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  void onMouseLeave() override { m_suppress = false; }
private:
  Color m_color;
  bool m_open = false, m_suppress = false;
};

// ------------------------------------------------------------------ Dialog
struct DialogButton {
  std::string label;
  bool primary = false;
  std::function<void()> action;
};
void showDialog(const std::string& title, Ref<Widget> content, std::vector<DialogButton> buttons, float width = 440);

} // namespace prism
