// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/math/sphere_uve.h"

#include <string>

namespace UVE::Math {

std::string ToStringUVE(const SphereUVE& sphere) {
    return "SphereUVE(center=" + ToStringUVE(sphere.center) +
           ", radius=" + std::to_string(sphere.radius) + ")";
}

} // namespace UVE::Math
