// univex/render/GridSettings.h
// -----------------------------------------------------------------------
// Every tunable of the infinite grid in one place, plus a CPU mirror of
// the shader's LOD decade selection.
//
// That mirror exists for two reasons. First, the viewport UI wants to
// print the grid's current spacing ("grid: 10 m") and snapping wants to
// query it, and neither can read a value that only exists inside a
// fragment shader. Second, it makes the auto-adjust behaviour testable
// without a GPU: the shader and ComputeGridLod() implement the same
// formula, so the unit tests can pin the decade boundaries exactly.
// -----------------------------------------------------------------------
#pragma once

#include <algorithm>
#include <cmath>

#include "univex/viewport/AxisPalette.h"

namespace univex::render {

struct GridColor {
    float r = 0.f, g = 0.f, b = 0.f;
};

// Which world plane the grid is drawn on. The ground (XZ) almost always; a named side view looks
// along the ground, where it is only an edge, so the grid stands up on the plane facing the
// camera instead - XY for Front/Back, ZY for Left/Right - and the view keeps a reference.
enum class GridPlane {
    XZ = 0,
    XY = 1,
    ZY = 2,
};

// The plane facing a view that looks along `direction` (either sign): the one whose normal is the
// dominant world axis, when the view is axis-aligned. Anything oblique, and Top/Bottom, keep the
// ground. Mirrors the host's choice so it is testable without a GPU.
[[nodiscard]] constexpr GridPlane GridPlaneFacing(float x, float y, float z) {
    const float ax = x < 0.f ? -x : x;
    const float az = z < 0.f ? -z : z;
    const float lengthSquared = x * x + y * y + z * z;
    constexpr float kAlignedCosSquared = 0.999f * 0.999f;
    if (az * az >= kAlignedCosSquared * lengthSquared && az > 0.f) return GridPlane::XY;
    if (ax * ax >= kAlignedCosSquared * lengthSquared && ax > 0.f) return GridPlane::ZY;
    return GridPlane::XZ;
}

struct GridSettings {
    // Finest spacing the grid will ever draw, in world units. 1.0 with a
    // metre-scale engine means the tightest visible cell is 1 m; the LOD
    // only ever multiplies this by powers of ten.
    float baseSpacing = 1.0f;

    // Minimum on-screen size (pixels) a cell may shrink to before the LOD
    // steps up a decade. Larger = sparser grid.
    float targetCellPixels = 24.0f;

    float lineWidthPixels = 1.25f;
    float axisWidthPixels = 1.6f;

    GridColor thinColor{0.36f, 0.40f, 0.49f};
    GridColor midColor{0.55f, 0.60f, 0.70f};
    GridColor thickColor{0.72f, 0.77f, 0.87f};

    float thinIntensity = 0.45f;
    float midIntensity = 0.70f;
    float thickIntensity = 0.95f;

    // Darker variants of univex/viewport/AxisPalette.h's shared axis hues: the grid's lines run
    // through the same origin the transform gizmo sits on, so they must read as the backdrop
    // rather than compete with the handle drawn over them.
    GridColor axisColorX{univex::viewport::kGridAxisColorXUVE.r, univex::viewport::kGridAxisColorXUVE.g,
                         univex::viewport::kGridAxisColorXUVE.b};
    GridColor axisColorY{univex::viewport::kGridAxisColorYUVE.r, univex::viewport::kGridAxisColorYUVE.g,
                         univex::viewport::kGridAxisColorYUVE.b};
    GridColor axisColorZ{univex::viewport::kGridAxisColorZUVE.r, univex::viewport::kGridAxisColorZUVE.g,
                         univex::viewport::kGridAxisColorZUVE.b};

    // Horizon fade, as multiples of the camera's orbit distance. Keeping
    // these relative to distance means the fade sits at the same place on
    // screen whether the camera is 2 m or 2 km from the pivot.
    float fadeStartDistanceScale = 12.0f;
    float fadeEndDistanceScale = 45.0f;

    float opacity = 1.0f;

    GridPlane plane = GridPlane::XZ;
};

struct GridLod {
    float level = 0.f;      // continuous LOD; floor() is the decade
    float fade = 0.f;       // fract(level): how far into the next decade
    float finestSpacing = 0.f; // world units, base * 10^floor(level)
};

// Exact CPU mirror of the LOD selection at the top of infinite_grid.frag.
// `worldPerPixel` is how much world space a single pixel covers on the
// ground plane at the point of interest.
[[nodiscard]] inline GridLod ComputeGridLod(float worldPerPixel, const GridSettings& settings) {
    const float safeWorldPerPixel = worldPerPixel > 1e-9f ? worldPerPixel : 1e-9f;
    const float ratio = safeWorldPerPixel * settings.targetCellPixels / settings.baseSpacing;
    const float level = std::max(0.f, std::log10(ratio));

    GridLod lod;
    lod.level = level;
    lod.fade = level - std::floor(level);
    lod.finestSpacing = settings.baseSpacing * std::pow(10.f, std::floor(level));
    return lod;
}

// The spacing a user would call "the grid size" right now — the finest
// tier that is still drawn at full strength. Useful for a viewport HUD or
// for grid snapping.
[[nodiscard]] inline float ComputeDisplayGridSpacing(float worldPerPixel, const GridSettings& settings) {
    const GridLod lod = ComputeGridLod(worldPerPixel, settings);
    // Past the halfway point of a decade the finest tier has faded more
    // than half out, so the next one up is what actually reads as "the"
    // grid.
    return lod.fade < 0.5f ? lod.finestSpacing : lod.finestSpacing * 10.f;
}

} // namespace univex::render
