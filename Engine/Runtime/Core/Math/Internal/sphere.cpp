// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "sphere.h"

#include <string>

namespace UVE::Math {

std::string ToString(const Sphere& sphere) {
    return "Sphere(center=" + ToString(sphere.center) +
           ", radius=" + std::to_string(sphere.radius) + ")";
}

} // namespace UVE::Math
