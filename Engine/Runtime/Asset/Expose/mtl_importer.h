// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "i_asset_importer.h"

namespace UVE::Asset {

/// Registers the bounded raw MTL source importer. The importer reads a caller-owned filesystem
/// path, converts one supported material block through ConvertMtlMaterial, persists one validated
/// MaterialAsset as a .uvmat destination, and leaves AssetImporter responsible for database
/// registration. It owns no texture/shader resolution, VFS mounts, GPU resources, background work,
/// or asset-manager state.
void RegisterMtlImporter(IAssetImporter& importer);

} // namespace UVE::Asset
