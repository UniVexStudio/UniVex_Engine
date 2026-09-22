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
    // Brighter than it was as a cube: a 1.2 px ring has a fraction of a solid block's ink, so the
    // same grey that read as a body reads as a smudge as an outline.
    Vec3 centerColor{0.827f, 0.855f, 0.902f};

    // ---- line weights, in pixels -----------------------------------------
    // Kept deliberately light. A gizmo is read, not admired: past about two pixels a stroke stops
    // looking precise and starts looking drawn on, and the rotate rings suffer worst because three
    // of them cross in a small area. These sit close to what ImGuizmo and Unreal use.
    float axisLineWidthPx = 1.5f;
    float ringLineWidthPx = 1.6f;
    float freeRingWidthPx = 1.1f;
    float cubeEdgeWidthPx = 0.9f;
    // The plane chips' outlines. Was a bare 1.1f repeated five times inside the geometry builders;
    // a number that decides how the widget looks belongs with the rest of the look.
    float planeHandleEdgeWidthPx = 1.1f;

    // ---- move gizmo -------------------------------------------------------
    float moveShaftStart = 0.18f;
    float moveShaftEnd = 1.28f;
    float moveConeLength = 0.34f;
    float moveConeRadius = 0.105f;
    int   moveConeSegments = 28;

    // Plane chips: smaller and firmer, not larger and fainter. A big wash at 0.22 alpha reads as a
    // smudge you are not sure is interactive; a small chip at 0.35 reads as a button. Pulling the
    // offset in as well keeps the three chips inside the rings instead of crowding the arrow heads.
    float planeHandleOffset = 0.36f;
    float planeHandleSize = 0.22f;
    float planeHandleAlpha = 0.35f;

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
    float universalLineWidthPx = 1.5f;
    float universalRingWidthPx = 1.5f;

    // ---- pivot dot -------------------------------------------------------
    // The visual for the Uniform (free-move / uniform-scale) handle, and the only thing Select
    // mode draws.
    //
    // This was a solid cube, which no production editor draws: a grey block in the middle of the
    // widget hides whatever sits behind it and reads as a fourth piece of geometry competing with
    // the three axes. A thin ring marks the same spot while claiming no volume.
    //
    // In PIXELS, unlike the rest of the gizmo's shape constants, which are in abstract gizmo units.
    // A dot is the one part that must not grow with the widget - at a world size it swells into a
    // disc as you zoom in on a small object. GizmoPicking derives the Uniform hit radius from
    // pivotDotRadiusPx too, so the clickable area can never drift from what is drawn.
    float pivotDotRadiusPx = 5.0f;
    float pivotDotWidthPx = 1.2f;
    int   pivotDotSegments = 24; // plenty for a 5 px circle; ringSegments would be 4x wasted work

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
