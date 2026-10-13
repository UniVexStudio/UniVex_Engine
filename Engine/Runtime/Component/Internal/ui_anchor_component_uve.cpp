// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_anchor_component_uve.h"

#include <cmath>

namespace UVE::Scene {

bool IsUIAnchorComponentValidUVE(const UIAnchorComponentUVE& value) noexcept {
    return std::isfinite(value.anchorMin.x) && std::isfinite(value.anchorMin.y) &&
           std::isfinite(value.anchorMax.x) && std::isfinite(value.anchorMax.y) &&
           std::isfinite(value.offsetMin.x) && std::isfinite(value.offsetMin.y) &&
           std::isfinite(value.offsetMax.x) && std::isfinite(value.offsetMax.y);
}

} // namespace UVE::Scene
