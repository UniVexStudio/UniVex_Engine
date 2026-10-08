// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/camera_component_uve.h"

#include <cmath>
#include <numbers>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsProjectionModeUVE(const CameraProjectionModeUVE projection) noexcept {
    return static_cast<std::uint8_t>(projection) <=
           static_cast<std::uint8_t>(CameraProjectionModeUVE::HumanEye);
}

} // namespace

bool IsCameraComponentValidUVE(const CameraComponentUVE& camera) noexcept {
    return IsProjectionModeUVE(camera.projection) && std::isfinite(camera.fieldOfViewDegrees) &&
           camera.fieldOfViewDegrees >= kMinimumCameraFieldOfViewDegreesUVE &&
           camera.fieldOfViewDegrees < 180.0F && std::isfinite(camera.nearPlane) && camera.nearPlane > 0.0F &&
           std::isfinite(camera.farPlane) && camera.farPlane > camera.nearPlane &&
           std::isfinite(camera.orthographicSize) &&
           camera.orthographicSize >= kMinimumCameraOrthographicSizeUVE;
}

float HumanEyeVerticalFieldOfViewDegreesUVE(const float aspectRatio) noexcept {
    if (!(std::isfinite(aspectRatio) && aspectRatio > 0.0F)) {
        return kHumanEyeCenterVerticalFieldOfViewDegreesUVE;
    }
    const float halfHorizontalRadians =
        kHumanEyeHorizontalFieldOfViewDegreesUVE * (std::numbers::pi_v<float> / 360.0F);
    return 2.0F * std::atan(std::tan(halfHorizontalRadians) / aspectRatio) *
           (180.0F / std::numbers::pi_v<float>);
}

float VerticalFieldOfViewDegreesUVE(const CameraComponentUVE& camera, const float aspectRatio) noexcept {
    if (camera.projection == CameraProjectionModeUVE::HumanEye) {
        return HumanEyeVerticalFieldOfViewDegreesUVE(aspectRatio);
    }
    return camera.fieldOfViewDegrees;
}

float HumanEyeCenterScaleUVE(const float aspectRatio) noexcept {
    const float renderHalfRadians =
        HumanEyeVerticalFieldOfViewDegreesUVE(aspectRatio) * (std::numbers::pi_v<float> / 360.0F);
    const float centerHalfRadians =
        kHumanEyeCenterVerticalFieldOfViewDegreesUVE * (std::numbers::pi_v<float> / 360.0F);
    if (!(renderHalfRadians > centerHalfRadians)) {
        return 1.0F;
    }
    const float renderTan = std::tan(renderHalfRadians);
    if (!(renderTan > 0.0F) || !std::isfinite(renderTan)) {
        return 1.0F;
    }
    const float scale = std::tan(centerHalfRadians) / renderTan;
    if (!std::isfinite(scale) || scale <= 0.0F) {
        return 1.0F;
    }
    return scale < 1.0F ? scale : 1.0F;
}

} // namespace UVE::Scene
