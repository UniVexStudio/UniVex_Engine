// tests/ViewportTests.cpp
// -----------------------------------------------------------------------
// CPU-side checks for everything in this module that does not need a GPU:
// the matrix math, the camera, and the auto-adjusting grid's LOD decade
// selection (which the fragment shader and ComputeGridLod() implement
// identically, so pinning it here pins the shader's behaviour too).
//
// The ray-reconstruction section is worth calling out: it replays exactly
// what infinite_grid.vert and infinite_grid.frag do to find the ground
// point under a pixel — unproject the near and far plane, intersect with
// y = 0 — and then checks the result projects back to the pixel it came
// from. That is the grid's core geometry, verified without a rasterizer.
//
// The Perspective/LookAt reference numbers are the ones read back from
// the WebGL reference build (univex_viewport_grid.html) via Playwright in
// an earlier session; the camera convention here reproduces them exactly.
// -----------------------------------------------------------------------
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <utility>

#include "univex/camera/OrbitCamera.h"
#include "univex/camera/ViewportMetrics.h"
#include "univex/gizmo/GizmoDrag.h"
#include "univex/gizmo/GizmoPicking.h"
#include "univex/gizmo/GizmoGeometry.h"
#include "univex/gizmo/GizmoStyle.h"
#include "univex/gizmo/NavGizmo.h"
#include "univex/math/Mat4.h"
#include "univex/math/Vec.h"
#include "univex/render/GridSettings.h"

using univex::camera::OrbitCamera;
using univex::math::Mat4;
using univex::math::Vec3;
using univex::math::Vec4;
using univex::render::ComputeDisplayGridSpacing;
using univex::render::ComputeGridLod;
using univex::render::GridSettings;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (!condition) ++g_failures;
}

void CheckNear(float actual, float expected, float tolerance, const std::string& what) {
    const bool ok = std::fabs(actual - expected) <= tolerance;
    std::printf("[%s] %s (got %.6f, expected %.6f +/- %g)\n",
                ok ? "PASS" : "FAIL", what.c_str(),
                static_cast<double>(actual), static_cast<double>(expected),
                static_cast<double>(tolerance));
    if (!ok) ++g_failures;
}

constexpr float kPi = std::numbers::pi_v<float>;

// Replays the vertex shader's Unproject().
Vec3 Unproject(const Mat4& invViewProj, float clipX, float clipY, float clipZ) {
    return univex::math::PerspectiveDivide(invViewProj.Transform(Vec4{clipX, clipY, clipZ, 1.f}));
}

} // namespace

