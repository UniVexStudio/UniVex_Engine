#include <gtest/gtest.h>
#include "Viewport2D/Viewport2D.h"
#include <cmath>
#include <string>
#include <vector>

using namespace univex::viewport;

namespace {
constexpr float kPi = 3.14159265358979f;

struct RecordingRenderer : IViewportRenderer {
    int begins = 0, ends = 0, lines = 0, rects = 0, quads = 0, texts = 0;
    std::vector<std::string> strings;
    void beginFrame(int, int, Color) override { ++begins; }
    void drawLine(Vec2, Vec2, Color, float) override { ++lines; }
    void fillRect(Vec2, Vec2, Color) override { ++rects; }
    void fillQuad(Vec2, Vec2, Vec2, Vec2, Color) override { ++quads; }
    void drawText(Vec2, const char* t, Color, float, TextOrient) override { ++texts; strings.push_back(t); }
    void endFrame() override { ++ends; }
};

// Identity-ish camera: 800x600, zoom 1, screen == world + offset so screen (400,300) is world origin.
Camera2D makeCam(bool yUp = false) {
    Camera2D c; c.setYUp(yUp); c.setViewportSize(800, 600); return c;
}
// Camera where world == screen (center at viewport middle).
Camera2D identityCam() {
    Camera2D c = makeCam(); c.setCenter({400.f, 300.f}); return c;
}
Viewport2D makeViewport() {
    Viewport2D v; v.setSize(800, 600); v.style().smoothNavigation = false; return v;
}
} // namespace

// ------------------------------------------------------------------ Camera2D
TEST(Camera2D, DefaultIsYDownLikeA2DEditor) {
    Camera2D c = makeCam();
    EXPECT_FALSE(c.yUp());
    EXPECT_GT(c.worldToScreen({0.f, 10.f}).y, c.worldToScreen({0.f, 0.f}).y);
}

TEST(Camera2D, RoundTripWorldScreen) {
    for (bool yUp : {true, false}) {
        Camera2D c = makeCam(yUp);
        c.setCenter({13.f, -7.f}); c.setZoom(3.5f);
        const Vec2 back = c.screenToWorld(c.worldToScreen({42.f, 11.f}));
        EXPECT_NEAR(back.x, 42.f, 1e-3f);
        EXPECT_NEAR(back.y, 11.f, 1e-3f);
    }
}

TEST(Camera2D, YUpPutsPositiveYAboveCenter) {
    Camera2D c = makeCam(true);
    EXPECT_LT(c.worldToScreen({0.f, 10.f}).y, c.worldToScreen({0.f, 0.f}).y);
}

TEST(Camera2D, ZoomAtKeepsPointUnderCursorFixed) {
    for (bool yUp : {true, false}) {
        Camera2D c = makeCam(yUp);
        const Vec2 cur{123.f, 456.f};
        const Vec2 before = c.screenToWorld(cur);
        c.zoomAt(cur, 2.5f);
        const Vec2 after = c.screenToWorld(cur);
        EXPECT_NEAR(before.x, after.x, 1e-3f);
        EXPECT_NEAR(before.y, after.y, 1e-3f);
    }
}

TEST(Camera2D, ZoomIsClamped) {
    Camera2D c = makeCam();
    c.zoomAt({0.f, 0.f}, 1e9f);  EXPECT_FLOAT_EQ(c.zoom(), Camera2D::kMaxZoom);
    c.zoomAt({0.f, 0.f}, 1e-9f); EXPECT_FLOAT_EQ(c.zoom(), Camera2D::kMinZoom);
}

TEST(Camera2D, PanFollowsCursor) {
    for (bool yUp : {true, false}) {
        Camera2D c = makeCam(yUp); c.setZoom(2.f);
        const Vec2 s0 = c.worldToScreen({10.f, 10.f});
        c.panByScreenDelta({30.f, -20.f});
        const Vec2 s1 = c.worldToScreen({10.f, 10.f});
        EXPECT_NEAR(s1.x - s0.x, 30.f, 1e-3f);
        EXPECT_NEAR(s1.y - s0.y, -20.f, 1e-3f);
    }
}

TEST(Camera2D, FitRectContainsRect) {
    Camera2D c = makeCam();
    c.fitRect({{-100.f, -50.f}, {300.f, 150.f}}, 20.f);
    const Rect vis = c.visibleWorldRect();
    EXPECT_LE(vis.min.x, -100.f); EXPECT_GE(vis.max.x, 300.f);
    EXPECT_LE(vis.min.y, -50.f);  EXPECT_GE(vis.max.y, 150.f);
}

