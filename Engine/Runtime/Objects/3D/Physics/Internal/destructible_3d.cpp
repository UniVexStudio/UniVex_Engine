// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "destructible_3d.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "i_entity_manager.h"
#include "quaternion.h"
#include "transform_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] std::uint32_t NextHashUVE(std::uint32_t x) noexcept {
    x ^= x >> 16U;
    x *= 0x7FEB352DU;
    x ^= x >> 15U;
    x *= 0x846CA68BU;
    x ^= x >> 16U;
    return x;
}

[[nodiscard]] float UnitSignedUVE(const std::uint32_t h) noexcept {
    return (static_cast<float>(h & 0x00FFFFFFU) / static_cast<float>(0x00800000U)) - 1.0F;
}

void BuildBoxChunkGeometryUVE(const Math::Vector3UVE& halfExtents,
                              std::vector<Math::Vector3UVE>& outVerts,
                              std::vector<std::uint32_t>& outIndices) {
    const float hx = halfExtents.x;
    const float hy = halfExtents.y;
    const float hz = halfExtents.z;
    outVerts = {
        {-hx, -hy, -hz}, {hx, -hy, -hz}, {hx, hy, -hz}, {-hx, hy, -hz},
        {-hx, -hy, hz},  {hx, -hy, hz},  {hx, hy, hz},  {-hx, hy, hz},
    };
    outIndices = {
        0, 2, 1, 0, 3, 2, // -Z
        4, 5, 6, 4, 6, 7, // +Z
        0, 1, 5, 0, 5, 4, // -Y
        2, 3, 7, 2, 7, 6, // +Y
        0, 4, 7, 0, 7, 3, // -X
        1, 2, 6, 1, 6, 5, // +X
    };
}

} // namespace

std::vector<DestructibleChunkUVE> FractureBoundsIntoChunksUVE(
    const Math::AabbUVE& localBounds, const std::uint32_t chunkCount, const float totalMass,
    const std::uint32_t seed, const Math::Vector3UVE& impactPointLocal,
    const float explosionImpulse) {
    const std::uint32_t targetCount = std::clamp(chunkCount, 2U, 64U);
    const Math::Vector3UVE fullSize = localBounds.max - localBounds.min;
    if (fullSize.x <= 1.0e-5F || fullSize.y <= 1.0e-5F || fullSize.z <= 1.0e-5F) {
        return {};
    }

    // Deterministic binary space partitioning along the longest axis of each box cell until we
    // reach `targetCount` airtight convex cells conserving 100% of total bounds volume.
    std::vector<Math::AabbUVE> cells;
    cells.reserve(targetCount);
    cells.push_back(localBounds);

    std::uint32_t rng = seed ^ 0x9E3779B9U;
    while (cells.size() < targetCount) {
        std::size_t bestIdx = 0U;
        float bestVol = -1.0F;
        for (std::size_t i = 0U; i < cells.size(); ++i) {
            const Math::Vector3UVE s = cells[i].max - cells[i].min;
            const float vol = s.x * s.y * s.z;
            if (vol > bestVol) {
                bestVol = vol;
                bestIdx = i;
            }
        }
        const Math::AabbUVE box = cells[bestIdx];
        const Math::Vector3UVE s = box.max - box.min;
        rng = NextHashUVE(rng + static_cast<std::uint32_t>(cells.size()));
        const float splitT = 0.35F + 0.30F * (0.5F + 0.5F * UnitSignedUVE(rng));

        Math::AabbUVE left = box;
        Math::AabbUVE right = box;
        if (s.x >= s.y && s.x >= s.z) {
            const float mid = box.min.x + s.x * splitT;
            left.max.x = mid;
            right.min.x = mid;
        } else if (s.y >= s.z) {
            const float mid = box.min.y + s.y * splitT;
            left.max.y = mid;
            right.min.y = mid;
        } else {
            const float mid = box.min.z + s.z * splitT;
            left.max.z = mid;
            right.min.z = mid;
        }
        cells[bestIdx] = left;
        cells.push_back(right);
    }

    const float totalVolume = fullSize.x * fullSize.y * fullSize.z;
    const float safeMass = std::max(totalMass, 0.1F);
    std::vector<DestructibleChunkUVE> chunks;
    chunks.reserve(cells.size());

    for (std::size_t i = 0U; i < cells.size(); ++i) {
        const Math::AabbUVE& cell = cells[i];
        const Math::Vector3UVE size = cell.max - cell.min;
        const float vol = std::max(size.x * size.y * size.z, 1.0e-6F);
        DestructibleChunkUVE chunk{};
        chunk.localCentroid = (cell.min + cell.max) * 0.5F;
        chunk.halfExtents = size * 0.5F;
        chunk.mass = std::max(safeMass * (vol / totalVolume), 0.05F);
        BuildBoxChunkGeometryUVE(chunk.halfExtents, chunk.vertices, chunk.indices);

        Math::Vector3UVE radial = chunk.localCentroid - impactPointLocal;
        const float radialLen = Math::LengthUVE(radial);
        if (radialLen > 1.0e-4F) {
            radial = radial * (1.0F / radialLen);
        } else {
            radial = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        }
        const float speed = std::max(explosionImpulse, 0.0F) / std::sqrt(chunk.mass);
        chunk.initialLinearVelocity = radial * speed + Math::Vector3UVE{0.0F, speed * 0.25F, 0.0F};

        const std::uint32_t h1 = NextHashUVE(seed ^ static_cast<std::uint32_t>(i * 31U + 1U));
        const std::uint32_t h2 = NextHashUVE(h1 + 17U);
        const std::uint32_t h3 = NextHashUVE(h2 + 29U);
        chunk.initialAngularVelocity = Math::Vector3UVE{
            UnitSignedUVE(h1) * 4.0F,
            UnitSignedUVE(h2) * 4.0F,
            UnitSignedUVE(h3) * 4.0F,
        };
        chunks.push_back(std::move(chunk));
    }
    return chunks;
}

bool ApplyDestructibleImpactUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                const float impulse, const Math::Vector3UVE& impactPointWorld,
                                std::vector<EntityUVE>* const outDebrisEntities) {
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<Destructible3DComponentUVE>(entity) ||
        !std::isfinite(impulse) || impulse <= 0.0F) {
        return false;
    }
    auto& dest = entityManager.GetComponentUVE<Destructible3DComponentUVE>(entity);
    if (!dest.enabled || dest.fractured) {
        return false;
    }
    dest.health = std::max(0.0F, dest.health - impulse);
    if (dest.health > 0.0F && impulse < dest.impactImpulseThreshold) {
        return false;
    }

    Math::Vector3UVE originWorld{0.0F, 0.0F, 0.0F};
    if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
        originWorld = entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).worldPosition;
    } else if (entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
        originWorld = entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition;
    }

    const Math::Vector3UVE impactLocal = impactPointWorld - originWorld;
    const auto chunks = FractureBoundsIntoChunksUVE(dest.localBounds, dest.chunkCount, dest.totalMass,
                                                    dest.fractureSeed, impactLocal, dest.explosionImpulse);
    dest.fractured = true;
    if (entityManager.HasComponentUVE<VisibilityComponentUVE>(entity)) {
        auto& vis = entityManager.GetComponentUVE<VisibilityComponentUVE>(entity);
        vis.visible = false;
        vis.visibleInHierarchy = false;
    }

    for (const DestructibleChunkUVE& chunk : chunks) {
        const EntityUVE debrisEntity = entityManager.CreateEntityUVE();
        TransformComponentUVE tr{};
        tr.localPosition = originWorld + chunk.localCentroid;
        entityManager.AddComponentUVE<TransformComponentUVE>(debrisEntity, tr);
        WorldTransformComponentUVE wt{};
        wt.worldPosition = tr.localPosition;
        wt.dirty = false;
        entityManager.AddComponentUVE<WorldTransformComponentUVE>(debrisEntity, wt);

        DestructibleDebrisComponentUVE debris{};
        debris.sourceEntity = entity;
        debris.halfExtents = chunk.halfExtents;
        debris.linearVelocity = chunk.initialLinearVelocity;
        debris.angularVelocity = chunk.initialAngularVelocity;
        debris.mass = chunk.mass;
        debris.remainingLifetimeSeconds = std::max(dest.debrisLifetimeSeconds, 0.1F);
        entityManager.AddComponentUVE<DestructibleDebrisComponentUVE>(debrisEntity, debris);
        if (outDebrisEntities != nullptr) {
            outDebrisEntities->push_back(debrisEntity);
        }
    }
    return true;
}

std::size_t UpdateDestructibleDebrisUVE(IEntityManagerUVE& entityManager,
                                        const Math::Vector3UVE& gravity,
                                        const float deltaTimeSeconds) {
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return 0U;
    }
    std::vector<EntityUVE> expired;
    std::size_t aliveCount = 0U;

    entityManager.ForEachUVE<DestructibleDebrisComponentUVE, TransformComponentUVE>(
        [&](const EntityUVE entity, DestructibleDebrisComponentUVE& debris,
            TransformComponentUVE& tr) {
            debris.remainingLifetimeSeconds -= deltaTimeSeconds;
            if (debris.remainingLifetimeSeconds <= 0.0F) {
                expired.push_back(entity);
                return;
            }
            debris.linearVelocity = debris.linearVelocity + gravity * deltaTimeSeconds;
            tr.localPosition = tr.localPosition + debris.linearVelocity * deltaTimeSeconds;
            const float angSpeed = Math::LengthUVE(debris.angularVelocity);
            if (angSpeed > 1.0e-5F) {
                const Math::Vector3UVE axis = debris.angularVelocity * (1.0F / angSpeed);
                Math::QuaternionUVE dq{};
                if (Math::TryMakeAxisAngleUVE(axis, angSpeed * deltaTimeSeconds, dq)) {
                    Math::QuaternionUVE nextRot{};
                    if (Math::TryNormalizeUVE(Math::MultiplyUVE(dq, tr.localRotation), nextRot)) {
                        tr.localRotation = nextRot;
                    }
                }
            }
            if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
                entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).dirty = true;
            }
            ++aliveCount;
        });

    for (const EntityUVE e : expired) {
        if (entityManager.IsAliveUVE(e)) {
            entityManager.DestroyEntityUVE(e);
        }
    }
    return aliveCount;
}

} // namespace UVE::Scene
