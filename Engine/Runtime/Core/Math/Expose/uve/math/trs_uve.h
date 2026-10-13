// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Math {

/// A composable transform value: translation, rotation, and (possibly non-uniform) scale as one
/// object instead of three loose fields. The scene graph, the gizmo drag path, reparenting, and
/// bone attachments all used to combine transforms as three separate members plus hand-rolled
/// quaternion/vector arithmetic; this type makes that composition one named operation.
/// A point `p` is transformed as `translation + rotation * (scale * p)` (scale first, then
/// rotation, then translation), matching `Matrix4x4UVE::ComposeTrsUVE` exactly.
/// Deliberately minimal, matching this module's precedent: compose, forward/inverse point,
/// direction, and rotation-delta transforms, equality, and a debug formatter. There is
/// intentionally NO materialized
/// inverse (`TryInverseUVE` returning a `TrsUVE`): rotation and non-uniform scale do not commute,
/// so the inverse of a general TRS is not itself a TRS — the in-tree precedent is the reparent
/// path refusing non-uniformly-scaled rotated parents, since no local TRS reproduces that world
/// TRS. The `TryInverseTransform*UVE()` functions below are the exact inverse APPLICATION for
/// every scale, which is what every caller actually needs.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct TrsUVE {
    Vector3UVE translation{};
    QuaternionUVE rotation{};
    Vector3UVE scale{1.0F, 1.0F, 1.0F};

    /// Returns the identity transform. Equivalent to the default constructor; provided for
    /// readability at call sites.
    [[nodiscard]] static constexpr TrsUVE IdentityUVE() noexcept { return TrsUVE{}; }
};

[[nodiscard]] constexpr bool operator==(const TrsUVE& lhs, const TrsUVE& rhs) noexcept {
    return lhs.translation == rhs.translation && lhs.rotation == rhs.rotation && lhs.scale == rhs.scale;
}

[[nodiscard]] constexpr bool operator!=(const TrsUVE& lhs, const TrsUVE& rhs) noexcept {
    return !(lhs == rhs);
}

/// Composes `parent` and `child` ("apply `child` first, then `parent`"), returning the single
/// transform equivalent to both. The exact arithmetic the scene graph has always used for
/// parent/child composition, now named: `scale` multiplies component-wise, `rotation`
/// multiplies as quaternions, and the child's translation is scaled, rotated into the parent
/// frame, and added to the parent's translation. Always succeeds — like transposition, and
/// unlike inversion, composition has no degenerate case (non-finite inputs produce non-finite
/// outputs; callers that must reject those validate, as the scene graph does).
/// Associativity holds when the composed scales are uniform (a uniform scale commutes past any
/// rotation): with a non-uniform scale under a rotation the component-wise scale product does
/// not commute, so a chain's grouping matters. That matches the scene graph exactly — this is
/// its rule, verbatim — and any chain that needs order-independence under non-uniform scale
/// should compose matrices instead.
[[nodiscard]] TrsUVE ComposeUVE(const TrsUVE& parent, const TrsUVE& child) noexcept;

/// Transforms `point` by `trs`: `translation + rotation * (scale * point)`.
[[nodiscard]] Vector3UVE TransformPointUVE(const TrsUVE& trs, const Vector3UVE& point) noexcept;

/// Transforms `direction` by `trs`, IGNORING translation: `rotation * (scale * direction)`.
/// For directions (normals-ish quantities, drag deltas), which live in no position.
[[nodiscard]] Vector3UVE TransformDirectionUVE(const TrsUVE& trs, const Vector3UVE& direction) noexcept;

/// The exact inverse of TransformPointUVE(): `(rotation^-1 * (point - translation)) / scale`.
/// Returns false (leaving `outPoint` untouched) if any input is non-finite, the rotation is
/// degenerate (not invertible as a quaternion), or the result is non-finite — so a zero scale
/// axis fails rather than producing an infinity, and callers simply propagate the failure.
/// What "too small to divide by" means stays the CALLER's policy, because it already differs per
/// path in this tree (the editor's drag path refuses scales at or under 1e-5, bone attachments
/// under 1e-6): callers pre-guard with their own cutoff, and this function revalidates cheaply.
/// Unrotate-then-divide order, matching the editor's long-standing world-to-local arithmetic.
[[nodiscard]] bool TryInverseTransformPointUVE(const TrsUVE& trs, const Vector3UVE& point,
                                               Vector3UVE& outPoint) noexcept;

/// The exact inverse of TransformDirectionUVE(): `(rotation^-1 * direction) / scale`.
/// Same contract as TryInverseTransformPointUVE().
[[nodiscard]] bool TryInverseTransformDirectionUVE(const TrsUVE& trs, const Vector3UVE& direction,
                                                   Vector3UVE& outDirection) noexcept;

/// Conjugates a world-space rotation delta into `trs`' frame: `rotation^-1 * worldDelta *
/// rotation` — the local delta that reproduces `worldDelta` back under the parent, for the
/// viewport rotate-drag path (a drag about a world axis becomes the entity-local spin). Rotation
/// conjugation is scale-free, so unlike the point/direction inverses this deliberately ignores
/// `trs` translation and scale: they never enter the arithmetic, and non-finite values there do
/// not fail. Returns false (leaving `outLocalDelta` untouched) only when the rotation or the
/// delta is non-finite, or the rotation is degenerate (not normalizable as a quaternion).
/// Normalize-then-conjugate order, matching the editor's long-standing world-to-local arithmetic.
/// A materialized-inverse formulation (`parent^-1 * newWorld`) stays impossible — the inverse of
/// a general TRS is not a TRS — so conjugation remains the value-type spelling of this gesture.
[[nodiscard]] bool TryInverseTransformRotationUVE(const TrsUVE& trs,
                                                 const QuaternionUVE& worldDelta,
                                                 QuaternionUVE& outLocalDelta) noexcept;

/// Formats `trs` as `"Trs((tx, ty, tz), (qx, qy, qz, qw), (sx, sy, sz))"`, for logging/debugging.
[[nodiscard]] std::string ToStringUVE(const TrsUVE& trs);

} // namespace UVE::Math
