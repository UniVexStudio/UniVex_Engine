// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "csg_shape_3d.h"

#include <algorithm>
#include <cmath>

#include "i_entity_manager.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {

bool IsCSGShape3DComponentValid(const CSGShape3DComponent& value) noexcept {
    if (static_cast<std::uint8_t>(value.primitiveKind) >
            static_cast<std::uint8_t>(CSGPrimitiveKind::Combiner) ||
        static_cast<std::uint8_t>(value.operation) >
            static_cast<std::uint8_t>(CSGOperation::Subtraction) ||
        !IsFinite3DObjectVector(value.boxSize) || value.boxSize.x <= 0.0F ||
        value.boxSize.y <= 0.0F || value.boxSize.z <= 0.0F || !std::isfinite(value.radius) ||
        value.radius <= 0.0F || !std::isfinite(value.innerRadius) || value.innerRadius <= 0.0F ||
        value.innerRadius >= value.radius || !std::isfinite(value.height) || value.height <= 0.0F ||
        value.polygonVertices2D.size() > 512U) {
        return false;
    }
    for (const Math::Vector2& v : value.polygonVertices2D) {
        if (!Math::IsFinite(v)) {
            return false;
        }
    }
    return true;
}

CSGShape3DComponent SanitizeCSGShape3DComponent(const CSGShape3DComponent& value) {
    CSGShape3DComponent out = value;
    if (static_cast<std::uint8_t>(out.primitiveKind) >
        static_cast<std::uint8_t>(CSGPrimitiveKind::Combiner)) {
        out.primitiveKind = CSGPrimitiveKind::Box;
    }
    if (static_cast<std::uint8_t>(out.operation) >
        static_cast<std::uint8_t>(CSGOperation::Subtraction)) {
        out.operation = CSGOperation::Union;
    }
    if (!IsFinite3DObjectVector(out.boxSize) || out.boxSize.x <= 0.0F ||
        out.boxSize.y <= 0.0F || out.boxSize.z <= 0.0F) {
        out.boxSize = Math::Vector3{1.0F, 1.0F, 1.0F};
    }
    if (!std::isfinite(out.radius) || out.radius <= 0.0F) {
        out.radius = 0.5F;
    }
    if (!std::isfinite(out.innerRadius) || out.innerRadius <= 0.0F ||
        out.innerRadius >= out.radius) {
        out.innerRadius = 0.5F * out.radius;
    }
    if (!std::isfinite(out.height) || out.height <= 0.0F) {
        out.height = 1.0F;
    }
    if (out.polygonVertices2D.size() > 512U) {
        out.polygonVertices2D.resize(512U);
    }
    for (Math::Vector2& v : out.polygonVertices2D) {
        if (!Math::IsFinite(v)) {
            v = Math::Vector2{0.0F, 0.0F};
        }
    }
    return out;
}

namespace {

void ApplyCSGKindDefinition(IEntityManager& entityManager, const Entity entity,
                               const std::string_view defaultName,
                               const CSGShape3DComponent& csg,
                               const CSGPrimitiveKind kind) {
    ApplySurfaceInstance3DBase(entityManager, entity, defaultName);
    CSGShape3DComponent sanitized = SanitizeCSGShape3DComponent(csg);
    sanitized.primitiveKind = kind;
    entityManager.AddComponent<CSGShape3DComponent>(entity, std::move(sanitized));
}

} // namespace

void ApplyCSGBox3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                      const CSGBox3DObjectDefinition& value) {
    ApplyCSGKindDefinition(entityManager, entity, CSGBox3DObjectDefinition::defaultName,
                              value.csg, CSGPrimitiveKind::Box);
}
void ApplyCSGSphere3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                         const CSGSphere3DObjectDefinition& value) {
    ApplyCSGKindDefinition(entityManager, entity, CSGSphere3DObjectDefinition::defaultName,
                              value.csg, CSGPrimitiveKind::Sphere);
}
void ApplyCSGCylinder3DObjectDefinition(IEntityManager& entityManager,
                                           const Entity entity,
                                           const CSGCylinder3DObjectDefinition& value) {
    ApplyCSGKindDefinition(entityManager, entity, CSGCylinder3DObjectDefinition::defaultName,
                              value.csg, CSGPrimitiveKind::Cylinder);
}
void ApplyCSGTorus3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                        const CSGTorus3DObjectDefinition& value) {
    ApplyCSGKindDefinition(entityManager, entity, CSGTorus3DObjectDefinition::defaultName,
                              value.csg, CSGPrimitiveKind::Torus);
}
void ApplyCSGMesh3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                       const CSGMesh3DObjectDefinition& value) {
    ApplyCSGKindDefinition(entityManager, entity, CSGMesh3DObjectDefinition::defaultName,
                              value.csg, CSGPrimitiveKind::Mesh);
}
void ApplyCSGPolygon3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                          const CSGPolygon3DObjectDefinition& value) {
    ApplyCSGKindDefinition(entityManager, entity, CSGPolygon3DObjectDefinition::defaultName,
                              value.csg, CSGPrimitiveKind::Polygon);
}
void ApplyCSGCombiner3DObjectDefinition(IEntityManager& entityManager,
                                           const Entity entity,
                                           const CSGCombiner3DObjectDefinition& value) {
    ApplyCSGKindDefinition(entityManager, entity, CSGCombiner3DObjectDefinition::defaultName,
                              value.csg, CSGPrimitiveKind::Combiner);
}

