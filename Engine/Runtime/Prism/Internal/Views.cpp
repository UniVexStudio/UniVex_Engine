// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Views.h"
#include "prism/ColorPicker.h"
#include "prism/Application.h"
#include "prism/Menu.h"
#include <cstring>
#include <cctype>
#include <ctime>

namespace prism {
namespace {
std::string lower(std::string s) {
  for (auto& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}
const Color kPopupBg = Color::hex(0x252527);
} // namespace

// ------------------------------------------------------------------ VirtualList
void VirtualList::arrange(Rect r) {
  m_rect = r;
  if (m_toEnd) { m_scroll = contentH(); m_toEnd = false; }
  m_scroll = std::clamp(m_scroll, 0.f, std::max(0.f, contentH() - r.h));
}

int VirtualList::rowAt(Vec2 p) const {
  if (!m_rect.contains(p)) return -1;
  int i = int(std::floor((p.y - m_rect.y + m_scroll) / m_rowH));
  return i >= 0 && i < rowCount() ? i : -1;
}

void VirtualList::paint(Canvas& c) {
  c.pushClip(m_rect);
  int n = rowCount();
  int first = std::max(0, int(std::floor(m_scroll / m_rowH)));
  int last = std::min(n - 1, int(std::floor((m_scroll + m_rect.h) / m_rowH)));
  for (int i = first; i <= last; i++) paintRow(c, i, rowRect(i), i == m_hoverRow && m_hovered);
  paintOverlay(c);
  c.popClip();
  m_bar.paint(c, m_rect, contentH(), m_scroll, m_hovered && Application::get().mousePos().x >= m_rect.right() - VScrollbar::W);
}

bool VirtualList::onMouseDown(const MouseEvent& e) {
  if (e.button == MouseButton::Left && VScrollbar::needed(contentH(), m_rect.h) && e.pos.x >= m_rect.right() - VScrollbar::W) {
    m_bar.mouseDown(e.pos, m_rect, contentH(), m_scroll);
    relayout();
    return true;
  }
  int row = rowAt(e.pos);
  return row >= 0 ? onRowDown(row, e, rowRect(row)) : false;
}

bool VirtualList::onMouseUp(const MouseEvent&) {
  bool was = m_bar.dragging;
  m_bar.mouseUp();
  return was;
}

bool VirtualList::onMouseMove(const MouseEvent& e) {
  if (m_bar.dragging) {
    m_bar.mouseMove(e.pos, m_rect, contentH(), m_scroll);
    relayout();
    return true;
  }
  int r = rowAt(e.pos);
  if (r != m_hoverRow) { m_hoverRow = r; invalidate(); }
  return false;
}

bool VirtualList::onMouseWheel(const MouseEvent& e) {
  if (!VScrollbar::needed(contentH(), m_rect.h)) return false;
  m_scroll -= e.wheel.y * m_rowH * 3;
  relayout();
  return true;
}

void VirtualList::ensureVisible(int row) {
  float top = float(row) * m_rowH;
  if (top < m_scroll) m_scroll = top;
  else if (top + m_rowH > m_scroll + m_rect.h) m_scroll = top + m_rowH - m_rect.h;
  m_scroll = std::clamp(m_scroll, 0.f, std::max(0.f, contentH() - m_rect.h));
  invalidate();
}

// ------------------------------------------------------------------ TreeView
TreeView::TreeView() {
  setRowHeight(theme().rowH);
  m_edit = make<TextInput>();
  m_edit->setVisible(false);
  addChild(m_edit);
  m_edit->onCommit = [this](const std::string&) { endRename(true); };
  m_edit->onCancel = [this] { endRename(false); };
  m_edit->onBlur = [this] { if (m_renaming) endRename(true); };
}

TreeNode* TreeView::addNode(TreeNode* parent, std::string label, Icon icon, int id) {
  static int autoId = 1000000;
  TreeNode* p = parent ? parent : &m_root;
  auto n = std::make_unique<TreeNode>();
  n->label = std::move(label);
  n->icon = icon;
  n->id = id ? id : ++autoId;
  n->parent = parent;
  TreeNode* raw = n.get();
  p->kids.push_back(std::move(n));
  m_dirty = true;
  invalidate();
  return raw;
}

void TreeView::clear() {
  m_root.kids.clear();
  m_sel.clear();
  m_renaming = nullptr;
  m_dirty = true;
  invalidate();
}

TreeNode* TreeView::find(int id) {
  std::function<TreeNode*(TreeNode*)> rec = [&](TreeNode* n) -> TreeNode* {
    for (auto& k : n->kids) {
      if (k->id == id) return k.get();
      if (TreeNode* r = rec(k.get())) return r;
    }
    return nullptr;
  };
  return rec(&m_root);
}

bool TreeView::matches(const TreeNode* n) const {
  if (lower(n->label).find(m_filter) != std::string::npos) return true;
  for (auto& k : n->kids) if (matches(k.get())) return true;
  return false;
}

const std::vector<TreeView::Row>& TreeView::rows() const {
  if (m_dirty) {
    m_rows.clear();
    bool filt = !m_filter.empty();
    std::function<void(TreeNode*, int)> walk = [&](TreeNode* n, int d) {
      for (auto& k : n->kids) {
        if (filt && !matches(k.get())) continue;
        m_rows.push_back({k.get(), d});
        if (k->expanded || filt) walk(k.get(), d + 1);
      }
    };
    walk(const_cast<TreeNode*>(&m_root), 0);
    m_dirty = false;
  }
  return m_rows;
}

void TreeView::arrange(Rect r) {
  rows();
  VirtualList::arrange(r);
}

void TreeView::setFilter(std::string f) {
  m_filter = lower(std::move(f));
  m_dirty = true;
  relayout();
}

std::vector<TreeNode*> TreeView::selectedNodes() {
  std::vector<TreeNode*> v;
  for (auto& r : rows()) if (m_sel.count(r.n->id)) v.push_back(r.n);
  return v;
}

void TreeView::setSelection(const std::vector<int>& ids) {
  m_sel = std::set<int>(ids.begin(), ids.end());
  m_anchor = ids.empty() ? -1 : ids.back();
  for (int id : ids)
    if (TreeNode* n = find(id))
      for (TreeNode* p = n->parent; p; p = p->parent) if (!p->expanded) { p->expanded = true; m_dirty = true; }
  if (!ids.empty())
    for (int i = 0; i < rowCount(); i++) if (rows()[size_t(i)].n->id == ids.back()) { ensureVisible(i); break; }
  invalidate();
}

void TreeView::fire() {
  if (onSelect) onSelect(selectedNodes());
}

void TreeView::selectRow(int row, const MouseEvent* e) {
  TreeNode* n = rows()[size_t(row)].n;
  bool ctrl = e && (e->mods & ModCtrl), shift = e && (e->mods & ModShift);
  if (ctrl) {
    if (!m_sel.erase(n->id)) m_sel.insert(n->id);
    m_anchor = n->id;
  } else if (shift && m_anchor >= 0) {
    int a = -1;
    for (int i = 0; i < rowCount(); i++) if (rows()[size_t(i)].n->id == m_anchor) a = i;
    if (a < 0) a = row;
    m_sel.clear();
    for (int i = std::min(a, row); i <= std::max(a, row); i++) m_sel.insert(rows()[size_t(i)].n->id);
  } else {
    m_sel = {n->id};
    m_anchor = n->id;
  }
  ensureVisible(row);
  fire();
  invalidate();
}

bool TreeView::onRowDown(int row, const MouseEvent& e, Rect r) {
  Row rw = rows()[size_t(row)];
  TreeNode* n = rw.n;
  if (e.button == MouseButton::Right) {
    if (!isSelected(n)) selectRow(row, nullptr);
    if (onContext) onContext(n, e.pos);
    return true;
  }
  if (e.button != MouseButton::Left) return false;
  float x0 = r.x + 8 + float(rw.depth) * 14;
  bool kids = !n->kids.empty();
  if (kids && e.pos.x >= x0 - 2 && e.pos.x < x0 + 14) {
    n->expanded = !n->expanded;
    m_dirty = true;
    relayout();
    return true;
  }
  if (n->toggles) {
    Rect eye{r.right() - 24, r.y + 3, 16, 16}, lock{r.right() - 46, r.y + 3, 16, 16};
    if (eye.inflated(2).contains(e.pos)) { n->visible = !n->visible; if (onVisibilityToggle) onVisibilityToggle(n); invalidate(); return true; }
    if (lock.inflated(2).contains(e.pos)) { n->locked = !n->locked; if (onLockToggle) onLockToggle(n); invalidate(); return true; }
  }
  if (e.clicks >= 2) {
    if (onActivate) onActivate(n);
    else if (kids) { n->expanded = !n->expanded; m_dirty = true; relayout(); }
    return true;
  }
  selectRow(row, &e);
  return true;
}

bool TreeView::onKeyDown(const KeyEvent& e) {
  if (m_renaming || rowCount() == 0) return false;
  int cur = -1;
  for (int i = 0; i < rowCount(); i++) if (rows()[size_t(i)].n->id == m_anchor) cur = i;
  switch (e.key) {
  case Key::Down: selectRow(std::min(rowCount() - 1, cur + 1), nullptr); return true;
  case Key::Up: selectRow(std::max(0, cur - 1), nullptr); return true;
  case Key::Home: selectRow(0, nullptr); return true;
  case Key::End: selectRow(rowCount() - 1, nullptr); return true;
  case Key::Left: case Key::Right: {
    if (cur < 0) return true;
    TreeNode* n = rows()[size_t(cur)].n;
    if (e.key == Key::Left) {
      if (!n->kids.empty() && n->expanded) { n->expanded = false; m_dirty = true; relayout(); }
      else if (n->parent)
        for (int i = 0; i < rowCount(); i++) if (rows()[size_t(i)].n == n->parent) { selectRow(i, nullptr); break; }
    } else if (!n->kids.empty()) {
      if (!n->expanded) { n->expanded = true; m_dirty = true; relayout(); }
      else selectRow(std::min(rowCount() - 1, cur + 1), nullptr);
    }
    return true;
  }
  case Key::F2: if (cur >= 0) beginRename(rows()[size_t(cur)].n); return true;
  case Key::Delete: if (cur >= 0 && onDelete) onDelete(rows()[size_t(cur)].n); return true;
  case Key::Enter: if (cur >= 0 && onActivate) onActivate(rows()[size_t(cur)].n); return true;
  default: return false;
  }
}

void TreeView::paintRow(Canvas& c, int row, Rect r, bool hover) {
  const Theme& t = theme();
  const Row& rw = rows()[size_t(row)];
  TreeNode* n = rw.n;
  bool sel = isSelected(n);
  if (sel) {
    c.fillRect(r, m_focused ? t.rowSelected : t.rowSelectedInactive);
    if (m_focused) c.fillRect({r.x, r.y, 2, r.h}, t.accent);
  } else if (hover) {
    c.fillRect(r, t.rowHover);
  }
  float x = r.x + 8 + float(rw.depth) * 14;
  bool filt = !m_filter.empty();
  if (!n->kids.empty())
    drawIcon(c, n->expanded || filt ? Icon::TriDown : Icon::TriRight, {x - 1, r.y + (r.h - 14) * 0.5f, 14, 14}, hover ? t.text : t.textDim);
  x += 16;
  Color ic = n->hasTint ? n->tint : t.textDim;
  if (!n->visible) ic = ic.alpha(0.4f);
  drawIcon(c, n->icon, {x, r.y + (r.h - 16) * 0.5f, 16, 16}, ic);
  x += 22;
  float rightEdge = r.right() - 6;
  if (n->toggles) {
    Rect eye{r.right() - 24, r.y + (r.h - 16) * 0.5f, 16, 16}, lock{r.right() - 46, r.y + (r.h - 16) * 0.5f, 16, 16};
    drawIcon(c, n->visible ? Icon::Eye : Icon::EyeOff, eye, n->visible ? t.textDim.alpha(0.8f) : t.textDisabled, 1.2f);
    if (n->locked) drawIcon(c, Icon::Lock, lock, t.warning, 1.2f);
    else if (hover || sel) drawIcon(c, Icon::Unlock, lock, t.textDisabled, 1.2f);
    rightEdge = r.right() - 52;
  }
  if (!n->badge.empty()) {
    // Fixed-width, left-aligned "type" column so every row's type starts at the same x.
    const float colW = 76;
    float colX = rightEdge - colW;
    if (colX > x + 60) {
      c.textIn(*t.small, {colX, r.y, colW, r.h}, n->badge, t.textDim, HAlign::Left, true);
      rightEdge = colX - 8;
    }
  }
  if (n == m_renaming) return;
  Color fg = !n->visible ? t.textDisabled : (sel ? t.textBright : t.text);
  c.textIn(*t.ui, {x, r.y, std::max(0.f, rightEdge - x), r.h}, n->label, fg);
}

void TreeView::paintOverlay(Canvas& c) {
  if (!m_renaming) return;
  int idx = -1;
  for (int i = 0; i < rowCount(); i++) if (rows()[size_t(i)].n == m_renaming) idx = i;
  if (idx < 0) { endRename(false); return; }
  Rect rr = rowRect(idx);
  float x = rr.x + 8 + float(rows()[size_t(idx)].depth) * 14 + 16 + 22 - 5;
  Rect er{x, rr.y + 1, std::max(60.f, rr.w - (x - rr.x) - 56), rr.h - 2};
  m_edit->measure(er.size());
  m_edit->arrange(er);
  m_edit->paint(c);
}

void TreeView::beginRename(TreeNode* n) {
  m_renaming = n;
  m_edit->setText(n->label);
  m_edit->setVisible(true);
  Application::get().setFocus(m_edit.get());
  m_edit->selectAll();
  invalidate();
}

void TreeView::endRename(bool apply) {
  if (!m_renaming) return;
  TreeNode* n = m_renaming;
  m_renaming = nullptr;
  std::string s = m_edit->text();
  m_edit->setVisible(false);
  if (m_edit->focused()) Application::get().setFocus(this);
  if (apply && !s.empty() && s != n->label) {
    n->label = s;
    if (onRename) onRename(n, s);
  }
  invalidate();
}

// ------------------------------------------------------------------ LogView
LogView::LogView() { setRowHeight(18); }

int LogView::severity(LogLevel l) {
  switch (l) {
  case LogLevel::Command: return 0;
  case LogLevel::Info: case LogLevel::Success: return 1;
  case LogLevel::Warning: return 2;
  case LogLevel::Error: return 3;
  }
  return 1;
}

void LogView::rebuild() {
  m_vis.clear();
  static const int minSeverity[4] = {0, 1, 2, 3};
  int need = minSeverity[std::clamp(m_verbosity, 0, 3)];
  for (int i = 0; i < int(m_entries.size()); i++) {
    const Entry& e = m_entries[size_t(i)];
    if (!m_on[int(e.level)]) continue;
    if (severity(e.level) < need) continue;
    if (!e.category.empty()) { auto it = m_cats.find(e.category); if (it != m_cats.end() && !it->second) continue; }
    if (!m_filter.empty() && lower(e.text + " " + e.category).find(m_filter) == std::string::npos) continue;
    m_vis.push_back(i);
  }
  relayout();
}

void LogView::add(LogLevel lv, std::string text, std::string category) {
  bool stick = atBottom();
  if (category.empty() && text.size() > 2 && text[0] == '[') {          // "[Renderer] message"
    size_t close = text.find(']');
    if (close != std::string::npos && close < 24) {
      category = text.substr(1, close - 1);
      size_t st = close + 1;
      while (st < text.size() && text[st] == ' ') st++;
      text = text.substr(st);
    }
  }
  if (!category.empty()) m_cats.emplace(category, true);
  char buf[16];
  std::time_t tt = std::time(nullptr);
  std::tm tmv{};
  localtime_r(&tt, &tmv);
  std::strftime(buf, sizeof buf, "%H:%M:%S", &tmv);
  m_entries.push_back({lv, buf, std::move(text), std::move(category)});
  m_counts[int(lv)]++;
  if (m_entries.size() > 4000) {
    m_counts[int(m_entries.front().level)]--;
    m_entries.erase(m_entries.begin());
  }
  rebuild();
  if (stick) scrollToEnd();
  if (onCountsChanged) onCountsChanged();
}

void LogView::clear() {
  m_entries.clear();
  m_cats.clear();
  for (int& c : m_counts) c = 0;
  m_sel = -1;
  rebuild();
  if (onCountsChanged) onCountsChanged();
}

void LogView::setLevelEnabled(LogLevel lv, bool on) { m_on[int(lv)] = on; rebuild(); }
void LogView::setTextFilter(std::string s) { m_filter = lower(std::move(s)); rebuild(); }
std::vector<std::string> LogView::categories() const {
  std::vector<std::string> v;
  for (auto& kv : m_cats) v.push_back(kv.first);
  return v;
}
void LogView::setCategoryEnabled(const std::string& cat, bool on) { m_cats[cat] = on; rebuild(); }
bool LogView::categoryEnabled(const std::string& cat) const { auto it = m_cats.find(cat); return it == m_cats.end() || it->second; }

std::string LogView::selectedText() const {
  if (m_sel < 0 || m_sel >= int(m_vis.size())) return "";
  const Entry& e = m_entries[size_t(m_vis[size_t(m_sel)])];
  return e.text;
}
std::string LogView::visibleText() const {
  std::string out;
  for (int i : m_vis) {
    const Entry& e = m_entries[size_t(i)];
    out += e.time + "  " + (e.category.empty() ? "" : "[" + e.category + "] ") + e.text + "\n";
  }
  return out;
}

// "dir/File.ext:123" (optionally ":col"): looks for a path-like token followed by :digits.
bool LogView::findSource(const std::string& t, Source& out) {
  static const char* exts[] = {".cpp", ".h", ".hpp", ".c", ".cc", ".cs", ".py", ".glsl", ".hlsl", ".lua", ".prism", ".json"};
  for (size_t i = 0; i < t.size(); i++) {
    if (t[i] != ':' || i + 1 >= t.size() || !std::isdigit(static_cast<unsigned char>(t[i + 1]))) continue;
    size_t b = i;
    while (b > 0 && (std::isalnum(static_cast<unsigned char>(t[b - 1])) || std::strchr("._/\\-", t[b - 1]))) b--;
    std::string path = t.substr(b, i - b);
    bool ok = false;
    for (const char* e : exts) { size_t n = std::strlen(e); if (path.size() > n && lower(path.substr(path.size() - n)) == e) { ok = true; break; } }
    if (!ok) continue;
    size_t e2 = i + 1;
    int line = 0;
    while (e2 < t.size() && std::isdigit(static_cast<unsigned char>(t[e2]))) line = line * 10 + (t[e2++] - '0');
    out = {b, e2, path, line};
    return true;
  }
  return false;
}

bool LogView::sourceAt(int row, Vec2 p, Rect r, Source* out) const {
  const Entry& e = m_entries[size_t(m_vis[size_t(row)])];
  Source s;
  if (!findSource(e.text, s)) return false;
  const Font& f = *theme().mono;
  float x0 = r.x + 28 + std::ceil(f.width(e.time)) + 12;
  if (!e.category.empty()) x0 += std::ceil(f.width("[" + e.category + "]")) + 8;
  float a = x0 + f.width(e.text.substr(0, s.begin)), b = x0 + f.width(e.text.substr(0, s.end));
  if (p.x < a || p.x > b) return false;
  if (out) *out = s;
  return true;
}

Cursor LogView::cursor() const { return m_overSource ? Cursor::Hand : Cursor::Arrow; }

bool LogView::onMouseMove(const MouseEvent& e) {
  bool over = false;
  int row = rowAt(e.pos);
  if (row >= 0 && row < int(m_vis.size())) over = sourceAt(row, e.pos, rowRect(row), nullptr);
  if (over != m_overSource) { m_overSource = over; invalidate(); }
  return VirtualList::onMouseMove(e);
}

void LogView::paintRow(Canvas& c, int row, Rect r, bool hover) {
  const Theme& t = theme();
  const Entry& e = m_entries[size_t(m_vis[size_t(row)])];
  if (row == m_sel) c.fillRect(r, t.rowSelected);
  else if (hover) c.fillRect(r, t.rowHover);
  else if (row % 2) c.fillRect(r, Color(255, 255, 255, 5));
  Color col = t.text;
  Icon ic = Icon::None;
  switch (e.level) {
  case LogLevel::Info: col = t.text; ic = Icon::Info; break;
  case LogLevel::Warning: col = t.warning; ic = Icon::Warning; break;
  case LogLevel::Error: col = t.error; ic = Icon::Error; break;
  case LogLevel::Command: col = t.accentHover; ic = Icon::ChevronRight; break;
  case LogLevel::Success: col = t.success; ic = Icon::Check; break;
  }
  drawIcon(c, ic, {r.x + 8, r.y + (r.h - 12) * 0.5f, 12, 12}, e.level == LogLevel::Info ? t.textDim : col, 1.2f);
  const Font& f = *t.mono;
  float tw = std::ceil(f.width(e.time));
  c.textIn(f, {r.x + 28, r.y, tw + 2, r.h}, e.time, t.textDisabled, HAlign::Left, false);
  float x = r.x + 28 + tw + 12;
  if (!e.category.empty()) {                                      // category tag
    std::string tag = "[" + e.category + "]";
    float cw = std::ceil(f.width(tag));
    c.textIn(f, {x, r.y, cw + 2, r.h}, tag, t.accentHover.mix(t.textDim, 0.35f), HAlign::Left, false);
    x += cw + 8;
  }
  Rect tr{x, r.y, r.w - (x - r.x) - 8, r.h};
  // search highlight
  if (!m_filter.empty()) {
    size_t p = lower(e.text).find(m_filter);
    if (p != std::string::npos) {
      float a = x + f.width(e.text.substr(0, p)), b = x + f.width(e.text.substr(0, p + m_filter.size()));
      c.fillRect({a - 1, r.y + 2, b - a + 2, r.h - 4}, t.accent.alpha(0.35f));
    }
  }
  c.textIn(f, tr, e.text, col, HAlign::Left, false);
  Source s;
  if (findSource(e.text, s)) {                                    // clickable source: underline
    float a = x + f.width(e.text.substr(0, s.begin)), b = x + f.width(e.text.substr(0, s.end));
    c.fillRect({a, r.bottom() - 4, b - a, 1}, t.accentHover);
  }
}

bool LogView::onRowDown(int row, const MouseEvent& e, Rect r) {
  if (e.button != MouseButton::Left && e.button != MouseButton::Right) return false;
  m_sel = row;
  invalidate();
  if (e.button == MouseButton::Left) {
    Source s;
    if (sourceAt(row, e.pos, r, &s) && onSourceClick) { onSourceClick(s.path, s.line); return true; }
    return true;
  }
  std::string txt = m_entries[size_t(m_vis[size_t(row)])].text;
  std::vector<MenuItem> cats;
  for (auto& cat : categories()) cats.push_back(MenuItem::Check(cat, categoryEnabled(cat), [this, cat] { setCategoryEnabled(cat, !categoryEnabled(cat)); }));
  if (cats.empty()) cats.push_back(MenuItem::Item("(no categories yet)", [] {}).disabled());
  std::vector<MenuItem> verb;
  const char* vn[4] = {"All messages", "Hide commands", "Warnings and errors", "Errors only"};
  for (int v = 0; v < 4; v++) verb.push_back(MenuItem::Check(vn[v], m_verbosity == v, [this, v] { setVerbosity(v); }));
  showContextMenu({MenuItem::Item("Copy Line", [txt] { Application::get().setClipboard(txt); }, "Ctrl+C", Icon::Copy),
                   MenuItem::Item("Copy All Visible", [this] { Application::get().setClipboard(visibleText()); }, "Ctrl+Shift+C", Icon::Copy),
                   MenuItem::Sep(),
                   MenuItem::Sub("Categories", std::move(cats), Icon::Filter),
                   MenuItem::Sub("Verbosity", std::move(verb), Icon::Layers),
                   MenuItem::Sep(),
                   MenuItem::Item("Clear Log", [this] { clear(); }, "", Icon::Trash)},
                  e.pos);
  return true;
}

bool LogView::onKeyDown(const KeyEvent& e) {
  if ((e.mods & ModCtrl) && e.key == Key::C) {
    if (e.mods & ModShift) { Application::get().setClipboard(visibleText()); return true; }
    if (m_sel >= 0 && m_sel < rowCount()) { Application::get().setClipboard(selectedText()); return true; }
  }
  return VirtualList::onKeyDown(e);
}

// ------------------------------------------------------------------ PropertyGrid
int PropertyGrid::addCategory(std::string name, bool expanded) {
  m_cats.push_back({std::move(name), expanded, {}, {}});
  relayout();
  return int(m_cats.size()) - 1;
}

void PropertyGrid::addRow(int cat, std::string label, Ref<Widget> editor, std::string tip) {
  Row r;
  r.label = std::move(label);
  r.tip = std::move(tip);
  r.editor = editor;
  m_cats[size_t(cat)].rows.push_back(std::move(r));
  addChild(std::move(editor));
  applyVisibility();
}

void PropertyGrid::clear() {
  m_cats.clear();
  clearChildren();
}

void PropertyGrid::applyVisibility() {
  for (auto& c : m_cats) for (auto& r : c.rows) r.editor->setVisible(c.expanded);
}

Vec2 PropertyGrid::measure(Vec2 avail) {
  float w = avail.x, lw = labelW(w), h = 0;
  for (auto& c : m_cats) {
    h += 26;
    if (!c.expanded) continue;
    for (auto& r : c.rows) {
      Vec2 d = r.editor->measure({w - lw - 16, 1e4f});
      r.h = std::max(26.f, d.y + 4);
      h += r.h;
    }
  }
  return m_desired = constrain({w, h});
}

void PropertyGrid::arrange(Rect r) {
  m_rect = r;
  float lw = labelW(r.w), y = r.y;
  for (auto& c : m_cats) {
    c.header = {r.x, y, r.w, 26};
    y += 26;
    if (!c.expanded) continue;
    for (auto& row : c.rows) {
      row.rect = {r.x, y, r.w, row.h};
      float ex = r.x + lw + 6, ew = r.right() - ex - 10;
      Vec2 d = row.editor->desired();
      float eh = std::min(row.h - 4, std::max(22.f, d.y));
      row.editor->arrange({ex, y + std::floor((row.h - eh) * 0.5f), std::max(10.f, ew), eh});
      y += row.h;
    }
  }
}

void PropertyGrid::paint(Canvas& c) {
  const Theme& t = theme();
  float lw = labelW(m_rect.w);
  for (auto& cat : m_cats) {
    c.fillRect(cat.header, t.header);
    c.fillRect({cat.header.x, cat.header.bottom() - 1, cat.header.w, 1}, t.border);
    drawIcon(c, cat.expanded ? Icon::TriDown : Icon::TriRight, {cat.header.x + 6, cat.header.y + 6, 14, 14}, t.textDim);
    c.textIn(*t.semibold, {cat.header.x + 24, cat.header.y, cat.header.w - 30, cat.header.h}, cat.name, t.text);
    if (!cat.expanded) continue;
    for (auto& r : cat.rows) {
      c.fillRect({r.rect.x, r.rect.bottom() - 1, r.rect.w, 1}, Color(255, 255, 255, 7));
      c.textIn(*t.ui, {r.rect.x + 24, r.rect.y, lw - 30, r.rect.h}, r.label, t.textDim);
      c.fillRect({r.rect.x + lw, r.rect.y, 1, r.rect.h}, Color(255, 255, 255, m_dragDiv ? 40 : 10));
    }
  }
  paintChildren(c);
}

Cursor PropertyGrid::cursor() const {
  float lw = labelW(m_rect.w);
  Vec2 m = Application::get().mousePos();
  if (m_dragDiv || (std::fabs(m.x - (m_rect.x + lw)) < 4 && m_rect.contains(m))) return Cursor::SizeWE;
  return Cursor::Arrow;
}

const std::string& PropertyGrid::tooltipAt(Vec2 p) const {
  static const std::string none;
  float lw = labelW(m_rect.w);
  for (auto& c : m_cats)
    if (c.expanded)
      for (auto& r : c.rows)
        if (r.rect.contains(p) && p.x < m_rect.x + lw) return r.tip;
  return none;
}

bool PropertyGrid::onMouseDown(const MouseEvent& e) {
  if (e.button != MouseButton::Left) return false;
  float lw = labelW(m_rect.w);
  for (auto& c : m_cats) {
    if (c.header.contains(e.pos)) {
      c.expanded = !c.expanded;
      applyVisibility();
      relayout();
      return true;
    }
  }
  if (std::fabs(e.pos.x - (m_rect.x + lw)) < 4) { m_dragDiv = true; return true; }
  return false;
}

bool PropertyGrid::onMouseMove(const MouseEvent& e) {
  if (!m_dragDiv) return false;
  m_ratio = std::clamp((e.pos.x - m_rect.x) / std::max(1.f, m_rect.w), 0.2f, 0.7f);
  relayout();
  return true;
}

// ------------------------------------------------------------------ Colour picker
namespace {

std::string hexOf(Color c) {
  char b[16];
  snprintf(b, sizeof b, "#%02X%02X%02X", c.r, c.g, c.b);
  return b;
}

} // namespace

void ColorField::paint(Canvas& c) {
  const Theme& t = theme();
  bool en = enabled();
  c.fillRoundRect(m_rect, 3, t.control);
  c.strokeRoundRect(m_rect, 3, m_open ? t.accent : (m_hovered && en ? t.borderStrong : t.borderSoft));
  Rect sw{m_rect.x + 4, m_rect.y + 4, std::max(18.f, m_rect.h - 8) + 14, m_rect.h - 8};
  c.fillRoundRect(sw, 2, m_color);
  c.strokeRoundRect(sw, 2, Color(0, 0, 0, 110));
  c.textIn(*t.mono, {sw.right() + 8, m_rect.y, m_rect.right() - sw.right() - 12, m_rect.h}, hexOf(m_color), en ? t.text : t.textDisabled);
}

bool ColorField::onMouseDown(const MouseEvent& e) {
  if (e.button != MouseButton::Left) return false;
  m_open = true;
  invalidate();
  showColorPickerWindow(
    m_color,
    [this](Color col) {
      m_open = false;
      setColor(col, true);
      if (onCommit) onCommit();
      invalidate();
    },
    [this] {
      m_open = false;
      invalidate();
    },
    [this](Color liveCol) {
      setColor(liveCol, true);
    });
  return true;
}

// ------------------------------------------------------------------ Dialog
void showDialog(const std::string& title, Ref<Widget> content, std::vector<DialogButton> buttons, float width) {
  const Theme& t = theme();
  auto root = make<Border>();
  root->setBackground(Color::hex(0x232325));
  root->setBorder(t.borderStrong);
  root->setRadius(6);
  root->setFixedWidth(width);
  std::weak_ptr<Widget> wr = root;
  auto close = [wr] { if (auto r = wr.lock()) Application::get().closePopup(r.get()); };

  auto col = vbox();
  auto bar = make<Border>();
  bar->setBorder(t.borderSoft, Margin(0, 0, 0, 1));
  bar->setPadding(Margin(16, 0, 8, 0));
  bar->setFixedHeight(38);
  auto row = hbox();
  auto lbl = make<Label>(title, FontRole::Semibold);
  row->add(lbl, {.fill = 1, .v = VAlign::Center});
  auto x = make<Button>("", Icon::Close, ButtonStyle::Toolbar);
  x->onClick = close;
  row->add(x, {.v = VAlign::Center});
  bar->setChild(row);
  col->add(bar);

  auto body = make<Border>();
  body->setPadding(Margin(18, 16));
  body->setChild(std::move(content));
  col->add(body);

  auto foot = make<Border>();
  foot->setBorder(t.borderSoft, Margin(0, 1, 0, 0));
  foot->setPadding(Margin(14, 10));
  auto frow = hbox(8);
  frow->add(make<Spacer>(), {.fill = 1});
  for (auto& b : buttons) {
    auto btn = make<Button>(b.label, Icon::None, b.primary ? ButtonStyle::Primary : ButtonStyle::Default);
    auto act = b.action;
    btn->onClick = [close, act] { close(); if (act) act(); };
    btn->setMinSize(76, 0);
    frow->add(btn);
  }
  foot->setChild(frow);
  col->add(foot);
  root->setChild(col);

  PopupOptions o;
  o.placement = PopupPlacement::Center;
  o.modal = true;
  o.dismissOnOutside = false;
  Application::get().showPopup(root, o);
}

} // namespace prism
