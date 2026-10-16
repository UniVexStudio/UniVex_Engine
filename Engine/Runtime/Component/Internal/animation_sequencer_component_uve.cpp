// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/animation_sequencer_component_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsNonNegativeUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] bool IsFiniteVectorUVE(const Math::Vector3UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

bool IsAnimationLoopModeValidUVE(const AnimationLoopModeUVE mode) noexcept {
    return mode <= AnimationLoopModeUVE::PingPong;
}

bool IsAnimationFinishActionValidUVE(const AnimationFinishActionUVE action) noexcept {
    return action <= AnimationFinishActionUVE::ReturnToStart;
}

bool IsAnimationSequencerComponentValidUVE(const AnimationSequencerComponentUVE& component) noexcept {
    const AnimationSequencerComponentUVE& c = component;
    return std::isfinite(c.speed) && IsAnimationLoopModeValidUVE(c.loopMode) &&
           IsAnimationFinishActionValidUVE(c.onFinish) && IsNonNegativeUVE(c.startOffsetSeconds) &&
           IsNonNegativeUVE(c.blendInSeconds) && IsNonNegativeUVE(c.currentTimeSeconds) &&
           (c.direction == 1.0F || c.direction == -1.0F) &&
           IsNonNegativeUVE(c.blendElapsedSeconds) && IsFiniteVectorUVE(c.startPosition) &&
           Math::IsFiniteUVE(c.startRotation) && IsFiniteVectorUVE(c.startScale);
}

bool HasPlayableAnimationClipUVE(const AnimationSequencerComponentUVE& component) noexcept {
    return component.clip != Asset::kInvalidAssetGuidUVE;
}

float ResolveAnimationBlendInWeightUVE(const AnimationSequencerComponentUVE& component) noexcept {
    if (!std::isfinite(component.blendInSeconds) || component.blendInSeconds <= 0.0F) {
        return 1.0F;
    }
    if (!std::isfinite(component.blendElapsedSeconds) || component.blendElapsedSeconds <= 0.0F) {
        return 0.0F;
    }
    return std::clamp(component.blendElapsedSeconds / component.blendInSeconds, 0.0F, 1.0F);
}

void ResetAnimationSequencerRuntimeStateUVE(AnimationSequencerComponentUVE& component) noexcept {
    component.isPlaying = false;
    component.currentTimeSeconds =
        IsNonNegativeUVE(component.startOffsetSeconds) ? component.startOffsetSeconds : 0.0F;
    component.direction = 1.0F;
    component.blendElapsedSeconds = 0.0F;
    component.finished = false;
    component.hasStartPose = false;
    component.startPosition = Math::Vector3UVE{};
    component.startRotation = Math::QuaternionUVE{};
    component.startScale = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    component.rootMotionDelta = Math::Vector3UVE{};
    component.playingClip = Asset::kInvalidAssetGuidUVE;
    component.inertialPosition.clear();
    component.inertialRotation.clear();
    component.inertialScale.clear();
    component.firedEvents.clear();
}

AnimationSequencerComponentUVE SanitizeAnimationSequencerComponentUVE(
    const AnimationSequencerComponentUVE& component) {
    AnimationSequencerComponentUVE out = component;
    if (!std::isfinite(out.speed)) {
        out.speed = 1.0F;
    }
    if (!IsAnimationLoopModeValidUVE(out.loopMode)) {
        out.loopMode = AnimationLoopModeUVE::Loop;
    }
    if (!IsAnimationFinishActionValidUVE(out.onFinish)) {
        out.onFinish = AnimationFinishActionUVE::HoldLastPose;
    }
    if (!IsNonNegativeUVE(out.startOffsetSeconds)) {
        out.startOffsetSeconds = 0.0F;
    }
    if (!IsNonNegativeUVE(out.blendInSeconds)) {
        out.blendInSeconds = 0.0F;
    }
    if (!IsNonNegativeUVE(out.currentTimeSeconds)) {
        out.currentTimeSeconds = 0.0F;
    }
    if (out.direction != 1.0F && out.direction != -1.0F) {
        out.direction = 1.0F;
    }
    if (!IsNonNegativeUVE(out.blendElapsedSeconds)) {
        out.blendElapsedSeconds = 0.0F;
    }
    if (!IsFiniteVectorUVE(out.startPosition)) {
        out.startPosition = Math::Vector3UVE{};
    }
    if (!Math::IsFiniteUVE(out.startRotation)) {
        out.startRotation = Math::QuaternionUVE{};
    }
    if (!IsFiniteVectorUVE(out.startScale)) {
        out.startScale = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    }
    return out;
}

} // namespace UVE::Scene
