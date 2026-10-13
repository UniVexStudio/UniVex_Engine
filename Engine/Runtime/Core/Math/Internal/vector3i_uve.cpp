// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/vector3i_uve.h"

#include <cmath>
#include <string>

namespace UVE::Math {

float LengthUVE(const Vector3iUVE& v) noexcept {
    // std::hypot takes at most 3 arguments via the 2-arg/3-arg overloads; the 3-arg form keeps
    // this overflow-safe without a manual sqrt-of-squares.
    return static_cast<float>(
        std::hypot(static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)));
}

std::string ToStringUVE(const Vector3iUVE& vector) {
    return "(" + std::to_string(vector.x) + ", " + std::to_string(vector.y) + ", " +
           std::to_string(vector.z) + ")";
}

} // namespace UVE::Math
