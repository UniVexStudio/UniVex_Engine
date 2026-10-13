// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_image_component_uve.h"

#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUIImageComponentValidUVE(const UIImageComponentUVE& component) noexcept {
    return Math::IsFiniteUVE(component.rect) &&
           component.rect.size.x >= kMinimumUIImageSizePixelsUVE &&
           component.rect.size.x <= kMaximumUIImageSizePixelsUVE &&
           component.rect.size.y >= kMinimumUIImageSizePixelsUVE &&
           component.rect.size.y <= kMaximumUIImageSizePixelsUVE && std::isfinite(component.tintColor.x) &&
           std::isfinite(component.tintColor.y) && std::isfinite(component.tintColor.z) &&
           std::isfinite(component.alpha) && component.alpha >= 0.0F && component.alpha <= 1.0F &&
           std::isfinite(component.sliceMarginMin.x) && std::isfinite(component.sliceMarginMin.y) &&
           std::isfinite(component.sliceMarginMax.x) && std::isfinite(component.sliceMarginMax.y) &&
           component.sliceMarginMin.x >= 0.0F && component.sliceMarginMin.y >= 0.0F &&
           component.sliceMarginMax.x >= 0.0F && component.sliceMarginMax.y >= 0.0F &&
           std::isfinite(component.sliceUVMin.x) && std::isfinite(component.sliceUVMin.y) &&
           std::isfinite(component.sliceUVMax.x) && std::isfinite(component.sliceUVMax.y) &&
           component.sliceUVMin.x >= 0.0F && component.sliceUVMin.x <= 1.0F &&
           component.sliceUVMin.y >= 0.0F && component.sliceUVMin.y <= 1.0F &&
           component.sliceUVMax.x >= 0.0F && component.sliceUVMax.x <= 1.0F &&
           component.sliceUVMax.y >= 0.0F && component.sliceUVMax.y <= 1.0F;
}

} // namespace UVE::Scene
