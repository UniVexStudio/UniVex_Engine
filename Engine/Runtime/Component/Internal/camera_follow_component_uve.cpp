// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/camera_follow_component_uve.h"

#include <cmath>

namespace UVE::Scene {

bool IsCameraFollowComponentValidUVE(const CameraFollowComponentUVE& value) noexcept {
    return std::isfinite(value.offset.x) && std::isfinite(value.offset.y) && std::isfinite(value.offset.z);
}

bool HasCameraFollowTargetUVE(const CameraFollowComponentUVE& value,
                              const EntityUVE cameraEntity) noexcept {
    return value.target != kInvalidEntityUVE && value.target != cameraEntity;
}

CameraFollowComponentUVE SanitizeCameraFollowComponentUVE(
    const CameraFollowComponentUVE& value, const EntityUVE cameraEntity) noexcept {
    CameraFollowComponentUVE out = value;
    if (!IsCameraFollowComponentValidUVE(out)) {
        out.offset = Math::Vector3UVE{
            std::isfinite(out.offset.x) ? out.offset.x : 0.0F,
            std::isfinite(out.offset.y) ? out.offset.y : 0.0F,
            std::isfinite(out.offset.z) ? out.offset.z : 0.0F,
        };
    }
    if (cameraEntity != kInvalidEntityUVE && out.target == cameraEntity) {
        out.target = kInvalidEntityUVE;
    }
    return out;
}

Math::Vector3UVE ResolveCameraFollowDesiredPositionUVE(
    const CameraFollowComponentUVE& value,
    const Math::Vector3UVE& targetWorldPosition) noexcept {
    const Math::Vector3UVE safeTarget = Math::IsFiniteUVE(targetWorldPosition)
                                            ? targetWorldPosition
                                            : Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    const Math::Vector3UVE safeOffset = IsCameraFollowComponentValidUVE(value)
                                            ? value.offset
                                            : Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    return safeTarget + safeOffset;
}

void ApplyPlayerPossessionToCameraFollowUVE(CameraFollowComponentUVE& value,
                                            const EntityUVE possessedPawn) noexcept {
    if (!value.followPossessedPawn) {
        return;
    }
    value.target = possessedPawn;
}

} // namespace UVE::Scene
