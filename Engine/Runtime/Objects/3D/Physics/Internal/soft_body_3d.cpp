// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "soft_body_3d.h"

#include <algorithm>
#include <cmath>

#include "disabled_component.h"
#include "i_entity_manager.h"
#include "abstract_objects_3d.h"
#include "object_3d.h"

namespace UVE::Scene {
namespace {

void SolveXpbdDistanceConstraint(SoftBody3DPoint& a, SoftBody3DPoint& b,
                                    const float restLength, const float compliance,
                                    const float stiffness, const float subDt) noexcept {
    if (a.pinned && b.pinned) {
        return;
    }
    const Math::Vector3 delta = b.position - a.position;
    const float dist = std::sqrt(Math::LengthSquared(delta));
    if (dist <= 1.0e-6F) {
        return;
    }
    const float wA = a.pinned ? 0.0F : 1.0F;
    const float wB = b.pinned ? 0.0F : 1.0F;
    const float wSum = wA + wB;
    if (wSum <= 0.0F) {
        return;
    }
    const float alphaTilde = compliance / std::max(subDt * subDt, 1.0e-8F);
    const float c = dist - restLength;
    const float lambda = (-c / (wSum + alphaTilde)) * std::clamp(stiffness, 0.0F, 1.0F);
    const Math::Vector3 dir = delta * (1.0F / dist);
    if (!a.pinned) {
        a.position = a.position - dir * (wA * lambda);
    }
    if (!b.pinned) {
        b.position = b.position + dir * (wB * lambda);
    }
}

void ResolvePointColliders(SoftBody3DPoint& pt,
                              const std::span<const SoftBodySphereCollider> spheres,
                              const std::span<const SoftBodyAabbCollider> boxes) noexcept {
    if (pt.pinned) {
        return;
    }
    for (const SoftBodySphereCollider& sphere : spheres) {
        if (sphere.radius <= 0.0F) {
            continue;
        }
        const Math::Vector3 offset = pt.position - sphere.center;
        const float distSq = Math::LengthSquared(offset);
        const float minDist = sphere.radius + 0.002F;
        if (distSq < minDist * minDist) {
            const float dist = std::sqrt(distSq);
            const Math::Vector3 n =
                dist > 1.0e-5F ? (offset * (1.0F / dist)) : Math::Vector3{0.0F, 1.0F, 0.0F};
            pt.position = sphere.center + n * minDist;
        }
    }
    for (const SoftBodyAabbCollider& box : boxes) {
        const Math::Vector3& mn = box.bounds.min;
        const Math::Vector3& mx = box.bounds.max;
        if (pt.position.x > mn.x && pt.position.x < mx.x && pt.position.y > mn.y &&
            pt.position.y < mx.y && pt.position.z > mn.z && pt.position.z < mx.z) {
            const float dxMin = pt.position.x - mn.x;
            const float dxMax = mx.x - pt.position.x;
            const float dyMin = pt.position.y - mn.y;
            const float dyMax = mx.y - pt.position.y;
            const float dzMin = pt.position.z - mn.z;
            const float dzMax = mx.z - pt.position.z;
            const float best = std::min({dxMin, dxMax, dyMin, dyMax, dzMin, dzMax});
            if (best == dyMax) {
                pt.position.y = mx.y + 0.002F;
            } else if (best == dyMin) {
                pt.position.y = mn.y - 0.002F;
            } else if (best == dxMin) {
                pt.position.x = mn.x - 0.002F;
            } else if (best == dxMax) {
                pt.position.x = mx.x + 0.002F;
            } else if (best == dzMin) {
                pt.position.z = mn.z - 0.002F;
            } else {
                pt.position.z = mx.z + 0.002F;
            }
        }
    }
}

} // namespace

SoftBody3DComponent BuildClothGridSoftBody3D(const std::uint32_t width,
                                                   const std::uint32_t height,
                                                   const float spacingMetres,
                                                   const Math::Vector3& origin,
                                                   const bool pinTopRow) {
    SoftBody3DComponent sb{};
    if (width < 2U || height < 2U || spacingMetres <= 0.0F ||
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) >
            kMaximumSoftBody3DPoints) {
        return sb;
    }
    sb.gridWidth = width;
    sb.gridHeight = height;
    sb.restSegmentLength = spacingMetres;
    sb.points.reserve(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            SoftBody3DPoint pt{};
            pt.position = Math::Vector3{
                origin.x + static_cast<float>(x) * spacingMetres,
                origin.y - static_cast<float>(y) * spacingMetres,
                origin.z,
            };
            pt.pinned = pinTopRow && (y == 0U);
            sb.points.push_back(pt);
        }
    }
    return sb;
}

