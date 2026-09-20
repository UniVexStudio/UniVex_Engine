// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>

#include <string>

#include "uve/math/vector3_uve.h"

namespace UVE::Math {

/// A unit quaternion used for 3D rotation. Default-constructs to the identity rotation
/// (x=y=z=0, w=1) — deliberately minimal, alongside Vector3UVE: no SLERP, no Euler-angle
/// conversion, no matrix conversion. Those are real design problems for whichever future
/// increment (Rendering, Physics, Animation) first needs them.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct QuaternionUVE {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

[[nodiscard]] constexpr bool operator==(const QuaternionUVE& lhs, const QuaternionUVE& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

[[nodiscard]] constexpr bool operator!=(const QuaternionUVE& lhs, const QuaternionUVE& rhs) noexcept {
    return !(lhs == rhs);
}

/// Hamilton product `lhs * rhs`: the rotation "rhs applied first, then lhs" — i.e.
/// `RotateVectorUVE(MultiplyUVE(lhs, rhs), v) == RotateVectorUVE(lhs, RotateVectorUVE(rhs, v))`.
[[nodiscard]] QuaternionUVE MultiplyUVE(const QuaternionUVE& lhs, const QuaternionUVE& rhs) noexcept;

/// Returns whether every quaternion component is finite.
[[nodiscard]] bool IsFiniteUVE(const QuaternionUVE& value) noexcept;

/// Returns the squared quaternion magnitude. Callers requiring a unit rotation should use
/// TryNormalizeUVE() rather than assuming a non-zero result.
[[nodiscard]] float LengthSquaredUVE(const QuaternionUVE& value) noexcept;

/// Normalizes `value` into `outNormalized`. Returns false and does not modify `outNormalized`
/// when the input is non-finite or has an effectively zero magnitude.
[[nodiscard]] bool TryNormalizeUVE(const QuaternionUVE& value, QuaternionUVE& outNormalized) noexcept;

/// Computes the inverse into `outInverse`. Returns false and does not modify `outInverse` for a
/// non-finite or effectively zero-magnitude input.
[[nodiscard]] bool TryInverseUVE(const QuaternionUVE& value, QuaternionUVE& outInverse) noexcept;

/// Builds a normalized axis-angle rotation around `axis` by `radians`. Returns false without
/// modifying `outRotation` when the axis or angle is non-finite or the axis has zero magnitude.
[[nodiscard]] bool TryMakeAxisAngleUVE(const Vector3UVE& axis, float radians,
                                        QuaternionUVE& outRotation) noexcept;

/// Rotates `vector` by `rotation`.
[[nodiscard]] Vector3UVE RotateVectorUVE(const QuaternionUVE& rotation, const Vector3UVE& vector) noexcept;

/// Builds a normalized XYZ Euler rotation from radians. Returns false for non-finite input.
[[nodiscard]] bool TryMakeEulerUVE(const Vector3UVE& radians, QuaternionUVE& outRotation) noexcept;

/// The inverse of TryMakeEulerUVE(): extracts the XYZ Euler angles (radians) that would rebuild
/// `rotation` via TryMakeEulerUVE(). Returns false for a non-finite/non-normalizable input. Like
/// every Euler-angle extraction, this is not unique at the gimbal-lock poles (pitch at +/-90
/// degrees) - the same accepted limitation every engine's rotation Inspector field has.
[[nodiscard]] bool TryToEulerUVE(const QuaternionUVE& rotation, Vector3UVE& outRadians) noexcept;

/// The order Euler angles are applied in, named by application sequence: `XYZ` means rotate about
/// X first, then Y, then Z, each about the PARENT frame's axes.
///
/// WHY SIX. A single order cannot avoid gimbal lock - every Euler convention has a pose where one
/// degree of freedom collapses - but the pose differs per order, so an author who hits it can
/// switch to an order whose singularity is somewhere their rig never goes. A turret that pitches
/// to vertical is unusable in XYZ and fine in ZXY. Offering one order and calling the limitation
/// inherent is what forces people into quaternion fields they cannot read.
///
/// `XYZ` is the default and is bit-for-bit what TryMakeEulerUVE()/TryToEulerUVE() have always
/// produced - verified by test, so no existing scene, asset or authored rotation changes meaning.
enum class EulerOrderUVE : std::uint8_t {
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
/// TryMakeEulerUVE(v, out) and TryMakeEulerOrderedUVE(v, EulerOrderUVE::XYZ, out) are the same
/// function; the two-argument form is kept because most callers neither know nor care about
/// order, and making all of them pass an enum would be noise.
[[nodiscard]] bool TryMakeEulerOrderedUVE(const Vector3UVE& radians, EulerOrderUVE order,
                                          QuaternionUVE& outRotation) noexcept;

/// Extracts the Euler angles, in the given order, that rebuild `rotation` via
/// TryMakeEulerOrderedUVE(). Returns false for non-finite or non-normalizable input.
///
/// At a gimbal-lock pose the decomposition is not unique: infinitely many angle triples describe
/// the same rotation, and this returns one of them with the collapsed term set to zero. The
/// ROTATION always round-trips exactly - it is only the angles that are ambiguous - which is why
/// authored Euler values are stored rather than re-extracted every frame (see
/// TransformComponentUVE).
[[nodiscard]] bool TryToEulerOrderedUVE(const QuaternionUVE& rotation, EulerOrderUVE order,
                                        Vector3UVE& outRadians) noexcept;

/// Builds a rotation that points local +Z along `direction` with the supplied up reference.
[[nodiscard]] bool TryMakeLookAtUVE(const Vector3UVE& direction, const Vector3UVE& up,
                                    QuaternionUVE& outRotation) noexcept;

/// Spherical interpolation between two rotations with finite alpha.
[[nodiscard]] bool TrySlerpUVE(const QuaternionUVE& lhs, const QuaternionUVE& rhs, float alpha,
                               QuaternionUVE& outRotation) noexcept;

/// Decomposes a normalized rotation into a unit axis and angle in radians.
[[nodiscard]] bool TryToAxisAngleUVE(const QuaternionUVE& rotation, Vector3UVE& outAxis,
                                     float& outRadians) noexcept;

/// Formats `rotation` as `"(x, y, z, w)"`, for logging/debugging.
[[nodiscard]] std::string ToStringUVE(const QuaternionUVE& rotation);

} // namespace UVE::Math
