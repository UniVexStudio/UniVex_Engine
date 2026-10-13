// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_scrollbar_component_uve.h"

#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUIScrollbarComponentValidUVE(const UIScrollbarComponentUVE& component) noexcept {
    return Math::IsFiniteUVE(component.rect) &&
           component.rect.size.x >= kMinimumUIScrollbarSizePixelsUVE &&
           component.rect.size.x <= kMaximumUIScrollbarSizePixelsUVE &&
           component.rect.size.y >= kMinimumUIScrollbarSizePixelsUVE &&
           component.rect.size.y <= kMaximumUIScrollbarSizePixelsUVE &&
           std::isfinite(component.minThumbHeight) && component.minThumbHeight >= 0.0F &&
           std::isfinite(component.trackColor.x) && std::isfinite(component.trackColor.y) &&
           std::isfinite(component.trackColor.z) && std::isfinite(component.thumbColor.x) &&
           std::isfinite(component.thumbColor.y) && std::isfinite(component.thumbColor.z);
}

} // namespace UVE::Scene
