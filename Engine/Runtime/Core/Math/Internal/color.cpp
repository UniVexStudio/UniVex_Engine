// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "color.h"

#include <cmath>
#include <string>

namespace UVE::Math {

float DisplayToLinear(const float value) noexcept {
    // Exact sRGB transfer (IEC 61966-2-1), computed in double like the rest of this module.
    // The low-segment division also covers negatives continuously (c/12.92 keeps its sign), and
    // the power segment extends monotonically past 1 for HDR inputs.
    constexpr double kDisplayThreshold = 0.04045;
    const double c = static_cast<double>(value);
    if (c <= kDisplayThreshold) {
        return static_cast<float>(c / 12.92);
    }
    return static_cast<float>(std::pow((c + 0.055) / 1.055, 2.4));
}

float LinearToDisplay(const float value) noexcept {
    constexpr double kLinearThreshold = 0.0031308;
    const double c = static_cast<double>(value);
    if (c <= kLinearThreshold) {
        return static_cast<float>(12.92 * c);
    }
    return static_cast<float>(1.055 * std::pow(c, 1.0 / 2.4) - 0.055);
}

Color ColorFromDisplay(const Vector3& displayColor) noexcept {
    return Color{
        DisplayToLinear(displayColor.x),
        DisplayToLinear(displayColor.y),
        DisplayToLinear(displayColor.z),
    };
}

Vector3 DisplayFromColor(const Color& color) noexcept {
    return Vector3{
        LinearToDisplay(color.r),
        LinearToDisplay(color.g),
        LinearToDisplay(color.b),
    };
}

std::string ToString(const Color& color) {
    return "(" + std::to_string(color.r) + ", " + std::to_string(color.g) + ", " +
           std::to_string(color.b) + ")";
}

} // namespace UVE::Math
