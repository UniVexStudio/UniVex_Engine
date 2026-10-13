// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/vector4_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Math {

float LengthUVE(const Vector4UVE& v) noexcept {
    // std::hypot has no 4-argument overload; nesting two pairs keeps the same overflow-safe
    // scaling behavior as the 3-argument form Vector3UVE uses.
    return std::hypot(std::hypot(v.x, v.y), std::hypot(v.z, v.w));
}

Vector4UVE NormalizeUVE(const Vector4UVE& v) noexcept {
    const float scale = std::max(std::fabs(v.x), std::max(std::fabs(v.y), std::max(std::fabs(v.z), std::fabs(v.w))));
    if (scale == 0.0F || !std::isfinite(scale)) {
        return v * (1.0F / LengthUVE(v));
    }
    const Vector4UVE scaled{v.x / scale, v.y / scale, v.z / scale, v.w / scale};
    return scaled * (1.0F / LengthUVE(scaled));
}

std::string ToStringUVE(const Vector4UVE& vector) {
    return "(" + std::to_string(vector.x) + ", " + std::to_string(vector.y) + ", " +
           std::to_string(vector.z) + ", " + std::to_string(vector.w) + ")";
}

bool IsFiniteUVE(const Vector4UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
           std::isfinite(value.w);
}

} // namespace UVE::Math
