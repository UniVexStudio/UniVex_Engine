// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/csg_shape_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

bool IsCSGShape3DComponentValidUVE(const CSGShape3DComponentUVE& value) noexcept {
    if (static_cast<std::uint8_t>(value.primitiveKind) >
            static_cast<std::uint8_t>(CSGPrimitiveKindUVE::Combiner) ||
        static_cast<std::uint8_t>(value.operation) >
            static_cast<std::uint8_t>(CSGOperationUVE::Subtraction) ||
        !IsFinite3DObjectVectorUVE(value.boxSize) || value.boxSize.x <= 0.0F ||
        value.boxSize.y <= 0.0F || value.boxSize.z <= 0.0F || !std::isfinite(value.radius) ||
        value.radius <= 0.0F || !std::isfinite(value.innerRadius) || value.innerRadius <= 0.0F ||
        value.innerRadius >= value.radius || !std::isfinite(value.height) || value.height <= 0.0F ||
        value.polygonVertices2D.size() > 512U) {
        return false;
    }
    for (const Math::Vector2UVE& v : value.polygonVertices2D) {
        if (!Math::IsFiniteUVE(v)) {
            return false;
        }
    }
    return true;
}

CSGShape3DComponentUVE SanitizeCSGShape3DComponentUVE(const CSGShape3DComponentUVE& value) {
    CSGShape3DComponentUVE out = value;
    if (static_cast<std::uint8_t>(out.primitiveKind) >
        static_cast<std::uint8_t>(CSGPrimitiveKindUVE::Combiner)) {
        out.primitiveKind = CSGPrimitiveKindUVE::Box;
    }
    if (static_cast<std::uint8_t>(out.operation) >
        static_cast<std::uint8_t>(CSGOperationUVE::Subtraction)) {
        out.operation = CSGOperationUVE::Union;
    }
    if (!IsFinite3DObjectVectorUVE(out.boxSize) || out.boxSize.x <= 0.0F ||
        out.boxSize.y <= 0.0F || out.boxSize.z <= 0.0F) {
        out.boxSize = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
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
    for (Math::Vector2UVE& v : out.polygonVertices2D) {
        if (!Math::IsFiniteUVE(v)) {
            v = Math::Vector2UVE{0.0F, 0.0F};
        }
    }
    return out;
}

namespace {

void ApplyCSGKindDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                               const std::string_view defaultName,
                               const CSGShape3DComponentUVE& csg,
                               const CSGPrimitiveKindUVE kind) {
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, defaultName);
    CSGShape3DComponentUVE sanitized = SanitizeCSGShape3DComponentUVE(csg);
    sanitized.primitiveKind = kind;
    entityManager.AddComponentUVE<CSGShape3DComponentUVE>(entity, std::move(sanitized));
}

} // namespace

void ApplyCSGBox3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                      const CSGBox3DObjectDefinitionUVE& value) {
    ApplyCSGKindDefinitionUVE(entityManager, entity, CSGBox3DObjectDefinitionUVE::defaultName,
                              value.csg, CSGPrimitiveKindUVE::Box);
}
void ApplyCSGSphere3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                         const CSGSphere3DObjectDefinitionUVE& value) {
    ApplyCSGKindDefinitionUVE(entityManager, entity, CSGSphere3DObjectDefinitionUVE::defaultName,
                              value.csg, CSGPrimitiveKindUVE::Sphere);
}
void ApplyCSGCylinder3DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                           const EntityUVE entity,
                                           const CSGCylinder3DObjectDefinitionUVE& value) {
    ApplyCSGKindDefinitionUVE(entityManager, entity, CSGCylinder3DObjectDefinitionUVE::defaultName,
                              value.csg, CSGPrimitiveKindUVE::Cylinder);
}
void ApplyCSGTorus3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                        const CSGTorus3DObjectDefinitionUVE& value) {
    ApplyCSGKindDefinitionUVE(entityManager, entity, CSGTorus3DObjectDefinitionUVE::defaultName,
                              value.csg, CSGPrimitiveKindUVE::Torus);
}
void ApplyCSGMesh3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                       const CSGMesh3DObjectDefinitionUVE& value) {
    ApplyCSGKindDefinitionUVE(entityManager, entity, CSGMesh3DObjectDefinitionUVE::defaultName,
                              value.csg, CSGPrimitiveKindUVE::Mesh);
}
void ApplyCSGPolygon3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                          const CSGPolygon3DObjectDefinitionUVE& value) {
    ApplyCSGKindDefinitionUVE(entityManager, entity, CSGPolygon3DObjectDefinitionUVE::defaultName,
                              value.csg, CSGPrimitiveKindUVE::Polygon);
}
void ApplyCSGCombiner3DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                           const EntityUVE entity,
                                           const CSGCombiner3DObjectDefinitionUVE& value) {
    ApplyCSGKindDefinitionUVE(entityManager, entity, CSGCombiner3DObjectDefinitionUVE::defaultName,
                              value.csg, CSGPrimitiveKindUVE::Combiner);
}

