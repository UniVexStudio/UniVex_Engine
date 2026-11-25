// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_layout_container_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

bool IsUILayoutContainerComponentValid(const UILayoutContainerComponent& value) noexcept {
    const bool directionKnown = value.direction == UILayoutDirection::Vertical ||
                                value.direction == UILayoutDirection::Horizontal;
    const bool alignmentKnown = value.alignment == UILayoutAlignment::Start ||
                                value.alignment == UILayoutAlignment::Center ||
                                value.alignment == UILayoutAlignment::End;
    return directionKnown && alignmentKnown && std::isfinite(value.rect.position.x) &&
           std::isfinite(value.rect.position.y) && std::isfinite(value.rect.size.x) &&
           std::isfinite(value.rect.size.y) && value.rect.size.x >= 0.0F && value.rect.size.y >= 0.0F &&
           std::isfinite(value.padding) && value.padding >= 0.0F && std::isfinite(value.spacing) &&
           value.spacing >= 0.0F;
}

UILayoutContainerComponent SanitizeUILayoutContainerComponent(
    const UILayoutContainerComponent& value) noexcept {
    UILayoutContainerComponent out = value;
    if (out.direction != UILayoutDirection::Vertical &&
        out.direction != UILayoutDirection::Horizontal) {
        out.direction = UILayoutDirection::Vertical;
    }
    if (out.alignment != UILayoutAlignment::Start &&
        out.alignment != UILayoutAlignment::Center &&
        out.alignment != UILayoutAlignment::End) {
        out.alignment = UILayoutAlignment::Start;
    }
    if (!Math::IsFinite(out.rect)) {
        out.rect = Math::Rect{{0.0F, 0.0F}, {320.0F, 240.0F}};
    } else {
        out.rect.size.x = std::max(0.0F, out.rect.size.x);
        out.rect.size.y = std::max(0.0F, out.rect.size.y);
    }
    out.padding = (std::isfinite(out.padding) && out.padding >= 0.0F) ? out.padding : 0.0F;
    out.spacing = (std::isfinite(out.spacing) && out.spacing >= 0.0F) ? out.spacing : 0.0F;
    return out;
}

Math::Rect ResolveUILayoutContentRect(
    const UILayoutContainerComponent& value) noexcept {
    const UILayoutContainerComponent safe = SanitizeUILayoutContainerComponent(value);
    const Math::Vector2 pad{safe.padding, safe.padding};
    return Math::Rect{
        .position = safe.rect.position + pad,
        .size = Math::Vector2{
            std::max(0.0F, safe.rect.size.x - safe.padding * 2.0F),
            std::max(0.0F, safe.rect.size.y - safe.padding * 2.0F),
        },
    };
}

std::uint32_t ComputeUILayoutLineCount(const UILayoutContainerComponent& value,
                                          const std::uint32_t childCount) noexcept {
    if (childCount == 0U) {
        return 0U;
    }
    if (value.wrapAfter == 0U) {
        return 1U;
    }
    return (childCount + value.wrapAfter - 1U) / value.wrapAfter;
}

} // namespace UVE::Scene
