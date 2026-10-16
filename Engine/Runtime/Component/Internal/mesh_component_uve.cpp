// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/mesh_component_uve.h"

namespace UVE::Scene {

[[nodiscard]] bool IsMeshComponentValidUVE(const MeshComponentUVE& component) noexcept {
    // A mesh without a material is drawn with the built-in lit shader; a material without a mesh
    // has nothing to draw on.
    return component.meshGuid != Asset::kInvalidAssetGuidUVE ||
           component.materialGuid == Asset::kInvalidAssetGuidUVE;
}

[[nodiscard]] bool HasAssignedMeshAssetUVE(const MeshComponentUVE& component) noexcept {
    return component.meshGuid != Asset::kInvalidAssetGuidUVE;
}

[[nodiscard]] bool HasAssignedMaterialAssetUVE(const MeshComponentUVE& component) noexcept {
    return component.materialGuid != Asset::kInvalidAssetGuidUVE;
}

[[nodiscard]] bool IsMeshVisibleOnLayerMaskUVE(const MeshComponentUVE& component,
                                               const std::uint32_t layerMask) noexcept {
    return (component.visibilityLayers & layerMask) != 0U;
}

[[nodiscard]] bool IsMeshOnVisibilityLayerUVE(const MeshComponentUVE& component,
                                              const std::uint32_t layerIndex) noexcept {
    if (layerIndex > kMaximumMeshVisibilityLayerIndexUVE) {
        return false;
    }
    return (component.visibilityLayers & (1U << layerIndex)) != 0U;
}

void SetMeshVisibilityLayerBitUVE(MeshComponentUVE& component, const std::uint32_t layerIndex,
                                  const bool enabled) noexcept {
    if (layerIndex > kMaximumMeshVisibilityLayerIndexUVE) {
        return;
    }
    const std::uint32_t bit = (1U << layerIndex);
    if (enabled) {
        component.visibilityLayers |= bit;
    } else {
        component.visibilityLayers &= ~bit;
    }
}

[[nodiscard]] MeshComponentUVE SanitizeMeshComponentUVE(
    const MeshComponentUVE& component) noexcept {
    MeshComponentUVE out = component;
    if (!HasAssignedMeshAssetUVE(out)) {
        out.materialGuid = Asset::kInvalidAssetGuidUVE;
    }
    return out;
}

} // namespace UVE::Scene
