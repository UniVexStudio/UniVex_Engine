// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_tween_component_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

bool IsUITweenComponentValidUVE(const UITweenComponentUVE& value) noexcept {
    const bool knownTarget = value.target == UITweenTargetUVE::Rect || value.target == UITweenTargetUVE::Alpha;
    const bool knownLoop = value.loop == UITweenLoopUVE::Once || value.loop == UITweenLoopUVE::Loop ||
                           value.loop == UITweenLoopUVE::PingPong;
    const auto ease = static_cast<std::uint8_t>(value.ease);
    const bool knownEase = ease <= static_cast<std::uint8_t>(UITweenEaseUVE::OutBack);
    return Math::IsFiniteUVE(value.fromRect) && Math::IsFiniteUVE(value.toRect) &&
           std::isfinite(value.fromAlpha) && std::isfinite(value.toAlpha) && std::isfinite(value.duration) &&
           value.duration > 0.0F && std::isfinite(value.delay) && value.delay >= 0.0F &&
           std::isfinite(value.elapsed) && value.elapsed >= 0.0F && std::isfinite(value.currentAlpha) && knownTarget &&
           knownLoop && knownEase;
}

bool IsUITweenInDelayPhaseUVE(const UITweenComponentUVE& value) noexcept {
    return IsUITweenComponentValidUVE(value) && value.elapsed < value.delay;
}

float ResolveUITweenNormalizedCycleProgressUVE(const UITweenComponentUVE& value) noexcept {
    if (!IsUITweenComponentValidUVE(value) || value.elapsed <= value.delay) {
        return 0.0F;
    }
    const float activeTime = value.elapsed - value.delay;
    switch (value.loop) {
    case UITweenLoopUVE::Once:
        return std::clamp(activeTime / value.duration, 0.0F, 1.0F);
    case UITweenLoopUVE::Loop:
        return std::clamp(std::fmod(activeTime, value.duration) / value.duration, 0.0F, 1.0F);
    case UITweenLoopUVE::PingPong: {
        const float period = value.duration * 2.0F;
        const float wrapped = std::fmod(activeTime, period);
        const float t = (wrapped <= value.duration)
                            ? (wrapped / value.duration)
                            : (2.0F - wrapped / value.duration);
        return std::clamp(t, 0.0F, 1.0F);
    }
    }
    return 0.0F;
}

void ResetUITweenRuntimeStateUVE(UITweenComponentUVE& value, const bool startPlaying) noexcept {
    value.elapsed = 0.0F;
    value.playing = startPlaying;
    value.completedThisFrame = false;
    value.currentAlpha =
        (value.target == UITweenTargetUVE::Alpha && std::isfinite(value.fromAlpha))
            ? std::clamp(value.fromAlpha, 0.0F, 1.0F)
            : 1.0F;
}

} // namespace UVE::Scene
