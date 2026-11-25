// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "i_asset_importer.h"

namespace UVE::Asset {

/// Registers the bounded raw PNG source importer. The importer reads a caller-owned filesystem path,
/// decodes supported PNG forms through DecodePngRgba8Image, persists one validated RGBA8
/// TextureAsset as a .uvtex destination, and leaves AssetImporter responsible for database
/// registration. It owns no VFS mounts, GPU resources, background work, or asset-manager state.
void RegisterPngImporter(IAssetImporter& importer);

} // namespace UVE::Asset
