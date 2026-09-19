// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/physics_interpolation_component_uve.h"

#include <cmath>

namespace UVE::Scene {

bool IsPhysicsInterpolationComponentValidUVE(const PhysicsInterpolationComponentUVE&) noexcept {
    // Plain values with no representable invalid state. The poses are copies of world transforms
    // that were already validated when they were produced, so re-checking them here would report
    // a fault against the wrong component.
    return true;
}

bool TryGetInterpolatedPoseUVE(const PhysicsInterpolationComponentUVE& component, const float alpha,
                               Math::Vector3UVE& outPosition, Math::QuaternionUVE& outRotation,
                               Math::Vector3UVE& outScale) noexcept {
    if (!component.interpolatedInHierarchy || !component.hasPreviousPose || !std::isfinite(alpha)) {
        return false;
    }
    // Clamped, not rejected: a timer that overshoots after a long frame should keep drawing
    // smoothly rather than stutter, and 1.0 means "the current pose", which is correct.
    const float blend = alpha < 0.0F ? 0.0F : (alpha > 1.0F ? 1.0F : alpha);

    Math::QuaternionUVE rotation{};
    if (!Math::TrySlerpUVE(component.previousRotation, component.currentRotation, blend, rotation)) {
        // Two poses that cannot be interpolated between - a non-normalizable rotation recorded
        // from a degenerate transform. Reporting failure sends the caller to the simulated pose
        // rather than drawing whatever a half-finished slerp produced.
        return false;
    }

    outPosition = component.previousPosition + (component.currentPosition - component.previousPosition) * blend;
    outScale = component.previousScale + (component.currentScale - component.previousScale) * blend;
    outRotation = rotation;
    return true;
}

} // namespace UVE::Scene
