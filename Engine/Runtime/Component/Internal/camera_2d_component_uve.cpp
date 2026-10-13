// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/camera_2d_component_uve.h"

namespace UVE::Scene {

bool IsCamera2DComponentValidUVE(const Camera2DComponentUVE& camera) noexcept {
    return Math::IsFiniteUVE(camera.zoom) && camera.zoom.x >= kMinimumCamera2DZoomUVE &&
           camera.zoom.y >= kMinimumCamera2DZoomUVE && Math::IsFiniteUVE(camera.offset);
}

} // namespace UVE::Scene