bool IsSoftBody3DComponentValid(const SoftBody3DComponent& value) noexcept {
    if (value.points.size() > kMaximumSoftBody3DPoints || !std::isfinite(value.stiffness) ||
        value.stiffness < 0.0F || value.stiffness > 1.0F || !std::isfinite(value.damping) ||
        value.damping < 0.0F || value.damping > 1.0F || !std::isfinite(value.restSegmentLength) ||
        value.restSegmentLength <= 0.0F || !std::isfinite(value.gravityScale) ||
        !std::isfinite(value.compliance) || value.compliance < 0.0F ||
        !IsFinite3DObjectVector(value.windVelocity)) {
        return false;
    }
    for (const SoftBody3DPoint& pt : value.points) {
        if (!IsFinite3DObjectVector(pt.position) || !IsFinite3DObjectVector(pt.velocity)) {
            return false;
        }
    }
    return true;
}

SoftBody3DComponent SanitizeSoftBody3DComponent(const SoftBody3DComponent& value) {
    SoftBody3DComponent out = value;
    if (out.points.size() > kMaximumSoftBody3DPoints) {
        out.points.resize(kMaximumSoftBody3DPoints);
    }
    for (SoftBody3DPoint& pt : out.points) {
        if (!IsFinite3DObjectVector(pt.position)) {
            pt.position = Math::Vector3{0.0F, 0.0F, 0.0F};
        }
        if (!IsFinite3DObjectVector(pt.velocity)) {
            pt.velocity = Math::Vector3{0.0F, 0.0F, 0.0F};
        }
    }
    out.substeps = std::clamp(out.substeps, 1U, 16U);
    out.stiffness = std::isfinite(out.stiffness) ? std::clamp(out.stiffness, 0.0F, 1.0F) : 0.8F;
    out.damping = std::isfinite(out.damping) ? std::clamp(out.damping, 0.0F, 1.0F) : 0.05F;
    out.compliance = (std::isfinite(out.compliance) && out.compliance >= 0.0F) ? out.compliance : 0.0005F;
    out.shearCompliance =
        (std::isfinite(out.shearCompliance) && out.shearCompliance >= 0.0F) ? out.shearCompliance : 0.002F;
    out.bendCompliance =
        (std::isfinite(out.bendCompliance) && out.bendCompliance >= 0.0F) ? out.bendCompliance : 0.02F;
    if (!std::isfinite(out.restSegmentLength) || out.restSegmentLength <= 0.0F) {
        out.restSegmentLength = 0.5F;
    }
    if (!std::isfinite(out.gravityScale)) {
        out.gravityScale = 1.0F;
    }
    if (!IsFinite3DObjectVector(out.windVelocity)) {
        out.windVelocity = Math::Vector3{0.0F, 0.0F, 0.0F};
    }
    out.windDrag = std::isfinite(out.windDrag) ? std::clamp(out.windDrag, 0.0F, 10.0F) : 0.25F;
    return out;
}

bool IsSoftBody3DObjectDefinitionValid(const SoftBody3DObjectDefinition& value) noexcept {
    return IsSoftBody3DComponentValid(value.softBody);
}

void ApplySoftBody3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                        const SoftBody3DObjectDefinition& value) {
    ApplySurfaceInstance3DBase(entityManager, entity, SoftBody3DObjectDefinition::defaultName);
    entityManager.AddComponent<SoftBody3DComponent>(
        entity, SanitizeSoftBody3DComponent(value.softBody));
}

