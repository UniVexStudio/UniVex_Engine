// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/vector2_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Math {

float LengthUVE(const Vector2UVE& v) noexcept {
    return std::hypot(v.x, v.y);
}

Vector2UVE NormalizeUVE(const Vector2UVE& v) noexcept {
    const float scale = std::max(std::fabs(v.x), std::fabs(v.y));
    if (scale == 0.0F || !std::isfinite(scale)) {
        return v * (1.0F / LengthUVE(v));
    }
    const Vector2UVE scaled{v.x / scale, v.y / scale};
    return scaled * (1.0F / LengthUVE(scaled));
}

bool IsFiniteUVE(const Vector2UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

std::string ToStringUVE(const Vector2UVE& vector) {
    return "(" + std::to_string(vector.x) + ", " + std::to_string(vector.y) + ")";
}

} // namespace UVE::Math
