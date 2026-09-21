// univex/gizmo/GizmoPicking.h
// -----------------------------------------------------------------------
// Which transform handle a cursor ray is over.
//
// The transform gizmo was drawable but not touchable: BuildGizmoMesh produced geometry and
// nothing ever asked what the pointer was on, so the widget was an illustration of an operation
// rather than a way to perform one. This is the missing half, kept beside PickNavGizmo in the
// engine-agnostic core for the same reason that one lives here - it is pure geometry, and a host
// that wants to drive a transform needs it before it can route any input at all.
//
// The hit regions are derived from the same GizmoStyle values BuildGizmoMesh draws from, so what
// is grabbable is what is visible. They are deliberately a little more generous than the drawn
// geometry: a 2.3-pixel-wide shaft that only responds when the cursor is within 2.3 pixels of its
// centre line is technically correct and miserable to use.
// -----------------------------------------------------------------------
#pragma once

#include <optional>

#include "univex/gizmo/GizmoGeometry.h"
#include "univex/gizmo/GizmoStyle.h"
#include "univex/math/Vec.h"

namespace univex::gizmo {

using univex::math::Vec3;

/// Which part of the widget a ray hit. The axis and plane handles name the axis (or axis pair)
/// they act along; Uniform is the centre handle, which acts on all three at once; ScreenRing is
/// the outer ring of the rotate gizmo, which turns around the view direction.
enum class GizmoHandleUVE {
    None,
    AxisX,
    AxisY,
    AxisZ,
    PlaneXY,
    PlaneYZ,
    PlaneZX,
    ScreenRing,
    Uniform,
};

/// True for the three single-axis handles, which is the distinction almost every caller wants.
[[nodiscard]] bool IsAxisHandleUVE(GizmoHandleUVE handle) noexcept;

/// The axis a handle acts along, for the three single-axis handles. Returns nullopt otherwise -
/// a plane or uniform handle has no single axis, and silently answering X would be worse than
/// making the caller say what it means.
[[nodiscard]] std::optional<Vec3> AxisDirectionForHandleUVE(GizmoHandleUVE handle) noexcept;

struct GizmoPickResultUVE {
    GizmoHandleUVE handle = GizmoHandleUVE::None;
    /// Distance along the ray to the hit, in world units. Only meaningful when handle != None.
    float rayDistance = 0.f;
};

/// Picks the handle under a world-space cursor ray.
///
/// `pivot` and `scale` are the same values the renderer draws with (GizmoDrawParams::origin and
/// ::scale), `viewDirection` points from the eye into the scene, and `unitsPerPixel` converts a
/// pixel width into gizmo units at the pivot - again exactly as BuildGizmoMesh takes them, so the
/// hit regions track the widget as it holds its on-screen size at any distance.
///
/// `rayDirection` need not be normalised. Ties are resolved by which handle is nearer along the
/// ray, except that the centre handle wins over the axes it sits between: it is the smallest
/// target on screen and is otherwise nearly impossible to hit.
[[nodiscard]] GizmoPickResultUVE PickGizmoHandleUVE(GizmoMode mode, const GizmoStyle& style,
                                                     const Vec3& rayOrigin, const Vec3& rayDirection,
                                                     const Vec3& pivot, float scale,
                                                     const Vec3& viewDirection, float unitsPerPixel);

} // namespace univex::gizmo
