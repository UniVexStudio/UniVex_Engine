// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_scroll_container_component_uve.h"

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

} // namespace UVE::Scene