TEST(Camera2D, SetCenterFromAnchorPinsWorldPointToScreenPoint) {
    for (bool yUp : {true, false}) {
        Camera2D c = makeCam(yUp); c.setZoom(3.f);
        c.setCenterFromAnchor({50.f, -20.f}, {210.f, 90.f});
        const Vec2 s = c.worldToScreen({50.f, -20.f});
        EXPECT_NEAR(s.x, 210.f, 1e-3f); EXPECT_NEAR(s.y, 90.f, 1e-3f);
    }
}

TEST(Camera2D, AlignToPixelsPutsOriginOnWholePixel) {
    for (bool yUp : {true, false}) {
        Camera2D c = makeCam(yUp); c.setZoom(3.7f); c.setCenter({13.3f, -7.1f});
        c.alignToPixels();
        const Vec2 o = c.worldToScreen({0.f, 0.f});
        EXPECT_NEAR(o.x, std::round(o.x), 1e-3f);
        EXPECT_NEAR(o.y, std::round(o.y), 1e-3f);
    }
}

TEST(Camera2D, ViewportSizeNeverZero) {
    Camera2D c; c.setViewportSize(0, -5);
    EXPECT_EQ(c.widthPx(), 1); EXPECT_EQ(c.heightPx(), 1);
}

// ------------------------------------------------------------- grid + rulers
TEST(Grid, LevelsAreDecadeRelatedAndDensityBounded) {
    for (float z : {0.05f, 0.3f, 1.f, 7.f, 123.f, 2000.f}) {
        const GridLevels g = computeGridLevels(z, 48.f, 0.f);
        EXPECT_NEAR(g.major / g.minor, 10.f, 1e-3f);
        EXPECT_GE(g.minor * z, 4.7f);
        EXPECT_LE(g.minor * z, 48.f + 1e-2f);
        EXPECT_GE(g.minorAlpha, 0.f); EXPECT_LE(g.minorAlpha, 1.f);
    }
}

TEST(Grid, MinorAlphaGrowsMonotonicallyAsYouZoomIn) {
    float prev = -1.f;
    for (float z = 1.f; z <= 4.8f; z += 0.1f) {   // inside one decade
        const float a = computeGridLevels(z, 48.f, 0.f).minorAlpha;
        EXPECT_GE(a, prev - 1e-5f); prev = a;
    }
}

TEST(Grid, NoPoppingAcrossDecadeBoundary) {
    // The old major lines become the new minor lines: same world step, same on-screen spacing.
    const float zJustBelow = 48.f / 9.999f, zJustAbove = 48.f / 10.001f;
    const GridLevels a = computeGridLevels(zJustBelow, 48.f, 0.f);
    const GridLevels b = computeGridLevels(zJustAbove, 48.f, 0.f);
    EXPECT_NEAR(a.major, b.minor, 1e-3f);
}

TEST(Grid, MinStepClampsToGamePixel) {
    const GridLevels g = computeGridLevels(60.f, 48.f, 1.f);
    EXPECT_FLOAT_EQ(g.minor, 1.f);
    EXPECT_FLOAT_EQ(g.minorAlpha, 1.f);
}

TEST(Ruler, TicksAreNiceAndLabeledOnLabelStep) {
    const RulerScale rs = computeRulerTicks(0.f, 200.f, 1.f, 80.f);
    EXPECT_NEAR(rs.labelStep, 100.f, 1e-3f);
    EXPECT_EQ(rs.decimals, 0);
    EXPECT_EQ(rs.ticks.size(), 11u);
    int labeled = 0; for (auto& t : rs.ticks) if (t.labeled) ++labeled;
    EXPECT_EQ(labeled, 3);
}

TEST(Ruler, FractionalStepsUseDecimals) {
    const RulerScale rs = computeRulerTicks(0.f, 5.f, 100.f, 90.f);   // ~0.9 world units per label
    EXPECT_NEAR(rs.labelStep, 1.f, 1e-3f);
    const RulerScale fine = computeRulerTicks(0.f, 1.f, 1000.f, 90.f);
    EXPECT_GE(fine.decimals, 1);
}

TEST(Ruler, FormatNeverPrintsNegativeZero) {
    EXPECT_EQ(formatRulerValue(-0.001f, 0), "0");
    EXPECT_EQ(formatRulerValue(12.5f, 1), "12.5");
    EXPECT_EQ(formatRulerValue(-3.f, 0), "-3");
}

TEST(Ruler, TooManyTicksReturnsNothing) {
    EXPECT_TRUE(computeRulerTicks(0.f, 1e9f, 1.f, 80.f, 100).ticks.empty());
}

