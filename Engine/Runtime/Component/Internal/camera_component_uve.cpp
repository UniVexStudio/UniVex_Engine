// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/camera_component_uve.h"

#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsCameraComponentValidUVE(const CameraComponentUVE& camera) noexcept {
    return std::isfinite(camera.fieldOfViewDegrees) &&
           camera.fieldOfViewDegrees >= kMinimumCameraFieldOfViewDegreesUVE &&
           camera.fieldOfViewDegrees < 180.0F && std::isfinite(camera.nearPlane) && camera.nearPlane > 0.0F &&
           std::isfinite(camera.farPlane) && camera.farPlane > camera.nearPlane;
}

} // namespace UVE::Scene
