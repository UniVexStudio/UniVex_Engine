// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_image_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUIImageComponentValid(const UIImageComponent& component) noexcept {
    return Math::IsFinite(component.rect) &&
           component.rect.size.x >= kMinimumUIImageSizePixels &&
           component.rect.size.x <= kMaximumUIImageSizePixels &&
           component.rect.size.y >= kMinimumUIImageSizePixels &&
           component.rect.size.y <= kMaximumUIImageSizePixels && std::isfinite(component.tintColor.x) &&
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

UIImageComponent SanitizeUIImageComponent(const UIImageComponent& component) noexcept {
    UIImageComponent out = component;
    if (!Math::IsFinite(out.rect)) {
        out.rect = Math::Rect{{0.0F, 0.0F}, {64.0F, 64.0F}};
    } else {
        out.rect.size.x =
            std::clamp(out.rect.size.x, kMinimumUIImageSizePixels, kMaximumUIImageSizePixels);
        out.rect.size.y =
            std::clamp(out.rect.size.y, kMinimumUIImageSizePixels, kMaximumUIImageSizePixels);
    }
    if (!Math::IsFinite(out.tintColor)) {
        out.tintColor = Math::Vector3{1.0F, 1.0F, 1.0F};
    }
    out.alpha = std::isfinite(out.alpha) ? std::clamp(out.alpha, 0.0F, 1.0F) : 1.0F;
    out.sliceMarginMin = Math::Vector2{
        (std::isfinite(out.sliceMarginMin.x) && out.sliceMarginMin.x >= 0.0F)
            ? out.sliceMarginMin.x
            : 0.0F,
        (std::isfinite(out.sliceMarginMin.y) && out.sliceMarginMin.y >= 0.0F)
            ? out.sliceMarginMin.y
            : 0.0F,
    };
    out.sliceMarginMax = Math::Vector2{
        (std::isfinite(out.sliceMarginMax.x) && out.sliceMarginMax.x >= 0.0F)
            ? out.sliceMarginMax.x
            : 0.0F,
        (std::isfinite(out.sliceMarginMax.y) && out.sliceMarginMax.y >= 0.0F)
            ? out.sliceMarginMax.y
            : 0.0F,
    };
    out.sliceUVMin = Math::Vector2{
        std::isfinite(out.sliceUVMin.x) ? std::clamp(out.sliceUVMin.x, 0.0F, 1.0F) : 0.0F,
        std::isfinite(out.sliceUVMin.y) ? std::clamp(out.sliceUVMin.y, 0.0F, 1.0F) : 0.0F,
    };
    out.sliceUVMax = Math::Vector2{
        std::isfinite(out.sliceUVMax.x) ? std::clamp(out.sliceUVMax.x, 0.0F, 1.0F) : 0.0F,
        std::isfinite(out.sliceUVMax.y) ? std::clamp(out.sliceUVMax.y, 0.0F, 1.0F) : 0.0F,
    };
    return out;
}

bool HasAssignedUIImageTexture(const UIImageComponent& component) noexcept {
    return component.textureAssetGuid != Asset::kInvalidAssetGuid;
}

bool UsesUIImageNineSliceSplitting(const UIImageComponent& component) noexcept {
    return component.nineSliceEnabled && HasAssignedUIImageTexture(component) &&
           IsUIImageComponentValid(component);
}

Math::Rect ResolveUIImageNineSliceCenterRect(
    const UIImageComponent& component) noexcept {
    const UIImageComponent safe = SanitizeUIImageComponent(component);
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

    return Math::Rect{
        .position = Math::Vector2{safe.rect.position.x + left, safe.rect.position.y + top},
        .size = Math::Vector2{
            std::max(0.0F, safe.rect.size.x - left - right),
            std::max(0.0F, safe.rect.size.y - top - bottom),
        },
    };
}

} // namespace UVE::Scene
