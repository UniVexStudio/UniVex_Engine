// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/2d/physics_2d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/disabled_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] Math::Vector2UVE ResolveEntity2DPosUVE(const IEntityManagerUVE& entityManager,
                                                     const EntityUVE entity) noexcept {
    if (entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
        const Math::Vector3UVE& p =
            entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition;
        return Math::Vector2UVE{p.x, p.y};
    }
    if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
        const Math::Vector3UVE& p =
            entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).worldPosition;
        return Math::Vector2UVE{p.x, p.y};
    }
    return Math::Vector2UVE{0.0F, 0.0F};
}

} // namespace

bool IsCollisionShape2DComponentValidUVE(const CollisionShape2DComponentUVE& value) noexcept {
    return static_cast<std::uint8_t>(value.shapeKind) <=
               static_cast<std::uint8_t>(CollisionShape2DKindUVE::Capsule) &&
           std::isfinite(value.radius) && value.radius > 0.0F &&
           Math::IsFiniteUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.collisionLayer != 0U && value.collisionMask != 0U;
}

CollisionShape2DComponentUVE SanitizeCollisionShape2DComponentUVE(
    const CollisionShape2DComponentUVE& value) noexcept {
    CollisionShape2DComponentUVE out = value;
    if (static_cast<std::uint8_t>(out.shapeKind) >
        static_cast<std::uint8_t>(CollisionShape2DKindUVE::Capsule)) {
        out.shapeKind = CollisionShape2DKindUVE::Circle;
    }
    if (!std::isfinite(out.radius) || out.radius <= 0.0F) {
        out.radius = 16.0F;
    }
    if (!Math::IsFiniteUVE(out.halfExtents) || out.halfExtents.x <= 0.0F ||
        out.halfExtents.y <= 0.0F) {
        out.halfExtents = Math::Vector2UVE{16.0F, 16.0F};
    }
    if (out.collisionLayer == 0U) {
        out.collisionLayer = 1U;
    }
    if (out.collisionMask == 0U) {
        out.collisionMask = 1U;
    }
    return out;
}

void ApplyCollisionShape2DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                              const EntityUVE entity,
                                              const CollisionShape2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<CollisionShape2DComponentUVE>(
        entity, SanitizeCollisionShape2DComponentUVE(value.shape));
}

bool IsCollisionPolygon2DComponentValidUVE(const CollisionPolygon2DComponentUVE& value) noexcept {
    if (value.vertices.size() > 512U || value.collisionLayer == 0U || value.collisionMask == 0U) {
        return false;
    }
    for (const Math::Vector2UVE& v : value.vertices) {
        if (!Math::IsFiniteUVE(v)) {
            return false;
        }
    }
    return true;
}

CollisionPolygon2DComponentUVE SanitizeCollisionPolygon2DComponentUVE(
    const CollisionPolygon2DComponentUVE& value) {
    CollisionPolygon2DComponentUVE out = value;
    if (out.vertices.size() > 512U) {
        out.vertices.resize(512U);
    }
    for (Math::Vector2UVE& v : out.vertices) {
        if (!Math::IsFiniteUVE(v)) {
            v = Math::Vector2UVE{0.0F, 0.0F};
        }
    }
    if (out.collisionLayer == 0U) {
        out.collisionLayer = 1U;
    }
    if (out.collisionMask == 0U) {
        out.collisionMask = 1U;
    }
    return out;
}

void ApplyCollisionPolygon2DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const CollisionPolygon2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<CollisionPolygon2DComponentUVE>(
        entity, SanitizeCollisionPolygon2DComponentUVE(value.polygon));
}

bool IsArea2DComponentValidUVE(const Area2DComponentUVE& value) noexcept {
    return Math::IsFiniteUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.collisionLayer != 0U && value.collisionMask != 0U;
}

Area2DComponentUVE SanitizeArea2DComponentUVE(const Area2DComponentUVE& value) noexcept {
    Area2DComponentUVE out = value;
    if (!Math::IsFiniteUVE(out.halfExtents) || out.halfExtents.x <= 0.0F ||
        out.halfExtents.y <= 0.0F) {
        out.halfExtents = Math::Vector2UVE{32.0F, 32.0F};
    }
    if (out.collisionLayer == 0U) {
        out.collisionLayer = 1U;
    }
    if (out.collisionMask == 0U) {
        out.collisionMask = 1U;
    }
    return out;
}

