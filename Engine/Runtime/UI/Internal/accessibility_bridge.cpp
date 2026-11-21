// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "accessibility_bridge.h"

#include <algorithm>

namespace UVE::UI {

Math::Vector3UVE ApplyColorblindDaltonizationCpuUVE(const Math::Vector3UVE& rgb,
                                                   const ColorblindModeUVE mode,
                                                   const float severity) noexcept {
    const float s = std::clamp(severity, 0.0F, 1.0F);
    if (mode == ColorblindModeUVE::None || s <= 0.0F) {
        return rgb;
    }
    // RGB -> LMS (Hunt-Pointer-Estevez).
    const float l = 17.8824F * rgb.x + 43.5161F * rgb.y + 4.11935F * rgb.z;
    const float m = 3.45565F * rgb.x + 27.1554F * rgb.y + 3.86714F * rgb.z;
    const float k = 0.0299566F * rgb.x + 0.184309F * rgb.y + 1.46709F * rgb.z;

    float sl = l;
    float sm = m;
    float sk = k;
    if (mode == ColorblindModeUVE::Protanopia) {
        sl = 2.02344F * m - 2.52581F * k;
    } else if (mode == ColorblindModeUVE::Deuteranopia) {
        sm = 0.494207F * l + 1.24827F * k;
    } else if (mode == ColorblindModeUVE::Tritanopia) {
        sk = -0.395913F * l + 0.801109F * m;
    }

    // LMS -> RGB inverse.
    const float simR = 0.0809444479F * sl - 0.130504409F * sm + 0.116721066F * sk;
    const float simG = -0.0102485335F * sl + 0.0540193266F * sm - 0.113614708F * sk;
    const float simB = -0.000365296938F * sl - 0.00412161469F * sm + 0.693511405F * sk;

    // Error shift into distinguishable channels.
    const float errR = rgb.x - simR;
    const float errG = rgb.y - simG;
    const float errB = rgb.z - simB;

    const float corrR = rgb.x;
    const float corrG = rgb.y + 0.7F * errR + errG;
    const float corrB = rgb.z + 0.7F * errR + errB;

    return Math::Vector3UVE{
        std::clamp(rgb.x + (corrR - rgb.x) * s, 0.0F, 1.0F),
        std::clamp(rgb.y + (corrG - rgb.y) * s, 0.0F, 1.0F),
        std::clamp(rgb.z + (corrB - rgb.z) * s, 0.0F, 1.0F),
    };
}

} // namespace UVE::UI
