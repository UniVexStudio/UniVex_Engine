// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_scroll_container_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUIScrollContainerComponentValid(const UIScrollContainerComponent& component) noexcept {
    return Math::IsFinite(component.rect) && component.rect.size.x >= kMinimumUIScrollSizePixels &&
           component.rect.size.x <= kMaximumUIScrollSizePixels &&
           component.rect.size.y >= kMinimumUIScrollSizePixels &&
           component.rect.size.y <= kMaximumUIScrollSizePixels && std::isfinite(component.padding) &&
           component.padding >= 0.0F && std::isfinite(component.gap) && component.gap >= 0.0F &&
           std::isfinite(component.wheelStep) && component.wheelStep >= 0.0F &&
           std::isfinite(component.scrollOffset.x) && component.scrollOffset.x >= 0.0F &&
           std::isfinite(component.scrollOffset.y) && component.scrollOffset.y >= 0.0F &&
           std::isfinite(component.contentSize.x) && component.contentSize.x >= 0.0F &&
           std::isfinite(component.contentSize.y) && component.contentSize.y >= 0.0F;
}

Math::Vector2 ResolveUIScrollMaxOffset(
    const UIScrollContainerComponent& component) noexcept {
    if (!Math::IsFinite(component.rect) || !Math::IsFinite(component.contentSize)) {
        return Math::Vector2{0.0F, 0.0F};
    }
    return Math::Vector2{
        std::max(0.0F, component.contentSize.x - std::max(0.0F, component.rect.size.x)),
        std::max(0.0F, component.contentSize.y - std::max(0.0F, component.rect.size.y)),
    };
}

Math::Vector2 ClampUIScrollOffset(const UIScrollContainerComponent& component,
                                        const Math::Vector2& desiredScrollOffset) noexcept {
    const Math::Vector2 maxOffset = ResolveUIScrollMaxOffset(component);
    const float sx = std::isfinite(desiredScrollOffset.x)
                         ? std::clamp(desiredScrollOffset.x, 0.0F, maxOffset.x)
                         : 0.0F;
    const float sy = std::isfinite(desiredScrollOffset.y)
                         ? std::clamp(desiredScrollOffset.y, 0.0F, maxOffset.y)
                         : 0.0F;
    return Math::Vector2{sx, sy};
}

bool HasUIScrollVerticalOverflow(const UIScrollContainerComponent& component) noexcept {
    return Math::IsFinite(component.rect) && Math::IsFinite(component.contentSize) &&
           component.contentSize.y > component.rect.size.y;
}

Math::Vector2 ApplyUIScrollWheelDelta(UIScrollContainerComponent& component,
                                            const float wheelDeltaUnits,
                                            const bool horizontalWheel) noexcept {
    if (!std::isfinite(wheelDeltaUnits) || !std::isfinite(component.wheelStep) ||
        component.wheelStep <= 0.0F) {
        component.scrollOffset = ClampUIScrollOffset(component, component.scrollOffset);
        return component.scrollOffset;
    }
    const float deltaPixels = -wheelDeltaUnits * component.wheelStep;
    const Math::Vector2 desired{
        horizontalWheel ? (component.scrollOffset.x + deltaPixels) : component.scrollOffset.x,
        horizontalWheel ? component.scrollOffset.y : (component.scrollOffset.y + deltaPixels),
    };
    component.scrollOffset = ClampUIScrollOffset(component, desired);
    return component.scrollOffset;
}

} // namespace UVE::Scene
