// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/spring_bone_simulator_3d_uve.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "uve/component/disabled_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_animation_objects_3d_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsSpringBoneCollision3DComponentValidUVE(
    const SpringBoneCollision3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.centerOffset) && std::isfinite(value.radius) &&
           value.radius > 0.0F;
}

SpringBoneCollision3DComponentUVE SanitizeSpringBoneCollision3DComponentUVE(
    const SpringBoneCollision3DComponentUVE& value) noexcept {
    SpringBoneCollision3DComponentUVE out = value;
    if (!IsFinite3DObjectVectorUVE(out.centerOffset)) {
        out.centerOffset = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
    if (!std::isfinite(out.radius) || out.radius <= 0.0F) {
        out.radius = 0.25F;
    }
    return out;
}

bool IsSpringBoneCollision3DObjectDefinitionValidUVE(
    const SpringBoneCollision3DObjectDefinitionUVE& value) noexcept {
    return IsSpringBoneCollision3DComponentValidUVE(value.collision);
}

void ApplySpringBoneCollision3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const SpringBoneCollision3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity,
                              SpringBoneCollision3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<SpringBoneCollision3DComponentUVE>(
        entity, SanitizeSpringBoneCollision3DComponentUVE(value.collision));
}

bool IsSpringBoneSimulator3DComponentValidUVE(
    const SpringBoneSimulator3DComponentUVE& value) noexcept {
    return IsBounded3DObjectStringUVE(value.boneName) &&
           IsFinite3DObjectVectorUVE(value.tipOffsetLocal) &&
           Math::LengthSquaredUVE(value.tipOffsetLocal) > 1.0e-8F &&
           std::isfinite(value.stiffness) && value.stiffness >= 0.0F &&
           std::isfinite(value.damping) && value.damping >= 0.0F && value.damping <= 1.0F &&
           std::isfinite(value.gravityScale) &&
           IsFinite3DObjectVectorUVE(value.gravityDirection) &&
           Math::LengthSquaredUVE(value.gravityDirection) > 1.0e-8F &&
           std::isfinite(value.hitRadius) && value.hitRadius >= 0.0F;
}

SpringBoneSimulator3DComponentUVE SanitizeSpringBoneSimulator3DComponentUVE(
    const SpringBoneSimulator3DComponentUVE& value) noexcept {
    SpringBoneSimulator3DComponentUVE out = value;
    if (!IsBounded3DObjectStringUVE(out.boneName)) {
        out.boneName.resize(kMaximum3DObjectStringLengthUVE);
    }
    if (!IsFinite3DObjectVectorUVE(out.tipOffsetLocal) ||
        Math::LengthSquaredUVE(out.tipOffsetLocal) <= 1.0e-8F) {
        out.tipOffsetLocal = Math::Vector3UVE{0.0F, -0.25F, 0.0F};
    }
    if (!std::isfinite(out.stiffness) || out.stiffness < 0.0F) {
        out.stiffness = 15.0F;
    }
    if (!std::isfinite(out.damping)) {
        out.damping = 0.35F;
    } else {
        out.damping = std::clamp(out.damping, 0.0F, 1.0F);
    }
    if (!std::isfinite(out.gravityScale)) {
        out.gravityScale = 0.0F;
    }
    if (!IsFinite3DObjectVectorUVE(out.gravityDirection) ||
        Math::LengthSquaredUVE(out.gravityDirection) <= 1.0e-8F) {
        out.gravityDirection = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
    }
    if (!std::isfinite(out.hitRadius) || out.hitRadius < 0.0F) {
        out.hitRadius = 0.05F;
    }
    return out;
}

void StepSpringBoneTipStateUVE(SpringBoneSimulator3DComponentUVE& simulator,
                               const Math::Vector3UVE& pivotWorldPosition,
                               const Math::Vector3UVE& restTipWorldPosition,
                               const float deltaTimeSeconds) noexcept {
    if (!IsFinite3DObjectVectorUVE(pivotWorldPosition) ||
        !IsFinite3DObjectVectorUVE(restTipWorldPosition)) {
        return;
    }
    if (!simulator.initialized || !IsFinite3DObjectVectorUVE(simulator.currentTipWorldPosition) ||
        !IsFinite3DObjectVectorUVE(simulator.tipVelocity)) {
        simulator.currentTipWorldPosition = restTipWorldPosition;
        simulator.tipVelocity = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
        simulator.initialized = true;
        return;
    }
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return;
    }
    const float dt = std::min(deltaTimeSeconds, 0.1F);
    const Math::Vector3UVE gravDir =
        (Math::LengthSquaredUVE(simulator.gravityDirection) > 1.0e-8F)
            ? Math::NormalizeUVE(simulator.gravityDirection)
            : Math::Vector3UVE{0.0F, -1.0F, 0.0F};
    const Math::Vector3UVE springForce =
        (restTipWorldPosition - simulator.currentTipWorldPosition) * simulator.stiffness +
        gravDir * simulator.gravityScale;
    const float velocityDecay = std::clamp(1.0F - simulator.damping, 0.0F, 1.0F);
    simulator.tipVelocity = simulator.tipVelocity * velocityDecay + springForce * dt;
    Math::Vector3UVE nextTip = simulator.currentTipWorldPosition + simulator.tipVelocity * dt;

    const float linkLen = std::sqrt(Math::LengthSquaredUVE(restTipWorldPosition - pivotWorldPosition));
    if (linkLen > 1.0e-6F) {
        const Math::Vector3UVE fromPivot = nextTip - pivotWorldPosition;
        const float distSq = Math::LengthSquaredUVE(fromPivot);
        if (distSq > 1.0e-8F) {
            nextTip = pivotWorldPosition + fromPivot * (linkLen / std::sqrt(distSq));
        } else {
            nextTip = restTipWorldPosition;
        }
    }
    simulator.currentTipWorldPosition = nextTip;
}

