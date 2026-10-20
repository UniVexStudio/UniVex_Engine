#pragma once
#include "Camera2D.h"
#include "Snapping.h"

namespace univex::viewport {

// Oriented box in world space. pivotOffset is in the box's local (unrotated) space.
struct Box {
    Vec2  center{0.f, 0.f};
    Vec2  size{1.f, 1.f};
    float rotation = 0.f;      // radians, world space
    Vec2  pivotOffset{0.f, 0.f};

    Vec2 localToWorld(Vec2 l) const;
    Vec2 worldToLocal(Vec2 w) const;
    Vec2 pivotWorld() const { return localToWorld(pivotOffset); }
};

struct Handle {
    enum class Kind { None, Body, Scale, Rotate, Pivot };
    Kind kind = Kind::None;
    int  hx = 0, hy = 0;   // scale handle direction in local space, each in {-1,0,1}
    bool operator==(const Handle& o) const { return kind == o.kind && hx == o.hx && hy == o.hy; }
    bool operator!=(const Handle& o) const { return !(*this == o); }
};

// shift: Move = lock to dominant axis, Scale = keep aspect, Rotate = snap angle.
// alt:   Scale = scale from center.
struct DragModifiers { bool shift = false; bool alt = false; };

// Draws the handles and turns mouse drags into a proposed new Box.
// It never touches scene data: the host applies the Box it is given.
class TransformGizmo {
public:
    void setBox(const Box& b) { m_box = b; m_has = true; }
    void clear() { m_has = false; m_dragging = false; }
    bool hasBox() const { return m_has; }
    const Box& box() const { return m_box; }
    void setPivotEditing(bool v) { m_pivotEditing = v; }
    bool pivotEditing() const { return m_pivotEditing; }

    Handle hitTest(Vec2 screen, const Camera2D& cam) const;
    void   beginDrag(Handle h, Vec2 screen, const Camera2D& cam);
    Box    updateDrag(Vec2 screen, const Camera2D& cam, const SnapSettings& snap,
                      const Guides& guides, DragModifiers mods);
    void   endDrag() { m_dragging = false; }
    bool   dragging() const { return m_dragging; }
    Handle dragHandle() const { return m_handle; }

    void draw(IViewportRenderer& r, const Camera2D& cam, Handle hover) const;

private:
    bool  edgeHandlesVisible(const Camera2D& cam) const;
    Vec2  rotateHandleWorld(const Camera2D& cam) const;

    Box    m_box;
    bool   m_has = false;
    bool   m_pivotEditing = false;
    bool   m_dragging = false;
    Handle m_handle;
    Box    m_start;
    Vec2   m_startMouseWorld;
    float  m_startAngle = 0.f;
};

// Fill an oriented box (e.g. a rotated sprite placeholder).
void fillBox(IViewportRenderer& r, const Camera2D& cam, const Box& b, Color c);

} // namespace univex::viewport
