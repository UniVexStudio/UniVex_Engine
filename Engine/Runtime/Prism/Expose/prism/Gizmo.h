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
#include <utility>
#include <vector>

namespace prism {

struct GizmoCamera {
  Vec3 eye, right{1, 0, 0}, up{0, 1, 0}, fwd{0, 0, -1};   // orthonormal basis, world space
  float focal = 500;                                       // pixels: (viewportHeight / 2) / tan(fovY / 2)
  float cx = 0, cy = 0;                                    // viewport centre in canvas pixels
  float viewH = 600;                                       // viewport height in pixels (sizes the gizmo)
  float nearPlane = 0.1f;
  // Orthographic projection: screen offset = world offset * orthoScale (pixels per world unit) along right / up;
  // every ray runs along fwd. `eye` still sets depth (keep it behind what is drawn).
  bool ortho = false;
  float orthoScale = 50;
  bool project(Vec3 p, Vec2& out, float* depth = nullptr) const;
  Vec3 rayOrigin(Vec2 p) const;   // eye, or the point on the view plane under p when orthographic
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

// ------------------------------------------------------------------ navigation gizmo
// The corner orientation widget: one ball per world axis direction, laid out by the camera's basis and drawn back
// to front. Positive axes are full balls with their letter; negative ones are rings. hit() names the facing under
// the pointer (0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z) or -1; the host snaps the view to it, and orbits on a drag.
class NavGizmo {
public:
  Color axisColor[3] = {Color(230, 78, 78), Color(110, 200, 70), Color(70, 140, 235)};
  void setCamera(const GizmoCamera& c) { m_cam = c; }
  void setArea(Rect r) { m_area = r; }          // the square the widget occupies (canvas pixels)
  Rect area() const { return m_area; }
  int hit(Vec2 p) const;
  bool contains(Vec2 p) const;                  // inside the widget's disc
  void setHover(int id) { m_hover = id; }
  void setActive(bool on) { m_active = on; }    // being dragged: the backing disc stays lit
  void draw(Canvas& c) const;
  static Vec3 facingDirection(int id);          // the world axis direction of a ball
private:
  Vec2 ballCentre(int id) const;
  float ballRadius(int id) const;
  GizmoCamera m_cam;
  Rect m_area{0, 0, 96, 96};
  int m_hover = -1;
  bool m_active = false;
};

// ------------------------------------------------------------------ world-space drawing
// Host gizmo geometry (frustums, light rays, volumes, bones ...) projected through a GizmoCamera onto a Canvas.
// Segments are clipped at the near plane; a triangle with a corner behind it is skipped.
void drawWorldLine(Canvas& c, const GizmoCamera& cam, Vec3 a, Vec3 b, Color col, float width = 1.5f);
void drawWorldTriangle(Canvas& c, const GizmoCamera& cam, Vec3 a, Vec3 b, Vec3 cc, Color col);

// ------------------------------------------------------------------ scene depth
// The host's scene depth, so world-space drawing hides behind (or shows faintly through) the scene's geometry.
// `data` is linear view depth - the distance along the camera's fwd axis - one float per texel, row 0 at the top,
// covering `area` in canvas pixels. Texels with nothing drawn hold +infinity (or anything >= farDepth).
struct GizmoDepth {
  const float* data = nullptr;
  int width = 0, height = 0;
  Rect area;
  float bias = 0.004f;          // relative tolerance: a point this fraction behind the surface still counts as on it
  float minTolerance = 0.02f;   // and at least this far (world units)
  float farDepth = 1e30f;
  float zOffset = 0.f;          // subtracted from a tested depth (the drawing camera's eye sits this far behind the host's)
  bool valid() const { return data != nullptr && width > 0 && height > 0 && area.w > 0 && area.h > 0; }
  float sample(Vec2 p) const;   // depth under canvas point p (+inf outside the buffer)
  // True when a point at canvas p and view depth z is in front of the scene there.
  bool visible(Vec2 p, float z) const;
  // True only when the point is clearly in front of the scene (by the tolerance): a surface lying on it, such
  // as a floor on the ground grid, hides it instead of fighting it.
  bool visibleStrict(Vec2 p, float z) const;
};

// A world segment, depth-tested: the parts behind the scene are drawn at `hiddenAlpha` of the colour's alpha
// (0 hides them). `depth` may be null (no test). Long segments are tested every few pixels.
void drawWorldLine(Canvas& c, const GizmoCamera& cam, Vec3 a, Vec3 b, Color col, float width, const GizmoDepth* depth,
                   float hiddenAlpha);
// A world triangle, tested at its centroid.
void drawWorldTriangle(Canvas& c, const GizmoCamera& cam, Vec3 a, Vec3 b, Vec3 cc, Color col, const GizmoDepth* depth,
                       float hiddenAlpha);

// ------------------------------------------------------------------ world grid
// The editor's ground grid drawn as projected lines: three decades of lines (thin / mid / thick) whose spacing
// follows the zoom, the axis lines through the origin, and a fade with distance. Hidden behind the scene's depth.
struct WorldGrid {
  enum class Plane { XZ, XY, ZY };
  Plane plane = Plane::XZ;
  float baseSpacing = 1.f;
  float targetCellPixels = 24.f;   // a thin cell is about this many pixels at the focus
  float lineWidth = 1.25f, axisWidth = 1.6f;
  Color thin = Color(92, 102, 125), mid = Color(140, 153, 179), thick = Color(184, 196, 222);
  float thinIntensity = 0.45f, midIntensity = 0.70f, thickIntensity = 0.95f;
  Color axis[3] = {Color(176, 60, 60), Color(84, 152, 54), Color(54, 107, 179)};
  float fadeStart = 12.f, fadeEnd = 45.f;   // multiples of the camera's height above the plane
  float opacity = 1.f;
};
void drawWorldGrid(Canvas& c, const GizmoCamera& cam, const WorldGrid& grid, const GizmoDepth* depth);

// ------------------------------------------------------------------ object gizmos
// Glyphs for objects with no surface of their own, sized on screen (about `pixels` across wherever they are) or in
// the world (`worldSize` units, like a mesh of that size),
// oriented by the object. Each records the screen segments it drew in `outSegments` (when given) so the host
// can pick it: pickDistance() is the pointer's distance to the nearest one.
struct ObjectGizmoStyle {
  Color color = Color(230, 232, 240);
  float width = 1.5f;
  float pixels = 44.f;
  float worldSize = 0.f;        // > 0: a fixed size in world units instead (the glyph scales with the scene)
  const GizmoDepth* depth = nullptr;
  float hiddenAlpha = 0.22f;
  std::vector<std::pair<Vec2, Vec2>>* outSegments = nullptr;
};
// World size that covers style.pixels at `at` (perspective: by depth; orthographic: by orthoScale).
float gizmoWorldSize(const GizmoCamera& cam, Vec3 at, float pixels);
// A film camera: body, two reels on top and the lens opening along `fwd` by the field of view (orthographic:
// a straight box). With `showFrustum`, the view frustum continues out to `frustumLength` world units.
void drawCameraGizmo(Canvas& c, const GizmoCamera& cam, Vec3 pos, Vec3 fwd, Vec3 up, float fovYDeg, float aspect,
                     bool ortho, bool showFrustum, float frustumLength, const ObjectGizmoStyle& style);
// A directional light: a ring facing the light and three parallel rays with arrow heads along `dir`.
void drawDirectionalLightGizmo(Canvas& c, const GizmoCamera& cam, Vec3 pos, Vec3 dir, const ObjectGizmoStyle& style);
// A spot light: its cone along `dir` with the cone's half angle, `length` world units long, and the rim.
void drawSpotLightGizmo(Canvas& c, const GizmoCamera& cam, Vec3 pos, Vec3 dir, float halfAngleDeg, float length,
                        const ObjectGizmoStyle& style);
float pickDistance(Vec2 p, const std::vector<std::pair<Vec2, Vec2>>& segments);

// ------------------------------------------------------------------ selection outline
// A band `radius` pixels wide just outside every silhouette of a selection mask (one byte of weight per pixel,
// row 0 at the top; 0 = not selected). The colour is scaled by the strongest weight found, like the engine's own
// outline: the active object full strength, the rest of a multi-selection dimmer. Writes straight-alpha ARGB into
// `out` covering `outRect` (pixels of the mask), ready for Canvas::blitBlend; returns false when there is nothing.
bool buildSelectionOutline(const uint8_t* mask, int width, int height, int radius, Color color,
                           std::vector<uint32_t>& out, int outRect[4]);

} // namespace prism
