#include "univex/gizmo/GizmoGeometry.h"

#include "univex/gizmo/SolidPrimitives.h"

#include <array>
#include <cmath>
#include <numbers>

namespace univex::gizmo {

using univex::math::Cross;
using univex::math::Dot;
using univex::math::Normalize;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

struct Axis {
    Vec3 direction;
    Vec3 color;
};

std::array<Axis, 3> AxesOf(const GizmoStyle& style) {
    return {{
        {Vec3{1.f, 0.f, 0.f}, style.axisColorX},
        {Vec3{0.f, 1.f, 0.f}, style.axisColorY},
        {Vec3{0.f, 0.f, 1.f}, style.axisColorZ},
    }};
}

// A rotation ring shows only the half curving toward the camera.
//
// This is the convention every established editor follows (ImGuizmo's DrawRotationGizmo emits a
// half circle per axis; Maya and Unreal read the same way), and the reason is not decoration:
// three full circles through one point overlap into a ball of arcs where no ring can be told from
// another. Half rings cannot overlap, so each one stays a readable curve.
//
// Crucially the cut costs nothing visually, because it falls exactly where the ring turns away
// from the eye - there the circle is edge-on, so its end is a point rather than a blunt edge.
// This value is the width of a short fade either side of that silhouette, in the ring's own
// facing-dot space, purely so the last segment resolves smoothly instead of popping as the camera
// orbits. It is deliberately tiny: wide enough to anti-alias the end, far too narrow to bring the
// back half of the ring back into view.
constexpr float kRingSilhouetteFeatherUVE = 0.06f;

[[nodiscard]] float Clamp01UVE(float value) {
    return value < 0.f ? 0.f : (value > 1.f ? 1.f : value);
}

// Two unit vectors perpendicular to `axis` and to each other.
void PerpBasis(const Vec3& axis, Vec3& outU, Vec3& outV) {
    const Vec3 a = Normalize(axis);
    const Vec3 helper = (std::fabs(a.y) < 0.98f) ? Vec3{0.f, 1.f, 0.f} : Vec3{1.f, 0.f, 0.f};
    outU = Normalize(Cross(helper, a));
    outV = Normalize(Cross(a, outU));
}

std::vector<Vec3> CirclePoints(const Vec3& center, const Vec3& u, const Vec3& v,
                               float radius, int segments) {
    std::vector<Vec3> points;
    points.reserve(static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        const float t = (2.f * kPi * static_cast<float>(i)) / static_cast<float>(segments);
        points.push_back(center + u * (std::cos(t) * radius) + v * (std::sin(t) * radius));
    }
    return points;
}

void AddTriangle(GizmoMesh& mesh, const Vec3& a, const Vec3& b, const Vec3& c,
                 const Vec3& color, float alpha) {
    mesh.triangles.push_back(GizmoTriangle{a, b, c, color, alpha});
}

void AddQuad(GizmoMesh& mesh, const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3,
             const Vec3& color, float alpha) {
    AddTriangle(mesh, p0, p1, p2, color, alpha);
    AddTriangle(mesh, p0, p2, p3, color, alpha);
}

// A solid, axis-aligned cube, lit so its faces separate by shading instead of drawn edges.
void AddSolidCube(GizmoMesh& mesh, const Vec3& center, float size, const Vec3& color) {
    AddSolidBoxUVE(mesh, center, size, color);
}

// Arrow: a lit cylindrical shaft capped by a lit cone - one solid piece, no strokes.
void AddMoveArrow(GizmoMesh& mesh, const Axis& axis, const GizmoStyle& style,
                  float shaftStart, float shaftEnd, float coneLength, float coneRadius) {
    AddSolidCylinderUVE(mesh, axis.direction * shaftStart, axis.direction * shaftEnd, style.shaftRadius,
                        style.shaftSegments, axis.color);
    AddSolidConeUVE(mesh, axis.direction * shaftEnd, axis.direction * (shaftEnd + coneLength), coneRadius,
                    style.moveConeSegments, axis.color);
}

// A ring drawn as a solid annulus: two concentric circles joined by quads.
// Seamless where a stroked polyline would show its joints.
//
// `keepSegment` decides which parts of the ring survive — that is where the
// near-side arc selection happens, so the same routine serves both the
// camera-facing arcs and the full free-rotation ring.
// `segmentAlpha` returns the opacity for one segment, given its two midpoints. Returning zero
// drops the segment entirely, so the same routine serves a solid ring and a depth-cued one.
template <typename AlphaFn>
void AddAnnulus(GizmoMesh& mesh, const Vec3& center, const Vec3& axis, const Vec3& color,
                float radius, float halfWidth, int segments, AlphaFn segmentAlpha) {
    Vec3 u, v;
    PerpBasis(axis, u, v);
    const auto inner = CirclePoints(center, u, v, radius - halfWidth, segments);
    const auto outer = CirclePoints(center, u, v, radius + halfWidth, segments);
    const auto mid = CirclePoints(center, u, v, radius, segments);

    for (std::size_t i = 0; i + 1 < mid.size(); ++i) {
        const float alpha = segmentAlpha(mid[i] - center, mid[i + 1] - center);
        if (alpha <= 0.f) continue;
        AddTriangle(mesh, inner[i], outer[i], outer[i + 1], color, alpha);
        AddTriangle(mesh, inner[i], outer[i + 1], inner[i + 1], color, alpha);
    }
}

// The camera-facing half of a ring, as a lit tube. See kRingSilhouetteFeatherUVE for why only half.
void AddRingArc(GizmoMesh& mesh, const Vec3& axis, const Vec3& color, float radius,
                int segments, const Vec3& viewDirection, float tubeRadius, int tubeSegments) {
    Vec3 u, v;
    PerpBasis(axis, u, v);
    const Vec3 n = Normalize(axis);
    const auto spine = CirclePoints(Vec3{0.f, 0.f, 0.f}, u, v, radius, segments);
    for (std::size_t i = 0; i + 1 < spine.size(); ++i) {
        // Facing is negative on the near side: viewDirection points away from the eye.
        const float facing = (Dot(Normalize(spine[i]), viewDirection) +
                              Dot(Normalize(spine[i + 1]), viewDirection)) * 0.5f;
        // Fully opaque across the near half, falling to nothing over the narrow band at the
        // silhouette; the back half is never built at all.
        const float nearness = Clamp01UVE(-facing / kRingSilhouetteFeatherUVE);
        const float alpha = nearness * nearness * (3.f - 2.f * nearness);
        if (alpha <= 0.f) continue;
        // The tube's cross-section at each end of this segment lies in the plane of the radius
        // and the ring axis.
        const Vec3 r0 = Normalize(spine[i]);
        const Vec3 r1 = Normalize(spine[i + 1]);
        for (int k = 0; k < tubeSegments; ++k) {
            const float t0 = (2.f * kPi * static_cast<float>(k)) / static_cast<float>(tubeSegments);
            const float t1 = (2.f * kPi * static_cast<float>(k + 1)) / static_cast<float>(tubeSegments);
            const Vec3 a0 = spine[i] + (r0 * std::cos(t0) + n * std::sin(t0)) * tubeRadius;
            const Vec3 a1 = spine[i] + (r0 * std::cos(t1) + n * std::sin(t1)) * tubeRadius;
            const Vec3 b0 = spine[i + 1] + (r1 * std::cos(t0) + n * std::sin(t0)) * tubeRadius;
            const Vec3 b1 = spine[i + 1] + (r1 * std::cos(t1) + n * std::sin(t1)) * tubeRadius;
            GizmoTriangle first{a0, b0, b1, color, alpha};
            GizmoTriangle second{a0, b1, a1, color, alpha};
            first.lit = 1.f;
            second.lit = 1.f;
            mesh.triangles.push_back(first);
            mesh.triangles.push_back(second);
        }
    }
}

void AddFullRing(GizmoMesh& mesh, const Vec3& center, const Vec3& axis, const Vec3& color,
                 float radius, int segments, float halfWidth) {
    AddAnnulus(mesh, center, axis, color, radius, halfWidth, segments,
               [](const Vec3&, const Vec3&) { return 1.f; });
}

// How far in front of the pivot the view-facing screen ring sits, in pixels. The ring is built in
// the plane perpendicular to the view, so centred on the pivot it passes exactly through the
// middle of the three axis rings and through the pivot dot - with depth testing on, half of it
// ends up buried inside the widget. Lifting it a few pixels toward the eye puts the whole ring in
// front, where it reads as the outer boundary of the gizmo. In pixels rather than world units so
// the offset does not change with zoom.
constexpr float kScreenRingLiftPixelsUVE = 6.f;

// A plane handle tinted by the axis it is NORMAL to - the convention that makes XY, YZ and ZX
// tellable apart at a glance, and after which the colour of a handle predicts which drag it
// starts. Kept mostly neutral so it still reads as a face rather than as a fourth axis.
[[nodiscard]] Vec3 PlaneFillColor(const Vec3& normalAxisColor, const GizmoStyle& style) {
    return normalAxisColor * 0.35f + style.planeColor * 0.65f;
}

[[nodiscard]] Vec3 PlaneEdgeColor(const Vec3& normalAxisColor, const GizmoStyle& style) {
    return normalAxisColor * 0.55f + style.planeColor * 0.45f;
}

void AddMovePlaneHandles(GizmoMesh& mesh, const GizmoStyle& style, float unitsPerPixel) {
    const auto axes = AxesOf(style);
    const std::array<std::pair<int, int>, 3> pairs = {{{0, 1}, {1, 2}, {2, 0}}};
    for (const auto& [i, j] : pairs) {
        const Vec3 a = axes[static_cast<std::size_t>(i)].direction;
        const Vec3 b = axes[static_cast<std::size_t>(j)].direction;
        // The third axis is the one this plane is normal to, and the one that names its colour.
        const Vec3 normalColor = axes[static_cast<std::size_t>(3 - i - j)].color;
        const Vec3 fill = PlaneFillColor(normalColor, style);
        const Vec3 edge = PlaneEdgeColor(normalColor, style);
        const float o = style.planeHandleOffset;
        const float s = style.planeHandleSize;
        const Vec3 p0 = a * o + b * o;
        const Vec3 p1 = a * (o + s) + b * o;
        const Vec3 p2 = a * (o + s) + b * (o + s);
        const Vec3 p3 = a * o + b * (o + s);
        // A translucent face inside an opaque frame: the frame gives the handle its edge as
        // geometry, at the same pixel width the old stroke had.
        const float t = style.planeHandleEdgeWidthPx * unitsPerPixel;
        const Vec3 q0 = a * (o + t) + b * (o + t);
        const Vec3 q1 = a * (o + s - t) + b * (o + t);
        const Vec3 q2 = a * (o + s - t) + b * (o + s - t);
        const Vec3 q3 = a * (o + t) + b * (o + s - t);
        AddQuad(mesh, q0, q1, q2, q3, fill, style.planeHandleAlpha);
        AddQuad(mesh, p0, p1, q1, q0, edge, 1.f);
        AddQuad(mesh, p1, p2, q2, q1, edge, 1.f);
        AddQuad(mesh, p2, p3, q3, q2, edge, 1.f);
        AddQuad(mesh, p3, p0, q0, q3, edge, 1.f);
    }
}

void AddScalePlaneHandles(GizmoMesh& mesh, const GizmoStyle& style) {
    const auto axes = AxesOf(style);
    const std::array<std::pair<int, int>, 3> pairs = {{{0, 1}, {1, 2}, {2, 0}}};
    for (const auto& [i, j] : pairs) {
        const Vec3 a = axes[static_cast<std::size_t>(i)].direction;
        const Vec3 b = axes[static_cast<std::size_t>(j)].direction;
        // Same normal-axis tint as the move gizmo's plane handles, so Move and Scale read off one
        // basis instead of each inventing their own.
        const Vec3 normalColor = axes[static_cast<std::size_t>(3 - i - j)].color;
        const Vec3 p0 = a * style.scalePlaneOffset;
        const Vec3 p1 = b * style.scalePlaneOffset;
        const Vec3 p2 = (a + b) * style.scalePlanePull;
        AddTriangle(mesh, p0, p1, p2, PlaneFillColor(normalColor, style), style.planeHandleAlpha);
    }
}

// How far in front of the pivot the dot sits, in pixels. All three axis strokes cross exactly at
// the pivot, so a ring built in the same place fights them for depth and comes out broken. Nudged
// along the view direction, which changes its depth without moving it on screen at all.
constexpr float kPivotDotLiftPixelsUVE = 2.f;

// A thin ring at the pivot - see GizmoStyle::pivotDotRadiusPx for why this replaced a solid cube.
// View-aligned, so it stays a circle from every angle instead of foreshortening into an ellipse.
void AddPivotDot(GizmoMesh& mesh, const GizmoStyle& style, const Vec3& view, float scale) {
    AddFullRing(mesh, view * (-kPivotDotLiftPixelsUVE * scale), view, style.centerColor,
                style.pivotDotRadiusPx * scale, style.pivotDotSegments,
                style.pivotDotWidthPx * 0.5f * scale);
}

} // namespace

