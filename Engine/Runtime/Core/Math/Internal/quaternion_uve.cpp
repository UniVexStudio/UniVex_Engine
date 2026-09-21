

#include "uve/math/quaternion_uve.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace UVE::Math {

namespace {

constexpr float kMinimumQuaternionLengthSquaredUVE = std::numeric_limits<float>::epsilon();

[[nodiscard]] float LargestAbsoluteComponentUVE(const QuaternionUVE& value) noexcept {
    return std::max(std::fabs(value.x), std::max(std::fabs(value.y), std::max(std::fabs(value.z), std::fabs(value.w))));
}

} // namespace

QuaternionUVE MultiplyUVE(const QuaternionUVE& lhs, const QuaternionUVE& rhs) noexcept {
    return QuaternionUVE{
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
    };
}

bool IsFiniteUVE(const QuaternionUVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

float LengthSquaredUVE(const QuaternionUVE& value) noexcept {
    return value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
}

bool TryNormalizeUVE(const QuaternionUVE& value, QuaternionUVE& outNormalized) noexcept {
    if (!IsFiniteUVE(value)) {
        return false;
    }

    const float lengthSquared = LengthSquaredUVE(value);
    if (std::isfinite(lengthSquared)) {
        if (lengthSquared <= kMinimumQuaternionLengthSquaredUVE) {
            return false;
        }

        const float inverseLength = 1.0F / std::sqrt(lengthSquared);
        if (!std::isfinite(inverseLength)) {
            return false;
        }

        const QuaternionUVE candidate{
            value.x * inverseLength,
            value.y * inverseLength,
            value.z * inverseLength,
            value.w * inverseLength,
        };
        if (!IsFiniteUVE(candidate)) {
            return false;
        }

        outNormalized = candidate;
        return true;
    }

    const float scale = LargestAbsoluteComponentUVE(value);
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
    const QuaternionUVE candidate{
        static_cast<float>(scaledX * inverseScaledLength),
        static_cast<float>(scaledY * inverseScaledLength),
        static_cast<float>(scaledZ * inverseScaledLength),
        static_cast<float>(scaledW * inverseScaledLength),
    };
    if (!IsFiniteUVE(candidate)) {
        return false;
    }

    outNormalized = candidate;
    return true;
}

bool TryInverseUVE(const QuaternionUVE& value, QuaternionUVE& outInverse) noexcept {
    if (!IsFiniteUVE(value)) {
        return false;
    }

    const float lengthSquared = LengthSquaredUVE(value);
    if (std::isfinite(lengthSquared)) {
        if (lengthSquared <= kMinimumQuaternionLengthSquaredUVE) {
            return false;
        }

        const float inverseLengthSquared = 1.0F / lengthSquared;
        if (!std::isfinite(inverseLengthSquared)) {
            return false;
        }

        const QuaternionUVE candidate{
            -value.x * inverseLengthSquared,
            -value.y * inverseLengthSquared,
            -value.z * inverseLengthSquared,
            value.w * inverseLengthSquared,
        };
        if (!IsFiniteUVE(candidate)) {
            return false;
        }

        outInverse = candidate;
        return true;
    }

    const float scale = LargestAbsoluteComponentUVE(value);
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
    const QuaternionUVE candidate{
        static_cast<float>(-static_cast<double>(value.x) * inverseLengthSquared),
        static_cast<float>(-static_cast<double>(value.y) * inverseLengthSquared),
        static_cast<float>(-static_cast<double>(value.z) * inverseLengthSquared),
        static_cast<float>(static_cast<double>(value.w) * inverseLengthSquared),
    };
    if (!IsFiniteUVE(candidate)) {
        return false;
    }

    outInverse = candidate;
    return true;
}

bool TryMakeAxisAngleUVE(const Vector3UVE& axis, const float radians, QuaternionUVE& outRotation) noexcept {
    if (!IsFiniteUVE(axis) || !std::isfinite(radians)) {
        return false;
    }

    const float axisLengthSquared = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
    if (std::isfinite(axisLengthSquared) && axisLengthSquared <= kMinimumQuaternionLengthSquaredUVE) {
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

    const Vector3UVE normalizedAxis = NormalizeUVE(axis);
    if (!IsFiniteUVE(normalizedAxis)) {
        return false;
    }
    const QuaternionUVE candidate{
        normalizedAxis.x * sine,
        normalizedAxis.y * sine,
        normalizedAxis.z * sine,
        cosine,
    };
    return TryNormalizeUVE(candidate, outRotation);
}

Vector3UVE RotateVectorUVE(const QuaternionUVE& rotation, const Vector3UVE& vector) noexcept {
    const Vector3UVE axis{rotation.x, rotation.y, rotation.z};
    const Vector3UVE twiceCross = CrossUVE(axis, vector) * 2.0F;
    const Vector3UVE scalarPart = twiceCross * rotation.w;
    const Vector3UVE correction = CrossUVE(axis, twiceCross);
    const Vector3UVE floatResult = vector + scalarPart + correction;
    if (IsFiniteUVE(twiceCross) && IsFiniteUVE(scalarPart) && IsFiniteUVE(correction) &&
        IsFiniteUVE(floatResult)) {
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

    return Vector3UVE{
        static_cast<float>(vectorX + twiceCrossX * scalar + correctionX),
        static_cast<float>(vectorY + twiceCrossY * scalar + correctionY),
        static_cast<float>(vectorZ + twiceCrossZ * scalar + correctionZ),
    };
}

namespace {

/// Unit rotation about a single axis. Kept file-local: these are building blocks for the ordered
/// Euler composition below, not a general axis-angle API - TryMakeAxisAngleUVE() is that.
[[nodiscard]] QuaternionUVE AxisRotationXUVE(const float radians) noexcept {
    return QuaternionUVE{std::sin(radians * 0.5F), 0.0F, 0.0F, std::cos(radians * 0.5F)};
}

[[nodiscard]] QuaternionUVE AxisRotationYUVE(const float radians) noexcept {
    return QuaternionUVE{0.0F, std::sin(radians * 0.5F), 0.0F, std::cos(radians * 0.5F)};
}

[[nodiscard]] QuaternionUVE AxisRotationZUVE(const float radians) noexcept {
    return QuaternionUVE{0.0F, 0.0F, std::sin(radians * 0.5F), std::cos(radians * 0.5F)};
}

/// Rotation matrix of a unit quaternion, row-major, column-vector convention.
void ToRotationMatrixUVE(const QuaternionUVE& q, float outMatrix[3][3]) noexcept {
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
constexpr float kEulerSingularLimitUVE = 0.999999F;

[[nodiscard]] float ClampToAsinDomainUVE(const float value) noexcept {
    if (value > kEulerSingularLimitUVE) {
        return kEulerSingularLimitUVE;
    }
    if (value < -kEulerSingularLimitUVE) {
        return -kEulerSingularLimitUVE;
    }
    return value;
}

} // namespace

bool TryMakeEulerOrderedUVE(const Vector3UVE& radians, const EulerOrderUVE order,
                            QuaternionUVE& outRotation) noexcept {
    if (!IsFiniteUVE(radians)) {
        return false;
    }
    const QuaternionUVE x = AxisRotationXUVE(radians.x);
    const QuaternionUVE y = AxisRotationYUVE(radians.y);
    const QuaternionUVE z = AxisRotationZUVE(radians.z);

    // Applied first-to-last about the PARENT frame, which composes right-to-left: the axis named
    // first in the order is the rightmost factor. XYZ is therefore z*y*x, which is exactly the
    // expression TryMakeEulerUVE() has always used - asserted by test, so this is a
    // generalisation of the existing behaviour rather than a change to it.
    QuaternionUVE composed{};
    switch (order) {
        case EulerOrderUVE::XYZ: composed = MultiplyUVE(z, MultiplyUVE(y, x)); break;
        case EulerOrderUVE::YXZ: composed = MultiplyUVE(z, MultiplyUVE(x, y)); break;
        case EulerOrderUVE::ZYX: composed = MultiplyUVE(x, MultiplyUVE(y, z)); break;
        case EulerOrderUVE::XZY: composed = MultiplyUVE(y, MultiplyUVE(z, x)); break;
        case EulerOrderUVE::YZX: composed = MultiplyUVE(x, MultiplyUVE(z, y)); break;
        case EulerOrderUVE::ZXY: composed = MultiplyUVE(y, MultiplyUVE(x, z)); break;
        default: return false;
    }
    return TryNormalizeUVE(composed, outRotation);
}

bool TryToEulerOrderedUVE(const QuaternionUVE& rotation, const EulerOrderUVE order,
                          Vector3UVE& outRadians) noexcept {
    QuaternionUVE normalized{};
    if (!TryNormalizeUVE(rotation, normalized)) {
        return false;
    }
    float m[3][3];
    ToRotationMatrixUVE(normalized, m);

    // Each case reads the middle axis out of the one matrix element that isolates it, then the
    // outer two from a pair of atan2 terms. The `else` arm is the gimbal-lock pose: the two outer
    // rotations become a single combined turn, so one is reported as zero and the other absorbs
    // it. That is a real ambiguity in the angles, not an error - the rotation still rebuilds
    // exactly, which is what the round-trip tests assert.
    Vector3UVE result{};
    switch (order) {
        case EulerOrderUVE::XYZ:
            result.y = std::asin(ClampToAsinDomainUVE(-m[2][0]));
            if (std::abs(m[2][0]) < kEulerSingularLimitUVE) {
                result.x = std::atan2(m[2][1], m[2][2]);
                result.z = std::atan2(m[1][0], m[0][0]);
            } else {
                result.x = std::atan2(-m[1][2], m[1][1]);
                result.z = 0.0F;
            }
            break;
        case EulerOrderUVE::YXZ:
            result.x = std::asin(ClampToAsinDomainUVE(m[2][1]));
            if (std::abs(m[2][1]) < kEulerSingularLimitUVE) {
                result.y = std::atan2(-m[2][0], m[2][2]);
                result.z = std::atan2(-m[0][1], m[1][1]);
            } else {
                result.y = std::atan2(m[0][2], m[0][0]);
                result.z = 0.0F;
            }
            break;
        case EulerOrderUVE::ZYX:
            result.y = std::asin(ClampToAsinDomainUVE(m[0][2]));
            if (std::abs(m[0][2]) < kEulerSingularLimitUVE) {
                result.z = std::atan2(-m[0][1], m[0][0]);
                result.x = std::atan2(-m[1][2], m[2][2]);
            } else {
                result.z = std::atan2(m[1][0], m[1][1]);
                result.x = 0.0F;
            }
            break;
        case EulerOrderUVE::XZY:
            result.z = std::asin(ClampToAsinDomainUVE(m[1][0]));
            if (std::abs(m[1][0]) < kEulerSingularLimitUVE) {
                result.x = std::atan2(-m[1][2], m[1][1]);
                result.y = std::atan2(-m[2][0], m[0][0]);
            } else {
                result.x = std::atan2(m[2][1], m[2][2]);
                result.y = 0.0F;
            }
            break;
        case EulerOrderUVE::YZX:
            result.z = std::asin(ClampToAsinDomainUVE(-m[0][1]));
            if (std::abs(m[0][1]) < kEulerSingularLimitUVE) {
                result.y = std::atan2(m[0][2], m[0][0]);
                result.x = std::atan2(m[2][1], m[1][1]);
            } else {
                result.y = std::atan2(-m[2][0], m[2][2]);
                result.x = 0.0F;
            }
            break;
        case EulerOrderUVE::ZXY:
            result.x = std::asin(ClampToAsinDomainUVE(-m[1][2]));
            if (std::abs(m[1][2]) < kEulerSingularLimitUVE) {
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
    if (!IsFiniteUVE(result)) {
        return false;
    }
    outRadians = result;
    return true;
}

bool TryMakeEulerUVE(const Vector3UVE& radians, QuaternionUVE& outRotation) noexcept {
    if (!IsFiniteUVE(radians)) {
        return false;
    }
    const Vector3UVE half = radians * 0.5F;
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
    return TryNormalizeUVE(QuaternionUVE{
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz,
    }, outRotation);
}

bool TryToEulerUVE(const QuaternionUVE& rotation, Vector3UVE& outRadians) noexcept {
    QuaternionUVE q{};
    if (!TryNormalizeUVE(rotation, q)) {
        return false;
    }

    // Matches TryMakeEulerUVE()'s exact composition (verified by direct symbolic expansion:
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

    const Vector3UVE candidate{angleX, angleY, angleZ};
    if (!IsFiniteUVE(candidate)) {
        return false;
    }
    outRadians = candidate;
    return true;
}

bool TryMakeLookAtUVE(const Vector3UVE& direction, const Vector3UVE& up,
                      QuaternionUVE& outRotation) noexcept {
    if (!IsFiniteUVE(direction) || !IsFiniteUVE(up)) {
        return false;
    }
    const float directionLengthSquared = LengthSquaredUVE(direction);
    const float upLengthSquared = LengthSquaredUVE(up);
    if ((std::isfinite(directionLengthSquared) &&
         directionLengthSquared <= kMinimumQuaternionLengthSquaredUVE) ||
        (std::isfinite(upLengthSquared) && upLengthSquared <= kMinimumQuaternionLengthSquaredUVE)) {
        return false;
    }
    const Vector3UVE forward = NormalizeUVE(direction);
    const Vector3UVE normalizedUp = NormalizeUVE(up);
    if (!IsFiniteUVE(forward) || !IsFiniteUVE(normalizedUp)) {
        return false;
    }
    const Vector3UVE rightUnnormalized = CrossUVE(normalizedUp, forward);
    if (!IsFiniteUVE(rightUnnormalized) ||
        LengthSquaredUVE(rightUnnormalized) <= kMinimumQuaternionLengthSquaredUVE) {
        return false;
    }
    const Vector3UVE right = NormalizeUVE(rightUnnormalized);
    const Vector3UVE correctedUp = CrossUVE(forward, right);
    const float trace = right.x + correctedUp.y + forward.z;
    QuaternionUVE candidate{};
    if (trace > 0.0F) {
        const float scale = std::sqrt(trace + 1.0F) * 2.0F;
        if (!std::isfinite(scale) || scale <= std::numeric_limits<float>::epsilon()) return false;
        candidate = QuaternionUVE{(correctedUp.z - forward.y) / scale, (forward.x - right.z) / scale,
                                  (right.y - correctedUp.x) / scale, scale * 0.25F};
    } else if (right.x > correctedUp.y && right.x > forward.z) {
        const float scale = std::sqrt(1.0F + right.x - correctedUp.y - forward.z) * 2.0F;
        if (!std::isfinite(scale) || scale <= std::numeric_limits<float>::epsilon()) return false;
        candidate = QuaternionUVE{scale * 0.25F, (right.y + correctedUp.x) / scale,
                                  (right.z + forward.x) / scale, (correctedUp.z - forward.y) / scale};
    } else if (correctedUp.y > forward.z) {
        const float scale = std::sqrt(1.0F + correctedUp.y - right.x - forward.z) * 2.0F;
        if (!std::isfinite(scale) || scale <= std::numeric_limits<float>::epsilon()) return false;
        candidate = QuaternionUVE{(right.y + correctedUp.x) / scale, scale * 0.25F,
                                  (correctedUp.z + forward.y) / scale, (forward.x - right.z) / scale};
    } else {
        const float scale = std::sqrt(1.0F + forward.z - right.x - correctedUp.y) * 2.0F;
        if (!std::isfinite(scale) || scale <= std::numeric_limits<float>::epsilon()) return false;
        candidate = QuaternionUVE{(right.z + forward.x) / scale, (correctedUp.z + forward.y) / scale,
                                  scale * 0.25F, (right.y - correctedUp.x) / scale};
    }
    return TryNormalizeUVE(candidate, outRotation);
}

bool TrySlerpUVE(const QuaternionUVE& lhs, const QuaternionUVE& rhs, const float alpha,
                 QuaternionUVE& outRotation) noexcept {
    if (!IsFiniteUVE(lhs) || !IsFiniteUVE(rhs) || !std::isfinite(alpha)) return false;
    QuaternionUVE a{};
    QuaternionUVE b{};
    if (!TryNormalizeUVE(lhs, a) || !TryNormalizeUVE(rhs, b)) return false;
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0F) {
        b = QuaternionUVE{-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }
    dot = std::clamp(dot, -1.0F, 1.0F);
    QuaternionUVE candidate{};
    if (dot > 0.9995F) {
        candidate = QuaternionUVE{a.x + alpha * (b.x - a.x), a.y + alpha * (b.y - a.y),
                                  a.z + alpha * (b.z - a.z), a.w + alpha * (b.w - a.w)};
    } else {
        const float theta = std::acos(dot);
        const float sineTheta = std::sin(theta);
        if (!std::isfinite(theta) || !std::isfinite(sineTheta) || std::fabs(sineTheta) <= 1.0e-6F) return false;
        const float leftWeight = std::sin((1.0F - alpha) * theta) / sineTheta;
        const float rightWeight = std::sin(alpha * theta) / sineTheta;
        candidate = QuaternionUVE{leftWeight * a.x + rightWeight * b.x,
                                  leftWeight * a.y + rightWeight * b.y,
                                  leftWeight * a.z + rightWeight * b.z,
                                  leftWeight * a.w + rightWeight * b.w};
    }
    return TryNormalizeUVE(candidate, outRotation);
}

bool TryToAxisAngleUVE(const QuaternionUVE& rotation, Vector3UVE& outAxis,
                       float& outRadians) noexcept {
    QuaternionUVE normalized{};
    if (!TryNormalizeUVE(rotation, normalized)) return false;
    const float clampedW = std::clamp(normalized.w, -1.0F, 1.0F);
    const float radians = 2.0F * std::acos(clampedW);
    const float sine = std::sqrt(std::max(0.0F, 1.0F - clampedW * clampedW));
    if (!std::isfinite(radians) || !std::isfinite(sine)) return false;
    Vector3UVE axis{1.0F, 0.0F, 0.0F};
    if (sine > 1.0e-5F) {
        axis = Vector3UVE{normalized.x / sine, normalized.y / sine, normalized.z / sine};
    }
    if (!IsFiniteUVE(axis)) return false;
    outAxis = axis;
    outRadians = radians;
    return true;
}

std::string ToStringUVE(const QuaternionUVE& rotation) {
    return "(" + std::to_string(rotation.x) + ", " + std::to_string(rotation.y) + ", " +
           std::to_string(rotation.z) + ", " + std::to_string(rotation.w) + ")";
}

} // namespace UVE::Math
