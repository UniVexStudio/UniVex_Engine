// Internal/BoneShape.cpp - see univex/gizmo/BoneShape.h.
#include "univex/gizmo/BoneShape.h"

#include <algorithm>

#include "univex/gizmo/SolidPrimitives.h"

namespace univex::gizmo {

using univex::math::Length;

GizmoMesh BuildBoneMeshUVE(const std::vector<BoneOverlayUVE>& bones, const BoneStyleUVE& style) {
    GizmoMesh mesh;
    const std::size_t perBone = static_cast<std::size_t>(style.jointRings * style.jointSegments * 2 +
                                                         style.coneSegments * 2);
    mesh.triangles.reserve(bones.size() * perBone);
    for (const BoneOverlayUVE& bone : bones) {
        const Vec3 color = bone.boneSelected ? style.activeBoneColor
                           : bone.skeletonSelected ? style.selectedColor
                                                   : style.idleColor;
        if (bone.hasLink && Length(bone.head - bone.linkFrom) > 1e-4f) {
            mesh.lines.push_back(GizmoLine{bone.linkFrom, bone.head, style.linkColor, style.linkPx});
        }
        const Vec3 along = bone.tail - bone.head;
        const float length = Length(along);
        if (length < 1e-3f) {
            continue;
        }
        const Vec3 axis = along / length;
        const float jointRadius = std::clamp(length * style.jointRatio, style.minimumRadius, style.maximumRadius);
        AddSolidSphereUVE(mesh, bone.head, jointRadius, style.jointRings, style.jointSegments, color);
        // The cone starts at the sphere's surface, so the two read as one piece.
        AddSolidConeUVE(mesh, bone.head + axis * (jointRadius * 0.6f), bone.tail, jointRadius * style.coneBaseRatio,
                        style.coneSegments, color);
    }
    return mesh;
}

} // namespace univex::gizmo
