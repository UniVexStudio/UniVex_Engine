// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/sprite_2d_component_uve.h"

#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsSprite2DComponentValidUVE(const Sprite2DComponentUVE& component) noexcept {
    return Math::IsFiniteUVE(component.sourceRect) &&
           component.sourceRect.position.x >= 0.0F &&
           component.sourceRect.position.x <= kMaximumSprite2DSizePixelsUVE &&
           component.sourceRect.position.y >= 0.0F &&
           component.sourceRect.position.y <= kMaximumSprite2DSizePixelsUVE &&
           component.sourceRect.size.x >= kMinimumSprite2DSizePixelsUVE &&
           component.sourceRect.size.x <= kMaximumSprite2DSizePixelsUVE &&
           component.sourceRect.size.y >= kMinimumSprite2DSizePixelsUVE &&
           component.sourceRect.size.y <= kMaximumSprite2DSizePixelsUVE &&
           Math::IsFiniteUVE(component.modulate) && std::isfinite(component.alpha) &&
           component.alpha >= 0.0F && component.alpha <= 1.0F &&
           Math::IsFiniteUVE(component.offset);
}

} // namespace UVE::Scene
