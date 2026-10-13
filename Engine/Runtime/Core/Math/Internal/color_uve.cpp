// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/color_uve.h"

#include <cmath>
#include <string>

namespace UVE::Math {

float DisplayToLinearUVE(const float value) noexcept {
    // Exact sRGB transfer (IEC 61966-2-1), computed in double like the rest of this module.
    // The low-segment division also covers negatives continuously (c/12.92 keeps its sign), and
    // the power segment extends monotonically past 1 for HDR inputs.
    constexpr double kDisplayThresholdUVE = 0.04045;
    const double c = static_cast<double>(value);
    if (c <= kDisplayThresholdUVE) {
        return static_cast<float>(c / 12.92);
    }
    return static_cast<float>(std::pow((c + 0.055) / 1.055, 2.4));
}

float LinearToDisplayUVE(const float value) noexcept {
    constexpr double kLinearThresholdUVE = 0.0031308;
    const double c = static_cast<double>(value);
    if (c <= kLinearThresholdUVE) {
        return static_cast<float>(12.92 * c);
    }
    return static_cast<float>(1.055 * std::pow(c, 1.0 / 2.4) - 0.055);
}

ColorUVE ColorFromDisplayUVE(const Vector3UVE& displayColor) noexcept {
    return ColorUVE{
        DisplayToLinearUVE(displayColor.x),
        DisplayToLinearUVE(displayColor.y),
        DisplayToLinearUVE(displayColor.z),
    };
}

Vector3UVE DisplayFromColorUVE(const ColorUVE& color) noexcept {
    return Vector3UVE{
        LinearToDisplayUVE(color.r),
        LinearToDisplayUVE(color.g),
        LinearToDisplayUVE(color.b),
    };
}

std::string ToStringUVE(const ColorUVE& color) {
    return "(" + std::to_string(color.r) + ", " + std::to_string(color.g) + ", " +
           std::to_string(color.b) + ")";
}

} // namespace UVE::Math
