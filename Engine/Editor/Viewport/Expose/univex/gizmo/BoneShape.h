// univex/gizmo/BoneShape.h
// -----------------------------------------------------------------------
// The editor's bone: how a Skeleton3D's bones look in the viewport.
//
// One shape for every rig that comes in, so a skeleton reads the same
// whatever tool exported it:
//
//   - a slim four-sided spindle from head to tail, widest a fifth of the
//     way along, so the direction a bone points is obvious at a glance;
//   - a small diamond joint at the head, where the bone rotates;
//   - a thin link from a parent's tail to a child that does not start
//     there, so a disconnected bone still shows whom it belongs to.
//
// Width follows length (clamped), so fingers and spines keep the same
// proportions. Colour carries state: the selected skeleton's bones in the
// engine's teal, the bone picked in the Inspector in amber, every other
// skeleton in a quiet slate so it stays out of the way.
//
// Pure geometry - no GL - so it is testable and the renderer only draws.
// -----------------------------------------------------------------------
#pragma once

#include <vector>

#include "univex/gizmo/GizmoGeometry.h"
#include "univex/math/Vec.h"

namespace univex::gizmo {

using univex::math::Vec3;

/// One bone in world space, as the editor hands it over.
struct BoneOverlayUVE {
    Vec3 head{};
    Vec3 tail{};
    /// The bone's own X axis in world space: fixes which way the spindle's edges face, so a bone's
    /// roll is visible. Need not be unit length or exactly perpendicular; a zero vector falls back
    /// to any perpendicular.
    Vec3 side{1.f, 0.f, 0.f};
    /// The parent's tail, when this bone does not start there: a thin link is drawn back to it.
    bool hasLink = false;
    Vec3 linkFrom{};
    bool skeletonSelected = false;
    bool boneSelected = false;
};

/// Colours and proportions of the bone. One value, so the look is defined in one place.
struct BoneStyleUVE {
    Vec3 selectedColor{0.30f, 0.80f, 0.88f};   // UniVex teal
    Vec3 activeBoneColor{1.00f, 0.70f, 0.24f}; // amber
    Vec3 idleColor{0.46f, 0.53f, 0.62f};       // slate
    float fillAlphaSelected = 0.42f;
    float fillAlphaIdle = 0.22f;
    float widthRatio = 0.10f;        // spindle half-width as a fraction of the bone's length
    float widestAt = 0.20f;          // where along the bone the spindle is widest
    float jointRatio = 0.055f;       // joint diamond radius as a fraction of length
    float minimumHalfWidth = 0.004f; // world units - a tiny bone stays visible
    float maximumHalfWidth = 0.12f;  // and a huge one does not swallow the view
    float outlinePx = 1.5f;
    float linkPx = 1.f;
};

/// Builds the lines and triangles for `bones`. Bones shorter than a thousandth of a unit are
/// skipped - they have no direction to draw.
[[nodiscard]] GizmoMesh BuildBoneMeshUVE(const std::vector<BoneOverlayUVE>& bones, const BoneStyleUVE& style = {});

} // namespace univex::gizmo
