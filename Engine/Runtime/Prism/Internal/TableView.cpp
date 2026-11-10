// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/TableView.h"
#include "prism/Application.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace prism {
namespace {
constexpr float kGrip = 5;
// Natural, case-insensitive comparison ("file2" < "file10"); numbers compare by value.
int naturalCmp(const std::string& a, const std::string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (std::isdigit((unsigned char)a[i]) && std::isdigit((unsigned char)b[j])) {
      size_t si = i, sj = j;
      while (si < a.size() && a[si] == '0') si++;
      while (sj < b.size() && b[sj] == '0') sj++;
      size_t ei = si, ej = sj;
      while (ei < a.size() && std::isdigit((unsigned char)a[ei])) ei++;
      while (ej < b.size() && std::isdigit((unsigned char)b[ej])) ej++;
      if (ei - si != ej - sj) return (ei - si) < (ej - sj) ? -1 : 1;
      int c = a.compare(si, ei - si, b, sj, ej - sj);
      if (c) return c < 0 ? -1 : 1;
      i = ei; j = ej;
    } else {
      int x = std::tolower((unsigned char)a[i]), y = std::tolower((unsigned char)b[j]);
      if (x != y) return x < y ? -1 : 1;
      i++; j++;
    }
  }
  if (i < a.size()) return 1;
  if (j < b.size()) return -1;
  return 0;
}
} // namespace

int TableView::addColumn(TableColumn c) {
  m_cols.push_back(std::move(c));
  m_order.push_back(int(m_cols.size()) - 1);
  m_headerH = theme().rowH + 2; m_rowH = theme().rowH;
  invalidate();
  return int(m_cols.size()) - 1;
}

void TableView::moveColumn(int from, int to) {
  auto it = std::find(m_order.begin(), m_order.end(), from);
  if (it == m_order.end()) return;
  m_order.erase(it);
  to = std::clamp(to, 0, int(m_order.size()));
  m_order.insert(m_order.begin() + to, from);
  invalidate();
  if (onColumnsChanged) onColumnsChanged();
}

void TableView::setColumnWidth(int col, float w) {
  auto& c = m_cols[size_t(col)];
  w = std::max(w, c.minWidth);
  if (w == c.width) return;
  c.width = w;
  clampScroll(); invalidate();
}

void TableView::autoFitColumn(int col) {
  auto& c = m_cols[size_t(col)];
  const Font& f = *theme().ui;
  float w = f.width(c.title) + 28;
  rebuild();
  int first = viewRowAt(m_scrollY), last = viewRowAt(m_scrollY + body().h);
  for (int v = std::max(0, first); v <= std::min(m_rows - 1, last); v++)
    if (cellText) w = std::max(w, f.width(cellText(m_view[size_t(v)], col)) + 16);
  setColumnWidth(col, w);
  if (onColumnsChanged) onColumnsChanged();
}

void TableView::rebuild() const {
  if (!m_dirtyRows && int(m_tops.size()) == m_rows + 1) return;
  if (int(m_view.size()) != m_rows) { m_view.resize(size_t(m_rows)); for (int i = 0; i < m_rows; i++) m_view[size_t(i)] = i; }
  m_tops.assign(size_t(m_rows) + 1, 0.f);
  for (int i = 0; i < m_rows; i++)
    m_tops[size_t(i) + 1] = m_tops[size_t(i)] + (rowHeightOf ? std::max(1.f, rowHeightOf(m_view[size_t(i)])) : m_rowH);
  m_dirtyRows = false;
}

void TableView::resort() {
  m_view.resize(size_t(m_rows));
  for (int i = 0; i < m_rows; i++) m_view[size_t(i)] = i;
  if (m_sortState != SortState::None && m_sortCol >= 0 && m_sortCol < columnCount()) {
    int col = m_sortCol; bool asc = m_sortState == SortState::Ascending;
    auto less = [&](int a, int b) {
      if (lessThan) return lessThan(a, b, col);
      return naturalCmp(cellText ? cellText(a, col) : "", cellText ? cellText(b, col) : "") < 0;
    };
    std::stable_sort(m_view.begin(), m_view.end(), [&](int a, int b) { return asc ? less(a, b) : less(b, a); });
  }
  m_dirtyRows = true;
}

void TableView::sortBy(int col, SortState s) {
  m_sortCol = s == SortState::None ? -1 : col;
  m_sortState = s;
  resort(); invalidate();
  if (onSort) onSort(col, s);
}

int TableView::dataRow(int v) const { rebuild(); return v >= 0 && v < m_rows ? m_view[size_t(v)] : -1; }
int TableView::viewRow(int d) const {
  rebuild();
  for (int i = 0; i < m_rows; i++) if (m_view[size_t(i)] == d) return i;
  return -1;
}