bool ResolveSpringBoneSphereCollisionUVE(SpringBoneSimulator3DComponentUVE& simulator,
                                         const Math::Vector3UVE& pivotWorldPosition,
                                         const Math::Vector3UVE& sphereCenterWorld,
                                         const float sphereRadius) noexcept {
    if (!simulator.initialized || !std::isfinite(sphereRadius) || sphereRadius <= 0.0F) {
        return false;
    }
    const float combinedRadius = sphereRadius + std::max(0.0F, simulator.hitRadius);
    const Math::Vector3UVE delta = simulator.currentTipWorldPosition - sphereCenterWorld;
    const float distSq = Math::LengthSquaredUVE(delta);
    if (distSq >= combinedRadius * combinedRadius) {
        return false;
    }
    const Math::Vector3UVE boneDir = simulator.currentTipWorldPosition - pivotWorldPosition;
    const float linkLen = std::sqrt(Math::LengthSquaredUVE(boneDir));
    Math::Vector3UVE pushNormal =
        (distSq > 1.0e-8F) ? (delta * (1.0F / std::sqrt(distSq)))
                           : Math::Vector3UVE{0.0F, 0.0F, 1.0F};
    if (linkLen > 1.0e-6F) {
        const Math::Vector3UVE unitBone = boneDir * (1.0F / linkLen);
        if (std::fabs(Math::DotUVE(pushNormal, unitBone)) > 0.99F) {
            const Math::Vector3UVE fallbackAxis =
                (std::fabs(unitBone.x) < 0.9F) ? Math::Vector3UVE{1.0F, 0.0F, 0.0F}
                                               : Math::Vector3UVE{0.0F, 0.0F, 1.0F};
            pushNormal = Math::NormalizeUVE(Math::CrossUVE(unitBone, fallbackAxis));
        }
    }
    Math::Vector3UVE pushed = sphereCenterWorld + pushNormal * combinedRadius;
    if (linkLen > 1.0e-6F) {
        const Math::Vector3UVE fromPivot = pushed - pivotWorldPosition;
        const float fromPivotSq = Math::LengthSquaredUVE(fromPivot);
        if (fromPivotSq > 1.0e-8F) {
            pushed = pivotWorldPosition + fromPivot * (linkLen / std::sqrt(fromPivotSq));
        }
    }
    simulator.currentTipWorldPosition = pushed;
    return true;
}

bool IsSpringBoneSimulator3DObjectDefinitionValidUVE(
    const SpringBoneSimulator3DObjectDefinitionUVE& value) noexcept {
    return IsSpringBoneSimulator3DComponentValidUVE(value.simulator);
}

void ApplySpringBoneSimulator3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const SpringBoneSimulator3DObjectDefinitionUVE& value) {
    ApplyBoneModifier3DBaseUVE(entityManager, entity,
                               SpringBoneSimulator3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<SpringBoneSimulator3DComponentUVE>(
        entity, SanitizeSpringBoneSimulator3DComponentUVE(value.simulator));
}

std::size_t UpdateSpringBoneSimulator3DObjectsUVE(IEntityManagerUVE& entityManager,
                                                  const float deltaTimeSeconds) {
    struct SphereObstacleUVE final {
        Math::Vector3UVE center{};
        float radius = 0.0F;
    };
    std::vector<SphereObstacleUVE> obstacles;
    entityManager.ForEachUVE<SpringBoneCollision3DComponentUVE, WorldTransformComponentUVE>(
        [&](const EntityUVE entity, const SpringBoneCollision3DComponentUVE& col,
            const WorldTransformComponentUVE& world) {
            if (!col.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsSpringBoneCollision3DComponentValidUVE(col) ||
                !IsWorldTransformComponentValidUVE(world)) {
                return;
            }
            obstacles.push_back(SphereObstacleUVE{
                TransformWorldPointUVE(world, col.centerOffset), col.radius});
        });

    std::size_t steppedCount = 0U;
    entityManager.ForEachUVE<SpringBoneSimulator3DComponentUVE>(
        [&](const EntityUVE entity, SpringBoneSimulator3DComponentUVE& sim) {
            if (!sim.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsSpringBoneSimulator3DComponentValidUVE(sim) ||
                !entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
                sim.initialized = false;
                return;
            }
            const WorldTransformComponentUVE& world =
                entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity);
            if (!IsWorldTransformComponentValidUVE(world)) {
                sim.initialized = false;
                return;
            }
            const Math::Vector3UVE pivotWorld = world.worldPosition;
            const Math::Vector3UVE restTipWorld = TransformWorldPointUVE(world, sim.tipOffsetLocal);
            StepSpringBoneTipStateUVE(sim, pivotWorld, restTipWorld, deltaTimeSeconds);
            for (const SphereObstacleUVE& obs : obstacles) {
                static_cast<void>(
                    ResolveSpringBoneSphereCollisionUVE(sim, pivotWorld, obs.center, obs.radius));
            }
            ++steppedCount;
        });
    return steppedCount;
}

} // namespace UVE::Scene
