// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/rect_uve.h"

#include <cmath>
#include <string>

namespace UVE::Math {

bool IsFiniteUVE(const RectUVE& value) noexcept {
    return IsFiniteUVE(value.position) && IsFiniteUVE(value.size);
}

std::string ToStringUVE(const RectUVE& rect) {
    return "(" + ToStringUVE(rect.position) + ", " + ToStringUVE(rect.size) + ")";
}

} // namespace UVE::Math
