// Internal/BoneShape.cpp - see univex/gizmo/BoneShape.h.
#include "univex/gizmo/BoneShape.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace univex::gizmo {
namespace {

using univex::math::Cross;
using univex::math::Dot;
using univex::math::Length;
using univex::math::Normalize;

/// A unit vector perpendicular to `axis`, preferring `hint` projected off the axis.
[[nodiscard]] Vec3 PerpendicularUVE(const Vec3& axis, const Vec3& hint) {
    Vec3 side = hint - axis * Dot(hint, axis);
    if (Length(side) < 1e-5f) {
        side = std::fabs(axis.y) < 0.9f ? Cross(axis, Vec3{0.f, 1.f, 0.f}) : Cross(axis, Vec3{1.f, 0.f, 0.f});
    }
    return Normalize(side);
}

void PushLitUVE(GizmoMesh& mesh, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& color) {
    GizmoTriangle triangle{a, b, c, color, 1.f};
    triangle.lit = 1.f;
    mesh.triangles.push_back(triangle);
}

/// A flat disc facing the eye, lifted `lift` toward it.
void PushDiscUVE(GizmoMesh& mesh, const Vec3& center, float radius, const Vec3& right, const Vec3& up,
                 const Vec3& color, int segments) {
    constexpr float kTwoPi = 2.f * std::numbers::pi_v<float>;
    Vec3 previous = center + right * radius;
    for (int i = 1; i <= segments; ++i) {
        const float t = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
        const Vec3 current = center + right * (std::cos(t) * radius) + up * (std::sin(t) * radius);
        mesh.triangles.push_back(GizmoTriangle{center, previous, current, color, 1.f});
        previous = current;
    }
}

} // namespace

GizmoMesh BuildBoneMeshUVE(const std::vector<BoneOverlayUVE>& bones, const Vec3& viewDirection,
                           const BoneStyleUVE& style) {
    GizmoMesh mesh;
    mesh.triangles.reserve(bones.size() * static_cast<std::size_t>(8 + style.jointSegments * 2));
    mesh.lines.reserve(bones.size() * 9U);
    const Vec3 view = Normalize(viewDirection);
    const Vec3 right = PerpendicularUVE(view, Vec3{0.f, 1.f, 0.f});
    const Vec3 up = Cross(right, view);
    for (const BoneOverlayUVE& bone : bones) {
        const Vec3 color = bone.boneSelected ? style.activeBoneColor
                           : bone.skeletonSelected ? style.selectedColor
                                                   : style.idleColor;
        const Vec3 outline = color * style.outlineShade;
        if (bone.hasLink && Length(bone.head - bone.linkFrom) > 1e-4f) {
            mesh.lines.push_back(GizmoLine{bone.linkFrom, bone.head, style.linkColor, style.linkPx});
        }
        const Vec3 along = bone.tail - bone.head;
        const float length = Length(along);
        if (length < 1e-3f) {
            continue;
        }
        const Vec3 axis = along / length;
        const Vec3 u = PerpendicularUVE(axis, bone.side);
        const Vec3 v = Cross(axis, u);
        const float halfWidth = std::clamp(length * style.widthRatio, style.minimumRadius * 0.8f, style.maximumRadius);
        const Vec3 waist = bone.head + axis * (length * style.widestAt);
        const std::array<Vec3, 4> ring{waist + u * halfWidth, waist + v * halfWidth, waist - u * halfWidth,
                                       waist - v * halfWidth};
        for (std::size_t i = 0U; i < ring.size(); ++i) {
            const Vec3& a = ring[i];
            const Vec3& b = ring[(i + 1U) % ring.size()];
            PushLitUVE(mesh, bone.head, a, b, color);
            PushLitUVE(mesh, a, bone.tail, b, color);
            mesh.lines.push_back(GizmoLine{a, bone.tail, outline, style.outlinePx});
            mesh.lines.push_back(GizmoLine{a, b, outline, style.outlinePx});
        }

        // The joint: a dark rim disc with the bone's colour inside, turned to the eye and lifted
        // a little so it sits over the diamond's root rather than inside it.
        const float radius = std::clamp(length * style.jointRatio, style.minimumRadius, style.maximumRadius);
        const Vec3 center = bone.head - view * (radius * 1.2f);
        PushDiscUVE(mesh, center, radius, right, up, outline, style.jointSegments);
        PushDiscUVE(mesh, center - view * (radius * 0.05f), radius * 0.72f, right, up, color, style.jointSegments);
    }
    return mesh;
}

} // namespace univex::gizmo
