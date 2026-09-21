#include "univex/gizmo/GizmoDrag.h"

#include <cmath>
#include <numbers>

namespace univex::gizmo {

namespace {

using univex::math::Cross;
using univex::math::Dot;
using univex::math::Length;
using univex::math::Normalize;

constexpr float kPiUVE = std::numbers::pi_v<float>;

// How close to parallel is too close. At |dot| = 0.999 the projection's sensitivity is already
// ~22x, which is the point where a drag stops feeling controlled and starts feeling like a throw.
constexpr float kParallelLimitUVE = 0.999f;

// The same idea for a plane, stated as its own number rather than derived from the one above:
// a ray within ~2.6 degrees of the plane it is being projected onto lands its intersection
// arbitrarily far from the pivot, so the drag is refused rather than allowed to jump.
constexpr float kMinimumPlaneFacingUVE = 0.045f;

/// Two unit vectors perpendicular to `axis` and to each other. Matches GizmoGeometry's own
/// PerpBasis exactly, so a ring's measured angle is expressed in the basis it was drawn in.
void PerpBasisUVE(const Vec3& axis, Vec3& outU, Vec3& outV) {
    const Vec3 a = Normalize(axis);
    const Vec3 helper = (std::fabs(a.y) < 0.98f) ? Vec3{0.f, 1.f, 0.f} : Vec3{1.f, 0.f, 0.f};
    outU = Normalize(Cross(helper, a));
    outV = Normalize(Cross(a, outU));
}

[[nodiscard]] bool TryNormalizeRayUVE(const Vec3& rayDirection, Vec3& outDirection) {
    const float length = Length(rayDirection);
    if (!(length > 0.f) || !std::isfinite(length)) {
        return false;
    }
    outDirection = rayDirection / length;
    return true;
}

} // namespace

std::optional<float> ProjectRayOntoAxisUVE(const Vec3& rayOrigin, const Vec3& rayDirection,
                                           const Vec3& pivot, const Vec3& axis) {
    Vec3 direction{};
    if (!TryNormalizeRayUVE(rayDirection, direction)) {
        return std::nullopt;
    }
    const Vec3 axisDirection = Normalize(axis);

    // Closest approach between the ray and the infinite axis line. With u the ray direction, v the
    // axis (both unit) and w the ray origin relative to the pivot, the axis parameter is
    // (dot(v,w) - dot(u,v)*dot(u,w)) / (1 - dot(u,v)^2). The denominator vanishes exactly when the
    // two are parallel, which is the case this guards.
    const float b = Dot(direction, axisDirection);
    if (std::fabs(b) > kParallelLimitUVE) {
        return std::nullopt; // sighting down the axis: no usable projection
    }

    const Vec3 w = rayOrigin - pivot;
    const float d = Dot(direction, w);
    const float e = Dot(axisDirection, w);
    const float along = (e - b * d) / (1.f - b * b);
    return std::isfinite(along) ? std::optional<float>{along} : std::nullopt;
}

std::optional<Vec3> ProjectRayOntoPlaneUVE(const Vec3& rayOrigin, const Vec3& rayDirection,
                                           const Vec3& pivot, const Vec3& planeNormal) {
    Vec3 direction{};
    if (!TryNormalizeRayUVE(rayDirection, direction)) {
        return std::nullopt;
    }
    const Vec3 normal = Normalize(planeNormal);

    const float denominator = Dot(direction, normal);
    if (std::fabs(denominator) < kMinimumPlaneFacingUVE) {
        return std::nullopt;
    }

    const float distance = Dot(pivot - rayOrigin, normal) / denominator;
    if (!std::isfinite(distance)) {
        return std::nullopt;
    }
    return rayOrigin + direction * distance;
}

std::optional<float> ProjectRayOntoRingAngleUVE(const Vec3& rayOrigin, const Vec3& rayDirection,
                                                const Vec3& pivot, const Vec3& axis) {
    const std::optional<Vec3> hit = ProjectRayOntoPlaneUVE(rayOrigin, rayDirection, pivot, axis);
    if (!hit.has_value()) {
        return std::nullopt;
    }

    Vec3 u{}, v{};
    PerpBasisUVE(axis, u, v);
    const Vec3 radial = *hit - pivot;
    const float x = Dot(radial, u);
    const float y = Dot(radial, v);
    if (!(x * x + y * y > 0.f)) {
        return std::nullopt; // exactly on the pivot: no direction to read an angle from
    }
    return std::atan2(y, x);
}

float ShortestAngleDeltaUVE(const float from, const float to) noexcept {
    float wrapped = std::fmod(to - from + kPiUVE, 2.f * kPiUVE);
    if (wrapped < 0.f) {
        wrapped += 2.f * kPiUVE;
    }
    return wrapped - kPiUVE;
}

} // namespace univex::gizmo
