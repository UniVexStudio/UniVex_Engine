// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_scrollbar_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUIScrollbarComponentValid(const UIScrollbarComponent& component) noexcept {
    return Math::IsFinite(component.rect) &&
           component.rect.size.x >= kMinimumUIScrollbarSizePixels &&
           component.rect.size.x <= kMaximumUIScrollbarSizePixels &&
           component.rect.size.y >= kMinimumUIScrollbarSizePixels &&
           component.rect.size.y <= kMaximumUIScrollbarSizePixels &&
           std::isfinite(component.minThumbHeight) && component.minThumbHeight >= 0.0F &&
           std::isfinite(component.trackColor.x) && std::isfinite(component.trackColor.y) &&
           std::isfinite(component.trackColor.z) && std::isfinite(component.thumbColor.x) &&
           std::isfinite(component.thumbColor.y) && std::isfinite(component.thumbColor.z);
}

float ResolveUIScrollbarThumbHeight(const UIScrollbarComponent& component,
                                       const float viewportHeight,
                                       const float contentHeight) noexcept {
    if (!IsUIScrollbarComponentValid(component)) {
        return 0.0F;
    }
    const float trackHeight = component.rect.size.y;
    if (!std::isfinite(viewportHeight) || !std::isfinite(contentHeight) ||
        viewportHeight <= 0.0F || contentHeight <= viewportHeight) {
        return trackHeight;
    }
    const float rawHeight = trackHeight * (viewportHeight / contentHeight);
    const float minThumb = std::min(trackHeight, std::max(0.0F, component.minThumbHeight));
    return std::clamp(rawHeight, minThumb, trackHeight);
}

Math::Rect ResolveUIScrollbarThumbRect(const UIScrollbarComponent& component,
                                             const float scrollOffsetY,
                                             const float viewportHeight,
                                             const float contentHeight) noexcept {
    if (!IsUIScrollbarComponentValid(component)) {
        return Math::Rect{};
    }
    const float thumbHeight =
        ResolveUIScrollbarThumbHeight(component, viewportHeight, contentHeight);
    const float maxScrollY =
        (std::isfinite(contentHeight) && std::isfinite(viewportHeight))
            ? std::max(0.0F, contentHeight - viewportHeight)
            : 0.0F;
    const float travel = std::max(0.0F, component.rect.size.y - thumbHeight);
    const float ratio =
        (maxScrollY > 1e-5F && std::isfinite(scrollOffsetY))
            ? std::clamp(scrollOffsetY / maxScrollY, 0.0F, 1.0F)
            : 0.0F;
    return Math::Rect{
        .position = Math::Vector2{
            component.rect.position.x,
            component.rect.position.y + travel * ratio,
        },
        .size = Math::Vector2{component.rect.size.x, thumbHeight},
    };
}

float ResolveUIScrollbarOffsetFromPointerY(const UIScrollbarComponent& component,
                                              const float pointerYPixels,
                                              const float viewportHeight,
                                              const float contentHeight) noexcept {
    if (!IsUIScrollbarComponentValid(component) || !std::isfinite(pointerYPixels) ||
        !std::isfinite(viewportHeight) || !std::isfinite(contentHeight) ||
        contentHeight <= viewportHeight) {
        return 0.0F;
    }
    const float maxScrollY = contentHeight - viewportHeight;
    const float thumbHeight =
        ResolveUIScrollbarThumbHeight(component, viewportHeight, contentHeight);
    const float travel = component.rect.size.y - thumbHeight;
    if (travel <= 1e-5F) {
        return 0.0F;
    }
    const float ratio = std::clamp(
        ((pointerYPixels - component.rect.position.y) - thumbHeight * 0.5F) / travel, 0.0F, 1.0F);
    return ratio * maxScrollY;
}

void ResetUIScrollbarRuntimeState(UIScrollbarComponent& component) noexcept {
    component.isHovered = false;
    component.isDragging = false;
    component.wasChangedThisFrame = false;
}

} // namespace UVE::Scene