// ------------------------------------------------------------------ snapping
TEST(Snap, GridPixelAndDisabled) {
    Guides g; SnapSettings s; s.guides = false;
    Vec2 p = snapPoint({20.f, 25.f}, s, g, 1.f);
    EXPECT_FLOAT_EQ(p.x, 20.f); EXPECT_FLOAT_EQ(p.y, 25.f);
    s.grid = true; s.gridStep = 16.f;
    p = snapPoint({20.f, 25.f}, s, g, 1.f);
    EXPECT_FLOAT_EQ(p.x, 16.f); EXPECT_FLOAT_EQ(p.y, 32.f);
    s.grid = false; s.pixel = true;
    p = snapPoint({3.4f, 2.6f}, s, g, 1.f);
    EXPECT_FLOAT_EQ(p.x, 3.f); EXPECT_FLOAT_EQ(p.y, 3.f);
}

TEST(Snap, GuidesBeatGridAndRespectScreenDistance) {
    Guides g; g.vertical = {50.f};
    SnapSettings s; s.grid = true; s.gridStep = 16.f;
    EXPECT_FLOAT_EQ(snapPoint({52.f, 0.f}, s, g, 1.f).x, 50.f);     // 2px away -> guide
    EXPECT_FLOAT_EQ(snapPoint({58.f, 0.f}, s, g, 1.f).x, 50.f);     // exactly 8px: boundary is inclusive
    EXPECT_FLOAT_EQ(snapPoint({58.5f, 0.f}, s, g, 1.f).x, 64.f);    // 8.5px: out of guide range -> grid
    EXPECT_FLOAT_EQ(snapPoint({50.5f, 0.f}, s, g, 100.f).x, 48.f);  // zoomed in: 50px away on screen -> grid
}

// ------------------------------------------------------------ TransformGizmo
TEST(Gizmo, HitTestFindsBodyCornersAndRotateHandle) {
    const Camera2D cam = identityCam();
    TransformGizmo g; g.setBox({{100.f, 100.f}, {40.f, 20.f}, 0.f, {}});
    EXPECT_TRUE(g.hitTest({100.f, 100.f}, cam).kind == Handle::Kind::Body);
    const Handle c = g.hitTest({80.f, 90.f}, cam);
    EXPECT_TRUE(c.kind == Handle::Kind::Scale); EXPECT_EQ(c.hx, -1); EXPECT_EQ(c.hy, -1);
    EXPECT_TRUE(g.hitTest({100.f, 62.f}, cam).kind == Handle::Kind::Rotate);   // 28px above top edge (Y-down)
    EXPECT_TRUE(g.hitTest({300.f, 300.f}, cam).kind == Handle::Kind::None);
}

TEST(Gizmo, PivotOnlyHitWhenEditingPivot) {
    const Camera2D cam = identityCam();
    TransformGizmo g; g.setBox({{100.f, 100.f}, {40.f, 20.f}, 0.f, {}});
    EXPECT_TRUE(g.hitTest({100.f, 100.f}, cam).kind == Handle::Kind::Body);
    g.setPivotEditing(true);
    EXPECT_TRUE(g.hitTest({100.f, 100.f}, cam).kind == Handle::Kind::Pivot);
}

TEST(Gizmo, TinyBoxHidesEdgeHandles) {
    const Camera2D cam = identityCam();
    TransformGizmo g; g.setBox({{100.f, 100.f}, {10.f, 10.f}, 0.f, {}});
    // Edge midpoint of a 10px box would be inside the corner handles' reach; edge handles are disabled.
    const Handle h = g.hitTest({100.f, 95.f}, cam);
    EXPECT_FALSE(h.kind == Handle::Kind::Scale && (h.hx == 0 || h.hy == 0));
}

TEST(Gizmo, MoveFollowsMouseAndSnapsCorner) {
    const Camera2D cam = identityCam();
    Guides gd; SnapSettings s; s.guides = false;
    TransformGizmo g; g.setBox({{100.f, 100.f}, {40.f, 20.f}, 0.f, {}});
    g.beginDrag({Handle::Kind::Body, 0, 0}, {100.f, 100.f}, cam);
    Box b = g.updateDrag({130.f, 110.f}, cam, s, gd, {});
    EXPECT_NEAR(b.center.x, 130.f, 1e-3f); EXPECT_NEAR(b.center.y, 110.f, 1e-3f);
    s.grid = true; s.gridStep = 16.f;                // min corner (80,90)+(30,10) = (110,100) -> (112,96)
    b = g.updateDrag({130.f, 110.f}, cam, s, gd, {});
    EXPECT_NEAR(b.center.x, 132.f, 1e-3f); EXPECT_NEAR(b.center.y, 106.f, 1e-3f);
}

