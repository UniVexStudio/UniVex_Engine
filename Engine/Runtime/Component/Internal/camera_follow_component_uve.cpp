// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/camera_follow_component_uve.h"

#include <cmath>

namespace UVE::Scene {

bool IsCameraFollowComponentValidUVE(const CameraFollowComponentUVE& value) noexcept {
    return std::isfinite(value.offset.x) && std::isfinite(value.offset.y) && std::isfinite(value.offset.z);
}

} // namespace UVE::Scene
