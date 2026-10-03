// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/animation_driver_component_uve.h"

#include <cmath>

namespace UVE::Scene {

bool IsAnimationDriverComponentValidUVE(const AnimationDriverComponentUVE& component) noexcept {
    return std::isfinite(component.speedScale) && component.speedScale >= 0.0F &&
           component.processCallback <= AnimationProcessCallbackUVE::Physics &&
           component.rootMotion <= AnimationRootMotionModeUVE::ApplyToTarget &&
           component.transition <= AnimationTransitionModeUVE::Crossfade;
}

} // namespace UVE::Scene
