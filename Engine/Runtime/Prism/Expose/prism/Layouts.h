// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Layout Containers: Grid, UniformGrid, WrapBox, SizeBox, ScaleBox, StackSwitcher, Expander, ScrollBox.
#pragma once
#include "Widget.h"
#include "Containers.h"
#include "Animation.h"
#include <string>
#include <vector>

namespace prism {

// ============================================================================
// 1. Grid Definition & Widget
// ============================================================================
enum class GridUnitType : uint8_t { Auto, Pixel, Star };

struct GridTrack {
  GridUnitType type = GridUnitType::Star;
  float value = 1.0f;

  static GridTrack autoSize() { return {GridUnitType::Auto, 0.0f}; }
  static GridTrack pixel(float px) { return {GridUnitType::Pixel, px}; }
  static GridTrack star(float ratio = 1.0f) { return {GridUnitType::Star, ratio}; }
};

struct GridSlot : public Slot {
  int row = 0;
  int column = 0;
  int rowSpan = 1;
  int colSpan = 1;
};

class Grid : public Widget {
public:
  Grid& addRow(GridTrack track = GridTrack::star(1.0f)) { m_rows.push_back(track); relayout(); return *this; }
  Grid& addColumn(GridTrack track = GridTrack::star(1.0f)) { m_cols.push_back(track); relayout(); return *this; }
  Grid& add(Ref<Widget> w, int row, int col, int rowSpan = 1, int colSpan = 1, Slot s = {});

  Grid& setRowSpacing(float sp) { m_rowSpacing = sp; relayout(); return *this; }
  Grid& setColSpacing(float sp) { m_colSpacing = sp; relayout(); return *this; }

  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  bool hitSelf() const override { return false; }

private:
  std::vector<GridTrack> m_rows;
  std::vector<GridTrack> m_cols;
  std::vector<GridSlot> m_gridSlots;
  std::vector<float> m_rowHeights;
  std::vector<float> m_colWidths;
  float m_rowSpacing = 4.0f;
  float m_colSpacing = 4.0f;
};

// ============================================================================
// 2. UniformGrid: cells of equal size across N columns
// ============================================================================
class UniformGrid : public Widget {
public:
  explicit UniformGrid(int columns = 4, float slotPadding = 4.0f)
      : m_columns(std::max(1, columns)), m_slotPadding(slotPadding) {}

  UniformGrid& add(Ref<Widget> w, Slot s = {});
  UniformGrid& setColumns(int c) { m_columns = std::max(1, c); relayout(); return *this; }
  UniformGrid& setSlotPadding(float p) { m_slotPadding = p; relayout(); return *this; }

  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  bool hitSelf() const override { return false; }

private:
  int m_columns = 4;
  float m_slotPadding = 4.0f;
};

// ============================================================================
// 3. WrapBox: flows items and wraps to the next line when width is exceeded
// ============================================================================
class WrapBox : public Widget {
public:
  explicit WrapBox(Orientation o = Orientation::Horizontal, float itemSpacing = 4.0f, float lineSpacing = 4.0f)
      : m_orient(o), m_itemSpacing(itemSpacing), m_lineSpacing(lineSpacing) {}

  WrapBox& add(Ref<Widget> w, Slot s = {});
  WrapBox& setItemSpacing(float s) { m_itemSpacing = s; relayout(); return *this; }
  WrapBox& setLineSpacing(float s) { m_lineSpacing = s; relayout(); return *this; }

  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  bool hitSelf() const override { return false; }

private:
  Orientation m_orient = Orientation::Horizontal;
  float m_itemSpacing = 4.0f;
  float m_lineSpacing = 4.0f;
};

// ============================================================================
// 4. SizeBox: enforces min/max/exact dimensions on child
// ============================================================================
class SizeBox : public Widget {
public:
  SizeBox& setContent(Ref<Widget> w);
  SizeBox& setWidthOverride(float w) { m_widthOverride = w; relayout(); return *this; }
  SizeBox& setHeightOverride(float h) { m_heightOverride = h; relayout(); return *this; }
  SizeBox& setMinDesiredWidth(float w) { m_minDesiredW = w; relayout(); return *this; }
  SizeBox& setMinDesiredHeight(float h) { m_minDesiredH = h; relayout(); return *this; }
  SizeBox& setMaxDesiredWidth(float w) { m_maxDesiredW = w; relayout(); return *this; }
  SizeBox& setMaxDesiredHeight(float h) { m_maxDesiredH = h; relayout(); return *this; }

  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  bool hitSelf() const override { return false; }

private:
  Ref<Widget> m_content;
  float m_widthOverride = -1.0f;
  float m_heightOverride = -1.0f;
  float m_minDesiredW = 0.0f;
  float m_minDesiredH = 0.0f;
  float m_maxDesiredW = 1e9f;
  float m_maxDesiredH = 1e9f;
};

// ============================================================================
// 5. ScaleBox: scales content to fit or fill parent bounds
// ============================================================================
enum class StretchMode : uint8_t {
  None,
  ScaleToFit,
  ScaleToFill,
};

class ScaleBox : public Widget {
public:
  ScaleBox& setContent(Ref<Widget> w);
  ScaleBox& setStretch(StretchMode mode) { m_stretch = mode; relayout(); return *this; }

  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  bool hitSelf() const override { return false; }

private:
  Ref<Widget> m_content;
  StretchMode m_stretch = StretchMode::ScaleToFit;
};

// ============================================================================
// 6. StackSwitcher: displays one child page at a time
// ============================================================================
class StackSwitcher : public Widget {
public:
  StackSwitcher& addPage(Ref<Widget> page);
  int activeIndex() const { return m_active; }
  void setActiveIndex(int index);
  int count() const { return int(m_children.size()); }

  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  Widget* hitTest(Vec2 p) override;
  bool hitSelf() const override { return false; }

private:
  int m_active = 0;
};

// ============================================================================
// 7. Expander: collapsible header with arrow, title, and body
// ============================================================================
class Expander : public Widget {
public:
  explicit Expander(std::string title = "", bool initiallyExpanded = true);

  Expander& setContent(Ref<Widget> content);
  Expander& setTitle(std::string title) { m_title = std::move(title); invalidate(); return *this; }
  bool isExpanded() const { return m_expanded; }
  void setExpanded(bool exp);

  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  Widget* hitTest(Vec2 p) override;
  bool hitSelf() const override { return true; }

  Callback<bool> onToggle;

private:
  std::string m_title;
  bool m_expanded = true;
  Ref<Widget> m_content;
  float m_headerHeight = 24.0f;
  CurveSequence m_anim;
};

// ============================================================================
// 8. ScrollBox: dual-axis scrolling container
// ============================================================================
class ScrollBox : public Widget {
public:
  explicit ScrollBox(Orientation o = Orientation::Vertical) : m_orientation(o) {}

  void setContent(Ref<Widget> content);
  Widget* content() const { return m_content.get(); }

  Vec2 scrollOffset() const { return m_scroll; }
  void setScrollOffset(Vec2 offset) { m_scroll = offset; relayout(); }

  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  Widget* hitTest(Vec2 p) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  bool onMouseWheel(const MouseEvent& e) override;

private:
  Orientation m_orientation = Orientation::Vertical;
  Ref<Widget> m_content;
  Vec2 m_scroll{0, 0};
  Vec2 m_contentSize{0, 0};
  VScrollbar m_vbar;
  bool m_draggingH = false;
  float m_dragStartH = 0.0f;
};

} // namespace prism
