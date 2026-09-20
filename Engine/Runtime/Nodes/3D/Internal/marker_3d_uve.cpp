// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/marker_3d_uve.h"

namespace UVE::Scene {

bool IsMarker3DNodeComponentValidUVE(const Marker3DNodeComponentUVE& value) noexcept {
    return IsBounded3DNodeStringUVE(value.markerName, false) && IsFinite3DNodeVectorUVE(value.localPosition) &&
           IsFinite3DNodeQuaternionUVE(value.localRotation);
}

std::optional<Marker3DPoseUVE> ComposeMarker3DPoseUVE(
    const Math::Vector3UVE nodePosition, const Math::QuaternionUVE nodeRotation,
    const Math::Vector3UVE localPosition, const Math::QuaternionUVE localRotation) noexcept {
    if (!IsFinite3DNodeVectorUVE(nodePosition) || !IsFinite3DNodeVectorUVE(localPosition) ||
        !IsFinite3DNodeQuaternionUVE(nodeRotation) || !IsFinite3DNodeQuaternionUVE(localRotation)) {
        return std::nullopt;
    }
    Math::QuaternionUVE nodeRotationNormalized{};
    if (!Math::TryNormalizeUVE(nodeRotation, nodeRotationNormalized)) {
        return std::nullopt;
    }
    Marker3DPoseUVE pose{};
    pose.position = nodePosition + Math::RotateVectorUVE(nodeRotationNormalized, localPosition);
    pose.rotation = Math::MultiplyUVE(nodeRotationNormalized, localRotation);
    Math::QuaternionUVE rotationNormalized{};
    if (!Math::TryNormalizeUVE(pose.rotation, rotationNormalized)) {
        return std::nullopt;
    }
    pose.rotation = rotationNormalized;
    return pose;
}

} // namespace UVE::Scene
