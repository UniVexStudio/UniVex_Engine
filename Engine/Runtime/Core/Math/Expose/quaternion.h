// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>

#include <string>

#include "vector3.h"

namespace UVE::Math {

/// A unit quaternion used for 3D rotation. Default-constructs to the identity rotation
/// (x=y=z=0, w=1) — deliberately minimal, alongside Vector3: no SLERP, no Euler-angle
/// conversion, no matrix conversion. Those are real design problems for whichever future
/// increment (Rendering, Physics, Animation) first needs them.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct Quaternion {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

[[nodiscard]] constexpr bool operator==(const Quaternion& lhs, const Quaternion& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

[[nodiscard]] constexpr bool operator!=(const Quaternion& lhs, const Quaternion& rhs) noexcept {
    return !(lhs == rhs);
}

/// Hamilton product `lhs * rhs`: the rotation "rhs applied first, then lhs" — i.e.
/// `RotateVector(Multiply(lhs, rhs), v) == RotateVector(lhs, RotateVector(rhs, v))`.
[[nodiscard]] Quaternion Multiply(const Quaternion& lhs, const Quaternion& rhs) noexcept;

/// Returns whether every quaternion component is finite.
[[nodiscard]] bool IsFinite(const Quaternion& value) noexcept;

/// Returns the squared quaternion magnitude. Callers requiring a unit rotation should use
/// TryNormalize() rather than assuming a non-zero result.
[[nodiscard]] float LengthSquared(const Quaternion& value) noexcept;

/// Normalizes `value` into `outNormalized`. Returns false and does not modify `outNormalized`
/// when the input is non-finite or has an effectively zero magnitude.
[[nodiscard]] bool TryNormalize(const Quaternion& value, Quaternion& outNormalized) noexcept;

/// Computes the inverse into `outInverse`. Returns false and does not modify `outInverse` for a
/// non-finite or effectively zero-magnitude input.
[[nodiscard]] bool TryInverse(const Quaternion& value, Quaternion& outInverse) noexcept;

/// Builds a normalized axis-angle rotation around `axis` by `radians`. Returns false without
/// modifying `outRotation` when the axis or angle is non-finite or the axis has zero magnitude.
[[nodiscard]] bool TryMakeAxisAngle(const Vector3& axis, float radians,
                                        Quaternion& outRotation) noexcept;

/// Rotates `vector` by `rotation`.
[[nodiscard]] Vector3 RotateVector(const Quaternion& rotation, const Vector3& vector) noexcept;

/// The conjugate (negated xyz, kept w): the inverse of a unit rotation, without the
/// fallibility of `TryInverse`. Caller ensures the input is unit-length.
[[nodiscard]] constexpr Quaternion Conjugate(const Quaternion& rotation) noexcept {
    return Quaternion{-rotation.x, -rotation.y, -rotation.z, rotation.w};
}

/// Builds a normalized XYZ Euler rotation from radians. Returns false for non-finite input.
[[nodiscard]] bool TryMakeEuler(const Vector3& radians, Quaternion& outRotation) noexcept;

/// The inverse of TryMakeEuler(): extracts the XYZ Euler angles (radians) that would rebuild
/// `rotation` via TryMakeEuler(). Returns false for a non-finite/non-normalizable input. Like
/// every Euler-angle extraction, this is not unique at the gimbal-lock poles (pitch at +/-90
/// degrees) - the same accepted limitation every engine's rotation Inspector field has.
[[nodiscard]] bool TryToEuler(const Quaternion& rotation, Vector3& outRadians) noexcept;

/// The order Euler angles are applied in, named by application sequence: `XYZ` means rotate about
/// X first, then Y, then Z, each about the PARENT frame's axes.
///
/// WHY SIX. A single order cannot avoid gimbal lock - every Euler convention has a pose where one
/// degree of freedom collapses - but the pose differs per order, so an author who hits it can
/// switch to an order whose singularity is somewhere their rig never goes. A turret that pitches
/// to vertical is unusable in XYZ and fine in ZXY. Offering one order and calling the limitation
/// inherent is what forces people into quaternion fields they cannot read.
///
/// `XYZ` is the default and is bit-for-bit what TryMakeEuler()/TryToEuler() have always
/// produced - verified by test, so no existing scene, asset or authored rotation changes meaning.
enum class EulerOrder : std::uint8_t {
    XYZ = 0,
    YXZ,
    ZYX,
    XZY,
    YZX,
    ZXY,
};

/// Builds a normalized rotation from Euler angles in the given order. Returns false for
/// non-finite input or a rotation that cannot be normalized.
///
/// TryMakeEuler(v, out) and TryMakeEulerOrdered(v, EulerOrder::XYZ, out) are the same
/// function; the two-argument form is kept because most callers neither know nor care about
/// order, and making all of them pass an enum would be noise.
[[nodiscard]] bool TryMakeEulerOrdered(const Vector3& radians, EulerOrder order,
                                          Quaternion& outRotation) noexcept;

/// Extracts the Euler angles, in the given order, that rebuild `rotation` via
/// TryMakeEulerOrdered(). Returns false for non-finite or non-normalizable input.
///
/// At a gimbal-lock pose the decomposition is not unique: infinitely many angle triples describe
/// the same rotation, and this returns one of them with the collapsed term set to zero. The
/// ROTATION always round-trips exactly - it is only the angles that are ambiguous - which is why
/// authored Euler values are stored rather than re-extracted every frame (see
/// TransformComponent).
[[nodiscard]] bool TryToEulerOrdered(const Quaternion& rotation, EulerOrder order,
                                        Vector3& outRadians) noexcept;

/// Builds a rotation that points local +Z along `direction` with the supplied up reference.
[[nodiscard]] bool TryMakeLookAt(const Vector3& direction, const Vector3& up,
                                    Quaternion& outRotation) noexcept;

/// Spherical interpolation between two rotations with finite alpha.
[[nodiscard]] bool TrySlerp(const Quaternion& lhs, const Quaternion& rhs, float alpha,
                               Quaternion& outRotation) noexcept;

/// Decomposes a normalized rotation into a unit axis and angle in radians.
[[nodiscard]] bool TryToAxisAngle(const Quaternion& rotation, Vector3& outAxis,
                                     float& outRadians) noexcept;

/// Formats `rotation` as `"(x, y, z, w)"`, for logging/debugging.
[[nodiscard]] std::string ToString(const Quaternion& rotation);

} // namespace UVE::Math
