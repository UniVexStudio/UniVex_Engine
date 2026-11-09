// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Scene {

inline constexpr float kMinimumCameraFieldOfViewDegreesUVE = 0.1F;
inline constexpr float kMinimumCameraOrthographicSizeUVE = 0.001F;
inline constexpr float kHumanEyeHorizontalFieldOfViewDegreesUVE = 120.0F;
inline constexpr float kHumanEyeCenterVerticalFieldOfViewDegreesUVE = 60.0F;

enum class CameraProjectionModeUVE : std::uint8_t {
    Perspective = 0,
    Orthographic,
    HumanEye,
};

struct CameraComponentUVE final {
    float fieldOfViewDegrees = 60.0F;
    float nearPlane = 0.1F;
    float farPlane = 1000.0F;
    CameraProjectionModeUVE projection = CameraProjectionModeUVE::Perspective;
    float orthographicSize = 5.0F;
    bool current = false;
};

[[nodiscard]] bool IsCameraComponentValidUVE(const CameraComponentUVE& camera) noexcept;

[[nodiscard]] float HumanEyeVerticalFieldOfViewDegreesUVE(float aspectRatio) noexcept;

[[nodiscard]] float VerticalFieldOfViewDegreesUVE(const CameraComponentUVE& camera, float aspectRatio) noexcept;

[[nodiscard]] float HumanEyeCenterScaleUVE(float aspectRatio) noexcept;

} // namespace UVE::Scene
