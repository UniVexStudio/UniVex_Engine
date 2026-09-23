// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/mesh_component_uve.h"


namespace UVE::Scene {

[[nodiscard]] bool IsMeshComponentValidUVE(const MeshComponentUVE& component) noexcept {
    // A mesh without a material is drawn with the built-in lit shader; a material without a mesh
    // has nothing to draw on.
    return component.meshGuid != Asset::kInvalidAssetGuidUVE || component.materialGuid == Asset::kInvalidAssetGuidUVE;
}

} // namespace UVE::Scene