void ApplyArea2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                    const Area2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<Area2DComponentUVE>(entity,
                                                      SanitizeArea2DComponentUVE(value.area));
}

bool IsPhysicsBody2DComponentValidUVE(const PhysicsBody2DComponentUVE& value) noexcept {
    return static_cast<std::uint8_t>(value.kind) <=
               static_cast<std::uint8_t>(Body2DKindUVE::Character) &&
           std::isfinite(value.mass) && value.mass > 0.0F && std::isfinite(value.gravityScale) &&
           std::isfinite(value.linearDamping) && value.linearDamping >= 0.0F &&
           value.linearDamping <= 1.0F && Math::IsFiniteUVE(value.velocity);
}

PhysicsBody2DComponentUVE SanitizePhysicsBody2DComponentUVE(
    const PhysicsBody2DComponentUVE& value) noexcept {
    PhysicsBody2DComponentUVE out = value;
    if (static_cast<std::uint8_t>(out.kind) >
        static_cast<std::uint8_t>(Body2DKindUVE::Character)) {
        out.kind = Body2DKindUVE::Rigid;
    }
    if (!std::isfinite(out.mass) || out.mass <= 0.0F) {
        out.mass = 1.0F;
    }
    if (!std::isfinite(out.gravityScale)) {
        out.gravityScale = 1.0F;
    }
    if (!std::isfinite(out.linearDamping)) {
        out.linearDamping = 0.05F;
    } else {
        out.linearDamping = std::clamp(out.linearDamping, 0.0F, 1.0F);
    }
    if (!Math::IsFiniteUVE(out.velocity)) {
        out.velocity = Math::Vector2UVE{0.0F, 0.0F};
    }
    return out;
}

void ApplyStatic2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                      const Static2DObjectDefinitionUVE& value) {
    PhysicsBody2DComponentUVE sanitized = SanitizePhysicsBody2DComponentUVE(value.body);
    sanitized.kind = Body2DKindUVE::Static;
    entityManager.AddComponentUVE<PhysicsBody2DComponentUVE>(entity, sanitized);
}

void ApplyRigid2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                     const Rigid2DObjectDefinitionUVE& value) {
    PhysicsBody2DComponentUVE sanitized = SanitizePhysicsBody2DComponentUVE(value.body);
    sanitized.kind = Body2DKindUVE::Rigid;
    entityManager.AddComponentUVE<PhysicsBody2DComponentUVE>(entity, sanitized);
}

void ApplyCharacter2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                         const Character2DObjectDefinitionUVE& value) {
    PhysicsBody2DComponentUVE sanitized = SanitizePhysicsBody2DComponentUVE(value.body);
    sanitized.kind = Body2DKindUVE::Character;
    entityManager.AddComponentUVE<PhysicsBody2DComponentUVE>(entity, sanitized);
}

void ApplyRayCast2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                       const RayCast2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<RayCast2DComponentUVE>(entity, value.rayCast);
}

void ApplyShapeCast2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                         const ShapeCast2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<ShapeCast2DComponentUVE>(entity, value.shapeCast);
}

void ApplyPinJoint2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                        const PinJoint2DObjectDefinitionUVE& value) {
    Joint2DComponentUVE j = value.joint;
    j.kind = Joint2DKindUVE::Pin;
    entityManager.AddComponentUVE<Joint2DComponentUVE>(entity, j);
}

void ApplyGrooveJoint2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                           const GrooveJoint2DObjectDefinitionUVE& value) {
    Joint2DComponentUVE j = value.joint;
    j.kind = Joint2DKindUVE::Groove;
    entityManager.AddComponentUVE<Joint2DComponentUVE>(entity, j);
}

void ApplyDampedSpringJoint2DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const DampedSpringJoint2DObjectDefinitionUVE& value) {
    Joint2DComponentUVE j = value.joint;
    j.kind = Joint2DKindUVE::DampedSpring;
    entityManager.AddComponentUVE<Joint2DComponentUVE>(entity, j);
}

