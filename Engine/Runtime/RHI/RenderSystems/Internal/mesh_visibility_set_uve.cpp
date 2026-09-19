// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/mesh_visibility_set_uve.h"

namespace UVE::Render {

void MeshVisibilitySetUVE::ClearUVE() noexcept {
    // clear(), not a fresh vector: the whole reason this is caller-owned is so a frame reuses last
    // frame's capacity instead of reallocating a scene-sized buffer every frame.
    candidates.clear();
    invalidAssetReferences = 0U;
    pendingAssetLoads = 0U;
    failedAssetLoads = 0U;
    invalidRenderEligibility = 0U;
}

} // namespace UVE::Render