int main() {
    std::puts("== Mat4::Perspective — cross-checked against the WebGL reference build ==");
    {
        const Mat4 p = Mat4::Perspective(50.f * kPi / 180.f, 1280.f / 800.f, 0.05f, 20000.f);
        const float expected[16] = {
            1.3403167724609375f, 0.f, 0.f, 0.f,
            0.f, 2.1445069313049316f, 0.f, 0.f,
            0.f, 0.f, -1.0000050067901611f, -1.f,
            0.f, 0.f, -0.10000024735927582f, 0.f,
        };
        bool allMatch = true;
        for (int i = 0; i < 16; ++i) {
            if (std::fabs(p.m[static_cast<std::size_t>(i)] - expected[i]) > 1e-4f) allMatch = false;
        }
        Check(allMatch, "Perspective(50deg, 1280/800, 0.05, 20000) matches element-for-element");
    }

    std::puts("\n== Mat4::LookAt — cross-checked against the WebGL reference build ==");
    {
        const Vec3 eye{10.663887674305112f, -4.931839265851259f, -7.613045456688878f};
        const Mat4 v = Mat4::LookAt(eye, Vec3{0.f, 0.f, 0.f}, Vec3{0.f, 1.f, 0.f});
        const float expected[16] = {
            -0.5810351371765137f, 0.28670841455459595f, 0.7617062330245972f, 0.f,
            0.f, 0.9358968138694763f, -0.35227423906326294f, 0.f,
            -0.8138784766197205f, -0.20468372106552124f, -0.5437889695167542f, 0.f,
            1.7763568394002505e-15f, 2.220446049250313e-16f, -14.f, 1.f,
        };
        bool allMatch = true;
        for (int i = 0; i < 16; ++i) {
            if (std::fabs(v.m[static_cast<std::size_t>(i)] - expected[i]) > 1e-4f) allMatch = false;
        }
        Check(allMatch, "LookAt(reference eye, origin, +Y) matches element-for-element");
    }

    std::puts("\n== OrbitCamera::Eye reproduces the same orbit convention ==");
    {
        OrbitCamera camera;
        camera.SetYawPitch(-0.62f, -0.36f);
        camera.SetDistance(14.f);
        const Vec3 eye = camera.Eye();
        CheckNear(eye.x, 10.663887674305112f, 1e-4f, "eye.x");
        CheckNear(eye.y, -4.931839265851259f, 1e-4f, "eye.y");
        CheckNear(eye.z, -7.613045456688878f, 1e-4f, "eye.z");
    }

    std::puts("\n== Mat4::Inverse ==");
    {
        OrbitCamera camera;
        const Mat4 viewProj = camera.ViewProjection(1280.f / 800.f);
        const auto inverse = Mat4::Inverse(viewProj);
        Check(inverse.has_value(), "a normal view-projection is invertible");
        if (inverse.has_value()) {
            const Mat4 product = Mat4::Multiply(viewProj, *inverse);
            float worstError = 0.f;
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    const float expected = (r == c) ? 1.f : 0.f;
                    worstError = std::max(worstError, std::fabs(product.At(r, c) - expected));
                }
            }
            CheckNear(worstError, 0.f, 1e-3f, "M * inverse(M) is the identity");
        }
        Check(!Mat4::Inverse(Mat4{}).has_value(), "a singular (all-zero) matrix returns nullopt, not infinities");
    }

    std::puts("\n== Ray reconstruction: the exact geometry infinite_grid.{vert,frag} performs ==");
    {
        OrbitCamera camera;
        camera.SetYawPitch(-0.62f, 0.42f);
        camera.SetDistance(14.f);
        const float aspect = 1280.f / 800.f;
        const Mat4 viewProj = camera.ViewProjection(aspect);
        const Mat4 invViewProj = camera.InverseViewProjection(aspect);

        // A spread of pixels across the lower half of the screen, where the
        // ground plane is visible from this camera.
        const float samples[][2] = {{0.f, -0.5f}, {-0.7f, -0.8f}, {0.6f, -0.2f}, {0.f, -0.95f}};
        int hits = 0;
        float worstReprojectionError = 0.f;
        float worstPlaneError = 0.f;

        for (const auto& sample : samples) {
            const Vec3 nearPoint = Unproject(invViewProj, sample[0], sample[1], -1.f);
            const Vec3 farPoint = Unproject(invViewProj, sample[0], sample[1], 1.f);
            const Vec3 rayDir = farPoint - nearPoint;
            if (std::fabs(rayDir.y) < 1e-9f) continue;

            const float t = -nearPoint.y / rayDir.y;
            if (t <= 0.f || t >= 1.f) continue; // same rejection the shader does
            ++hits;

            const Vec3 world = nearPoint + rayDir * t;
            worstPlaneError = std::max(worstPlaneError, std::fabs(world.y));

            const Vec4 clip = viewProj.Transform(Vec4{world, 1.f});
            const Vec3 ndc = univex::math::PerspectiveDivide(clip);
            worstReprojectionError = std::max(worstReprojectionError,
                                              std::max(std::fabs(ndc.x - sample[0]),
                                                       std::fabs(ndc.y - sample[1])));
        }

        Check(hits == 4, "all four sample pixels hit the ground plane in front of the camera");
        CheckNear(worstPlaneError, 0.f, 1e-3f, "every hit lands exactly on y = 0");
        CheckNear(worstReprojectionError, 0.f, 1e-4f, "every hit projects back to the pixel it came from");
    }

    std::puts("\n== Ray reconstruction rejects pixels above the horizon ==");
    {
        OrbitCamera camera;
        camera.SetYawPitch(0.f, 0.05f); // almost horizontal, looking just over the ground
        camera.SetDistance(20.f);
        const float aspect = 16.f / 9.f;
        const Mat4 invViewProj = camera.InverseViewProjection(aspect);

        // Top of the screen: the ray goes up and away, so it must not
        // produce a ground hit (this is what makes the grid stop at the
        // horizon instead of wrapping around).
        const Vec3 nearPoint = Unproject(invViewProj, 0.f, 0.95f, -1.f);
        const Vec3 farPoint = Unproject(invViewProj, 0.f, 0.95f, 1.f);
        const float rayDirY = farPoint.y - nearPoint.y;
        const float t = -nearPoint.y / rayDirY;
        Check(t <= 0.f || t >= 1.f, "a pixel above the horizon yields no valid ground hit");
    }

    std::puts("\n== Auto-adjusting grid: LOD decade selection ==");
    {
        GridSettings settings; // baseSpacing 1.0, targetCellPixels 24
        const float base = settings.baseSpacing;
        const float target = settings.targetCellPixels;

        // Exactly at the decade boundary: one cell is exactly targetCellPixels.
        {
            const auto lod = ComputeGridLod(base / target, settings);
            CheckNear(lod.level, 0.f, 1e-4f, "worldPerPixel = base/target -> LOD level 0");
            CheckNear(lod.finestSpacing, 1.f, 1e-4f, "... finest spacing is the base spacing");
        }
        // One decade out.
        {
            const auto lod = ComputeGridLod(10.f * base / target, settings);
            CheckNear(lod.level, 1.f, 1e-4f, "10x further out -> LOD level 1");
            CheckNear(lod.finestSpacing, 10.f, 1e-3f, "... finest spacing steps to 10");
        }
        // Three decades out.
        {
            const auto lod = ComputeGridLod(1000.f * base / target, settings);
            CheckNear(lod.level, 3.f, 1e-4f, "1000x further out -> LOD level 3");
            CheckNear(lod.finestSpacing, 1000.f, 1e-1f, "... finest spacing steps to 1000");
        }
        // Zoomed far in: the grid must not subdivide below the base spacing.
        {
            const auto lod = ComputeGridLod(1e-6f, settings);
            CheckNear(lod.level, 0.f, 1e-6f, "zoomed way in -> LOD clamps at 0");
            CheckNear(lod.finestSpacing, settings.baseSpacing, 1e-6f,
                      "... and never draws finer than baseSpacing");
        }
        // Halfway through a decade the fade is halfway too.
        {
            const auto lod = ComputeGridLod(std::sqrt(10.f) * base / target, settings);
            CheckNear(lod.fade, 0.5f, 1e-3f, "sqrt(10) into the decade -> fade 0.5");
        }

        // Monotonicity across six decades of zoom: spacing must never shrink
        // as the camera pulls back.
        bool monotonic = true;
        float previousSpacing = 0.f;
        for (int exponent = -3; exponent <= 3; ++exponent) {
            const float worldPerPixel = std::pow(10.f, static_cast<float>(exponent)) * base / target;
            const float spacing = ComputeGridLod(worldPerPixel, settings).finestSpacing;
            if (spacing < previousSpacing - 1e-6f) monotonic = false;
            previousSpacing = spacing;
        }
        Check(monotonic, "finest spacing is monotonically non-decreasing across 6 decades of zoom");

        // The grid has two regimes, and they need separate assertions.
        //
        // ADAPTIVE REGIME (worldPerPixel >= baseSpacing/targetCellPixels):
        // the LOD is free to step decades, and the whole point of the
        // mechanism is that the grid the user sees holds a roughly constant
        // on-screen density. Tiers step by factors of ten and the reported
        // tier switches at the halfway point of a decade, so the displayed
        // cell is bounded to target/sqrt(10) .. target*sqrt(10) — a factor
        // of ~3.16 either way, and never more, no matter how far out.
        {
            float minCellPixels = 1e9f;
            float maxCellPixels = 0.f;
            for (int step = 0; step <= 600; ++step) {
                const float worldPerPixel = std::pow(10.f, 0.01f * static_cast<float>(step)) * base / target;
                const float spacing = ComputeDisplayGridSpacing(worldPerPixel, settings);
                const float cellPixels = spacing / worldPerPixel;
                minCellPixels = std::min(minCellPixels, cellPixels);
                maxCellPixels = std::max(maxCellPixels, cellPixels);
            }
            const float band = std::sqrt(10.f);
            std::printf("       adaptive regime, 6 decades of zoom-out: cell size %.1f .. %.1f px "
                        "(allowed %.1f .. %.1f)\n",
                        static_cast<double>(minCellPixels), static_cast<double>(maxCellPixels),
                        static_cast<double>(target / band), static_cast<double>(target * band));
            Check(minCellPixels >= target / band - 0.1f,
                  "adaptive regime: displayed cells never shrink past target/sqrt(10)");
            Check(maxCellPixels <= target * band + 0.1f,
                  "adaptive regime: displayed cells never grow past target*sqrt(10)");
        }

        // CLAMPED REGIME (zoomed in closer than the base spacing): the LOD
        // bottoms out, because a 1 m grid must not start drawing 10 cm
        // lines just because there is room for them. Cells legitimately
        // grow past the target here — that is the clamp working, not a
        // density failure — so what gets asserted is that the spacing stays
        // pinned at exactly baseSpacing however far in the camera goes.
        {
            bool alwaysBaseSpacing = true;
            float largestCellPixels = 0.f;
            for (int step = 0; step <= 300; ++step) {
                const float worldPerPixel = std::pow(10.f, -3.f + 0.01f * static_cast<float>(step)) * base / target;
                const float spacing = ComputeDisplayGridSpacing(worldPerPixel, settings);
                if (std::fabs(spacing - settings.baseSpacing) > 1e-6f) alwaysBaseSpacing = false;
                largestCellPixels = std::max(largestCellPixels, spacing / worldPerPixel);
            }
            std::printf("       clamped regime, 3 decades of zoom-in: cells reach %.0f px, "
                        "spacing stays at baseSpacing\n", static_cast<double>(largestCellPixels));
            Check(alwaysBaseSpacing,
                  "clamped regime: spacing stays pinned at baseSpacing, never subdividing below it");
        }
    }

    std::puts("\n== OrbitCamera: input, clamps, dynamic clip planes ==");
    {
        OrbitCamera camera;
        const float startYaw = camera.Yaw();
        camera.Orbit(100.f, 0.f);
        Check(camera.Yaw() > startYaw, "dragging right increases yaw");

        camera.SetYawPitch(0.f, 0.f);
        for (int i = 0; i < 500; ++i) camera.Orbit(0.f, 100.f);
        CheckNear(camera.Pitch(), camera.Settings().pitchMax, 1e-4f, "pitch clamps at pitchMax");
        for (int i = 0; i < 1000; ++i) camera.Orbit(0.f, -100.f);
        CheckNear(camera.Pitch(), camera.Settings().pitchMin, 1e-4f, "pitch clamps at pitchMin");

        camera.SetDistance(10.f);
        camera.Dolly(1.f);
        Check(camera.Distance() < 10.f, "scrolling up dollies in");
        camera.Dolly(-1.f);
        CheckNear(camera.Distance(), 10.f, 1e-3f, "dolly is exponential and exactly reversible");

        for (int i = 0; i < 2000; ++i) camera.Dolly(1.f);
        CheckNear(camera.Distance(), camera.Settings().distanceMin, 1e-4f, "dolly clamps at distanceMin");
        for (int i = 0; i < 4000; ++i) camera.Dolly(-1.f);
        CheckNear(camera.Distance(), camera.Settings().distanceMax, 1e-1f, "dolly clamps at distanceMax");

        // Dynamic clip planes: the near:far ratio should stay constant so
        // depth precision does not collapse when zoomed far out.
        camera.SetDistance(1.f);
        const float ratioNear = camera.FarPlane() / camera.NearPlane();
        camera.SetDistance(1000.f);
        const float ratioFar = camera.FarPlane() / camera.NearPlane();
        Check(camera.NearPlane() > 0.f && camera.NearPlane() < camera.FarPlane(),
              "near plane is positive and in front of the far plane");
        CheckNear(ratioFar / ratioNear, 1.f, 1e-3f,
                  "near:far ratio is preserved across a 1000x zoom range");

        // Panning slides the pivot without changing the orbit angles.
        OrbitCamera panCamera;
        const Vec3 before = panCamera.Target();
        panCamera.Pan(100.f, 0.f, 800);
        const Vec3 after = panCamera.Target();
        Check(std::fabs(after.x - before.x) + std::fabs(after.z - before.z) > 1e-4f,
              "panning moves the pivot");
        CheckNear(after.y, before.y, 1e-4f, "a horizontal pan does not lift the pivot");
    }

    std::puts("\n== Gizmo meshes: every mode builds real geometry ==");
    {
        using univex::gizmo::BuildGizmoMesh;
        using univex::gizmo::GizmoMode;
        using univex::gizmo::GizmoStyle;

        const GizmoStyle style;
        const Vec3 view = univex::math::Normalize(Vec3{-0.65f, -0.44f, 0.62f});
        constexpr float kUnitsPerPixel = 1.f / 82.f; // ~155 px radius over 1.9 units

        for (const auto mode : {GizmoMode::Move, GizmoMode::Rotate,
                                GizmoMode::Scale, GizmoMode::Universal}) {
            const auto mesh = BuildGizmoMesh(mode, style, view, kUnitsPerPixel);
            char label[96];
            std::snprintf(label, sizeof label, "%s builds lines and triangles",
                          univex::gizmo::GizmoModeName(mode));
            Check(!mesh.lines.empty() && !mesh.triangles.empty(), label);
        }

        // Only the camera-facing half of each rotate ring is built. Three full circles through
        // one small area overlap into a mesh of arcs where no ring can be told from another,
        // which is why every established editor (ImGuizmo, Maya, Unreal) shows half rings. The
        // cut is free visually because it falls where the ring is edge-on to the eye.
        const auto rotate = BuildGizmoMesh(GizmoMode::Rotate, style, view, kUnitsPerPixel);
        int nearSide = 0;
        int farSide = 0;
        float weakestNearAlpha = 1.f;
        for (const auto& tri : rotate.triangles) {
            // Only the three axis rings; skip the screen-facing free ring and the centre cube.
            const float radius = univex::math::Length(tri.a);
            if (radius < style.ringRadius * 0.8f || radius > style.ringRadius * 1.1f) continue;
            const float facing = univex::math::Dot(univex::math::Normalize(tri.a), view);
            if (facing > 0.2f) {
                ++farSide;
            } else if (facing < -0.2f) {
                ++nearSide;
                weakestNearAlpha = std::min(weakestNearAlpha, tri.alpha);
            }
        }
        Check(nearSide > 0, "the camera-facing half of each rotate ring is built");
        Check(farSide == 0, "the half curving away from the camera is not built at all");
        Check(weakestNearAlpha > 0.99f,
              "the near half is fully opaque, not a faded ghost of a whole ring");
    }

    std::puts("\n== Universal gizmo layout: the three tools stay separated ==");
    {
        using univex::gizmo::GizmoStyle;
        const GizmoStyle style;

        const float ringOuter = style.universalRingRadius;
        const float arrowTip = style.universalShaftEnd + style.universalConeLength;
        const float scaleNear = style.universalScaleBoxOffset - style.universalScaleBoxSize * 0.5f;

        std::printf("       ring %.2f  ->  arrow tip %.2f  ->  scale cube starts %.2f (gizmo units)\n",
                    static_cast<double>(ringOuter), static_cast<double>(arrowTip),
                    static_cast<double>(scaleNear));

        Check(ringOuter < style.universalShaftEnd,
              "the rotate ring sits inside the move arrow's shaft, not across its head");
        Check(arrowTip < scaleNear,
              "the scale cube starts beyond the arrow tip, so the two never touch");
        CheckNear(scaleNear - arrowTip, 0.24f, 0.15f,
                  "there is a clear gap between arrow tip and scale cube");
        Check(style.universalLineWidthPx <= style.axisLineWidthPx,
              "universal strokes are no heavier than the single-tool gizmos'");
    }

    std::puts("\n== Nav gizmo: picking resolves the ball under the cursor ==");
    {
        using univex::gizmo::NavHandles;
        using univex::gizmo::NavViewHalfExtent;
        using univex::gizmo::PickNavGizmo;
        using univex::gizmo::GizmoStyle;

        const GizmoStyle style;
        OrbitCamera camera; // default three-quarter view: all six balls separated
        const Mat4 navView = Mat4::LookAt(
            univex::math::Normalize(camera.Eye() - camera.Target()) * 3.f,
            Vec3{0.f, 0.f, 0.f}, Vec3{0.f, 1.f, 0.f});

        const float size = style.navPixelSize;
        const float pixelsPerUnit = (size * 0.5f) / NavViewHalfExtent(style);
        const float center = size * 0.5f;

        int resolved = 0;
        for (const auto& handle : NavHandles(style)) {
            const Vec3 viewSpace = navView.TransformDirection(handle.direction);
            const float screenX = center + viewSpace.x * pixelsPerUnit;
            const float screenY = center - viewSpace.y * pixelsPerUnit;
            const auto pick = PickNavGizmo(style, navView, screenX, screenY, size);
            if (pick.hit && pick.axisLabel == handle.axisLabel && pick.positive == handle.positive) {
                ++resolved;
            }
        }
        Check(resolved == 6, "each of the six balls is picked at its own projected position");

        // A press in the empty corner of the widget must not snap the view.
        const auto miss = PickNavGizmo(style, navView, 2.f, 2.f, size);
        Check(!miss.hit, "a press on empty space inside the widget picks nothing");

        // Looking straight down +Y, the +Y and -Y balls project on top of each
        // other; the near one has to win.
        OrbitCamera topCamera;
        topCamera.SetYawPitch(0.f, topCamera.Settings().pitchMax);
        const Mat4 topView = Mat4::LookAt(
            univex::math::Normalize(topCamera.Eye() - topCamera.Target()) * 3.f,
            Vec3{0.f, 0.f, 0.f}, Vec3{0.f, 1.f, 0.f});
        const auto overlapping = PickNavGizmo(style, topView, center, center, size);
        Check(overlapping.hit && overlapping.axisLabel == 'Y' && overlapping.positive,
              "with +Y and -Y overlapping, the nearer (+Y) ball is the one picked");
    }

    std::puts("\n== Camera: snap animation and projection toggle ==");
    {
        OrbitCamera camera;
        camera.SnapToDirection(Vec3{0.f, 0.f, 1.f}); // Front
        Check(camera.IsAnimating(), "SnapToDirection starts an animation");
        int steps = 0;
        while (camera.Update(1.f / 60.f) && steps < 600) ++steps;
        Check(!camera.IsAnimating(), "the snap settles");
        CheckNear(camera.Yaw(), 1.5707963f, 1e-2f, "front view yaw settles on +Z");
        CheckNear(camera.Pitch(), 0.f, 1e-2f, "front view pitch settles level");

        // A drag must win over an in-flight snap rather than fighting it.
        OrbitCamera dragged;
        dragged.SnapToDirection(Vec3{1.f, 0.f, 0.f});
        dragged.Orbit(10.f, 0.f);
        Check(!dragged.IsAnimating(), "a manual orbit cancels an in-flight snap");

        // Toggling projection must not change how big things look at the pivot.
        OrbitCamera projection;
        const float perspectiveHalfHeight =
            projection.Distance() * std::tan(projection.Settings().fovYRadians * 0.5f);
        projection.SetOrthographic(true);
        CheckNear(projection.OrthographicHalfHeight(), perspectiveHalfHeight, 1e-4f,
                  "ortho half-height matches the perspective framing at the pivot");
        const Mat4 orthoViewProj = projection.ViewProjection(16.f / 9.f);
        bool finite = true;
        for (float v : orthoViewProj.m) if (!std::isfinite(v)) finite = false;
        Check(finite, "the orthographic view-projection is finite");
        Check(Mat4::Inverse(orthoViewProj).has_value(),
              "the orthographic view-projection inverts (the grid needs it for ray rebuilding)");
    }

    std::puts("\n== Viewport metric: world-per-pixel is measured AT A POINT ==");
    {
        using univex::camera::WorldPerPixelAtPointUVE;

        OrbitCamera camera;
        constexpr int kHeight = 720;

        // At the orbit target the metric must reproduce the frustum-height formula the whole
        // module used before it took a point at all, or every existing on-screen size shifts.
        const float expectedAtTarget =
            (2.f * camera.Distance() * std::tan(camera.Settings().fovYRadians * 0.5f)) /
            static_cast<float>(kHeight);
        CheckNear(WorldPerPixelAtPointUVE(camera, kHeight, camera.Target()), expectedAtTarget,
                  1e-6f, "at the orbit target the metric matches the frustum height there");

        // The regression this whole change exists to prevent: a pivot that is not the orbit
        // target must scale with ITS depth, not the camera's orbit distance. Twice as far along
        // the view axis is exactly twice the world per pixel.
        const Vec3 forward =
            univex::math::Normalize(camera.Target() - camera.Eye());
        const Vec3 twiceAsFar = camera.Eye() + forward * (camera.Distance() * 2.f);
        CheckNear(WorldPerPixelAtPointUVE(camera, kHeight, twiceAsFar), expectedAtTarget * 2.f,
                  1e-5f, "a pivot twice as deep spans twice the world per pixel");

        // Depth is measured ALONG the view axis, not as straight-line distance from the eye -
        // otherwise the metric would swell toward the corners of the screen and a gizmo would
        // grow simply for being off-centre.
        const Vec3 right = univex::math::Normalize(
            univex::math::Cross(forward, Vec3{0.f, 1.f, 0.f}));
        const Vec3 offCentre = camera.Target() + right * (camera.Distance() * 0.5f);
        CheckNear(WorldPerPixelAtPointUVE(camera, kHeight, offCentre), expectedAtTarget, 1e-5f,
                  "sliding a pivot sideways does not change its world-per-pixel");

        // A pivot level with or behind the eye cannot produce a zero or negative scale.
        const Vec3 behindEye = camera.Eye() - forward * 5.f;
        Check(WorldPerPixelAtPointUVE(camera, kHeight, behindEye) > 0.f,
              "a pivot behind the eye still yields a positive scale");
        Check(WorldPerPixelAtPointUVE(camera, 0, camera.Target()) == 0.f,
              "a zero-height viewport yields zero rather than a division by zero");

        // Orthographic has no converging frustum, so the answer cannot depend on the point.
        OrbitCamera ortho;
        ortho.SetOrthographic(true);
        const float orthoAtTarget = WorldPerPixelAtPointUVE(ortho, kHeight, ortho.Target());
        CheckNear(WorldPerPixelAtPointUVE(ortho, kHeight, twiceAsFar), orthoAtTarget, 1e-6f,
                  "orthographic world-per-pixel is the same at any depth");
        CheckNear(orthoAtTarget,
                  (ortho.OrthographicHalfHeight() * 2.f) / static_cast<float>(kHeight), 1e-6f,
                  "orthographic world-per-pixel is the view volume over the pixel height");
    }

    std::puts("\n== Gizmo picking: what is grabbable is what is drawn ==");
    {
        using univex::gizmo::AxisDirectionForHandleUVE;
        using univex::gizmo::GizmoHandleUVE;
        using univex::gizmo::GizmoMode;
        using univex::gizmo::GizmoStyle;
        using univex::gizmo::PickGizmoHandleUVE;

        const GizmoStyle style;
        const Vec3 pivot{2.f, 1.f, -3.f};   // deliberately NOT the origin
        constexpr float kScale = 0.1f;      // gizmo units -> world units
        constexpr float kUnitsPerPixel = 1.f / 82.f;
        const Vec3 view = univex::math::Normalize(Vec3{-0.65f, -0.44f, 0.62f});

        // Fires a ray straight at a point expressed in gizmo units about the pivot, from far
        // enough away that everything is in front of the eye.
        const auto pickAtLocal = [&](GizmoMode mode, const Vec3& local) {
            const Vec3 target = pivot + local * kScale;
            const Vec3 origin = target - view * 50.f;
            return PickGizmoHandleUVE(mode, style, origin, view, pivot, kScale, view,
                                      kUnitsPerPixel);
        };

        // --- Move: each axis shaft, aimed at its own midpoint -------------------------------
        const float moveMid = (style.moveShaftStart + style.moveShaftEnd) * 0.5f;
        const std::array<std::pair<Vec3, GizmoHandleUVE>, 3> moveAxes = {{
            {Vec3{1.f, 0.f, 0.f}, GizmoHandleUVE::AxisX},
            {Vec3{0.f, 1.f, 0.f}, GizmoHandleUVE::AxisY},
            {Vec3{0.f, 0.f, 1.f}, GizmoHandleUVE::AxisZ},
        }};
        for (const auto& [direction, expected] : moveAxes) {
            const auto hit = pickAtLocal(GizmoMode::Move, direction * moveMid);
            char label[96];
            std::snprintf(label, sizeof label, "move: the %c shaft picks its own axis",
                          expected == GizmoHandleUVE::AxisX ? 'X'
                              : (expected == GizmoHandleUVE::AxisY ? 'Y' : 'Z'));
            Check(hit.handle == expected, label);
            const auto axis = AxisDirectionForHandleUVE(hit.handle);
            Check(axis.has_value() && univex::math::Dot(*axis, direction) > 0.99f,
                  "   ... and the handle names the axis it was drawn along");
        }

        // --- Move: the plane quads, aimed at the centre of each drawn quad -------------------
        const float planeMid = style.planeHandleOffset + style.planeHandleSize * 0.5f;
        Check(pickAtLocal(GizmoMode::Move, Vec3{planeMid, planeMid, 0.f}).handle ==
                  GizmoHandleUVE::PlaneXY, "move: the XY quad picks the XY plane");
        Check(pickAtLocal(GizmoMode::Move, Vec3{0.f, planeMid, planeMid}).handle ==
                  GizmoHandleUVE::PlaneYZ, "move: the YZ quad picks the YZ plane");
        Check(pickAtLocal(GizmoMode::Move, Vec3{planeMid, 0.f, planeMid}).handle ==
                  GizmoHandleUVE::PlaneZX, "move: the ZX quad picks the ZX plane");

        // --- Centre and clean miss ----------------------------------------------------------
        Check(pickAtLocal(GizmoMode::Move, Vec3{0.f, 0.f, 0.f}).handle == GizmoHandleUVE::Uniform,
              "move: the pivot picks the centre handle");
        const auto miss = pickAtLocal(GizmoMode::Move, Vec3{6.f, 6.f, 6.f});
        Check(miss.handle == GizmoHandleUVE::None,
              "a ray well outside the widget hits nothing");

        // --- Rotate: each ring, aimed at a point that lies on THAT RING ONLY -----------------
        // Every axis-aligned point on a rotation ring is shared by two of them - (0, r, 0) is on
        // the X ring and the Z ring alike, since both contain the Y direction - so aiming at one
        // proves nothing about which ring answered. These points sit at 45 degrees between two
        // axes, which belongs to exactly one ring each, and so actually pin the mapping.
        const float diagonal = style.ringRadius * 0.70710678f;
        Check(pickAtLocal(GizmoMode::Rotate, Vec3{0.f, diagonal, diagonal}).handle ==
                  GizmoHandleUVE::AxisX,
              "rotate: the ring in the YZ plane - the only one there - is the X ring");
        Check(pickAtLocal(GizmoMode::Rotate, Vec3{diagonal, 0.f, diagonal}).handle ==
                  GizmoHandleUVE::AxisY,
              "rotate: the ring in the ZX plane is the Y ring");
        Check(pickAtLocal(GizmoMode::Rotate, Vec3{diagonal, diagonal, 0.f}).handle ==
                  GizmoHandleUVE::AxisZ,
              "rotate: the ring in the XY plane is the Z ring");

        // The same three points, checked against the DRAWN geometry rather than the picker, so a
        // ring that is drawn in one plane but picked as another cannot pass both halves.
        {
            const auto rotateMesh = BuildGizmoMesh(GizmoMode::Rotate, style, view, kUnitsPerPixel);
            const std::array<std::pair<Vec3, const char*>, 3> ringProbes = {{
                {style.axisColorX, "the ring in the YZ plane is drawn red (X)"},
                {style.axisColorY, "the ring in the ZX plane is drawn green (Y)"},
                {style.axisColorZ, "the ring in the XY plane is drawn blue (Z)"},
            }};
            const std::array<Vec3, 3> ringPoints = {{
                Vec3{0.f, diagonal, diagonal},
                Vec3{diagonal, 0.f, diagonal},
                Vec3{diagonal, diagonal, 0.f},
            }};
            for (std::size_t i = 0; i < ringPoints.size(); ++i) {
                // Find the ring triangle nearest this point and read the colour it was authored
                // with. The nearest ring geometry to a point on one ring is that ring.
                float bestDistance = 1e30f;
                Vec3 bestColor{};
                for (const auto& tri : rotateMesh.triangles) {
                    const float radius = univex::math::Length(tri.a);
                    if (radius < style.ringRadius * 0.9f || radius > style.ringRadius * 1.1f) continue;
                    const float distance = univex::math::Length(tri.a - ringPoints[i]);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        bestColor = tri.color;
                    }
                }
                const Vec3 expected = ringProbes[i].first;
                Check(bestDistance < 1e30f &&
                          std::fabs(bestColor.x - expected.x) < 1e-4f &&
                          std::fabs(bestColor.y - expected.y) < 1e-4f &&
                          std::fabs(bestColor.z - expected.z) < 1e-4f,
                      ringProbes[i].second);
            }
        }

        // --- Scale: the shafts and their end cubes -------------------------------------------
        const float scaleMid = (style.scaleShaftStart + style.scaleShaftEnd) * 0.5f;
        for (const auto& [direction, expected] : moveAxes) {
            Check(pickAtLocal(GizmoMode::Scale, direction * scaleMid).handle == expected,
                  "scale: a shaft picks the same axis the move gizmo would");
            Check(pickAtLocal(GizmoMode::Scale, direction * style.scaleShaftEnd).handle == expected,
                  "scale: the end cube picks that axis too");
        }

        // --- Select mode offers only the centre ----------------------------------------------
        Check(pickAtLocal(GizmoMode::Select, Vec3{1.f, 0.f, 0.f}).handle == GizmoHandleUVE::None,
              "select: there are no axis handles to grab");

        // --- Hit regions hold their pixel size at any depth -----------------------------------
        // The bug this guards: sizing from the camera's orbit distance instead of the pivot's own
        // depth made the grab radii drift as soon as the two differed. Picking is expressed in
        // gizmo units, so the same local aim point must hit at any scale.
        const Vec3 farPivot{200.f, 60.f, -140.f};
        const Vec3 farTarget = farPivot + Vec3{1.f, 0.f, 0.f} * (moveMid * 4.f);
        const auto farHit = PickGizmoHandleUVE(GizmoMode::Move, style, farTarget - view * 900.f,
                                               view, farPivot, 4.f, view, kUnitsPerPixel);
        Check(farHit.handle == GizmoHandleUVE::AxisX,
              "a gizmo at a distant pivot picks exactly as one at a near pivot does");
    }

    std::puts("\n== Gizmo drag: cursor movement to transform amount ==");
    {
        using univex::gizmo::ProjectRayOntoAxisUVE;
        using univex::gizmo::ProjectRayOntoPlaneUVE;
        using univex::gizmo::ProjectRayOntoRingAngleUVE;
        using univex::gizmo::ShortestAngleDeltaUVE;

        const Vec3 pivot{4.f, -2.f, 7.f}; // deliberately not the origin
        const Vec3 axisX{1.f, 0.f, 0.f};
        const Vec3 axisY{0.f, 1.f, 0.f};

        // --- axis: a ray aimed straight at a point on the axis reports that point ------------
        {
            const Vec3 target = pivot + axisX * 3.5f;
            const Vec3 direction = univex::math::Normalize(Vec3{0.f, -1.f, -1.f});
            const auto along = ProjectRayOntoAxisUVE(target - direction * 20.f, direction, pivot, axisX);
            Check(along.has_value(), "a ray crossing the axis projects onto it");
            if (along.has_value()) {
                CheckNear(*along, 3.5f, 1e-3f, "   ... at the distance it actually crosses");
            }
        }

        // The property a drag depends on: the value is measured from the PIVOT, so the caller's
        // (current - press) is a true delta regardless of where the gesture started.
        {
            const Vec3 direction = univex::math::Normalize(Vec3{0.f, -1.f, -1.f});
            const auto press = ProjectRayOntoAxisUVE((pivot + axisX * 1.f) - direction * 20.f,
                                                     direction, pivot, axisX);
            const auto current = ProjectRayOntoAxisUVE((pivot + axisX * 4.f) - direction * 20.f,
                                                       direction, pivot, axisX);
            Check(press.has_value() && current.has_value(), "both ends of a drag project");
            if (press.has_value() && current.has_value()) {
                CheckNear(*current - *press, 3.0f, 1e-3f,
                          "   ... and their difference is the distance dragged");
            }
        }

        // Sighting down the axis must refuse rather than fling the object across the world.
        Check(!ProjectRayOntoAxisUVE(pivot - axisX * 30.f, axisX, pivot, axisX).has_value(),
              "a ray parallel to the axis is refused, not projected with infinite sensitivity");
        Check(!ProjectRayOntoAxisUVE(pivot, Vec3{0.f, 0.f, 0.f}, pivot, axisX).has_value(),
              "a degenerate ray direction is refused");

        // --- plane ---------------------------------------------------------------------------
        {
            const Vec3 expected = pivot + Vec3{2.f, 3.f, 0.f}; // in the XY plane through the pivot
            const Vec3 normal{0.f, 0.f, 1.f};
            const auto hit = ProjectRayOntoPlaneUVE(expected + normal * 12.f, normal * -1.f, pivot, normal);
            Check(hit.has_value(), "a ray through the plane finds its crossing point");
            if (hit.has_value()) {
                CheckNear(univex::math::Length(*hit - expected), 0.f, 1e-3f,
                          "   ... exactly where it crosses");
            }
        }
        Check(!ProjectRayOntoPlaneUVE(pivot + Vec3{0.f, 0.f, 5.f}, Vec3{1.f, 0.f, 0.f}, pivot,
                                      Vec3{0.f, 0.f, 1.f}).has_value(),
              "a ray grazing along the plane is refused");

        // --- ring ----------------------------------------------------------------------------
        // A quarter turn around Y must read as a quarter turn, whichever way the basis happens to
        // be oriented - so the test compares two angles rather than asserting one absolute value.
        {
            const Vec3 normal = axisY;
            const Vec3 first = pivot + Vec3{2.f, 0.f, 0.f};
            const Vec3 second = pivot + Vec3{0.f, 0.f, 2.f};
            const auto a = ProjectRayOntoRingAngleUVE(first + normal * 9.f, normal * -1.f, pivot, normal);
            const auto b = ProjectRayOntoRingAngleUVE(second + normal * 9.f, normal * -1.f, pivot, normal);
            Check(a.has_value() && b.has_value(), "two points on a ring both report an angle");
            if (a.has_value() && b.has_value()) {
                CheckNear(std::fabs(ShortestAngleDeltaUVE(*a, *b)),
                          std::numbers::pi_v<float> * 0.5f, 1e-3f,
                          "   ... a quarter turn apart reads as a quarter turn");
            }
        }
        Check(!ProjectRayOntoRingAngleUVE(pivot + axisY * 5.f, axisY * -1.f, pivot, axisY).has_value(),
              "a ray landing exactly on the pivot has no angle to report");

        // --- angle wrapping --------------------------------------------------------------------
        {
            constexpr float kPi = std::numbers::pi_v<float>;
            CheckNear(ShortestAngleDeltaUVE(0.1f, 0.4f), 0.3f, 1e-5f, "a small turn is itself");
            // Crossing the +-pi seam the short way, not the 359-degree way round.
            CheckNear(ShortestAngleDeltaUVE(kPi - 0.1f, -kPi + 0.1f), 0.2f, 1e-5f,
                      "crossing the seam takes the short way round");
            CheckNear(ShortestAngleDeltaUVE(-kPi + 0.1f, kPi - 0.1f), -0.2f, 1e-5f,
                      "   ... and the same in reverse");
        }
    }

    std::puts("\n== Nav gizmo labels: legible vector glyphs, upright in screen space ==");
    {
        using univex::gizmo::BuildNavGizmoMeshes;
        using univex::gizmo::GizmoStyle;

        const GizmoStyle style;
        const Vec3 view = univex::math::Normalize(Vec3{-0.65f, -0.44f, 0.62f});
        const auto meshes = BuildNavGizmoMeshes(style, view);

        // The screen basis the labels are placed on - the same one BuildNavGizmoMeshes derives,
        // and the same one the nav viewport's own camera uses, so a glyph laid out on it is
        // axis-aligned on screen.
        const Vec3 forward = view;
        const Vec3 right = univex::math::Normalize(
            univex::math::Cross(forward, Vec3{0.f, 1.f, 0.f}));
        const Vec3 up = univex::math::Normalize(univex::math::Cross(right, forward));

        // Collect the strokes sitting near the +Y ball: those are its 'Y' glyph.
        const Vec3 ballCentre{0.f, 1.f, 0.f};
        const float halfSize = style.navBallRadius * style.navLabelScale;
        int glyphStrokes = 0;
        float widestSpan = 0.f;
        float tallestSpan = 0.f;
        bool everyStrokeInPlane = true;
        for (const auto& line : meshes.overlay.lines) {
            if (univex::math::Length(line.a - ballCentre) > style.navBallRadius ||
                univex::math::Length(line.b - ballCentre) > style.navBallRadius) {
                continue; // an axis stub or another ball's glyph
            }
            ++glyphStrokes;
            for (const Vec3& end : {line.a, line.b}) {
                const Vec3 offset = end - ballCentre;
                // A glyph laid out on (right, up) has no component along the view direction
                // beyond the small lift that keeps it in front of the ball.
                if (std::fabs(univex::math::Dot(offset, forward)) > halfSize * 0.5f) {
                    everyStrokeInPlane = false;
                }
                widestSpan = std::max(widestSpan, std::fabs(univex::math::Dot(offset, right)));
                tallestSpan = std::max(tallestSpan, std::fabs(univex::math::Dot(offset, up)));
            }
        }

        Check(glyphStrokes == 3, "the Y glyph is exactly three strokes - two arms and a stem");
        Check(everyStrokeInPlane, "every stroke lies in the screen plane, so the letter is upright");
        CheckNear(tallestSpan, halfSize, 1e-4f, "the glyph fills its nominal height exactly");
        Check(widestSpan < tallestSpan,
              "the glyph is taller than it is wide, as a letterform should be");

        // The stroke has to keep a solid core at the size it is actually drawn. gizmo_line.frag
        // fades coverage over one pixel either side of the nominal width, so a stroke thinner
        // than about 1.5 px has no fully-covered centre left and breaks into fragments - which is
        // exactly how these glyphs used to render.
        const float pixelsPerUnit = (style.navPixelSize * 0.5f) /
                                    univex::gizmo::NavViewHalfExtent(style);
        const float glyphHeightPx = 2.f * halfSize * pixelsPerUnit;
        std::printf("       nav %.0f px -> glyph %.1f px tall, stroke %.1f px (%.0f%%)\n",
                    static_cast<double>(style.navPixelSize), static_cast<double>(glyphHeightPx),
                    static_cast<double>(style.navLabelWidthPx),
                    static_cast<double>(style.navLabelWidthPx / glyphHeightPx * 100.f));
        Check(style.navLabelWidthPx >= 1.5f, "the label stroke keeps a solid core at its drawn width");
        Check(glyphHeightPx >= 14.f, "the glyph is tall enough to read");
        Check(style.navLabelWidthPx / glyphHeightPx < 0.18f,
              "the stroke stays a sane fraction of the glyph, not a blot");
    }

    std::printf("\n%s (%d failing check%s)\n",
                g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
