// univex/gizmo/BoneShape.h
// -----------------------------------------------------------------------
// The editor's bone: how a Skeleton3D's bones look in the viewport.
//
// One look for every rig that comes in, so a skeleton reads the same
// whatever tool exported it. Drawn as outlines only - no faces - so the model
// is always seen through its skeleton, and a full rig costs next to nothing:
//
//   - a slim diamond of strokes from head to tail, widest a fifth of the way
//     along, in the bone's colour;
//   - a ring at the head, always facing the camera, a little bolder;
//   - the picked bone in thicker strokes;
//   - helper bones (root, IK targets, sockets) as a ringed cross, tied to
//     their parent by a dashed line;
//   - a thin link from a parent's tail to a child that does not start
//     there, so a disconnected bone still shows whom it belongs to;
//   - a joint alone, smaller, for a bone that sits on the way down
//     another (a twist bone half-way along the arm).
//
// Neutral greys, so a rig never competes with the model for colour: the
// selected skeleton in light grey, every other skeleton in charcoal, and the
// bone picked in the Inspector in orange - the one accent, found at a glance.
//
// Pure geometry - no GL - so it is testable and the renderer only draws.
// -----------------------------------------------------------------------
#pragma once

#include <vector>

#include "univex/gizmo/gizmo_geometry.h"
#include "univex/math/vec.h"

namespace univex::gizmo {

using univex::math::Vec3;

/// One bone in world space, as the editor hands it over.
struct BoneOverlayUVE {
    Vec3 head{};
    Vec3 tail{};
    /// The bone's own X axis in world space: orients the diamond's edges, so a bone's roll is
    /// visible. Need not be unit length; a zero vector falls back to any perpendicular.
    Vec3 side{1.f, 0.f, 0.f};
    /// The parent's tail, when this bone does not start there: a thin link is drawn back to it.
    bool hasLink = false;
    Vec3 linkFrom{};
    bool skeletonSelected = false;
    bool boneSelected = false;
    /// A colour of its own, over the selection look (the Retarget window's joint colours).
    bool hasColor = false;
    Vec3 color{};
    /// Only the joint, no blade (a twist bone on the way down its limb); `tail` still sets its size.
    bool jointOnly = false;
    /// A helper bone (root, IK target, socket): a ringed cross marker of a fixed size, and its link
    /// (when hasLink) dashed, so it shows without cutting across the model.
    bool helper = false;
};

/// Colours and proportions of the bone. One value, so the look is defined in one place.
struct BoneStyleUVE {
    Vec3 selectedColor{0.74f, 0.75f, 0.77f};   // light grey
    Vec3 activeBoneColor{1.00f, 0.56f, 0.14f}; // orange - the only accent
    Vec3 idleColor{0.33f, 0.34f, 0.36f};       // charcoal
    Vec3 linkColor{0.52f, 0.53f, 0.55f};
    float outlineShade = 0.32f;      // outline and joint rim, as a fraction of the bone's colour
    float widthRatio = 0.09f;        // diamond half-width as a fraction of the bone's length
    float widestAt = 0.20f;          // where along the bone the diamond is widest
    float jointRatio = 0.075f;       // joint radius as a fraction of the bone's length
    float minimumRadius = 0.012f;    // world units - a tiny bone stays visible
    float maximumRadius = 0.07f;     // and a huge one does not swallow the view
    int jointSegments = 16;
    float outlinePx = 1.25f;
    float linePx = 1.4f;             // a bone's strokes
    float selectedLinePx = 2.6f;     // the picked bone's strokes
    Vec3 helperColor{0.35f, 0.85f, 1.00f};  // helpers with no colour of their own: cyan
    float helperRadius = 0.035f;     // world units
    float linkPx = 1.f;
};

/// Builds the faces and strokes for `bones`, with joints turned toward `viewDirection` (eye into
/// the scene). Bones shorter than a thousandth of a unit are skipped - they have no direction.
[[nodiscard]] GizmoMesh BuildBoneMeshUVE(const std::vector<BoneOverlayUVE>& bones, const Vec3& viewDirection,
                                         const BoneStyleUVE& style = {});

} // namespace univex::gizmo
