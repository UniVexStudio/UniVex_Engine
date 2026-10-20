#include "Viewport2D/TransformGizmo.h"
#include "Viewport2D/DrawUtil.h"
#include <algorithm>
#include <cmath>

namespace univex::viewport {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kHandleHalfPx = 4.f;
constexpr float kHitPx = 8.f;
constexpr float kRotateOffsetPx = 28.f;
constexpr float kEdgeHandleMinPx = 28.f;
constexpr float kMinSize = 1e-2f;
constexpr int   kHandles[8][2] = {{-1,-1},{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0}};

constexpr Color kAccent {86, 156, 255, 255};
constexpr Color kHover  {255, 190, 60, 255};
constexpr Color kDark   {20, 20, 24, 255};
constexpr Color kWhite  {245, 245, 250, 255};

Vec2 rot(Vec2 v, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return { v.x * c - v.y * s, v.x * s + v.y * c };
}
float topSign(const Camera2D& cam) { return cam.yUp() ? 1.f : -1.f; }
bool axisAligned(float rotation) {
    const float r = std::fabs(std::fmod(rotation, kPi));
    return r < 1e-4f || kPi - r < 1e-4f;
}

} // namespace

Vec2 Box::localToWorld(Vec2 l) const { return center + rot(l, rotation); }
Vec2 Box::worldToLocal(Vec2 w) const { return rot(w - center, -rotation); }

bool TransformGizmo::edgeHandlesVisible(const Camera2D& cam) const {
    return std::min(m_box.size.x, m_box.size.y) * cam.zoom() >= kEdgeHandleMinPx;
}

Vec2 TransformGizmo::rotateHandleWorld(const Camera2D& cam) const {
    return m_box.localToWorld({0.f, topSign(cam) * (m_box.size.y * 0.5f + kRotateOffsetPx / cam.zoom())});
}

Handle TransformGizmo::hitTest(Vec2 screen, const Camera2D& cam) const {
    if (!m_has) return {};
    auto near = [&](Vec2 worldPt) {
        const Vec2 s = cam.worldToScreen(worldPt);
        const float dx = s.x - screen.x, dy = s.y - screen.y;
        return dx * dx + dy * dy <= kHitPx * kHitPx;
    };
    if (m_pivotEditing && near(m_box.pivotWorld())) return {Handle::Kind::Pivot, 0, 0};
    if (near(rotateHandleWorld(cam))) return {Handle::Kind::Rotate, 0, 0};
    const bool edges = edgeHandlesVisible(cam);
    for (const auto& h : kHandles) {
        if (!edges && (h[0] == 0 || h[1] == 0)) continue;
        if (near(m_box.localToWorld({h[0] * m_box.size.x * 0.5f, h[1] * m_box.size.y * 0.5f})))
            return {Handle::Kind::Scale, h[0], h[1]};
    }
    const Vec2 l = m_box.worldToLocal(cam.screenToWorld(screen));
    if (std::fabs(l.x) <= m_box.size.x * 0.5f && std::fabs(l.y) <= m_box.size.y * 0.5f)
        return {Handle::Kind::Body, 0, 0};
    return {};
}

void TransformGizmo::beginDrag(Handle h, Vec2 screen, const Camera2D& cam) {
    if (!m_has || h.kind == Handle::Kind::None) return;
    m_dragging = true;
    m_handle = h;
    m_start = m_box;
    m_startMouseWorld = cam.screenToWorld(screen);
    const Vec2 p = m_box.pivotWorld();
    m_startAngle = std::atan2(m_startMouseWorld.y - p.y, m_startMouseWorld.x - p.x);
}

