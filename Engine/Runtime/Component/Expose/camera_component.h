// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Scene {

inline constexpr float kMinimumCameraFieldOfViewDegrees = 0.1F;
inline constexpr float kMinimumCameraOrthographicSize = 0.001F;
inline constexpr float kHumanEyeHorizontalFieldOfViewDegrees = 120.0F;
inline constexpr float kHumanEyeCenterVerticalFieldOfViewDegrees = 60.0F;

enum class CameraProjectionMode : std::uint8_t {
    Perspective = 0,
    Orthographic,
    HumanEye,
};

struct CameraComponent final {
    float fieldOfViewDegrees = 60.0F;
    float nearPlane = 0.1F;
    float farPlane = 1000.0F;
    CameraProjectionMode projection = CameraProjectionMode::Perspective;
    float orthographicSize = 5.0F;
    bool current = false;
};

[[nodiscard]] bool IsCameraComponentValid(const CameraComponent& camera) noexcept;

[[nodiscard]] float HumanEyeVerticalFieldOfViewDegrees(float aspectRatio) noexcept;

[[nodiscard]] float VerticalFieldOfViewDegrees(const CameraComponent& camera, float aspectRatio) noexcept;

[[nodiscard]] float HumanEyeCenterScale(float aspectRatio) noexcept;

} // namespace UVE::Scene
