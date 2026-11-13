// Internal/BoneShape.cpp - see univex/gizmo/BoneShape.h.
#include "univex/gizmo/bone_shape.h"

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

/// A ring of strokes facing the eye: a joint drawn as an outline, so the model shows through it.
void PushRingUVE(GizmoMesh& mesh, const Vec3& center, float radius, const Vec3& right, const Vec3& up,
                 const Vec3& color, float widthPx, int segments) {
    constexpr float kTwoPi = 2.f * std::numbers::pi_v<float>;
    Vec3 previous = center + right * radius;
    for (int i = 1; i <= segments; ++i) {
        const float t = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
        const Vec3 current = center + right * (std::cos(t) * radius) + up * (std::sin(t) * radius);
        mesh.lines.push_back(GizmoLine{previous, current, color, widthPx});
        previous = current;
    }
}

} // namespace

GizmoMesh BuildBoneMeshUVE(const std::vector<BoneOverlayUVE>& bones, const Vec3& viewDirection,
                           const BoneStyleUVE& style) {
    // Outlines only - no faces - so the model is never hidden behind its own skeleton.
    GizmoMesh mesh;
    mesh.lines.reserve(bones.size() * static_cast<std::size_t>(12 + style.jointSegments));
    const Vec3 view = Normalize(viewDirection);
    const Vec3 right = PerpendicularUVE(view, Vec3{0.f, 1.f, 0.f});
    const Vec3 up = Cross(right, view);
    for (const BoneOverlayUVE& bone : bones) {
        const Vec3 color = bone.hasColor        ? bone.color
                           : bone.boneSelected  ? style.activeBoneColor
                           : bone.skeletonSelected ? style.selectedColor
                                                   : style.idleColor;
        const float widthPx = bone.boneSelected ? style.selectedLinePx : style.linePx;
        if (bone.helper) {
            // Always the helper colour (cyan), so a helper never reads as a body joint; orange when picked.
            const Vec3 mark = bone.boneSelected ? style.activeBoneColor : style.helperColor;
            if (bone.hasLink && Length(bone.head - bone.linkFrom) > 1e-4f) {
                // Dashed, so a link across the figure (a hand target to its root) stays light.
                constexpr int kDashes = 24;
                const Vec3 step = (bone.head - bone.linkFrom) / static_cast<float>(kDashes * 2);
                for (int i = 0; i < kDashes; ++i) {
                    const Vec3 a = bone.linkFrom + step * static_cast<float>(i * 2);
                    mesh.lines.push_back(GizmoLine{a, a + step, mark * 0.8f, 1.f});
                }
            }
            const float r = style.helperRadius;
            const Vec3 c = bone.head - view * (r * 1.5f);
            PushRingUVE(mesh, c, r, right, up, mark, widthPx, style.jointSegments);
            mesh.lines.push_back(GizmoLine{c - right * (r * 1.6f), c + right * (r * 1.6f), mark, widthPx});
            mesh.lines.push_back(GizmoLine{c - up * (r * 1.6f), c + up * (r * 1.6f), mark, widthPx});
            continue;
        }
        if (bone.hasLink && Length(bone.head - bone.linkFrom) > 1e-4f) {
            mesh.lines.push_back(GizmoLine{bone.linkFrom, bone.head, style.linkColor, style.linkPx});
        }
        const Vec3 along = bone.tail - bone.head;
        const float length = Length(along);
        if (length < 1e-3f) {
            continue;
        }
        if (bone.jointOnly) {
            const float radius = std::clamp(length * style.jointRatio * 1.4f, style.minimumRadius * 0.8f, style.maximumRadius * 0.7f);
            PushRingUVE(mesh, bone.head - view * (radius * 1.6f), radius, right, up, color, widthPx, style.jointSegments);
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
            mesh.lines.push_back(GizmoLine{bone.head, a, color, widthPx});
            mesh.lines.push_back(GizmoLine{a, bone.tail, color, widthPx});
            mesh.lines.push_back(GizmoLine{a, b, color, widthPx});
        }
        // The joint: a ring turned to the eye, a little thicker than the blade.
        const float radius = std::clamp(length * style.jointRatio, style.minimumRadius, style.maximumRadius);
        PushRingUVE(mesh, bone.head - view * (radius * 1.2f), radius, right, up, color, widthPx + 0.6f, style.jointSegments);
    }
    return mesh;
}

} // namespace univex::gizmo
