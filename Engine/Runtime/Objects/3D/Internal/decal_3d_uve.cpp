// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/decal_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsUnitIntervalUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

[[nodiscard]] bool IsNonNegativeUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

} // namespace

bool IsDecal3DObjectComponentValidUVE(const Decal3DComponentUVE& value) noexcept {
    return IsBounded3DObjectStringUVE(value.materialAssetPath) && IsFinite3DObjectVectorUVE(value.size) &&
           value.size.x > 0.0F && value.size.y > 0.0F && value.size.z > 0.0F && IsNonNegativeUVE(value.lifetime) &&
           value.projection <= DecalProjectionModeUVE::Cylinder && IsNonNegativeUVE(value.modulate.x) &&
           IsNonNegativeUVE(value.modulate.y) && IsNonNegativeUVE(value.modulate.z) &&
           IsNonNegativeUVE(value.emissionEnergy) && IsUnitIntervalUVE(value.albedoMix) &&
           IsUnitIntervalUVE(value.normalFade) && IsNonNegativeUVE(value.upperFade) &&
           IsNonNegativeUVE(value.lowerFade) && IsNonNegativeUVE(value.distanceFadeBegin) &&
           IsNonNegativeUVE(value.distanceFadeLength);
}

bool IsDecal3DPaintingUVE(const Decal3DComponentUVE& value) noexcept {
    return value.enabled && !value.expired && value.size.x > 0.0F && value.size.y > 0.0F &&
           value.size.z > 0.0F;
}

namespace {

/// One authored fade evaluated against where a sample sits along the projection axis. Positive
/// `band` fades the outer `band` of the half-volume: 0 is off, 1 fades the whole half.
[[nodiscard]] float AxisFadeWeightUVE(const float unitCoordinate, const float band) noexcept {
    if (!(band > 0.0F)) {
        return 1.0F;
    }
    const float edge = 1.0F - band;
    if (unitCoordinate <= edge) {
        return 1.0F;
    }
    const float travelled = (unitCoordinate - edge) / band;
    return 1.0F - std::min(travelled, 1.0F);
}

[[nodiscard]] float ClampUnitUVE(const float value) noexcept {
    return std::min(std::max(value, 0.0F), 1.0F);
}

} // namespace

bool TryMakeDecal3DProjectionUVE(const Decal3DComponentUVE& value, const Math::Vector3UVE& worldPosition,
                                 const Math::QuaternionUVE& worldRotation, const Math::Vector3UVE& worldScale,
                                 Decal3DProjectionUVE& outProjection) noexcept {
    outProjection = Decal3DProjectionUVE{};
    if (!IsFinite3DObjectVectorUVE(worldPosition) || !IsFinite3DObjectVectorUVE(worldScale) ||
        !IsFinite3DObjectVectorUVE(value.size)) {
        return false;
    }
    if (!(value.size.x > 0.0F) || !(value.size.y > 0.0F) || !(value.size.z > 0.0F) ||
        !(worldScale.x > 0.0F) || !(worldScale.y > 0.0F) || !(worldScale.z > 0.0F)) {
        return false;
    }

    Math::QuaternionUVE inverseRotation{};
    if (!Math::TryInverseUVE(worldRotation, inverseRotation)) {
        return false;
    }

    outProjection.worldPosition = worldPosition;
    outProjection.worldRotation = worldRotation;
    outProjection.inverseWorldRotation = inverseRotation;
    outProjection.halfExtents = Math::Vector3UVE{value.size.x * worldScale.x, value.size.y * worldScale.y,
                                                 value.size.z * worldScale.z} *
                                0.5F;
    // The volume projects along the object's local -Y, so the direction it travels is that axis
    // rotated into the world - the same rotation every other local axis in the engine goes through.
    outProjection.projectionDirection =
        Math::NormalizeUVE(Math::RotateVectorUVE(worldRotation, Math::Vector3UVE{0.0F, -1.0F, 0.0F}));
    outProjection.mode = value.projection;
    outProjection.normalFade = value.normalFade;
    outProjection.upperFade = value.upperFade;
    outProjection.lowerFade = value.lowerFade;
    outProjection.distanceFadeEnabled = value.distanceFadeEnabled;
    outProjection.distanceFadeBegin = value.distanceFadeBegin;
    outProjection.distanceFadeLength = value.distanceFadeLength;
    return true;
}

Math::Vector3UVE Decal3DWorldToUnitUVE(const Decal3DProjectionUVE& projection,
                                       const Math::Vector3UVE& worldPoint) noexcept {
    const Math::Vector3UVE offset = worldPoint - projection.worldPosition;
    const Math::Vector3UVE local = Math::RotateVectorUVE(projection.inverseWorldRotation, offset);
    return Math::Vector3UVE{local.x / projection.halfExtents.x, local.y / projection.halfExtents.y,
                            local.z / projection.halfExtents.z};
}

