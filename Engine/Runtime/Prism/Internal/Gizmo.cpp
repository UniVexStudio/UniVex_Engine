// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Gizmo.h"
#include "prism/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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
  out = {cx + d.dot(right) / z * focal, cy - d.dot(up) / z * focal};
  if (depth) *depth = z;
  return true;
}
Vec3 GizmoCamera::rayDir(Vec2 p) const { return (fwd + right * ((p.x - cx) / focal) - up * ((p.y - cy) / focal)).normalized(); }
bool GizmoCamera::rayPlane(Vec2 p, Vec3 p0, Vec3 n, Vec3& out) const {
  Vec3 d = rayDir(p);
  float den = d.dot(n);
  if (std::fabs(den) < 1e-4f) return false;
  float t = (p0 - eye).dot(n) / den;
  if (t < 0) return false;
  out = eye + d * t;
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

} // namespace prism
