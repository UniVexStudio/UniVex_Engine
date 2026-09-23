#include "univex/gizmo/NavGizmo.h"

#include "univex/gizmo/SolidPrimitives.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace univex::gizmo {

using univex::math::Cross;
using univex::math::Dot;
using univex::math::Normalize;

namespace {


// A disc that always faces the camera, built from real triangles. No rim
// stroke here - an earlier version added one as a per-segment GizmoLine
// around the circumference, but that is exactly the "gear teeth" failure
// AddFacingAnnulus's own comment below already warns about (a stroke built
// from independent per-segment quads leaves visible spikes/gaps at each
// vertex joint whenever the chord is short relative to the stroke width,
// which a small nav-gizmo ball always is) - it read as a "hairy" fringe
// around every ball instead of a clean rim. Callers that want a defined
// edge use the same seamless double-disc technique AddFacingAnnulus already
// relies on (see AddFacingDiscWithRim below) instead of a stroke.
void AddAxisLabel(GizmoMesh& mesh, const Vec3& center, const Vec3& right, const Vec3& up,
                  char letter, float halfSize, const Vec3& color, float widthPx) {
    const auto place = [&](float x, float y) {
        return center + right * (x * halfSize) + up * (y * halfSize);
    };
    const auto stroke = [&](float x0, float y0, float x1, float y1) {
        mesh.lines.push_back(GizmoLine{place(x0, y0), place(x1, y1), color, widthPx});
    };

    switch (letter) {
        // Proportions matter at this size: too narrow and the Y's arms close up against its stem,
        // too wide and the X's crossing thickens into a blob. These are conventional letterform
        // ratios - a little over half as wide as tall, with the Y's arms meeting above centre so
        // its stem stays a clearly separate stroke.
        case 'X':
            stroke(-0.58f,  1.f,   0.58f, -1.f);
            stroke(-0.58f, -1.f,   0.58f,  1.f);
            break;
        case 'Y':
            // The arms meet well above centre. Bringing the junction down towards the middle makes
            // the two arms and the stem converge at a shallow angle, and three stroke quads
            // overlapping at a shallow angle read as a blot rather than as a join.
            stroke(-0.58f,  1.f,   0.f,    0.28f);
            stroke( 0.58f,  1.f,   0.f,    0.28f);
            stroke( 0.f,    0.28f, 0.f,   -1.f);
            break;
        case 'Z':
            stroke(-0.58f,  1.f,   0.58f,  1.f);
            stroke( 0.58f,  1.f,  -0.58f, -1.f);
            stroke(-0.58f, -1.f,   0.58f, -1.f);
            break;
        default:
            break;
    }
}

// Screen-aligned basis for the nav gizmo's own camera, so labels stay upright
// however the view is orbited.
void ScreenBasis(const Vec3& viewDirection, Vec3& outRight, Vec3& outUp) {
    const Vec3 forward = Normalize(viewDirection);
    Vec3 worldUp{0.f, 1.f, 0.f};
    // Looking straight up or down, world-up is parallel to forward and the
    // cross product collapses; pick a different reference in that case.
    if (std::fabs(Dot(forward, worldUp)) > 0.999f) worldUp = Vec3{0.f, 0.f, 1.f};
    outRight = Normalize(Cross(forward, worldUp));
    outUp = Normalize(Cross(outRight, forward));
}

} // namespace

std::array<NavHandle, 6> NavHandles(const GizmoStyle& style) {
    return {{
        {{ 1.f, 0.f, 0.f}, style.axisColorX, true,  'X'},
        {{-1.f, 0.f, 0.f}, style.axisColorX, false, 'X'},
        {{ 0.f, 1.f, 0.f}, style.axisColorY, true,  'Y'},
        {{ 0.f,-1.f, 0.f}, style.axisColorY, false, 'Y'},
        {{ 0.f, 0.f, 1.f}, style.axisColorZ, true,  'Z'},
        {{ 0.f, 0.f,-1.f}, style.axisColorZ, false, 'Z'},
    }};
}

