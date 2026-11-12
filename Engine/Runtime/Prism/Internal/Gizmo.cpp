// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Gizmo.h"
#include "prism/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <span>
#include <vector>

namespace prism {
namespace {
constexpr float kPi = 3.14159265f, kHalfPi = 1.57079633f;
constexpr float kEdgeOn = 0.18f;
constexpr float kEndOn = 0.97f;   // |axis . view| above this: the axis points at the camera (a dot on screen)   // |axis . view| below this: the arc plane is nearly edge-on
constexpr float kArcR = 0.64f, kArcHalf = 0.045f, kRotR = 0.85f, kRotHalf = 0.075f;

float sgn(float v) { return v >= 0 ? 1.f : -1.f; }
float distSeg(Vec2 p, Vec2 a, Vec2 b) {
  Vec2 ab = b - a, ap = p - a;
  float l2 = ab.x * ab.x + ab.y * ab.y;
  float t = l2 > 1e-6f ? std::clamp((ap.x * ab.x + ap.y * ab.y) / l2, 0.f, 1.f) : 0.f;
  return (p - (a + ab * t)).length();
}
bool inQuad(const Vec2 q[4], Vec2 p) {
  float s = 0;
  for (int i = 0; i < 4; i++) {
    Vec2 a = q[i], b = q[(i + 1) % 4];
    float cr = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    if (std::fabs(cr) < 1e-6f) continue;
    if (s == 0) s = cr > 0 ? 1.f : -1.f;
    else if ((cr > 0 ? 1.f : -1.f) != s) return false;
  }
  return s != 0;      // an edge-on (zero-area) quad contains nothing
}
Vec3 anyPerp(Vec3 d) {
  Vec3 e = std::fabs(d.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  return d.cross(e).normalized();
}
Vec3 worldAxis(int i) { return {i == 0 ? 1.f : 0.f, i == 1 ? 1.f : 0.f, i == 2 ? 1.f : 0.f}; }
} // namespace

// ------------------------------------------------------------------ camera
bool GizmoCamera::project(Vec3 p, Vec2& out, float* depth) const {
  Vec3 d = p - eye;
  float z = d.dot(fwd);
  if (z < nearPlane) return false;
  if (ortho) out = {cx + d.dot(right) * orthoScale, cy - d.dot(up) * orthoScale};
  else out = {cx + d.dot(right) / z * focal, cy - d.dot(up) / z * focal};
  if (depth) *depth = z;
  return true;
}
Vec3 GizmoCamera::rayDir(Vec2 p) const {
  if (ortho) return fwd;
  return (fwd + right * ((p.x - cx) / focal) - up * ((p.y - cy) / focal)).normalized();
}
Vec3 GizmoCamera::rayOrigin(Vec2 p) const {
  if (!ortho) return eye;
  const float s = orthoScale > 0 ? orthoScale : 1.f;
  return eye + right * ((p.x - cx) / s) - up * ((p.y - cy) / s);
}
bool GizmoCamera::rayPlane(Vec2 p, Vec3 p0, Vec3 n, Vec3& out) const {
  Vec3 d = rayDir(p);
  float den = d.dot(n);
  if (std::fabs(den) < 1e-4f) return false;
  const Vec3 o = rayOrigin(p);
  float t = (p0 - o).dot(n) / den;
  if (t < 0) return false;
  out = o + d * t;
  return true;
}

// ------------------------------------------------------------------ frame
Vec3 TransformGizmo::dotLocal(int i) {
  if (i <= 0) return {0, 0, 0};
  if (i <= 6) { int a = (i - 1) / 2; float sg = (i - 1) % 2 ? -0.5f : 0.5f; return {a == 0 ? sg : 0.f, a == 1 ? sg : 0.f, a == 2 ? sg : 0.f}; }
  int k = i - 7;
  return {(k & 1) ? .5f : -.5f, (k & 2) ? .5f : -.5f, (k & 4) ? .5f : -.5f};
}

Vec3 TransformGizmo::origin() const {
  return m_world.point(Vec3{0, 0, 0});
}

float TransformGizmo::length() const {
  float z = std::max(0.5f, (origin() - m_cam.eye).dot(m_cam.fwd));
  float k = mode == GizmoMode::Dots ? 0.8f : mode == GizmoMode::Universal ? 1.3f : 1.f;
  if (m_cam.ortho) return 0.162f * m_cam.viewH / std::max(1e-3f, m_cam.orthoScale) * k * size;
  return z * 0.162f * m_cam.viewH / std::max(1.f, m_cam.focal) * k * size;   // ~16% of the viewport height on screen
}

void TransformGizmo::axes(Vec3 out[3]) const {
  for (int i = 0; i < 3; i++) out[i] = worldAxis(i);
  if (m_local && mode != GizmoMode::Rotate)
    for (int i = 0; i < 3; i++) out[i] = m_world.dir(out[i]).normalized();
}

void TransformGizmo::arcFrame(int a, Vec3 org, Vec3& u, Vec3& v) const {
  Vec3 toEye = m_cam.eye - org;
  Vec3 u0 = worldAxis((a + 1) % 3), v0 = worldAxis((a + 2) % 3);
  u = u0 * sgn(u0.dot(toEye));
  v = v0 * sgn(v0.dot(toEye));
}

bool TransformGizmo::planeQuad(int n, Vec3 org, const Vec3 ax[3], float L, Vec2 out[4], float qa, float qb) const {
  Vec3 u = ax[(n + 1) % 3], v = ax[(n + 2) % 3];
  Vec3 toEye = m_cam.eye - org;
  float su = sgn(u.dot(toEye)), sv = sgn(v.dot(toEye));
  float a = qa * L, b = qb * L;
  Vec3 p[4] = {org + u * (su * a) + v * (sv * a), org + u * (su * b) + v * (sv * a),
               org + u * (su * b) + v * (sv * b), org + u * (su * a) + v * (sv * b)};
  for (int i = 0; i < 4; i++) if (!m_cam.project(p[i], out[i])) return false;
  return true;
}

// ------------------------------------------------------------------ hit testing
int TransformGizmo::hitArcs(Vec2 p, Vec3 org, float L, float R, int idBase) const {
  int best = -1;
  float bd = 7.f;
  Vec3 view = (m_cam.eye - org).normalized();
  for (int a = 0; a < 3; a++) {
    if (std::fabs(worldAxis(a).dot(view)) < kEdgeOn) continue;   // an arc seen edge-on is a line: it would steal axis clicks
    Vec3 u, v;
    arcFrame(a, org, u, v);
    Vec2 prev; bool have = false;
    for (int i = 0; i <= 20; i++) {
      float ang = kHalfPi * float(i) / 20.f;
      Vec2 s;
      if (!m_cam.project(org + (u * std::cos(ang) + v * std::sin(ang)) * (L * R), s)) { have = false; continue; }
      if (have) { float d = distSeg(p, prev, s); if (d < bd) { bd = d; best = idBase + a; } }
      prev = s; have = true;
    }
  }
  return best;
}

int TransformGizmo::hitUniversal(Vec2 p, Vec3 org, const Vec3 ax[3], float L, Vec2 so) const {
  (void)so;
  int best = -1;
  float bd = 8.f;
  best = hitArcs(p, org, L, kArcR, 10);
  if (best >= 0) return best;
  for (int n = 0; n < 3; n++) {
    Vec2 q[4];
    if (planeQuad(n, org, ax, L, q, 0.15f, 0.33f) && inQuad(q, p)) return 4 + n;
  }
  bd = 8.f;
  Vec3 view = (m_cam.eye - org).normalized();
  for (int a = 0; a < 3; a++) {
    if (std::fabs(ax[a].dot(view)) > kEndOn) continue;                // end-on axis: hidden, not clickable
    Vec2 se, sb;
    if (!m_cam.project(org + ax[a] * L, se) || !m_cam.project(org + ax[a] * (L * 0.16f), sb)) continue;
    float d = distSeg(p, sb, se);
    if (d < bd) { bd = d; best = a; }
  }
  return best;
}

int TransformGizmo::hit(Vec2 p) const {
  if (mode != GizmoMode::Dots) {
    Vec3 org = origin(), ax[3];
    axes(ax);
    float L = length();
    Vec2 so;
    if (!m_cam.project(org, so)) return -1;
    if (mode == GizmoMode::Universal) return hitUniversal(p, org, ax, L, so);
    if (mode == GizmoMode::Rotate) return hitArcs(p, org, L, kRotR, 0);
    if (mode == GizmoMode::Scale && (p - so).length() < 9.f) return 3;
    const bool mv = mode != GizmoMode::Scale;
    int best = -1;
    float bd = 8.f;
    Vec3 view = (m_cam.eye - org).normalized();
    for (int a = 0; a < 3; a++) {
      if (std::fabs(ax[a].dot(view)) > kEndOn) continue;
      Vec2 se;
      if (!m_cam.project(org + ax[a] * (L * (mv ? 0.86f : 0.92f)), se)) continue;
      float d = distSeg(p, so, se);
      if (d < bd) { bd = d; best = a; }
    }
    if (best < 0 && mv)
      for (int n = 0; n < 3; n++) {
        Vec2 q[4];
        if (planeQuad(n, org, ax, L, q, 0.30f, 0.58f) && inQuad(q, p)) return 4 + n;
      }
    return best;
  }
  int bestDot = -1;
  float bd = 9.f;
  for (int i = 1; i < dotCount; i++) {          // 0 is the pivot: a marker, not a handle
    Vec2 s;
    if (m_cam.project(m_world.point(dotLocal(i)), s) && (s - p).length() < bd) { bd = (s - p).length(); bestDot = i; }
  }
  return bestDot >= 0 ? 30 + bestDot : -1;
}

// ------------------------------------------------------------------ dragging
bool TransformGizmo::begin(int id, Vec2 pos) {
  if (id < 0) return false;
  if (isDot(id)) return beginDot(dotIndex(id), pos);
  m_drag = {};
  m_drag.active = true;
  m_drag.raw = id;
  int axis = id;
  GizmoOp op = mode == GizmoMode::Rotate ? GizmoOp::Rotate : mode == GizmoMode::Scale ? GizmoOp::Scale : GizmoOp::Move;
  if (mode == GizmoMode::Universal) {
    if (id >= 10 && id <= 12) { op = GizmoOp::Rotate; axis = id - 10; }
    else if ((id >= 0 && id <= 2) || (id >= 4 && id <= 6)) op = GizmoOp::Move;
    else { m_drag = {}; return false; }
  }
  m_drag.axis = axis;
  m_drag.op = op;
  m_drag.start = pos;
  Vec3 org = origin(), ax[3];
  axes(ax);
  if (op == GizmoOp::Rotate) for (int i = 0; i < 3; i++) ax[i] = worldAxis(i);
  m_drag.originWorld = org;
  float L = length();
  Vec2 so;
  m_cam.project(org, so);
  if (axis >= 4 && axis <= 6) {
    m_drag.axisVec = ax[axis - 4];
    if (!m_cam.rayPlane(pos, org, ax[axis - 4], m_drag.hit0)) m_drag.hit0 = org;
  }
  if (axis < 3) {
    m_drag.axisVec = ax[axis];
    Vec2 se;
    if (m_cam.project(org + ax[axis] * L, se)) m_drag.sdir = (se - so) / L;
    m_drag.rotSign = ax[axis].dot(m_cam.eye - org) > 0 ? 1.f : -1.f;
    Vec2 r = pos - so, tg{r.y, -r.x};
    float l = tg.length();
    m_drag.tangent = l > 1e-3f ? tg / l : Vec2{1, 0};
    if (op == GizmoOp::Rotate) {                    // follow the arc's own screen tangent at the grab point
      Vec3 u, v;
      arcFrame(axis, org, u, v);
      float R = L * (mode == GizmoMode::Rotate ? kRotR : kArcR), bestD = 1e9f, bestA = 0;
      for (int i = 0; i <= 24; i++) {
        float a = kHalfPi * float(i) / 24.f; Vec2 s2;
        if (m_cam.project(org + (u * std::cos(a) + v * std::sin(a)) * R, s2)) { float dd = (s2 - pos).length(); if (dd < bestD) { bestD = dd; bestA = a; } }
      }
      Vec2 s0, s1;
      float a0 = std::max(0.f, bestA - 0.06f), a1 = std::min(kHalfPi, bestA + 0.06f);
      if (m_cam.project(org + (u * std::cos(a0) + v * std::sin(a0)) * R, s0) && m_cam.project(org + (u * std::cos(a1) + v * std::sin(a1)) * R, s1)) {
        Vec2 ta = s1 - s0; float tl = ta.length();
        if (tl > 1e-4f) {
          ta = ta / tl;
          float agree = ta.x * m_drag.tangent.x + ta.y * m_drag.tangent.y;
          m_drag.rotSign *= agree >= 0 ? 1.f : -1.f;
          m_drag.tangent = ta;
        }
      }
    }
  }
  return true;
}

bool TransformGizmo::beginDot(int i, Vec2 pos) {
  if (i < 1 || i >= dotCount || i > 14) return false;
  Vec2 sc, sd;
  if (!m_cam.project(origin(), sc) || !m_cam.project(m_world.point(dotLocal(i)), sd)) return false;
  m_drag = {};
  m_drag.active = true;
  m_drag.raw = 30 + i;
  m_drag.op = GizmoOp::Scale;
  m_drag.axis = i <= 6 ? (i - 1) / 2 : 3;         // faces: their own axis, corners: uniform
  m_drag.start = pos;
  Vec2 v = sd - sc;
  m_drag.dotLen = std::max(24.f, v.length());      // short projections would make the drag twitchy
  m_drag.dotDir = v.length() > 1e-3f ? v / v.length() : Vec2{1, 0};
  return true;
}

GizmoDelta TransformGizmo::drag(Vec2 pos, bool uniform) {
  GizmoDelta out;
  if (!m_drag.active) return out;
  out.op = m_drag.op;
  out.axis = m_drag.axis;
  Vec2 d = pos - m_drag.start;
  float proj = 0;
  float sl2 = m_drag.sdir.x * m_drag.sdir.x + m_drag.sdir.y * m_drag.sdir.y;
  if (sl2 > 1e-6f) proj = (d.x * m_drag.sdir.x + d.y * m_drag.sdir.y) / sl2;
  char lb[64];
  m_label.clear();
  if (m_drag.op == GizmoOp::Move && m_drag.axis >= 4) {
    int n = m_drag.axis - 4;
    Vec3 ax[3];
    axes(ax);
    Vec3 hitp;
    if (m_cam.rayPlane(pos, m_drag.originWorld, ax[n], hitp)) {
      Vec3 dl = hitp - m_drag.hit0;
      float du = dl.dot(ax[(n + 1) % 3]), dv = dl.dot(ax[(n + 2) % 3]);
      if (snap) { du = std::round(du / moveSnap) * moveSnap; dv = std::round(dv / moveSnap) * moveSnap; }
      out.translate = ax[(n + 1) % 3] * du + ax[(n + 2) % 3] * dv;
      std::snprintf(lb, sizeof lb, "%c %+.2f   %c %+.2f", "XYZ"[(n + 1) % 3], double(du), "XYZ"[(n + 2) % 3], double(dv));
      m_label = lb;
    }
  } else if (m_drag.op == GizmoOp::Move) {
    float v = snap ? std::round(proj / moveSnap) * moveSnap : proj;
    out.translate = m_drag.axisVec * v;
    std::snprintf(lb, sizeof lb, "%c %+.2f", "XYZ"[m_drag.axis], double(v));
    m_label = lb;
  } else if (m_drag.op == GizmoOp::Rotate) {
    float ang = (d.x * m_drag.tangent.x + d.y * m_drag.tangent.y) * 0.5f * m_drag.rotSign;
    if (snap) ang = std::round(ang / rotateSnap) * rotateSnap;
    out.angleDeg = ang;
    std::snprintf(lb, sizeof lb, "%+.1f\xC2\xB0", double(ang));
    m_label = lb;
  } else if (m_drag.op == GizmoOp::Scale && m_drag.raw >= 30) {      // dot: dragging outward grows, inward shrinks
    float f = std::max(0.05f, 1.f + (d.x * m_drag.dotDir.x + d.y * m_drag.dotDir.y) / m_drag.dotLen);
    if (snap) f = std::max(scaleSnap, std::round(f / scaleSnap) * scaleSnap);
    if (m_drag.axis == 3 || uniform) { out.scale = {f, f, f}; out.axis = 3; }
    else out.scale[m_drag.axis] = f;
    std::snprintf(lb, sizeof lb, "x%.2f", double(f));
    m_label = lb;
  } else if (m_drag.op == GizmoOp::Scale) {
    float amount = m_drag.axis == 3 ? (d.x - d.y) * 0.01f : proj * 0.5f;
    float f = std::max(0.05f, 1.f + amount);
    if (snap) f = std::max(scaleSnap, std::round(f / scaleSnap) * scaleSnap);
    if (m_drag.axis == 3 || uniform) { out.scale = {f, f, f}; out.axis = 3; }
    else out.scale[m_drag.axis] = f;
    std::snprintf(lb, sizeof lb, "x%.2f", double(f));
    m_label = lb;
  }
  return out;
}

// ------------------------------------------------------------------ drawing primitives
void TransformGizmo::line3(Canvas& c, Vec3 a, Vec3 b, Color col, float w) const {
  float n = m_cam.nearPlane * 1.01f;
  float za = (a - m_cam.eye).dot(m_cam.fwd), zb = (b - m_cam.eye).dot(m_cam.fwd);
  if (za < n && zb < n) return;
  if (za < n) a = a + (b - a) * ((n - za) / (zb - za));
  else if (zb < n) b = b + (a - b) * ((n - zb) / (za - zb));
  Vec2 pa, pb;
  if (m_cam.project(a, pa) && m_cam.project(b, pb)) c.line(pa, pb, col, w);
}

void TransformGizmo::ink(Canvas& c, Vec3 a, Vec3 b, Color col, float w) const {
  line3(c, a, b, Color(0, 0, 0, uint8_t(120 * col.a / 255)), w + 1.8f);
  line3(c, a, b, col, w);
}

void TransformGizmo::cone3(Canvas& c, Vec3 base, Vec3 dir, float len, float rad, Color col) const {
  Vec3 u = anyPerp(dir), v = dir.cross(u);
  Vec3 apex = base + dir * len;
  const int N = 14;
  Vec3 ring[N];
  for (int i = 0; i < N; i++) {
    float a = 2.f * kPi * float(i) / float(N);
    ring[i] = base + (u * std::cos(a) + v * std::sin(a)) * rad;
  }
  Vec3 Lt = (m_cam.right * -0.4f + m_cam.up * 0.7f - m_cam.fwd * 0.6f).normalized();
  for (int i = 0; i < N; i++) {
    Vec3 p1 = ring[i], p2 = ring[(i + 1) % N];
    Vec3 n = (p1 - apex).cross(p2 - apex).normalized();
    Vec3 cen = (p1 + p2 + apex) * (1.f / 3.f);
    if (n.dot(cen - (base + dir * (len * 0.5f))) < 0) n = n * -1.f;
    if (n.dot(m_cam.eye - cen) <= 0) continue;
    Vec2 a, b, d;
    if (!m_cam.project(p1, a) || !m_cam.project(p2, b) || !m_cam.project(apex, d)) continue;
    float k = 0.62f + 0.38f * std::max(0.f, n.dot(Lt));
    Vec2 tri[3] = {a, b, d};
    c.fillPolygon(std::span<const Vec2>(tri, 3), Color(uint8_t(col.r * k), uint8_t(col.g * k), uint8_t(col.b * k), col.a), false);
  }
  if ((m_cam.eye - base).dot(dir) < 0) {
    std::vector<Vec2> cap;
    for (int i = 0; i < N; i++) { Vec2 p; if (m_cam.project(ring[i], p)) cap.push_back(p); }
    if (cap.size() == size_t(N)) c.fillPolygon(cap, col.darken(0.35f), false);
  }
}

void TransformGizmo::hex3(Canvas& c, Vec3 ctr, Vec3 dir, float len, float rad, Color col) const {
  Vec3 u = anyPerp(dir), v = dir.cross(u);
  Vec3 Lt = (m_cam.right * -0.4f + m_cam.up * 0.7f - m_cam.fwd * 0.6f).normalized();
  Vec3 b = ctr - dir * (len * 0.5f), f = ctr + dir * (len * 0.5f);
  Vec3 rb[6], rf[6];
  for (int i = 0; i < 6; i++) {
    float a = float(i) * 1.0471976f + 0.5235988f;
    Vec3 o = (u * std::cos(a) + v * std::sin(a)) * rad;
    rb[i] = b + o; rf[i] = f + o;
  }
  auto shade = [&](Vec3 n, float base) { float k = base + (1 - base) * std::max(0.f, n.dot(Lt)); return Color(uint8_t(col.r * k), uint8_t(col.g * k), uint8_t(col.b * k), col.a); };
  for (int i = 0; i < 6; i++) {
    int j = (i + 1) % 6;
    Vec3 n = ((rb[i] + rb[j]) * 0.5f - b).normalized();
    if (n.dot(m_cam.eye - (rb[i] + rf[j]) * 0.5f) <= 0) continue;
    Vec2 q[4];
    if (!m_cam.project(rb[i], q[0]) || !m_cam.project(rb[j], q[1]) || !m_cam.project(rf[j], q[2]) || !m_cam.project(rf[i], q[3])) continue;
    c.fillPolygon(std::span<const Vec2>(q, 4), shade(n, 0.55f), false);
  }
  for (int e = 0; e < 2; e++) {
    Vec3 n = e ? dir : dir * -1.f;
    const Vec3* ring = e ? rf : rb;
    if (n.dot(m_cam.eye - (e ? f : b)) <= 0) continue;
    Vec2 q[6]; bool ok = true;
    for (int i = 0; i < 6; i++) ok = ok && m_cam.project(ring[i], q[i]);
    if (!ok) continue;
    c.fillPolygon(std::span<const Vec2>(q, 6), shade(n, 0.7f).lighten(0.08f), true);
    for (int i = 0; i < 6; i++) c.line(q[i], q[(i + 1) % 6], Color(0, 0, 0, uint8_t(110 * col.a / 255)), 1.f);
  }
}

// Three quarter-circle bands that meet at the axis tips: a curved triangle on the camera-facing octant.
void TransformGizmo::drawArcs(Canvas& c, Vec3 org, float L, float R, float half, int active, int idBase) const {
  const bool dragging = m_drag.active;
  int order[3] = {0, 1, 2};
  auto depth = [&](int a) { Vec3 u, v; arcFrame(a, org, u, v); Vec3 mid = org + (u + v).normalized() * (L * R); return (mid - m_cam.eye).dot(m_cam.fwd); };
  std::sort(order, order + 3, [&](int x, int y) { return depth(x) > depth(y); });
  for (int a : order) {
    Vec3 u, v;
    arcFrame(a, org, u, v);
    bool on = active == idBase + a;
    Color col = on ? hotColor : axisColor[a];
    if (dragging && !on) col = col.alpha(0.22f);
    float facing = std::fabs(worldAxis(a).dot((m_cam.eye - org).normalized()));
    if (!on && facing < kEdgeOn) col = col.alpha(0.25f + 0.75f * facing / kEdgeOn);   // edge-on arcs fade (and are not clickable)
    const int N = 26;
    float hw = half * (on ? 1.3f : 1.f);
    float r0 = L * (R - hw), r1 = L * (R + hw);
    Vec2 pi0[N + 1], pi1[N + 1];
    bool ok = true;
    for (int i = 0; i <= N && ok; i++) {
      float ang = kHalfPi * float(i) / float(N);
      Vec3 d = u * std::cos(ang) + v * std::sin(ang);
      ok = m_cam.project(org + d * r0, pi0[i]) && m_cam.project(org + d * r1, pi1[i]);
    }
    if (!ok) continue;
    for (int i = 0; i < N; i++)        // soft shadow so the band separates from bright meshes
      c.line((pi1[i] + pi0[i]) * 0.5f, (pi1[i + 1] + pi0[i + 1]) * 0.5f, Color(0, 0, 0, uint8_t(dragging && !on ? 20 : 70)), (pi1[i] - pi0[i]).length() + 3.f);
    for (int i = 0; i < N; i++) {
      float k = 0.82f + 0.18f * std::sin(kPi * (float(i) + 0.5f) / float(N));
      Color shade(uint8_t(std::min(255.f, col.r * k)), uint8_t(std::min(255.f, col.g * k)), uint8_t(std::min(255.f, col.b * k)), col.a);
      Vec2 quad[4] = {pi0[i], pi0[i + 1], pi1[i + 1], pi1[i]};
      c.fillPolygon(std::span<const Vec2>(quad, 4), shade, false);
    }
    for (int i = 0; i < N; i++) {
      c.line(pi1[i], pi1[i + 1], col.darken(0.38f), 1.3f);
      c.line(pi0[i], pi0[i + 1], col.lighten(on ? 0.55f : 0.30f), 1.3f);
    }
    c.line(pi0[0], pi1[0], col.darken(0.45f), 1.2f);
    c.line(pi0[N], pi1[N], col.darken(0.45f), 1.2f);
    if (on && dragging)
      for (int i = 0; i <= 6; i++) {
        float ang = kHalfPi * float(i) / 6.f;
        Vec3 d = u * std::cos(ang) + v * std::sin(ang);
        ink(c, org + d * (L * (R + hw)), org + d * (L * (R + hw + 0.07f)), hotColor.alpha(0.85f), 1.f);
      }
  }
}

void TransformGizmo::drawUniversal(Canvas& c, Vec3 org, const Vec3 ax[3], float L, int active) const {
  const bool dragging = m_drag.active;
  auto tone = [&](int id, Color base) {
    bool on = id == active;
    Color col = on ? hotColor : base;
    if (dragging && !on) col = col.alpha(0.22f);
    return col;
  };
  for (int n = 0; n < 3; n++) {
    Vec2 q[4];
    if (!planeQuad(n, org, ax, L, q, 0.15f, 0.33f)) continue;
    bool on = active == 4 + n;
    Color col = tone(4 + n, axisColor[n]);
    c.fillPolygon(std::span<const Vec2>(q, 4), col.alpha(on ? 0.55f : 0.24f), false);
    for (int i = 0; i < 4; i++) c.line(q[i], q[(i + 1) % 4], col.alpha(0.95f), on ? 1.8f : 1.2f);
  }
  drawArcs(c, org, L, kArcR, kArcHalf, active, 10);
  int order[3] = {0, 1, 2};
  std::sort(order, order + 3, [&](int x, int y) { return (org + ax[x] * L - m_cam.eye).dot(m_cam.fwd) > (org + ax[y] * L - m_cam.eye).dot(m_cam.fwd); });
  Vec3 view = (m_cam.eye - org).normalized();
  for (int a : order) {
    if (std::fabs(ax[a].dot(view)) > kEndOn) continue;               // pointing at the camera: draw nothing (it would be a blob)
    bool on = active == a;
    Color col = tone(a, axisColor[a]);
    ink(c, org + ax[a] * (L * 0.16f), org + ax[a] * (L * 0.76f), col, on ? 3.6f : 2.6f);
    cone3(c, org + ax[a] * (L * 0.76f), ax[a], L * 0.24f, L * 0.052f, col);
  }
  Vec2 so;
  if (!m_cam.project(org, so)) return;
  c.fillCircle(so, 4.4f, Color(0, 0, 0, dragging ? 60 : 150));      // plain pivot: the universal gizmo is move + rotate only
  c.fillCircle(so, 3.3f, dragging ? Color(236, 238, 242, 90) : Color(236, 238, 242));
}

// Dots on the target's outline; far-side dots are faint. Pressing one scales: a line shows the pivot -> dot reach.
void TransformGizmo::drawDots(Canvas& c) const {
  struct D { Vec2 s; float depth; bool front; int i; };
  std::vector<D> ds;
  Vec2 sc; const bool haveC = m_cam.project(origin(), sc);
  for (int i = 0; i < dotCount; i++) {
    Vec3 w = m_world.point(dotLocal(i));
    Vec2 s; float dep;
    if (!m_cam.project(w, s, &dep)) continue;
    bool front = i == 0 || m_world.dir(dotLocal(i)).dot(m_cam.eye - w) > 0;
    ds.push_back({s, dep, front, i});
  }
  std::sort(ds.begin(), ds.end(), [](const D& a, const D& b) { return a.front != b.front ? !a.front : a.depth > b.depth; });
  const int hoverId = m_drag.active ? m_drag.raw : m_hover;
  for (auto& d : ds) {
    bool hot = hoverId == 30 + d.i, act = m_drag.active && hot;
    if (act && haveC) { c.line(sc, d.s, Color(0, 0, 0, 120), 3.2f); c.line(sc, d.s, hotColor.alpha(0.9f), 1.6f); }
    float r = d.i == 0 ? 3.4f : d.i <= 6 ? 4.6f : 3.8f;
    if (hot) r += 1.4f;
    float a = d.front || hot ? 1.f : 0.32f;
    Color fill = act ? hotColor : hot ? Color(255, 244, 205) : Color(240, 242, 246);
    if (act) c.fillCircle(d.s, r + 6.f, hotColor.alpha(0.18f));
    c.fillCircle(d.s, r + 1.6f, Color(10, 12, 16, uint8_t(170 * a)));
    if (d.i == 0) { c.strokeCircle(d.s, r - 0.6f, fill.alpha(0.7f), 1.4f); continue; }
    if (d.i <= 6) {                                   // face dots: scale one axis -> rounded square, corners -> circle
      c.fillRoundRect({d.s.x - r, d.s.y - r, r * 2, r * 2}, 1.6f, fill.alpha(a));
    } else c.fillCircle(d.s, r, fill.alpha(a));
    if (hot && !act) c.strokeCircle(d.s, r + 4.f, hotColor.alpha(0.7f), 1.2f);
  }
}

void TransformGizmo::draw(Canvas& c) const {
  const bool dots = mode == GizmoMode::Dots;
  if (dots) { drawDots(c); return; }
  Vec3 org = origin(), ax[3];
  axes(ax);
  float L = length();
  Vec2 so;
  if (!m_cam.project(org, so)) return;
  int active = activeId();
  if (mode == GizmoMode::Universal) {
    drawUniversal(c, org, ax, L, active);
  } else if (mode == GizmoMode::Rotate) {
    Vec2 se;
    float Rs = m_cam.project(org + m_cam.right * (L * kRotR), se) ? (se - so).length() : 60.f;
    c.fillCircle(so, Rs * 0.98f, Color(255, 255, 255, 6));
    drawArcs(c, org, L, kRotR, kRotHalf, active, 0);
    c.fillCircle(so, 4.2f, Color(0, 0, 0, 140));
    c.fillCircle(so, 3.2f, Color(236, 238, 242));
  } else {
    const bool mv = mode != GizmoMode::Scale;
    Vec3 view = (m_cam.eye - org).normalized();
    for (int a = 0; a < 3; a++) {
      if (std::fabs(ax[a].dot(view)) > kEndOn) continue;
      bool on = a == active || (active == 3 && !mv);
      Color col = on ? hotColor : axisColor[a];
      if (mv) {
        ink(c, org + ax[a] * (L * 0.16f), org + ax[a] * (L * 0.80f), col, on ? 3.6f : 2.6f);
        cone3(c, org + ax[a] * (L * 0.80f), ax[a], L * 0.24f, L * 0.07f, col);
      } else {
        ink(c, org + ax[a] * (L * 0.10f), org + ax[a] * (L * 0.80f), col, on ? 3.6f : 2.6f);
        hex3(c, org + ax[a] * (L * 0.88f), ax[a], L * 0.14f, L * (on ? 0.085f : 0.075f), col);
        line3(c, org + ax[a] * (L * 0.5f), org + ax[(a + 1) % 3] * (L * 0.5f), Color(255, 255, 255, active == 3 ? 150 : 50), 1.f);
      }
    }
    if (mv)
      for (int n = 0; n < 3; n++) {
        Vec2 q[4];
        if (!planeQuad(n, org, ax, L, q, dots ? 0.16f : 0.30f, dots ? 0.36f : 0.58f)) continue;
        bool on = active == 4 + n;
        Color col = on ? hotColor : axisColor[n];
        c.fillPolygon(std::span<const Vec2>(q, 4), col.alpha(on ? 0.55f : 0.26f), false);
        for (int i = 0; i < 4; i++) c.line(q[i], q[(i + 1) % 4], col.alpha(0.95f), on ? 1.8f : 1.2f);
      }
    if (!mv) {
      bool core = active == 3;
      c.fillRoundRect({so.x - 6, so.y - 6, 12, 12}, 2, core ? hotColor : Color(236, 238, 242));
      c.strokeRoundRect({so.x - 6, so.y - 6, 12, 12}, 2, Color(0, 0, 0, 160), 1.f);
    } else if (!dots) {
      c.fillCircle(so, 4.2f, Color(0, 0, 0, 140));
      c.fillCircle(so, 3.2f, Color(236, 238, 242));
    }
  }
  if (m_drag.active && !m_label.empty()) {
    float w = 12.f + 7.f * float(m_label.size());
    Rect pill{so.x - w * 0.5f, so.y - 58.f, w, 20.f};
    c.fillRoundRect(pill, 4, Color(14, 16, 20, 230));
    c.strokeRoundRect(pill, 4, hotColor.alpha(0.8f), 1.f);
    c.textIn(*theme().small, pill, m_label, Color(255, 246, 214), HAlign::Center);
  }
}

// ------------------------------------------------------------------ navigation gizmo
Vec3 NavGizmo::facingDirection(int id) {
  const float s = (id % 2 == 0) ? 1.f : -1.f;
  const int a = id / 2;
  return {a == 0 ? s : 0.f, a == 1 ? s : 0.f, a == 2 ? s : 0.f};
}

Vec2 NavGizmo::ballCentre(int id) const {
  const Vec3 d = facingDirection(id);
  const float r = std::min(m_area.w, m_area.h) * 0.5f;
  const Vec2 c = m_area.center();
  return {c.x + d.dot(m_cam.right) * r * 0.70f, c.y - d.dot(m_cam.up) * r * 0.70f};
}

float NavGizmo::ballRadius(int id) const {
  const float r = std::min(m_area.w, m_area.h) * 0.5f;
  return (id % 2 == 0 ? 0.20f : 0.15f) * r;
}

bool NavGizmo::contains(Vec2 p) const {
  const Vec2 c = m_area.center();
  const float r = std::min(m_area.w, m_area.h) * 0.5f;
  const float dx = p.x - c.x, dy = p.y - c.y;
  return dx * dx + dy * dy <= r * r;
}

int NavGizmo::hit(Vec2 p) const {
  // Front-most first: the ball nearest the camera wins where two overlap.
  int best = -1;
  float bestDepth = 1e9f;
  for (int id = 0; id < 6; id++) {
    const Vec2 b = ballCentre(id);
    const float rr = ballRadius(id) + 2.f;
    const float dx = p.x - b.x, dy = p.y - b.y;
    if (dx * dx + dy * dy > rr * rr) continue;
    const float depth = facingDirection(id).dot(m_cam.fwd);
    if (depth < bestDepth) { bestDepth = depth; best = id; }
  }
  return best;
}

void NavGizmo::draw(Canvas& c) const {
  const Vec2 centre = m_area.center();
  const float r = std::min(m_area.w, m_area.h) * 0.5f;
  if (m_active || m_hover >= 0 || m_hover == -2) c.fillCircle(centre, r, Color(255, 255, 255, m_active ? 34 : 22));
  int order[6] = {0, 1, 2, 3, 4, 5};
  // Back to front: furthest along the view direction first.
  std::sort(order, order + 6, [&](int a, int b) { return facingDirection(a).dot(m_cam.fwd) > facingDirection(b).dot(m_cam.fwd); });
  static const char* const kLetters[3] = {"X", "Y", "Z"};
  const Theme& t = theme();
  for (int k = 0; k < 6; k++) {
    const int id = order[k];
    const int axis = id / 2;
    const bool positive = id % 2 == 0;
    const Vec2 b = ballCentre(id);
    const float br = ballRadius(id);
    Color col = axisColor[axis];
    const bool hot = id == m_hover;
    if (hot) col = col.lighten(0.35f);
    if (positive) {
      c.line(centre, b, col.alpha(0.9f), 2.0f);
      c.fillCircle(b, br, col);
      c.textIn(*t.small, Rect{b.x - br, b.y - br, br * 2, br * 2}, kLetters[axis], Color(20, 22, 28), HAlign::Center, false);
    } else {
      c.fillCircle(b, br, col.darken(0.55f).alpha(0.85f));
      c.strokeCircle(b, br, col, 1.5f);
    }
  }
}

// ------------------------------------------------------------------ world-space drawing
void drawWorldLine(Canvas& c, const GizmoCamera& cam, Vec3 a, Vec3 b, Color col, float width) {
  float za = (a - cam.eye).dot(cam.fwd), zb = (b - cam.eye).dot(cam.fwd);
  // Clip a little in front of the near plane: a point placed exactly on it can round to just behind it.
  const float n = cam.nearPlane * 1.02f + 1e-5f;
  if (za < n && zb < n) return;
  if (za < n) { const float t = (n - za) / (zb - za); a = a + (b - a) * t; }
  else if (zb < n) { const float t = (n - zb) / (za - zb); b = b + (a - b) * t; }
  Vec2 pa, pb;
  if (!cam.project(a, pa) || !cam.project(b, pb)) return;
  c.line(pa, pb, col, width);
}

void drawWorldTriangle(Canvas& c, const GizmoCamera& cam, Vec3 a, Vec3 b, Vec3 cc, Color col) {
  Vec2 p[3];
  if (!cam.project(a, p[0]) || !cam.project(b, p[1]) || !cam.project(cc, p[2])) return;
  c.fillPolygon(std::span<const Vec2>(p, 3), col, true);
}

// ------------------------------------------------------------------ scene depth
float GizmoDepth::sample(Vec2 p) const {
  if (!valid()) return std::numeric_limits<float>::infinity();
  const float u = (p.x - area.x) / area.w, v = (p.y - area.y) / area.h;
  if (u < 0 || v < 0 || u >= 1 || v >= 1) return std::numeric_limits<float>::infinity();
  const int x = std::min(width - 1, int(u * float(width)));
  const int y = std::min(height - 1, int(v * float(height)));
  return data[size_t(y) * size_t(width) + size_t(x)];
}

bool GizmoDepth::visible(Vec2 p, float z) const {
  const float d = sample(p);
  if (!(d < farDepth)) return true;
  z -= zOffset;
  return z <= d + std::max(minTolerance, std::fabs(d) * bias);
}

namespace {
// Clips a segment to the near plane; false when it lies wholly behind it.
bool clipNear(const GizmoCamera& cam, Vec3& a, Vec3& b) {
  const float za = (a - cam.eye).dot(cam.fwd), zb = (b - cam.eye).dot(cam.fwd);
  const float n = cam.nearPlane * 1.02f + 1e-5f;   // see drawWorldLine
  if (za < n && zb < n) return false;
  if (za < n) { const float t = (n - za) / (zb - za); a = a + (b - a) * t; }
  else if (zb < n) { const float t = (n - zb) / (za - zb); b = b + (a - b) * t; }
  return true;
}

// Draws a world segment in pieces a few pixels long. `alphaAt(point)` scales the alpha at a world point; pieces
// behind the scene take `hiddenAlpha` more. Runs of equal look are joined into one line.
template <class AlphaFn>
void drawPieces(Canvas& c, const GizmoCamera& cam, Vec3 a, Vec3 b, Color col, float width, const GizmoDepth* depth,
                float hiddenAlpha, AlphaFn alphaAt, float pixelsPerPiece) {
  if (!clipNear(cam, a, b)) return;
  Vec2 pa, pb;
  if (!cam.project(a, pa) || !cam.project(b, pb)) return;
  const bool test = depth != nullptr && depth->valid();
  const float len = (pb - pa).length();
  const int n = std::clamp(int(len / pixelsPerPiece) + 1, 1, 96);
  std::vector<Vec2> run;
  int runAlpha = -1;
  const auto flush = [&]() {
    if (run.size() >= 2 && runAlpha > 0) {
      Color k = col;
      k.a = uint8_t(runAlpha);
      // A straight world segment projects to a straight screen segment, so a run is one line.
      c.line(run.front(), run.back(), k, width);
    }
    run.clear();
  };
  // Visibility at a parameter along the segment (true without a depth test).
  const auto seen = [&](const float t) {
    if (!test) return true;
    Vec2 p;
    float z = 0;
    return !cam.project(a + (b - a) * t, p, &z) || depth->visible(p, z);
  };
  // The pieces, at most `pixelsPerPiece` long ON SCREEN (a long world line in perspective has pieces near
  // the eye far longer than those far off, so equal steps in world space would step over a whole mesh),
  // each cut where the line passes behind (or out from behind) the scene, found by bisection to well
  // under a pixel, so a line never pokes into a mesh.
  const auto screenAt = [&](const float t, Vec2& out) { return cam.project(a + (b - a) * t, out); };
  std::vector<float> steps{0.f};
  {
    std::vector<std::pair<float, float>> todo;
    for (int i = n - 1; i >= 0; --i) todo.push_back({float(i) / float(n), float(i + 1) / float(n)});
    while (!todo.empty()) {
      const auto [t0, t1] = todo.back();
      todo.pop_back();
      Vec2 s0, s1;
      if (t1 - t0 > 1e-4f && screenAt(t0, s0) && screenAt(t1, s1) && (s1 - s0).length() > pixelsPerPiece && steps.size() < 2048) {
        const float tm = (t0 + t1) * 0.5f;
        todo.push_back({tm, t1});
        todo.push_back({t0, tm});
        continue;
      }
      steps.push_back(t1);
    }
  }
  std::vector<std::pair<float, bool>> cuts;   // (start t, visible) of each stretch
  bool prevSeen = seen(0.f);
  cuts.push_back({0.f, prevSeen});
  for (size_t i = 0; i + 1 < steps.size(); ++i) {
    const float t0 = steps[i], t1 = steps[i + 1];
    const bool endSeen = seen(t1);
    if (endSeen != prevSeen) {
      float lo = t0, hi = t1;
      for (int it = 0; it < 8; ++it) {
        const float mid = (lo + hi) * 0.5f;
        (seen(mid) == prevSeen ? lo : hi) = mid;
      }
      cuts.push_back({(lo + hi) * 0.5f, endSeen});
      prevSeen = endSeen;
    }
    // A piece also ends at its own end, so the fade along it keeps its steps.
    if (i + 2 < steps.size()) cuts.push_back({t1, prevSeen});
  }
  cuts.push_back({1.f, prevSeen});
  Vec2 prev = pa;
  for (size_t i = 0; i + 1 < cuts.size(); ++i) {
    const float t0 = cuts[i].first, t1 = cuts[i + 1].first;
    if (t1 <= t0) continue;
    Vec2 q;
    if (!cam.project(a + (b - a) * t1, q)) break;
    float k = alphaAt(a + (b - a) * ((t0 + t1) * 0.5f));
    if (!cuts[i].second) k *= hiddenAlpha;
    const int al = std::clamp(int(std::lround(float(col.a) * std::clamp(k, 0.f, 1.f) / 8.f)) * 8, 0, 255);
    if (al != runAlpha) {
      flush();
      runAlpha = al;
      run.push_back(prev);
    }
    run.push_back(q);
    prev = q;
  }
  flush();
}
} // namespace

void drawWorldLine(Canvas& c, const GizmoCamera& cam, Vec3 a, Vec3 b, Color col, float width, const GizmoDepth* depth,
                   float hiddenAlpha) {
  if (depth == nullptr || !depth->valid()) { drawWorldLine(c, cam, a, b, col, width); return; }
  drawPieces(c, cam, a, b, col, width, depth, hiddenAlpha, [](Vec3) { return 1.f; }, 6.f);
}

void drawWorldTriangle(Canvas& c, const GizmoCamera& cam, Vec3 a, Vec3 b, Vec3 cc, Color col, const GizmoDepth* depth,
                       float hiddenAlpha) {
  if (depth != nullptr && depth->valid()) {
    Vec2 m;
    float z = 0;
    if (cam.project((a + b + cc) * (1.f / 3.f), m, &z) && !depth->visible(m, z)) {
      col.a = uint8_t(std::clamp(float(col.a) * hiddenAlpha, 0.f, 255.f));
      if (col.a == 0) return;
    }
  }
  drawWorldTriangle(c, cam, a, b, cc, col);
}

// ------------------------------------------------------------------ world grid
void drawWorldGrid(Canvas& c, const GizmoCamera& cam, const WorldGrid& g, const GizmoDepth* depth) {
  if (g.opacity <= 0 || g.baseSpacing <= 0) return;
  // Plane basis: u, v in the plane, nrm its normal; axis ids of u and v for the axis lines.
  Vec3 u{1, 0, 0}, v{0, 0, 1}, nrm{0, 1, 0};
  int uAxis = 0, vAxis = 2, nAxis = 1;
  if (g.plane == WorldGrid::Plane::XY) { u = {1, 0, 0}; v = {0, 1, 0}; nrm = {0, 0, 1}; uAxis = 0; vAxis = 1; nAxis = 2; }
  else if (g.plane == WorldGrid::Plane::ZY) { u = {0, 0, 1}; v = {0, 1, 0}; nrm = {1, 0, 0}; uAxis = 2; vAxis = 1; nAxis = 0; }

  // Focus: where the view centre meets the plane (or the eye's foot when the view runs along it).
  const Vec2 centre{cam.cx, cam.cy};
  Vec3 focus;
  if (!cam.rayPlane(centre, Vec3{0, 0, 0}, nrm, focus)) focus = cam.eye - nrm * cam.eye.dot(nrm);
  const float height = std::max(std::fabs(cam.eye.dot(nrm)), 1e-3f);
  float worldPerPixel;
  float reach;
  if (cam.ortho) {
    worldPerPixel = 1.f / std::max(cam.orthoScale, 1e-6f);
    reach = std::hypot(cam.cx, cam.cy) * 2.f * worldPerPixel + 1.f;
    reach *= 1.5f;
  } else {
    const float dist = std::max((focus - cam.eye).length(), cam.nearPlane);
    worldPerPixel = dist / std::max(cam.focal, 1e-3f);
    reach = std::max(height * g.fadeEnd, dist * 2.f);
  }
  const float ratio = std::max(worldPerPixel * g.targetCellPixels / g.baseSpacing, 1e-9f);
  const float level = std::max(0.f, std::log10(ratio));
  const float fade = level - std::floor(level);
  const float s0 = g.baseSpacing * std::pow(10.f, std::floor(level));

  // Distance fade: by the camera's height above the plane (perspective), none in orthographic.
  const float fadeA = cam.ortho ? 1e30f : height * g.fadeStart;
  const float fadeB = cam.ortho ? 2e30f : std::max(height * g.fadeEnd, fadeA + 1e-3f);
  const Vec3 eyeFoot = cam.eye - nrm * cam.eye.dot(nrm);
  const auto distAlpha = [&](Vec3 p) {
    if (cam.ortho) return 1.f;
    const float d = (p - eyeFoot).length();
    if (d <= fadeA) return 1.f;
    if (d >= fadeB) return 0.f;
    const float t = (d - fadeA) / (fadeB - fadeA);
    return 1.f - t * t * (3.f - 2.f * t);
  };
  const auto mix = [](Color a, float k) {
    a.a = uint8_t(std::clamp(k, 0.f, 1.f) * 255.f);
    return a;
  };

  const float fu = focus.dot(u), fv = focus.dot(v);
  // One decade of lines: every `spacing`, skipping multiples of `skipEvery * spacing` (drawn by a coarser decade).
  const auto decade = [&](float spacing, Color col, float alpha, long skipEvery) {
    if (alpha <= 0.01f) return;
    const float r = std::min({reach, spacing * 150.f, cam.ortho ? 1e30f : spacing * cam.focal / 5.f});
    const long i0 = long(std::floor((fu - r) / spacing)), i1 = long(std::ceil((fu + r) / spacing));
    const long j0 = long(std::floor((fv - r) / spacing)), j1 = long(std::ceil((fv + r) / spacing));
    const Color k = mix(col, alpha * g.opacity);
    // A decade fades out where its cells shrink below a few pixels, like the engine grid's level of detail, so
    // distant lines never pile up into a bright band at the horizon.
    const float lodFar = cam.ortho ? 1e30f : spacing * cam.focal / 5.f;
    const auto lodAlpha = [&](Vec3 at) {
      float k2 = distAlpha(at);
      if (!cam.ortho) {
        const float d = (at - cam.eye).length();
        const float t = std::clamp((d - lodFar * 0.45f) / (lodFar * 0.55f), 0.f, 1.f);
        k2 *= 1.f - t * t * (3.f - 2.f * t);
      }
      return k2;
    };
    for (int dir = 0; dir < 2; ++dir) {
      const long a0 = dir == 0 ? i0 : j0, a1 = dir == 0 ? i1 : j1;
      for (long i = a0; i <= a1; ++i) {
        if (i == 0) continue;                                   // the axis line is drawn on its own
        if (skipEvery > 0 && i % skipEvery == 0) continue;
        const float w = float(i) * spacing;
        Vec3 p, q;
        if (dir == 0) { p = u * w + v * (fv - r); q = u * w + v * (fv + r); }
        else { p = v * w + u * (fu - r); q = v * w + u * (fu + r); }
        drawPieces(c, cam, p, q, k, g.lineWidth, depth, 0.f, lodAlpha, 14.f);
      }
    }
  };
  decade(s0, g.thin, g.thinIntensity * (1.f - fade), 10);
  decade(s0 * 10.f, g.mid, g.midIntensity, 10);
  decade(s0 * 100.f, g.thick, g.thickIntensity, 0);

  // Axis lines through the origin: the plane's two axes, and the normal axis as a vertical guide on the ground.
  const float r = reach;
  const auto axisLine = [&](Vec3 dir, int axisId, float along) {
    const Vec3 p = dir * (along - r), q = dir * (along + r);
    drawPieces(c, cam, p, q, mix(g.axis[axisId], g.opacity), g.axisWidth, depth, 0.f, distAlpha, 14.f);
  };
  axisLine(u, uAxis, fu);
  axisLine(v, vAxis, fv);
  if (g.plane == WorldGrid::Plane::XZ) {
    const Vec3 p = nrm * (-r), q = nrm * r;
    drawPieces(c, cam, p, q, mix(g.axis[nAxis], g.opacity * 0.8f), g.axisWidth, depth, 0.f,
               [&](Vec3 at) { const float d = std::min(1.f, std::fabs(at.dot(nrm)) / r); return 1.f - d * d; }, 14.f);
  }
}

// ------------------------------------------------------------------ object gizmos
float gizmoWorldSize(const GizmoCamera& cam, Vec3 at, float pixels) {
  if (cam.ortho) return pixels / std::max(cam.orthoScale, 1e-6f);
  const float z = std::max((at - cam.eye).dot(cam.fwd), cam.nearPlane);
  return pixels * z / std::max(cam.focal, 1e-3f);
}

namespace {
// Orthonormal basis around `fwd` with `up` as the hint.
void basisFor(Vec3 fwd, Vec3 upHint, Vec3& f, Vec3& r, Vec3& u) {
  f = fwd.normalized();
  if (std::fabs(f.dot(upHint.normalized())) > 0.98f) upHint = std::fabs(f.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  r = f.cross(upHint).normalized();
  u = r.cross(f).normalized();
}

void glyphLine(Canvas& c, const GizmoCamera& cam, Vec3 a, Vec3 b, const ObjectGizmoStyle& st, float widthScale = 1.f) {
  drawWorldLine(c, cam, a, b, st.color, st.width * widthScale, st.depth, st.hiddenAlpha);
  if (st.outSegments) {
    Vec3 ca = a, cb = b;
    if (!clipNear(cam, ca, cb)) return;
    Vec2 pa, pb;
    if (cam.project(ca, pa) && cam.project(cb, pb)) st.outSegments->push_back({pa, pb});
  }
}

void glyphCircle(Canvas& c, const GizmoCamera& cam, Vec3 centre, Vec3 u, Vec3 v, float radius,
                 const ObjectGizmoStyle& st, int segments = 20) {
  Vec3 prev = centre + u * radius;
  for (int i = 1; i <= segments; ++i) {
    const float a = float(i) * 6.2831853f / float(segments);
    const Vec3 p = centre + u * (std::cos(a) * radius) + v * (std::sin(a) * radius);
    glyphLine(c, cam, prev, p, st);
    prev = p;
  }
}

void glyphArrow(Canvas& c, const GizmoCamera& cam, Vec3 from, Vec3 to, Vec3 side, float head, const ObjectGizmoStyle& st) {
  glyphLine(c, cam, from, to, st);
  const Vec3 d = (to - from).normalized();
  const Vec3 base = to - d * head;
  glyphLine(c, cam, to, base + side * (head * 0.5f), st);
  glyphLine(c, cam, to, base - side * (head * 0.5f), st);
}
} // namespace

void drawCameraGizmo(Canvas& c, const GizmoCamera& cam, Vec3 pos, Vec3 fwdIn, Vec3 upIn, float fovYDeg, float aspect,
                     bool ortho, bool showFrustum, float frustumLength, const ObjectGizmoStyle& st) {
  Vec3 f, r, u;
  basisFor(fwdIn, upIn, f, r, u);
  const float s = st.worldSize > 0 ? st.worldSize : gizmoWorldSize(cam, pos, st.pixels);
  aspect = std::clamp(aspect, 0.2f, 5.f);
  // Body: a box behind the lens.
  const float bw = s * 0.26f, bh = s * 0.20f, bd = s * 0.55f;
  const Vec3 back = pos - f * bd;
  const Vec3 corners[8] = {
      pos + r * bw + u * bh, pos - r * bw + u * bh, pos - r * bw - u * bh, pos + r * bw - u * bh,
      back + r * bw + u * bh, back - r * bw + u * bh, back - r * bw - u * bh, back + r * bw - u * bh};
  for (int i = 0; i < 4; ++i) {
    glyphLine(c, cam, corners[i], corners[(i + 1) % 4], st);
    glyphLine(c, cam, corners[4 + i], corners[4 + (i + 1) % 4], st);
    glyphLine(c, cam, corners[i], corners[4 + i], st);
  }
  // Two film reels on top.
  const float reel = s * 0.15f;
  glyphCircle(c, cam, pos - f * (bd * 0.28f) + u * (bh + reel), f, u, reel, st, 16);
  glyphCircle(c, cam, pos - f * (bd * 0.74f) + u * (bh + reel), f, u, reel, st, 16);
  // Lens: opens along the view by the field of view (a straight hood when orthographic).
  const float lensLen = s * 0.32f;
  const float tanY = ortho ? 0.f : std::tan(std::clamp(fovYDeg, 1.f, 170.f) * 0.5f * 0.0174532925f);
  const float lh0 = bh * 0.7f, lw0 = lh0 * aspect;
  const float lh1 = ortho ? lh0 : lh0 + lensLen * tanY, lw1 = lh1 * aspect;
  const Vec3 lensEnd = pos + f * lensLen;
  const Vec3 a0[4] = {pos + r * lw0 + u * lh0, pos - r * lw0 + u * lh0, pos - r * lw0 - u * lh0, pos + r * lw0 - u * lh0};
  const Vec3 a1[4] = {lensEnd + r * lw1 + u * lh1, lensEnd - r * lw1 + u * lh1, lensEnd - r * lw1 - u * lh1,
                      lensEnd + r * lw1 - u * lh1};
  for (int i = 0; i < 4; ++i) {
    glyphLine(c, cam, a0[i], a1[i], st);
    glyphLine(c, cam, a1[i], a1[(i + 1) % 4], st);
  }
  // Which way is up: a small triangle over the lens opening.
  glyphLine(c, cam, lensEnd + u * (lh1 * 1.25f) - r * (lw1 * 0.35f), lensEnd + u * (lh1 * 1.75f), st);
  glyphLine(c, cam, lensEnd + u * (lh1 * 1.75f), lensEnd + u * (lh1 * 1.25f) + r * (lw1 * 0.35f), st);
  if (showFrustum && frustumLength > lensLen) {
    ObjectGizmoStyle thin = st;
    thin.width = std::max(1.f, st.width * 0.7f);
    thin.outSegments = nullptr;
    const float h = ortho ? lh0 : frustumLength * tanY, w = h * aspect;
    const Vec3 e = pos + f * frustumLength;
    const Vec3 far4[4] = {e + r * w + u * h, e - r * w + u * h, e - r * w - u * h, e + r * w - u * h};
    for (int i = 0; i < 4; ++i) {
      glyphLine(c, cam, ortho ? a0[i] : pos, far4[i], thin);
      glyphLine(c, cam, far4[i], far4[(i + 1) % 4], thin);
    }
  }
}

void drawDirectionalLightGizmo(Canvas& c, const GizmoCamera& cam, Vec3 pos, Vec3 dir, const ObjectGizmoStyle& st) {
  Vec3 f, r, u;
  basisFor(dir, Vec3{0, 1, 0}, f, r, u);
  const float s = st.worldSize > 0 ? st.worldSize : gizmoWorldSize(cam, pos, st.pixels);
  glyphCircle(c, cam, pos, r, u, s * 0.30f, st, 24);
  const float len = s * 1.1f, head = s * 0.22f;
  const Vec3 offsets[3] = {u * (s * 0.30f), r * (s * 0.26f) - u * (s * 0.15f), r * (-s * 0.26f) - u * (s * 0.15f)};
  for (const Vec3& o : offsets) glyphArrow(c, cam, pos + o + f * (s * 0.1f), pos + o + f * len, o.normalized(), head, st);
}

void drawSpotLightGizmo(Canvas& c, const GizmoCamera& cam, Vec3 pos, Vec3 dir, float halfAngleDeg, float length,
                        const ObjectGizmoStyle& st) {
  Vec3 f, r, u;
  basisFor(dir, Vec3{0, 1, 0}, f, r, u);
  const float radius = length * std::tan(std::clamp(halfAngleDeg, 0.5f, 89.f) * 0.0174532925f);
  const Vec3 end = pos + f * length;
  glyphCircle(c, cam, end, r, u, radius, st, 28);
  for (const Vec3 side : {r, r * -1.f, u, u * -1.f}) glyphLine(c, cam, pos, end + side * radius, st);
  // The housing: a small ring at the apex.
  glyphCircle(c, cam, pos, r, u, (st.worldSize > 0 ? st.worldSize : gizmoWorldSize(cam, pos, st.pixels)) * 0.12f, st, 14);
}

float pickDistance(Vec2 p, const std::vector<std::pair<Vec2, Vec2>>& segments) {
  float best = 1e30f;
  for (const auto& [a, b] : segments) {
    const Vec2 ab = b - a;
    const float l2 = ab.x * ab.x + ab.y * ab.y;
    float t = l2 > 1e-6f ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / l2 : 0.f;
    t = std::clamp(t, 0.f, 1.f);
    best = std::min(best, (p - (a + ab * t)).length());
  }
  return best;
}

// ------------------------------------------------------------------ selection outline
bool buildSelectionOutline(const uint8_t* mask, int w, int h, int radius, Color color, std::vector<uint32_t>& out,
                           int rect[4]) {
  out.clear();
  if (mask == nullptr || w <= 0 || h <= 0) return false;
  radius = std::clamp(radius, 1, 8);
  int x0 = w, y0 = h, x1 = -1, y1 = -1;
  for (int y = 0; y < h; ++y) {
    const uint8_t* row = mask + size_t(y) * size_t(w);
    for (int x = 0; x < w; ++x)
      if (row[x]) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
  }
  if (x1 < 0) return false;
  x0 = std::max(0, x0 - radius - 1); y0 = std::max(0, y0 - radius - 1);
  x1 = std::min(w - 1, x1 + radius + 1); y1 = std::min(h - 1, y1 + radius + 1);
  const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
  out.assign(size_t(bw) * size_t(bh), 0u);
  // The disc of offsets, nearest first, so each pixel stops at its first hit.
  struct Off { int dx, dy; float d; };
  std::vector<Off> offs;
  for (int dy = -radius; dy <= radius; ++dy)
    for (int dx = -radius; dx <= radius; ++dx) {
      const float d = std::sqrt(float(dx * dx + dy * dy));
      if (d <= float(radius) + 0.5f && (dx != 0 || dy != 0)) offs.push_back({dx, dy, d});
    }
  std::sort(offs.begin(), offs.end(), [](const Off& a, const Off& b) { return a.d < b.d; });
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      if (mask[size_t(y) * size_t(w) + size_t(x)]) continue;     // the band sits outside the object
      float nearest = 1e9f;
      int strongest = 0;
      for (const Off& o : offs) {
        if (o.d > nearest + 1.0f) break;
        const int px = x + o.dx, py = y + o.dy;
        if (px < 0 || py < 0 || px >= w || py >= h) continue;
        const int m = mask[size_t(py) * size_t(w) + size_t(px)];
        if (m) { nearest = std::min(nearest, o.d); strongest = std::max(strongest, m); }
      }
      if (strongest == 0) continue;
      const float coverage = std::clamp(float(radius) + 0.5f - nearest, 0.f, 1.f);
      if (coverage <= 0) continue;
      const float k = float(strongest) / 255.f;
      const float shade = 0.7f + 0.3f * k;
      const uint32_t a = uint32_t(std::clamp(k * coverage, 0.f, 1.f) * 255.f + 0.5f);
      const uint32_t r = uint32_t(std::clamp(float(color.r) * shade, 0.f, 255.f));
      const uint32_t gg = uint32_t(std::clamp(float(color.g) * shade, 0.f, 255.f));
      const uint32_t b = uint32_t(std::clamp(float(color.b) * shade, 0.f, 255.f));
      out[size_t(y - y0) * size_t(bw) + size_t(x - x0)] = (a << 24) | (r << 16) | (gg << 8) | b;
    }
  }
  rect[0] = x0; rect[1] = y0; rect[2] = bw; rect[3] = bh;
  return true;
}

} // namespace prism