Math::Vector3UVE Decal3DUnitToWorldUVE(const Decal3DProjectionUVE& projection,
                                       const Math::Vector3UVE& unitPoint) noexcept {
    const Math::Vector3UVE local{unitPoint.x * projection.halfExtents.x,
                                 unitPoint.y * projection.halfExtents.y,
                                 unitPoint.z * projection.halfExtents.z};
    return projection.worldPosition + Math::RotateVectorUVE(projection.worldRotation, local);
}

Decal3DSampleUVE SampleDecal3DUVE(const Decal3DProjectionUVE& projection, const Math::Vector3UVE& worldPoint,
                                  const Math::Vector3UVE& worldNormal, const float distanceToCamera) noexcept {
    Decal3DSampleUVE sample{};
    if (!IsFinite3DObjectVectorUVE(worldPoint) || !IsFinite3DObjectVectorUVE(worldNormal) ||
        !std::isfinite(distanceToCamera)) {
        // Nothing painted for a point or a distance that is not a number: a NaN weight would spread
        // through the blend and take the whole frame with it.
        sample.insideVolume = false;
        sample.combinedWeight = 0.0F;
        return sample;
    }

    // Unit coordinates: the world offset rotated into the volume's frame, divided by the half
    // extents, so "inside" is |component| <= 1 for every mode and the box-to-cylinder difference
    // is one footprint test rather than two coordinate systems.
    sample.local = Decal3DWorldToUnitUVE(projection, worldPoint);

    switch (projection.mode) {
        case DecalProjectionModeUVE::Cylinder:
            // The cylinder runs along the projection axis (local Y): a radial footprint, bounded
            // the same way along the axis.
            sample.insideVolume = (sample.local.x * sample.local.x + sample.local.z * sample.local.z) <= 1.0F &&
                                  std::fabs(sample.local.y) <= 1.0F;
            break;
        case DecalProjectionModeUVE::Box:
        default:
            sample.insideVolume = std::fabs(sample.local.x) <= 1.0F && std::fabs(sample.local.y) <= 1.0F &&
                                  std::fabs(sample.local.z) <= 1.0F;
            break;
    }

    // Normal fade: how squarely the surface faces the decal decides how much of it the decal
    // replaces. The surface's normal pointing back UP the projection direction is "fully facing";
    // 0 on the authored field paints every facing inside the volume and 1 paints only those facing
    // back at the decal.
    // surface squarely under the decal does; a backface or an edge-on sliver scores zero.
    const float facingTowardsDecal = ClampUnitUVE(Math::DotUVE(worldNormal, -projection.projectionDirection));
    sample.normalFadeWeight = 1.0F - ClampUnitUVE(projection.normalFade) * (1.0F - facingTowardsDecal);

    // Vertical (along the projection axis) fades: the volume's +Y end is the far end of the
    // projection, and each authored band fades the outer part of the corresponding half.
    sample.depthFadeWeight = AxisFadeWeightUVE(sample.local.y, projection.upperFade) *
                             AxisFadeWeightUVE(-sample.local.y, projection.lowerFade);

    // Distance fade: the same authored begin/length the Inspector shows. A zero length is a hard
    // cut at `begin` rather than a division by zero.
    if (projection.distanceFadeEnabled) {
        if (projection.distanceFadeLength > 0.0F) {
            sample.distanceFadeWeight =
                1.0F - ClampUnitUVE((distanceToCamera - projection.distanceFadeBegin) / projection.distanceFadeLength);
        } else {
            sample.distanceFadeWeight = distanceToCamera <= projection.distanceFadeBegin ? 1.0F : 0.0F;
        }
    }

    sample.combinedWeight = sample.insideVolume
                                ? sample.normalFadeWeight * sample.depthFadeWeight * sample.distanceFadeWeight
                                : 0.0F;
    if (!std::isfinite(sample.combinedWeight)) {
        sample.insideVolume = false;
        sample.combinedWeight = 0.0F;
    }
    return sample;
}

bool AdvanceDecal3DLifetimeUVE(Decal3DComponentUVE& value, const float deltaSeconds) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F || value.expired) {
        return false;
    }
    if (!std::isfinite(value.lifetime)) {
        return false;
    }
    if (value.lifetime <= 0.0F) {
        // Permanent. Its remaining time is not a countdown, so it stays where the author left it.
        value.remainingLifetime = 0.0F;
        return false;
    }
    if (value.remainingLifetime <= 0.0F) {
        // Not armed yet - freshly created, loaded, or handed to the runtime for the first time.
        value.remainingLifetime = value.lifetime;
    }

    value.remainingLifetime -= deltaSeconds;
    if (value.remainingLifetime > 0.0F) {
        return false;
    }
    value.remainingLifetime = 0.0F;
    value.expired = true;
    return true;
}

void ApplyDecal3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                   const Decal3DObjectDefinitionUVE& value) {
    ApplyRenderInstance3DBaseUVE(entityManager, entity, Decal3DObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<Decal3DComponentUVE>(entity)) {
        entityManager.AddComponentUVE<Decal3DComponentUVE>(entity, value.decal);
    }
}

} // namespace UVE::Scene
