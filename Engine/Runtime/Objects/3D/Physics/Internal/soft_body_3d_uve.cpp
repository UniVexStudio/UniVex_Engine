// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/soft_body_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/disabled_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsSoftBody3DComponentValidUVE(const SoftBody3DComponentUVE& value) noexcept {
    if (value.points.size() > kMaximumSoftBody3DPointsUVE || !std::isfinite(value.stiffness) ||
        value.stiffness < 0.0F || value.stiffness > 1.0F || !std::isfinite(value.damping) ||
        value.damping < 0.0F || value.damping > 1.0F || !std::isfinite(value.restSegmentLength) ||
        value.restSegmentLength <= 0.0F || !std::isfinite(value.gravityScale)) {
        return false;
    }
    for (const SoftBody3DPointUVE& pt : value.points) {
        if (!IsFinite3DObjectVectorUVE(pt.position) || !IsFinite3DObjectVectorUVE(pt.velocity)) {
            return false;
        }
    }
    return true;
}

SoftBody3DComponentUVE SanitizeSoftBody3DComponentUVE(const SoftBody3DComponentUVE& value) {
    SoftBody3DComponentUVE out = value;
    if (out.points.size() > kMaximumSoftBody3DPointsUVE) {
        out.points.resize(kMaximumSoftBody3DPointsUVE);
    }
    for (SoftBody3DPointUVE& pt : out.points) {
        if (!IsFinite3DObjectVectorUVE(pt.position)) {
            pt.position = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
        }
        if (!IsFinite3DObjectVectorUVE(pt.velocity)) {
            pt.velocity = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
        }
    }
    if (!std::isfinite(out.stiffness)) {
        out.stiffness = 0.8F;
    } else {
        out.stiffness = std::clamp(out.stiffness, 0.0F, 1.0F);
    }
    if (!std::isfinite(out.damping)) {
        out.damping = 0.05F;
    } else {
        out.damping = std::clamp(out.damping, 0.0F, 1.0F);
    }
    if (!std::isfinite(out.restSegmentLength) || out.restSegmentLength <= 0.0F) {
        out.restSegmentLength = 0.5F;
    }
    if (!std::isfinite(out.gravityScale)) {
        out.gravityScale = 1.0F;
    }
    return out;
}

bool IsSoftBody3DObjectDefinitionValidUVE(const SoftBody3DObjectDefinitionUVE& value) noexcept {
    return IsSoftBody3DComponentValidUVE(value.softBody);
}

void ApplySoftBody3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                        const SoftBody3DObjectDefinitionUVE& value) {
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, SoftBody3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<SoftBody3DComponentUVE>(
        entity, SanitizeSoftBody3DComponentUVE(value.softBody));
}

std::size_t UpdateSoftBody3DObjectsUVE(IEntityManagerUVE& entityManager,
                                       const Math::Vector3UVE& gravity,
                                       const float deltaTimeSeconds) {
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return 0U;
    }
    const Math::Vector3UVE safeGravity = IsFinite3DObjectVectorUVE(gravity)
                                             ? gravity
                                             : Math::Vector3UVE{0.0F, -9.81F, 0.0F};
    std::size_t steppedCount = 0U;
    entityManager.ForEachUVE<SoftBody3DComponentUVE>(
        [&](const EntityUVE entity, SoftBody3DComponentUVE& sb) {
            if (!sb.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsSoftBody3DComponentValidUVE(sb) || sb.points.empty()) {
                return;
            }
            const float damp = std::clamp(1.0F - sb.damping, 0.0F, 1.0F);
            for (SoftBody3DPointUVE& pt : sb.points) {
                if (pt.pinned) {
                    pt.velocity = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
                    continue;
                }
                pt.velocity =
                    (pt.velocity + safeGravity * (sb.gravityScale * deltaTimeSeconds)) * damp;
                pt.position = pt.position + pt.velocity * deltaTimeSeconds;
            }
            for (std::size_t i = 0U; i + 1U < sb.points.size(); ++i) {
                SoftBody3DPointUVE& a = sb.points[i];
                SoftBody3DPointUVE& b = sb.points[i + 1U];
                const Math::Vector3UVE delta = b.position - a.position;
                const float dist = std::sqrt(Math::LengthSquaredUVE(delta));
                if (dist <= 1.0e-6F) {
                    continue;
                }
                const float diff = (dist - sb.restSegmentLength) / dist;
                const Math::Vector3UVE correction = delta * (diff * sb.stiffness);
                if (!a.pinned && !b.pinned) {
                    a.position = a.position + correction * 0.5F;
                    b.position = b.position - correction * 0.5F;
                } else if (a.pinned && !b.pinned) {
                    b.position = b.position - correction;
                } else if (!a.pinned && b.pinned) {
                    a.position = a.position + correction;
                }
            }
            ++steppedCount;
        });
    return steppedCount;
}

