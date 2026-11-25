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

[[nodiscard]] std::uint32_t NextHash(std::uint32_t x) noexcept {
    x ^= x >> 16U;
    x *= 0x7FEB352DU;
    x ^= x >> 15U;
    x *= 0x846CA68BU;
    x ^= x >> 16U;
    return x;
}

[[nodiscard]] float UnitSigned(const std::uint32_t h) noexcept {
    return (static_cast<float>(h & 0x00FFFFFFU) / static_cast<float>(0x00800000U)) - 1.0F;
}

void BuildBoxChunkGeometry(const Math::Vector3& halfExtents,
                              std::vector<Math::Vector3>& outVerts,
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

std::vector<DestructibleChunk> FractureBoundsIntoChunks(
    const Math::Aabb& localBounds, const std::uint32_t chunkCount, const float totalMass,
    const std::uint32_t seed, const Math::Vector3& impactPointLocal,
    const float explosionImpulse) {
    const std::uint32_t targetCount = std::clamp(chunkCount, 2U, 64U);
    const Math::Vector3 fullSize = localBounds.max - localBounds.min;
    if (fullSize.x <= 1.0e-5F || fullSize.y <= 1.0e-5F || fullSize.z <= 1.0e-5F) {
        return {};
    }

    // Deterministic binary space partitioning along the longest axis of each box cell until we
    // reach `targetCount` airtight convex cells conserving 100% of total bounds volume.
    std::vector<Math::Aabb> cells;
    cells.reserve(targetCount);
    cells.push_back(localBounds);

    std::uint32_t rng = seed ^ 0x9E3779B9U;
    while (cells.size() < targetCount) {
        std::size_t bestIdx = 0U;
        float bestVol = -1.0F;
        for (std::size_t i = 0U; i < cells.size(); ++i) {
            const Math::Vector3 s = cells[i].max - cells[i].min;
            const float vol = s.x * s.y * s.z;
            if (vol > bestVol) {
                bestVol = vol;
                bestIdx = i;
            }
        }
        const Math::Aabb box = cells[bestIdx];
        const Math::Vector3 s = box.max - box.min;
        rng = NextHash(rng + static_cast<std::uint32_t>(cells.size()));
        const float splitT = 0.35F + 0.30F * (0.5F + 0.5F * UnitSigned(rng));

        Math::Aabb left = box;
        Math::Aabb right = box;
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
    std::vector<DestructibleChunk> chunks;
    chunks.reserve(cells.size());

    for (std::size_t i = 0U; i < cells.size(); ++i) {
        const Math::Aabb& cell = cells[i];
        const Math::Vector3 size = cell.max - cell.min;
        const float vol = std::max(size.x * size.y * size.z, 1.0e-6F);
        DestructibleChunk chunk{};
        chunk.localCentroid = (cell.min + cell.max) * 0.5F;
        chunk.halfExtents = size * 0.5F;
        chunk.mass = std::max(safeMass * (vol / totalVolume), 0.05F);
        BuildBoxChunkGeometry(chunk.halfExtents, chunk.vertices, chunk.indices);

        Math::Vector3 radial = chunk.localCentroid - impactPointLocal;
        const float radialLen = Math::Length(radial);
        if (radialLen > 1.0e-4F) {
            radial = radial * (1.0F / radialLen);
        } else {
            radial = Math::Vector3{0.0F, 1.0F, 0.0F};
        }
        const float speed = std::max(explosionImpulse, 0.0F) / std::sqrt(chunk.mass);
        chunk.initialLinearVelocity = radial * speed + Math::Vector3{0.0F, speed * 0.25F, 0.0F};

        const std::uint32_t h1 = NextHash(seed ^ static_cast<std::uint32_t>(i * 31U + 1U));
        const std::uint32_t h2 = NextHash(h1 + 17U);
        const std::uint32_t h3 = NextHash(h2 + 29U);
        chunk.initialAngularVelocity = Math::Vector3{
            UnitSigned(h1) * 4.0F,
            UnitSigned(h2) * 4.0F,
            UnitSigned(h3) * 4.0F,
        };
        chunks.push_back(std::move(chunk));
    }
    return chunks;
}

bool ApplyDestructibleImpact(IEntityManager& entityManager, const Entity entity,
                                const float impulse, const Math::Vector3& impactPointWorld,
                                std::vector<Entity>* const outDebrisEntities) {
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<Destructible3DComponent>(entity) ||
        !std::isfinite(impulse) || impulse <= 0.0F) {
        return false;
    }
    auto& dest = entityManager.GetComponent<Destructible3DComponent>(entity);
    if (!dest.enabled || dest.fractured) {
        return false;
    }
    dest.health = std::max(0.0F, dest.health - impulse);
    if (dest.health > 0.0F && impulse < dest.impactImpulseThreshold) {
        return false;
    }

    Math::Vector3 originWorld{0.0F, 0.0F, 0.0F};
    if (entityManager.HasComponent<WorldTransformComponent>(entity)) {
        originWorld = entityManager.GetComponent<WorldTransformComponent>(entity).worldPosition;
    } else if (entityManager.HasComponent<TransformComponent>(entity)) {
        originWorld = entityManager.GetComponent<TransformComponent>(entity).localPosition;
    }

    const Math::Vector3 impactLocal = impactPointWorld - originWorld;
    const auto chunks = FractureBoundsIntoChunks(dest.localBounds, dest.chunkCount, dest.totalMass,
                                                    dest.fractureSeed, impactLocal, dest.explosionImpulse);
    dest.fractured = true;
    if (entityManager.HasComponent<VisibilityComponent>(entity)) {
        auto& vis = entityManager.GetComponent<VisibilityComponent>(entity);
        vis.visible = false;
        vis.visibleInHierarchy = false;
    }

    for (const DestructibleChunk& chunk : chunks) {
        const Entity debrisEntity = entityManager.CreateEntity();
        TransformComponent tr{};
        tr.localPosition = originWorld + chunk.localCentroid;
        entityManager.AddComponent<TransformComponent>(debrisEntity, tr);
        WorldTransformComponent wt{};
        wt.worldPosition = tr.localPosition;
        wt.dirty = false;
        entityManager.AddComponent<WorldTransformComponent>(debrisEntity, wt);

        DestructibleDebrisComponent debris{};
        debris.sourceEntity = entity;
        debris.halfExtents = chunk.halfExtents;
        debris.linearVelocity = chunk.initialLinearVelocity;
        debris.angularVelocity = chunk.initialAngularVelocity;
        debris.mass = chunk.mass;
        debris.remainingLifetimeSeconds = std::max(dest.debrisLifetimeSeconds, 0.1F);
        entityManager.AddComponent<DestructibleDebrisComponent>(debrisEntity, debris);
        if (outDebrisEntities != nullptr) {
            outDebrisEntities->push_back(debrisEntity);
        }
    }
    return true;
}

std::size_t UpdateDestructibleDebris(IEntityManager& entityManager,
                                        const Math::Vector3& gravity,
                                        const float deltaTimeSeconds) {
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return 0U;
    }
    std::vector<Entity> expired;
    std::size_t aliveCount = 0U;

    entityManager.ForEach<DestructibleDebrisComponent, TransformComponent>(
        [&](const Entity entity, DestructibleDebrisComponent& debris,
            TransformComponent& tr) {
            debris.remainingLifetimeSeconds -= deltaTimeSeconds;
            if (debris.remainingLifetimeSeconds <= 0.0F) {
                expired.push_back(entity);
                return;
            }
            debris.linearVelocity = debris.linearVelocity + gravity * deltaTimeSeconds;
            tr.localPosition = tr.localPosition + debris.linearVelocity * deltaTimeSeconds;
            const float angSpeed = Math::Length(debris.angularVelocity);
            if (angSpeed > 1.0e-5F) {
                const Math::Vector3 axis = debris.angularVelocity * (1.0F / angSpeed);
                Math::Quaternion dq{};
                if (Math::TryMakeAxisAngle(axis, angSpeed * deltaTimeSeconds, dq)) {
                    Math::Quaternion nextRot{};
                    if (Math::TryNormalize(Math::Multiply(dq, tr.localRotation), nextRot)) {
                        tr.localRotation = nextRot;
                    }
                }
            }
            if (entityManager.HasComponent<WorldTransformComponent>(entity)) {
                entityManager.GetComponent<WorldTransformComponent>(entity).dirty = true;
            }
            ++aliveCount;
        });

    for (const Entity e : expired) {
        if (entityManager.IsAlive(e)) {
            entityManager.DestroyEntity(e);
        }
    }
    return aliveCount;
}

} // namespace UVE::Scene
