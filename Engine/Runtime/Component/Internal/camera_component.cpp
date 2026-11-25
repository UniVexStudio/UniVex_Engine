// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "camera_component.h"

#include <cmath>
#include <numbers>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsProjectionMode(const CameraProjectionMode projection) noexcept {
    return static_cast<std::uint8_t>(projection) <=
           static_cast<std::uint8_t>(CameraProjectionMode::HumanEye);
}

} // namespace

bool IsCameraComponentValid(const CameraComponent& camera) noexcept {
    return IsProjectionMode(camera.projection) && std::isfinite(camera.fieldOfViewDegrees) &&
           camera.fieldOfViewDegrees >= kMinimumCameraFieldOfViewDegrees &&
           camera.fieldOfViewDegrees < 180.0F && std::isfinite(camera.nearPlane) && camera.nearPlane > 0.0F &&
           std::isfinite(camera.farPlane) && camera.farPlane > camera.nearPlane &&
           std::isfinite(camera.orthographicSize) &&
           camera.orthographicSize >= kMinimumCameraOrthographicSize;
}

float HumanEyeVerticalFieldOfViewDegrees(const float aspectRatio) noexcept {
    if (!(std::isfinite(aspectRatio) && aspectRatio > 0.0F)) {
        return kHumanEyeCenterVerticalFieldOfViewDegrees;
    }
    const float halfHorizontalRadians =
        kHumanEyeHorizontalFieldOfViewDegrees * (std::numbers::pi_v<float> / 360.0F);
    return 2.0F * std::atan(std::tan(halfHorizontalRadians) / aspectRatio) *
           (180.0F / std::numbers::pi_v<float>);
}

float VerticalFieldOfViewDegrees(const CameraComponent& camera, const float aspectRatio) noexcept {
    if (camera.projection == CameraProjectionMode::HumanEye) {
        return HumanEyeVerticalFieldOfViewDegrees(aspectRatio);
    }
    return camera.fieldOfViewDegrees;
}

float HumanEyeCenterScale(const float aspectRatio) noexcept {
    const float renderHalfRadians =
        HumanEyeVerticalFieldOfViewDegrees(aspectRatio) * (std::numbers::pi_v<float> / 360.0F);
    const float centerHalfRadians =
        kHumanEyeCenterVerticalFieldOfViewDegrees * (std::numbers::pi_v<float> / 360.0F);
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
