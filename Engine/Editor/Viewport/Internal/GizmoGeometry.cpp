#include "univex/gizmo/GizmoGeometry.h"

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

// How opaque the half of a rotation ring curving away from the camera stays, and how wide the
// band over which it fades there. Both are in the ring's own facing-dot space, where 0 is the
// silhouette and -1 is pointing straight at the eye.
constexpr float kRingFarAlphaUVE = 0.16f;
constexpr float kRingSilhouetteFeatherUVE = 0.42f;

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

void AddLine(GizmoMesh& mesh, const Vec3& a, const Vec3& b, const Vec3& color, float widthPx) {
    mesh.lines.push_back(GizmoLine{a, b, color, widthPx});
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

// A solid, axis-aligned cube: six filled faces plus twelve darker edges, so
// the handle reads as a body with a defined silhouette rather than a wire
// box that disappears against the grid.
void AddSolidCube(GizmoMesh& mesh, const Vec3& center, float size,
                  const Vec3& color, float edgeWidthPx) {
    const float h = size * 0.5f;
    struct Face { Vec3 n, u, v; };
    const std::array<Face, 6> faces = {{
        {{ 1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {{ 0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{ 0,-1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{ 0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{ 0, 0,-1}, {1, 0, 0}, {0, 1, 0}},
    }};
    for (const Face& f : faces) {
        const Vec3 c = center + f.n * h;
        AddQuad(mesh,
                c + f.u * -h + f.v * -h,
                c + f.u *  h + f.v * -h,
                c + f.u *  h + f.v *  h,
                c + f.u * -h + f.v *  h,
                color, 1.f);
    }

    const Vec3 edgeColor = color * 0.55f;
    for (int axis = 0; axis < 3; ++axis) {
        for (int i = 0; i < 4; ++i) {
            Vec3 a = center, b = center;
            const float s0 = (i & 1) ? h : -h;
            const float s1 = (i & 2) ? h : -h;
            const int a1 = (axis + 1) % 3;
            const int a2 = (axis + 2) % 3;
            float* pa[3] = {&a.x, &a.y, &a.z};
            float* pb[3] = {&b.x, &b.y, &b.z};
            *pa[a1] += s0; *pb[a1] += s0;
            *pa[a2] += s1; *pb[a2] += s1;
            *pa[axis] -= h; *pb[axis] += h;
            AddLine(mesh, a, b, edgeColor, edgeWidthPx);
        }
    }
}

// Arrow head: a cone of real triangles, capped so it stays solid when seen
// from behind. The base-circle outline (same darkened-edge convention as
// AddSolidCube's own 12 edges) gives the flat-shaded cone a defined
// silhouette instead of reading as a featureless colored blob - previously
// the only shape in this file with zero outline treatment at all.
void AddCone(GizmoMesh& mesh, const Vec3& baseCenter, const Vec3& axis,
             float length, float radius, int segments, const Vec3& color,
             float edgeWidthPx) {
    Vec3 u, v;
    PerpBasis(axis, u, v);
    const Vec3 tip = baseCenter + axis * length;
    const auto ring = CirclePoints(baseCenter, u, v, radius, segments);
    for (std::size_t i = 0; i + 1 < ring.size(); ++i) {
        AddTriangle(mesh, tip, ring[i], ring[i + 1], color, 1.f);
        AddTriangle(mesh, baseCenter, ring[i + 1], ring[i], color, 1.f);
    }
    const Vec3 edgeColor = color * 0.55f;
    for (std::size_t i = 0; i + 1 < ring.size(); ++i) {
        AddLine(mesh, ring[i], ring[i + 1], edgeColor, edgeWidthPx);
    }
}

void AddMoveArrow(GizmoMesh& mesh, const Axis& axis, const GizmoStyle& style,
                  float shaftStart, float shaftEnd, float coneLength, float coneRadius,
                  float lineWidthPx) {
    AddLine(mesh, axis.direction * shaftStart, axis.direction * shaftEnd,
            axis.color, lineWidthPx);
    AddCone(mesh, axis.direction * shaftEnd, axis.direction,
            coneLength, coneRadius, style.moveConeSegments, axis.color, style.cubeEdgeWidthPx);
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

// A whole ring, with the half that curves away from the camera faded rather than cut off.
//
// This used to drop the far segments outright, which did stop three overlapping circles reading
// as a ball of spaghetti - but it also ended each arc on a hard, square edge in mid-air, and that
// reads as a rendering fault rather than as depth. Fading keeps the ring continuous, so its shape
// is legible as a circle in 3D, while the near side still clearly dominates. The fade is smooth
// across the silhouette band so there is no visible seam where it turns over.
void AddRingArc(GizmoMesh& mesh, const Vec3& axis, const Vec3& color, float radius,
                int segments, const Vec3& viewDirection, float halfWidth) {
    AddAnnulus(mesh, Vec3{0.f, 0.f, 0.f}, axis, color, radius, halfWidth, segments,
               [&](const Vec3& a, const Vec3& b) {
                   // Facing is negative on the near side: viewDirection points away from the eye.
                   const float facing = (Dot(Normalize(a), viewDirection) +
                                         Dot(Normalize(b), viewDirection)) * 0.5f;
                   const float nearness = Clamp01UVE(-facing / kRingSilhouetteFeatherUVE);
                   const float smoothNearness = nearness * nearness * (3.f - 2.f * nearness);
                   return kRingFarAlphaUVE + (1.f - kRingFarAlphaUVE) * smoothNearness;
               });
}

void AddFullRing(GizmoMesh& mesh, const Vec3& center, const Vec3& axis, const Vec3& color,
                 float radius, int segments, float halfWidth) {
    AddAnnulus(mesh, center, axis, color, radius, halfWidth, segments,
               [](const Vec3&, const Vec3&) { return 1.f; });
}

// How far in front of the pivot the view-facing screen ring sits, in pixels. The ring is built in
// the plane perpendicular to the view, so centred on the pivot it passes exactly through the
// middle of the three axis rings and through the centre cube - with depth testing on, half of it
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

void AddMovePlaneHandles(GizmoMesh& mesh, const GizmoStyle& style) {
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
        AddQuad(mesh, p0, p1, p2, p3, fill, style.planeHandleAlpha);
        AddLine(mesh, p0, p1, edge, 1.1f);
        AddLine(mesh, p1, p2, edge, 1.1f);
        AddLine(mesh, p2, p3, edge, 1.1f);
        AddLine(mesh, p3, p0, edge, 1.1f);
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
        AddLine(mesh, p0, p1, PlaneEdgeColor(normalColor, style), 1.1f);
    }
}

// A small solid block at the pivot. It used to be a wire box, but twelve
// edges a few pixels long read as scribble rather than as a cube.
void AddCenterCube(GizmoMesh& mesh, const GizmoStyle& style) {
    AddSolidCube(mesh, Vec3{0.f, 0.f, 0.f}, style.centerCubeSize,
                 style.centerColor, style.centerCubeWidthPx);
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
    const float ringHalfWidth = style.ringLineWidthPx * 0.5f * scale;
    const float freeRingHalfWidth = style.freeRingWidthPx * 0.5f * scale;
    const float universalRingHalfWidth = style.universalRingWidthPx * 0.5f * scale;

    switch (mode) {
        case GizmoMode::Select:
            AddCenterCube(mesh, style);
            break;

        case GizmoMode::Move:
            AddCenterCube(mesh, style);
            for (const Axis& axis : axes) {
                AddMoveArrow(mesh, axis, style,
                             style.moveShaftStart, style.moveShaftEnd,
                             style.moveConeLength, style.moveConeRadius,
                             style.axisLineWidthPx);
            }
            AddMovePlaneHandles(mesh, style);
            break;

        case GizmoMode::Rotate:
            AddCenterCube(mesh, style);
            for (const Axis& axis : axes) {
                AddRingArc(mesh, axis.direction, axis.color, style.ringRadius,
                           style.ringSegments, view, ringHalfWidth);
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
            AddCenterCube(mesh, style);
            for (const Axis& axis : axes) {
                AddLine(mesh, axis.direction * style.scaleShaftStart,
                        axis.direction * style.scaleShaftEnd, axis.color, style.axisLineWidthPx);
                AddSolidCube(mesh, axis.direction * style.scaleShaftEnd, style.scaleBoxSize,
                             axis.color, style.cubeEdgeWidthPx);
            }
            AddScalePlaneHandles(mesh, style);
            break;

        case GizmoMode::Universal:
            AddCenterCube(mesh, style);
            for (const Axis& axis : axes) {
                // rotate: smallest radius, closest to the pivot
                AddRingArc(mesh, axis.direction, axis.color, style.universalRingRadius,
                           style.ringSegments, view, universalRingHalfWidth);
                // move: reaches well out past the ring
                AddMoveArrow(mesh, axis, style,
                             style.universalShaftStart, style.universalShaftEnd,
                             style.universalConeLength, style.universalConeRadius,
                             style.universalLineWidthPx);
                // scale: sits clear of the arrow tip, further out again
                AddSolidCube(mesh, axis.direction * style.universalScaleBoxOffset,
                             style.universalScaleBoxSize, axis.color, style.cubeEdgeWidthPx);
            }
            break;
    }
    return mesh;
}

} // namespace univex::gizmo
