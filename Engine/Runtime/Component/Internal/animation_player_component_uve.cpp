// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/animation_player_component_uve.h"

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

bool IsAnimationPlayerComponentValidUVE(const AnimationPlayerComponentUVE& component) noexcept {
    const AnimationPlayerComponentUVE& c = component;
    return std::isfinite(c.speed) && c.loopMode <= AnimationLoopModeUVE::PingPong &&
           c.onFinish <= AnimationFinishActionUVE::ReturnToStart &&
           c.processCallback <= AnimationProcessCallbackUVE::Physics && IsNonNegativeUVE(c.startOffsetSeconds) &&
           IsNonNegativeUVE(c.blendInSeconds) && IsNonNegativeUVE(c.currentTimeSeconds) &&
           (c.direction == 1.0F || c.direction == -1.0F) && IsNonNegativeUVE(c.blendElapsedSeconds) &&
           IsFiniteVectorUVE(c.startPosition) && Math::IsFiniteUVE(c.startRotation) &&
           IsFiniteVectorUVE(c.startScale);
}

} // namespace UVE::Scene
