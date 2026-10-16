// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/marker_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsMarker3DObjectComponentValidUVE(const Marker3DComponentUVE& value) noexcept {
    return IsBounded3DObjectStringUVE(value.markerName, false) && IsFinite3DObjectVectorUVE(value.localPosition) &&
           IsFinite3DObjectQuaternionUVE(value.localRotation);
}

bool IsMarker3DObjectDefinitionValidUVE(const Marker3DObjectDefinitionUVE& value) noexcept {
    return IsMarker3DObjectComponentValidUVE(value.marker);
}

void ApplyMarker3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                      const Marker3DObjectDefinitionUVE& value) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    EnsureObject3DBaselineUVE(entityManager, entity,
                              Marker3DObjectDefinitionUVE::defaultName);
    if (entityManager.HasComponentUVE<Marker3DComponentUVE>(entity)) {
        entityManager.GetComponentUVE<Marker3DComponentUVE>(entity) = value.marker;
    } else {
        entityManager.AddComponentUVE<Marker3DComponentUVE>(entity, value.marker);
    }
}

std::optional<Marker3DPoseUVE> ComposeMarker3DPoseUVE(
    const Math::Vector3UVE objectPosition, const Math::QuaternionUVE objectRotation,
    const Math::Vector3UVE localPosition, const Math::QuaternionUVE localRotation) noexcept {
    const std::optional<Math::Vector3UVE> worldPoint =
        Compose3DObjectWorldPointUVE(objectPosition, objectRotation, localPosition);
    const std::optional<Math::QuaternionUVE> worldRotation =
        Compose3DObjectWorldRotationUVE(objectRotation, localRotation);
    if (!worldPoint.has_value() || !worldRotation.has_value()) {
        return std::nullopt;
    }
    return Marker3DPoseUVE{*worldPoint, *worldRotation};
}

bool Marker3DUVE::IsQueryableUVE(const Marker3DComponentUVE& marker) noexcept {
    return marker.enabled && IsMarker3DObjectComponentValidUVE(marker);
}

bool Marker3DUVE::MatchesNameFilterUVE(
    const Marker3DComponentUVE& marker, const std::string_view filterName) noexcept {
    return filterName.empty() || marker.markerName == filterName;
}

std::optional<Marker3DPoseUVE> Marker3DUVE::ResolveWorldPoseUVE(
    const Marker3DComponentUVE& marker, const Math::Vector3UVE& objectPosition,
    const Math::QuaternionUVE& objectRotation) noexcept {
    if (!IsQueryableUVE(marker)) {
        return std::nullopt;
    }
    return ComposeMarker3DPoseUVE(objectPosition, objectRotation, marker.localPosition,
                                  marker.localRotation);
}

Math::Vector3UVE Marker3DUVE::ForwardDirectionUVE(const Marker3DPoseUVE& pose) noexcept {
    Math::QuaternionUVE normalized{};
    if (!Math::TryNormalizeUVE(pose.rotation, normalized)) {
        return kLocalForwardAxisUVE;
    }
    return Math::RotateVectorUVE(normalized, kLocalForwardAxisUVE);
}

Math::Vector3UVE Marker3DUVE::UpDirectionUVE(const Marker3DPoseUVE& pose) noexcept {
    Math::QuaternionUVE normalized{};
    if (!Math::TryNormalizeUVE(pose.rotation, normalized)) {
        return kLocalUpAxisUVE;
    }
    return Math::RotateVectorUVE(normalized, kLocalUpAxisUVE);
}

} // namespace UVE::Scene