TEST(Gizmo, ShiftLocksMoveToDominantAxis) {
    const Camera2D cam = identityCam();
    Guides gd; SnapSettings s; s.guides = false;
    TransformGizmo g; g.setBox({{100.f, 100.f}, {40.f, 20.f}, 0.f, {}});
    g.beginDrag({Handle::Kind::Body, 0, 0}, {100.f, 100.f}, cam);
    const Box b = g.updateDrag({150.f, 110.f}, cam, s, gd, {true, false});
    EXPECT_NEAR(b.center.x, 150.f, 1e-3f); EXPECT_NEAR(b.center.y, 100.f, 1e-3f);
}

TEST(Gizmo, ScaleFromCornerKeepsOppositeCornerFixed) {
    const Camera2D cam = identityCam();
    Guides gd; SnapSettings s; s.guides = false;
    TransformGizmo g; g.setBox({{100.f, 100.f}, {40.f, 20.f}, 0.f, {}});
    g.beginDrag({Handle::Kind::Scale, 1, 1}, {120.f, 110.f}, cam);
    const Box b = g.updateDrag({140.f, 130.f}, cam, s, gd, {});
    EXPECT_NEAR(b.size.x, 60.f, 1e-3f); EXPECT_NEAR(b.size.y, 40.f, 1e-3f);
    EXPECT_NEAR(b.center.x, 110.f, 1e-3f); EXPECT_NEAR(b.center.y, 110.f, 1e-3f);
    EXPECT_NEAR(b.center.x - b.size.x * 0.5f, 80.f, 1e-3f);   // top-left corner unchanged
    EXPECT_NEAR(b.center.y - b.size.y * 0.5f, 90.f, 1e-3f);
}

TEST(Gizmo, AltScalesFromCenterAndShiftKeepsAspect) {
    const Camera2D cam = identityCam();
    Guides gd; SnapSettings s; s.guides = false;
    TransformGizmo g; g.setBox({{100.f, 100.f}, {40.f, 20.f}, 0.f, {}});
    g.beginDrag({Handle::Kind::Scale, 1, 1}, {120.f, 110.f}, cam);
    Box b = g.updateDrag({140.f, 130.f}, cam, s, gd, {false, true});
    EXPECT_NEAR(b.size.x, 80.f, 1e-3f); EXPECT_NEAR(b.size.y, 60.f, 1e-3f);
    EXPECT_NEAR(b.center.x, 100.f, 1e-3f); EXPECT_NEAR(b.center.y, 100.f, 1e-3f);
    b = g.updateDrag({140.f, 130.f}, cam, s, gd, {true, false});
    EXPECT_NEAR(b.size.x / b.size.y, 2.f, 1e-3f);
    EXPECT_NEAR(b.size.x, 80.f, 1e-3f);
    EXPECT_NEAR(b.center.x, 120.f, 1e-3f); EXPECT_NEAR(b.center.y, 110.f, 1e-3f);
}

TEST(Gizmo, ScaleNeverCollapsesBelowMinimum) {
    const Camera2D cam = identityCam();
    Guides gd; SnapSettings s; s.guides = false;
    TransformGizmo g; g.setBox({{100.f, 100.f}, {40.f, 20.f}, 0.f, {}});
    g.beginDrag({Handle::Kind::Scale, 1, 0}, {120.f, 100.f}, cam);
    const Box b = g.updateDrag({-500.f, 100.f}, cam, s, gd, {});   // dragged far past the opposite edge
    EXPECT_GT(b.size.x, 0.f);
}

TEST(Gizmo, RotateAroundCenterAndSnapWithShift) {
    const Camera2D cam = identityCam();
    Guides gd; SnapSettings s; s.guides = false;
    TransformGizmo g; g.setBox({{100.f, 100.f}, {40.f, 20.f}, 0.f, {}});
    g.beginDrag({Handle::Kind::Rotate, 0, 0}, {100.f, 62.f}, cam);   // handle straight above
    Box b = g.updateDrag({140.f, 100.f}, cam, s, gd, {});            // moved to the right: +90 degrees
    EXPECT_NEAR(b.rotation, kPi * 0.5f, 1e-4f);
    EXPECT_NEAR(b.center.x, 100.f, 1e-3f); EXPECT_NEAR(b.center.y, 100.f, 1e-3f);

    const float a = -kPi * 0.5f + 0.3f;                               // +0.3 rad from start
    b = g.updateDrag({100.f + 40.f * std::cos(a), 100.f + 40.f * std::sin(a)}, cam, s, gd, {true, false});
    EXPECT_NEAR(b.rotation, 15.f * kPi / 180.f, 1e-4f);               // shift snaps to 15 degrees
}

