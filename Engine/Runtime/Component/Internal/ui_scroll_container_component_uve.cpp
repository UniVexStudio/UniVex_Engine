// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_scroll_container_component_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUIScrollContainerComponentValidUVE(const UIScrollContainerComponentUVE& component) noexcept {
    return Math::IsFiniteUVE(component.rect) && component.rect.size.x >= kMinimumUIScrollSizePixelsUVE &&
           component.rect.size.x <= kMaximumUIScrollSizePixelsUVE &&
           component.rect.size.y >= kMinimumUIScrollSizePixelsUVE &&
           component.rect.size.y <= kMaximumUIScrollSizePixelsUVE && std::isfinite(component.padding) &&
           component.padding >= 0.0F && std::isfinite(component.gap) && component.gap >= 0.0F &&
           std::isfinite(component.wheelStep) && component.wheelStep >= 0.0F &&
           std::isfinite(component.scrollOffset.x) && component.scrollOffset.x >= 0.0F &&
           std::isfinite(component.scrollOffset.y) && component.scrollOffset.y >= 0.0F &&
           std::isfinite(component.contentSize.x) && component.contentSize.x >= 0.0F &&
           std::isfinite(component.contentSize.y) && component.contentSize.y >= 0.0F;
}

Math::Vector2UVE ResolveUIScrollMaxOffsetUVE(
    const UIScrollContainerComponentUVE& component) noexcept {
    if (!Math::IsFiniteUVE(component.rect) || !Math::IsFiniteUVE(component.contentSize)) {
        return Math::Vector2UVE{0.0F, 0.0F};
    }
    return Math::Vector2UVE{
        std::max(0.0F, component.contentSize.x - std::max(0.0F, component.rect.size.x)),
        std::max(0.0F, component.contentSize.y - std::max(0.0F, component.rect.size.y)),
    };
}

Math::Vector2UVE ClampUIScrollOffsetUVE(const UIScrollContainerComponentUVE& component,
                                        const Math::Vector2UVE& desiredScrollOffset) noexcept {
    const Math::Vector2UVE maxOffset = ResolveUIScrollMaxOffsetUVE(component);
    const float sx = std::isfinite(desiredScrollOffset.x)
                         ? std::clamp(desiredScrollOffset.x, 0.0F, maxOffset.x)
                         : 0.0F;
    const float sy = std::isfinite(desiredScrollOffset.y)
                         ? std::clamp(desiredScrollOffset.y, 0.0F, maxOffset.y)
                         : 0.0F;
    return Math::Vector2UVE{sx, sy};
}

bool HasUIScrollVerticalOverflowUVE(const UIScrollContainerComponentUVE& component) noexcept {
    return Math::IsFiniteUVE(component.rect) && Math::IsFiniteUVE(component.contentSize) &&
           component.contentSize.y > component.rect.size.y;
}

Math::Vector2UVE ApplyUIScrollWheelDeltaUVE(UIScrollContainerComponentUVE& component,
                                            const float wheelDeltaUnits,
                                            const bool horizontalWheel) noexcept {
    if (!std::isfinite(wheelDeltaUnits) || !std::isfinite(component.wheelStep) ||
        component.wheelStep <= 0.0F) {
        component.scrollOffset = ClampUIScrollOffsetUVE(component, component.scrollOffset);
        return component.scrollOffset;
    }
    const float deltaPixels = -wheelDeltaUnits * component.wheelStep;
    const Math::Vector2UVE desired{
        horizontalWheel ? (component.scrollOffset.x + deltaPixels) : component.scrollOffset.x,
        horizontalWheel ? component.scrollOffset.y : (component.scrollOffset.y + deltaPixels),
    };
    component.scrollOffset = ClampUIScrollOffsetUVE(component, desired);
    return component.scrollOffset;
}

} // namespace UVE::Scene
