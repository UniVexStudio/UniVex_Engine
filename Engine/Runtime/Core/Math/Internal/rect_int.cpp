// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "rect_int.h"

#include <string>

namespace UVE::Math {

std::string ToStringUVE(const RectIntUVE& rect) {
    return "(" + ToStringUVE(rect.position) + ", " + ToStringUVE(rect.size) + ")";
}

} // namespace UVE::Math
