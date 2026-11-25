// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "vector3.h"

#include <limits>

namespace UVE::Math {

[[nodiscard]] constexpr bool IsFiniteScalar(float value) noexcept {
    return value == value && value <= std::numeric_limits<float>::max() &&
           value >= -std::numeric_limits<float>::max();
}

/// A plane in `normal . point + distance = 0` form, where `normal` is expected to be unit
/// length. Used by Frustum (Part 7.2 culling). Deliberately minimal: only the signed-distance
/// query Frustum's intersection test needs.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct Plane {
    Vector3 normal{0.0F, 1.0F, 0.0F};
    float distance = 0.0F;

    /// Builds a plane with the given unit `normal` passing through `point`.
    [[nodiscard]] static constexpr Plane FromNormalAndPoint(Vector3 normal, Vector3 point) noexcept {
        const float floatDistance = -(normal.x * point.x + normal.y * point.y + normal.z * point.z);
        if (IsFiniteScalar(floatDistance)) {
            return Plane{normal, floatDistance};
        }
        const double distance = -(static_cast<double>(normal.x) * static_cast<double>(point.x) +
                                  static_cast<double>(normal.y) * static_cast<double>(point.y) +
                                  static_cast<double>(normal.z) * static_cast<double>(point.z));
        return Plane{normal, static_cast<float>(distance)};
    }

    /// Positive when `point` is on the side `normal` points toward, negative on the other side,
    /// zero exactly on the plane.
    [[nodiscard]] constexpr float GetSignedDistance(Vector3 point) const noexcept {
        const float floatDistance = normal.x * point.x + normal.y * point.y + normal.z * point.z + distance;
        if (IsFiniteScalar(floatDistance)) {
            return floatDistance;
        }
        return static_cast<float>(static_cast<double>(normal.x) * static_cast<double>(point.x) +
                                  static_cast<double>(normal.y) * static_cast<double>(point.y) +
                                  static_cast<double>(normal.z) * static_cast<double>(point.z) +
                                  static_cast<double>(distance));
    }
};

} // namespace UVE::Math
