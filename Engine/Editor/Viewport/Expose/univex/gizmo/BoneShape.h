// univex/gizmo/BoneShape.h
// -----------------------------------------------------------------------
// The editor's bone: how a Skeleton3D's bones look in the viewport.
//
// One shape for every rig that comes in, so a skeleton reads the same
// whatever tool exported it. Solid, lit geometry - not strokes:
//
//   - a sphere at the head, where the bone rotates;
//   - a round cone from that sphere to the tail, so the direction a bone
//     points is obvious at a glance;
//   - a thin link from a parent's tail to a child that does not start
//     there, so a disconnected bone still shows whom it belongs to.
//
// Neutral greys, so a rig never competes with the model for colour: the
// selected skeleton in light grey, every other skeleton in charcoal, and the
// bone picked in the Inspector in orange - the one accent, so it is found at
// a glance. Joint size follows the bone's length (clamped), so fingers and
// spines keep the same proportions.
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
    /// The bone's own X axis in world space (its roll). Carried for tools that need it; the round
    /// shape itself does not depend on it.
    Vec3 side{1.f, 0.f, 0.f};
    /// The parent's tail, when this bone does not start there: a thin link is drawn back to it.
    bool hasLink = false;
    Vec3 linkFrom{};
    bool skeletonSelected = false;
    bool boneSelected = false;
};

/// Colours and proportions of the bone. One value, so the look is defined in one place.
struct BoneStyleUVE {
    Vec3 selectedColor{0.74f, 0.75f, 0.77f};   // light grey
    Vec3 activeBoneColor{1.00f, 0.56f, 0.14f}; // orange - the only accent
    Vec3 idleColor{0.27f, 0.28f, 0.30f};       // charcoal
    Vec3 linkColor{0.55f, 0.56f, 0.58f};
    float jointRatio = 0.14f;        // joint sphere radius as a fraction of the bone's length
    float coneBaseRatio = 0.9f;      // cone base radius as a fraction of the joint radius
    float minimumRadius = 0.018f;    // world units - a tiny bone stays visible
    float maximumRadius = 0.09f;     // and a huge one does not swallow the view
    int jointRings = 6;
    int jointSegments = 10;
    int coneSegments = 10;
    float linkPx = 1.25f;
};

/// Builds the lit solids (and link lines) for `bones`. Bones shorter than a thousandth of a unit
/// are skipped - they have no direction to draw.
[[nodiscard]] GizmoMesh BuildBoneMeshUVE(const std::vector<BoneOverlayUVE>& bones, const BoneStyleUVE& style = {});

} // namespace univex::gizmo
