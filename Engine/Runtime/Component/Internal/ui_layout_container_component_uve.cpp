// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_layout_container_component_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

bool IsUILayoutContainerComponentValidUVE(const UILayoutContainerComponentUVE& value) noexcept {
    const bool directionKnown = value.direction == UILayoutDirectionUVE::Vertical ||
                                value.direction == UILayoutDirectionUVE::Horizontal;
    const bool alignmentKnown = value.alignment == UILayoutAlignmentUVE::Start ||
                                value.alignment == UILayoutAlignmentUVE::Center ||
                                value.alignment == UILayoutAlignmentUVE::End;
    return directionKnown && alignmentKnown && std::isfinite(value.rect.position.x) &&
           std::isfinite(value.rect.position.y) && std::isfinite(value.rect.size.x) &&
           std::isfinite(value.rect.size.y) && value.rect.size.x >= 0.0F && value.rect.size.y >= 0.0F &&
           std::isfinite(value.padding) && value.padding >= 0.0F && std::isfinite(value.spacing) &&
           value.spacing >= 0.0F;
}

UILayoutContainerComponentUVE SanitizeUILayoutContainerComponentUVE(
    const UILayoutContainerComponentUVE& value) noexcept {
    UILayoutContainerComponentUVE out = value;
    if (out.direction != UILayoutDirectionUVE::Vertical &&
        out.direction != UILayoutDirectionUVE::Horizontal) {
        out.direction = UILayoutDirectionUVE::Vertical;
    }
    if (out.alignment != UILayoutAlignmentUVE::Start &&
        out.alignment != UILayoutAlignmentUVE::Center &&
        out.alignment != UILayoutAlignmentUVE::End) {
        out.alignment = UILayoutAlignmentUVE::Start;
    }
    if (!Math::IsFiniteUVE(out.rect)) {
        out.rect = Math::RectUVE{{0.0F, 0.0F}, {320.0F, 240.0F}};
    } else {
        out.rect.size.x = std::max(0.0F, out.rect.size.x);
        out.rect.size.y = std::max(0.0F, out.rect.size.y);
    }
    out.padding = (std::isfinite(out.padding) && out.padding >= 0.0F) ? out.padding : 0.0F;
    out.spacing = (std::isfinite(out.spacing) && out.spacing >= 0.0F) ? out.spacing : 0.0F;
    return out;
}

Math::RectUVE ResolveUILayoutContentRectUVE(
    const UILayoutContainerComponentUVE& value) noexcept {
    const UILayoutContainerComponentUVE safe = SanitizeUILayoutContainerComponentUVE(value);
    const Math::Vector2UVE pad{safe.padding, safe.padding};
    return Math::RectUVE{
        .position = safe.rect.position + pad,
        .size = Math::Vector2UVE{
            std::max(0.0F, safe.rect.size.x - safe.padding * 2.0F),
            std::max(0.0F, safe.rect.size.y - safe.padding * 2.0F),
        },
    };
}

std::uint32_t ComputeUILayoutLineCountUVE(const UILayoutContainerComponentUVE& value,
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