float TableView::contentHeight() const { rebuild(); return m_tops.back(); }
float TableView::rowTop(int v) const { rebuild(); return m_tops[size_t(std::clamp(v, 0, m_rows))]; }
int TableView::viewRowAt(float y) const {
  rebuild();
  if (m_rows == 0 || y < 0) return m_rows ? 0 : -1;
  if (y >= m_tops.back()) return m_rows - 1;
  return int(std::upper_bound(m_tops.begin(), m_tops.end(), y) - m_tops.begin()) - 1;
}

float TableView::totalWidth() const { float w = 0; for (int c : m_order) w += m_cols[size_t(c)].width; return w; }
float TableView::colX(int d) const { float x = 0; for (int i = 0; i < d; i++) x += m_cols[size_t(m_order[size_t(i)])].width; return x; }

Rect TableView::headerCell(int col) const {
  auto it = std::find(m_order.begin(), m_order.end(), col);
  if (it == m_order.end()) return {};
  int d = int(it - m_order.begin());
  return {m_rect.x + colX(d) - m_scrollX, m_rect.y, m_cols[size_t(col)].width, m_headerH};
}

void TableView::clampScroll() {
  float viewW = m_rect.w - (vBarNeeded() ? VScrollbar::W : 0.f);
  m_scrollX = std::clamp(m_scrollX, 0.f, std::max(0.f, totalWidth() - viewW));
  m_scrollY = std::clamp(m_scrollY, 0.f, std::max(0.f, contentHeight() - body().h));
}

void TableView::arrange(Rect r) { m_rect = r; m_dirtyRows = true; clampScroll(); }

void TableView::ensureVisible(int v) {
  if (v < 0 || v >= m_rows) return;
  float t = rowTop(v), b = rowTop(v + 1);
  if (t < m_scrollY) m_scrollY = t; else if (b > m_scrollY + body().h) m_scrollY = b - body().h;
  clampScroll(); invalidate();
}

int TableView::hdrAt(Vec2 p, bool* onEdge) const {
  if (onEdge) *onEdge = false;
  if (p.y < m_rect.y || p.y >= m_rect.y + m_headerH || p.x < m_rect.x || p.x > m_rect.right()) return -1;
  for (int d = 0; d < int(m_order.size()); d++) {
    int c = m_order[size_t(d)];
    Rect r = headerCell(c);
    if (m_cols[size_t(c)].resizable && std::abs(p.x - r.right()) <= kGrip / 2 + 1) { if (onEdge) *onEdge = true; return c; }
    if (p.x >= r.x && p.x < r.right()) return c;
  }
  return -1;
}

Cursor TableView::cursor() const {
  if (m_drag == Drag::Resize) return Cursor::SizeWE;
  if (m_drag == Drag::Reorder) return Cursor::SizeAll;
  bool edge = false;
  hdrAt(Application::get().mousePos(), &edge);
  return edge ? Cursor::SizeWE : Cursor::Arrow;
}

