// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/bone_modifier_component_uve.h"

#include <cmath>

namespace UVE::Scene {

bool IsBoneModifierComponentValidUVE(const BoneModifierComponentUVE& component) noexcept {
    return std::isfinite(component.influence) && component.influence >= 0.0F && component.influence <= 1.0F;
}

} // namespace UVE::Scene
