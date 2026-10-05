// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/marker_3d_uve.h"

namespace UVE::Scene {

bool IsMarker3DObjectComponentValidUVE(const Marker3DComponentUVE& value) noexcept {
    return IsBounded3DObjectStringUVE(value.markerName, false) && IsFinite3DObjectVectorUVE(value.localPosition) &&
           IsFinite3DObjectQuaternionUVE(value.localRotation);
}

std::optional<Marker3DPoseUVE> ComposeMarker3DPoseUVE(
    const Math::Vector3UVE objectPosition, const Math::QuaternionUVE objectRotation,
    const Math::Vector3UVE localPosition, const Math::QuaternionUVE localRotation) noexcept {
    if (!IsFinite3DObjectVectorUVE(objectPosition) || !IsFinite3DObjectVectorUVE(localPosition) ||
        !IsFinite3DObjectQuaternionUVE(objectRotation) || !IsFinite3DObjectQuaternionUVE(localRotation)) {
        return std::nullopt;
    }
    Math::QuaternionUVE objectRotationNormalized{};
    if (!Math::TryNormalizeUVE(objectRotation, objectRotationNormalized)) {
        return std::nullopt;
    }
    Marker3DPoseUVE pose{};
    pose.position = objectPosition + Math::RotateVectorUVE(objectRotationNormalized, localPosition);
    pose.rotation = Math::MultiplyUVE(objectRotationNormalized, localRotation);
    Math::QuaternionUVE rotationNormalized{};
    if (!Math::TryNormalizeUVE(pose.rotation, rotationNormalized)) {
        return std::nullopt;
    }
    pose.rotation = rotationNormalized;
    return pose;
}

} // namespace UVE::Scene
