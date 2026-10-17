// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/animation_driver_component_uve.h"

#include <cmath>

namespace UVE::Scene {

bool IsAnimationProcessCallbackValidUVE(const AnimationProcessCallbackUVE callback) noexcept {
    return callback <= AnimationProcessCallbackUVE::Physics;
}

bool IsAnimationRootMotionModeValidUVE(const AnimationRootMotionModeUVE mode) noexcept {
    return mode <= AnimationRootMotionModeUVE::ApplyToTarget;
}

bool IsAnimationTransitionModeValidUVE(const AnimationTransitionModeUVE mode) noexcept {
    return mode <= AnimationTransitionModeUVE::Crossfade;
}

bool IsAnimationDriverComponentValidUVE(const AnimationDriverComponentUVE& component) noexcept {
    return std::isfinite(component.speedScale) && component.speedScale >= 0.0F &&
           IsAnimationProcessCallbackValidUVE(component.processCallback) &&
           IsAnimationRootMotionModeValidUVE(component.rootMotion) &&
           IsAnimationTransitionModeValidUVE(component.transition);
}

bool HasAnyAnimatedTransformChannelUVE(const AnimationDriverComponentUVE& component) noexcept {
    return component.animatePosition || component.animateRotation || component.animateScale;
}

bool ShouldStepAnimationDriverOnClockUVE(const AnimationDriverComponentUVE& component,
                                         const AnimationProcessCallbackUVE clock) noexcept {
    return component.active && component.processCallback == clock &&
           std::isfinite(component.speedScale) && component.speedScale >= 0.0F;
}

EntityUVE ResolveAnimationDriverTargetEntityUVE(
    const AnimationDriverComponentUVE& component,
    const EntityUVE fallbackParentEntity) noexcept {
    return component.target != kInvalidEntityUVE ? component.target : fallbackParentEntity;
}

float ComputeScaledAnimationDeltaTimeUVE(const AnimationDriverComponentUVE& component,
                                         const float deltaSeconds) noexcept {
    if (!component.active || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F ||
        !std::isfinite(component.speedScale) || component.speedScale <= 0.0F) {
        return 0.0F;
    }
    return deltaSeconds * component.speedScale;
}

AnimationDriverComponentUVE SanitizeAnimationDriverComponentUVE(
    const AnimationDriverComponentUVE& component) {
    AnimationDriverComponentUVE out = component;
    if (!std::isfinite(out.speedScale) || out.speedScale < 0.0F) {
        out.speedScale = 1.0F;
    }
    if (!IsAnimationProcessCallbackValidUVE(out.processCallback)) {
        out.processCallback = AnimationProcessCallbackUVE::Frame;
    }
    if (!IsAnimationRootMotionModeValidUVE(out.rootMotion)) {
        out.rootMotion = AnimationRootMotionModeUVE::Off;
    }
    if (!IsAnimationTransitionModeValidUVE(out.transition)) {
        out.transition = AnimationTransitionModeUVE::Inertialize;
    }
    return out;
}

} // namespace UVE::Scene
