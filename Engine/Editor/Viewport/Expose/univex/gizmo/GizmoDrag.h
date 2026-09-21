// univex/gizmo/GizmoDrag.h
// -----------------------------------------------------------------------
// Turning cursor movement into a transform amount.
//
// PickGizmoHandleUVE answers which handle the pointer grabbed; this answers how far it has since
// dragged that handle. The three questions are the three kinds of handle: how far along an axis,
// where on a plane, and how far around a ring.
//
// Each returns a value in the handle's own terms - a signed distance along the axis, a world point
// on the plane, an angle around the ring - measured from the gizmo's pivot rather than from the
// press. The caller subtracts its own press-time value, which is what keeps a drag absolute: every
// frame reports the total offset from where the gesture began, never an increment, so a dropped or
// duplicated frame cannot accumulate error.
//
// All three return nullopt when the view is too close to edge-on for the answer to be meaningful:
// a ray nearly parallel to an axis projects onto it with unbounded sensitivity, so a pixel of
// cursor movement would fling the object across the world. Returning nothing lets the caller hold
// its last good value, which is what every editor does at those angles.
// -----------------------------------------------------------------------
#pragma once

#include <optional>

#include "univex/math/Vec.h"

namespace univex::gizmo {

using univex::math::Vec3;

/// How far along `axis` from `pivot` the cursor ray's closest approach to that axis line lies.
/// `axis` must be unit length; `rayDirection` need not be.
[[nodiscard]] std::optional<float> ProjectRayOntoAxisUVE(const Vec3& rayOrigin, const Vec3& rayDirection,
                                                          const Vec3& pivot, const Vec3& axis);

/// Where the cursor ray crosses the plane through `pivot` with normal `planeNormal`.
[[nodiscard]] std::optional<Vec3> ProjectRayOntoPlaneUVE(const Vec3& rayOrigin, const Vec3& rayDirection,
                                                          const Vec3& pivot, const Vec3& planeNormal);

/// The angle, in radians, at which the cursor ray crosses the plane of the ring whose normal is
/// `axis`. Measured around `axis` in a basis derived from it alone, so the value is stable for a
/// fixed axis and comparable between two calls - which is all a drag needs, since it only ever
/// uses the difference between the current angle and the one captured at the press.
///
/// The result is continuous except for one wrap at +-pi. A caller accumulating rotation across
/// more than half a turn must unwrap it; ShortestAngleDeltaUVE does exactly that.
[[nodiscard]] std::optional<float> ProjectRayOntoRingAngleUVE(const Vec3& rayOrigin,
                                                               const Vec3& rayDirection,
                                                               const Vec3& pivot, const Vec3& axis);

/// `to - from` wrapped into (-pi, pi]. Turning past half a circle in one frame is not something a
/// pointer does, so the short way round is always the one the user meant.
[[nodiscard]] float ShortestAngleDeltaUVE(float from, float to) noexcept;

} // namespace univex::gizmo
