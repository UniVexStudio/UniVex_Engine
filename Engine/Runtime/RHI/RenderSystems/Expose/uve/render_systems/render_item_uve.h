// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>

#include "uve/asset/asset_handle_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/math/matrix4x4_uve.h"

namespace UVE::Render {

/// One culled, visible draw item extracted by MeshRendererUVE::ExtractRenderQueueUVE (Part 7.2,
/// Increment 13). Holds live AssetHandleUVE<T> references (not raw AssetGuidUVE values) so the
/// underlying MeshAssetUVE/MaterialAssetUVE stay loaded for as long as the RenderQueueUVE holding
/// this item is held by the caller — otherwise AssetManagerUVE::CollectGarbageUVE() (called once
/// per frame from EngineCoreUVE::Update(), before Render()) could unload an asset between
/// extraction and consumption purely because its last other reference dropped, forcing a
/// needless reload next frame. Genuine cross-frame GPU-resource caching is Increment 14's
/// GpuResourceCacheUVE, not this struct's job.
/// Thread-safety: value type; safe to copy/move (AssetHandleUVE<T> is itself safe to copy/move
/// from any thread), but not to share for concurrent mutation.
struct RenderItemUVE {
    Math::Matrix4x4UVE worldMatrix;
    Asset::AssetHandleUVE<Asset::MeshAssetUVE> meshHandle;
    Asset::AssetHandleUVE<Asset::MaterialAssetUVE> materialHandle;
    float sortDepth = 0.0F;
    /// RenderInstance layers this item is on; 1 when the entity has no RenderInstance.
    std::uint32_t renderLayers = 1U;
    /// SurfaceInstance opacity: 1 solid, 0 invisible. 1 when the entity has no surface.
    float opacity = 1.0F;
    /// Second colour pass (SurfaceInstance overlay). Drawn blended, never a shadow caster.
    bool overlay = false;
};

} // namespace UVE::Render