std::size_t UpdateSoftBody3DObjects(IEntityManager& entityManager,
                                       const Math::Vector3& gravity,
                                       const float deltaTimeSeconds,
                                       const std::span<const SoftBodySphereCollider> sphereColliders,
                                       const std::span<const SoftBodyAabbCollider> boxColliders) {
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return 0U;
    }
    const Math::Vector3 safeGravity = IsFinite3DObjectVector(gravity)
                                             ? gravity
                                             : Math::Vector3{0.0F, -9.81F, 0.0F};
    std::size_t steppedCount = 0U;
    std::vector<Math::Vector3> prevPositions;

    entityManager.ForEach<SoftBody3DComponent>(
        [&](const Entity entity, SoftBody3DComponent& sb) {
            if (!sb.enabled || entityManager.HasComponent<DisabledComponent>(entity) ||
                !IsSoftBody3DComponentValid(sb) || sb.points.empty()) {
                return;
            }
            const std::uint32_t substeps = std::clamp(sb.substeps, 1U, 16U);
            const float subDt = deltaTimeSeconds / static_cast<float>(substeps);
            const float dampPerSubstep =
                std::pow(std::clamp(1.0F - sb.damping, 0.0F, 1.0F), 1.0F / static_cast<float>(substeps));
            const bool isClothGrid =
                sb.gridWidth >= 2U && sb.gridHeight >= 2U &&
                static_cast<std::size_t>(sb.gridWidth) * static_cast<std::size_t>(sb.gridHeight) ==
                    sb.points.size();
            const float diagRest = sb.restSegmentLength * 1.41421356F;
            const float bendRest = sb.restSegmentLength * 2.0F;

            prevPositions.resize(sb.points.size());
            for (std::uint32_t step = 0U; step < substeps; ++step) {
                for (std::size_t i = 0U; i < sb.points.size(); ++i) {
                    SoftBody3DPoint& pt = sb.points[i];
                    prevPositions[i] = pt.position;
                    if (pt.pinned) {
                        pt.velocity = Math::Vector3{0.0F, 0.0F, 0.0F};
                        continue;
                    }
                    const Math::Vector3 windAccel = (sb.windVelocity - pt.velocity) * sb.windDrag;
                    pt.velocity = pt.velocity + (safeGravity * sb.gravityScale + windAccel) * subDt;
                    pt.position = pt.position + pt.velocity * subDt;
                }

                if (isClothGrid) {
                    const auto idx = [&](std::uint32_t x, std::uint32_t y) -> std::size_t {
                        return static_cast<std::size_t>(y) * sb.gridWidth + x;
                    };
                    // Structural horizontal & vertical constraints.
                    for (std::uint32_t y = 0U; y < sb.gridHeight; ++y) {
                        for (std::uint32_t x = 0U; x < sb.gridWidth; ++x) {
                            if (x + 1U < sb.gridWidth) {
                                SolveXpbdDistanceConstraint(sb.points[idx(x, y)], sb.points[idx(x + 1U, y)],
                                                               sb.restSegmentLength, sb.compliance, sb.stiffness,
                                                               subDt);
                            }
                            if (y + 1U < sb.gridHeight) {
                                SolveXpbdDistanceConstraint(sb.points[idx(x, y)], sb.points[idx(x, y + 1U)],
                                                               sb.restSegmentLength, sb.compliance, sb.stiffness,
                                                               subDt);
                            }
                        }
                    }
                    // Shear diagonal constraints.
                    for (std::uint32_t y = 0U; y + 1U < sb.gridHeight; ++y) {
                        for (std::uint32_t x = 0U; x + 1U < sb.gridWidth; ++x) {
                            SolveXpbdDistanceConstraint(sb.points[idx(x, y)], sb.points[idx(x + 1U, y + 1U)],
                                                           diagRest, sb.shearCompliance, sb.stiffness, subDt);
                            SolveXpbdDistanceConstraint(sb.points[idx(x + 1U, y)], sb.points[idx(x, y + 1U)],
                                                           diagRest, sb.shearCompliance, sb.stiffness, subDt);
                        }
                    }
                    // Bend 2-hop constraints.
                    for (std::uint32_t y = 0U; y < sb.gridHeight; ++y) {
                        for (std::uint32_t x = 0U; x < sb.gridWidth; ++x) {
                            if (x + 2U < sb.gridWidth) {
                                SolveXpbdDistanceConstraint(sb.points[idx(x, y)], sb.points[idx(x + 2U, y)],
                                                               bendRest, sb.bendCompliance, sb.stiffness, subDt);
                            }
                            if (y + 2U < sb.gridHeight) {
                                SolveXpbdDistanceConstraint(sb.points[idx(x, y)], sb.points[idx(x, y + 2U)],
                                                               bendRest, sb.bendCompliance, sb.stiffness, subDt);
                            }
                        }
                    }
                } else {
                    for (std::size_t i = 0U; i + 1U < sb.points.size(); ++i) {
                        SolveXpbdDistanceConstraint(sb.points[i], sb.points[i + 1U], sb.restSegmentLength,
                                                       sb.compliance, sb.stiffness, subDt);
                    }
                }

                for (std::size_t i = 0U; i < sb.points.size(); ++i) {
                    SoftBody3DPoint& pt = sb.points[i];
                    ResolvePointColliders(pt, sphereColliders, boxColliders);
                    if (!pt.pinned) {
                        pt.velocity = ((pt.position - prevPositions[i]) * (1.0F / subDt)) * dampPerSubstep;
                    }
                }
            }
            ++steppedCount;
        });
    return steppedCount;
}

