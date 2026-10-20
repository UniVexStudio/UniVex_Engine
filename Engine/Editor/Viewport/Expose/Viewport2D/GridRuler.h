#pragma once
#include <string>
#include <vector>

namespace univex::viewport {

// Two-level decade grid with fading minor lines (smooth LOD, no popping).
struct GridLevels {
    float minor = 1.f;       // world units
    float major = 10.f;      // world units (always 10x minor)
    float minorAlpha = 1.f;  // 0..1, fades as minor lines get dense
};
// minStep > 0 clamps the minor step (e.g. 1 = never finer than one game pixel).
GridLevels computeGridLevels(float zoom, float targetPx, float minStep);

struct RulerTick { float value; bool labeled; };
struct RulerScale {
    float labelStep = 1.f;
    int   decimals = 0;
    std::vector<RulerTick> ticks;
};
RulerScale  computeRulerTicks(float lo, float hi, float zoom, float labelTargetPx, int maxTicks = 600);
std::string formatRulerValue(float v, int decimals);

} // namespace univex::viewport
