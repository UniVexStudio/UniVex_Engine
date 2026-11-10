// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - TransformGizmo: engine-facing 3D manipulator (move / rotate / scale / universal move+rotate / dot handles).
//
// The gizmo owns no scene. Each frame the host gives it a camera and the target's world matrix; the gizmo
// hit-tests, draws through a Canvas and, while dragging, returns a GizmoDelta (relative to the drag start)
// that the host applies to its own objects:
//
//   gizmo.setCamera(cam); gizmo.setTarget(worldMatrix, localAxes);
//   int id = gizmo.hit(mouse);                         // hover / press
//   if (pressed && id >= 0) gizmo.begin(id, mouse);     // dots too: press + drag on a dot scales
//   if (gizmo.dragging()) { GizmoDelta d = gizmo.drag(mouse, shiftHeld); apply(d); }
//   on release: gizmo.end();        gizmo.draw(canvas);
#pragma once
#include "Canvas.h"
#include "Math3D.h"
#include <string>

namespace prism {

struct GizmoCamera {
  Vec3 eye, right{1, 0, 0}, up{0, 1, 0}, fwd{0, 0, -1};   // orthonormal basis, world space
  float focal = 500;                                       // pixels: (viewportHeight / 2) / tan(fovY / 2)
  float cx = 0, cy = 0;                                    // viewport centre in canvas pixels
  float viewH = 600;                                       // viewport height in pixels (sizes the gizmo)
  float nearPlane = 0.1f;
  bool project(Vec3 p, Vec2& out, float* depth = nullptr) const;
  Vec3 rayDir(Vec2 p) const;
  bool rayPlane(Vec2 p, Vec3 p0, Vec3 n, Vec3& out) const;
};

enum class GizmoMode { Move, Rotate, Scale, Universal, Dots };
enum class GizmoOp { None, Move, Rotate, Scale };

// Change since begin(). The host applies it to the values it captured at begin().
struct GizmoDelta {
  GizmoOp op = GizmoOp::None;
  Vec3 translate;              // Move: world-space offset
  int axis = -1;               // Rotate: 0/1/2 = world X/Y/Z; Scale: 0/1/2 or 3 = uniform
  float angleDeg = 0;          // Rotate
  Vec3 scale{1, 1, 1};         // Scale: per-axis multiplier (all equal when uniform)
};

class TransformGizmo {
public:
  // Handle ids returned by hit(): 0-2 move/rotate/scale axis (single-op modes), 3 uniform scale, 4-6 move plane,
  // Universal (move + rotate): 0-2 move axis, 4-6 plane, 10-12 rotate arc. Dots: 30 + dot index (press + drag = scale).
  GizmoMode mode = GizmoMode::Move;
  bool snap = true;
  float moveSnap = 0.5f, rotateSnap = 15.f, scaleSnap = 0.1f;
  float size = 1.f;                                  // on-screen size multiplier
  Color axisColor[3] = {Color(230, 78, 78), Color(110, 200, 70), Color(70, 140, 235)};
  Color hotColor = Color(255, 215, 90);
  int dotCount = 15;                                 // 15 = pivot + 6 faces + 8 corners (unit box); 1 = pivot only

  void setCamera(const GizmoCamera& c) { m_cam = c; }
  // `world` places the unit box [-0.5, 0.5]^3 of the target (pivot at its origin). Local axes follow the object
  // for move / scale; rotation is always about world axes (matches Euler-angle hosts).
  void setTarget(const Mat4& world, bool localAxes = false) { m_world = world; m_local = localAxes; }

  int hit(Vec2 p) const;
  void setHover(int id) { m_hover = id; }
  void draw(Canvas& c) const;

  bool begin(int id, Vec2 p);
  bool dragging() const { return m_drag.active; }
  GizmoOp activeOp() const { return m_drag.active ? m_drag.op : GizmoOp::None; }
  GizmoDelta drag(Vec2 p, bool uniformModifier = false);
  void end() { m_drag = {}; m_label.clear(); }
  const std::string& label() const { return m_label; }   // "X +1.50", "+45.0°", "x1.20" while dragging

  // dot handles (GizmoMode::Dots): press + drag a dot to scale. Face dots scale their own axis, corner dots uniformly.
  static bool isDot(int id) { return id >= 30; }
  static int dotIndex(int id) { return id - 30; }
  static Vec3 dotLocal(int i);                        // 0 pivot (not a handle), 1-6 face centres, 7-14 corners

  Vec3 origin() const;                                // pivot
  float length() const;                               // world length of one gizmo arm at the origin
private:
  struct Drag {
    bool active = false;
    int raw = -1, axis = -1;
    GizmoOp op = GizmoOp::None;
    Vec2 start, tangent, sdir, dotDir;   // dotDir: screen direction pivot -> dot
    Vec3 axisVec, originWorld, hit0;
    float dotLen = 1;            // dot scale: screen distance pivot -> dot at grab time
    float rotSign = 1;
  };
  bool beginDot(int i, Vec2 p);
  void axes(Vec3 out[3]) const;
  void arcFrame(int a, Vec3 org, Vec3& u, Vec3& v) const;
  bool planeQuad(int n, Vec3 org, const Vec3 ax[3], float L, Vec2 out[4], float a, float b) const;
  int hitArcs(Vec2 p, Vec3 org, float L, float R, int idBase) const;
  int hitUniversal(Vec2 p, Vec3 org, const Vec3 ax[3], float L, Vec2 so) const;
  int activeId() const { return m_drag.active ? m_drag.raw : m_hover; }
  // drawing
  void line3(Canvas& c, Vec3 a, Vec3 b, Color col, float w) const;
  void ink(Canvas& c, Vec3 a, Vec3 b, Color col, float w) const;
  void cone3(Canvas& c, Vec3 base, Vec3 dir, float len, float rad, Color col) const;
  void hex3(Canvas& c, Vec3 ctr, Vec3 dir, float len, float rad, Color col) const;
  void drawArcs(Canvas& c, Vec3 org, float L, float R, float half, int active, int idBase) const;
  void drawUniversal(Canvas& c, Vec3 org, const Vec3 ax[3], float L, int active) const;
  void drawDots(Canvas& c) const;

  GizmoCamera m_cam;
  Mat4 m_world = Mat4::identity();
  bool m_local = false;
  int m_hover = -1;
  Drag m_drag;
  std::string m_label;
};

} // namespace prism
