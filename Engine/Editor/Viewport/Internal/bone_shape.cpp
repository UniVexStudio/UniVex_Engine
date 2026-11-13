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

} // namespace

GizmoMesh BuildBoneMeshUVE(const std::vector<BoneOverlayUVE>& bones, const Vec3& viewDirection,
                           const BoneStyleUVE& style) {
    // Outlines only - no faces - so the model is never hidden behind its own skeleton. Every
    // stroke has a dark stroke under it (all of those first), so the skeleton reads crisply over a
    // bright model and over the dark grid alike.
    GizmoMesh shadow;
    GizmoMesh mesh;
    const Vec3 view = Normalize(viewDirection);
    const Vec3 right = PerpendicularUVE(view, Vec3{0.f, 1.f, 0.f});
    const Vec3 up = Cross(right, view);
    const Vec3 dark{0.04f, 0.045f, 0.055f};
    const auto stroke = [&](const Vec3& a, const Vec3& b, const Vec3& color, const float widthPx) {
        shadow.lines.push_back(GizmoLine{a, b, dark, widthPx + style.shadowPx});
        mesh.lines.push_back(GizmoLine{a, b, color, widthPx});
    };
    const auto ring = [&](const Vec3& center, const float radius, const Vec3& color, const float widthPx) {
        constexpr float kTwoPi = 2.f * std::numbers::pi_v<float>;
        Vec3 previous = center + right * radius;
        for (int i = 1; i <= style.jointSegments; ++i) {
            const float t = kTwoPi * static_cast<float>(i) / static_cast<float>(style.jointSegments);
            const Vec3 current = center + right * (std::cos(t) * radius) + up * (std::sin(t) * radius);
            stroke(previous, current, color, widthPx);
            previous = current;
        }
    };
    const auto colorOf = [&style](const BoneOverlayUVE& bone) {
        return bone.hasColor ? bone.color
               : bone.boneSelected ? style.activeBoneColor
               : bone.skeletonSelected ? style.selectedColor
                                       : style.idleColor;
    };

    // One ring per joint, however many bones leave it, sized by the longest of them: a hip has one
    // ring, not three of different sizes.
    struct JointUVE {
        Vec3 at;
        float radius;
        Vec3 color;
        float widthPx;
    };
    std::vector<JointUVE> joints;
    const auto jointAt = [&joints](const Vec3& p) -> JointUVE* {
        for (JointUVE& joint : joints) {
            if (Length(joint.at - p) < 1e-5f) {
                return &joint;
            }
        }
        return nullptr;
    };
    for (const BoneOverlayUVE& bone : bones) {
        const float length = Length(bone.tail - bone.head);
        if (bone.helper || bone.jointOnly || length < 1e-3f) {
            continue;
        }
        const float radius = std::clamp(length * style.jointRatio, style.jointMinRadius, style.jointMaxRadius);
        const float widthPx = bone.boneSelected ? style.selectedLinePx : style.linePx;
        if (JointUVE* joint = jointAt(bone.head); joint != nullptr) {
            joint->radius = std::max(joint->radius, radius);
            if (bone.boneSelected) {
                joint->color = colorOf(bone);
                joint->widthPx = widthPx;
            }
        } else {
            joints.push_back(JointUVE{bone.head, radius, colorOf(bone), widthPx});
        }
    }

    for (const BoneOverlayUVE& bone : bones) {
        const Vec3 color = colorOf(bone);
        const float widthPx = bone.boneSelected ? style.selectedLinePx : style.linePx;
        if (bone.helper) {
            // A ringed cross in the helper colour (cyan; orange when picked), tied to its parent by
            // a dashed line so a link across the figure stays light.
            const Vec3 mark = bone.boneSelected ? style.activeBoneColor : style.helperColor;
            if (bone.hasLink && Length(bone.head - bone.linkFrom) > 1e-4f) {
                constexpr int kDashes = 24;
                const Vec3 step = (bone.head - bone.linkFrom) / static_cast<float>(kDashes * 2);
                for (int i = 0; i < kDashes; ++i) {
                    const Vec3 a = bone.linkFrom + step * static_cast<float>(i * 2);
                    mesh.lines.push_back(GizmoLine{a, a + step, mark * 0.8f, 1.f});
                }
            }
            const float r = style.helperRadius;
            const Vec3 c = bone.head - view * (r * 1.5f);
            ring(c, r, mark, widthPx);
            stroke(c - right * (r * 1.6f), c + right * (r * 1.6f), mark, widthPx);
            stroke(c - up * (r * 1.6f), c + up * (r * 1.6f), mark, widthPx);
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
            // A twist bone on the way down its limb: a small ring on the blade.
            ring(bone.head - view * 0.01f, std::clamp(length * 0.06f, style.jointMinRadius * 0.8f, style.jointMaxRadius * 0.45f), color,
                 widthPx);
            continue;
        }
        // The blade: a slim kite in the plane facing the eye, from the edge of its joint's ring to
        // the edge of the next one - four clean strokes, nothing crossing inside the joints.
        const Vec3 axis = along / length;
        Vec3 across = Cross(axis, view);
        across = Length(across) > 1e-4f ? Normalize(across) : right;
        const JointUVE* const from = jointAt(bone.head);
        const JointUVE* const to = jointAt(bone.tail);
        const float startGap = from != nullptr ? from->radius : 0.f;
        const float endGap = to != nullptr ? to->radius : 0.f;
        if (length <= startGap + endGap + 1e-3f) {
            continue;
        }
        const Vec3 start = bone.head + axis * startGap;
        const Vec3 end = bone.tail - axis * endGap;
        const float span = length - startGap - endGap;
        const float halfWidth = std::clamp(length * style.widthRatio, style.jointMinRadius * 0.6f, style.jointMaxRadius * 0.8f);
        const Vec3 waist = start + axis * (span * style.widestAt);
        stroke(start, waist + across * halfWidth, color, widthPx);
        stroke(waist + across * halfWidth, end, color, widthPx);
        stroke(end, waist - across * halfWidth, color, widthPx);
        stroke(waist - across * halfWidth, start, color, widthPx);
    }
    for (const JointUVE& joint : joints) {
        ring(joint.at - view * (joint.radius * 0.5f), joint.radius, joint.color, joint.widthPx + 0.4f);
    }
    // The dark strokes first, then the colours over them.
    shadow.lines.insert(shadow.lines.end(), mesh.lines.begin(), mesh.lines.end());
    return shadow;
}

} // namespace univex::gizmo
