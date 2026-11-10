// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Layouts.h"
#include <algorithm>
#include <cmath>

namespace prism {

// ============================================================================
// 1. Grid Implementation
// ============================================================================
Grid& Grid::add(Ref<Widget> w, int row, int col, int rowSpan, int colSpan, Slot s) {
  w->slot = s;
  GridSlot gs;
  static_cast<Slot&>(gs) = s;
  gs.row = std::max(0, row);
  gs.column = std::max(0, col);
  gs.rowSpan = std::max(1, rowSpan);
  gs.colSpan = std::max(1, colSpan);
  m_gridSlots.push_back(gs);
  addChild(std::move(w));
  return *this;
}

Vec2 Grid::measure(Vec2 avail) {
  if (m_rows.empty()) m_rows.push_back(GridTrack::star(1.0f));
  if (m_cols.empty()) m_cols.push_back(GridTrack::star(1.0f));

  m_rowHeights.assign(m_rows.size(), 0.0f);
  m_colWidths.assign(m_cols.size(), 0.0f);

  // Initial pass: measure auto and pixel tracks
  for (size_t r = 0; r < m_rows.size(); r++) {
    if (m_rows[r].type == GridUnitType::Pixel) m_rowHeights[r] = m_rows[r].value;
  }
  for (size_t c = 0; c < m_cols.size(); c++) {
    if (m_cols[c].type == GridUnitType::Pixel) m_colWidths[c] = m_cols[c].value;
  }

  for (size_t i = 0; i < m_children.size(); i++) {
    auto& child = m_children[i];
    if (!child->visible()) continue;
    const auto& gs = m_gridSlots[i];
    Vec2 childAvail{avail.x, avail.y};
    Vec2 d = child->measure(childAvail);
    d.x += gs.pad.horizontal();
    d.y += gs.pad.vertical();

    if (gs.colSpan == 1 && gs.column < int(m_cols.size()) && m_cols[size_t(gs.column)].type == GridUnitType::Auto) {
      m_colWidths[size_t(gs.column)] = std::max(m_colWidths[size_t(gs.column)], d.x);
    }
    if (gs.rowSpan == 1 && gs.row < int(m_rows.size()) && m_rows[size_t(gs.row)].type == GridUnitType::Auto) {
      m_rowHeights[size_t(gs.row)] = std::max(m_rowHeights[size_t(gs.row)], d.y);
    }
  }

  float totalW = (m_cols.size() > 1 ? float(m_cols.size() - 1) * m_colSpacing : 0.0f);
  for (float w : m_colWidths) totalW += w;

  float totalH = (m_rows.size() > 1 ? float(m_rows.size() - 1) * m_rowSpacing : 0.0f);
  for (float h : m_rowHeights) totalH += h;

  return m_desired = constrain({totalW, totalH});
}

void Grid::arrange(Rect r) {
  m_rect = r;
  if (m_rows.empty() || m_cols.empty()) return;

  if (m_colWidths.size() != m_cols.size() || m_rowHeights.size() != m_rows.size()) {
    measure(Vec2(r.w, r.h));
  }

  // Resolve Column Widths
  float fixedW = (m_cols.size() > 1 ? float(m_cols.size() - 1) * m_colSpacing : 0.0f);
  float starColsSum = 0.0f;
  for (size_t c = 0; c < m_cols.size(); c++) {
    if (m_cols[c].type == GridUnitType::Star) starColsSum += m_cols[c].value;
    else fixedW += m_colWidths[c];
  }
  float remainingW = std::max(0.0f, r.w - fixedW);
  for (size_t c = 0; c < m_cols.size(); c++) {
    if (m_cols[c].type == GridUnitType::Star && starColsSum > 0.0f) {
      m_colWidths[c] = std::floor(remainingW * m_cols[c].value / starColsSum);
    }
  }

  // Resolve Row Heights
  float fixedH = (m_rows.size() > 1 ? float(m_rows.size() - 1) * m_rowSpacing : 0.0f);
  float starRowsSum = 0.0f;
  for (size_t row = 0; row < m_rows.size(); row++) {
    if (m_rows[row].type == GridUnitType::Star) starRowsSum += m_rows[row].value;
    else fixedH += m_rowHeights[row];
  }
  float remainingH = std::max(0.0f, r.h - fixedH);
  for (size_t row = 0; row < m_rows.size(); row++) {
    if (m_rows[row].type == GridUnitType::Star && starRowsSum > 0.0f) {
      m_rowHeights[row] = std::floor(remainingH * m_rows[row].value / starRowsSum);
    }
  }

  // Precompute grid lines
  std::vector<float> colPositions(m_cols.size() + 1, r.x);
  float curX = r.x;
  for (size_t c = 0; c < m_cols.size(); c++) {
    colPositions[c] = curX;
    curX += m_colWidths[c] + m_colSpacing;
  }
  colPositions.back() = curX - m_colSpacing;

  std::vector<float> rowPositions(m_rows.size() + 1, r.y);
  float curY = r.y;
  for (size_t row = 0; row < m_rows.size(); row++) {
    rowPositions[row] = curY;
    curY += m_rowHeights[row] + m_rowSpacing;
  }
  rowPositions.back() = curY - m_rowSpacing;

  // Arrange each child
  for (size_t i = 0; i < m_children.size(); i++) {
    auto& child = m_children[i];
    if (!child->visible()) continue;
    const auto& gs = m_gridSlots[i];
    int c0 = std::min(gs.column, int(m_cols.size()) - 1);
    int c1 = std::min(gs.column + gs.colSpan, int(m_cols.size()));
    int r0 = std::min(gs.row, int(m_rows.size()) - 1);
    int r1 = std::min(gs.row + gs.rowSpan, int(m_rows.size()));

    float cellX = colPositions[size_t(c0)];
    float cellW = colPositions[size_t(c1)] - cellX - (c1 < int(m_cols.size()) ? m_colSpacing : 0.0f);
    float cellY = rowPositions[size_t(r0)];
    float cellH = rowPositions[size_t(r1)] - cellY - (r1 < int(m_rows.size()) ? m_rowSpacing : 0.0f);

    Rect cellRect(cellX, cellY, std::max(0.0f, cellW), std::max(0.0f, cellH));
    Rect inner = cellRect.deflated(gs.pad);
    child->arrange(alignInCell(inner, child->desired(), gs));
  }
}

// ============================================================================
// 2. UniformGrid Implementation
// ============================================================================
UniformGrid& UniformGrid::add(Ref<Widget> w, Slot s) {
  w->slot = s;
  addChild(std::move(w));
  return *this;
}

Vec2 UniformGrid::measure(Vec2 avail) {
  float maxW = 0.0f, maxH = 0.0f;
  int count = 0;
  for (auto& c : m_children) {
    if (!c->visible()) continue;
    Vec2 d = c->measure(avail);
    d.x += c->slot.pad.horizontal();
    d.y += c->slot.pad.vertical();
    maxW = std::max(maxW, d.x);
    maxH = std::max(maxH, d.y);
    count++;
  }
  if (count == 0) return m_desired = constrain({0, 0});
  int cols = std::min(count, m_columns);
  int rows = (count + cols - 1) / cols;
  float totalW = maxW * float(cols) + m_slotPadding * float(std::max(0, cols - 1));
  float totalH = maxH * float(rows) + m_slotPadding * float(std::max(0, rows - 1));
  return m_desired = constrain({totalW, totalH});
}

void UniformGrid::arrange(Rect r) {
  m_rect = r;
  int visibleCount = 0;
  for (auto& c : m_children) if (c->visible()) visibleCount++;
  if (visibleCount == 0) return;

  int cols = std::min(visibleCount, m_columns);
  int rows = (visibleCount + cols - 1) / cols;
  float cellW = std::max(0.0f, (r.w - m_slotPadding * float(cols - 1)) / float(cols));
  float cellH = std::max(0.0f, (r.h - m_slotPadding * float(rows - 1)) / float(rows));

  int idx = 0;
  for (auto& c : m_children) {
    if (!c->visible()) continue;
    int col = idx % cols;
    int row = idx / cols;
    float cx = r.x + float(col) * (cellW + m_slotPadding);
    float cy = r.y + float(row) * (cellH + m_slotPadding);
    Rect cell(cx, cy, cellW, cellH);
    c->arrange(alignInCell(cell.deflated(c->slot.pad), c->desired(), c->slot));
    idx++;
  }
}

// ============================================================================
// 3. WrapBox Implementation
// ============================================================================
WrapBox& WrapBox::add(Ref<Widget> w, Slot s) {
  w->slot = s;
  addChild(std::move(w));
  return *this;
}

Vec2 WrapBox::measure(Vec2 avail) {
  float maxW = 0.0f;
  float totalH = 0.0f;
  float curX = 0.0f;
  float lineH = 0.0f;

  for (auto& c : m_children) {
    if (!c->visible()) continue;
    Vec2 d = c->measure(avail);
    d.x += c->slot.pad.horizontal();
    d.y += c->slot.pad.vertical();

    if (curX > 0.0f && curX + d.x > avail.x) {
      // Wrap line
      maxW = std::max(maxW, curX - m_itemSpacing);
      totalH += lineH + m_lineSpacing;
      curX = 0.0f;
      lineH = 0.0f;
    }

    curX += d.x + m_itemSpacing;
    lineH = std::max(lineH, d.y);
  }
  if (curX > 0.0f) {
    maxW = std::max(maxW, curX - m_itemSpacing);
    totalH += lineH;
  }

  return m_desired = constrain({maxW, totalH});
}

void WrapBox::arrange(Rect r) {
  m_rect = r;
  float curX = 0.0f;
  float curY = 0.0f;
  float lineH = 0.0f;

  struct LineItem {
    Widget* w;
    float x;
  };
  std::vector<LineItem> lineWidgets;

  auto flushLine = [&](float lH) {
    for (const auto& item : lineWidgets) {
      Rect cell(r.x + item.x, r.y + curY, item.w->desired().x, lH);
      item.w->arrange(alignInCell(cell.deflated(item.w->slot.pad), item.w->desired(), item.w->slot));
    }
    lineWidgets.clear();
  };

  for (auto& c : m_children) {
    if (!c->visible()) continue;
    Vec2 d = c->measure(Vec2(r.w, r.h));
    d.x += c->slot.pad.horizontal();
    d.y += c->slot.pad.vertical();

    if (curX > 0.0f && curX + d.x > r.w) {
      flushLine(lineH);
      curY += lineH + m_lineSpacing;
      curX = 0.0f;
      lineH = 0.0f;
    }

    lineWidgets.push_back({c.get(), curX});

    curX += d.x + m_itemSpacing;
    lineH = std::max(lineH, d.y);
  }

  if (!lineWidgets.empty()) {
    flushLine(lineH);
  }
}

// ============================================================================
// 4. SizeBox Implementation
// ============================================================================
SizeBox& SizeBox::setContent(Ref<Widget> w) {
  clearChildren();
  m_content = w;
  if (w) addChild(std::move(w));
  return *this;
}

Vec2 SizeBox::measure(Vec2 avail) {
  Vec2 cd{0, 0};
  if (m_content && m_content->visible()) {
    cd = m_content->measure(avail);
  }
  float w = (m_widthOverride >= 0.0f) ? m_widthOverride : cd.x;
  float h = (m_heightOverride >= 0.0f) ? m_heightOverride : cd.y;
  w = std::clamp(w, m_minDesiredW, m_maxDesiredW);
  h = std::clamp(h, m_minDesiredH, m_maxDesiredH);
  return m_desired = constrain({w, h});
}

void SizeBox::arrange(Rect r) {
  m_rect = r;
  if (m_content && m_content->visible()) {
    m_content->arrange(r);
  }
}

// ============================================================================
// 5. ScaleBox Implementation
// ============================================================================
ScaleBox& ScaleBox::setContent(Ref<Widget> w) {
  clearChildren();
  m_content = w;
  if (w) addChild(std::move(w));
  return *this;
}

Vec2 ScaleBox::measure(Vec2 avail) {
  if (!m_content || !m_content->visible()) return m_desired = constrain({0, 0});
  return m_desired = constrain(m_content->measure(avail));
}

void ScaleBox::arrange(Rect r) {
  m_rect = r;
  if (!m_content || !m_content->visible()) return;
  Vec2 d = m_content->desired();
  if (d.x <= 0.0f || d.y <= 0.0f) {
    d = m_content->measure(Vec2(r.w, r.h));
  }
  if (d.x <= 0.0f || d.y <= 0.0f) {
    m_content->arrange(r);
    return;
  }
  float scale = 1.0f;
  if (m_stretch == StretchMode::ScaleToFit) {
    scale = std::min(r.w / d.x, r.h / d.y);
  } else if (m_stretch == StretchMode::ScaleToFill) {
    scale = std::max(r.w / d.x, r.h / d.y);
  }
  float finalW = d.x * scale;
  float finalH = d.y * scale;
  float offX = r.x + (r.w - finalW) * 0.5f;
  float offY = r.y + (r.h - finalH) * 0.5f;
  m_content->arrange(Rect(offX, offY, finalW, finalH));
}

// ============================================================================
// 6. StackSwitcher Implementation
// ============================================================================
StackSwitcher& StackSwitcher::addPage(Ref<Widget> page) {
  addChild(std::move(page));
  return *this;
}

void StackSwitcher::setActiveIndex(int index) {
  int next = std::clamp(index, 0, std::max(0, int(m_children.size()) - 1));
  if (m_active != next) {
    m_active = next;
    relayout();
    invalidate();
  }
}

Vec2 StackSwitcher::measure(Vec2 avail) {
  float maxW = 0.0f, maxH = 0.0f;
  for (auto& c : m_children) {
    if (c->visible()) {
      Vec2 d = c->measure(avail);
      maxW = std::max(maxW, d.x);
      maxH = std::max(maxH, d.y);
    }
  }
  return m_desired = constrain({maxW, maxH});
}

void StackSwitcher::arrange(Rect r) {
  m_rect = r;
  for (size_t i = 0; i < m_children.size(); i++) {
    if (int(i) == m_active && m_children[i]->visible()) {
      m_children[i]->arrange(r);
    }
  }
}

void StackSwitcher::paint(Canvas& c) {
  if (m_active >= 0 && m_active < int(m_children.size())) {
    auto& activeChild = m_children[size_t(m_active)];
    if (activeChild->visible()) {
      activeChild->paint(c);
    }
  }
}

Widget* StackSwitcher::hitTest(Vec2 p) {
  if (m_active >= 0 && m_active < int(m_children.size())) {
    auto& activeChild = m_children[size_t(m_active)];
    if (activeChild->visible() && activeChild->rect().contains(p)) {
      return activeChild->hitTest(p);
    }
  }
  return nullptr;
}

// ============================================================================
// 7. Expander Implementation
// ============================================================================
Expander::Expander(std::string title, bool initiallyExpanded)
    : m_title(std::move(title)), m_expanded(initiallyExpanded) {}

Expander& Expander::setContent(Ref<Widget> content) {
  clearChildren();
  m_content = content;
  if (content) addChild(std::move(content));
  return *this;
}

void Expander::setExpanded(bool exp) {
  if (m_expanded != exp) {
    m_expanded = exp;
    if (onToggle) onToggle(m_expanded);
    relayout();
    invalidate();
  }
}

Vec2 Expander::measure(Vec2 avail) {
  float w = 120.0f;
  float h = m_headerHeight;
  if (m_content && m_expanded && m_content->visible()) {
    Vec2 cd = m_content->measure({avail.x, std::max(0.0f, avail.y - m_headerHeight)});
    w = std::max(w, cd.x);
    h += cd.y;
  }
  return m_desired = constrain({w, h});
}

void Expander::arrange(Rect r) {
  m_rect = r;
  if (m_content && m_expanded && m_content->visible()) {
    Rect contentRect(r.x, r.y + m_headerHeight, r.w, std::max(0.0f, r.h - m_headerHeight));
    m_content->arrange(contentRect);
  }
}

void Expander::paint(Canvas& c) {
  Rect r = m_rect;
  const Theme& t = theme();
  Rect headerRect(r.x, r.y, r.w, m_headerHeight);

  // Draw header bar
  Color headerBg = m_hovered ? t.header.mix(t.accent, 0.08f) : t.header;
  c.fillRect(headerRect, headerBg);
  c.strokeRect(headerRect, t.border);

  // Chevron arrow icon
  Icon arrowIcon = m_expanded ? Icon::TriDown : Icon::TriRight;
  drawIcon(c, arrowIcon, Rect(r.x + 6, r.y + 4, 16, 16), t.text);

  // Title
  c.textIn(*t.semibold, Rect(r.x + 26, r.y, r.w - 30, m_headerHeight), m_title, t.text, HAlign::Left);

  // Body content
  if (m_content && m_expanded && m_content->visible()) {
    m_content->paint(c);
  }
}

bool Expander::onMouseDown(const MouseEvent& e) {
  Rect headerRect(m_rect.x, m_rect.y, m_rect.w, m_headerHeight);
  if (e.button == MouseButton::Left && headerRect.contains(e.pos)) {
    setExpanded(!m_expanded);
    return true;
  }
  return false;
}

Widget* Expander::hitTest(Vec2 p) {
  if (!m_rect.contains(p)) return nullptr;
  Rect headerRect(m_rect.x, m_rect.y, m_rect.w, m_headerHeight);
  if (headerRect.contains(p)) return this;
  if (m_content && m_expanded && m_content->visible() && m_content->rect().contains(p)) {
    return m_content->hitTest(p);
  }
  return this;
}

// ============================================================================
// 8. ScrollBox Implementation
// ============================================================================
void ScrollBox::setContent(Ref<Widget> content) {
  clearChildren();
  m_content = content;
  if (content) addChild(std::move(content));
}

Vec2 ScrollBox::measure(Vec2 avail) {
  if (!m_content || !m_content->visible()) return m_desired = constrain({0, 0});
  m_contentSize = m_content->measure(avail);
  return m_desired = constrain({0, 0});
}

void ScrollBox::arrange(Rect r) {
  m_rect = r;
  if (!m_content || !m_content->visible()) return;

  float maxScrollY = std::max(0.0f, m_contentSize.y - r.h);
  float maxScrollX = std::max(0.0f, m_contentSize.x - r.w);
  m_scroll.y = std::clamp(m_scroll.y, 0.0f, maxScrollY);
  m_scroll.x = std::clamp(m_scroll.x, 0.0f, maxScrollX);

  float arrangeW = std::max(r.w, m_contentSize.x);
  float arrangeH = std::max(r.h, m_contentSize.y);
  m_content->arrange(Rect(r.x - m_scroll.x, r.y - m_scroll.y, arrangeW, arrangeH));
}

void ScrollBox::paint(Canvas& c) {
  if (!m_content || !m_content->visible()) return;
  c.pushClip(m_rect);
  m_content->paint(c);
  c.popClip();

  // Draw vertical scrollbar if needed
  if (VScrollbar::needed(m_contentSize.y, m_rect.h)) {
    m_vbar.paint(c, m_rect, m_contentSize.y, m_scroll.y, m_hovered);
  }
}

Widget* ScrollBox::hitTest(Vec2 p) {
  if (!m_rect.contains(p)) return nullptr;
  if (VScrollbar::needed(m_contentSize.y, m_rect.h) && p.x >= m_rect.right() - VScrollbar::W) {
    return this;
  }
  if (m_content && m_content->visible()) {
    return m_content->hitTest(p);
  }
  return this;
}

bool ScrollBox::onMouseDown(const MouseEvent& e) {
  if (VScrollbar::needed(m_contentSize.y, m_rect.h)) {
    if (m_vbar.mouseDown(e.pos, m_rect, m_contentSize.y, m_scroll.y)) {
      relayout();
      return true;
    }
  }
  return false;
}

bool ScrollBox::onMouseMove(const MouseEvent& e) {
  if (m_vbar.dragging) {
    if (m_vbar.mouseMove(e.pos, m_rect, m_contentSize.y, m_scroll.y)) {
      relayout();
      return true;
    }
  }
  return false;
}

bool ScrollBox::onMouseUp(const MouseEvent& /*e*/) {
  if (m_vbar.dragging) {
    m_vbar.mouseUp();
    return true;
  }
  return false;
}

bool ScrollBox::onMouseWheel(const MouseEvent& e) {
  if (m_contentSize.y > m_rect.h) {
    float maxScroll = m_contentSize.y - m_rect.h;
    m_scroll.y = std::clamp(m_scroll.y - e.wheel.y * 36.0f, 0.0f, maxScroll);
    relayout();
    invalidate();
    return true;
  }
  return false;
}

} // namespace prism
