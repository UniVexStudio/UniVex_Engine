// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "vector2.h"

#include <algorithm>
#include <cmath>

namespace UVE::Math {

float Length(const Vector2& v) noexcept {
    return std::hypot(v.x, v.y);
}

Vector2 Normalize(const Vector2& v) noexcept {
    const float scale = std::max(std::fabs(v.x), std::fabs(v.y));
    if (scale == 0.0F || !std::isfinite(scale)) {
        return v * (1.0F / Length(v));
    }
    const Vector2 scaled{v.x / scale, v.y / scale};
    return scaled * (1.0F / Length(scaled));
}

bool IsFinite(const Vector2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

std::string ToString(const Vector2& vector) {
    return "(" + std::to_string(vector.x) + ", " + std::to_string(vector.y) + ")";
}

} // namespace UVE::Math
