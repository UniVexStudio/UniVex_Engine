// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/multi_mesh_instance_3d_uve.h"

#include <algorithm>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsInstanceTransformValidUVE(
    const MultiMeshInstanceTransform3DUVE& inst) noexcept {
    return IsFinite3DObjectVectorUVE(inst.position) &&
           IsFinite3DObjectQuaternionUVE(inst.rotation) &&
           IsFinite3DObjectVectorUVE(inst.scale) && inst.scale.x > 0.0F && inst.scale.y > 0.0F &&
           inst.scale.z > 0.0F && std::isfinite(inst.customColor.r) &&
           std::isfinite(inst.customColor.g) && std::isfinite(inst.customColor.b);
}

} // namespace

bool IsMultiMeshInstance3DComponentValidUVE(
    const MultiMeshInstance3DComponentUVE& value) noexcept {
    if (value.visibleInstanceCount < -1 ||
        value.instances.size() > kMaximumMultiMeshInstances3DUVE) {
        return false;
    }
    for (const MultiMeshInstanceTransform3DUVE& inst : value.instances) {
        if (!IsInstanceTransformValidUVE(inst)) {
            return false;
        }
    }
    return true;
}

MultiMeshInstance3DComponentUVE SanitizeMultiMeshInstance3DComponentUVE(
    const MultiMeshInstance3DComponentUVE& value) {
    MultiMeshInstance3DComponentUVE out = value;
    if (out.visibleInstanceCount < -1) {
        out.visibleInstanceCount = -1;
    }
    if (out.instances.size() > kMaximumMultiMeshInstances3DUVE) {
        out.instances.resize(kMaximumMultiMeshInstances3DUVE);
    }
    for (MultiMeshInstanceTransform3DUVE& inst : out.instances) {
        if (!IsFinite3DObjectVectorUVE(inst.position)) {
            inst.position = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
        }
        if (!IsFinite3DObjectQuaternionUVE(inst.rotation)) {
            inst.rotation = Math::QuaternionUVE{};
        }
        if (!IsFinite3DObjectVectorUVE(inst.scale) || inst.scale.x <= 0.0F ||
            inst.scale.y <= 0.0F || inst.scale.z <= 0.0F) {
            inst.scale = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
        }
        if (!std::isfinite(inst.customColor.r) || !std::isfinite(inst.customColor.g) ||
            !std::isfinite(inst.customColor.b)) {
            inst.customColor = Math::ColorUVE{1.0F, 1.0F, 1.0F};
        }
    }
    return out;
}

bool IsMultiMeshInstance3DObjectDefinitionValidUVE(
    const MultiMeshInstance3DObjectDefinitionUVE& value) noexcept {
    return IsMultiMeshInstance3DComponentValidUVE(value.multiMesh);
}

void ApplyMultiMeshInstance3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const MultiMeshInstance3DObjectDefinitionUVE& value) {
    ApplySurfaceInstance3DBaseUVE(entityManager, entity,
                                  MultiMeshInstance3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<MultiMeshInstance3DComponentUVE>(
        entity, SanitizeMultiMeshInstance3DComponentUVE(value.multiMesh));
}

std::size_t MultiMeshInstance3DUVE::ResolveActiveInstanceCountUVE(
    const MultiMeshInstance3DComponentUVE& multiMesh) noexcept {
    if (!multiMesh.enabled || multiMesh.instances.empty()) {
        return 0U;
    }
    if (multiMesh.visibleInstanceCount < 0) {
        return multiMesh.instances.size();
    }
    return std::min(static_cast<std::size_t>(multiMesh.visibleInstanceCount),
                    multiMesh.instances.size());
}

Math::Matrix4x4UVE MultiMeshInstance3DUVE::BuildInstanceWorldMatrixUVE(
    const MultiMeshInstance3DComponentUVE& multiMesh, const std::size_t index,
    const WorldTransformComponentUVE& ownerWorldTransform) noexcept {
    if (index >= multiMesh.instances.size()) {
        return BuildWorldTransformMatrixUVE(ownerWorldTransform);
    }
    const MultiMeshInstanceTransform3DUVE& inst = multiMesh.instances[index];
    TransformComponentUVE local{};
    local.localPosition = inst.position;
    local.localRotation = inst.rotation;
    local.localScale = inst.scale;
    const WorldTransformComponentUVE composed =
        ComposeWorldTransformUVE(ownerWorldTransform, local);
    return BuildWorldTransformMatrixUVE(composed);
}

Math::AabbUVE MultiMeshInstance3DUVE::ComputeCombinedWorldBoundsUVE(
    const MultiMeshInstance3DComponentUVE& multiMesh,
    const WorldTransformComponentUVE& ownerWorldTransform,
    const Math::Vector3UVE& instanceLocalHalfExtents) noexcept {
    const std::size_t count = ResolveActiveInstanceCountUVE(multiMesh);
    const Math::Vector3UVE safeExtents =
        (IsFinite3DObjectVectorUVE(instanceLocalHalfExtents) && instanceLocalHalfExtents.x > 0.0F &&
         instanceLocalHalfExtents.y > 0.0F && instanceLocalHalfExtents.z > 0.0F)
            ? instanceLocalHalfExtents
            : Math::Vector3UVE{0.5F, 0.5F, 0.5F};
    const Math::AabbUVE unitBox =
        Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, safeExtents);
    if (count == 0U) {
        return unitBox.TransformUVE(BuildWorldTransformMatrixUVE(ownerWorldTransform));
    }
    Math::AabbUVE combined =
        unitBox.TransformUVE(BuildInstanceWorldMatrixUVE(multiMesh, 0U, ownerWorldTransform));
    for (std::size_t i = 1U; i < count; ++i) {
        const Math::AabbUVE box =
            unitBox.TransformUVE(BuildInstanceWorldMatrixUVE(multiMesh, i, ownerWorldTransform));
        combined.min = Math::Vector3UVE{std::min(combined.min.x, box.min.x),
                                        std::min(combined.min.y, box.min.y),
                                        std::min(combined.min.z, box.min.z)};
        combined.max = Math::Vector3UVE{std::max(combined.max.x, box.max.x),
                                        std::max(combined.max.y, box.max.y),
                                        std::max(combined.max.z, box.max.z)};
    }
    return combined;
}

} // namespace UVE::Scene
