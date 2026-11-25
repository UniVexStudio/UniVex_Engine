// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "quaternion.h"
#include "vector3.h"

namespace UVE::Scene {

/// Whether an entity's rendered pose is smoothed between fixed physics steps.
enum class PoseSmoothing : std::uint8_t {
    /// Take the parent's answer, or Blended at the top of the hierarchy. The default, so a whole rig
    /// can be switched with one toggle instead of one per bone.
    Inherit = 0,
    /// Blend between the last two simulated poses when drawing.
    Blended,
    /// Draw the simulated pose exactly. The right answer for anything whose exact position is
    /// the point - a physics debug view, a teleport, a placement gizmo.
    Exact,
};

/// The two most recent simulated world poses of an entity, kept so the renderer can draw
/// somewhere between them.
///
/// WHY THIS EXISTS. Physics runs on a fixed timestep - 60 Hz here - while the display runs at
/// whatever the monitor does. On a 144 Hz screen that was measured on this engine's own timer
/// maths:
///
///   58% of frames received no new pose at all
///   per-frame movement jumped between 0.0 cm and 16.7 cm at 10 m/s
///
/// The object is not moving unevenly; it is being SHOWN unevenly. Blending the last two poses by
/// the fraction of a step already elapsed makes every frame advance by the same 6.94 cm, and drops
/// the frozen-frame count to zero.
///
/// WHY IT IS A SEPARATE COMPONENT RATHER THAN EXTRA FIELDS ON WorldTransformComponent. Ten
/// systems read that component - physics, collision, constraints, audio, the camera. They must all
/// keep seeing the exact simulated pose: a physics step that integrated from a blended position
/// would simulate a world nobody authored, and the error would compound every step. The
/// interpolated pose is a RENDERING concern, so it lives beside the authoritative one and is
/// consumed only when drawing.
///
/// The component is optional, and an entity without one is drawn at its simulated pose. Only
/// things that actually move on the fixed step need it.
struct PhysicsInterpolationComponent final {
    /// The authored switch.
    PoseSmoothing mode = PoseSmoothing::Inherit;

    /// Resolved from `mode` and the entity's ancestors, the same way visibility is. Written by
    /// SceneGraph::Update, read when drawing.
    bool interpolatedInHierarchy = true;

    /// The pose at the END of the step before last, and at the end of the last step. The renderer
    /// draws between them.
    ///
    /// Both are WORLD poses, not local: interpolating local values and then composing would blend
    /// a child against an un-blended parent, which bends rigs apart at exactly the moments the
    /// feature is supposed to smooth.
    Math::Vector3 previousPosition{};
    Math::Quaternion previousRotation{};
    Math::Vector3 previousScale{1.0F, 1.0F, 1.0F};

    Math::Vector3 currentPosition{};
    Math::Quaternion currentRotation{};
    Math::Vector3 currentScale{1.0F, 1.0F, 1.0F};

    /// False until seeded, or after an explicit reset/disable. The first valid update seeds both
    /// poses to the same authoritative value; later changed poses produce a useful pair. Never
    /// blend from an uninitialized origin or across a teleport/reparent/disabled interval.
    bool hasPreviousPose = false;
};

/// Validates the authored mode. Runtime poses are not serialized and are checked when produced
/// or sampled, so an invalid transient cache cannot silently publish a non-finite drawn pose.
[[nodiscard]] bool IsPhysicsInterpolationComponentValid(
    const PhysicsInterpolationComponent& component) noexcept;

/// The pose to DRAW this frame: the two recorded poses blended by `alpha`, the fraction of a fixed
/// step already elapsed.
///
/// Returns false - leaving the outputs untouched - when this entity should be drawn at its
/// simulated pose instead: interpolation resolved off, only one pose recorded so far, or an alpha
/// that is not a usable fraction. A caller that ignores the result and uses the world transform
/// gets correct-but-unsmoothed output, which is the safe direction for a failure in a purely
/// visual feature.
///
/// `alpha` is clamped rather than rejected at the boundaries. A timer that overshoots slightly
/// after a long frame should keep drawing, not stutter - and 1.0 simply means "the current pose",
/// which is exactly right.
[[nodiscard]] bool TryGetInterpolatedPose(const PhysicsInterpolationComponent& component, float alpha,
                                             Math::Vector3& outPosition, Math::Quaternion& outRotation,
                                             Math::Vector3& outScale) noexcept;

} // namespace UVE::Scene
