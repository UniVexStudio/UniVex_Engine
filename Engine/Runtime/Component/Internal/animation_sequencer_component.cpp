// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_sequencer_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsNonNegative(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] bool IsFiniteVector(const Math::Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

bool IsAnimationLoopModeValid(const AnimationLoopMode mode) noexcept {
    return mode <= AnimationLoopMode::PingPong;
}

bool IsAnimationFinishActionValid(const AnimationFinishAction action) noexcept {
    return action <= AnimationFinishAction::ReturnToStart;
}

bool IsAnimationSequencerComponentValid(const AnimationSequencerComponent& component) noexcept {
    const AnimationSequencerComponent& c = component;
    return std::isfinite(c.speed) && IsAnimationLoopModeValid(c.loopMode) &&
           IsAnimationFinishActionValid(c.onFinish) && IsNonNegative(c.startOffsetSeconds) &&
           IsNonNegative(c.blendInSeconds) && IsNonNegative(c.currentTimeSeconds) &&
           (c.direction == 1.0F || c.direction == -1.0F) &&
           IsNonNegative(c.blendElapsedSeconds) && IsFiniteVector(c.startPosition) &&
           Math::IsFinite(c.startRotation) && IsFiniteVector(c.startScale);
}

bool HasPlayableAnimationClip(const AnimationSequencerComponent& component) noexcept {
    return component.clip != Asset::kInvalidAssetGuid;
}

float ResolveAnimationBlendInWeight(const AnimationSequencerComponent& component) noexcept {
    if (!std::isfinite(component.blendInSeconds) || component.blendInSeconds <= 0.0F) {
        return 1.0F;
    }
    if (!std::isfinite(component.blendElapsedSeconds) || component.blendElapsedSeconds <= 0.0F) {
        return 0.0F;
    }
    return std::clamp(component.blendElapsedSeconds / component.blendInSeconds, 0.0F, 1.0F);
}

void ResetAnimationSequencerRuntimeState(AnimationSequencerComponent& component) noexcept {
    component.isPlaying = false;
    component.currentTimeSeconds =
        IsNonNegative(component.startOffsetSeconds) ? component.startOffsetSeconds : 0.0F;
    component.direction = 1.0F;
    component.blendElapsedSeconds = 0.0F;
    component.finished = false;
    component.hasStartPose = false;
    component.startPosition = Math::Vector3{};
    component.startRotation = Math::Quaternion{};
    component.startScale = Math::Vector3{1.0F, 1.0F, 1.0F};
    component.rootMotionDelta = Math::Vector3{};
    component.playingClip = Asset::kInvalidAssetGuid;
    component.inertialPosition.clear();
    component.inertialRotation.clear();
    component.inertialScale.clear();
    component.firedEvents.clear();
}

AnimationSequencerComponent SanitizeAnimationSequencerComponent(
    const AnimationSequencerComponent& component) {
    AnimationSequencerComponent out = component;
    if (!std::isfinite(out.speed)) {
        out.speed = 1.0F;
    }
    if (!IsAnimationLoopModeValid(out.loopMode)) {
        out.loopMode = AnimationLoopMode::Loop;
    }
    if (!IsAnimationFinishActionValid(out.onFinish)) {
        out.onFinish = AnimationFinishAction::HoldLastPose;
    }
    if (!IsNonNegative(out.startOffsetSeconds)) {
        out.startOffsetSeconds = 0.0F;
    }
    if (!IsNonNegative(out.blendInSeconds)) {
        out.blendInSeconds = 0.0F;
    }
    if (!IsNonNegative(out.currentTimeSeconds)) {
        out.currentTimeSeconds = 0.0F;
    }
    if (out.direction != 1.0F && out.direction != -1.0F) {
        out.direction = 1.0F;
    }
    if (!IsNonNegative(out.blendElapsedSeconds)) {
        out.blendElapsedSeconds = 0.0F;
    }
    if (!IsFiniteVector(out.startPosition)) {
        out.startPosition = Math::Vector3{};
    }
    if (!Math::IsFinite(out.startRotation)) {
        out.startRotation = Math::Quaternion{};
    }
    if (!IsFiniteVector(out.startScale)) {
        out.startScale = Math::Vector3{1.0F, 1.0F, 1.0F};
    }
    return out;
}

} // namespace UVE::Scene
