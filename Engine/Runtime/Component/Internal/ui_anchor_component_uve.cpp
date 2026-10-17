// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_anchor_component_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

bool IsUIAnchorComponentValidUVE(const UIAnchorComponentUVE& value) noexcept {
    return std::isfinite(value.anchorMin.x) && std::isfinite(value.anchorMin.y) &&
           std::isfinite(value.anchorMax.x) && std::isfinite(value.anchorMax.y) &&
           std::isfinite(value.offsetMin.x) && std::isfinite(value.offsetMin.y) &&
           std::isfinite(value.offsetMax.x) && std::isfinite(value.offsetMax.y);
}

UIAnchorComponentUVE SanitizeUIAnchorComponentUVE(const UIAnchorComponentUVE& value) noexcept {
    if (IsUIAnchorComponentValidUVE(value)) {
        return value;
    }
    return UIAnchorComponentUVE{
        .anchorMin = Math::IsFiniteUVE(value.anchorMin) ? value.anchorMin
                                                        : Math::Vector2UVE{0.0F, 0.0F},
        .anchorMax = Math::IsFiniteUVE(value.anchorMax) ? value.anchorMax
                                                        : Math::Vector2UVE{1.0F, 1.0F},
        .offsetMin = Math::IsFiniteUVE(value.offsetMin) ? value.offsetMin
                                                        : Math::Vector2UVE{0.0F, 0.0F},
        .offsetMax = Math::IsFiniteUVE(value.offsetMax) ? value.offsetMax
                                                        : Math::Vector2UVE{0.0F, 0.0F},
    };
}

bool IsUIAnchorStretchingUVE(const UIAnchorComponentUVE& value) noexcept {
    return value.anchorMin.x != value.anchorMax.x || value.anchorMin.y != value.anchorMax.y;
}

Math::RectUVE ResolveUIAnchoredRectUVE(const UIAnchorComponentUVE& value,
                                       const Math::RectUVE& parentRect) noexcept {
    if (!IsUIAnchorComponentValidUVE(value) || !Math::IsFiniteUVE(parentRect)) {
        return Math::RectUVE{};
    }
    const Math::Vector2UVE parentSize{
        std::max(0.0F, parentRect.size.x),
        std::max(0.0F, parentRect.size.y),
    };
    const Math::Vector2UVE resolvedMin{
        parentRect.position.x + value.anchorMin.x * parentSize.x + value.offsetMin.x,
        parentRect.position.y + value.anchorMin.y * parentSize.y + value.offsetMin.y,
    };
    const Math::Vector2UVE resolvedMax{
        parentRect.position.x + value.anchorMax.x * parentSize.x + value.offsetMax.x,
        parentRect.position.y + value.anchorMax.y * parentSize.y + value.offsetMax.y,
    };
    return Math::RectUVE{
        .position = resolvedMin,
        .size = Math::Vector2UVE{
            std::max(0.0F, resolvedMax.x - resolvedMin.x),
            std::max(0.0F, resolvedMax.y - resolvedMin.y),
        },
    };
}

Math::Vector2UVE ResolveUIAnchoredPositionUVE(const UIAnchorComponentUVE& value,
                                              const Math::RectUVE& parentRect) noexcept {
    return ResolveUIAnchoredRectUVE(value, parentRect).position;
}

} // namespace UVE::Scene