TEST(Gizmo, RotatingAroundOffsetPivotKeepsPivotFixedInWorld) {
    const Camera2D cam = identityCam();
    Guides gd; SnapSettings s; s.guides = false;
    TransformGizmo g; g.setBox({{100.f, 100.f}, {40.f, 20.f}, 0.f, {10.f, 0.f}});
    const Vec2 pivotBefore = g.box().pivotWorld();                    // (110,100)
    const Vec2 hp = {110.f, 62.f};
    g.beginDrag({Handle::Kind::Rotate, 0, 0}, hp, cam);
    const Box b = g.updateDrag({110.f + 38.f, 100.f}, cam, s, gd, {});
    const Vec2 pivotAfter = b.pivotWorld();
    EXPECT_NEAR(pivotAfter.x, pivotBefore.x, 1e-3f);
    EXPECT_NEAR(pivotAfter.y, pivotBefore.y, 1e-3f);
    EXPECT_NEAR(b.center.x, 110.f, 1e-3f); EXPECT_NEAR(b.center.y, 90.f, 1e-3f);
}

TEST(Gizmo, PivotDragMovesLocalOffsetOnly) {
    const Camera2D cam = identityCam();
    Guides gd; SnapSettings s; s.guides = false;
    TransformGizmo g; g.setPivotEditing(true); g.setBox({{100.f, 100.f}, {40.f, 20.f}, 0.f, {}});
    g.beginDrag({Handle::Kind::Pivot, 0, 0}, {100.f, 100.f}, cam);
    const Box b = g.updateDrag({110.f, 100.f}, cam, s, gd, {});
    EXPECT_NEAR(b.pivotOffset.x, 10.f, 1e-3f); EXPECT_NEAR(b.pivotOffset.y, 0.f, 1e-3f);
    EXPECT_NEAR(b.center.x, 100.f, 1e-3f);
}

TEST(Gizmo, ScaleOfRotatedBoxStaysAnchoredInLocalSpace) {
    const Camera2D cam = identityCam();
    Guides gd; SnapSettings s; s.guides = false;
    Box start{{100.f, 100.f}, {40.f, 20.f}, kPi * 0.5f, {}};
    TransformGizmo g; g.setBox(start);
    const Vec2 anchorBefore = start.localToWorld({-20.f, -10.f});     // opposite of handle (+1,+1)
    const Vec2 handle = start.localToWorld({20.f, 10.f});
    g.beginDrag({Handle::Kind::Scale, 1, 1}, handle, cam);
    const Vec2 target = start.localToWorld({50.f, 30.f});
    const Box b = g.updateDrag(target, cam, s, gd, {});
    const Vec2 anchorAfter = b.localToWorld({-b.size.x * 0.5f, -b.size.y * 0.5f});
    EXPECT_NEAR(anchorAfter.x, anchorBefore.x, 1e-3f);
    EXPECT_NEAR(anchorAfter.y, anchorBefore.y, 1e-3f);
}

// ---------------------------------------------------------------- Viewport2D
TEST(Viewport2D, ClickPicksButDragDoesNot) {
    Viewport2D v = makeViewport();
    std::vector<Vec2> worlds; int marquees = 0;
    v.setOnPick([&](Vec2 w, Vec2, bool) { worlds.push_back(w); });
    v.setOnMarquee([&](const Rect&, bool, Phase) { ++marquees; });
    v.onMouseDown(MouseButton::Left, {400.f, 300.f});
    v.onMouseUp(MouseButton::Left, {400.f, 300.f});
    ASSERT_EQ(worlds.size(), 1u);
    EXPECT_NEAR(worlds[0].x, 0.f, 1e-4f); EXPECT_NEAR(worlds[0].y, 0.f, 1e-4f);
    EXPECT_EQ(marquees, 0);
    v.onMouseDown(MouseButton::Left, {400.f, 300.f});
    v.onMouseMove({440.f, 300.f});
    v.onMouseUp(MouseButton::Left, {440.f, 300.f});
    EXPECT_EQ(worlds.size(), 1u);
    EXPECT_GT(marquees, 0);
}

TEST(Viewport2D, MarqueeReportsNormalizedWorldRectWithPhases) {
    Viewport2D v = makeViewport();
    Rect last{}; bool additive = true; std::vector<Phase> phases;
    v.setOnMarquee([&](const Rect& r, bool a, Phase p) { last = r; additive = a; phases.push_back(p); });
    v.onMouseDown(MouseButton::Left, {300.f, 260.f});
    v.onMouseMove({200.f, 200.f});          // drag up-left: rect must still be normalized
    v.onMouseUp(MouseButton::Left, {200.f, 200.f});
    EXPECT_NEAR(last.min.x, -200.f, 1e-3f); EXPECT_NEAR(last.min.y, -100.f, 1e-3f);
    EXPECT_NEAR(last.max.x, -100.f, 1e-3f); EXPECT_NEAR(last.max.y, -40.f, 1e-3f);
    EXPECT_FALSE(additive);
    ASSERT_GE(phases.size(), 2u);
    EXPECT_TRUE(phases.front() == Phase::Begin); EXPECT_TRUE(phases.back() == Phase::End);
}

