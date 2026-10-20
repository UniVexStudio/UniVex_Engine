#include "Viewport2D/GridRuler.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace univex::viewport {

GridLevels computeGridLevels(float zoom, float targetPx, float minStep) {
    const float raw = targetPx / zoom;
    float base = std::pow(10.f, std::floor(std::log10(raw)));
    if (minStep > 0.f && base < minStep) base = minStep;
    GridLevels g;
    g.minor = base;
    g.major = base * 10.f;
    const float minorPx = g.minor * zoom;
    g.minorAlpha = std::clamp((minorPx - 6.f) / (targetPx * 0.5f - 6.f), 0.f, 1.f);
    return g;
}

static float niceStep(float raw) {
    const float base = std::pow(10.f, std::floor(std::log10(raw)));
    const float m = raw / base;
    const float k = (m <= 1.f) ? 1.f : (m <= 2.f) ? 2.f : (m <= 5.f) ? 5.f : 10.f;
    return base * k;
}

RulerScale computeRulerTicks(float lo, float hi, float zoom, float labelTargetPx, int maxTicks) {
    RulerScale rs;
    rs.labelStep = niceStep(labelTargetPx / zoom);
    const float decade = std::pow(10.f, std::floor(std::log10(rs.labelStep) + 1e-4f));
    const float mant = rs.labelStep / decade;
    const int divisions = (std::fabs(mant - 2.f) < 0.05f) ? 4 : 5;
    const float tickStep = rs.labelStep / float(divisions);
    rs.decimals = std::clamp(int(std::ceil(-std::log10(rs.labelStep) - 1e-4f)), 0, 5);

    const long long i0 = (long long)std::floor(lo / tickStep);
    const long long i1 = (long long)std::ceil(hi / tickStep);
    if (i1 - i0 > maxTicks) return rs;
    for (long long i = i0; i <= i1; ++i)
        rs.ticks.push_back({ float(i) * tickStep, (i % divisions) == 0 });
    return rs;
}

std::string formatRulerValue(float v, int decimals) {
    if (std::fabs(v) < 0.5f * std::pow(10.f, -float(decimals))) v = 0.f;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, double(v));
    return buf;
}

} // namespace univex::viewport