bool IsCollisionPolygon3DComponentValidUVE(const CollisionPolygon3DComponentUVE& value) noexcept {
    if (value.vertices2D.size() > kMaximumSoftBody3DPointsUVE ||
        !std::isfinite(value.depthMetres) || value.depthMetres <= 0.0F) {
        return false;
    }
    for (const Math::Vector2UVE& v : value.vertices2D) {
        if (!Math::IsFiniteUVE(v)) {
            return false;
        }
    }
    return true;
}

CollisionPolygon3DComponentUVE SanitizeCollisionPolygon3DComponentUVE(
    const CollisionPolygon3DComponentUVE& value) {
    CollisionPolygon3DComponentUVE out = value;
    if (out.vertices2D.size() > kMaximumSoftBody3DPointsUVE) {
        out.vertices2D.resize(kMaximumSoftBody3DPointsUVE);
    }
    for (Math::Vector2UVE& v : out.vertices2D) {
        if (!Math::IsFiniteUVE(v)) {
            v = Math::Vector2UVE{0.0F, 0.0F};
        }
    }
    if (!std::isfinite(out.depthMetres) || out.depthMetres <= 0.0F) {
        out.depthMetres = 1.0F;
    }
    return out;
}

bool IsCollisionPolygon3DObjectDefinitionValidUVE(
    const CollisionPolygon3DObjectDefinitionUVE& value) noexcept {
    return IsCollisionPolygon3DComponentValidUVE(value.polygon);
}

void ApplyCollisionPolygon3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const CollisionPolygon3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity,
                              CollisionPolygon3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<CollisionPolygon3DComponentUVE>(
        entity, SanitizeCollisionPolygon3DComponentUVE(value.polygon));
}

Math::AabbUVE CollisionPolygon3DUVE::ComputeLocalAabbUVE(
    const CollisionPolygon3DComponentUVE& polygon) noexcept {
    if (!IsCollisionPolygon3DComponentValidUVE(polygon) || polygon.vertices2D.empty()) {
        return Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F},
                                                   Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    }
    float minX = polygon.vertices2D.front().x;
    float maxX = polygon.vertices2D.front().x;
    float minY = polygon.vertices2D.front().y;
    float maxY = polygon.vertices2D.front().y;
    for (const Math::Vector2UVE& v : polygon.vertices2D) {
        minX = std::min(minX, v.x);
        maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
    }
    const float halfDepth = 0.5F * polygon.depthMetres;
    return Math::AabbUVE{Math::Vector3UVE{minX, minY, -halfDepth},
                         Math::Vector3UVE{maxX, maxY, halfDepth}};
}

bool CollisionPolygon3DUVE::ContainsLocalPointUVE(
    const CollisionPolygon3DComponentUVE& polygon, const Math::Vector3UVE& localPoint) noexcept {
    if (!IsCollisionPolygon3DComponentValidUVE(polygon) || polygon.vertices2D.size() < 3U ||
        !IsFinite3DObjectVectorUVE(localPoint)) {
        return false;
    }
    const float halfDepth = 0.5F * polygon.depthMetres;
    if (localPoint.z < -halfDepth || localPoint.z > halfDepth) {
        return false;
    }
    bool inside = false;
    const std::size_t n = polygon.vertices2D.size();
    for (std::size_t i = 0U, j = n - 1U; i < n; j = i++) {
        const Math::Vector2UVE& vi = polygon.vertices2D[i];
        const Math::Vector2UVE& vj = polygon.vertices2D[j];
        const bool intersects = ((vi.y > localPoint.y) != (vj.y > localPoint.y)) &&
                                (localPoint.x < (vj.x - vi.x) * (localPoint.y - vi.y) /
                                                        (vj.y - vi.y) +
                                                    vi.x);
        if (intersects) {
            inside = !inside;
        }
    }
    return inside;
}

} // namespace UVE::Scene
