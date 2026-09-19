// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <vector>

#include "uve/asset/asset_handle_uve.h"
#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/i_asset_manager_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/render_systems/mesh_render_eligibility_uve.h"
#include "uve/render_systems/render_queue_uve.h"

namespace UVE::Render {

/// One renderable entity, resolved and placed, with no frustum yet applied.
///
/// The asset handles are held rather than re-resolved per frustum: resolving them is two hash
/// lookups plus the ready/failed checks, and neither answer can change between the cascades and
/// the main view of a single frame.
struct MeshVisibilityCandidateUVE final {
    Asset::AssetHandleUVE<Asset::MeshAssetUVE> meshHandle;
    Asset::AssetHandleUVE<Asset::MaterialAssetUVE> materialHandle;
    MeshRenderPlacementUVE placement;
    bool isTransparent = false;
};

/// This frame's renderable set, built once and then culled against as many frusta as the frame
/// needs.
///
/// THE POINT OF THIS TYPE. Extraction used to run in full once per frustum, and a frame culls
/// against four of them - three shadow cascades plus the main view. Every one of those passes
/// re-resolved the same asset handles, re-normalized the same quaternions, re-composed the same
/// TRS matrices and re-transformed the same bounding boxes, to reach four different plane tests.
/// Only the plane test actually differed. Building the candidate list once turns the other three
/// passes into plane tests over a flat vector.
///
/// The frame counters live here rather than on each queue because they describe the SCENE, not a
/// view of it: an asset that failed to load failed once, and reporting it four times would make a
/// single broken material look like four.
///
/// Caller-owned, exactly like RenderQueueUVE, so MeshRendererUVE stays stateless as its contract
/// promises and the storage can be reused across frames instead of reallocated.
struct MeshVisibilitySetUVE final {
    std::vector<MeshVisibilityCandidateUVE> candidates;

    std::size_t invalidAssetReferences = 0U;
    std::size_t pendingAssetLoads = 0U;
    std::size_t failedAssetLoads = 0U;
    std::size_t invalidRenderEligibility = 0U;

    void ClearUVE() noexcept;
};

} // namespace UVE::Render
