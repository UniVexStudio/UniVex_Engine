// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_layout_container_component_uve.h"

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

} // namespace UVE::Scene
