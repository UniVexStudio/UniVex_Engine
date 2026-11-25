// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "rect.h"

#include <cmath>
#include <string>

namespace UVE::Math {

bool IsFinite(const Rect& value) noexcept {
    return IsFinite(value.position) && IsFinite(value.size);
}

std::string ToString(const Rect& rect) {
    return "(" + ToString(rect.position) + ", " + ToString(rect.size) + ")";
}

} // namespace UVE::Math