std::size_t UpdatePhysics2DObjectsUVE(IEntityManagerUVE& entityManager,
                                      const float deltaTimeSeconds,
                                      const Math::Vector2UVE& gravity2D) {
    std::size_t activeStepCount = 0U;
    const float dt =
        (std::isfinite(deltaTimeSeconds) && deltaTimeSeconds > 0.0F) ? deltaTimeSeconds : 0.0F;
    const Math::Vector2UVE safeGrav =
        Math::IsFiniteUVE(gravity2D) ? gravity2D : Math::Vector2UVE{0.0F, -980.0F};

    // 1. Step Dynamic/Character 2D bodies
    if (dt > 0.0F) {
        entityManager.ForEachUVE<PhysicsBody2DComponentUVE, TransformComponentUVE>(
            [&](const EntityUVE entity, PhysicsBody2DComponentUVE& body,
                TransformComponentUVE& local) {
                if (!body.enabled ||
                    entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                    body.kind == Body2DKindUVE::Static ||
                    !IsPhysicsBody2DComponentValidUVE(body)) {
                    return;
                }
                if (body.kind == Body2DKindUVE::Rigid) {
                    const float damp = std::clamp(1.0F - body.linearDamping * dt, 0.0F, 1.0F);
                    body.velocity =
                        (body.velocity + safeGrav * (body.gravityScale * dt)) * damp;
                }
                local.localPosition.x += body.velocity.x * dt;
                local.localPosition.y += body.velocity.y * dt;
                body.grounded = (body.velocity.y <= 0.0F && local.localPosition.y <= 0.0F);
                if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
                    entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).dirty = true;
                }
                ++activeStepCount;
            });

        // 2. Solve 2D joints (Pin / Groove / DampedSpring)
        entityManager.ForEachUVE<Joint2DComponentUVE>(
            [&](const EntityUVE jointEntity, const Joint2DComponentUVE& joint) {
                if (!joint.enabled ||
                    entityManager.HasComponentUVE<DisabledComponentUVE>(jointEntity) ||
                    joint.nodeA == kInvalidEntityUVE || joint.nodeB == kInvalidEntityUVE ||
                    joint.nodeA == joint.nodeB || !entityManager.IsAliveUVE(joint.nodeA) ||
                    !entityManager.IsAliveUVE(joint.nodeB) ||
                    !entityManager.HasComponentUVE<TransformComponentUVE>(joint.nodeA) ||
                    !entityManager.HasComponentUVE<TransformComponentUVE>(joint.nodeB)) {
                    return;
                }
                TransformComponentUVE& trA =
                    entityManager.GetComponentUVE<TransformComponentUVE>(joint.nodeA);
                TransformComponentUVE& trB =
                    entityManager.GetComponentUVE<TransformComponentUVE>(joint.nodeB);
                const Math::Vector2UVE posA{trA.localPosition.x, trA.localPosition.y};
                const Math::Vector2UVE posB{trB.localPosition.x, trB.localPosition.y};
                const Math::Vector2UVE delta = posB - posA;
                const float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                if (dist <= 1.0e-5F) {
                    return;
                }
                const float targetLen = std::max(0.0F, joint.restLength);
                const float error = dist - targetLen;
                const Math::Vector2UVE dir = delta * (1.0F / dist);
                const Math::Vector2UVE correction = dir * (0.5F * error);
                trA.localPosition.x += correction.x;
                trA.localPosition.y += correction.y;
                trB.localPosition.x -= correction.x;
                trB.localPosition.y -= correction.y;
                ++activeStepCount;
            });
    }

    // 3. Snapshot 2D colliders for Area2D & RayCast2D / ShapeCast2D
    struct Collider2DSnapshotUVE final {
        EntityUVE entity;
        Math::Vector2UVE center;
        float radius;
        std::uint32_t layer;
    };
    std::vector<Collider2DSnapshotUVE> colliders;
    entityManager.ForEachUVE<CollisionShape2DComponentUVE>(
        [&](const EntityUVE entity, const CollisionShape2DComponentUVE& col) {
            if (!col.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsCollisionShape2DComponentValidUVE(col)) {
                return;
            }
            const float effRadius = (col.shapeKind == CollisionShape2DKindUVE::Rectangle)
                                        ? std::max(col.halfExtents.x, col.halfExtents.y)
                                        : col.radius;
            colliders.push_back(Collider2DSnapshotUVE{
                entity, ResolveEntity2DPosUVE(entityManager, entity), effRadius, col.collisionLayer});
        });

    // 4. Area2D overlaps
    entityManager.ForEachUVE<Area2DComponentUVE>(
        [&](const EntityUVE areaEntity, Area2DComponentUVE& area) {
            area.overlappingEntities.clear();
            if (!area.monitoring ||
                entityManager.HasComponentUVE<DisabledComponentUVE>(areaEntity) ||
                !IsArea2DComponentValidUVE(area)) {
                return;
            }
            const Math::Vector2UVE center = ResolveEntity2DPosUVE(entityManager, areaEntity);
            for (const Collider2DSnapshotUVE& col : colliders) {
                if (col.entity == areaEntity || (area.collisionMask & col.layer) == 0U) {
                    continue;
                }
                const float dx = std::fabs(col.center.x - center.x);
                const float dy = std::fabs(col.center.y - center.y);
                if (dx <= area.halfExtents.x + col.radius &&
                    dy <= area.halfExtents.y + col.radius) {
                    area.overlappingEntities.push_back(col.entity);
                }
            }
        });

    // 5. RayCast2D & ShapeCast2D
    entityManager.ForEachUVE<RayCast2DComponentUVE>(
        [&](const EntityUVE rayEntity, RayCast2DComponentUVE& rc) {
            rc.hit = false;
            rc.hitEntity = kInvalidEntityUVE;
            rc.hitDistance = 0.0F;
            if (!rc.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(rayEntity) ||
                !Math::IsFiniteUVE(rc.targetPosition)) {
                return;
            }
            const Math::Vector2UVE origin = ResolveEntity2DPosUVE(entityManager, rayEntity);
            const float len = std::sqrt(rc.targetPosition.x * rc.targetPosition.x +
                                        rc.targetPosition.y * rc.targetPosition.y);
            if (len <= 1.0e-5F) {
                return;
            }
            const Math::Vector2UVE dir = rc.targetPosition * (1.0F / len);
            float bestDist = len + 1.0F;
            for (const Collider2DSnapshotUVE& col : colliders) {
                if (col.entity == rayEntity || (rc.collisionMask & col.layer) == 0U) {
                    continue;
                }
                const Math::Vector2UVE toCenter = col.center - origin;
                const float proj = toCenter.x * dir.x + toCenter.y * dir.y;
                if (proj < 0.0F || proj > len + col.radius) {
                    continue;
                }
                const Math::Vector2UVE closest = origin + dir * std::clamp(proj, 0.0F, len);
                const Math::Vector2UVE diff = col.center - closest;
                if (diff.x * diff.x + diff.y * diff.y <= col.radius * col.radius) {
                    const float hitDist = std::max(0.0F, proj - col.radius);
                    if (hitDist < bestDist) {
                        bestDist = hitDist;
                        rc.hit = true;
                        rc.hitEntity = col.entity;
                        rc.hitDistance = hitDist;
                        rc.hitPoint = origin + dir * hitDist;
                    }
                }
            }
        });

    entityManager.ForEachUVE<ShapeCast2DComponentUVE>(
        [&](const EntityUVE castEntity, ShapeCast2DComponentUVE& sc) {
            sc.hit = false;
            sc.hitEntity = kInvalidEntityUVE;
            sc.hitDistance = 0.0F;
            if (!sc.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(castEntity) ||
                !Math::IsFiniteUVE(sc.targetPosition) || !std::isfinite(sc.radius) ||
                sc.radius <= 0.0F) {
                return;
            }
            const Math::Vector2UVE origin = ResolveEntity2DPosUVE(entityManager, castEntity);
            const float len = std::sqrt(sc.targetPosition.x * sc.targetPosition.x +
                                        sc.targetPosition.y * sc.targetPosition.y);
            if (len <= 1.0e-5F) {
                return;
            }
            const Math::Vector2UVE dir = sc.targetPosition * (1.0F / len);
            float bestDist = len + 1.0F;
            for (const Collider2DSnapshotUVE& col : colliders) {
                if (col.entity == castEntity || (sc.collisionMask & col.layer) == 0U) {
                    continue;
                }
                const float combinedR = col.radius + sc.radius;
                const Math::Vector2UVE toCenter = col.center - origin;
                const float proj = toCenter.x * dir.x + toCenter.y * dir.y;
                if (proj < 0.0F || proj > len + combinedR) {
                    continue;
                }
                const Math::Vector2UVE closest = origin + dir * std::clamp(proj, 0.0F, len);
                const Math::Vector2UVE diff = col.center - closest;
                if (diff.x * diff.x + diff.y * diff.y <= combinedR * combinedR) {
                    const float hitDist = std::max(0.0F, proj - combinedR);
                    if (hitDist < bestDist) {
                        bestDist = hitDist;
                        sc.hit = true;
                        sc.hitEntity = col.entity;
                        sc.hitDistance = hitDist;
                    }
                }
            }
        });

    return activeStepCount;
}

} // namespace UVE::Scene
