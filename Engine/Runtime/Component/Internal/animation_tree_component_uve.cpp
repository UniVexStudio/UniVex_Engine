// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/animation_tree_component_uve.h"

#include <cmath>

namespace UVE::Scene {

bool IsAnimationTreeComponentValidUVE(const AnimationTreeComponentUVE& component) noexcept {
    const AnimationTreeComponentUVE& c = component;
    return std::isfinite(c.blend) && c.blend >= 0.0F && c.blend <= 1.0F && std::isfinite(c.blendSmoothing) &&
           c.blendSmoothing >= 0.0F && std::isfinite(c.speed) && std::isfinite(c.currentBlend) &&
           c.currentBlend >= 0.0F && c.currentBlend <= 1.0F && std::isfinite(c.timeA) && c.timeA >= 0.0F &&
           std::isfinite(c.timeB) && c.timeB >= 0.0F;
}

} // namespace UVE::Scene