void TableView::paint(Canvas& c) {
  const Theme& t = theme();
  const Font& f = *t.ui;
  const Font& fb = *t.semibold;
  rebuild();
  c.fillRect(m_rect, t.panel);
  bool vbar = vBarNeeded();
  float viewW = m_rect.w - (vbar ? VScrollbar::W : 0.f);
  Rect bd = body();
  // rows
  c.pushClip(bd);
  if (m_rows > 0) {
    int first = viewRowAt(m_scrollY), last = viewRowAt(m_scrollY + bd.h);
    for (int v = first; v <= last; v++) {
      int dr = m_view[size_t(v)];
      Rect row{m_rect.x, bd.y + m_tops[size_t(v)] - m_scrollY, viewW, m_tops[size_t(v) + 1] - m_tops[size_t(v)]};
      bool sel = m_sel.count(dr) > 0;
      Color bg = sel ? t.accent.withAlpha(110) : (v % 2 ? Color(255, 255, 255, 6) : Color(0, 0, 0, 0));
      if (!sel && v == m_hoverRow && m_hovered) bg = Color(255, 255, 255, 14);
      if (bg.a) c.fillRect(row, bg);
      for (int d = 0; d < int(m_order.size()); d++) {
        int col = m_order[size_t(d)];
        Rect cell{m_rect.x + colX(d) - m_scrollX, row.y, m_cols[size_t(col)].width, row.h};
        if (cell.right() < m_rect.x || cell.x > m_rect.x + viewW) continue;
        if (paintCell) { c.pushClip(cell); paintCell(c, dr, col, cell, sel); c.popClip(); }
        else if (cellText)
          c.textIn(f, {cell.x + 8, cell.y, cell.w - 16, cell.h}, cellText(dr, col), sel ? t.textBright : t.text, m_cols[size_t(col)].align);
      }
    }
  }
  c.popClip();
  // header
  c.pushClip({m_rect.x, m_rect.y, viewW, m_headerH});
  c.fillRect({m_rect.x, m_rect.y, m_rect.w, m_headerH}, t.header);
  for (int d = 0; d < int(m_order.size()); d++) {
    int col = m_order[size_t(d)];
    const TableColumn& tc = m_cols[size_t(col)];
    Rect r = headerCell(col);
    if (r.right() < m_rect.x || r.x > m_rect.x + viewW) continue;
    bool dragging = m_drag == Drag::Reorder && m_dragCol == col && m_dragMoved;
    if (col == m_hoverHdr && m_drag == Drag::None) c.fillRect(r, t.controlHover);
    if (dragging) c.fillRect(r, t.accent.withAlpha(60));
    float tx = r.x + 8;
    if (tc.icon != Icon::None) { drawIcon(c, tc.icon, {tx, r.y + (r.h - 14) / 2, 14, 14}, t.textDim); tx += 18; }
    float arrowW = col == m_sortCol ? 16.f : 0.f;
    c.textIn(fb, {tx, r.y, std::max(0.f, r.right() - tx - 8 - arrowW), r.h}, tc.title, t.text, tc.align);
    if (col == m_sortCol) {
      float cx = r.right() - 14, cy = r.y + r.h / 2;
      bool up = m_sortState == SortState::Ascending;
      Vec2 a = up ? Vec2{cx - 4, cy + 2} : Vec2{cx - 4, cy - 2}, b = up ? Vec2{cx, cy - 3} : Vec2{cx, cy + 3}, d2 = up ? Vec2{cx + 4, cy + 2} : Vec2{cx + 4, cy - 2};
      c.line(a, b, t.accent, 1.6f); c.line(b, d2, t.accent, 1.6f);
    }
    c.vline(r.right() - 1, r.y + 5, r.y + r.h - 5, t.border);
  }
  c.hline(m_rect.x, m_rect.right(), m_rect.y + m_headerH - 1, t.borderStrong);
  c.popClip();
  // reorder drop indicator
  if (m_drag == Drag::Reorder && m_dragMoved) {
    float x = m_dragMouseX; int target = 0; float best = 1e9f;
    for (int d = 0; d <= int(m_order.size()); d++) {
      float edge = m_rect.x + colX(d) - m_scrollX;
      if (std::abs(edge - x) < best) { best = std::abs(edge - x); target = d; }
    }
    c.fillRect({m_rect.x + colX(target) - m_scrollX - 1, m_rect.y, 2, m_rect.h}, t.accentHover);
  }
  if (vbar) m_bar.paint(c, bd, contentHeight(), m_scrollY, m_hovered && Application::get().mousePos().x >= m_rect.right() - VScrollbar::W);
}

bool TableView::onMouseDown(const MouseEvent& e) {
  rebuild();
  Rect bd = body();
  if (e.button == MouseButton::Left && vBarNeeded() && e.pos.x >= m_rect.right() - VScrollbar::W && e.pos.y >= bd.y) {
    m_bar.mouseDown(e.pos, bd, contentHeight(), m_scrollY);
    m_drag = Drag::VBar; relayout(); return true;
  }
  bool edge = false;
  int col = hdrAt(e.pos, &edge);
  if (col >= 0 && e.button == MouseButton::Left) {
    if (edge) {
      if (e.clicks >= 2) { autoFitColumn(col); return true; }
      m_drag = Drag::Resize; m_dragCol = col; m_dragStartX = e.pos.x; m_dragStartW = m_cols[size_t(col)].width;
      return true;
    }
    m_drag = Drag::Reorder; m_dragCol = col; m_dragStartX = e.pos.x; m_dragMouseX = e.pos.x; m_dragMoved = false;
    return true;
  }
  if (e.pos.y < bd.y) return false;
  int v = e.pos.y >= bd.y && e.pos.x <= m_rect.right() ? viewRowAt(e.pos.y - bd.y + m_scrollY) : -1;
  if (v < 0 || e.pos.y - bd.y + m_scrollY > contentHeight()) {
    if (e.button == MouseButton::Left && !m_sel.empty()) { m_sel.clear(); fire(); invalidate(); }
    return true;
  }
  int dr = m_view[size_t(v)];
  if (e.button == MouseButton::Right) {
    if (!m_sel.count(dr)) { m_sel = {dr}; m_anchor = v; fire(); }
    invalidate();
    if (onContext) onContext(dr, e.pos);
    return true;
  }
  if (e.button != MouseButton::Left) return false;
  if (e.mods & ModShift && m_anchor >= 0) {
    m_sel.clear();
    for (int i = std::min(m_anchor, v); i <= std::max(m_anchor, v); i++) m_sel.insert(m_view[size_t(i)]);
  } else if (e.mods & ModCtrl) {
    if (!m_sel.erase(dr)) m_sel.insert(dr);
    m_anchor = v;
  } else { m_sel = {dr}; m_anchor = v; }
  fire(); invalidate();
  if (e.clicks >= 2 && onActivate) onActivate(dr);
  return true;
}