Math::Aabb CSGShape3D::ComputeLocalAabb(const CSGShape3DComponent& shape) noexcept {
    const CSGShape3DComponent safe = SanitizeCSGShape3DComponent(shape);
    switch (safe.primitiveKind) {
    case CSGPrimitiveKind::Box:
    case CSGPrimitiveKind::Mesh:
    case CSGPrimitiveKind::Combiner:
        return Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                   safe.boxSize * 0.5F);
    case CSGPrimitiveKind::Sphere:
        return Math::Aabb::FromCenterExtents(
            Math::Vector3{0.0F, 0.0F, 0.0F},
            Math::Vector3{safe.radius, safe.radius, safe.radius});
    case CSGPrimitiveKind::Cylinder:
    case CSGPrimitiveKind::Polygon:
        return Math::Aabb::FromCenterExtents(
            Math::Vector3{0.0F, 0.0F, 0.0F},
            Math::Vector3{safe.radius, 0.5F * safe.height, safe.radius});
    case CSGPrimitiveKind::Torus: {
        const float outerR = safe.radius + safe.innerRadius;
        return Math::Aabb::FromCenterExtents(
            Math::Vector3{0.0F, 0.0F, 0.0F},
            Math::Vector3{outerR, safe.innerRadius, outerR});
    }
    }
    return Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F},
                                               Math::Vector3{0.5F, 0.5F, 0.5F});
}

float CSGShape3D::EvaluateLocalSignedDistance(
    const CSGShape3DComponent& shape, const Math::Vector3& localPoint) noexcept {
    if (!IsFinite3DObjectVector(localPoint)) {
        return 1.0F;
    }
    const CSGShape3DComponent safe = SanitizeCSGShape3DComponent(shape);
    if (safe.primitiveKind == CSGPrimitiveKind::Sphere) {
        return std::sqrt(Math::LengthSquared(localPoint)) - safe.radius;
    }
    if (safe.primitiveKind == CSGPrimitiveKind::Cylinder) {
        const float radial =
            std::sqrt(localPoint.x * localPoint.x + localPoint.z * localPoint.z) - safe.radius;
        const float vertical = std::fabs(localPoint.y) - 0.5F * safe.height;
        const float outX = std::max(radial, 0.0F);
        const float outY = std::max(vertical, 0.0F);
        return std::min(std::max(radial, vertical), 0.0F) + std::sqrt(outX * outX + outY * outY);
    }
    const Math::Vector3 halfExtents = safe.boxSize * 0.5F;
    const Math::Vector3 q{std::fabs(localPoint.x) - halfExtents.x,
                             std::fabs(localPoint.y) - halfExtents.y,
                             std::fabs(localPoint.z) - halfExtents.z};
    const Math::Vector3 maxQ{std::max(q.x, 0.0F), std::max(q.y, 0.0F), std::max(q.z, 0.0F)};
    return std::sqrt(Math::LengthSquared(maxQ)) +
           std::min(std::max({q.x, q.y, q.z}), 0.0F);
}

float CSGShape3D::CombineSignedDistance(const CSGOperation operation, const float sdfA,
                                              const float sdfB) noexcept {
    switch (operation) {
    case CSGOperation::Union:
        return std::min(sdfA, sdfB);
    case CSGOperation::Intersection:
        return std::max(sdfA, sdfB);
    case CSGOperation::Subtraction:
        return std::max(sdfA, -sdfB);
    }
    return std::min(sdfA, sdfB);
}

} // namespace UVE::Scene
