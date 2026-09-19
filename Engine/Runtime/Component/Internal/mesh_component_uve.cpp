// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/mesh_component_uve.h"


namespace UVE::Scene {

[[nodiscard]] bool IsMeshComponentValidUVE(const MeshComponentUVE& component) noexcept {
    const bool meshUnassigned = component.meshGuid == Asset::kInvalidAssetGuidUVE;
    const bool materialUnassigned = component.materialGuid == Asset::kInvalidAssetGuidUVE;
    return meshUnassigned == materialUnassigned;
}

} // namespace UVE::Scene
