// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "marker_3d.h"

#include "i_entity_manager.h"

namespace UVE::Scene {

bool IsMarker3DObjectComponentValid(const Marker3DComponent& value) noexcept {
    return IsBounded3DObjectString(value.markerName, false) && IsFinite3DObjectVector(value.localPosition) &&
           IsFinite3DObjectQuaternion(value.localRotation);
}

bool IsMarker3DObjectDefinitionValid(const Marker3DObjectDefinition& value) noexcept {
    return IsMarker3DObjectComponentValid(value.marker);
}

void ApplyMarker3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                      const Marker3DObjectDefinition& value) {
    if (!entityManager.IsAlive(entity)) {
        return;
    }
    EnsureObject3DBaseline(entityManager, entity,
                              Marker3DObjectDefinition::defaultName);
    if (entityManager.HasComponent<Marker3DComponent>(entity)) {
        entityManager.GetComponent<Marker3DComponent>(entity) = value.marker;
    } else {
        entityManager.AddComponent<Marker3DComponent>(entity, value.marker);
    }
}

std::optional<Marker3DPose> ComposeMarker3DPose(
    const Math::Vector3 objectPosition, const Math::Quaternion objectRotation,
    const Math::Vector3 localPosition, const Math::Quaternion localRotation) noexcept {
    const std::optional<Math::Vector3> worldPoint =
        Compose3DObjectWorldPoint(objectPosition, objectRotation, localPosition);
    const std::optional<Math::Quaternion> worldRotation =
        Compose3DObjectWorldRotation(objectRotation, localRotation);
    if (!worldPoint.has_value() || !worldRotation.has_value()) {
        return std::nullopt;
    }
    return Marker3DPose{*worldPoint, *worldRotation};
}

bool Marker3D::IsQueryable(const Marker3DComponent& marker) noexcept {
    return marker.enabled && IsMarker3DObjectComponentValid(marker);
}

bool Marker3D::MatchesNameFilter(
    const Marker3DComponent& marker, const std::string_view filterName) noexcept {
    return filterName.empty() || marker.markerName == filterName;
}

std::optional<Marker3DPose> Marker3D::ResolveWorldPose(
    const Marker3DComponent& marker, const Math::Vector3& objectPosition,
    const Math::Quaternion& objectRotation) noexcept {
    if (!IsQueryable(marker)) {
        return std::nullopt;
    }
    return ComposeMarker3DPose(objectPosition, objectRotation, marker.localPosition,
                                  marker.localRotation);
}

Math::Vector3 Marker3D::ForwardDirection(const Marker3DPose& pose) noexcept {
    Math::Quaternion normalized{};
    if (!Math::TryNormalize(pose.rotation, normalized)) {
        return kLocalForwardAxis;
    }
    return Math::RotateVector(normalized, kLocalForwardAxis);
}

Math::Vector3 Marker3D::UpDirection(const Marker3DPose& pose) noexcept {
    Math::Quaternion normalized{};
    if (!Math::TryNormalize(pose.rotation, normalized)) {
        return kLocalUpAxis;
    }
    return Math::RotateVector(normalized, kLocalUpAxis);
}

} // namespace UVE::Scene
