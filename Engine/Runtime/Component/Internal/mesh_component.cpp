// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "mesh_component.h"

namespace UVE::Scene {

[[nodiscard]] bool IsMeshComponentValid(const MeshComponent& component) noexcept {
    // A mesh without a material is drawn with the built-in lit shader; a material without a mesh
    // has nothing to draw on.
    return component.meshGuid != Asset::kInvalidAssetGuid ||
           component.materialGuid == Asset::kInvalidAssetGuid;
}

[[nodiscard]] bool HasAssignedMeshAsset(const MeshComponent& component) noexcept {
    return component.meshGuid != Asset::kInvalidAssetGuid;
}

[[nodiscard]] bool HasAssignedMaterialAsset(const MeshComponent& component) noexcept {
    return component.materialGuid != Asset::kInvalidAssetGuid;
}

[[nodiscard]] bool IsMeshVisibleOnLayerMask(const MeshComponent& component,
                                               const std::uint32_t layerMask) noexcept {
    return (component.visibilityLayers & layerMask) != 0U;
}

[[nodiscard]] bool IsMeshOnVisibilityLayer(const MeshComponent& component,
                                              const std::uint32_t layerIndex) noexcept {
    if (layerIndex > kMaximumMeshVisibilityLayerIndex) {
        return false;
    }
    return (component.visibilityLayers & (1U << layerIndex)) != 0U;
}

void SetMeshVisibilityLayerBit(MeshComponent& component, const std::uint32_t layerIndex,
                                  const bool enabled) noexcept {
    if (layerIndex > kMaximumMeshVisibilityLayerIndex) {
        return;
    }
    const std::uint32_t bit = (1U << layerIndex);
    if (enabled) {
        component.visibilityLayers |= bit;
    } else {
        component.visibilityLayers &= ~bit;
    }
}

[[nodiscard]] MeshComponent SanitizeMeshComponent(
    const MeshComponent& component) noexcept {
    MeshComponent out = component;
    if (!HasAssignedMeshAsset(out)) {
        out.materialGuid = Asset::kInvalidAssetGuid;
    }
    return out;
}

} // namespace UVE::Scene