float NavViewHalfExtent(const GizmoStyle& style) {
    // One unit out to each ball centre, plus its radius, plus a little air.
    return 1.f + style.navBallRadius + 0.10f;
}

NavGizmoMeshes BuildNavGizmoMeshes(const GizmoStyle& style, const Vec3& viewDirection) {
    NavGizmoMeshes meshes;
    GizmoMesh& mesh = meshes.overlay;
    const Vec3 view = Normalize(viewDirection);
    const auto handles = NavHandles(style);

    // Axis stubs go in the underlay, so the balls sit on top of them.
    // Nav units per pixel, so the stubs keep their pixel thickness at any widget size.
    const float unitsPerPixel = NavViewHalfExtent(style) / (style.navPixelSize * 0.5f);
    for (const NavHandle& handle : handles) {
        if (!handle.positive) continue; // one stub per axis, drawn full length both ways
        // A lit rod rather than a stroke. There is no depth buffer here, so only the faces
        // turned toward the eye are built - painting the far side would show through.
        AddSolidCylinderUVE(meshes.underlay, handle.direction * -1.f, handle.direction,
                            style.navAxisLineWidthPx * 0.65f * unitsPerPixel, 12, handle.color * 0.75f, 1.f, view);
    }

    std::vector<const NavHandle*> sorted;
    sorted.reserve(handles.size());
    for (const NavHandle& handle : handles) sorted.push_back(&handle);
    std::sort(sorted.begin(), sorted.end(), [&](const NavHandle* a, const NavHandle* b) {
        return Dot(a->direction, view) > Dot(b->direction, view); // most negative dot == nearest, drawn last
    });

    Vec3 right, up;
    ScreenBasis(view, right, up);

    for (const NavHandle* handle : sorted) {
        const Vec3 center = handle->direction;
        if (handle->positive) {
            // A lit sphere carrying its axis letter. The letter is a stroke, drawn after the
            // spheres in this no-depth pass, so it sits on top without being lifted off the ball.
            AddSolidSphereUVE(mesh, center, style.navBallRadius, 12, 24, handle->color, 1.f, view);
            AddAxisLabel(mesh, center + view * -0.01f, right, up, handle->axisLabel,
                         style.navBallRadius * style.navLabelScale,
                         style.navLabelColor, style.navLabelWidthPx);
        } else {
            // The negative end: a smaller, darker sphere - same family, clearly the back of the axis.
            AddSolidSphereUVE(mesh, center, style.navBallRadius * 0.72f, 10, 20, handle->color * 0.5f, 1.f, view);
        }
    }
    return meshes;
}

NavPickResult PickNavGizmo(const GizmoStyle& style,
                           const Mat4& viewRotation,
                           float localX,
                           float localY,
                           float viewportSizePx) {
    NavPickResult result;
    if (viewportSizePx <= 0.f) return result;

    const float halfExtent = NavViewHalfExtent(style);
    const float pixelsPerUnit = (viewportSizePx * 0.5f) / halfExtent;
    const float centerPx = viewportSizePx * 0.5f;
    const float ballRadiusPx = style.navBallRadius * pixelsPerUnit;

    float bestDepth = -1e30f;
    for (const NavHandle& handle : NavHandles(style)) {
        const Vec3 viewSpace = viewRotation.TransformDirection(handle.direction);
        const float screenX = centerPx + viewSpace.x * pixelsPerUnit;
        const float screenY = centerPx - viewSpace.y * pixelsPerUnit;
        const float dx = localX - screenX;
        const float dy = localY - screenY;
        if (dx * dx + dy * dy > ballRadiusPx * ballRadiusPx) continue;

        // Camera looks down -Z in view space, so the largest z is nearest.
        if (viewSpace.z > bestDepth) {
            bestDepth = viewSpace.z;
            result.hit = true;
            result.direction = handle.direction;
            result.axisLabel = handle.axisLabel;
            result.positive = handle.positive;
        }
    }
    return result;
}

} // namespace univex::gizmo
