#include "Viewport2D/Camera2D.h"
#include <algorithm>
#include <cmath>

namespace univex::viewport {

void Camera2D::setViewportSize(int w, int h) { m_w = std::max(1, w); m_h = std::max(1, h); }
void Camera2D::setZoom(float z) { m_zoom = std::clamp(z, kMinZoom, kMaxZoom); }

Vec2 Camera2D::worldToScreen(Vec2 w) const {
    return { (w.x - m_center.x) * m_zoom + m_w * 0.5f,
             (w.y - m_center.y) * m_zoom * ySign() + m_h * 0.5f };
}

Vec2 Camera2D::screenToWorld(Vec2 s) const {
    return { (s.x - m_w * 0.5f) / m_zoom + m_center.x,
             (s.y - m_h * 0.5f) / (m_zoom * ySign()) + m_center.y };
}

Rect Camera2D::visibleWorldRect() const {
    const Vec2 a = screenToWorld({0.f, 0.f});
    const Vec2 b = screenToWorld({float(m_w), float(m_h)});
    return { {std::min(a.x, b.x), std::min(a.y, b.y)}, {std::max(a.x, b.x), std::max(a.y, b.y)} };
}

void Camera2D::panByScreenDelta(Vec2 d) {
    m_center.x -= d.x / m_zoom;
    m_center.y -= d.y / (m_zoom * ySign());
}

void Camera2D::zoomAt(Vec2 sp, float factor) {
    if (!(factor > 0.f)) return;
    const Vec2 before = screenToWorld(sp);
    setZoom(m_zoom * factor);
    setCenterFromAnchor(before, sp);
}

void Camera2D::fitRect(const Rect& r, float margin) {
    const float w = std::max(r.max.x - r.min.x, 1e-6f);
    const float h = std::max(r.max.y - r.min.y, 1e-6f);
    const float availW = std::max(1.f, m_w - 2.f * margin);
    const float availH = std::max(1.f, m_h - 2.f * margin);
    setZoom(std::min(availW / w, availH / h));
    m_center = { (r.min.x + r.max.x) * 0.5f, (r.min.y + r.max.y) * 0.5f };
}

void Camera2D::setCenterFromAnchor(Vec2 world, Vec2 screen) {
    m_center.x = world.x - (screen.x - m_w * 0.5f) / m_zoom;
    m_center.y = world.y - (screen.y - m_h * 0.5f) / (m_zoom * ySign());
}

void Camera2D::alignToPixels() {
    const float ox = m_w * 0.5f - m_center.x * m_zoom;
    const float oy = m_h * 0.5f - m_center.y * m_zoom * ySign();
    m_center.x = (m_w * 0.5f - std::round(ox)) / m_zoom;
    m_center.y = (m_h * 0.5f - std::round(oy)) / (m_zoom * ySign());
}

} // namespace univex::viewport
