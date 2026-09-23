// Internal/BoneShape.cpp - see univex/gizmo/BoneShape.h.
#include "univex/gizmo/BoneShape.h"

#include <algorithm>
#include <array>
#include <cmath>

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

} // namespace

GizmoMesh BuildBoneMeshUVE(const std::vector<BoneOverlayUVE>& bones, const BoneStyleUVE& style) {
    GizmoMesh mesh;
    mesh.lines.reserve(bones.size() * 13U);
    mesh.triangles.reserve(bones.size() * 16U);
    for (const BoneOverlayUVE& bone : bones) {
        const Vec3 color = bone.boneSelected ? style.activeBoneColor
                           : bone.skeletonSelected ? style.selectedColor
                                                   : style.idleColor;
        const float alpha = bone.skeletonSelected ? style.fillAlphaSelected : style.fillAlphaIdle;
        if (bone.hasLink && Length(bone.head - bone.linkFrom) > 1e-4f) {
            mesh.lines.push_back(GizmoLine{bone.linkFrom, bone.head, color * 0.8f, style.linkPx});
        }
        const Vec3 along = bone.tail - bone.head;
        const float length = Length(along);
        if (length < 1e-3f) {
            continue;
        }
        const Vec3 axis = along / length;
        const Vec3 u = PerpendicularUVE(axis, bone.side);
        const Vec3 v = Cross(axis, u);
        const float halfWidth = std::clamp(length * style.widthRatio, style.minimumHalfWidth, style.maximumHalfWidth);
        const Vec3 waist = bone.head + axis * (length * style.widestAt);
        const std::array<Vec3, 4> ring{waist + u * halfWidth, waist + v * halfWidth, waist - u * halfWidth,
                                       waist - v * halfWidth};

        // The spindle: four faces to the head, four to the tail, and the eight edges outlined so
        // the shape reads even where the translucent fill is thin.
        for (std::size_t i = 0U; i < ring.size(); ++i) {
            const Vec3& a = ring[i];
            const Vec3& b = ring[(i + 1U) % ring.size()];
            // Faces lit by which way they point, so the solid reads as a solid without a lighting pass.
            const Vec3 shadeHead = color * (0.75f + 0.25f * static_cast<float>(i % 2U));
            const Vec3 shadeTail = color * (0.85f + 0.15f * static_cast<float>(i % 2U));
            mesh.triangles.push_back(GizmoTriangle{bone.head, a, b, shadeHead, alpha});
            mesh.triangles.push_back(GizmoTriangle{a, bone.tail, b, shadeTail, alpha});
            mesh.lines.push_back(GizmoLine{a, b, color, style.outlinePx});
            mesh.lines.push_back(GizmoLine{bone.head, a, color, style.outlinePx});
            mesh.lines.push_back(GizmoLine{a, bone.tail, color, style.outlinePx});
        }

        // The joint: a small opaque diamond at the head.
        const float radius = std::clamp(length * style.jointRatio, style.minimumHalfWidth * 0.8f,
                                        style.maximumHalfWidth * 0.6f);
        const std::array<Vec3, 6> tips{bone.head + axis * radius, bone.head - axis * radius, bone.head + u * radius,
                                       bone.head + v * radius,    bone.head - u * radius,    bone.head - v * radius};
        const Vec3 jointColor = color * 1.1f;
        for (std::size_t i = 0U; i < 4U; ++i) {
            const Vec3& a = tips[2U + i];
            const Vec3& b = tips[2U + ((i + 1U) % 4U)];
            mesh.triangles.push_back(GizmoTriangle{tips[0], a, b, jointColor, 1.f});
            mesh.triangles.push_back(GizmoTriangle{tips[1], b, a, jointColor * 0.8f, 1.f});
        }
    }
    return mesh;
}

} // namespace univex::gizmo
