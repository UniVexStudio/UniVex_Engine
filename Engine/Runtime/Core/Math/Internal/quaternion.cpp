

#include "quaternion.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace UVE::Math {

namespace {

constexpr float kMinimumQuaternionLengthSquared = std::numeric_limits<float>::epsilon();

[[nodiscard]] float LargestAbsoluteComponent(const Quaternion& value) noexcept {
    return std::max(std::fabs(value.x), std::max(std::fabs(value.y), std::max(std::fabs(value.z), std::fabs(value.w))));
}

} // namespace

Quaternion Multiply(const Quaternion& lhs, const Quaternion& rhs) noexcept {
    return Quaternion{
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
    };
}

bool IsFinite(const Quaternion& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

float LengthSquared(const Quaternion& value) noexcept {
    return value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
}

bool TryNormalize(const Quaternion& value, Quaternion& outNormalized) noexcept {
    if (!IsFinite(value)) {
        return false;
    }

    const float lengthSquared = LengthSquared(value);
    if (std::isfinite(lengthSquared)) {
        if (lengthSquared <= kMinimumQuaternionLengthSquared) {
            return false;
        }

        const float inverseLength = 1.0F / std::sqrt(lengthSquared);
        if (!std::isfinite(inverseLength)) {
            return false;
        }

        const Quaternion candidate{
            value.x * inverseLength,
            value.y * inverseLength,
            value.z * inverseLength,
            value.w * inverseLength,
        };
        if (!IsFinite(candidate)) {
            return false;
        }

        outNormalized = candidate;
        return true;
    }

    const float scale = LargestAbsoluteComponent(value);
    if (!std::isfinite(scale) || scale <= 0.0F) {
        return false;
    }
    const double scaledX = static_cast<double>(value.x) / static_cast<double>(scale);
    const double scaledY = static_cast<double>(value.y) / static_cast<double>(scale);
    const double scaledZ = static_cast<double>(value.z) / static_cast<double>(scale);
    const double scaledW = static_cast<double>(value.w) / static_cast<double>(scale);
    const double scaledLengthSquared = scaledX * scaledX + scaledY * scaledY + scaledZ * scaledZ + scaledW * scaledW;
    const double inverseScaledLength = 1.0 / std::sqrt(scaledLengthSquared);
    if (!std::isfinite(inverseScaledLength)) {
        return false;
    }
    const Quaternion candidate{
        static_cast<float>(scaledX * inverseScaledLength),
        static_cast<float>(scaledY * inverseScaledLength),
        static_cast<float>(scaledZ * inverseScaledLength),
        static_cast<float>(scaledW * inverseScaledLength),
    };
    if (!IsFinite(candidate)) {
        return false;
    }

    outNormalized = candidate;
    return true;
}

bool TryInverse(const Quaternion& value, Quaternion& outInverse) noexcept {
    if (!IsFinite(value)) {
        return false;
    }

    const float lengthSquared = LengthSquared(value);
    if (std::isfinite(lengthSquared)) {
        if (lengthSquared <= kMinimumQuaternionLengthSquared) {
            return false;
        }

        const float inverseLengthSquared = 1.0F / lengthSquared;
        if (!std::isfinite(inverseLengthSquared)) {
            return false;
        }

        const Quaternion candidate{
            -value.x * inverseLengthSquared,
            -value.y * inverseLengthSquared,
            -value.z * inverseLengthSquared,
            value.w * inverseLengthSquared,
        };
        if (!IsFinite(candidate)) {
            return false;
        }

        outInverse = candidate;
        return true;
    }

    const float scale = LargestAbsoluteComponent(value);
    if (!std::isfinite(scale) || scale <= 0.0F) {
        return false;
    }
    const double scaledX = static_cast<double>(value.x) / static_cast<double>(scale);
    const double scaledY = static_cast<double>(value.y) / static_cast<double>(scale);
    const double scaledZ = static_cast<double>(value.z) / static_cast<double>(scale);
    const double scaledW = static_cast<double>(value.w) / static_cast<double>(scale);
    const double scaledLengthSquared = scaledX * scaledX + scaledY * scaledY + scaledZ * scaledZ + scaledW * scaledW;
    const double inverseLengthSquared = 1.0 / (static_cast<double>(scale) * static_cast<double>(scale) * scaledLengthSquared);
    if (!std::isfinite(inverseLengthSquared)) {
        return false;
    }
    const Quaternion candidate{
        static_cast<float>(-static_cast<double>(value.x) * inverseLengthSquared),
        static_cast<float>(-static_cast<double>(value.y) * inverseLengthSquared),
        static_cast<float>(-static_cast<double>(value.z) * inverseLengthSquared),
        static_cast<float>(static_cast<double>(value.w) * inverseLengthSquared),
    };
    if (!IsFinite(candidate)) {
        return false;
    }

    outInverse = candidate;
    return true;
}

bool TryMakeAxisAngle(const Vector3& axis, const float radians, Quaternion& outRotation) noexcept {
    if (!IsFinite(axis) || !std::isfinite(radians)) {
        return false;
    }

    const float axisLengthSquared = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
    if (std::isfinite(axisLengthSquared) && axisLengthSquared <= kMinimumQuaternionLengthSquared) {
        return false;
    }

    const float halfAngle = radians * 0.5F;
    if (!std::isfinite(halfAngle)) {
        return false;
    }

    const float sine = std::sin(halfAngle);
    const float cosine = std::cos(halfAngle);
    if (!std::isfinite(sine) || !std::isfinite(cosine)) {
        return false;
    }

    const Vector3 normalizedAxis = Normalize(axis);
    if (!IsFinite(normalizedAxis)) {
        return false;
    }
    const Quaternion candidate{
        normalizedAxis.x * sine,
        normalizedAxis.y * sine,
        normalizedAxis.z * sine,
        cosine,
    };
    return TryNormalize(candidate, outRotation);
}

Vector3 RotateVector(const Quaternion& rotation, const Vector3& vector) noexcept {
    const Vector3 axis{rotation.x, rotation.y, rotation.z};
    const Vector3 twiceCross = Cross(axis, vector) * 2.0F;
    const Vector3 scalarPart = twiceCross * rotation.w;
    const Vector3 correction = Cross(axis, twiceCross);
    const Vector3 floatResult = vector + scalarPart + correction;
    if (IsFinite(twiceCross) && IsFinite(scalarPart) && IsFinite(correction) &&
        IsFinite(floatResult)) {
        return floatResult;
    }

    const double axisX = static_cast<double>(rotation.x);
    const double axisY = static_cast<double>(rotation.y);
    const double axisZ = static_cast<double>(rotation.z);
    const double vectorX = static_cast<double>(vector.x);
    const double vectorY = static_cast<double>(vector.y);
    const double vectorZ = static_cast<double>(vector.z);

    const double twiceCrossX = 2.0 * (axisY * vectorZ - axisZ * vectorY);
    const double twiceCrossY = 2.0 * (axisZ * vectorX - axisX * vectorZ);
    const double twiceCrossZ = 2.0 * (axisX * vectorY - axisY * vectorX);
    const double correctionX = axisY * twiceCrossZ - axisZ * twiceCrossY;
    const double correctionY = axisZ * twiceCrossX - axisX * twiceCrossZ;
    const double correctionZ = axisX * twiceCrossY - axisY * twiceCrossX;
    const double scalar = static_cast<double>(rotation.w);

    return Vector3{
        static_cast<float>(vectorX + twiceCrossX * scalar + correctionX),
        static_cast<float>(vectorY + twiceCrossY * scalar + correctionY),
        static_cast<float>(vectorZ + twiceCrossZ * scalar + correctionZ),
    };
}

namespace {

/// Unit rotation about a single axis. Kept file-local: these are building blocks for the ordered
/// Euler composition below, not a general axis-angle API - TryMakeAxisAngle() is that.
[[nodiscard]] Quaternion AxisRotationX(const float radians) noexcept {
    return Quaternion{std::sin(radians * 0.5F), 0.0F, 0.0F, std::cos(radians * 0.5F)};
}

[[nodiscard]] Quaternion AxisRotationY(const float radians) noexcept {
    return Quaternion{0.0F, std::sin(radians * 0.5F), 0.0F, std::cos(radians * 0.5F)};
}

[[nodiscard]] Quaternion AxisRotationZ(const float radians) noexcept {
    return Quaternion{0.0F, 0.0F, std::sin(radians * 0.5F), std::cos(radians * 0.5F)};
}

/// Rotation matrix of a unit quaternion, row-major, column-vector convention.
void ToRotationMatrix(const Quaternion& q, float outMatrix[3][3]) noexcept {
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;
    outMatrix[0][0] = 1.0F - 2.0F * (yy + zz);
    outMatrix[0][1] = 2.0F * (xy - wz);
    outMatrix[0][2] = 2.0F * (xz + wy);
    outMatrix[1][0] = 2.0F * (xy + wz);
    outMatrix[1][1] = 1.0F - 2.0F * (xx + zz);
    outMatrix[1][2] = 2.0F * (yz - wx);
    outMatrix[2][0] = 2.0F * (xz - wy);
    outMatrix[2][1] = 2.0F * (yz + wx);
    outMatrix[2][2] = 1.0F - 2.0F * (xx + yy);
}

/// How close the middle axis may get to +/-90 degrees before the decomposition is treated as
/// singular. Just inside 1 so std::asin never sees an out-of-domain value from rounding; the
/// rotation still round-trips exactly, only the angle split becomes arbitrary.
constexpr float kEulerSingularLimit = 0.999999F;

[[nodiscard]] float ClampToAsinDomain(const float value) noexcept {
    if (value > kEulerSingularLimit) {
        return kEulerSingularLimit;
    }
    if (value < -kEulerSingularLimit) {
        return -kEulerSingularLimit;
    }
    return value;
}

} // namespace

bool TryMakeEulerOrdered(const Vector3& radians, const EulerOrder order,
                            Quaternion& outRotation) noexcept {
    if (!IsFinite(radians)) {
        return false;
    }
    const Quaternion x = AxisRotationX(radians.x);
    const Quaternion y = AxisRotationY(radians.y);
    const Quaternion z = AxisRotationZ(radians.z);

    // Applied first-to-last about the PARENT frame, which composes right-to-left: the axis named
    // first in the order is the rightmost factor. XYZ is therefore z*y*x, which is exactly the
    // expression TryMakeEuler() has always used - asserted by test, so this is a
    // generalisation of the existing behaviour rather than a change to it.
    Quaternion composed{};
    switch (order) {
        case EulerOrder::XYZ: composed = Multiply(z, Multiply(y, x)); break;
        case EulerOrder::YXZ: composed = Multiply(z, Multiply(x, y)); break;
        case EulerOrder::ZYX: composed = Multiply(x, Multiply(y, z)); break;
        case EulerOrder::XZY: composed = Multiply(y, Multiply(z, x)); break;
        case EulerOrder::YZX: composed = Multiply(x, Multiply(z, y)); break;
        case EulerOrder::ZXY: composed = Multiply(y, Multiply(x, z)); break;
        default: return false;
    }
    return TryNormalize(composed, outRotation);
}

bool TryToEulerOrdered(const Quaternion& rotation, const EulerOrder order,
                          Vector3& outRadians) noexcept {
    Quaternion normalized{};
    if (!TryNormalize(rotation, normalized)) {
        return false;
    }
    float m[3][3];
    ToRotationMatrix(normalized, m);

    // Each case reads the middle axis out of the one matrix element that isolates it, then the
    // outer two from a pair of atan2 terms. The `else` arm is the gimbal-lock pose: the two outer
    // rotations become a single combined turn, so one is reported as zero and the other absorbs
    // it. That is a real ambiguity in the angles, not an error - the rotation still rebuilds
    // exactly, which is what the round-trip tests assert.
    Vector3 result{};
    switch (order) {
        case EulerOrder::XYZ:
            result.y = std::asin(ClampToAsinDomain(-m[2][0]));
            if (std::abs(m[2][0]) < kEulerSingularLimit) {
                result.x = std::atan2(m[2][1], m[2][2]);
                result.z = std::atan2(m[1][0], m[0][0]);
            } else {
                result.x = std::atan2(-m[1][2], m[1][1]);
                result.z = 0.0F;
            }
            break;
        case EulerOrder::YXZ:
            result.x = std::asin(ClampToAsinDomain(m[2][1]));
            if (std::abs(m[2][1]) < kEulerSingularLimit) {
                result.y = std::atan2(-m[2][0], m[2][2]);
                result.z = std::atan2(-m[0][1], m[1][1]);
            } else {
                result.y = std::atan2(m[0][2], m[0][0]);
                result.z = 0.0F;
            }
            break;
        case EulerOrder::ZYX:
            result.y = std::asin(ClampToAsinDomain(m[0][2]));
            if (std::abs(m[0][2]) < kEulerSingularLimit) {
                result.z = std::atan2(-m[0][1], m[0][0]);
                result.x = std::atan2(-m[1][2], m[2][2]);
            } else {
                result.z = std::atan2(m[1][0], m[1][1]);
                result.x = 0.0F;
            }
            break;
        case EulerOrder::XZY:
            result.z = std::asin(ClampToAsinDomain(m[1][0]));
            if (std::abs(m[1][0]) < kEulerSingularLimit) {
                result.x = std::atan2(-m[1][2], m[1][1]);
                result.y = std::atan2(-m[2][0], m[0][0]);
            } else {
                result.x = std::atan2(m[2][1], m[2][2]);
                result.y = 0.0F;
            }
            break;
        case EulerOrder::YZX:
            result.z = std::asin(ClampToAsinDomain(-m[0][1]));
            if (std::abs(m[0][1]) < kEulerSingularLimit) {
                result.y = std::atan2(m[0][2], m[0][0]);
                result.x = std::atan2(m[2][1], m[1][1]);
            } else {
                result.y = std::atan2(-m[2][0], m[2][2]);
                result.x = 0.0F;
            }
            break;
        case EulerOrder::ZXY:
            result.x = std::asin(ClampToAsinDomain(-m[1][2]));
            if (std::abs(m[1][2]) < kEulerSingularLimit) {
                result.z = std::atan2(m[1][0], m[1][1]);
                result.y = std::atan2(m[0][2], m[2][2]);
            } else {
                result.z = std::atan2(-m[0][1], m[0][0]);
                result.y = 0.0F;
            }
            break;
        default:
            return false;
    }
    if (!IsFinite(result)) {
        return false;
    }
    outRadians = result;
    return true;
}

bool TryMakeEuler(const Vector3& radians, Quaternion& outRotation) noexcept {
    if (!IsFinite(radians)) {
        return false;
    }
    const Vector3 half = radians * 0.5F;
    const float sx = std::sin(half.x);
    const float cx = std::cos(half.x);
    const float sy = std::sin(half.y);
    const float cy = std::cos(half.y);
    const float sz = std::sin(half.z);
    const float cz = std::cos(half.z);
    if (!std::isfinite(sx) || !std::isfinite(cx) || !std::isfinite(sy) || !std::isfinite(cy) ||
        !std::isfinite(sz) || !std::isfinite(cz)) {
        return false;
    }
    return TryNormalize(Quaternion{
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz,
    }, outRotation);
}

bool TryToEuler(const Quaternion& rotation, Vector3& outRadians) noexcept {
    Quaternion q{};
    if (!TryNormalize(rotation, q)) {
        return false;
    }

    // Matches TryMakeEuler()'s exact composition (verified by direct symbolic expansion:
    // q = qz(z) * qy(y) * qx(x)) - the standard closed-form quaternion-to-Euler extraction for
    // that ordering.
    const float sinXCosY = 2.0F * (q.w * q.x + q.y * q.z);
    const float cosXCosY = 1.0F - 2.0F * (q.x * q.x + q.y * q.y);
    const float angleX = std::atan2(sinXCosY, cosXCosY);

    const float sinY = std::clamp(2.0F * (q.w * q.y - q.z * q.x), -1.0F, 1.0F);
    const float angleY = std::asin(sinY);

    const float sinZCosY = 2.0F * (q.w * q.z + q.x * q.y);
    const float cosZCosY = 1.0F - 2.0F * (q.y * q.y + q.z * q.z);
    const float angleZ = std::atan2(sinZCosY, cosZCosY);

    const Vector3 candidate{angleX, angleY, angleZ};
    if (!IsFinite(candidate)) {
        return false;
    }
    outRadians = candidate;
    return true;
}

bool TryMakeLookAt(const Vector3& direction, const Vector3& up,
                      Quaternion& outRotation) noexcept {
    if (!IsFinite(direction) || !IsFinite(up)) {
        return false;
    }
    const float directionLengthSquared = LengthSquared(direction);
    const float upLengthSquared = LengthSquared(up);
    if ((std::isfinite(directionLengthSquared) &&
         directionLengthSquared <= kMinimumQuaternionLengthSquared) ||
        (std::isfinite(upLengthSquared) && upLengthSquared <= kMinimumQuaternionLengthSquared)) {
        return false;
    }
    const Vector3 forward = Normalize(direction);
    const Vector3 normalizedUp = Normalize(up);
    if (!IsFinite(forward) || !IsFinite(normalizedUp)) {
        return false;
    }
    const Vector3 rightUnnormalized = Cross(normalizedUp, forward);
    if (!IsFinite(rightUnnormalized) ||
        LengthSquared(rightUnnormalized) <= kMinimumQuaternionLengthSquared) {
        return false;
    }
    const Vector3 right = Normalize(rightUnnormalized);
    const Vector3 correctedUp = Cross(forward, right);
    const float trace = right.x + correctedUp.y + forward.z;
    Quaternion candidate{};
    if (trace > 0.0F) {
        const float scale = std::sqrt(trace + 1.0F) * 2.0F;
        if (!std::isfinite(scale) || scale <= std::numeric_limits<float>::epsilon()) return false;
        candidate = Quaternion{(correctedUp.z - forward.y) / scale, (forward.x - right.z) / scale,
                                  (right.y - correctedUp.x) / scale, scale * 0.25F};
    } else if (right.x > correctedUp.y && right.x > forward.z) {
        const float scale = std::sqrt(1.0F + right.x - correctedUp.y - forward.z) * 2.0F;
        if (!std::isfinite(scale) || scale <= std::numeric_limits<float>::epsilon()) return false;
        candidate = Quaternion{scale * 0.25F, (right.y + correctedUp.x) / scale,
                                  (right.z + forward.x) / scale, (correctedUp.z - forward.y) / scale};
    } else if (correctedUp.y > forward.z) {
        const float scale = std::sqrt(1.0F + correctedUp.y - right.x - forward.z) * 2.0F;
        if (!std::isfinite(scale) || scale <= std::numeric_limits<float>::epsilon()) return false;
        candidate = Quaternion{(right.y + correctedUp.x) / scale, scale * 0.25F,
                                  (correctedUp.z + forward.y) / scale, (forward.x - right.z) / scale};
    } else {
        const float scale = std::sqrt(1.0F + forward.z - right.x - correctedUp.y) * 2.0F;
        if (!std::isfinite(scale) || scale <= std::numeric_limits<float>::epsilon()) return false;
        candidate = Quaternion{(right.z + forward.x) / scale, (correctedUp.z + forward.y) / scale,
                                  scale * 0.25F, (right.y - correctedUp.x) / scale};
    }
    return TryNormalize(candidate, outRotation);
}

bool TrySlerp(const Quaternion& lhs, const Quaternion& rhs, const float alpha,
                 Quaternion& outRotation) noexcept {
    if (!IsFinite(lhs) || !IsFinite(rhs) || !std::isfinite(alpha)) return false;
    Quaternion a{};
    Quaternion b{};
    if (!TryNormalize(lhs, a) || !TryNormalize(rhs, b)) return false;
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0F) {
        b = Quaternion{-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }
    dot = std::clamp(dot, -1.0F, 1.0F);
    Quaternion candidate{};
    if (dot > 0.9995F) {
        candidate = Quaternion{a.x + alpha * (b.x - a.x), a.y + alpha * (b.y - a.y),
                                  a.z + alpha * (b.z - a.z), a.w + alpha * (b.w - a.w)};
    } else {
        const float theta = std::acos(dot);
        const float sineTheta = std::sin(theta);
        if (!std::isfinite(theta) || !std::isfinite(sineTheta) || std::fabs(sineTheta) <= 1.0e-6F) return false;
        const float leftWeight = std::sin((1.0F - alpha) * theta) / sineTheta;
        const float rightWeight = std::sin(alpha * theta) / sineTheta;
        candidate = Quaternion{leftWeight * a.x + rightWeight * b.x,
                                  leftWeight * a.y + rightWeight * b.y,
                                  leftWeight * a.z + rightWeight * b.z,
                                  leftWeight * a.w + rightWeight * b.w};
    }
    return TryNormalize(candidate, outRotation);
}

bool TryToAxisAngle(const Quaternion& rotation, Vector3& outAxis,
                       float& outRadians) noexcept {
    Quaternion normalized{};
    if (!TryNormalize(rotation, normalized)) return false;
    const float clampedW = std::clamp(normalized.w, -1.0F, 1.0F);
    const float radians = 2.0F * std::acos(clampedW);
    const float sine = std::sqrt(std::max(0.0F, 1.0F - clampedW * clampedW));
    if (!std::isfinite(radians) || !std::isfinite(sine)) return false;
    Vector3 axis{1.0F, 0.0F, 0.0F};
    if (sine > 1.0e-5F) {
        axis = Vector3{normalized.x / sine, normalized.y / sine, normalized.z / sine};
    }
    if (!IsFinite(axis)) return false;
    outAxis = axis;
    outRadians = radians;
    return true;
}

std::string ToString(const Quaternion& rotation) {
    return "(" + std::to_string(rotation.x) + ", " + std::to_string(rotation.y) + ", " +
           std::to_string(rotation.z) + ", " + std::to_string(rotation.w) + ")";
}

} // namespace UVE::Math
