// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_text_component_uve.h"

#include <cmath>
#include <cstddef>
#include <string>

namespace UVE::Scene {

[[nodiscard]] bool IsUITextComponentValidUVE(const UITextComponentUVE& component) noexcept {
    return component.text.size() <= kMaximumUITextBytesUVE && component.text.find('\0') == std::string::npos &&
           std::isfinite(component.positionPixels.x) && std::isfinite(component.positionPixels.y) &&
           std::isfinite(component.fontSize) && component.fontSize >= kMinimumUIFontSizeUVE &&
           component.fontSize <= kMaximumUIFontSizeUVE && std::isfinite(component.color.x) &&
           std::isfinite(component.color.y) && std::isfinite(component.color.z) &&
           std::isfinite(component.alpha) && component.alpha >= 0.0F && component.alpha <= 1.0F;
}

} // namespace UVE::Scene