std::size_t UpdateSoftBody3DObjects(IEntityManager& entityManager,
                                       const Math::Vector3& gravity,
                                       const float deltaTimeSeconds) {
    return UpdateSoftBody3DObjects(entityManager, gravity, deltaTimeSeconds, {}, {});
}

bool IsCollisionPolygon3DComponentValid(const CollisionPolygon3DComponent& value) noexcept {
    if (value.vertices2D.size() > kMaximumSoftBody3DPoints ||
        !std::isfinite(value.depthMetres) || value.depthMetres <= 0.0F) {
        return false;
    }
    for (const Math::Vector2& v : value.vertices2D) {
        if (!Math::IsFinite(v)) {
            return false;
        }
    }
    return true;
}

CollisionPolygon3DComponent SanitizeCollisionPolygon3DComponent(
    const CollisionPolygon3DComponent& value) {
    CollisionPolygon3DComponent out = value;
    if (out.vertices2D.size() > kMaximumSoftBody3DPoints) {
        out.vertices2D.resize(kMaximumSoftBody3DPoints);
    }
    for (Math::Vector2& v : out.vertices2D) {
        if (!Math::IsFinite(v)) {
            v = Math::Vector2{0.0F, 0.0F};
        }
    }
    if (!std::isfinite(out.depthMetres) || out.depthMetres <= 0.0F) {
        out.depthMetres = 1.0F;
    }
    return out;
}

bool IsCollisionPolygon3DObjectDefinitionValid(
    const CollisionPolygon3DObjectDefinition& value) noexcept {
    return IsCollisionPolygon3DComponentValid(value.polygon);
}

void ApplyCollisionPolygon3DObjectDefinition(
    IEntityManager& entityManager, const Entity entity,
    const CollisionPolygon3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity,
                              CollisionPolygon3DObjectDefinition::defaultName);
    entityManager.AddComponent<CollisionPolygon3DComponent>(
        entity, SanitizeCollisionPolygon3DComponent(value.polygon));
}

Math::Aabb CollisionPolygon3D::ComputeLocalAabb(
    const CollisionPolygon3DComponent& polygon) noexcept {
    if (!IsCollisionPolygon3DComponentValid(polygon) || polygon.vertices2D.empty()) {
        return Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                   Math::Vector3{0.0F, 0.0F, 0.0F});
    }
    float minX = polygon.vertices2D.front().x;
    float maxX = polygon.vertices2D.front().x;
    float minY = polygon.vertices2D.front().y;
    float maxY = polygon.vertices2D.front().y;
    for (const Math::Vector2& v : polygon.vertices2D) {
        minX = std::min(minX, v.x);
        maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
    }
    const float halfDepth = 0.5F * polygon.depthMetres;
    return Math::Aabb{Math::Vector3{minX, minY, -halfDepth},
                         Math::Vector3{maxX, maxY, halfDepth}};
}

bool CollisionPolygon3D::ContainsLocalPoint(
    const CollisionPolygon3DComponent& polygon, const Math::Vector3& localPoint) noexcept {
    if (!IsCollisionPolygon3DComponentValid(polygon) || polygon.vertices2D.size() < 3U ||
        !IsFinite3DObjectVector(localPoint)) {
        return false;
    }
    const float halfDepth = 0.5F * polygon.depthMetres;
    if (localPoint.z < -halfDepth || localPoint.z > halfDepth) {
        return false;
    }
    bool inside = false;
    const std::size_t n = polygon.vertices2D.size();
    for (std::size_t i = 0U, j = n - 1U; i < n; j = i++) {
        const Math::Vector2& vi = polygon.vertices2D[i];
        const Math::Vector2& vj = polygon.vertices2D[j];
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
