// univex/viewport/AxisPaletteApply.h
// -----------------------------------------------------------------------
// The one place that knows an axis palette has TWO destinations: the transform gizmo's own axes
// and the grid's axis lines, which run through the same origin and must stay recognisably the
// same X/Y/Z with the grid as the darker backdrop.
//
// It lives here, apart from ViewportRenderPass, for one reason: the render pass cannot be
// constructed without a GL context, and this is the part worth testing - six colour slots written
// from three, with a dim factor on half of them, is exactly the shape of code where one forgotten
// line goes unnoticed until someone looks at the screen. Taking the two plain settings structs by
// reference makes it checkable on a machine with no GPU at all.
// -----------------------------------------------------------------------
#pragma once

#include "univex/gizmo/GizmoStyle.h"
#include "univex/render/GridSettings.h"
#include "univex/viewport/AxisPalette.h"

namespace univex::viewport {

/// Writes `palette` to the gizmo's axis colours and the grid's dimmed axis colours.
///
/// Does not validate - callers that accept a palette from outside the program (a settings file, a
/// colour picker) check IsAxisPaletteValidUVE first, so that an invalid palette leaves BOTH
/// destinations untouched rather than half-written.
inline void ApplyAxisPaletteUVE(const AxisPaletteUVE& palette, univex::gizmo::GizmoStyle& style,
                                univex::render::GridSettings& grid) {
    using univex::math::Vec3;
    style.axisColorX = Vec3{palette.x.r, palette.x.g, palette.x.b};
    style.axisColorY = Vec3{palette.y.r, palette.y.g, palette.y.b};
    style.axisColorZ = Vec3{palette.z.r, palette.z.g, palette.z.b};

    const AxisPaletteUVE dimmed = palette.GridVariantUVE();
    grid.axisColorX = univex::render::GridColor{dimmed.x.r, dimmed.x.g, dimmed.x.b};
    grid.axisColorY = univex::render::GridColor{dimmed.y.r, dimmed.y.g, dimmed.y.b};
    grid.axisColorZ = univex::render::GridColor{dimmed.z.r, dimmed.z.g, dimmed.z.b};
}

/// The palette currently in effect, read back from the gizmo rather than the grid: the gizmo holds
/// the colours as chosen, the grid holds them already dimmed, and dividing the factor back out
/// would only reintroduce rounding.
[[nodiscard]] inline AxisPaletteUVE AxisPaletteOfUVE(const univex::gizmo::GizmoStyle& style) {
    return AxisPaletteUVE{AxisRgbUVE{style.axisColorX.x, style.axisColorX.y, style.axisColorX.z},
                          AxisRgbUVE{style.axisColorY.x, style.axisColorY.y, style.axisColorY.z},
                          AxisRgbUVE{style.axisColorZ.x, style.axisColorZ.y, style.axisColorZ.z}};
}

} // namespace univex::viewport
