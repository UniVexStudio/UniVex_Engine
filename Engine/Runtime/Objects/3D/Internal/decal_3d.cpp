// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "decal_3d.h"

#include <algorithm>
#include <cmath>

#include "i_entity_manager.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsUnitInterval(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

[[nodiscard]] bool IsNonNegative(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

} // namespace

bool IsDecal3DObjectComponentValid(const Decal3DComponent& value) noexcept {
    return IsBounded3DObjectString(value.materialAssetPath) && IsFinite3DObjectVector(value.size) &&
           value.size.x > 0.0F && value.size.y > 0.0F && value.size.z > 0.0F && IsNonNegative(value.lifetime) &&
           value.projection <= DecalProjectionMode::Cylinder && IsNonNegative(value.modulate.x) &&
           IsNonNegative(value.modulate.y) && IsNonNegative(value.modulate.z) &&
           IsNonNegative(value.emissionEnergy) && IsUnitInterval(value.albedoMix) &&
           IsUnitInterval(value.normalFade) && IsNonNegative(value.upperFade) &&
           IsNonNegative(value.lowerFade) && IsNonNegative(value.distanceFadeBegin) &&
           IsNonNegative(value.distanceFadeLength);
}

bool IsDecal3DPainting(const Decal3DComponent& value) noexcept {
    return value.enabled && !value.expired && value.size.x > 0.0F && value.size.y > 0.0F &&
           value.size.z > 0.0F;
}

namespace {

/// One authored fade evaluated against where a sample sits along the projection axis. Positive
/// `band` fades the outer `band` of the half-volume: 0 is off, 1 fades the whole half.
[[nodiscard]] float AxisFadeWeight(const float unitCoordinate, const float band) noexcept {
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

[[nodiscard]] float ClampUnit(const float value) noexcept {
    return std::min(std::max(value, 0.0F), 1.0F);
}

} // namespace

bool TryMakeDecal3DProjection(const Decal3DComponent& value, const Math::Vector3& worldPosition,
                                 const Math::Quaternion& worldRotation, const Math::Vector3& worldScale,
                                 Decal3DProjection& outProjection) noexcept {
    outProjection = Decal3DProjection{};
    if (!IsFinite3DObjectVector(worldPosition) || !IsFinite3DObjectVector(worldScale) ||
        !IsFinite3DObjectVector(value.size)) {
        return false;
    }
    if (!(value.size.x > 0.0F) || !(value.size.y > 0.0F) || !(value.size.z > 0.0F) ||
        !(worldScale.x > 0.0F) || !(worldScale.y > 0.0F) || !(worldScale.z > 0.0F)) {
        return false;
    }

    Math::Quaternion inverseRotation{};
    if (!Math::TryInverse(worldRotation, inverseRotation)) {
        return false;
    }

    outProjection.worldPosition = worldPosition;
    outProjection.worldRotation = worldRotation;
    outProjection.inverseWorldRotation = inverseRotation;
    outProjection.halfExtents = Math::Vector3{value.size.x * worldScale.x, value.size.y * worldScale.y,
                                                 value.size.z * worldScale.z} *
                                0.5F;
    // The volume projects along the object's local -Y, so the direction it travels is that axis
    // rotated into the world - the same rotation every other local axis in the engine goes through.
    outProjection.projectionDirection =
        Math::Normalize(Math::RotateVector(worldRotation, Math::Vector3{0.0F, -1.0F, 0.0F}));
    outProjection.mode = value.projection;
    outProjection.normalFade = value.normalFade;
    outProjection.upperFade = value.upperFade;
    outProjection.lowerFade = value.lowerFade;
    outProjection.distanceFadeEnabled = value.distanceFadeEnabled;
    outProjection.distanceFadeBegin = value.distanceFadeBegin;
    outProjection.distanceFadeLength = value.distanceFadeLength;
    return true;
}

Math::Vector3 Decal3DWorldToUnit(const Decal3DProjection& projection,
                                       const Math::Vector3& worldPoint) noexcept {
    const Math::Vector3 offset = worldPoint - projection.worldPosition;
    const Math::Vector3 local = Math::RotateVector(projection.inverseWorldRotation, offset);
    return Math::Vector3{local.x / projection.halfExtents.x, local.y / projection.halfExtents.y,
                            local.z / projection.halfExtents.z};
}

Math::Vector3 Decal3DUnitToWorld(const Decal3DProjection& projection,
                                       const Math::Vector3& unitPoint) noexcept {
    const Math::Vector3 local{unitPoint.x * projection.halfExtents.x,
                                 unitPoint.y * projection.halfExtents.y,
                                 unitPoint.z * projection.halfExtents.z};
    return projection.worldPosition + Math::RotateVector(projection.worldRotation, local);
}

Decal3DSample SampleDecal3D(const Decal3DProjection& projection, const Math::Vector3& worldPoint,
                                  const Math::Vector3& worldNormal, const float distanceToCamera) noexcept {
    Decal3DSample sample{};
    if (!IsFinite3DObjectVector(worldPoint) || !IsFinite3DObjectVector(worldNormal) ||
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
    sample.local = Decal3DWorldToUnit(projection, worldPoint);

    switch (projection.mode) {
        case DecalProjectionMode::Cylinder:
            // The cylinder runs along the projection axis (local Y): a radial footprint, bounded
            // the same way along the axis.
            sample.insideVolume = (sample.local.x * sample.local.x + sample.local.z * sample.local.z) <= 1.0F &&
                                  std::fabs(sample.local.y) <= 1.0F;
            break;
        case DecalProjectionMode::Box:
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
    const float facingTowardsDecal = ClampUnit(Math::Dot(worldNormal, -projection.projectionDirection));
    sample.normalFadeWeight = 1.0F - ClampUnit(projection.normalFade) * (1.0F - facingTowardsDecal);

    // Vertical (along the projection axis) fades: the volume's +Y end is the far end of the
    // projection, and each authored band fades the outer part of the corresponding half.
    sample.depthFadeWeight = AxisFadeWeight(sample.local.y, projection.upperFade) *
                             AxisFadeWeight(-sample.local.y, projection.lowerFade);

    // Distance fade: the same authored begin/length the Inspector shows. A zero length is a hard
    // cut at `begin` rather than a division by zero.
    if (projection.distanceFadeEnabled) {
        if (projection.distanceFadeLength > 0.0F) {
            sample.distanceFadeWeight =
                1.0F - ClampUnit((distanceToCamera - projection.distanceFadeBegin) / projection.distanceFadeLength);
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

bool AdvanceDecal3DLifetime(Decal3DComponent& value, const float deltaSeconds) noexcept {
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

void ApplyDecal3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                   const Decal3DObjectDefinition& value) {
    ApplyRenderInstance3DBase(entityManager, entity, Decal3DObjectDefinition::defaultName);
    if (entityManager.IsAlive(entity) && !entityManager.HasComponent<Decal3DComponent>(entity)) {
        entityManager.AddComponent<Decal3DComponent>(entity, value.decal);
    }
}

} // namespace UVE::Scene
