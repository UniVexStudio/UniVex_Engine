// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_tween_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

bool IsUITweenComponentValid(const UITweenComponent& value) noexcept {
    const bool knownTarget = value.target == UITweenTarget::Rect || value.target == UITweenTarget::Alpha;
    const bool knownLoop = value.loop == UITweenLoop::Once || value.loop == UITweenLoop::Loop ||
                           value.loop == UITweenLoop::PingPong;
    const auto ease = static_cast<std::uint8_t>(value.ease);
    const bool knownEase = ease <= static_cast<std::uint8_t>(UITweenEase::OutBack);
    return Math::IsFinite(value.fromRect) && Math::IsFinite(value.toRect) &&
           std::isfinite(value.fromAlpha) && std::isfinite(value.toAlpha) && std::isfinite(value.duration) &&
           value.duration > 0.0F && std::isfinite(value.delay) && value.delay >= 0.0F &&
           std::isfinite(value.elapsed) && value.elapsed >= 0.0F && std::isfinite(value.currentAlpha) && knownTarget &&
           knownLoop && knownEase;
}

bool IsUITweenInDelayPhase(const UITweenComponent& value) noexcept {
    return IsUITweenComponentValid(value) && value.elapsed < value.delay;
}

float ResolveUITweenNormalizedCycleProgress(const UITweenComponent& value) noexcept {
    if (!IsUITweenComponentValid(value) || value.elapsed <= value.delay) {
        return 0.0F;
    }
    const float activeTime = value.elapsed - value.delay;
    switch (value.loop) {
    case UITweenLoop::Once:
        return std::clamp(activeTime / value.duration, 0.0F, 1.0F);
    case UITweenLoop::Loop:
        return std::clamp(std::fmod(activeTime, value.duration) / value.duration, 0.0F, 1.0F);
    case UITweenLoop::PingPong: {
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

void ResetUITweenRuntimeState(UITweenComponent& value, const bool startPlaying) noexcept {
    value.elapsed = 0.0F;
    value.playing = startPlaying;
    value.completedThisFrame = false;
    value.currentAlpha =
        (value.target == UITweenTarget::Alpha && std::isfinite(value.fromAlpha))
            ? std::clamp(value.fromAlpha, 0.0F, 1.0F)
            : 1.0F;
}

} // namespace UVE::Scene