TEST(Viewport2D, ShiftMakesMarqueeAndPickAdditive) {
    Viewport2D v = makeViewport();
    bool pickAdd = false, marqAdd = false;
    v.setOnPick([&](Vec2, Vec2, bool a) { pickAdd = a; });
    v.setOnMarquee([&](const Rect&, bool a, Phase) { marqAdd = a; });
    v.setModifiers({true, false, false});
    v.onMouseDown(MouseButton::Left, {400.f, 300.f}); v.onMouseUp(MouseButton::Left, {400.f, 300.f});
    v.onMouseDown(MouseButton::Left, {400.f, 300.f}); v.onMouseMove({480.f, 360.f}); v.onMouseUp(MouseButton::Left, {480.f, 360.f});
    EXPECT_TRUE(pickAdd); EXPECT_TRUE(marqAdd);
}

TEST(Viewport2D, MiddleDragPansAndHandToolPansWithoutPicking) {
    Viewport2D v = makeViewport(); int picks = 0;
    v.setOnPick([&](Vec2, Vec2, bool) { ++picks; });
    const Vec2 s0 = v.camera().worldToScreen({0.f, 0.f});
    v.onMouseDown(MouseButton::Middle, {100.f, 100.f});
    EXPECT_TRUE(v.isPanning());
    v.onMouseMove({140.f, 130.f}); v.onMouseUp(MouseButton::Middle, {140.f, 130.f});
    EXPECT_FALSE(v.isPanning());
    const Vec2 s1 = v.camera().worldToScreen({0.f, 0.f});
    EXPECT_NEAR(s1.x - s0.x, 40.f, 1e-3f); EXPECT_NEAR(s1.y - s0.y, 30.f, 1e-3f);

    v.setTool(Tool::Hand);
    v.onMouseDown(MouseButton::Left, {300.f, 300.f}); v.onMouseMove({320.f, 300.f}); v.onMouseUp(MouseButton::Left, {320.f, 300.f});
    EXPECT_EQ(picks, 0);
    EXPECT_NEAR(v.camera().worldToScreen({0.f, 0.f}).x - s1.x, 20.f, 1e-3f);

    v.setTool(Tool::Select); v.setPanModifier(true);   // Space + left drag
    v.onMouseDown(MouseButton::Left, {300.f, 300.f}); v.onMouseMove({310.f, 300.f}); v.onMouseUp(MouseButton::Left, {310.f, 300.f});
    EXPECT_EQ(picks, 0);
}

TEST(Viewport2D, DragGuideFromTopRulerCreatesHorizontalGuide) {
    Viewport2D v = makeViewport();
    v.onMouseDown(MouseButton::Left, {100.f, 10.f});            // inside the 24px top ruler
    v.onMouseMove({100.f, 200.f});
    v.onMouseUp(MouseButton::Left, {100.f, 200.f});
    ASSERT_EQ(v.guides().horizontal.size(), 1u);
    EXPECT_NEAR(v.guides().horizontal[0], -100.f, 1e-3f);        // screen y 200 -> world y -100
    EXPECT_TRUE(v.guides().vertical.empty());
}

TEST(Viewport2D, DragGuideFromLeftRulerCreatesVerticalGuideAndHonorsGridSnap) {
    Viewport2D v = makeViewport();
    v.snap().grid = true; v.snap().gridStep = 16.f;
    v.onMouseDown(MouseButton::Left, {10.f, 100.f});
    v.onMouseMove({205.f, 100.f});                               // world x = -195 -> snaps to -192
    v.onMouseUp(MouseButton::Left, {205.f, 100.f});
    ASSERT_EQ(v.guides().vertical.size(), 1u);
    EXPECT_NEAR(v.guides().vertical[0], -192.f, 1e-3f);
}

TEST(Viewport2D, ReleasingNewGuideOverRulerCancelsIt) {
    Viewport2D v = makeViewport();
    v.onMouseDown(MouseButton::Left, {100.f, 10.f});
    v.onMouseMove({100.f, 200.f}); v.onMouseMove({100.f, 8.f});
    v.onMouseUp(MouseButton::Left, {100.f, 8.f});
    EXPECT_TRUE(v.guides().horizontal.empty());
}

