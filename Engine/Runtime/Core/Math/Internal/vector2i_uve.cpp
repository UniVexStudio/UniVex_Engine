// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/vector2i_uve.h"

#include <cmath>
#include <string>

namespace UVE::Math {

float LengthUVE(const Vector2iUVE& v) noexcept {
    return static_cast<float>(std::hypot(static_cast<double>(v.x), static_cast<double>(v.y)));
}

std::string ToStringUVE(const Vector2iUVE& vector) {
    return "(" + std::to_string(vector.x) + ", " + std::to_string(vector.y) + ")";
}

} // namespace UVE::Math