bool TableView::onMouseUp(const MouseEvent& e) {
  Drag d = m_drag; m_drag = Drag::None; m_bar.mouseUp();
  if (d == Drag::Reorder) {
    if (m_dragMoved) {
      float x = e.pos.x; int target = 0; float best = 1e9f;
      for (int i = 0; i <= int(m_order.size()); i++) {
        float edge = m_rect.x + colX(i) - m_scrollX;
        if (std::abs(edge - x) < best) { best = std::abs(edge - x); target = i; }
      }
      int from = int(std::find(m_order.begin(), m_order.end(), m_dragCol) - m_order.begin());
      if (target > from) target--;                       // removal shifts later slots left
      if (target != from) moveColumn(m_dragCol, target);
    } else if (m_cols[size_t(m_dragCol)].sortable) {      // plain click: none -> asc -> desc -> none
      SortState s = SortState::Ascending;
      if (m_sortCol == m_dragCol) s = m_sortState == SortState::Ascending ? SortState::Descending : SortState::None;
      sortBy(m_dragCol, s);
    }
    m_dragMoved = false; invalidate(); return true;
  }
  if (d == Drag::Resize) { if (onColumnsChanged) onColumnsChanged(); invalidate(); return true; }
  return d == Drag::VBar;
}

bool TableView::onMouseMove(const MouseEvent& e) {
  if (m_drag == Drag::VBar) { m_bar.mouseMove(e.pos, body(), contentHeight(), m_scrollY); relayout(); return true; }
  if (m_drag == Drag::Resize) { setColumnWidth(m_dragCol, m_dragStartW + e.pos.x - m_dragStartX); return true; }
  if (m_drag == Drag::Reorder) {
    m_dragMouseX = e.pos.x;
    if (std::abs(e.pos.x - m_dragStartX) > 4) m_dragMoved = true;
    invalidate(); return true;
  }
  int h = hdrAt(e.pos);
  int r = -1;
  if (e.pos.y >= m_rect.y + m_headerH && m_rect.contains(e.pos) && e.pos.y - body().y + m_scrollY < contentHeight())
    r = viewRowAt(e.pos.y - body().y + m_scrollY);
  if (h != m_hoverHdr || r != m_hoverRow) { m_hoverHdr = h; m_hoverRow = r; invalidate(); }
  return false;
}

bool TableView::onMouseWheel(const MouseEvent& e) {
  if (e.mods & ModShift) { m_scrollX -= e.wheel.y * 40; clampScroll(); invalidate(); return true; }
  if (!vBarNeeded() && e.wheel.x == 0) return false;
  m_scrollY -= e.wheel.y * m_rowH * 3; m_scrollX -= e.wheel.x * 40;
  clampScroll(); invalidate(); return true;
}

void TableView::select(const std::vector<int>& rows) {
  m_sel = std::set<int>(rows.begin(), rows.end());
  m_anchor = rows.empty() ? -1 : viewRow(rows.back());
  fire(); invalidate();
}

void TableView::fire() { if (onSelect) onSelect(std::vector<int>(m_sel.begin(), m_sel.end())); }

bool TableView::onKeyDown(const KeyEvent& e) {
  rebuild();
  if (m_rows == 0) return false;
  int cur = m_anchor < 0 ? -1 : m_anchor;
  int page = std::max(1, int(body().h / m_rowH) - 1);
  int next = cur;
  switch (e.key) {
    case Key::Down: next = cur + 1; break;
    case Key::Up: next = cur < 0 ? 0 : cur - 1; break;
    case Key::PageDown: next = cur + page; break;
    case Key::PageUp: next = cur - page; break;
    case Key::Home: next = 0; break;
    case Key::End: next = m_rows - 1; break;
    case Key::Enter: if (cur >= 0 && onActivate) { onActivate(m_view[size_t(cur)]); return true; } return false;
    case Key::A: if (e.mods & ModCtrl) { m_sel.clear(); for (int i = 0; i < m_rows; i++) m_sel.insert(i); fire(); invalidate(); return true; } return false;
    default: return false;
  }
  next = std::clamp(next, 0, m_rows - 1);
  if (e.mods & ModShift && m_anchor >= 0) {
    m_sel.clear();
    for (int i = std::min(cur, next); i <= std::max(cur, next); i++) m_sel.insert(m_view[size_t(i)]);
  } else m_sel = {m_view[size_t(next)]};
  m_anchor = next;
  ensureVisible(next); fire(); invalidate();
  return true;
}

} // namespace prism