TEST(Viewport2D, ExistingGuideCanBeMovedAndDeletedByDraggingBackToRuler) {
    Viewport2D v = makeViewport();
    v.guides().horizontal.push_back(0.f);                        // screen y = 300
    v.onMouseDown(MouseButton::Left, {400.f, 302.f});            // within 4px
    v.onMouseMove({400.f, 350.f});
    v.onMouseUp(MouseButton::Left, {400.f, 350.f});
    ASSERT_EQ(v.guides().horizontal.size(), 1u);
    EXPECT_NEAR(v.guides().horizontal[0], 50.f, 1e-3f);
    v.onMouseDown(MouseButton::Left, {400.f, 350.f});
    v.onMouseMove({400.f, 5.f});
    v.onMouseUp(MouseButton::Left, {400.f, 5.f});
    EXPECT_TRUE(v.guides().horizontal.empty());
}

TEST(Viewport2D, RulersCanBeDisabledSoTopEdgeIsCanvas) {
    Viewport2D v = makeViewport(); v.style().showRulers = false;
    int picks = 0; v.setOnPick([&](Vec2, Vec2, bool) { ++picks; });
    v.onMouseDown(MouseButton::Left, {100.f, 5.f}); v.onMouseUp(MouseButton::Left, {100.f, 5.f});
    EXPECT_EQ(picks, 1);
    EXPECT_TRUE(v.guides().horizontal.empty());
}

TEST(Viewport2D, GizmoDragFiresBeginUpdateEndAndSuppressesPick) {
    Viewport2D v = makeViewport();
    v.camera().setCenter({400.f, 300.f});                        // world == screen
    v.setSelectionBox({{300.f, 300.f}, {60.f, 40.f}, 0.f, {}});
    int picks = 0; std::vector<Phase> ph; Box last;
    v.setOnPick([&](Vec2, Vec2, bool) { ++picks; });
    v.setOnTransform([&](const Box& b, Phase p) { ph.push_back(p); last = b; });
    v.onMouseDown(MouseButton::Left, {300.f, 300.f});
    v.onMouseMove({340.f, 330.f});
    v.onMouseUp(MouseButton::Left, {340.f, 330.f});
    EXPECT_EQ(picks, 0);
    ASSERT_EQ(ph.size(), 3u);
    EXPECT_TRUE(ph[0] == Phase::Begin); EXPECT_TRUE(ph[1] == Phase::Update); EXPECT_TRUE(ph[2] == Phase::End);
    EXPECT_NEAR(last.center.x, 340.f, 1e-3f); EXPECT_NEAR(last.center.y, 330.f, 1e-3f);
    EXPECT_FALSE(v.gizmo().dragging());
}

TEST(Viewport2D, ClickOutsideSelectionStillPicks) {
    Viewport2D v = makeViewport(); v.camera().setCenter({400.f, 300.f});
    v.setSelectionBox({{300.f, 300.f}, {60.f, 40.f}, 0.f, {}});
    int picks = 0; v.setOnPick([&](Vec2, Vec2, bool) { ++picks; });
    v.onMouseDown(MouseButton::Left, {600.f, 500.f}); v.onMouseUp(MouseButton::Left, {600.f, 500.f});
    EXPECT_EQ(picks, 1);
}

TEST(Viewport2D, CursorHintTracksHoverAndDrag) {
    Viewport2D v = makeViewport(); v.camera().setCenter({400.f, 300.f});
    v.setSelectionBox({{300.f, 300.f}, {60.f, 40.f}, 0.f, {}});
    EXPECT_TRUE(v.cursorHint() == CursorHint::Default);
    v.onMouseMove({300.f, 300.f});  EXPECT_TRUE(v.cursorHint() == CursorHint::MoveAll);
    v.onMouseMove({300.f, 252.f});  EXPECT_TRUE(v.cursorHint() == CursorHint::Rotate);    // 28px above top edge (y=280)
    v.onMouseMove({330.f, 320.f});  EXPECT_TRUE(v.cursorHint() == CursorHint::ResizeNWSE); // bottom-right corner
    v.onMouseMove({330.f, 300.f});  EXPECT_TRUE(v.cursorHint() == CursorHint::ResizeEW);
    v.onMouseMove({300.f, 320.f});  EXPECT_TRUE(v.cursorHint() == CursorHint::ResizeNS);
    v.setTool(Tool::Hand);          EXPECT_TRUE(v.cursorHint() == CursorHint::Pan);
}

TEST(Viewport2D, WheelZoomsAtCursorImmediatelyWhenSmoothingOff) {
    Viewport2D v = makeViewport();
    const Vec2 cur{600.f, 200.f};
    const Vec2 before = v.camera().screenToWorld(cur);
    v.onWheel(3.f, cur);
    const Vec2 after = v.camera().screenToWorld(cur);
    EXPECT_GT(v.camera().zoom(), 1.f);
    EXPECT_NEAR(before.x, after.x, 1e-3f); EXPECT_NEAR(before.y, after.y, 1e-3f);
}

