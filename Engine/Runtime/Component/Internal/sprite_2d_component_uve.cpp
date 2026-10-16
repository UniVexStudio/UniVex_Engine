// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/sprite_2d_component_uve.h"

#include <algorithm>
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

Sprite2DComponentUVE SanitizeSprite2DComponentUVE(const Sprite2DComponentUVE& component) noexcept {
    Sprite2DComponentUVE out = component;
    if (!Math::IsFiniteUVE(out.sourceRect)) {
        out.sourceRect = Math::RectUVE{};
    } else {
        out.sourceRect.position.x =
            std::clamp(out.sourceRect.position.x, 0.0F, kMaximumSprite2DSizePixelsUVE);
        out.sourceRect.position.y =
            std::clamp(out.sourceRect.position.y, 0.0F, kMaximumSprite2DSizePixelsUVE);
        out.sourceRect.size.x = std::clamp(out.sourceRect.size.x, kMinimumSprite2DSizePixelsUVE,
                                           kMaximumSprite2DSizePixelsUVE);
        out.sourceRect.size.y = std::clamp(out.sourceRect.size.y, kMinimumSprite2DSizePixelsUVE,
                                           kMaximumSprite2DSizePixelsUVE);
    }
    if (!Math::IsFiniteUVE(out.modulate)) {
        out.modulate = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    }
    out.alpha = std::isfinite(out.alpha) ? std::clamp(out.alpha, 0.0F, 1.0F) : 1.0F;
    if (!Math::IsFiniteUVE(out.offset)) {
        out.offset = Math::Vector2UVE{0.0F, 0.0F};
    }
    return out;
}

bool HasAssignedSprite2DTextureUVE(const Sprite2DComponentUVE& component) noexcept {
    return component.textureAssetGuid != Asset::kInvalidAssetGuidUVE;
}

bool HasCustomSprite2DSourceRegionUVE(const Sprite2DComponentUVE& component) noexcept {
    return IsSprite2DComponentValidUVE(component) && component.sourceRect.size.x > 0.0F &&
           component.sourceRect.size.y > 0.0F;
}

Math::RectUVE ResolveSprite2DLocalQuadRectUVE(
    const Sprite2DComponentUVE& component, const Math::Vector2UVE& fallbackSizePixels) noexcept {
    const Sprite2DComponentUVE safe = SanitizeSprite2DComponentUVE(component);
    const Math::Vector2UVE size =
        HasCustomSprite2DSourceRegionUVE(safe)
            ? safe.sourceRect.size
            : (Math::IsFiniteUVE(fallbackSizePixels) && fallbackSizePixels.x > 0.0F &&
                       fallbackSizePixels.y > 0.0F
                   ? fallbackSizePixels
                   : Math::Vector2UVE{0.0F, 0.0F});
    return Math::RectUVE{
        .position = safe.offset - size * 0.5F,
        .size = size,
    };
}

} // namespace UVE::Scene
