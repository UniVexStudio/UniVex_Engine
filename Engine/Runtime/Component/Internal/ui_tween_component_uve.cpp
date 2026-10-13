// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_tween_component_uve.h"

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

} // namespace UVE::Scene
