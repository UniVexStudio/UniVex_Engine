// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "box_mesh_3d.h"

#include <cmath>

#include "i_entity_manager.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {

bool IsBoxMesh3DObjectDefinitionValid(const BoxMesh3DObjectDefinition& value) noexcept {
    return IsPrimitiveMeshComponentValid(value.mesh) && IsColliderComponentValid(value.collider);
}

void ApplyBoxMesh3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                       const BoxMesh3DObjectDefinition& value) {
    ApplySurfaceInstance3DBase(entityManager, entity, BoxMesh3DObjectDefinition::defaultName);
    entityManager.AddComponent<PrimitiveMeshComponent>(entity, value.mesh);
    entityManager.AddComponent<ColliderComponent>(entity, value.collider);
}

bool BoxMesh3D::IsBoxMesh(const PrimitiveMeshComponent& mesh) noexcept {
    return IsPrimitiveMeshComponentValid(mesh) && mesh.kind == PrimitiveMeshKind::Cube;
}

Math::Vector3 BoxMesh3D::DefaultColliderHalfExtents() noexcept {
    return kDefaultHalfExtents;
}

Math::Aabb BoxMesh3D::LocalBounds() noexcept {
    return Math::Aabb::FromCenterExtents(Math::Vector3{}, kDefaultHalfExtents);
}

float BoxMesh3D::VolumeFromScale(const Math::Vector3& worldScale) noexcept {
    if (!Math::IsFinite(worldScale) || worldScale.x <= 0.0F || worldScale.y <= 0.0F ||
        worldScale.z <= 0.0F) {
        return 0.0F;
    }
    const float volume = worldScale.x * worldScale.y * worldScale.z;
    return std::isfinite(volume) ? volume : 0.0F;
}

void BoxMesh3D::SyncColliderToBox(ColliderComponent& collider) noexcept {
    collider.shapeType = ColliderShapeType::Box;
    collider.halfExtents = kDefaultHalfExtents;
}

} // namespace UVE::Scene
