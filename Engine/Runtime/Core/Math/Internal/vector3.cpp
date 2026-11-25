// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "vector3.h"

#include <algorithm>
#include <cmath>

namespace UVE::Math {

float Length(const Vector3& v) noexcept {
    return std::hypot(v.x, v.y, v.z);
}

Vector3 Normalize(const Vector3& v) noexcept {
    const float scale = std::max(std::fabs(v.x), std::max(std::fabs(v.y), std::fabs(v.z)));
    if (scale == 0.0F || !std::isfinite(scale)) {
        return v * (1.0F / Length(v));
    }
    const Vector3 scaled{v.x / scale, v.y / scale, v.z / scale};
    return scaled * (1.0F / Length(scaled));
}

std::string ToString(const Vector3& vector) {
    return "(" + std::to_string(vector.x) + ", " + std::to_string(vector.y) + ", " +
           std::to_string(vector.z) + ")";
}

bool IsFinite(const Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace UVE::Math