const char* GizmoModeName(GizmoMode mode) {
    switch (mode) {
        case GizmoMode::Select:    return "Select";
        case GizmoMode::Move:      return "Move";
        case GizmoMode::Rotate:    return "Rotate";
        case GizmoMode::Scale:     return "Scale";
        case GizmoMode::Universal: return "Universal";
    }
    return "Unknown";
}

GizmoMesh BuildGizmoMesh(GizmoMode mode, const GizmoStyle& style, const Vec3& viewDirection,
                         float unitsPerPixel) {
    GizmoMesh mesh;
    const auto axes = AxesOf(style);
    const Vec3 view = Normalize(viewDirection);

    // Ring strokes are solid geometry, so their pixel widths have to be
    // converted into gizmo units up front.
    const float scale = (unitsPerPixel > 0.f) ? unitsPerPixel : 1.f;
    const float freeRingHalfWidth = style.freeRingWidthPx * 0.5f * scale;

    switch (mode) {
        case GizmoMode::Select:
            AddPivotDot(mesh, style, view, scale);
            break;

        case GizmoMode::Move:
            AddPivotDot(mesh, style, view, scale);
            for (const Axis& axis : axes) {
                AddMoveArrow(mesh, axis, style,
                             style.moveShaftStart, style.moveShaftEnd,
                             style.moveConeLength, style.moveConeRadius);
            }
            AddMovePlaneHandles(mesh, style, scale);
            break;

        case GizmoMode::Rotate:
            AddPivotDot(mesh, style, view, scale);
            for (const Axis& axis : axes) {
                AddRingArc(mesh, axis.direction, axis.color, style.ringRadius,
                           style.ringSegments, view, style.ringTubeRadius, style.ringTubeSegments);
            }
            // The free ring always faces the viewer, so it is built in the
            // plane perpendicular to the view direction rather than to an axis,
            // and lifted toward the eye so it bounds the widget instead of
            // slicing through it.
            AddFullRing(mesh, view * (-kScreenRingLiftPixelsUVE * scale), view,
                        style.freeRingColor, style.freeRingRadius,
                        style.ringSegments, freeRingHalfWidth);
            break;

        case GizmoMode::Scale:
            AddPivotDot(mesh, style, view, scale);
            for (const Axis& axis : axes) {
                AddSolidCylinderUVE(mesh, axis.direction * style.scaleShaftStart,
                                    axis.direction * style.scaleShaftEnd, style.shaftRadius,
                                    style.shaftSegments, axis.color);
                AddSolidCube(mesh, axis.direction * style.scaleShaftEnd, style.scaleBoxSize, axis.color);
            }
            AddScalePlaneHandles(mesh, style);
            break;

        case GizmoMode::Universal:
            AddPivotDot(mesh, style, view, scale);
            for (const Axis& axis : axes) {
                // rotate: smallest radius, closest to the pivot
                AddRingArc(mesh, axis.direction, axis.color, style.universalRingRadius,
                           style.ringSegments, view, style.ringTubeRadius, style.ringTubeSegments);
                // move: reaches well out past the ring
                AddMoveArrow(mesh, axis, style,
                             style.universalShaftStart, style.universalShaftEnd,
                             style.universalConeLength, style.universalConeRadius);
                // scale: sits clear of the arrow tip, further out again
                AddSolidCube(mesh, axis.direction * style.universalScaleBoxOffset,
                             style.universalScaleBoxSize, axis.color);
            }
            break;
    }
    return mesh;
}

} // namespace univex::gizmo
