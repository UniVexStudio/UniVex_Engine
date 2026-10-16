// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_image_component_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUIImageComponentValidUVE(const UIImageComponentUVE& component) noexcept {
    return Math::IsFiniteUVE(component.rect) &&
           component.rect.size.x >= kMinimumUIImageSizePixelsUVE &&
           component.rect.size.x <= kMaximumUIImageSizePixelsUVE &&
           component.rect.size.y >= kMinimumUIImageSizePixelsUVE &&
           component.rect.size.y <= kMaximumUIImageSizePixelsUVE && std::isfinite(component.tintColor.x) &&
           std::isfinite(component.tintColor.y) && std::isfinite(component.tintColor.z) &&
           std::isfinite(component.alpha) && component.alpha >= 0.0F && component.alpha <= 1.0F &&
           std::isfinite(component.sliceMarginMin.x) && std::isfinite(component.sliceMarginMin.y) &&
           std::isfinite(component.sliceMarginMax.x) && std::isfinite(component.sliceMarginMax.y) &&
           component.sliceMarginMin.x >= 0.0F && component.sliceMarginMin.y >= 0.0F &&
           component.sliceMarginMax.x >= 0.0F && component.sliceMarginMax.y >= 0.0F &&
           std::isfinite(component.sliceUVMin.x) && std::isfinite(component.sliceUVMin.y) &&
           std::isfinite(component.sliceUVMax.x) && std::isfinite(component.sliceUVMax.y) &&
           component.sliceUVMin.x >= 0.0F && component.sliceUVMin.x <= 1.0F &&
           component.sliceUVMin.y >= 0.0F && component.sliceUVMin.y <= 1.0F &&
           component.sliceUVMax.x >= 0.0F && component.sliceUVMax.x <= 1.0F &&
           component.sliceUVMax.y >= 0.0F && component.sliceUVMax.y <= 1.0F;
}

UIImageComponentUVE SanitizeUIImageComponentUVE(const UIImageComponentUVE& component) noexcept {
    UIImageComponentUVE out = component;
    if (!Math::IsFiniteUVE(out.rect)) {
        out.rect = Math::RectUVE{{0.0F, 0.0F}, {64.0F, 64.0F}};
    } else {
        out.rect.size.x =
            std::clamp(out.rect.size.x, kMinimumUIImageSizePixelsUVE, kMaximumUIImageSizePixelsUVE);
        out.rect.size.y =
            std::clamp(out.rect.size.y, kMinimumUIImageSizePixelsUVE, kMaximumUIImageSizePixelsUVE);
    }
    if (!Math::IsFiniteUVE(out.tintColor)) {
        out.tintColor = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    }
    out.alpha = std::isfinite(out.alpha) ? std::clamp(out.alpha, 0.0F, 1.0F) : 1.0F;
    out.sliceMarginMin = Math::Vector2UVE{
        (std::isfinite(out.sliceMarginMin.x) && out.sliceMarginMin.x >= 0.0F)
            ? out.sliceMarginMin.x
            : 0.0F,
        (std::isfinite(out.sliceMarginMin.y) && out.sliceMarginMin.y >= 0.0F)
            ? out.sliceMarginMin.y
            : 0.0F,
    };
    out.sliceMarginMax = Math::Vector2UVE{
        (std::isfinite(out.sliceMarginMax.x) && out.sliceMarginMax.x >= 0.0F)
            ? out.sliceMarginMax.x
            : 0.0F,
        (std::isfinite(out.sliceMarginMax.y) && out.sliceMarginMax.y >= 0.0F)
            ? out.sliceMarginMax.y
            : 0.0F,
    };
    out.sliceUVMin = Math::Vector2UVE{
        std::isfinite(out.sliceUVMin.x) ? std::clamp(out.sliceUVMin.x, 0.0F, 1.0F) : 0.0F,
        std::isfinite(out.sliceUVMin.y) ? std::clamp(out.sliceUVMin.y, 0.0F, 1.0F) : 0.0F,
    };
    out.sliceUVMax = Math::Vector2UVE{
        std::isfinite(out.sliceUVMax.x) ? std::clamp(out.sliceUVMax.x, 0.0F, 1.0F) : 0.0F,
        std::isfinite(out.sliceUVMax.y) ? std::clamp(out.sliceUVMax.y, 0.0F, 1.0F) : 0.0F,
    };
    return out;
}

bool HasAssignedUIImageTextureUVE(const UIImageComponentUVE& component) noexcept {
    return component.textureAssetGuid != Asset::kInvalidAssetGuidUVE;
}

bool UsesUIImageNineSliceSplittingUVE(const UIImageComponentUVE& component) noexcept {
    return component.nineSliceEnabled && HasAssignedUIImageTextureUVE(component) &&
           IsUIImageComponentValidUVE(component);
}

Math::RectUVE ResolveUIImageNineSliceCenterRectUVE(
    const UIImageComponentUVE& component) noexcept {
    const UIImageComponentUVE safe = SanitizeUIImageComponentUVE(component);
    if (!safe.nineSliceEnabled) {
        return safe.rect;
    }
    float left = safe.sliceMarginMin.x;
    float right = safe.sliceMarginMax.x;
    const float sumX = left + right;
    if (sumX > safe.rect.size.x && sumX > 0.0F) {
        const float scaleX = safe.rect.size.x / sumX;
        left *= scaleX;
        right *= scaleX;
    }

    float top = safe.sliceMarginMin.y;
    float bottom = safe.sliceMarginMax.y;
    const float sumY = top + bottom;
    if (sumY > safe.rect.size.y && sumY > 0.0F) {
        const float scaleY = safe.rect.size.y / sumY;
        top *= scaleY;
        bottom *= scaleY;
    }

    return Math::RectUVE{
        .position = Math::Vector2UVE{safe.rect.position.x + left, safe.rect.position.y + top},
        .size = Math::Vector2UVE{
            std::max(0.0F, safe.rect.size.x - left - right),
            std::max(0.0F, safe.rect.size.y - top - bottom),
        },
    };
}

} // namespace UVE::Scene
