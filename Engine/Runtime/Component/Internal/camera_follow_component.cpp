// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "camera_follow_component.h"

#include <cmath>

namespace UVE::Scene {

bool IsCameraFollowComponentValid(const CameraFollowComponent& value) noexcept {
    return std::isfinite(value.offset.x) && std::isfinite(value.offset.y) && std::isfinite(value.offset.z);
}

bool HasCameraFollowTarget(const CameraFollowComponent& value,
                              const Entity cameraEntity) noexcept {
    return value.target != kInvalidEntity && value.target != cameraEntity;
}

CameraFollowComponent SanitizeCameraFollowComponent(
    const CameraFollowComponent& value, const Entity cameraEntity) noexcept {
    CameraFollowComponent out = value;
    if (!IsCameraFollowComponentValid(out)) {
        out.offset = Math::Vector3{
            std::isfinite(out.offset.x) ? out.offset.x : 0.0F,
            std::isfinite(out.offset.y) ? out.offset.y : 0.0F,
            std::isfinite(out.offset.z) ? out.offset.z : 0.0F,
        };
    }
    if (cameraEntity != kInvalidEntity && out.target == cameraEntity) {
        out.target = kInvalidEntity;
    }
    return out;
}

Math::Vector3 ResolveCameraFollowDesiredPosition(
    const CameraFollowComponent& value,
    const Math::Vector3& targetWorldPosition) noexcept {
    const Math::Vector3 safeTarget = Math::IsFinite(targetWorldPosition)
                                            ? targetWorldPosition
                                            : Math::Vector3{0.0F, 0.0F, 0.0F};
    const Math::Vector3 safeOffset = IsCameraFollowComponentValid(value)
                                            ? value.offset
                                            : Math::Vector3{0.0F, 0.0F, 0.0F};
    return safeTarget + safeOffset;
}

void ApplyPlayerPossessionToCameraFollow(CameraFollowComponent& value,
                                            const Entity possessedPawn) noexcept {
    if (!value.followPossessedPawn) {
        return;
    }
    value.target = possessedPawn;
}

} // namespace UVE::Scene
