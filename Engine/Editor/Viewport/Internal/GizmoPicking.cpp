#include "univex/gizmo/GizmoPicking.h"

#include <array>
#include <cmath>

namespace univex::gizmo {

namespace {

using univex::math::Cross;
using univex::math::Dot;
using univex::math::Length;
using univex::math::Normalize;

// How much bigger than the drawn line a handle's grab region is, in pixels either side. A handle
// you can only hit by landing on its exact centre line is correct and unusable; this is the usual
// "forgiving but not sloppy" allowance, and it is in pixels so it stays constant on screen.
constexpr float kGrabPaddingPixelsUVE = 5.0f;

// Minimum grab radius in gizmo units, so a handle stays hittable if a caller passes a degenerate
// unitsPerPixel (a zero-height viewport, a camera mid-initialisation).
constexpr float kMinimumGrabRadiusUVE = 0.02f;

struct RayUVE {
    Vec3 origin;
    Vec3 direction; // normalised
};

[[nodiscard]] float GrabRadiusUVE(float lineWidthPixels, float unitsPerPixel) {
    const float radius = (lineWidthPixels * 0.5f + kGrabPaddingPixelsUVE) * unitsPerPixel;
    return radius > kMinimumGrabRadiusUVE ? radius : kMinimumGrabRadiusUVE;
}

/// Closest approach between a ray and a finite segment, as (distance between them, distance along
/// the ray). Used for the axis shafts, which are segments from `start` to `end` along one axis.
struct ClosestApproachUVE {
    float separation = 0.f;
    float rayDistance = 0.f;
};

[[nodiscard]] ClosestApproachUVE ClosestRayToSegmentUVE(const RayUVE& ray, const Vec3& start,
                                                         const Vec3& end) {
    const Vec3 segment = end - start;
    const float segmentLengthSquared = Dot(segment, segment);
    const Vec3 toStart = start - ray.origin;

    if (segmentLengthSquared <= 1e-12f) {
        const float along = Dot(-toStart, ray.direction);
        const float clampedAlong = along > 0.f ? along : 0.f;
        const Vec3 pointOnRay = ray.origin + ray.direction * clampedAlong;
        return ClosestApproachUVE{Length(pointOnRay - start), clampedAlong};
    }

    // Standard two-line closest approach. With w = origin - start, u = ray.direction (unit) and
    // v = segment, the segment parameter is (dot(v,w) - dot(u,v)*dot(u,w)) / (dot(v,v) -
    // dot(u,v)^2). It is then clamped to the segment and the ray re-solved against that fixed
    // point - clamping alone would report the separation measured at the unclamped point and so
    // overshoot near the ends.
    const Vec3 w = -toStart; // origin - start
    const float b = Dot(ray.direction, segment);
    const float d = Dot(ray.direction, w);
    const float e = Dot(segment, w);
    const float denominator = segmentLengthSquared - b * b;

    float segmentParameter = 0.f;
    if (std::fabs(denominator) > 1e-12f) {
        segmentParameter = (e - b * d) / denominator;
    } else {
        // Ray parallel to the segment: every point is equidistant, so project the ray origin.
        segmentParameter = e / segmentLengthSquared;
    }
    segmentParameter = segmentParameter < 0.f ? 0.f : (segmentParameter > 1.f ? 1.f : segmentParameter);

    const Vec3 pointOnSegment = start + segment * segmentParameter;
    float along = Dot(pointOnSegment - ray.origin, ray.direction);
    if (along < 0.f) {
        along = 0.f;
    }
    const Vec3 pointOnRay = ray.origin + ray.direction * along;
    return ClosestApproachUVE{Length(pointOnRay - pointOnSegment), along};
}

/// Ray against the plane through `center` with normal `normal`. Returns the ray distance, or
/// nullopt when the ray runs parallel to the plane or would hit it behind the eye.
[[nodiscard]] std::optional<float> RayPlaneDistanceUVE(const RayUVE& ray, const Vec3& center,
                                                        const Vec3& normal) {
    const float denominator = Dot(ray.direction, normal);
    if (std::fabs(denominator) < 1e-6f) {
        return std::nullopt;
    }
    const float distance = Dot(center - ray.origin, normal) / denominator;
    if (distance <= 0.f) {
        return std::nullopt;
    }
    return distance;
}

/// Ray against a ring: the circle of `radius` about `center` lying in the plane whose normal is
/// `axis`. A hit means the ray crosses that plane within `tolerance` of the circle itself, which
/// is what makes the thin band of a rotation ring grabbable rather than its whole disc.
[[nodiscard]] std::optional<float> RayRingDistanceUVE(const RayUVE& ray, const Vec3& center,
                                                       const Vec3& axis, float radius,
                                                       float tolerance) {
    const std::optional<float> planeDistance = RayPlaneDistanceUVE(ray, center, axis);
    if (!planeDistance.has_value()) {
        return std::nullopt;
    }
    const Vec3 hit = ray.origin + ray.direction * *planeDistance;
    const float radialDistance = Length(hit - center);
    if (std::fabs(radialDistance - radius) > tolerance) {
        return std::nullopt;
    }
    return planeDistance;
}

/// Ray against a sphere of `radius` about `center`; the near intersection, if it is in front.
[[nodiscard]] std::optional<float> RaySphereDistanceUVE(const RayUVE& ray, const Vec3& center,
                                                         float radius) {
    const Vec3 toCenter = center - ray.origin;
    const float along = Dot(toCenter, ray.direction);
    const float centerDistanceSquared = Dot(toCenter, toCenter) - along * along;
    const float radiusSquared = radius * radius;
    if (centerDistanceSquared > radiusSquared) {
        return std::nullopt;
    }
    const float halfChord = std::sqrt(radiusSquared - centerDistanceSquared);
    const float near = along - halfChord;
    if (near > 0.f) {
        return near;
    }
    const float far = along + halfChord;
    return far > 0.f ? std::optional<float>{far} : std::nullopt;
}

/// Ray against the square plane handle spanning `offset..offset+size` along two axes.
[[nodiscard]] std::optional<float> RayPlaneHandleDistanceUVE(const RayUVE& ray, const Vec3& pivot,
                                                              float scale, const Vec3& axisA,
                                                              const Vec3& axisB, float offset,
                                                              float size) {
    const Vec3 normal = Normalize(Cross(axisA, axisB));
    const std::optional<float> distance = RayPlaneDistanceUVE(ray, pivot, normal);
    if (!distance.has_value()) {
        return std::nullopt;
    }
    const Vec3 local = (ray.origin + ray.direction * *distance) - pivot;
    const float a = Dot(local, axisA) / scale;
    const float b = Dot(local, axisB) / scale;
    const bool inside = a >= offset && a <= offset + size && b >= offset && b <= offset + size;
    return inside ? distance : std::nullopt;
}

/// The scale gizmo's plane handle, which is a TRIANGLE, not a square.
///
/// AddScalePlaneHandles draws corners at (offset, 0), (0, offset) and (pull, pull) in the plane's
/// own two-axis coordinates. This used to be picked with the move gizmo's square test instead -
/// `a` and `b` both within [scalePlaneOffset, scalePlaneOffset + planeHandleSize] - which is not a
/// near-enough approximation, it is a different region entirely: the square needs a + b >= 1.2
/// while the whole triangle lives below a + b = 0.6. The drawn handle was therefore close to
/// unclickable, and a patch of empty space outside it answered to the click instead.
[[nodiscard]] std::optional<float> RayScalePlaneHandleDistanceUVE(const RayUVE& ray,
                                                                  const Vec3& pivot, float scale,
                                                                  const Vec3& axisA,
                                                                  const Vec3& axisB, float offset,
                                                                  float pull) {
    const Vec3 normal = Normalize(Cross(axisA, axisB));
    const std::optional<float> distance = RayPlaneDistanceUVE(ray, pivot, normal);
    if (!distance.has_value()) {
        return std::nullopt;
    }
    const Vec3 local = (ray.origin + ray.direction * *distance) - pivot;
    const float a = Dot(local, axisA) / scale;
    const float b = Dot(local, axisB) / scale;

    // Point-in-triangle by the sign of the three edge cross products: inside means the point falls
    // on the same side of every edge, and a zero lands exactly on one.
    const auto edgeSign = [](float px, float py, float x0, float y0, float x1, float y1) {
        return (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
    };
    const float s0 = edgeSign(a, b, offset, 0.f, 0.f, offset);
    const float s1 = edgeSign(a, b, 0.f, offset, pull, pull);
    const float s2 = edgeSign(a, b, pull, pull, offset, 0.f);
    const bool allNonNegative = s0 >= 0.f && s1 >= 0.f && s2 >= 0.f;
    const bool allNonPositive = s0 <= 0.f && s1 <= 0.f && s2 <= 0.f;
    return (allNonNegative || allNonPositive) ? distance : std::nullopt;
}

struct CandidateUVE {
    GizmoHandleUVE handle = GizmoHandleUVE::None;
    float rayDistance = 0.f;
    bool hit = false;
};

void ConsiderUVE(CandidateUVE& best, GizmoHandleUVE handle, const std::optional<float>& distance) {
    if (!distance.has_value()) {
        return;
    }
    if (!best.hit || *distance < best.rayDistance) {
        best = CandidateUVE{handle, *distance, true};
    }
}

[[nodiscard]] std::array<Vec3, 3> WorldAxesUVE() {
    return {Vec3{1.f, 0.f, 0.f}, Vec3{0.f, 1.f, 0.f}, Vec3{0.f, 0.f, 1.f}};
}

[[nodiscard]] GizmoHandleUVE AxisHandleForIndexUVE(std::size_t index) {
    switch (index) {
        case 0: return GizmoHandleUVE::AxisX;
        case 1: return GizmoHandleUVE::AxisY;
        default: return GizmoHandleUVE::AxisZ;
    }
}

[[nodiscard]] GizmoHandleUVE PlaneHandleForPairUVE(std::size_t first, std::size_t second) {
    if ((first == 0 && second == 1) || (first == 1 && second == 0)) return GizmoHandleUVE::PlaneXY;
    if ((first == 1 && second == 2) || (first == 2 && second == 1)) return GizmoHandleUVE::PlaneYZ;
    return GizmoHandleUVE::PlaneZX;
}

} // namespace

bool IsAxisHandleUVE(const GizmoHandleUVE handle) noexcept {
    return handle == GizmoHandleUVE::AxisX || handle == GizmoHandleUVE::AxisY ||
           handle == GizmoHandleUVE::AxisZ;
}

std::optional<Vec3> AxisDirectionForHandleUVE(const GizmoHandleUVE handle) noexcept {
    switch (handle) {
        case GizmoHandleUVE::AxisX: return Vec3{1.f, 0.f, 0.f};
        case GizmoHandleUVE::AxisY: return Vec3{0.f, 1.f, 0.f};
        case GizmoHandleUVE::AxisZ: return Vec3{0.f, 0.f, 1.f};
        default: return std::nullopt;
    }
}

GizmoPickResultUVE PickGizmoHandleUVE(const GizmoMode mode, const GizmoStyle& style,
                                       const Vec3& rayOrigin, const Vec3& rayDirection,
                                       const Vec3& pivot, const float scale,
                                       const Vec3& viewDirection, const float unitsPerPixel) {
    static_cast<void>(viewDirection); // hit regions are whole handles; facing only affects drawing
    if (mode == GizmoMode::Select || scale <= 0.f) {
        return {};
    }
    const float directionLength = Length(rayDirection);
    if (!(directionLength > 0.f)) {
        return {};
    }
    const RayUVE ray{rayOrigin, rayDirection / directionLength};
    const std::array<Vec3, 3> axes = WorldAxesUVE();
    CandidateUVE best;

    const auto worldPoint = [&](const Vec3& gizmoSpace) { return pivot + gizmoSpace * scale; };

    // ---- axis shafts -----------------------------------------------------
    if (mode == GizmoMode::Move || mode == GizmoMode::Scale || mode == GizmoMode::Universal) {
        const float shaftStart = mode == GizmoMode::Move      ? style.moveShaftStart
                                 : mode == GizmoMode::Scale   ? style.scaleShaftStart
                                                              : style.universalShaftStart;
        // Scale's handle runs to its box, Universal's past the arrow to its own box, Move's to the
        // arrow tip - each shaft is grabbable for the whole length that is actually drawn.
        const float shaftEnd = mode == GizmoMode::Move ? style.moveShaftEnd + style.moveConeLength
                               : mode == GizmoMode::Scale
                                   ? style.scaleShaftEnd + style.scaleBoxSize
                                   : style.universalScaleBoxOffset + style.universalScaleBoxSize;
        const float lineWidth = mode == GizmoMode::Universal ? style.universalLineWidthPx
                                                             : style.axisLineWidthPx;
        const float grabRadius = GrabRadiusUVE(lineWidth, unitsPerPixel) * scale;

        for (std::size_t index = 0; index < axes.size(); ++index) {
            const ClosestApproachUVE approach =
                ClosestRayToSegmentUVE(ray, worldPoint(axes[index] * shaftStart),
                                       worldPoint(axes[index] * shaftEnd));
            if (approach.separation <= grabRadius) {
                ConsiderUVE(best, AxisHandleForIndexUVE(index), approach.rayDistance);
            }
        }
    }

    // ---- plane handles ---------------------------------------------------
    // Move's chip is a square and Scale's is a triangle, so each is tested against its own drawn
    // shape rather than both sharing the square test.
    if (mode == GizmoMode::Move || mode == GizmoMode::Scale) {
        const std::array<std::pair<std::size_t, std::size_t>, 3> pairs = {{{0, 1}, {1, 2}, {2, 0}}};
        for (const auto& [first, second] : pairs) {
            const std::optional<float> distance =
                mode == GizmoMode::Move
                    ? RayPlaneHandleDistanceUVE(ray, pivot, scale, axes[first], axes[second],
                                                style.planeHandleOffset, style.planeHandleSize)
                    : RayScalePlaneHandleDistanceUVE(ray, pivot, scale, axes[first], axes[second],
                                                     style.scalePlaneOffset, style.scalePlanePull);
            ConsiderUVE(best, PlaneHandleForPairUVE(first, second), distance);
        }
    }

    // ---- rotation rings --------------------------------------------------
    if (mode == GizmoMode::Rotate || mode == GizmoMode::Universal) {
        const float ringRadius = mode == GizmoMode::Rotate ? style.ringRadius : style.universalRingRadius;
        const float ringWidth = mode == GizmoMode::Rotate ? style.ringLineWidthPx : style.universalRingWidthPx;
        const float tolerance = GrabRadiusUVE(ringWidth, unitsPerPixel) * scale;
        for (std::size_t index = 0; index < axes.size(); ++index) {
            ConsiderUVE(best, AxisHandleForIndexUVE(index),
                        RayRingDistanceUVE(ray, pivot, axes[index], ringRadius * scale, tolerance));
        }
        if (mode == GizmoMode::Rotate) {
            const float freeTolerance = GrabRadiusUVE(style.freeRingWidthPx, unitsPerPixel) * scale;
            ConsiderUVE(best, GizmoHandleUVE::ScreenRing,
                        RayRingDistanceUVE(ray, pivot, Normalize(viewDirection),
                                           style.freeRingRadius * scale, freeTolerance));
        }
    }

    // ---- centre handle ---------------------------------------------------
    // Considered last and allowed to win outright: it is the smallest thing on screen and sits
    // exactly where all three axes converge, so nearest-along-the-ray would almost never choose it.
    //
    // Derived from the dot's own PIXEL radius, not a world size. The dot is drawn pixel-sized, so
    // a world-sized hit disc would agree with it at one zoom level and drift at every other. Same
    // padding as every other handle, so the dot is no harder to hit than a 1.5 px line - and no
    // easier: the old world-unit radius covered roughly 20 px and quietly swallowed clicks aimed
    // at the plane chips and rings beside it, which is why this one wins outright and yet had to
    // be this generous.
    const float centerRadiusUnits = (style.pivotDotRadiusPx + kGrabPaddingPixelsUVE) * unitsPerPixel;
    const float centerRadius =
        (centerRadiusUnits > kMinimumGrabRadiusUVE ? centerRadiusUnits : kMinimumGrabRadiusUVE) * scale;
    const std::optional<float> centerDistance = RaySphereDistanceUVE(ray, pivot, centerRadius);
    if (centerDistance.has_value()) {
        return GizmoPickResultUVE{GizmoHandleUVE::Uniform, *centerDistance};
    }

    if (!best.hit) {
        return {};
    }
    return GizmoPickResultUVE{best.handle, best.rayDistance};
}

} // namespace univex::gizmo
