// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "multi_mesh_instance_3d.h"

#include <algorithm>

#include "i_entity_manager.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsInstanceTransformValid(
    const MultiMeshInstanceTransform3D& inst) noexcept {
    return IsFinite3DObjectVector(inst.position) &&
           IsFinite3DObjectQuaternion(inst.rotation) &&
           IsFinite3DObjectVector(inst.scale) && inst.scale.x > 0.0F && inst.scale.y > 0.0F &&
           inst.scale.z > 0.0F && std::isfinite(inst.customColor.r) &&
           std::isfinite(inst.customColor.g) && std::isfinite(inst.customColor.b);
}

} // namespace

bool IsMultiMeshInstance3DComponentValid(
    const MultiMeshInstance3DComponent& value) noexcept {
    if (value.visibleInstanceCount < -1 ||
        value.instances.size() > kMaximumMultiMeshInstances3D) {
        return false;
    }
    for (const MultiMeshInstanceTransform3D& inst : value.instances) {
        if (!IsInstanceTransformValid(inst)) {
            return false;
        }
    }
    return true;
}

MultiMeshInstance3DComponent SanitizeMultiMeshInstance3DComponent(
    const MultiMeshInstance3DComponent& value) {
    MultiMeshInstance3DComponent out = value;
    if (out.visibleInstanceCount < -1) {
        out.visibleInstanceCount = -1;
    }
    if (out.instances.size() > kMaximumMultiMeshInstances3D) {
        out.instances.resize(kMaximumMultiMeshInstances3D);
    }
    for (MultiMeshInstanceTransform3D& inst : out.instances) {
        if (!IsFinite3DObjectVector(inst.position)) {
            inst.position = Math::Vector3{0.0F, 0.0F, 0.0F};
        }
        if (!IsFinite3DObjectQuaternion(inst.rotation)) {
            inst.rotation = Math::Quaternion{};
        }
        if (!IsFinite3DObjectVector(inst.scale) || inst.scale.x <= 0.0F ||
            inst.scale.y <= 0.0F || inst.scale.z <= 0.0F) {
            inst.scale = Math::Vector3{1.0F, 1.0F, 1.0F};
        }
        if (!std::isfinite(inst.customColor.r) || !std::isfinite(inst.customColor.g) ||
            !std::isfinite(inst.customColor.b)) {
            inst.customColor = Math::Color{1.0F, 1.0F, 1.0F};
        }
    }
    return out;
}

bool IsMultiMeshInstance3DObjectDefinitionValid(
    const MultiMeshInstance3DObjectDefinition& value) noexcept {
    return IsMultiMeshInstance3DComponentValid(value.multiMesh);
}

void ApplyMultiMeshInstance3DObjectDefinition(
    IEntityManager& entityManager, const Entity entity,
    const MultiMeshInstance3DObjectDefinition& value) {
    ApplySurfaceInstance3DBase(entityManager, entity,
                                  MultiMeshInstance3DObjectDefinition::defaultName);
    entityManager.AddComponent<MultiMeshInstance3DComponent>(
        entity, SanitizeMultiMeshInstance3DComponent(value.multiMesh));
}

std::size_t MultiMeshInstance3D::ResolveActiveInstanceCount(
    const MultiMeshInstance3DComponent& multiMesh) noexcept {
    if (!multiMesh.enabled || multiMesh.instances.empty()) {
        return 0U;
    }
    if (multiMesh.visibleInstanceCount < 0) {
        return multiMesh.instances.size();
    }
    return std::min(static_cast<std::size_t>(multiMesh.visibleInstanceCount),
                    multiMesh.instances.size());
}

Math::Matrix4x4 MultiMeshInstance3D::BuildInstanceWorldMatrix(
    const MultiMeshInstance3DComponent& multiMesh, const std::size_t index,
    const WorldTransformComponent& ownerWorldTransform) noexcept {
    if (index >= multiMesh.instances.size()) {
        return BuildWorldTransformMatrix(ownerWorldTransform);
    }
    const MultiMeshInstanceTransform3D& inst = multiMesh.instances[index];
    TransformComponent local{};
    local.localPosition = inst.position;
    local.localRotation = inst.rotation;
    local.localScale = inst.scale;
    const WorldTransformComponent composed =
        ComposeWorldTransform(ownerWorldTransform, local);
    return BuildWorldTransformMatrix(composed);
}

Math::Aabb MultiMeshInstance3D::ComputeCombinedWorldBounds(
    const MultiMeshInstance3DComponent& multiMesh,
    const WorldTransformComponent& ownerWorldTransform,
    const Math::Vector3& instanceLocalHalfExtents) noexcept {
    const std::size_t count = ResolveActiveInstanceCount(multiMesh);
    const Math::Vector3 safeExtents =
        (IsFinite3DObjectVector(instanceLocalHalfExtents) && instanceLocalHalfExtents.x > 0.0F &&
         instanceLocalHalfExtents.y > 0.0F && instanceLocalHalfExtents.z > 0.0F)
            ? instanceLocalHalfExtents
            : Math::Vector3{0.5F, 0.5F, 0.5F};
    const Math::Aabb unitBox =
        Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F}, safeExtents);
    if (count == 0U) {
        return unitBox.Transform(BuildWorldTransformMatrix(ownerWorldTransform));
    }
    Math::Aabb combined =
        unitBox.Transform(BuildInstanceWorldMatrix(multiMesh, 0U, ownerWorldTransform));
    for (std::size_t i = 1U; i < count; ++i) {
        const Math::Aabb box =
            unitBox.Transform(BuildInstanceWorldMatrix(multiMesh, i, ownerWorldTransform));
        combined.min = Math::Vector3{std::min(combined.min.x, box.min.x),
                                        std::min(combined.min.y, box.min.y),
                                        std::min(combined.min.z, box.min.z)};
        combined.max = Math::Vector3{std::max(combined.max.x, box.max.x),
                                        std::max(combined.max.y, box.max.y),
                                        std::max(combined.max.z, box.max.z)};
    }
    return combined;
}

} // namespace UVE::Scene
