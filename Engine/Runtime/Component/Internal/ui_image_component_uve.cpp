// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_image_component_uve.h"

#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUIImageComponentValidUVE(const UIImageComponentUVE& component) noexcept {
    return std::isfinite(component.positionPixels.x) && std::isfinite(component.positionPixels.y) &&
           std::isfinite(component.sizePixels.x) && std::isfinite(component.sizePixels.y) &&
           component.sizePixels.x >= kMinimumUIImageSizePixelsUVE &&
           component.sizePixels.x <= kMaximumUIImageSizePixelsUVE &&
           component.sizePixels.y >= kMinimumUIImageSizePixelsUVE &&
           component.sizePixels.y <= kMaximumUIImageSizePixelsUVE && std::isfinite(component.tintColor.x) &&
           std::isfinite(component.tintColor.y) && std::isfinite(component.tintColor.z) &&
           std::isfinite(component.alpha) && component.alpha >= 0.0F && component.alpha <= 1.0F;
}

} // namespace UVE::Scene
