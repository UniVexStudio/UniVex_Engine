#pragma once
#include "IViewportRenderer.h"
#include <algorithm>
#include <cmath>

namespace univex::viewport {

inline Color withAlpha(Color c, float k) {
    c.a = static_cast<std::uint8_t>(std::clamp(static_cast<float>(c.a) * k, 0.f, 255.f));
    return c;
}

inline void strokePolygon(IViewportRenderer& r, const Vec2* p, int n, Color c, float t = 1.f) {
    for (int i = 0; i < n; ++i) r.drawLine(p[i], p[(i + 1) % n], c, t);
}

inline void strokeRect(IViewportRenderer& r, Vec2 a, Vec2 b, Color c, float t = 1.f) {
    const Vec2 p[4] = {{a.x, a.y}, {b.x, a.y}, {b.x, b.y}, {a.x, b.y}};
    strokePolygon(r, p, 4, c, t);
}

inline void strokeCircle(IViewportRenderer& r, Vec2 c, float rad, Color col, float t = 1.f, int seg = 24) {
    constexpr float kTau = 6.28318530718f;
    Vec2 prev{c.x + rad, c.y};
    for (int i = 1; i <= seg; ++i) {
        const float a = kTau * static_cast<float>(i) / static_cast<float>(seg);
        const Vec2 cur{c.x + std::cos(a) * rad, c.y + std::sin(a) * rad};
        r.drawLine(prev, cur, col, t);
        prev = cur;
    }
}

inline void dashedLine(IViewportRenderer& r, Vec2 a, Vec2 b, Color c, float t, float dash, float gap) {
    const Vec2 d = b - a;
    const float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len < 1e-3f) return;
    const Vec2 u{d.x / len, d.y / len};
    for (float s = 0.f; s < len; s += dash + gap) {
        const float e = std::min(s + dash, len);
        r.drawLine(a + u * s, a + u * e, c, t);
    }
}

} // namespace univex::viewport