Box TransformGizmo::updateDrag(Vec2 screen, const Camera2D& cam, const SnapSettings& snap,
                               const Guides& guides, DragModifiers mods) {
    if (!m_dragging) return m_box;
    const Vec2 mw = cam.screenToWorld(screen);
    Box b = m_start;

    switch (m_handle.kind) {
    case Handle::Kind::Body: {
        Vec2 d = mw - m_startMouseWorld;
        if (mods.shift) { if (std::fabs(d.x) >= std::fabs(d.y)) d.y = 0.f; else d.x = 0.f; }
        // Snap the selection's min corner when axis-aligned, else its center.
        const Vec2 ref = axisAligned(m_start.rotation)
            ? Vec2{m_start.center.x - m_start.size.x * 0.5f, m_start.center.y - m_start.size.y * 0.5f}
            : m_start.center;
        const Vec2 snapped = snapPoint(ref + d, snap, guides, cam.zoom());
        Vec2 nd = snapped - ref;
        if (mods.shift) { if (d.x == 0.f) nd.x = 0.f; if (d.y == 0.f) nd.y = 0.f; }
        b.center = m_start.center + nd;
        break;
    }
    case Handle::Kind::Scale: {
        const int hx = m_handle.hx, hy = m_handle.hy;
        const Vec2 sm = snapPoint(mw, snap, guides, cam.zoom());
        const Vec2 q = rot(sm - m_start.center, -m_start.rotation);
        const float hw = m_start.size.x * 0.5f, hh = m_start.size.y * 0.5f;
        float newW = m_start.size.x, newH = m_start.size.y;
        Vec2 cl{0.f, 0.f};
        auto axis = [&](int h, float half, float qv, float& newSize, float& cLocal) {
            if (h == 0) return;
            if (mods.alt) {
                newSize = std::max(float(h) * qv, kMinSize * 0.5f) * 2.f;
                cLocal = 0.f;
            } else {
                const float anchor = -float(h) * half;
                newSize = std::max(float(h) * (qv - anchor), kMinSize);
                cLocal = anchor + float(h) * newSize * 0.5f;
            }
        };
        axis(hx, hw, q.x, newW, cl.x);
        axis(hy, hh, q.y, newH, cl.y);
        if (mods.shift) {
            const float sx = newW / m_start.size.x, sy = newH / m_start.size.y;
            if (hx != 0 && hy != 0) {
                const float s = std::max(sx, sy);
                newW = m_start.size.x * s; newH = m_start.size.y * s;
                cl = mods.alt ? Vec2{0.f, 0.f}
                              : Vec2{-float(hx) * hw + float(hx) * newW * 0.5f,
                                     -float(hy) * hh + float(hy) * newH * 0.5f};
            } else if (hx != 0) {
                newH = m_start.size.y * sx;
            } else {
                newW = m_start.size.x * sy;
            }
        }
        b.size = {newW, newH};
        b.center = m_start.center + rot(cl, m_start.rotation);
        b.pivotOffset = {m_start.pivotOffset.x * newW / m_start.size.x,
                         m_start.pivotOffset.y * newH / m_start.size.y};
        break;
    }
    case Handle::Kind::Rotate: {
        const Vec2 p = m_start.pivotWorld();
        const float a = std::atan2(mw.y - p.y, mw.x - p.x);
        float newRot = m_start.rotation + (a - m_startAngle);
        if ((snap.rotation || mods.shift) && snap.rotationStepDeg > 0.f) {
            const float step = snap.rotationStepDeg * kPi / 180.f;
            newRot = std::round(newRot / step) * step;
        }
        const float delta = newRot - m_start.rotation;
        b.rotation = newRot;
        b.center = p + rot(m_start.center - p, delta);   // pivot stays fixed in world space
        break;
    }
    case Handle::Kind::Pivot: {
        const Vec2 sm = snapPoint(mw, snap, guides, cam.zoom());
        b.pivotOffset = rot(sm - m_start.center, -m_start.rotation);
        break;
    }
    case Handle::Kind::None: break;
    }
    m_box = b;
    return b;
}

void TransformGizmo::draw(IViewportRenderer& r, const Camera2D& cam, Handle hover) const {
    if (!m_has) return;
    const float hw = m_box.size.x * 0.5f, hh = m_box.size.y * 0.5f;
    const Vec2 corners[4] = {
        cam.worldToScreen(m_box.localToWorld({-hw, -hh})), cam.worldToScreen(m_box.localToWorld({hw, -hh})),
        cam.worldToScreen(m_box.localToWorld({hw, hh})),   cam.worldToScreen(m_box.localToWorld({-hw, hh}))};
    strokePolygon(r, corners, 4, kAccent, 1.5f);

    // rotate handle
    const bool rotHover = hover.kind == Handle::Kind::Rotate || (m_dragging && m_handle.kind == Handle::Kind::Rotate);
    const Vec2 top = cam.worldToScreen(m_box.localToWorld({0.f, topSign(cam) * hh}));
    const Vec2 rh  = cam.worldToScreen(rotateHandleWorld(cam));
    r.drawLine(top, rh, kAccent, 1.f);
    strokeCircle(r, rh, kHandleHalfPx + 1.5f, rotHover ? kHover : kWhite, 2.f, 20);

    // scale handles
    const bool edges = edgeHandlesVisible(cam);
    for (const auto& h : kHandles) {
        if (!edges && (h[0] == 0 || h[1] == 0)) continue;
        const Vec2 s = cam.worldToScreen(m_box.localToWorld({h[0] * hw, h[1] * hh}));
        const bool hot = (hover.kind == Handle::Kind::Scale && hover.hx == h[0] && hover.hy == h[1]) ||
                         (m_dragging && m_handle.kind == Handle::Kind::Scale && m_handle.hx == h[0] && m_handle.hy == h[1]);
        r.fillRect(s - Vec2{kHandleHalfPx + 1.f, kHandleHalfPx + 1.f}, {2.f * kHandleHalfPx + 2.f, 2.f * kHandleHalfPx + 2.f}, kDark);
        r.fillRect(s - Vec2{kHandleHalfPx, kHandleHalfPx}, {2.f * kHandleHalfPx, 2.f * kHandleHalfPx}, hot ? kHover : kWhite);
    }

    // pivot
    if (m_pivotEditing || m_box.pivotOffset.x != 0.f || m_box.pivotOffset.y != 0.f) {
        const Vec2 p = cam.worldToScreen(m_box.pivotWorld());
        const bool hot = hover.kind == Handle::Kind::Pivot || (m_dragging && m_handle.kind == Handle::Kind::Pivot);
        const Color c = hot ? kHover : Color{255, 120, 200, 255};
        strokeCircle(r, p, 6.f, c, 1.5f, 20);
        r.drawLine({p.x - 10.f, p.y}, {p.x + 10.f, p.y}, c, 1.f);
        r.drawLine({p.x, p.y - 10.f}, {p.x, p.y + 10.f}, c, 1.f);
    }
}

void fillBox(IViewportRenderer& r, const Camera2D& cam, const Box& b, Color c) {
    const float hw = b.size.x * 0.5f, hh = b.size.y * 0.5f;
    r.fillQuad(cam.worldToScreen(b.localToWorld({-hw, -hh})), cam.worldToScreen(b.localToWorld({hw, -hh})),
               cam.worldToScreen(b.localToWorld({hw, hh})),   cam.worldToScreen(b.localToWorld({-hw, hh})), c);
}

} // namespace univex::viewport