TEST(Viewport2D, SmoothWheelZoomConvergesAndKeepsCursorAnchored) {
    Viewport2D v; v.setSize(800, 600);
    const Vec2 cur{600.f, 200.f};
    const Vec2 before = v.camera().screenToWorld(cur);
    v.onWheel(1.f, cur); v.onWheel(1.f, cur);                    // accumulates: target = 1.1^2
    EXPECT_FLOAT_EQ(v.camera().zoom(), 1.f);                     // nothing moves until update()
    EXPECT_TRUE(v.isAnimating());
    v.update(0.016f);
    EXPECT_GT(v.camera().zoom(), 1.f); EXPECT_LT(v.camera().zoom(), 1.21f);   // in flight
    for (int i = 0; i < 200; ++i) v.update(0.016f);
    EXPECT_FALSE(v.isAnimating());
    EXPECT_NEAR(v.camera().zoom(), 1.21f, 1e-3f);
    const Vec2 after = v.camera().screenToWorld(cur);
    EXPECT_NEAR(before.x, after.x, 1e-2f); EXPECT_NEAR(before.y, after.y, 1e-2f);
}

TEST(Viewport2D, PanningCancelsAnimation) {
    Viewport2D v; v.setSize(800, 600);
    v.onWheel(2.f, {400.f, 300.f});
    ASSERT_TRUE(v.isAnimating());
    v.onMouseDown(MouseButton::Middle, {10.f, 10.f});
    EXPECT_FALSE(v.isAnimating());
}

TEST(Viewport2D, ZoomToFitAnimatesThenContainsRect) {
    Viewport2D v; v.setSize(800, 600);
    const Rect r{{0.f, 0.f}, {640.f, 360.f}};
    v.zoomToFit(r);
    EXPECT_TRUE(v.isAnimating());
    for (int i = 0; i < 300; ++i) v.update(0.016f);
    EXPECT_FALSE(v.isAnimating());
    const Rect vis = v.camera().visibleWorldRect();
    EXPECT_LE(vis.min.x, r.min.x); EXPECT_GE(vis.max.x, r.max.x);
    EXPECT_LE(vis.min.y, r.min.y); EXPECT_GE(vis.max.y, r.max.y);
}

TEST(Viewport2D, ZoomTo100IsPixelPerfect) {
    Viewport2D v = makeViewport();
    v.camera().setCenter({13.3f, 7.7f}); v.camera().setZoom(3.3f);
    v.zoomTo100();
    EXPECT_FLOAT_EQ(v.camera().zoom(), 1.f);
    const Vec2 o = v.camera().worldToScreen({0.f, 0.f});
    EXPECT_NEAR(o.x, std::round(o.x), 1e-3f); EXPECT_NEAR(o.y, std::round(o.y), 1e-3f);
}

TEST(Viewport2D, RenderIsBalancedDrawsChromeAndCallsScene) {
    Viewport2D v = makeViewport();
    bool sceneCalled = false;
    v.setDrawScene([&](IViewportRenderer&, const Camera2D&) { sceneCalled = true; });
    v.setCanvasRect(Rect{{0.f, 0.f}, {320.f, 180.f}});
    v.setSelectionBox({{100.f, 100.f}, {40.f, 20.f}, 0.f, {}});
    v.guides().vertical.push_back(10.f);
    RecordingRenderer r;
    v.onMouseMove({300.f, 200.f});
    v.render(r);
    EXPECT_EQ(r.begins, 1); EXPECT_EQ(r.ends, 1);
    EXPECT_GT(r.lines, 0); EXPECT_GT(r.rects, 0); EXPECT_GT(r.texts, 0);
    EXPECT_TRUE(sceneCalled);
    bool hud = false; for (auto& s : r.strings) if (s.rfind("X ", 0) == 0 && s.find("Z 100%") != std::string::npos) hud = true;
    EXPECT_TRUE(hud);
}

TEST(Viewport2D, HudAndRulersCanBeHidden) {
    Viewport2D v = makeViewport();
    v.style().showHud = false; v.style().showRulers = false;
    RecordingRenderer r; v.render(r);
    EXPECT_EQ(r.texts, 0);
}

TEST(Viewport2D, GridLineCountIsCapped) {
    Viewport2D v = makeViewport();
    v.camera().setZoom(Camera2D::kMinZoom);
    v.style().showAxes = false; v.style().showRulers = false; v.style().showHud = false;
    RecordingRenderer r; v.render(r);
    EXPECT_LE(r.lines, 4 * (v.style().maxGridLines + 1));
}
