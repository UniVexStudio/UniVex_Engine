// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - widget base class: two-pass layout (measure/arrange), painting, input routing.
#pragma once
#include "Canvas.h"
#include "Icons.h"
#include "Theme.h"
#include "EventRouting.h"
#include "DragDrop.h"
#include "AccessRole.h"

namespace prism {

class Application;

enum class InvalidateFlags : uint8_t {
  None = 0,
  Layout = 1 << 0,
  Paint = 1 << 1,
  Volatile = 1 << 2,
  Children = 1 << 3,
};
inline InvalidateFlags operator|(InvalidateFlags a, InvalidateFlags b) {
  return static_cast<InvalidateFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline InvalidateFlags operator&(InvalidateFlags a, InvalidateFlags b) {
  return static_cast<InvalidateFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline bool hasFlag(InvalidateFlags mask, InvalidateFlags flag) {
  return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(flag)) != 0;
}

// Layout parameters a parent container reads from each child.
struct Slot {
  float fill = 0;               // >0: share of remaining space along the main axis
  Margin pad{};                 // outer margin around the child
  HAlign h = HAlign::Fill;
  VAlign v = VAlign::Fill;
};

class Widget : public std::enable_shared_from_this<Widget> {
public:
  Widget() = default;
  // A child that outlives this widget (held elsewhere) forgets it, so its parent chain never dangles.
  virtual ~Widget();
  Widget(const Widget&) = delete;
  Widget& operator=(const Widget&) = delete;

  // ---- layout
  virtual Vec2 measure(Vec2 avail);  // desired size; must call constrain() on the result
  virtual void arrange(Rect r);      // receive final rectangle (window coordinates), lay out children
  Vec2 desired() const { return m_desired; }
  Rect rect() const { return m_rect; }
  Slot slot;

  void setFixedSize(float w, float h) { m_fixedW = w; m_fixedH = h; relayout(); }
  void setFixedWidth(float w) { m_fixedW = w; relayout(); }
  void setFixedHeight(float h) { m_fixedH = h; relayout(); }
  void setMinSize(float w, float h) { m_minW = w; m_minH = h; relayout(); }
  void setMaxSize(float w, float h) { m_maxW = w; m_maxH = h; relayout(); }

  // ---- state
  bool visible() const { return m_visible; }
  void setVisible(bool v);
  bool enabled() const { return m_enabled && (!m_parent || m_parent->enabled()); }
  void setEnabled(bool e) { if (m_enabled != e) { m_enabled = e; invalidate(); } }
  bool hovered() const { return m_hovered; }
  bool focused() const { return m_focused; }
  const std::string& tooltip() const { return m_tooltip; }
  virtual const std::string& tooltipAt(Vec2) const { return m_tooltip; }
  void setTooltip(std::string t) { m_tooltip = std::move(t); }
  Ref<Widget> tooltipWidget() const { return m_tooltipWidget; }
  void setTooltipWidget(Ref<Widget> w) { m_tooltipWidget = std::move(w); }

  // ---- accessibility (semantic tree; OS bridges consume buildAccessTree())
  virtual AccessRole accessRole() const { return m_accessRole; }
  virtual std::string accessName() const { return m_accessName.empty() ? m_tooltip : m_accessName; }
  virtual std::string accessValue() const { return {}; }
  virtual bool accessChecked() const { return false; }
  // Actions a screen reader / automation client can perform (OS bridges map these to Invoke / Toggle / Value).
  virtual bool accessActivate() { return false; }                        // default action (press, toggle, open)
  virtual bool accessSetValue(const std::string&) { return false; }      // editable value (text, number, selection)
  virtual bool accessValueEditable() const { return false; }
  const std::string& accessDescription() const { return m_accessDesc; }
  void setAccessName(std::string n) { m_accessName = std::move(n); }
  void setAccessDescription(std::string d) { m_accessDesc = std::move(d); }
  void setAccessRole(AccessRole r) { m_accessRole = r; }

  // ---- tree
  Widget* parent() const { return m_parent; }
  const std::vector<Ref<Widget>>& children() const { return m_children; }
  void addChild(Ref<Widget> c);
  virtual void removeChild(const Widget* c);
  void clearChildren();
  template <class T> T* findAncestor() const {
    for (Widget* p = m_parent; p; p = p->m_parent)
      if (auto* t = dynamic_cast<T*>(p)) return t;
    return nullptr;
  }
  bool isDescendantOf(const Widget* w) const {
    for (const Widget* p = this; p; p = p->m_parent) if (p == w) return true;
    return false;
  }

  // ---- painting
  virtual void paint(Canvas& c);
  void paintChildren(Canvas& c);

  // ---- input (return true when handled)
  virtual Widget* hitTest(Vec2 p);
  virtual bool hitSelf() const { return true; }
  // False for regions that belong to the host (e.g. an engine viewport): Application::wantsPointerAt() reports them as not UI.
  virtual bool consumesHostInput() const { return true; }

  // Preview (Tunnel) Phase: Top-Down event routing
  virtual Reply onPreviewMouseDown(const MouseEvent&) { return Reply::unhandled(); }
  virtual Reply onPreviewMouseUp(const MouseEvent&) { return Reply::unhandled(); }
  virtual Reply onPreviewKeyDown(const KeyEvent&) { return Reply::unhandled(); }

  // Bubble Phase: Bottom-Up event routing
  virtual bool onMouseDown(const MouseEvent&) { return false; }
  virtual bool onMouseUp(const MouseEvent&) { return false; }
  virtual bool onMouseMove(const MouseEvent&) { return false; }
  virtual bool onMouseWheel(const MouseEvent&) { return false; }
  virtual bool onKeyDown(const KeyEvent&) { return false; }
  virtual bool onTextInput(const std::string&) { return false; }
  virtual void onMouseEnter() {}
  virtual void onMouseLeave() {}
  virtual void onFocusChanged(bool) {}
  virtual Cursor cursor() const { return Cursor::Arrow; }
  virtual bool focusable() const { return false; }
  virtual bool capturesTyping() const { return false; }

  // Drag and Drop framework
  virtual bool onDragEnter(const DragDropEvent&) { return false; }
  virtual bool onDragOver(const DragDropEvent&) { return false; }
  virtual void onDragLeave() {}
  virtual bool onDrop(const DragDropEvent&) { return false; }

  // Invalidation Model
  void invalidate(InvalidateFlags flags = InvalidateFlags::Paint);
  void relayout();
  bool isLayoutDirty() const { return hasFlag(m_invalidationFlags, InvalidateFlags::Layout); }
  bool isPaintDirty() const { return hasFlag(m_invalidationFlags, InvalidateFlags::Paint); }
  bool isVolatile() const { return hasFlag(m_invalidationFlags, InvalidateFlags::Volatile); }
  void setVolatile(bool v) {
    if (v) m_invalidationFlags = m_invalidationFlags | InvalidateFlags::Volatile;
    else m_invalidationFlags = static_cast<InvalidateFlags>(static_cast<uint8_t>(m_invalidationFlags) & ~static_cast<uint8_t>(InvalidateFlags::Volatile));
  }
  void clearDirtyFlags() {
    m_invalidationFlags = isVolatile() ? InvalidateFlags::Volatile : InvalidateFlags::None;
  }

protected:
  Vec2 constrain(Vec2 v) const;
  Rect m_rect;
  Vec2 m_desired;
  Widget* m_parent = nullptr;
  std::vector<Ref<Widget>> m_children;
  float m_fixedW = -1, m_fixedH = -1, m_minW = 0, m_minH = 0, m_maxW = 1e9f, m_maxH = 1e9f;
  bool m_visible = true, m_enabled = true, m_hovered = false, m_focused = false;
  std::string m_tooltip, m_accessName, m_accessDesc;
  AccessRole m_accessRole = AccessRole::Generic;
  Ref<Widget> m_tooltipWidget;
  InvalidateFlags m_invalidationFlags = InvalidateFlags::None;

  friend class Application;
};

// Convenience: position a child of desired size `ds` inside `cell` using a slot's alignment.
Rect alignInCell(Rect cell, Vec2 ds, const Slot& s);

} // namespace prism
