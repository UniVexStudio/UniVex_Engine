// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_anchor_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

bool IsUIAnchorComponentValid(const UIAnchorComponent& value) noexcept {
    return std::isfinite(value.anchorMin.x) && std::isfinite(value.anchorMin.y) &&
           std::isfinite(value.anchorMax.x) && std::isfinite(value.anchorMax.y) &&
           std::isfinite(value.offsetMin.x) && std::isfinite(value.offsetMin.y) &&
           std::isfinite(value.offsetMax.x) && std::isfinite(value.offsetMax.y);
}

UIAnchorComponent SanitizeUIAnchorComponent(const UIAnchorComponent& value) noexcept {
    if (IsUIAnchorComponentValid(value)) {
        return value;
    }
    return UIAnchorComponent{
        .anchorMin = Math::IsFinite(value.anchorMin) ? value.anchorMin
                                                        : Math::Vector2{0.0F, 0.0F},
        .anchorMax = Math::IsFinite(value.anchorMax) ? value.anchorMax
                                                        : Math::Vector2{1.0F, 1.0F},
        .offsetMin = Math::IsFinite(value.offsetMin) ? value.offsetMin
                                                        : Math::Vector2{0.0F, 0.0F},
        .offsetMax = Math::IsFinite(value.offsetMax) ? value.offsetMax
                                                        : Math::Vector2{0.0F, 0.0F},
    };
}

bool IsUIAnchorStretching(const UIAnchorComponent& value) noexcept {
    return value.anchorMin.x != value.anchorMax.x || value.anchorMin.y != value.anchorMax.y;
}

Math::Rect ResolveUIAnchoredRect(const UIAnchorComponent& value,
                                       const Math::Rect& parentRect) noexcept {
    if (!IsUIAnchorComponentValid(value) || !Math::IsFinite(parentRect)) {
        return Math::Rect{};
    }
    const Math::Vector2 parentSize{
        std::max(0.0F, parentRect.size.x),
        std::max(0.0F, parentRect.size.y),
    };
    const Math::Vector2 resolvedMin{
        parentRect.position.x + value.anchorMin.x * parentSize.x + value.offsetMin.x,
        parentRect.position.y + value.anchorMin.y * parentSize.y + value.offsetMin.y,
    };
    const Math::Vector2 resolvedMax{
        parentRect.position.x + value.anchorMax.x * parentSize.x + value.offsetMax.x,
        parentRect.position.y + value.anchorMax.y * parentSize.y + value.offsetMax.y,
    };
    return Math::Rect{
        .position = resolvedMin,
        .size = Math::Vector2{
            std::max(0.0F, resolvedMax.x - resolvedMin.x),
            std::max(0.0F, resolvedMax.y - resolvedMin.y),
        },
    };
}

Math::Vector2 ResolveUIAnchoredPosition(const UIAnchorComponent& value,
                                              const Math::Rect& parentRect) noexcept {
    return ResolveUIAnchoredRect(value, parentRect).position;
}

} // namespace UVE::Scene
