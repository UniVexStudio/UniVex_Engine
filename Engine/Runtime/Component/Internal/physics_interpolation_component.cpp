// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "physics_interpolation_component.h"

#include <cmath>

namespace UVE::Scene {

bool IsPhysicsInterpolationComponentValid(const PhysicsInterpolationComponent& component) noexcept {
    // Only the authored setting is persisted. Runtime pose validation belongs at the read/write
    // boundary and must not make serialization depend on transient cache contents.
    return component.mode == PoseSmoothing::Inherit || component.mode == PoseSmoothing::Blended ||
           component.mode == PoseSmoothing::Exact;
}

bool TryGetInterpolatedPose(const PhysicsInterpolationComponent& component, const float alpha,
                               Math::Vector3& outPosition, Math::Quaternion& outRotation,
                               Math::Vector3& outScale) noexcept {
    if (!IsPhysicsInterpolationComponentValid(component) || !component.interpolatedInHierarchy ||
        !component.hasPreviousPose || !std::isfinite(alpha) ||
        !Math::IsFinite(component.previousPosition) || !Math::IsFinite(component.currentPosition) ||
        !Math::IsFinite(component.previousScale) || !Math::IsFinite(component.currentScale)) {
        return false;
    }
    // Clamped, not rejected: a timer that overshoots after a long frame should keep drawing
    // smoothly rather than stutter, and 1.0 means "the current pose", which is correct.
    const float blend = alpha < 0.0F ? 0.0F : (alpha > 1.0F ? 1.0F : alpha);

    Math::Quaternion rotation{};
    if (!Math::TrySlerp(component.previousRotation, component.currentRotation, blend, rotation)) {
        // Two poses that cannot be interpolated between - a non-normalizable rotation recorded
        // from a degenerate transform. Reporting failure sends the caller to the simulated pose
        // rather than drawing whatever a half-finished slerp produced.
        return false;
    }

    // Finite endpoints can still overflow a float subtraction (e.g. -FLT_MAX to +FLT_MAX).
    // Double weighted sums keep the convex blend finite without changing stored scalar layouts.
    const auto lerp = [blend](const Math::Vector3& a, const Math::Vector3& b) {
        const double t = static_cast<double>(blend);
        return Math::Vector3{
            static_cast<float>((1.0 - t) * a.x + t * b.x),
            static_cast<float>((1.0 - t) * a.y + t * b.y),
            static_cast<float>((1.0 - t) * a.z + t * b.z)};
    };
    const auto position = lerp(component.previousPosition, component.currentPosition);
    const auto scale = lerp(component.previousScale, component.currentScale);
    if (!Math::IsFinite(position) || !Math::IsFinite(scale)) return false;
    outPosition = position;
    outScale = scale;
    outRotation = rotation;
    return true;
}

} // namespace UVE::Scene
