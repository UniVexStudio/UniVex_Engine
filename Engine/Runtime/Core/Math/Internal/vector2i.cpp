// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "vector2i.h"

#include <cmath>
#include <string>

namespace UVE::Math {

float Length(const Vector2i& v) noexcept {
    return static_cast<float>(std::hypot(static_cast<double>(v.x), static_cast<double>(v.y)));
}

std::string ToString(const Vector2i& vector) {
    return "(" + std::to_string(vector.x) + ", " + std::to_string(vector.y) + ")";
}

} // namespace UVE::Math
