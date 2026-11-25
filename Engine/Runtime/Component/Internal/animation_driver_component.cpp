// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_driver_component.h"

#include <cmath>

namespace UVE::Scene {

bool IsAnimationProcessCallbackValid(const AnimationProcessCallback callback) noexcept {
    return callback <= AnimationProcessCallback::Physics;
}

bool IsAnimationRootMotionModeValid(const AnimationRootMotionMode mode) noexcept {
    return mode <= AnimationRootMotionMode::ApplyToTarget;
}

bool IsAnimationTransitionModeValid(const AnimationTransitionMode mode) noexcept {
    return mode <= AnimationTransitionMode::Crossfade;
}

bool IsAnimationDriverComponentValid(const AnimationDriverComponent& component) noexcept {
    return std::isfinite(component.speedScale) && component.speedScale >= 0.0F &&
           IsAnimationProcessCallbackValid(component.processCallback) &&
           IsAnimationRootMotionModeValid(component.rootMotion) &&
           IsAnimationTransitionModeValid(component.transition);
}

bool HasAnyAnimatedTransformChannel(const AnimationDriverComponent& component) noexcept {
    return component.animatePosition || component.animateRotation || component.animateScale;
}

bool ShouldStepAnimationDriverOnClock(const AnimationDriverComponent& component,
                                         const AnimationProcessCallback clock) noexcept {
    return component.active && component.processCallback == clock &&
           std::isfinite(component.speedScale) && component.speedScale >= 0.0F;
}

Entity ResolveAnimationDriverTargetEntity(
    const AnimationDriverComponent& component,
    const Entity fallbackParentEntity) noexcept {
    return component.target != kInvalidEntity ? component.target : fallbackParentEntity;
}

float ComputeScaledAnimationDeltaTime(const AnimationDriverComponent& component,
                                         const float deltaSeconds) noexcept {
    if (!component.active || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F ||
        !std::isfinite(component.speedScale) || component.speedScale <= 0.0F) {
        return 0.0F;
    }
    return deltaSeconds * component.speedScale;
}

AnimationDriverComponent SanitizeAnimationDriverComponent(
    const AnimationDriverComponent& component) {
    AnimationDriverComponent out = component;
    if (!std::isfinite(out.speedScale) || out.speedScale < 0.0F) {
        out.speedScale = 1.0F;
    }
    if (!IsAnimationProcessCallbackValid(out.processCallback)) {
        out.processCallback = AnimationProcessCallback::Frame;
    }
    if (!IsAnimationRootMotionModeValid(out.rootMotion)) {
        out.rootMotion = AnimationRootMotionMode::Off;
    }
    if (!IsAnimationTransitionModeValid(out.transition)) {
        out.transition = AnimationTransitionMode::Inertialize;
    }
    return out;
}

} // namespace UVE::Scene