Math::AabbUVE CSGShape3DUVE::ComputeLocalAabbUVE(const CSGShape3DComponentUVE& shape) noexcept {
    const CSGShape3DComponentUVE safe = SanitizeCSGShape3DComponentUVE(shape);
    switch (safe.primitiveKind) {
    case CSGPrimitiveKindUVE::Box:
    case CSGPrimitiveKindUVE::Mesh:
    case CSGPrimitiveKindUVE::Combiner:
        return Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F},
                                                   safe.boxSize * 0.5F);
    case CSGPrimitiveKindUVE::Sphere:
        return Math::AabbUVE::FromCenterExtentsUVE(
            Math::Vector3UVE{0.0F, 0.0F, 0.0F},
            Math::Vector3UVE{safe.radius, safe.radius, safe.radius});
    case CSGPrimitiveKindUVE::Cylinder:
    case CSGPrimitiveKindUVE::Polygon:
        return Math::AabbUVE::FromCenterExtentsUVE(
            Math::Vector3UVE{0.0F, 0.0F, 0.0F},
            Math::Vector3UVE{safe.radius, 0.5F * safe.height, safe.radius});
    case CSGPrimitiveKindUVE::Torus: {
        const float outerR = safe.radius + safe.innerRadius;
        return Math::AabbUVE::FromCenterExtentsUVE(
            Math::Vector3UVE{0.0F, 0.0F, 0.0F},
            Math::Vector3UVE{outerR, safe.innerRadius, outerR});
    }
    }
    return Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F},
                                               Math::Vector3UVE{0.5F, 0.5F, 0.5F});
}

float CSGShape3DUVE::EvaluateLocalSignedDistanceUVE(
    const CSGShape3DComponentUVE& shape, const Math::Vector3UVE& localPoint) noexcept {
    if (!IsFinite3DObjectVectorUVE(localPoint)) {
        return 1.0F;
    }
    const CSGShape3DComponentUVE safe = SanitizeCSGShape3DComponentUVE(shape);
    if (safe.primitiveKind == CSGPrimitiveKindUVE::Sphere) {
        return std::sqrt(Math::LengthSquaredUVE(localPoint)) - safe.radius;
    }
    if (safe.primitiveKind == CSGPrimitiveKindUVE::Cylinder) {
        const float radial =
            std::sqrt(localPoint.x * localPoint.x + localPoint.z * localPoint.z) - safe.radius;
        const float vertical = std::fabs(localPoint.y) - 0.5F * safe.height;
        const float outX = std::max(radial, 0.0F);
        const float outY = std::max(vertical, 0.0F);
        return std::min(std::max(radial, vertical), 0.0F) + std::sqrt(outX * outX + outY * outY);
    }
    const Math::Vector3UVE halfExtents = safe.boxSize * 0.5F;
    const Math::Vector3UVE q{std::fabs(localPoint.x) - halfExtents.x,
                             std::fabs(localPoint.y) - halfExtents.y,
                             std::fabs(localPoint.z) - halfExtents.z};
    const Math::Vector3UVE maxQ{std::max(q.x, 0.0F), std::max(q.y, 0.0F), std::max(q.z, 0.0F)};
    return std::sqrt(Math::LengthSquaredUVE(maxQ)) +
           std::min(std::max({q.x, q.y, q.z}), 0.0F);
}

float CSGShape3DUVE::CombineSignedDistanceUVE(const CSGOperationUVE operation, const float sdfA,
                                              const float sdfB) noexcept {
    switch (operation) {
    case CSGOperationUVE::Union:
        return std::min(sdfA, sdfB);
    case CSGOperationUVE::Intersection:
        return std::max(sdfA, sdfB);
    case CSGOperationUVE::Subtraction:
        return std::max(sdfA, -sdfB);
    }
    return std::min(sdfA, sdfB);
}

} // namespace UVE::Scene
