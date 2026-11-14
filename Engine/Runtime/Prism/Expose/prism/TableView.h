// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - TableView: virtualized multi-column list with column headers, sorting, resizing and reordering,
// variable row heights and horizontal scrolling. Data is supplied through callbacks (model stays in the host).
#pragma once
#include "Containers.h"
#include "Controls.h"
#include <functional>
#include <set>

namespace prism {

struct TableColumn {
  std::string id, title;
  float width = 120, minWidth = 40;
  bool sortable = true, resizable = true;
  HAlign align = HAlign::Left;
  Icon icon = Icon::None;
};

enum class SortState { None, Ascending, Descending };

class TableView : public Widget {
public:
  // -- columns (indices below are *data* column indices; display order is columnOrder())
  int addColumn(TableColumn c);
  int columnCount() const { return int(m_cols.size()); }
  TableColumn& column(int i) { return m_cols[size_t(i)]; }
  const std::vector<int>& columnOrder() const { return m_order; }
  void moveColumn(int from, int toDisplayIndex);       // reorder by data column index
  void setColumnWidth(int col, float w);
  void autoFitColumn(int col);                          // fits to the widest visible cell / header
  // -- model
  void setRowCount(int n) { m_rows = n; m_dirtyRows = true; invalidate(); }
  int rowCount() const { return m_rows; }
  std::function<std::string(int row, int col)> cellText;                      // required (also used by sorting)
  std::function<float(int row)> rowHeightOf;                                  // optional variable heights
  std::function<void(Canvas&, int row, int col, Rect, bool selected)> paintCell;  // optional custom cell
  // -- sorting
  void sortBy(int col, SortState s);
  int sortColumn() const { return m_sortCol; }
  SortState sortState() const { return m_sortState; }
  int dataRow(int viewRow) const;                       // view (sorted) index -> model row
  int viewRow(int dataRow) const;
  std::function<bool(int a, int b, int col)> lessThan;  // optional custom comparator (model rows)
  Callback<int, SortState> onSort;                      // fired after the view order changed
  // -- selection
  const std::set<int>& selection() const { return m_sel; }
  void select(const std::vector<int>& dataRows);
  Callback<const std::vector<int>&> onSelect;
  Callback<int> onActivate;                             // double click / Enter (model row)
  Callback<int, Vec2> onContext;                        // right click on a row
  Callback<> onColumnsChanged;                          // resize / reorder
  // -- geometry helpers (also used by tests)
  float headerHeight() const { return m_headerH; }
  Rect headerCell(int col) const;                       // absolute rect of a column header
  float contentHeight() const;
  float rowTop(int viewRow) const;                      // content-space y of a view row
  int viewRowAt(float contentY) const;
  float scrollY() const { return m_scrollY; }
  float scrollX() const { return m_scrollX; }
  void setScrollY(float y) { m_scrollY = y; clampScroll(); relayout(); }
  void ensureVisible(int viewRow);

  Vec2 measure(Vec2) override { return m_desired = constrain({0, 0}); }
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onMouseWheel(const MouseEvent& e) override;
  bool onKeyDown(const KeyEvent& e) override;
  void onMouseLeave() override { m_hoverRow = -1; m_hoverHdr = -1; invalidate(); }
  bool focusable() const override { return true; }
  AccessRole accessRole() const override { return AccessRole::Table; }
  Cursor cursor() const override;
private:
  float totalWidth() const;
  float colX(int displayIdx) const;                     // content-space x of a display column
  int hdrAt(Vec2 p, bool* onEdge = nullptr) const;      // data col under p (header strip), edge = resize grip
  void rebuild() const;
  void resort();
  Rect body() const { return {m_rect.x, m_rect.y + m_headerH, m_rect.w, std::max(0.f, m_rect.h - m_headerH)}; }
  bool vBarNeeded() const { return VScrollbar::needed(contentHeight(), body().h); }
  void clampScroll();
  void fire();

  std::vector<TableColumn> m_cols;
  std::vector<int> m_order;                             // display -> data column
  int m_rows = 0;
  mutable bool m_dirtyRows = true;
  mutable std::vector<int> m_view;                      // view row -> model row
  mutable std::vector<float> m_tops;                    // prefix sums, size = rows+1
  int m_sortCol = -1; SortState m_sortState = SortState::None;
  std::set<int> m_sel; int m_anchor = -1;
  float m_headerH = 24, m_rowH = 22, m_scrollX = 0, m_scrollY = 0;
  int m_hoverRow = -1, m_hoverHdr = -1;
  // header interaction
  enum class Drag { None, Resize, Reorder, VBar } m_drag = Drag::None;
  int m_dragCol = -1; float m_dragStartX = 0, m_dragStartW = 0, m_dragMouseX = 0; bool m_dragMoved = false;
  VScrollbar m_bar;
};

} // namespace prism
