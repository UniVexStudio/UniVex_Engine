// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/2d/marker_2d_uve.h"

#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsMarker2DComponentValidUVE(const Marker2DComponentUVE& value) noexcept {
    return Math::IsFiniteUVE(value.positionOffset) && std::isfinite(value.rotationRadiansOffset) &&
           std::isfinite(value.gizmoExtents) && value.gizmoExtents > 0.0F;
}

Marker2DComponentUVE SanitizeMarker2DComponentUVE(const Marker2DComponentUVE& value) noexcept {
    Marker2DComponentUVE out = value;
    if (!Math::IsFiniteUVE(out.positionOffset)) {
        out.positionOffset = Math::Vector2UVE{0.0F, 0.0F};
    }
    if (!std::isfinite(out.rotationRadiansOffset)) {
        out.rotationRadiansOffset = 0.0F;
    }
    if (!std::isfinite(out.gizmoExtents) || out.gizmoExtents <= 0.0F) {
        out.gizmoExtents = 10.0F;
    }
    return out;
}

bool IsMarker2DObjectDefinitionValidUVE(const Marker2DObjectDefinitionUVE& value) noexcept {
    return IsMarker2DComponentValidUVE(value.marker);
}

void ApplyMarker2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                      const Marker2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<Marker2DComponentUVE>(
        entity, SanitizeMarker2DComponentUVE(value.marker));
}

Math::Vector2UVE Marker2DUVE::ComposeWorldPositionUVE(
    const Marker2DComponentUVE& marker, const Math::Vector2UVE& entityPosition2D,
    const float entityRotationRadians) noexcept {
    const Marker2DComponentUVE safe = SanitizeMarker2DComponentUVE(marker);
    if (!Math::IsFiniteUVE(entityPosition2D) || !std::isfinite(entityRotationRadians)) {
        return safe.positionOffset;
    }
    const float c = std::cos(entityRotationRadians);
    const float s = std::sin(entityRotationRadians);
    return Math::Vector2UVE{
        entityPosition2D.x + safe.positionOffset.x * c - safe.positionOffset.y * s,
        entityPosition2D.y + safe.positionOffset.x * s + safe.positionOffset.y * c};
}

Math::Vector2UVE Marker2DUVE::ResolveForwardDirectionUVE(
    const Marker2DComponentUVE& marker, const float entityRotationRadians) noexcept {
    const Marker2DComponentUVE safe = SanitizeMarker2DComponentUVE(marker);
    const float totalAngle =
        (std::isfinite(entityRotationRadians) ? entityRotationRadians : 0.0F) +
        safe.rotationRadiansOffset;
    return Math::Vector2UVE{std::cos(totalAngle), std::sin(totalAngle)};
}

} // namespace UVE::Scene
