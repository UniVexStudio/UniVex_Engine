// univex/gizmo/GizmoStyle.h
// -----------------------------------------------------------------------
// Every tunable of the transform gizmos and the orientation nav gizmo.
//
// Line widths are in PIXELS, not world units. The renderer expands each
// segment into a screen-space quad, so a 2.4 here means 2.4 px on screen
// whether the camera is 20 cm or 2 km away — which is the only way a
// gizmo stays legible across an editor's whole zoom range, and the reason
// the old "thick at some zooms, hairline at others" look is gone.
//
// Likewise the whole gizmo is sized in pixels (gizmoPixelRadius): the
// geometry below is authored in abstract gizmo units and scaled per frame
// so the widget occupies a constant slice of the screen.
// -----------------------------------------------------------------------
#pragma once

#include "univex/viewport/AxisPalette.h"

#include "univex/math/Vec.h"

namespace univex::gizmo {

using univex::math::Vec3;

struct GizmoStyle {
    // ---- overall on-screen size ------------------------------------------
    float gizmoPixelRadius = 155.f; // screen radius of the widget, in pixels

    // ---- axis colours ----------------------------------------------------
    // From univex/viewport/AxisPalette.h, the single definition these and the grid's own axis
    // lines both derive from - see that header for why the grid takes a darker variant.
    Vec3 axisColorX{univex::viewport::kAxisColorXUVE.r, univex::viewport::kAxisColorXUVE.g,
                    univex::viewport::kAxisColorXUVE.b};
    Vec3 axisColorY{univex::viewport::kAxisColorYUVE.r, univex::viewport::kAxisColorYUVE.g,
                    univex::viewport::kAxisColorYUVE.b};
    Vec3 axisColorZ{univex::viewport::kAxisColorZUVE.r, univex::viewport::kAxisColorZUVE.g,
                    univex::viewport::kAxisColorZUVE.b};
    Vec3 planeColor{0.933f, 0.945f, 0.965f};
    Vec3 freeRingColor{0.906f, 0.918f, 0.949f};
    Vec3 centerColor{0.643f, 0.678f, 0.749f};

    // ---- line weights, in pixels -----------------------------------------
    // Kept deliberately light. A gizmo is read, not admired: past about two pixels a stroke stops
    // looking precise and starts looking drawn on, and the rotate rings suffer worst because three
    // of them cross in a small area. These sit close to what ImGuizmo and Unreal use.
    float axisLineWidthPx = 2.0f;
    float ringLineWidthPx = 2.0f;
    float freeRingWidthPx = 1.3f;
    float cubeEdgeWidthPx = 0.9f;
    float centerCubeWidthPx = 1.1f;

    // ---- move gizmo -------------------------------------------------------
    float moveShaftStart = 0.18f;
    float moveShaftEnd = 1.28f;
    float moveConeLength = 0.34f;
    float moveConeRadius = 0.105f;
    int   moveConeSegments = 28;

    float planeHandleOffset = 0.42f;
    float planeHandleSize = 0.30f;
    float planeHandleAlpha = 0.22f;

    // ---- rotate gizmo -----------------------------------------------------
    float ringRadius = 1.30f;
    float freeRingRadius = 1.55f;
    int   ringSegments = 96;

    // ---- scale gizmo ------------------------------------------------------
    float scaleShaftStart = 0.18f;
    float scaleShaftEnd = 1.34f;
    float scaleBoxSize = 0.19f;
    float scalePlaneOffset = 0.60f;
    float scalePlanePull = 0.28f;

    // ---- universal (all-in-one) gizmo -------------------------------------
    // Retuned after the compact version read as one cramped blob: the move
    // arrow reaches noticeably further out, the scale cube sits well past
    // its tip instead of touching it, the rotate ring pulls in closer to
    // the pivot, and every line is thinner so the three tools on one axis
    // stay separately readable.
    float universalRingRadius = 0.72f;
    float universalShaftStart = 0.18f;
    float universalShaftEnd = 1.24f;
    float universalConeLength = 0.30f;
    float universalConeRadius = 0.090f;
    float universalScaleBoxOffset = 1.86f;
    float universalScaleBoxSize = 0.165f;
    float universalLineWidthPx = 2.0f;
    float universalRingWidthPx = 2.0f;

    // ---- selected-object stand-in ----------------------------------------
    float centerCubeSize = 0.20f;

    // ---- orientation (nav) gizmo -----------------------------------------
    // The widget was previously 72 px across, which left each axis letter about 9 px tall drawn
    // with 2 px strokes - a quarter of the glyph's own height, so the three letters closed up into
    // unreadable blobs. Legible vector type wants a stroke nearer a tenth of its height, which
    // needs either thinner strokes or more room; at this size both are available, and the widget
    // still occupies a modest corner of the viewport.
    float navPixelSize = 118.f;   // side of the square corner viewport, px
    float navMarginPx = 16.f;
    float navAxisLineWidthPx = 1.8f;
    float navBallRadius = 0.34f;  // radius of the axis end balls
    int   navBallSegments = 48;

    // Axis letters on the positive balls, drawn as vector strokes (no font
    // dependency for three glyphs) sized as a fraction of the ball radius.
    // A vector stroke needs a solid core to read, not just coverage: below about 1.5 px the
    // fragment shader's analytic edge fade eats the whole width and the glyph breaks into
    // fragments. 1.7 px against a ~17 px glyph is both solid and proportionate.
    float navLabelScale = 0.62f;
    float navLabelWidthPx = 2.1f;
    Vec3  navLabelColor{0.043f, 0.051f, 0.074f}; // dark, to read on the bright balls
};

} // namespace univex::gizmo
