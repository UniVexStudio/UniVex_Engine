// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/render_instance_component_uve.h"

#include <cmath>

namespace UVE::Scene {

bool IsRenderInstanceComponentValidUVE(const RenderInstanceComponentUVE& component) noexcept {
    return std::isfinite(component.sortingOffset);
}

} // namespace UVE::Scene
