#include "Viewport2D/Snapping.h"
#include <cmath>

namespace univex::viewport {

float snapToStep(float v, float step) {
    return step > 0.f ? std::round(v / step) * step : v;
}

static float snapAxis(float v, const std::vector<float>& g, const SnapSettings& s, float zoom) {
    if (s.guides) {
        float best = 0.f; bool found = false; float bestD = s.guidePx / zoom;
        for (float gv : g) {
            const float d = std::fabs(v - gv);
            if (d <= bestD) { bestD = d; best = gv; found = true; }
        }
        if (found) return best;
    }
    if (s.grid)  return snapToStep(v, s.gridStep);
    if (s.pixel) return std::round(v);
    return v;
}

Vec2 snapPoint(Vec2 w, const SnapSettings& s, const Guides& g, float zoom) {
    return { snapAxis(w.x, g.vertical, s, zoom), snapAxis(w.y, g.horizontal, s, zoom) };
}

} // namespace univex::viewport
