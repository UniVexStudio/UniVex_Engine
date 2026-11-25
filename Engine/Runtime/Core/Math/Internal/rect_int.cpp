// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "rect_int.h"

#include <string>

namespace UVE::Math {

std::string ToString(const RectInt& rect) {
    return "(" + ToString(rect.position) + ", " + ToString(rect.size) + ")";
}

} // namespace UVE::Math
