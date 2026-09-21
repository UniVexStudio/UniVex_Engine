// univex/camera/ViewportMetrics.h
// -----------------------------------------------------------------------
// How much world space one screen pixel covers, at a given point.
//
// This used to be two functions that disagreed. GizmoRenderer::ScaleForPixelRadius had an
// orthographic branch; WorldPerPixelAtPivot did not, and both derived their answer from
// camera.Distance() - the eye-to-ORBIT-TARGET distance - even though the transform gizmo is drawn
// at the selected node's own pivot, which is only at the orbit target immediately after a focus.
// Select a node and then orbit, and the widget was sized for a depth it no longer sits at: too
// large past the pivot, too small in front of it. The same number converts pixel widths into
// gizmo units, so the picking regions drifted by exactly the same factor and what was grabbable
// stopped matching what was drawn.
//
// One function, taking the point it is actually being asked about, removes both failures at once.
// It lives in the engine-agnostic core rather than beside either renderer because it is pure
// camera maths, and because both renderers - and the tests - need the identical answer.
// -----------------------------------------------------------------------
#pragma once

#include "univex/camera/OrbitCamera.h"
#include "univex/math/Vec.h"

namespace univex::camera {

using univex::math::Vec3;

/// World units spanned by one pixel at `worldPoint`'s depth, for a viewport `framebufferHeight`
/// pixels tall. Returns 0 for a degenerate viewport.
///
/// Under perspective this is the vertical world extent of the frustum at that depth divided by
/// the pixel height; the depth is the point's distance along the view axis (not its straight-line
/// distance from the eye, which would make the metric swell toward the corners of the screen),
/// floored at the near plane so a point level with or behind the eye cannot produce a zero or
/// negative scale. Under orthographic the view volume does not converge, so the point is
/// irrelevant and the answer is constant.
[[nodiscard]] float WorldPerPixelAtPointUVE(const OrbitCamera& camera, int framebufferHeight,
                                            const Vec3& worldPoint);

/// The same metric at the camera's own orbit target - what a HUD readout about "the middle of the
/// screen" means, as opposed to what a gizmo at some other pivot means.
[[nodiscard]] float WorldPerPixelAtOrbitTargetUVE(const OrbitCamera& camera, int framebufferHeight);

} // namespace univex::camera
